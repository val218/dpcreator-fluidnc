// Stats.h - job history and machine hour counters, kept on the SD card.
//
//   /sd/.dpc/jobs.log    one line per file job, newest last:
//                        <unix time or 0>|<file>|<seconds>|<done|stopped|alarm N|...>|<last line>
//   /sd/.dpc/stats.txt   run=<s> spindle=<s> service=<s> jobs=<n>
//                        run: time in Cycle; spindle: time with the spindle on;
//                        service: spindle time since the last service reset.
//
// The board has no clock: a page tells it the time ($Stats/Time=<unix seconds>,
// the WebUI does this on connect), so the log has dates once a page was open.
//
// Commands:
//   $Stats                 [MSG:Stats:run=<s>:spindle=<s>:service=<s>:jobs=<n>]
//   $Stats/Time=<unix>     the time now
//   $Stats/ResetService    spindle service counter to 0 (bearings, brushes ...)
#pragma once
#include <string>

class Channel;

void stats_init();                                                // at boot (loads the counters)
void stats_poll();                                                // polling task: count run / spindle time
void stats_save_if_due();                                         // protocol task: write the counters now and then
void stats_job_start(const std::string& path);                    // a file job started
void stats_job_end(const char* result, uint32_t line);            // it ended (done / stopped / alarm ...)
bool stats_command(const char* line, Channel& channel);           // $Stats...; false = not ours
