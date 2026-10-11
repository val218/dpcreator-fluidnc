// ZAdjust.cpp - see ZAdjust.h
#include "ZAdjust.h"

#include "Job.h"
#include "Planner.h"
#include "Protocol.h"
#include "System.h"
#include "State.h"
#include "Serial.h"
#include "GCode.h"
#include "Settings.h"
#include "FileStream.h"
#include "FluidPath.h"
#include "Machine/MachineConfig.h"
#include "Driver/delay_usecs.h"  // usToCpuTicks

#include <Arduino.h>  // millis
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <strings.h>

// Stepper interrupt interface (see ZAdjust.h). target is written only here,
// done only by the interrupt, so neither needs a lock.
volatile int32_t zadj_isr_target   = 0;
volatile int32_t zadj_isr_done     = 0;
int32_t          zadj_isr_interval = 0;

namespace {
    volatile float _req       = 0.0f;   // wanted offset, mm
    volatile bool  _report    = false;  // something to tell the channels
    volatile bool  _resync    = false;  // job over: protocol task takes the real position over
    float          _spm       = 0.0f;   // Z steps per mm
    int32_t        _lastDone  = 0;      // for WCO / pending -> active reports
    uint32_t       _lastWcoMs = 0;

    float      _limit     = 1.0f;
    bool       _wasActive = false;
    bool       _hasEnded  = false;  // offer to add the last job's offset to the work zero
    float      _ended     = 0.0f;
    CoordIndex _endedWcs  = CoordIndex::G54;

    const char*     LIMIT_FILE = "zadjust.txt";  // local filesystem
    constexpr float MAX_STEP   = 0.5f;           // per command

    float spm() {
        if (_spm <= 0.0f && config && config->_axes && Axes::_numberAxis > Z_AXIS && Axes::_axis[Z_AXIS]) {
            _spm = Axes::_axis[Z_AXIS]->_stepsPerMm;
        }
        return _spm > 0.0f ? _spm : 1.0f;
    }
    float done_mm() { return zadj_isr_done / spm(); }

    const char* wcs_name(CoordIndex c) {
        static const char* n[] = { "G54", "G55", "G56", "G57", "G58", "G59" };
        int i = int(c) - int(CoordIndex::G54);
        return (i >= 0 && i < 6) ? n[i] : "WCS";
    }

    void print_all(const char* m) { allChannels.print(m); }

    void report() {
        char        m[96];
        const char* st;
        float       v = _req;
        if (!Job::active() && _hasEnded) {
            st = "ended";
            v  = _ended;
        } else if (_req == 0.0f && zadj_isr_done == 0 && zadj_isr_target == 0) {
            st = "none";
        } else if (zadj_isr_done != zadj_isr_target) {
            st = "pending";  // the tool is moving to it (or waits for Resume)
        } else {
            st = "active";
        }
        snprintf(m, sizeof(m), "[MSG:ZAdjust:%+.3f:%s:0]\r\n", v, st);
        print_all(m);
    }
    void report_err(const char* t) {
        char m[96];
        snprintf(m, sizeof(m), "[MSG:ZAdjustErr:%s]\r\n", t);
        print_all(m);
    }

    void save_limit() {
        try {
            FileStream f(LIMIT_FILE, "w", LocalFS);
            char       b[24];
            int        n = snprintf(b, sizeof(b), "%.3f\n", _limit);
            f.write((const uint8_t*)b, n);
        } catch (...) {}
    }
}

void zadj_init() {
    zadj_isr_interval = usToCpuTicks(1000);  // at most 1000 added steps per second
    try {
        FileStream f(LIMIT_FILE, "r", LocalFS);
        char       b[24] = {};
        if (f.read(b, sizeof(b) - 1) > 0) {
            float v = strtof(b, nullptr);
            if (v >= 0.05f && v <= 5.0f) _limit = v;
        }
    } catch (...) {}
}

float zadj_shown() { return zadj_isr_done / spm(); }

// ── Polling task ─────────────────────────────────────────────────────────────
void zadj_poll() {
    bool active = Job::active();
    if (active && !_wasActive) {
        _wasActive = true;
        if (_hasEnded) {  // a new job: the last one's offer is gone
            _hasEnded = false;
            _report   = true;
        }
    } else if (!active && _wasActive) {
        _wasActive      = false;
        zadj_isr_target = zadj_isr_done;  // no more added steps
        float made      = done_mm();
        if (zadj_isr_done != 0) {
            _ended    = std::round(made * 1000.0f) / 1000.0f;
            _endedWcs = gc_state.modal.coord_select;
            _hasEnded = _ended != 0.0f;
            _resync   = true;  // the protocol task takes the real position over
        }
        if (_req != 0.0f || _hasEnded) _report = true;
        _req = 0.0f;
    }
    // Tool position changed: WCO follows (so WPos keeps the file's Z), and
    // "pending" becomes "active" once all steps are out.
    int32_t done = zadj_isr_done;
    if (done != _lastDone) {
        bool settled = (done == zadj_isr_target);
        if (settled || millis() - _lastWcoMs > 150) {
            _lastDone  = done;
            _lastWcoMs = millis();
            allChannels.notifyWco();
            if (settled && active) _report = true;
        }
    }
    if (_report) {
        _report = false;
        report();
    }
}

// ── Protocol task: after the job ─────────────────────────────────────────────
void zadj_protocol_poll() {
    if (!_resync || Job::active()) return;
    // The tool is zadj_isr_done steps away from where the planner and the
    // parser think: let the last moves finish, then take the real position
    // over, so the next move goes where it is told and nothing moves back.
    protocol_buffer_synchronize();
    if (!sys.abort()) {
        plan_sync_position();
        gc_sync_position();
    }
    _resync         = false;
    zadj_isr_target = 0;
    zadj_isr_done   = 0;  // the interrupt is idle (planner empty)
    _lastDone       = 0;
    allChannels.notifyWco();
}

// ── Commands ─────────────────────────────────────────────────────────────────
bool zadj_command(const char* line, Channel& channel, bool on_protocol_task, bool& needDefer) {
    needDefer = false;
    if (strncasecmp(line, "$Job/ZAdjust", 12) != 0) return false;
    const char* rest = line + 12;

    if (*rest == '\0') {
        report();
        return true;
    }
    if (strncasecmp(rest, "/Limit", 6) == 0) {
        if (rest[6] == '=') {
            float v = strtof(rest + 7, nullptr);
            if (!(v >= 0.05f && v <= 5.0f)) {
                report_err("limit must be 0.05 .. 5 mm");
                return true;
            }
            _limit = v;
            save_limit();
        }
        char m[64];
        snprintf(m, sizeof(m), "[MSG:ZAdjustLimit:%.3f]\r\n", _limit);
        channel.print(m);
        return true;
    }
    if (strcasecmp(rest, "/Apply") == 0) {
        if (!on_protocol_task) {  // changes the work coordinates: run it with the G-code
            needDefer = true;
            return true;
        }
        if (!_hasEnded) {
            report_err("nothing to apply");
            return true;
        }
        if (!state_is(State::Idle)) {
            report_err("machine must be Idle");
            return true;
        }
        float d[MAX_N_AXIS];
        coords[_endedWcs]->get(d);
        d[Z_AXIS] += _ended;  // the job ran this much higher (+) / lower (-)
        coords[_endedWcs]->set(d);
        if (gc_state.modal.coord_select == _endedWcs) copyAxes(gc_state.coord_system, d);
        gc_wco_changed();
        gc_ngc_changed(_endedWcs);
        char m[96];
        snprintf(m, sizeof(m), "[MSG:ZAdjustInfo:%s Z zero moved %+.3f mm]\r\n", wcs_name(_endedWcs), _ended);
        print_all(m);
        _hasEnded = false;
        report();
        return true;
    }
    if (*rest != '=') {
        report_err("use $Job/ZAdjust=+0.05 / =-0.05 / =0");
        return true;
    }
    const char* v = rest + 1;
    char*       end;
    float       n = strtof(v, &end);
    if (end == v || !std::isfinite(n)) {
        report_err("bad number");
        return true;
    }
    bool  relative = (*v == '+' || *v == '-');
    float want     = relative ? _req + n : n;

    if (!Job::active()) {
        if (want == 0.0f) {  // "No" to the end-of-job offer, or nothing to clear
            _hasEnded = false;
            report();
        } else {
            report_err("no job running");
        }
        return true;
    }
    if (std::fabs(want - _req) > MAX_STEP + 1e-4f) {
        report_err("at most 0.5 mm per step");
        return true;
    }
    if (std::fabs(want) > _limit + 1e-4f) {
        char m[48];
        snprintf(m, sizeof(m), "limit is +-%.3f mm", _limit);
        report_err(m);
        want = want > 0 ? _limit : -_limit;
    }
    want = std::round(want * 1000.0f) / 1000.0f;
    if (want != _req) {
        _req            = want;
        zadj_isr_target = (int32_t)lroundf(want * spm());  // the stepper interrupt moves the tool there
        report();
    }
    return true;
}
