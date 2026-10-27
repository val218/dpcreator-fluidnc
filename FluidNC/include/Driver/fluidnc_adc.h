// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// One-shot ADC reads for user_inputs: analogN_pin (M66 E<n> L0).
#pragma once
#include "Driver/fluidnc_gpio.h"  // pinnum_t

// The average of `samples` raw 12-bit readings (0..4095) of a GPIO, or -1
// when this platform has no ADC driver.
int adc_read_raw(pinnum_t gpio, int samples);
