// CrashTrail.h - the last command lines before a crash.
//
// Kept in RTC memory, which survives a crash restart (not a power cut): after
// a crash, $Crash lists what was sent last ([MSG:CrashLast:...]), which helps
// to find what led to it when the backtrace alone is not enough.
#pragma once
#include <cstddef>

void crash_trail_boot();                          // at boot: keep the previous run's lines, start a new list
void crash_trail_note(const char* line);          // a $ / [ command line was received
int  crash_trail_count();                         // lines kept from before the restart
const char* crash_trail_line(int i);              // 0 = oldest
