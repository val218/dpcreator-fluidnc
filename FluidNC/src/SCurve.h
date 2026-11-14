// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// S-curve speed ramps for the step segment generator (Stepper::prep_buffer).
//
// The planner is unchanged: it still plans each ramp as a straight-line speed
// change from v0 to v1 at the acceleration a, taking T = |v1 - v0| / a.  An
// S-ramp keeps the same v0, v1 and T - so the same distance, (v0 + v1) / 2 * T,
// and the same junction speeds - but bends the speed curve:
//
//   v(t) = v0 + (v1 - v0) * shape(t / T),   shape(s) = (1 - k) s + k (3 s^2 - 2 s^3)
//
// k = 1 (Full): the acceleration starts and ends at 0 and peaks at 1.5 a.
// k = 0.5 (Light): it starts and ends at 0.5 a and peaks at 1.25 a.
// k = 0: the plain straight ramp.
//
// Pure arithmetic, so the unit tests can check it.  Units are the stepper's:
// mm, mm/min, minutes.
#pragma once

namespace SCurve {
    // Share of the speed change done after s (0..1) of the ramp time
    inline float shape(float k, float s) { return (1.0f - k) * s + k * s * s * (3.0f - 2.0f * s); }
    // Integral of shape() from 0 to s; area(k, 1) = 1/2 for every k, so the distance does not depend on k
    inline float area(float k, float s) { return (1.0f - k) * 0.5f * s * s + k * s * s * s * (1.0f - 0.5f * s); }
    // Acceleration factor at s (times a): 1 - k + 6 k s (1 - s); peak 1 + k / 2 at s = 0.5
    inline float accelFactor(float k, float s) { return 1.0f - k + 6.0f * k * s * (1.0f - s); }

    struct Ramp {
        float k  = 1.0f;
        float v0 = 0, v1 = 0;  // speed at the start and the end (mm/min)
        float T  = 0;          // duration (min)
        float t  = 0;          // time into the ramp (min)
        float mm0 = 0;         // distance to the end of the block at the start (mm)

        void start(float strength, float from, float to, float accel, float mm_at_start) {
            k   = strength;
            v0  = from;
            v1  = to;
            float dv = to > from ? to - from : from - to;
            T   = accel > 0 ? dv / accel : 0;
            t   = 0;
            mm0 = mm_at_start;
        }
        float speedAt(float tt) const { return T > 0 ? v0 + (v1 - v0) * shape(k, tt / T) : v1; }
        // Distance covered from the ramp start to time tt
        float distAt(float tt) const { return T > 0 ? v0 * tt + (v1 - v0) * T * area(k, tt / T) : 0; }
        float length() const { return 0.5f * (v0 + v1) * T; }
        // Distance to the end of the block where the ramp ends
        float endMm() const { return mm0 - length(); }
    };
}
