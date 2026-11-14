// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Motion quality: the S-curve setting, step-rate statistics and a segment trace.
//
//   $Stepping/SCurve=Off|Light|Full   S-shaped speed ramps (Stepper.cpp, SCurve.h); persisted
//   $Stepping/Stats   ($SST)          step engine limits, measured step rates, stepper ISR time
//   $Stepping/Stats=Reset             start measuring again
//   $Stepping/Trace=On                record the speed of the next 1000 step segments (~10 s)
//   $Stepping/Trace   ($STR)          print them: [TRACE:<ms>,<mm/min>,<ramp>]
#pragma once

#include "Platform.h"  // IRAM_ATTR
#include <cstdint>

namespace MotionStats {
    int  scurveMode();  // 0 Off, 1 Light, 2 Full
    void make_settings();
    void make_commands();

    // From the segment generator (main / protocol task): one segment of n step
    // events (dominant axis) over dt minutes, at speed mm/min.
    // ramp: 'A' accelerating, 'C' cruise, 'D' decelerating; S: shaped.
    void segment(float dt_min, uint32_t n_events, const uint32_t* axis_steps, uint32_t event_count, int n_axis, float speed, char ramp,
                 bool shaped);

    // From the stepper ISR: how long one pass took, in CPU ticks
    extern volatile uint32_t isr_max_ticks;
    extern volatile uint32_t isr_sum_ticks;
    extern volatile uint32_t isr_passes;
    inline void IRAM_ATTR isrTime(uint32_t ticks) {
        if (ticks > isr_max_ticks) {
            isr_max_ticks = ticks;
        }
        isr_sum_ticks = isr_sum_ticks + ticks;
        isr_passes    = isr_passes + 1;
    }
}
