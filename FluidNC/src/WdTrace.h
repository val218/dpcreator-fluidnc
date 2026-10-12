// WdTrace.h - what the watched tasks were doing when the task watchdog fired.
//
// The task watchdog resets the board when a watched task - the polling task
// (all channels: pendant UART, WebSocket, telnet), the protocol task (G-code,
// motion) or the network task async_tcp (web pages, WebDAV, WebSocket input)
// - does not check in for 5 s. The IDF names that task only on the serial
// port, and the crash backtrace it saves is the one of the CPU, which is just
// idle when the task is *waiting* (a lock, a queue, a slow device).
//
// So:
//  - each watched loop marks where it is: a short label kept as a pointer,
//    cheap enough for every pass; HTTP requests mark their method and URL;
//  - when the watchdog fires (IDF hook esp_task_wdt_isr_user_handler) the
//    tasks that missed it, every mark with its age and the call stack of each
//    waiting watched task are written to RTC memory, which survives the
//    restart;
//  - $Crash reports them as [MSG:CrashWdt:...] lines (decode the
//    "Backtrace:" parts like the crash backtrace).
#pragma once

enum WdSlot { WD_POLL = 0, WD_PROTO = 1, WD_NET = 2, WD_SLOTS = 3 };

void wd_task(WdSlot slot);                                   // the calling task is this slot's
void wd_mark(WdSlot slot, const char* where);                // where: a string literal
void wd_mark_text(WdSlot slot, const char* what, const char* text);  // text is copied (a URL)
void wd_boot();                                              // at boot: keep the last report, start fresh
int         wd_report_count();                               // lines of the previous run's report (0 = none)
const char* wd_report_line(int i);

extern "C" void wd_trace_on_timeout();                       // from the watchdog interrupt (panic_capture.c)
