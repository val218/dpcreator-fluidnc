// Recovery.h - job recovery owned by the controller.
//
// While a file job runs, the line being executed is recorded (on the local
// filesystem, so a power cut does not lose it). If the job ends any other way
// than reaching the end of the file - E-stop, alarm, reset, Stop, power loss -
// it is kept as "interrupted" and offered for recovery. The pendant and the
// WebUI only show it; the controller works out safe restart points and
// continues the job from a chosen line with the machine set up exactly as the
// file had it there (coordinate system, units, feed, spindle, coolant, tool
// position).
//
// Commands (any channel):
//   $Job/Recovery          report the state
//   $Job/Points            work out safe restart points (last retract, last tool change)
//   $Job/Resume=<line>     continue the interrupted job from <line>
//   $Job/Resume=<line>:dry go to that line's start position at safe height only
//   $Job/Discard           forget the interrupted job
// Reports (all channels):
//   [MSG:Recovery:<state>:<stopLine>:<retractLine>:<toolLine>:<reason>:<file>]
//     state: none | interrupted | scanning | resuming
//     retractLine / toolLine: suggested restart lines, 0 = not worked out / none
//   [MSG:RecoveryErr:<text>]
#pragma once
#include <string>

class Channel;

void recov_init();                                              // at boot
void recov_poll();                                              // protocol task, every pass
void recov_job_start(Channel* file, const std::string& path);   // just before Job::nest of an SD file job
void recov_job_eof(Channel* ch);                                // a job source reached its end (before unnest)
bool recov_command(const char* line);                           // $Job/... (protocol task); false = not ours
