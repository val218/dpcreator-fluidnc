// Rewind.cpp - see Rewind.h
#include "Rewind.h"
#include "RewindPath.h"
#include "ZAdjust.h"

#include "Planner.h"
#include "Protocol.h"
#include "System.h"
#include "Stepper.h"
#include "State.h"
#include "Serial.h"
#include "GCode.h"
#include "Job.h"
#include "Machine/MachineConfig.h"
#include "Spindles/Spindle.h"

#include <Arduino.h>  // millis
#include <esp_heap_caps.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <algorithm>

namespace {
    RwRing   _ring;
    bool     _recReset = true;  // next job block clears the record

    RwPath   _path;
    bool     _active     = false;  // protocol task
    bool     _continuing = false;
    bool     _cancelling = false;
    float    _sCur = 0.0f, _zCur = 0.0f;
    steps_t  _holdSteps[MAX_N_AXIS];
    float    _hold[4] = {};
    int32_t  _lastLine = 0;
    uint32_t _lastRepMs = 0;

    // Requests from the command handler (polling task)
    volatile bool  _startReq = false, _contReq = false, _cancelReq = false;
    volatile float _sTarget = 0.0f, _zTarget = 0.0f;
    volatile float _len     = 0.0f;  // path length, for clamping requests
    volatile bool  _activePub = false;

    plan_line_data_t _pd;

    constexpr float REWIND_MAX_FEED = 1500.0f;  // mm/min while going back / forward by hand
    constexpr float RAPID_FEED      = 1000.0f;  // rapids are retraced at this feed
    constexpr float Z_FEED          = 200.0f;   // Z height changes
    constexpr float CHUNK           = 1.0f;     // mm per move while turning the wheel

    const char* state_name() {
        if (!_active) return "off";
        if (_continuing) return "continuing";
        if (_cancelling) return "returning";
        return "active";
    }

    void report(bool force = true) {
        if (!force && millis() - _lastRepMs < 150) return;
        _lastRepMs = millis();
        char m[112];
        snprintf(m, sizeof(m), "[MSG:Rewind:%s:%.3f:%+.3f:%d:%.3f]\r\n", state_name(), _active ? _sCur : 0.0f,
                 _active ? _zCur : 0.0f, (int)(_active ? _path.lineAt(_sCur) : 0), _active ? _path.length() : 0.0f);
        allChannels.print(m);
    }
    void report_err(const char* t) {
        char m[112];
        snprintf(m, sizeof(m), "[MSG:RewindErr:%s]\r\n", t);
        allChannels.print(m);
    }

    void motor_now(float* pos) {
        steps_t s[MAX_N_AXIS];
        get_steps(s);
        steps_to_motor_pos(pos, s);
    }

    // A single "system motion" (as for safety-door parking): the held job and
    // its planner stay untouched. Blocks until the move is done.
    void move_to(const float p[4], float feed) {
        if (sys.abort()) return;
        float target[MAX_N_AXIS];
        motor_now(target);
        auto n = std::min<int>(Axes::_numberAxis, 4);
        for (int i = 0; i < n; ++i) target[i] = p[i];

        plan_block_t* b = plan_get_current_block();  // the held job's block: keep its spindle / coolant
        memset(&_pd, 0, sizeof(_pd));
        _pd.feed_rate              = std::max(10.0f, feed);
        _pd.motion.systemMotion    = 1;
        _pd.motion.noFeedOverride  = 1;
        _pd.spindle                = b ? b->spindle : gc_state.modal.spindle;
        _pd.spindle_speed          = b ? b->spindle_speed : gc_state.spindle_speed;
        _pd.coolant                = b ? b->coolant : gc_state.modal.coolant;
        _pd.line_number            = 0;

        if (plan_buffer_line(target, &_pd)) {
            sys.step_control.executeSysMotion = true;
            sys.step_control.endMotion        = false;  // run it although a feed hold is active
            Stepper::parking_setup_buffer();
            Stepper::prep_buffer();
            Stepper::wake_up();
            do {
                protocol_exec_rt_system();
                if (sys.abort()) return;
            } while (sys.step_control.executeSysMotion);
            Stepper::parking_restore_buffer();
        } else {
            sys.step_control.executeSysMotion = false;  // zero length
            protocol_exec_rt_system();
        }
    }

    void stop(bool resume) {
        _active = _activePub = false;
        _continuing = _cancelling = false;
        _len = 0.0f;
        zadj_freeze(false);
        report();
        if (resume) protocol_send_event(&cycleStartEvent);  // not taken by rewind any more: resumes the job
    }

    void start() {
        if (_active) { report(); return; }
        if (!Job::active()) { report_err("no job running"); return; }
        if (!state_is(State::Hold) || !sys.suspend().bit.holdComplete) { report_err("wait until the machine stopped (Hold)"); return; }
        if (spindle->isRateAdjusted()) { report_err("not in laser mode"); return; }
        if (_ring.count() == 0) { report_err("nothing recorded for this job"); return; }

        get_steps(_holdSteps);
        float now[MAX_N_AXIS];
        steps_to_motor_pos(now, _holdSteps);
        for (int i = 0; i < 4; ++i) _hold[i] = i < (int)Axes::_numberAxis ? now[i] : 0.0f;

        // The recorded ends have no live Z adjust in them; the tool has.
        _path.build(_ring, plan_blocks_queued(), _hold, zadj_shown(), _ring.capacity() + 1);
        if (_path.length() < 0.01f) { report_err("nothing to rewind here"); return; }

        zadj_freeze(true);  // no added steps while the tool is moved here
        _sCur = 0.0f; _zCur = 0.0f;
        _sTarget = 0.0f; _zTarget = 0.0f;
        _len = _path.length();
        _continuing = _cancelling = false;
        _active = _activePub = true;
        _lastLine = 0;
        report();
    }

    void finish() {
        steps_t now[MAX_N_AXIS];
        get_steps(now);
        auto n = Axes::_numberAxis;
        for (axis_t a = X_AXIS; a < n; ++a) {
            if (a == Z_AXIS) continue;
            if (std::labs((long)(now[a] - _holdSteps[a])) > 1) {
                report_err("not back at the hold point - not resuming");
                _continuing = _cancelling = false;
                report();
                return;
            }
        }
        int32_t dz = n > Z_AXIS ? (int32_t)(now[Z_AXIS] - _holdSteps[Z_AXIS]) : 0;
        if (_continuing) {
            zadj_add_steps(dz);  // the new height stays for the rest of the job
            stop(true);
        } else {
            stop(false);  // Cancel: at the hold point, job still in Hold
        }
    }
}

// ── Boot / recording (protocol task) ─────────────────────────────────────────
void rewind_init() {
    int      cap = 8192;  // ~ 256 KB in PSRAM: many metres of short moves
    RwEntry* buf = (RwEntry*)heap_caps_malloc(sizeof(RwEntry) * cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        cap = 512;  // no PSRAM: a short history
        buf = (RwEntry*)malloc(sizeof(RwEntry) * cap);
    }
    if (buf) _ring.init(buf, cap);
}

void rewind_job_start() {
    _ring.clear();
    _recReset = false;
}

void rewind_record(const float* target, const plan_line_data_t* pl) {
    if (pl->motion.systemMotion || pl->is_jog) return;
    if (!Job::active()) {  // MDI moves are not part of a job's path
        _recReset = true;
        return;
    }
    if (_recReset) {
        _ring.clear();
        _recReset = false;
    }
    RwEntry e;
    auto    n = std::min<int>(Axes::_numberAxis, 4);
    for (int i = 0; i < 4; ++i) e.p[i] = i < n ? target[i] : 0.0f;
    e.feed    = (pl->motion.rapidMotion || pl->motion.inverseTime) ? 0.0f : pl->feed_rate;
    e.speed   = (float)pl->spindle_speed;
    e.line    = pl->line_number;
    e.spindle = (uint8_t)pl->spindle;
    _ring.push(e);
}

// ── Motion (protocol task, Hold suspend loop) ────────────────────────────────
void rewind_suspend_poll() {
    if (_startReq) {
        _startReq = false;
        start();
    }
    if (!_active) return;
    if (!Job::active() || !state_is(State::Hold)) {  // job gone (reset, alarm)
        stop(false);
        return;
    }
    if (_cancelReq) {
        _cancelReq  = false;
        _cancelling = true;
        _continuing = false;
        _zTarget    = 0.0f;
        _sTarget    = 0.0f;
        report();
    }
    if (_contReq) {
        _contReq    = false;
        _continuing = true;
        _sTarget    = 0.0f;
        report();
    }

    float zt = _zTarget;
    float st = std::max(0.0f, std::min((float)_sTarget, _path.length()));
    // Height first, where the tool is
    if (std::fabs(zt - _zCur) > 1e-4f) {
        float p[4];
        _path.at(_sCur, p);
        p[2] += zt;
        move_to(p, Z_FEED);
        _zCur = zt;
        report(false);
        return;
    }
    if (std::fabs(st - _sCur) > 1e-5f) {
        float stop = _path.nextStop(_sCur, st);  // corners: one move per path segment
        float next;
        if (_continuing || _cancelling) {
            next = stop;  // re-cut / return: whole segments
        } else {
            next = st > _sCur ? std::min(stop, _sCur + CHUNK) : std::max(stop, _sCur - CHUNK);
        }
        float mid  = (next + _sCur) * 0.5f;
        float feed = _path.feedAt(mid);
        if (feed <= 0.0f) feed = RAPID_FEED;
        if (!_continuing) feed = std::min(feed, REWIND_MAX_FEED);  // by hand: never faster than this
        float p[4];
        _path.at(next, p);
        p[2] += _zCur;
        move_to(p, feed);
        _sCur = next;
        int32_t ln = _path.lineAt(_sCur);
        report(ln != _lastLine);
        _lastLine = ln;
        return;
    }
    if ((_continuing || _cancelling) && _sCur <= 1e-5f && std::fabs(_zCur - zt) <= 1e-4f) {
        finish();
    }
}

bool rewind_cycle_start() {
    if (!_active) return false;
    _contReq = true;  // cycle start while rewound = Continue (re-cut, then resume)
    return true;
}

void rewind_reset() {
    if (_active) {
        _active = _activePub = false;
        _continuing = _cancelling = false;
        zadj_freeze(false);
    }
    _startReq = _contReq = _cancelReq = false;
}

bool rewind_active() { return _activePub; }

// ── Commands (any task) ──────────────────────────────────────────────────────
bool rewind_command(const char* line, Channel& channel) {
    if (strncasecmp(line, "$Rewind", 7) != 0) return false;
    const char* c = line + 7;
    if (*c == '\0') {
        char m[112];
        snprintf(m, sizeof(m), "[MSG:Rewind:%s:%.3f:%+.3f:0:%.3f]\r\n", _activePub ? "active" : "off", (float)_sTarget,
                 (float)_zTarget, (float)_len);
        channel.print(m);
        return true;
    }
    if (strcasecmp(c, "/Start") == 0) {
        if (!Job::active() || !state_is(State::Hold)) {
            report_err("only in Hold while a job runs");
            return true;
        }
        _startReq = true;  // taken by the Hold loop
        return true;
    }
    if (!_activePub) {
        report_err("rewind is not active - Hold, then Rewind");
        return true;
    }
    if (strncasecmp(c, "/Move=", 6) == 0) {
        float mm = strtof(c + 6, nullptr);
        if (!std::isfinite(mm) || std::fabs(mm) > 50.0f) {
            report_err("bad distance");
            return true;
        }
        // - = back along the path (larger "back" distance)
        float s = _sTarget - mm;
        _sTarget = std::max(0.0f, std::min(s, (float)_len));
        return true;
    }
    if (strncasecmp(c, "/Z=", 3) == 0) {
        float mm = strtof(c + 3, nullptr);
        float z  = _zTarget + mm;
        float lim = zadj_limit() - std::fabs(zadj_requested());
        if (!std::isfinite(z) || std::fabs(mm) > 0.5f) {
            report_err("at most 0.5 mm per step");
            return true;
        }
        if (std::fabs(z) > lim + 1e-4f) {
            report_err("Z adjust limit reached");
            z = z > 0 ? lim : -lim;
        }
        _zTarget = std::round(z * 1000.0f) / 1000.0f;
        return true;
    }
    if (strcasecmp(c, "/Continue") == 0) {
        _contReq = true;
        return true;
    }
    if (strcasecmp(c, "/Cancel") == 0) {
        _cancelReq = true;
        return true;
    }
    report_err("use $Rewind/Start, /Move=-0.5, /Z=+0.05, /Continue, /Cancel");
    return true;
}
