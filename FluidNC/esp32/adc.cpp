// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// ESP32 / ESP32-S3: Arduino's analogRead (ESP-IDF oneshot ADC underneath),
// 12 bit, 11 dB attenuation (0 to ~3.1 V) - the Arduino default.
#include "Driver/fluidnc_adc.h"

#include <Arduino.h>

int adc_read_raw(pinnum_t gpio, int samples) {
    static bool resolution_set = false;
    if (!resolution_set) {
        analogReadResolution(12);
        resolution_set = true;
    }
    if (samples < 1) {
        samples = 1;
    }
    int32_t sum = 0;
    for (int i = 0; i < samples; ++i) {
        sum += analogRead(gpio);
    }
    return int(sum / samples);
}
