// Stats.cpp - see Stats.h
#include "Stats.h"

#include "FileStream.h"
#include "FluidPath.h"
#include "Serial.h"
#include "Channel.h"
#include "State.h"
#include "GCode.h"

#include <Arduino.h>  // millis
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <strings.h>
#include <algorithm>

namespace {
    const char* DIR   = "/.dpc";
    const char* LOG   = "/.dpc/jobs.log";
    const char* STATS = "/.dpc/stats.txt";

    // seconds, counted by the polling task (whole-second steps from millis)
    volatile uint32_t _run = 0, _spindle = 0, _service = 0, _jobs = 0;
    uint32_t          _lastTick  = 0;
    uint32_t          _msRun     = 0, _msSpin = 0;
    volatile bool     _dirty     = false;
    uint32_t          _lastSave  = 0;
    bool              _loaded    = false;

    int64_t     _epochAtBoot = 0;  // unix time at millis() == 0, 0 = unknown
    std::string _jobPath;
    uint32_t    _jobStartMs = 0;
    uint32_t    _jobStartEpoch = 0;
    bool        _jobOn = false;

    uint32_t now_epoch() { return _epochAtBoot ? (uint32_t)(_epochAtBoot + millis() / 1000) : 0; }

    void make_dir() {
        std::error_code ec;
        FluidPath       p { DIR, SD, ec };
        if (!ec) stdfs::create_directory(p, ec);
    }

    void load() {
        _loaded = true;
        try {
            FileStream f(STATS, "r", SD);
            char       b[160] = {};
            int        n      = f.read(b, sizeof(b) - 1);
            if (n <= 0) return;
            b[n]      = '\0';
            auto get  = [&](const char* k) -> uint32_t {
                const char* p = strstr(b, k);
                return p ? (uint32_t)strtoul(p + strlen(k), nullptr, 10) : 0;
            };
            _run     = get("run=");
            _spindle = get("spindle=");
            _service = get("service=");
            _jobs    = get("jobs=");
        } catch (...) {}
    }

    void save() {
        make_dir();
        try {
            FileStream f(STATS, "w", SD);
            char       b[160];
            int        n = snprintf(b, sizeof(b), "run=%u spindle=%u service=%u jobs=%u\n", (unsigned)_run, (unsigned)_spindle,
                             (unsigned)_service, (unsigned)_jobs);
            f.write((const uint8_t*)b, n);
            _dirty    = false;
            _lastSave = millis();
        } catch (...) {}
    }

    void report(Channel& out) {
        char m[120];
        snprintf(m, sizeof(m), "[MSG:Stats:run=%u:spindle=%u:service=%u:jobs=%u]\r\n", (unsigned)_run, (unsigned)_spindle,
                 (unsigned)_service, (unsigned)_jobs);
        out.print(m);
    }
}

void stats_init() {
    _lastTick = millis();
}

void stats_poll() {
    uint32_t now = millis();
    uint32_t dt  = now - _lastTick;
    if (dt < 200) return;
    _lastTick = now;
    if (dt > 5000) return;  // stalled / just started: do not count
    if (state_is(State::Cycle)) {
        _msRun += dt;
        if (_msRun >= 1000) { _run = _run + _msRun / 1000; _msRun %= 1000; _dirty = true; }
    }
    if (gc_state.modal.spindle != SpindleState::Disable && !state_is(State::Alarm) && !state_is(State::Idle)) {
        _msSpin += dt;
        if (_msSpin >= 1000) {
            uint32_t s = _msSpin / 1000;
            _msSpin %= 1000;
            _spindle = _spindle + s;
            _service = _service + s;
            _dirty   = true;
        }
    }
}

void stats_save_if_due() {
    if (!_loaded) load();  // first pass after boot (SD is mounted by then)
    if (_dirty && millis() - _lastSave > 300000) save();  // every 5 minutes while counting
}

void stats_job_start(const std::string& path) {
    if (!_loaded) load();
    _jobPath       = path;
    _jobStartMs    = millis();
    _jobStartEpoch = now_epoch();
    _jobOn         = true;
}

void stats_job_end(const char* result, uint32_t line) {
    if (!_jobOn) return;
    _jobOn = false;
    _jobs  = _jobs + 1;
    make_dir();
    try {
        FileStream f(LOG, "a", SD);
        char       b[300];
        int        n = snprintf(b, sizeof(b), "%u|%s|%u|%s|%u\n", (unsigned)_jobStartEpoch, _jobPath.c_str(),
                         (unsigned)((millis() - _jobStartMs) / 1000), result, (unsigned)line);
        f.write((const uint8_t*)b, std::min(n, (int)sizeof(b) - 1));
    } catch (...) {}
    save();
}

bool stats_command(const char* line, Channel& channel) {
    if (strncasecmp(line, "$Stats", 6) != 0) return false;
    const char* c = line + 6;
    if (!_loaded) load();
    if (*c == '\0') {
        report(channel);
        return true;
    }
    if (strncasecmp(c, "/Time=", 6) == 0) {
        long long t = atoll(c + 6);
        if (t > 1600000000LL) _epochAtBoot = t - (int64_t)(millis() / 1000);
        return true;
    }
    if (strcasecmp(c, "/ResetService") == 0) {
        _service = 0;
        _dirty   = true;
        _lastSave = 0;  // write soon
        allChannels.print("[MSG:StatsInfo:spindle service counter reset]\r\n");
        report(channel);
        return true;
    }
    return false;
}
