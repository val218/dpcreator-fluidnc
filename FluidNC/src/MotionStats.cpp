// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "MotionStats.h"
#include "Settings.h"
#include "SettingsDefinitions.h"
#include "Logging.h"
#include "Stepping.h"
#include "Driver/step_engine.h"
#include "Driver/delay_usecs.h"  // ticks_per_us
#include "Machine/MachineConfig.h"
#include "string_util.h"

#include <cstdio>
#include <cstring>

namespace MotionStats {
    volatile uint32_t isr_max_ticks = 0;
    volatile uint32_t isr_sum_ticks = 0;
    volatile uint32_t isr_passes    = 0;

    namespace {
        EnumSetting*       scurve = nullptr;
        const enum_opt_t   scurveOptions = { { "Off", 0 }, { "Light", 1 }, { "Full", 2 } };

        // Peaks since the last reset (segment generator, one task)
        float peak_events = 0;
        float peak_axis[MAX_N_AXIS] = {};

        // Trace of segment speeds
        struct TraceItem {
            float    ms;     // segment end, from the first traced segment
            float    speed;  // mm/min at the segment end
            char     ramp;
            bool     shaped;
        };
        const int     TRACE_N = 1000;
        TraceItem*    trace   = nullptr;
        volatile int  trace_n = -1;  // -1 off, else items recorded
        float         trace_t = 0;

        const char* engineName() { return Machine::Stepping::_engine ? Machine::Stepping::_engine->name : "?"; }

        void reset() {
            peak_events = 0;
            for (auto& p : peak_axis) {
                p = 0;
            }
            isr_max_ticks = 0;
            isr_sum_ticks = 0;
            isr_passes    = 0;
        }

        Error stats(const char* value, AuthenticationLevel auth_level, Channel& out) {
            if (value && *value) {
                if (string_util::equal_ignore_case(value, "reset")) {
                    reset();
                    log_info_to(out, "Stepping statistics reset");
                    return Error::Ok;
                }
                return Error::InvalidValue;
            }
            char     b[160];
            uint32_t engine_limit = Machine::Stepping::maxPulsesPerSec();
            snprintf(b, sizeof(b), "[STEP:ENGINE=%s,PULSE_US=%u,DIR_US=%u,ENGINE_LIMIT=%u,SCURVE=%s]\n", engineName(),
                     unsigned(Machine::Stepping::_pulseUsecs), unsigned(Machine::Stepping::_directionDelayUsecs), unsigned(engine_limit),
                     scurveMode() == 2 ? "Full" : scurveMode() == 1 ? "Light" : "Off");
            out.print(b);

            // The stepper ISR runs once per step event (more often at low rates,
            // with AMASS): its longest pass bounds the step event rate.
            uint32_t passes = isr_passes, maxt = isr_max_ticks, sum = isr_sum_ticks;
            float    tpu    = ticks_per_us ? float(ticks_per_us) : 1.0f;
            if (passes) {
                float    max_us    = maxt / tpu, avg_us = (float(sum) / passes) / tpu;
                uint32_t isr_limit = max_us > 0 ? uint32_t(1000000.0f / max_us) : 0;
                snprintf(b, sizeof(b), "[STEP:ISR_MAX_US=%.2f,ISR_AVG_US=%.2f,ISR_PASSES=%u,ISR_LIMIT=%u]\n", max_us, avg_us,
                         unsigned(passes), unsigned(isr_limit));
            } else {
                snprintf(b, sizeof(b), "[STEP:ISR_MAX_US=-,ISR_PASSES=0]\n");
            }
            out.print(b);

            // Measured peaks, and what each axis needs at its max_rate_mm_per_min
            auto n_axis = Axes::_numberAxis;
            std::string peak = "[STEP:PEAK=" + std::to_string(uint32_t(peak_events));
            std::string need = "[STEP:NEED";
            for (axis_t a = X_AXIS; a < n_axis; ++a) {
                auto ax = config->_axes->_axis[a];
                peak += std::string(",") + Axes::axisName(a) + "=" + std::to_string(uint32_t(peak_axis[a]));
                uint32_t n = uint32_t(ax->_stepsPerMm * ax->_maxRate / 60.0f);
                need += std::string(",") + Axes::axisName(a) + "=" + std::to_string(n);
            }
            out.print((peak + "]\n").c_str());
            out.print((need + "]\n").c_str());
            return Error::Ok;
        }

        Error traceCmd(const char* value, AuthenticationLevel auth_level, Channel& out) {
            if (value && *value) {
                if (string_util::equal_ignore_case(value, "on")) {
                    if (!trace) {
                        trace = new TraceItem[TRACE_N];
                    }
                    trace_t = 0;
                    trace_n = 0;
                    log_info_to(out, "Stepping trace: recording the next " << TRACE_N << " segments");
                    return Error::Ok;
                }
                if (string_util::equal_ignore_case(value, "off")) {
                    trace_n = -1;
                    return Error::Ok;
                }
                return Error::InvalidValue;
            }
            int n = trace_n;
            if (n <= 0 || !trace) {
                log_info_to(out, "Stepping trace: empty ($Stepping/Trace=On, then move)");
                return Error::Ok;
            }
            char b[64];
            for (int i = 0; i < n; ++i) {
                snprintf(b, sizeof(b), "[TRACE:%.1f,%.1f,%c%s]\n", trace[i].ms, trace[i].speed, trace[i].ramp, trace[i].shaped ? "S" : "");
                out.print(b);
            }
            return Error::Ok;
        }
    }

    int scurveMode() { return scurve ? scurve->get() : 0; }

    void make_settings() {
        // S-shaped acceleration ramps: Light = half way (peak acceleration 1.25 x), Full (peak 1.5 x).
        scurve = new EnumSetting("S-curve acceleration", EXTENDED, WG, NULL, "Stepping/SCurve", 0, &scurveOptions);
    }

    void make_commands() {
        new ReportCommand("SST", "Stepping/Stats", stats, anyState);
        new ReportCommand("STR", "Stepping/Trace", traceCmd, anyState);
    }

    void segment(float dt_min, uint32_t n_events, const uint32_t* axis_steps, uint32_t event_count, int n_axis, float speed, char ramp,
                 bool shaped) {
        if (dt_min > 0 && n_events && event_count) {
            float rate = n_events / (dt_min * 60.0f);
            if (rate > peak_events) {
                peak_events = rate;
            }
            for (int a = 0; a < n_axis && a < MAX_N_AXIS; ++a) {
                float r = rate * float(axis_steps[a]) / float(event_count);
                if (r > peak_axis[a]) {
                    peak_axis[a] = r;
                }
            }
        }
        int n = trace_n;
        if (n >= 0 && n < TRACE_N && trace) {
            trace_t += dt_min * 60000.0f;
            trace[n] = { trace_t, speed, ramp, shaped };
            trace_n  = n + 1;
        }
    }
}

void motion_stats_commands() {
    MotionStats::make_commands();
}
