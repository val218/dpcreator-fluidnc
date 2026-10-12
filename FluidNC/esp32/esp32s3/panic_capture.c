// panic_capture.c - keep the backtrace of a crash across the restart (ESP32-S3).
//
// The generic esp32/backtrace.c is a stub (the IDF coredump hook in
// esp32/coredump.c is not part of the PlatformIO build), so "$Crash" could
// only ever say "unknown". Here the IDF panic handler is wrapped
// (-Wl,--wrap=esp_panic_handler): before the normal panic output and restart,
// the faulting PC, cause and call stack are saved in RTC memory that survives
// the restart. MachineConfig logs it at boot and $Crash reports it.
#ifdef FNC_PANIC_CAPTURE

#include <stddef.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_private/panic_internal.h"
#include "esp_debug_helpers.h"
#include "xtensa_context.h"
#include "Driver/backtrace.h"

#define BACKTRACE_MAGIC 0x42545243  // "BTRC"

typedef struct {
    uint32_t    magic;
    uint32_t    crc;
    backtrace_t bt;
} backtrace_record_t;

static RTC_NOINIT_ATTR backtrace_record_t _saved_bt;

static uint32_t IRAM_ATTR backtrace_crc(const backtrace_t* bt) {
    const uint8_t* p   = (const uint8_t*)bt;
    uint32_t       crc = 0xFFFFFFFF;
    for (size_t i = 0; i < sizeof(backtrace_t); i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return crc ^ 0xFFFFFFFF;
}

static bool backtrace_valid(void) {
    return _saved_bt.magic == BACKTRACE_MAGIC && _saved_bt.crc == backtrace_crc(&_saved_bt.bt);
}

bool backtrace_available(void) {
    return backtrace_valid();
}

bool backtrace_get(backtrace_t* bt) {
    if (!backtrace_valid()) {
        return false;
    }
    *bt = _saved_bt.bt;
    return true;
}

void backtrace_clear(void) {
    _saved_bt.magic = 0;
}

// Windowed-call PCs carry the call size in the top bits: back to an address.
static inline IRAM_ATTR uint32_t stack_pc(uint32_t pc) {
    return (pc & 0x80000000) ? ((pc & 0x3fffffff) | 0x40000000) : pc;
}

static void IRAM_ATTR capture(panic_info_t* info) {
    _saved_bt.magic = 0;
    backtrace_t* bt = &_saved_bt.bt;
    for (size_t i = 0; i < sizeof(backtrace_t); i++) ((volatile uint8_t*)bt)[i] = 0;
    if (!info || !info->frame) {
        return;
    }
    XtExcFrame* frame = (XtExcFrame*)info->frame;
    bt->pc            = frame->pc;
    bt->excvaddr      = frame->excvaddr;
    bt->exccause      = frame->exccause;

    esp_backtrace_frame_t f;
    f.pc        = frame->pc;
    f.sp        = frame->a1;
    f.next_pc   = frame->a0;
    f.exc_frame = info->frame;

    bt->addresses[0]  = stack_pc(f.pc);
    bt->num_addresses = 1;
    while (bt->num_addresses < BACKTRACE_MAX_ADDRESSES && f.next_pc != 0) {
        if (!esp_backtrace_get_next_frame(&f)) {
            break;
        }
        bt->addresses[bt->num_addresses++] = stack_pc(f.pc);
    }
    _saved_bt.crc   = backtrace_crc(bt);
    _saved_bt.magic = BACKTRACE_MAGIC;
}

// Call stack of a task that is not running (WdTrace: the watched task the
// watchdog fired for is usually *waiting*). Its saved context is either an
// interrupt frame (preempted) or the solicited frame of vPortYield() (blocked
// in a FreeRTOS call), whose register windows were spilled to the stack.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_private/freertos_debug.h"
#include "esp_memory_utils.h"

int wd_task_backtrace(void* task, uint32_t* out, int max) {
    TaskSnapshot_t snap;
    if (!task || max <= 0 || vTaskGetSnapshot((TaskHandle_t)task, &snap) != pdTRUE) {
        return 0;
    }
    uint32_t top = (uint32_t)snap.pxTopOfStack;
    if (!esp_stack_ptr_is_sane(top)) {
        return 0;
    }
    esp_backtrace_frame_t f;
    int                   n = 0;
    if (((const XtSolFrame*)top)->exit != 0) {  // preempted: full interrupt frame
        const XtExcFrame* x = (const XtExcFrame*)top;
        f.pc                = x->pc;
        f.sp                = x->a1;
        f.next_pc           = x->a0;
        f.exc_frame         = x;
        out[n++]            = stack_pc(f.pc);
    } else {  // blocked: the frame is vPortYield's, its caller comes next
        const XtSolFrame* s = (const XtSolFrame*)top;
        f.pc                = 0;
        f.sp                = top;
        f.next_pc           = s->pc;
        f.exc_frame         = NULL;
    }
    while (n < max && f.next_pc != 0) {
        if (!esp_backtrace_get_next_frame(&f)) {
            if (n < max && f.pc) out[n++] = stack_pc(f.pc);  // last good PC
            break;
        }
        out[n++] = stack_pc(f.pc);
    }
    return n;
}

// IDF hook, called from the task watchdog interrupt before it aborts.
void wd_trace_on_timeout(void);
void esp_task_wdt_isr_user_handler(void) {
    wd_trace_on_timeout();
}

void __real_esp_panic_handler(panic_info_t* info);

void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t* info) {
    capture(info);
    __real_esp_panic_handler(info);  // the usual panic output and restart
}

#endif
