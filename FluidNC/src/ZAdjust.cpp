// ZAdjust.cpp - see ZAdjust.h
#include "ZAdjust.h"

#include "Job.h"
#include "Planner.h"
#include "State.h"
#include "Serial.h"
#include "GCode.h"
#include "Settings.h"
#include "FileStream.h"
#include "FluidPath.h"
#include "Machine/MachineConfig.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <strings.h>

namespace {
    // Written by the command handler (polling task), read by the parser
    // (protocol task): single 32-bit values.
    volatile float    _req      = 0.0f;   // wanted offset
    volatile float    _applied  = 0.0f;   // offset in the parser position
    volatile uint32_t _pendLine = 0;      // file line the last change starts at
    volatile bool     _pending  = false;  // that line is not executing yet
    volatile bool     _report   = false;  // something to tell the channels
    volatile float    _shown    = 0.0f;   // in the moves executing now (WCO)

    float      _limit     = 1.0f;
    bool       _wasActive = false;
    bool       _hasEnded  = false;  // offer to add the last job's offset to the work zero
    float      _ended     = 0.0f;
    CoordIndex _endedWcs  = CoordIndex::G54;

    const char*   LIMIT_FILE = "zadjust.txt";  // local filesystem
    constexpr float MAX_STEP = 0.5f;           // per command

    const char* wcs_name(CoordIndex c) {
        static const char* n[] = { "G54", "G55", "G56", "G57", "G58", "G59" };
        int i = int(c) - int(CoordIndex::G54);
        return (i >= 0 && i < 6) ? n[i] : "WCS";
    }

    void print_all(const char* m) { allChannels.print(m); }

    void report() {
        char        m[96];
        const char* st;
        float       v    = _req;
        unsigned    line = 0;
        if (!Job::active() && _hasEnded) {
            st = "ended";
            v  = _ended;
        } else if (_req == 0.0f && _applied == 0.0f) {
            st = "none";
        } else if (_req != _applied) {
            st = "pending";  // waiting for the next move to be read
        } else if (_pending) {
            st   = "pending";
            line = _pendLine;  // applies when the machine gets to this line
        } else {
            st = "active";
        }
        snprintf(m, sizeof(m), "[MSG:ZAdjust:%+.3f:%s:%u]\r\n", v, st, line);
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
    try {
        FileStream f(LIMIT_FILE, "r", LocalFS);
        char       b[24] = {};
        if (f.read(b, sizeof(b) - 1) > 0) {
            float v = strtof(b, nullptr);
            if (v >= 0.05f && v <= 5.0f) _limit = v;
        }
    } catch (...) {}
}

// ── Parser hooks (protocol task) ─────────────────────────────────────────────
float zadj_applied() { return _applied; }
float zadj_shown() { return _shown; }

float zadj_target(bool canApply) { return canApply ? (float)_req : (float)_applied; }

void zadj_moved(float applied) {
    if (applied == _applied) return;
    Channel* jc = Job::channel();
    _applied    = applied;
    _pendLine   = jc ? jc->lineNumber() : 0;
    _pending    = _pendLine != 0;
    _report     = true;
}

void zadj_reset_position() { _applied = 0.0f; }

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
        _wasActive = false;
        if (_applied != 0.0f) {
            _ended    = _applied;
            _endedWcs = gc_state.modal.coord_select;
            _hasEnded = true;
        }
        if (_req != 0.0f || _applied != 0.0f || _hasEnded) _report = true;
        _req     = 0.0f;
        _applied = 0.0f;
        _pending = false;
        if (_shown != 0.0f) {
            _shown = 0.0f;
            allChannels.notifyWco();
        }
    }
    if (_pending) {
        plan_block_t* b = plan_get_current_block();
        if (!b || b->line_number >= (int32_t)_pendLine) {
            _pending = false;
            _report  = true;
            _shown   = _applied;
            allChannels.notifyWco();  // WPos keeps showing the file's Z
        }
    }
    if (_report) {
        _report = false;
        report();
    }
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
        _req = want;
        report();  // "pending" until the parser reaches a move
    }
    return true;
}
