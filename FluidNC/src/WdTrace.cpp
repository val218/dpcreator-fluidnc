// WdTrace.cpp - see WdTrace.h
#include "WdTrace.h"

#include <cstring>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#if __has_include(<esp_attr.h>)
#    include <esp_attr.h>
#endif
// The report goes to .noinit RAM: like RTC memory it is not cleared by a panic
// or watchdog restart, and RTC slow memory has no room for it.
#ifndef __NOINIT_ATTR
#    define __NOINIT_ATTR
#endif
#ifdef FNC_PANIC_CAPTURE
#    include <esp_task_wdt.h>
extern "C" int wd_task_backtrace(void* task, uint32_t* out, int max);  // panic_capture.c
#endif

namespace {
    // Live marks, in normal RAM: the hot path is two stores.
    struct Mark {
        const char* volatile where = nullptr;
        volatile uint32_t    since = 0;     // ms when it got there
        void* volatile       task  = nullptr;
    };
    Mark _m[WD_SLOTS];
    char _text[WD_SLOTS][64];  // wd_mark_text() labels

    const char* slot_name(int s) { return s == WD_POLL ? "poller" : s == WD_PROTO ? "protocol" : "network"; }

    uint32_t now_ms() { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }

    // The report, written once by the watchdog interrupt, kept across the restart
    // (not across a power cycle).
    constexpr int      LINES = 4;
    constexpr int      LEN   = 300;
    constexpr uint32_t MAGIC = 0x57445431;  // "WDT1"
    struct Report {
        uint32_t magic;
        uint32_t n;
        char     line[LINES][LEN];
    };
    __NOINIT_ATTR Report   _rtc;
    Report                 _prev;
    int                    _prevCount = 0;

    // Tiny appenders: no printf in the interrupt.
    struct Buf {
        char* p;
        int   n;
        int   cap;
        void  s(const char* t) {
            while (t && *t && n < cap - 1) p[n++] = *t++;
            p[n] = '\0';
        }
        void u(uint32_t v) {
            char d[11];
            int  k = 0;
            do { d[k++] = char('0' + v % 10); v /= 10; } while (v && k < 10);
            while (k && n < cap - 1) p[n++] = d[--k];
            p[n] = '\0';
        }
        void hex(uint32_t v) {
            s("0x");
            for (int sh = 28; sh >= 0; sh -= 4) {
                if (n < cap - 1) p[n++] = "0123456789abcdef"[(v >> sh) & 15];
            }
            p[n] = '\0';
        }
    };

#ifdef FNC_PANIC_CAPTURE
    // esp_task_wdt_print_triggered_tasks() hands over the caption, then
    // "\n - ", name, " (CPU n)" per task: keep the names and CPUs.
    void on_triggered(void* opaque, const char* msg) {
        Buf* b = static_cast<Buf*>(opaque);
        if (strncmp(msg, "Task watchdog", 13) == 0) return;
        if (strcmp(msg, "\n - ") == 0) {
            b->s(b->n > 0 && b->p[b->n - 1] == ':' ? " " : ", ");
            return;
        }
        b->s(msg);
    }
#endif
}

void wd_task(WdSlot slot) {
    _m[slot].task = xTaskGetCurrentTaskHandle();
}

void wd_mark(WdSlot slot, const char* where) {
    if (_m[slot].where != where) {
        _m[slot].since = now_ms();
        _m[slot].where = where;
    }
}

void wd_mark_text(WdSlot slot, const char* what, const char* text) {
    Buf b { _text[slot], 0, (int)sizeof(_text[slot]) };
    b.s(what);
    b.s(" ");
    b.s(text);
    _m[slot].since = now_ms();
    _m[slot].where = _text[slot];
}

void wd_boot() {
    _prevCount = 0;
    if (_rtc.magic == MAGIC && _rtc.n <= (uint32_t)LINES) {
        _prev = _rtc;
        for (int i = 0; i < LINES; i++) _prev.line[i][LEN - 1] = '\0';
        _prevCount = (int)_prev.n;
    }
    _rtc.magic = 0;
    _rtc.n     = 0;
}

int wd_report_count() {
    return _prevCount;
}

const char* wd_report_line(int i) {
    return (i >= 0 && i < _prevCount) ? _prev.line[i] : "";
}

// From the task watchdog interrupt, before the IDF aborts. Interrupt context:
// no locks, no allocation, no printf.
extern "C" void wd_trace_on_timeout() {
#ifdef FNC_PANIC_CAPTURE
    _rtc.magic = 0;
    _rtc.n     = 0;
    uint32_t now = (uint32_t)(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS);

    // 1: who missed the watchdog, and what each CPU is running
    {
        Buf b { _rtc.line[0], 0, LEN };
        b.s("missed:");
        esp_task_wdt_print_triggered_tasks(on_triggered, &b, nullptr);
        for (int c = 0; c < portNUM_PROCESSORS; c++) {
            b.s(c == 0 ? " | running: CPU0 " : ", CPU1 ");
            b.s(pcTaskGetName(xTaskGetCurrentTaskHandleForCore(c)));
        }
    }
    // 2..4: each watched task - where it was, how long, its call stack if waiting
    int n = 1;
    for (int s = 0; s < WD_SLOTS && n < LINES; s++) {
        const char* where = _m[s].where;
        void*       task  = _m[s].task;
        if (!where && !task) continue;
        Buf b { _rtc.line[n++], 0, LEN };
        b.s(slot_name(s));
        if (task) {
            b.s(" (");
            b.s(pcTaskGetName((TaskHandle_t)task));
            b.s(")");
        }
        b.s(" at ");
        b.s(where ? where : "?");
        b.s(" for ");
        b.u(now - _m[s].since);
        b.s(" ms");
        if (!task) continue;
        bool running = false;
        for (int c = 0; c < portNUM_PROCESSORS; c++) {
            if (xTaskGetCurrentTaskHandleForCore(c) == (TaskHandle_t)task) running = true;
        }
        if (running) {
            b.s(", running (see the crash backtrace)");
            continue;
        }
        uint32_t addr[10];
        int      k = wd_task_backtrace(task, addr, 10);
        if (k > 0) {
            b.s(", waiting. Backtrace:");
            for (int i = 0; i < k; i++) {
                b.s(" ");
                b.hex(addr[i]);
                b.s(":0x00000000");
            }
        }
    }
    _rtc.n     = (uint32_t)n;
    _rtc.magic = MAGIC;
#endif
}
