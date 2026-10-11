// Recovery.cpp - see Recovery.h
#include "Recovery.h"
#include "ResumeScan.h"

#include "Job.h"
#include "InputFile.h"
#include "FileStream.h"
#include "FluidPath.h"
#include "Macro.h"
#include "Planner.h"
#include "State.h"
#include "Alarm.h"
#include "Limit.h"
#include "Serial.h"
#include "Logging.h"
#include "Machine/MachineConfig.h"
#include "Machine/Homing.h"
#include "Driver/watchdog.h"

#include <Arduino.h>  // millis
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace {
    enum class St { None, Running, Interrupted, Scanning, Resuming };

    struct Record {
        St          st     = St::None;
        std::string path;           // /sd/...
        uint32_t    line   = 0;     // last line seen executing
        std::string reason;         // why it stopped
        uint32_t    retract = 0;    // suggested restart lines
        uint32_t    tool    = 0;
    };
    Record                _r;
    Channel*              _file = nullptr;           // the job's file channel while it runs
    std::atomic<Channel*> _eof { nullptr };          // set when a job source reached its end
    uint32_t              _savedLine  = 0;
    uint32_t              _lastSaveMs = 0;
    bool                  _holdSaved  = false;

    // Scanner (restart points / resume) - advanced from recov_poll()
    struct Scan {
        bool       active  = false;
        bool       resume  = false;  // false: restart points only
        bool       dry     = false;
        uint32_t   target  = 0;      // resume: line to continue from; points: stop line
        InputFile* f       = nullptr;
        ResumeScan rs;
    };
    Scan  _scan;
    Macro _resumeMacro { "resume" };

    const char* REC_FILE = "recovery.txt";  // on the local filesystem

    const char* st_name(St s) {
        switch (s) {
            case St::Interrupted: return "interrupted";
            case St::Scanning: return "scanning";
            case St::Resuming: return "resuming";
            case St::Running: return "running";
            default: return "none";
        }
    }

    void report() {
        St s = _r.st == St::Running ? St::None : _r.st;  // a running job is not "recovery"
        std::string reason = _r.reason;
        std::replace(reason.begin(), reason.end(), ':', ' ');
        char head[96];
        snprintf(head, sizeof(head), "[MSG:Recovery:%s:%u:%u:%u:", st_name(s), (unsigned)_r.line, (unsigned)_r.retract,
                 (unsigned)_r.tool);
        std::string m = head + reason + ":" + (s == St::None ? std::string() : _r.path) + "]\r\n";
        allChannels.print(m.c_str());
    }
    void report_err(const char* text) {
        std::string m = std::string("[MSG:RecoveryErr:") + text + "]\r\n";
        allChannels.print(m.c_str());
    }

    void save() {
        _savedLine  = _r.line;
        _lastSaveMs = millis();
        try {
            FileStream f(REC_FILE, "w", LocalFS);
            St   s = (_r.st == St::Scanning || _r.st == St::Resuming) ? St::Interrupted : _r.st;
            char buf[400];
            int  n = snprintf(buf, sizeof(buf), "REC1\n%s\n%u\n%s\n%s\n", st_name(s), (unsigned)_r.line, _r.reason.c_str(),
                             _r.path.c_str());
            f.write((const uint8_t*)buf, std::min(n, (int)sizeof(buf) - 1));
        } catch (...) {}
    }

    void load() {
        try {
            FileStream f(REC_FILE, "r", LocalFS);
            char       buf[400] = {};
            int        n        = f.read(buf, sizeof(buf) - 1);
            if (n <= 0) return;
            buf[n] = '\0';
            char* save;
            char* tag   = strtok_r(buf, "\n", &save);
            char* st    = strtok_r(nullptr, "\n", &save);
            char* line  = strtok_r(nullptr, "\n", &save);
            char* why   = strtok_r(nullptr, "\n", &save);
            char* path  = strtok_r(nullptr, "\n", &save);
            if (!tag || strcmp(tag, "REC1") || !st || !line || !why || !path) return;
            if (!strcmp(st, "running")) {
                // The controller restarted while the job ran: power loss or a crash.
                _r.st     = St::Interrupted;
                _r.reason = "power off / restart";
            } else if (!strcmp(st, "interrupted")) {
                _r.st     = St::Interrupted;
                _r.reason = why;
            } else {
                return;
            }
            _r.line = strtoul(line, nullptr, 10);
            _r.path = path;
        } catch (...) {}
    }

    void scan_stop() {
        delete _scan.f;
        _scan = Scan();
    }

    bool scan_start(bool resume, uint32_t target, bool dry) {
        scan_stop();
        try {
            _scan.f = new InputFile(SD, _r.path.c_str());
        } catch (...) {
            _scan.f = nullptr;
        }
        if (!_scan.f) {
            report_err("cannot open the job file");
            return false;
        }
        _scan.active = true;
        _scan.resume = resume;
        _scan.dry    = dry;
        _scan.target = target;
        return true;
    }

    void addf(std::string& g, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void addf(std::string& g, const char* fmt, ...) {
        char    b[96];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(b, sizeof(b), fmt, ap);
        va_end(ap);
        g += b;
        g += '\n';
    }

    // Commands that put the machine where the file had it just before line
    // `target`, then (unless dry) start spindle / coolant and lower the tool.
    std::string preamble(const ResumeScan& s, bool dry) {
        std::string g;
        float safeZ = limitsMaxPosition(Z_AXIS) - 0.5f;
        addf(g, "G21");  // positions below are in mm; the file's units are restored at the end
        addf(g, "G90");
        addf(g, "%s", s.wcs.c_str());
        addf(g, "G%d", s.plane);
        addf(g, "M5");
        addf(g, "M9");
        addf(g, "G53 G0 Z%.3f", safeZ);  // lift first - never turn on or move sideways in the part
        if (s.known[0] && s.known[1]) {
            if (s.known[3] && config->_axes->_numberAxis > 3) addf(g, "G0 X%.4f Y%.4f A%.4f", s.pos[0], s.pos[1], s.pos[3]);
            else addf(g, "G0 X%.4f Y%.4f", s.pos[0], s.pos[1]);
        }
        if (dry) return g;
        if ((s.spindle == 3 || s.spindle == 4) && s.speed > 0) {
            addf(g, "M%d S%.0f", s.spindle, s.speed);
            addf(g, "G4 P2");  // spin up
        }
        if (s.mist) addf(g, "M7");
        if (s.flood) addf(g, "M8");
        float feed_mm = s.inch ? s.feed * 25.4f : s.feed;
        // Lower only over the restart point; if X/Y are not known the file's
        // next move starts from safe height.
        if (s.known[0] && s.known[1] && s.known[2]) {
            float plunge = feed_mm > 0 ? std::min(feed_mm, 500.0f) : 300.0f;
            addf(g, "G0 Z%.4f", s.pos[2] + 3.0f);
            addf(g, "G1 Z%.4f F%.1f", s.pos[2], plunge);
        }
        // The file's next lines may rely on the modal motion: set it with a
        // zero-length move (relative X0).
        if (s.motion == 1) {
            if (feed_mm > 0) addf(g, "G91 G1 X0 F%.1f", feed_mm);
            else addf(g, "G91 G1 X0 F300");  // G1 needs a feed
        }
        else if (s.motion == 0) addf(g, "G91 G0 X0");
        addf(g, "%s", s.absolute ? "G90" : "G91");
        if (s.inch) {
            addf(g, "G20");
            if (s.feed > 0) addf(g, "F%.3f", s.feed);
        } else if (s.feed > 0) {
            addf(g, "F%.1f", s.feed);
        }
        return g;
    }

    void scan_finish() {
        ResumeScan& s = _scan.rs;
        if (!_scan.resume) {
            // Restart points before the stop line; "the line after" is where to continue.
            _r.retract = (s.lastRetract && s.lastRetract + 1 < _r.line) ? s.lastRetract + 1 : 0;
            _r.tool    = (s.lastToolLine && s.lastToolLine + 1 < _r.line) ? s.lastToolLine + 1 : 0;
            _r.st      = St::Interrupted;
            scan_stop();
            report();
            return;
        }
        if (!state_is(State::Idle)) {
            report_err("machine is not Idle");
            _r.st = St::Interrupted;
            scan_stop();
            report();
            return;
        }
        std::string g = preamble(s, _scan.dry);
        if (_scan.dry) {
            scan_stop();
            _r.st = St::Interrupted;
            _resumeMacro.set(g);
            _resumeMacro.run(nullptr);
            allChannels.print("[MSG:RecoveryInfo:dry run - moving to the start point at safe height]\r\n");
            report();
            return;
        }
        if ((s.motion == 2 || s.motion == 3)) {
            allChannels.print("[MSG:RecoveryInfo:the line continues an arc - check the first move]\r\n");
        }
        InputFile*  f    = _scan.f;
        std::string path = _r.path;
        uint32_t    line = _scan.target;
        _scan.f          = nullptr;
        scan_stop();
        // The file continues at `line`; the preamble runs first (on top of it).
        recov_job_start(f, path);
        _r.line = line;
        Job::nest(f, nullptr, nullptr);
        _resumeMacro.set(g);
        _resumeMacro.run(nullptr);
        char m[128];
        snprintf(m, sizeof(m), "[MSG:RecoveryInfo:continuing from line %u]\r\n", (unsigned)line);
        allChannels.print(m);
        report();  // recovery is over: "none" (the job runs)
    }

    // Read lines for up to ~15 ms.
    void scan_step() {
        if (!_scan.active) return;
        const uint32_t until = _scan.resume ? _scan.target - 1 : _scan.target;  // lines to read
        const uint32_t t0    = millis();
        char           buf[256];
        while (_scan.rs.line < until) {
            Error e = _scan.f->readLine(buf, sizeof(buf) - 1);
            if (e == Error::Eof) break;
            if (e != Error::Ok) {
                report_err("cannot read the job file");
                _r.st = St::Interrupted;
                scan_stop();
                report();
                return;
            }
            _scan.rs.feed_line(buf);
            if ((_scan.rs.line & 255) == 0) {
                feed_watchdog();
                if (millis() - t0 > 15) return;
            }
        }
        if (_scan.resume && _scan.rs.line < until) {
            report_err("the file has fewer lines than that");
            _r.st = St::Interrupted;
            scan_stop();
            report();
            return;
        }
        scan_finish();
    }
}

void recov_init() {
    load();
    if (_r.st == St::Interrupted) {
        save();  // "running" -> "interrupted"
        report();
    }
}

void recov_job_start(Channel* file, const std::string& path) {
    if (Job::active()) return;  // a file run from inside a job: the outer job is tracked
    _r          = Record();
    _r.st       = St::Running;
    _r.path     = path;
    _file       = file;
    _eof        = nullptr;
    _holdSaved  = false;
    save();
}

void recov_job_eof(Channel* ch) {
    _eof = ch;
}

void recov_poll() {
    if (_r.st == St::Running) {
        if (Job::active()) {
            if (Job::channel() == _file) {
                plan_block_t* b = plan_get_current_block();
                if (b && b->line_number > 0) _r.line = b->line_number;
            }
            bool hold = state_is(State::Hold);
            if (hold && !_holdSaved) {
                _holdSaved = true;
                save();
            } else if (!hold) {
                _holdSaved = false;
            }
            if (_r.line != _savedLine && millis() - _lastSaveMs > 5000) save();
        } else if (_eof == _file) {
            // Reached the end of the file: finished, nothing to recover.
            _r    = Record();
            _file = nullptr;
            save();
        } else {
            _r.st     = St::Interrupted;
            _r.reason = state_is(State::Alarm) ? std::string("alarm ") + std::to_string((int)lastAlarm) : "stopped";
            _file     = nullptr;
            save();
            report();
        }
        return;
    }
    if (_r.st == St::Scanning || _r.st == St::Resuming) scan_step();
}

bool recov_command(const char* line) {
    if (strncasecmp(line, "$Job/", 5) != 0) return false;
    const char* cmd = line + 5;
    if (strcasecmp(cmd, "Recovery") == 0) {
        report();
        return true;
    }
    if (strcasecmp(cmd, "Discard") == 0) {
        scan_stop();
        _r = Record();
        save();
        report();
        return true;
    }
    if (strcasecmp(cmd, "Points") == 0) {
        if (_r.st != St::Interrupted) { report(); return true; }
        if (scan_start(false, _r.line, false)) {
            _r.st = St::Scanning;
            report();
        }
        return true;
    }
    if (strncasecmp(cmd, "Resume=", 7) == 0) {
        if (_r.st != St::Interrupted) { report_err("no interrupted job"); return true; }
        if (!state_is(State::Idle)) { report_err("machine must be Idle - unlock or home first"); return true; }
        if (Machine::Homing::unhomed_axes()) { report_err("home the machine first"); return true; }
        char*    end;
        uint32_t ln  = strtoul(cmd + 7, &end, 10);
        bool     dry = *end == ':' && strncasecmp(end + 1, "dry", 3) == 0;
        if (ln < 1) { report_err("bad line number"); return true; }
        if (scan_start(true, ln, dry)) {
            _r.st = St::Resuming;
            report();
        }
        return true;
    }
    return false;
}
