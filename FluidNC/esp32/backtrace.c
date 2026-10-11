// Copyright 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Driver/backtrace.h"

// ESP32-S3 builds capture the panic backtrace (esp32s3/panic_capture.c).
#ifndef FNC_PANIC_CAPTURE

bool backtrace_available(void) {
    return false;
}

bool backtrace_get(backtrace_t* bt) {
    (void)bt;
    return false;
}

void backtrace_clear(void) {}
#endif
