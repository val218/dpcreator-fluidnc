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
//   $Job/Zeroed            no homing on this machine: X/Y/Z zero is set again at the same spot
//   $Job/RunFrom=<line>:<file>  start a file from a line: offered like an interrupted job
// Reports (all channels):
//   [MSG:Recovery:<state>:<stopLine>:<retractLine>:<toolLine>:<reason>:<file>]
//     state: none | interrupted | scanning | resuming
//     retractLine / toolLine: suggested restart lines, 0 = not worked out / none
//   [MSG:RecoveryErr:<text>]
#pragma once
#include <cstdint>
#include <string>

class Channel;

void recov_init();                                              // at boot
void recov_poll();                                              // protocol task, every pass
void recov_job_start(Channel* file, const std::string& path);   // just before Job::nest of an SD file job
void recov_job_eof(Channel* ch);                                // a job source reached its end (before unnest)
bool recov_command(const char* line);                           // $Job/... (protocol task); false = not ours

// Position trust for a resume (stricter than FluidNC's own homed flags, which
// $X sets back): lost at power-up and when motion stops abruptly (a reset or
// E-stop while moving, a hard limit, a hard stop, a failed homing). Only a real
// homing of the axes that home brings it back - or, on a machine without
// homing, the operator's confirmation that X/Y/Z zero is set again at the same
// spot ($Job/Zeroed). Until then RecoveryReady is "unhomed" (or "rezero").
void recov_position_lost();                                     // any task (alarm handling)
void recov_axes_homed(uint32_t axes);                           // a homing cycle set these axes
