// CrashTrail.cpp - see CrashTrail.h
#include "CrashTrail.h"

#include <cstring>
#include <cstdint>
#if __has_include(<esp_attr.h>)
#    include <esp_attr.h>
#else
#    define RTC_NOINIT_ATTR
#endif

namespace {
    constexpr int    N   = 6;
    constexpr size_t LEN = 40;
    struct Trail {
        uint32_t magic;
        uint32_t next;
        char     line[N][LEN];
    };
    RTC_NOINIT_ATTR Trail _rtc;
    Trail                 _prev;
    int                   _prevCount = 0;
    constexpr uint32_t    MAGIC      = 0x54524C31;  // "TRL1"
}

void crash_trail_boot() {
    _prevCount = 0;
    if (_rtc.magic == MAGIC && _rtc.next < 100000) {
        _prev = _rtc;
        uint32_t have = _prev.next < (uint32_t)N ? _prev.next : (uint32_t)N;
        _prevCount    = (int)have;
    }
    memset(&_rtc, 0, sizeof(_rtc));
    _rtc.magic = MAGIC;
}

void crash_trail_note(const char* line) {
    if (_rtc.magic != MAGIC) return;
    uint32_t i = _rtc.next % N;
    strncpy(_rtc.line[i], line, LEN - 1);
    _rtc.line[i][LEN - 1] = '\0';
    _rtc.next             = _rtc.next + 1;
}

int crash_trail_count() { return _prevCount; }

const char* crash_trail_line(int i) {
    if (i < 0 || i >= _prevCount) return "";
    uint32_t first = _prev.next - (uint32_t)_prevCount;  // oldest kept
    return _prev.line[(first + (uint32_t)i) % N];
}
