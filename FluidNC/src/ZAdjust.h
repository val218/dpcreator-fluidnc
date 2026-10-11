// ZAdjust.h - live Z adjustment while a job runs ("babystepping").
//
// A temporary Z offset for the running job, set from the WebUI or the pendant
// while the machine cuts. Like babystepping on a 3D printer it is applied
// live: the stepper interrupt adds single Z steps between the job's own steps
// (at most one per millisecond, only on ticks where Z does not step), so the
// tool moves within a fraction of a second, not after the moves already
// planned. Like a wear offset on an industrial control it is never written to
// the work coordinates and it ends with the job; afterwards the offset can be
// added to the work zero on request, so the next run cuts at the corrected
// depth.
//
// Positive = tool higher = shallower cut.
//
// The steps only go out while the machine moves (the stepper runs); in Hold a
// change waits for Resume. The machine position (MPos) is the real tool
// position; the work offset (WCO) includes the steps made, so WPos, the DRO
// and the 3D views keep showing the file's Z. When the job ends the planner
// and parser take the real position over, so nothing moves back.
//
// Commands (any channel, any time):
//   $Job/ZAdjust               report
//   $Job/ZAdjust=+0.05         add (sign given); job must be running or in Hold
//   $Job/ZAdjust=0.2           set (no sign); =0 clears
//   $Job/ZAdjust/Apply         add the offset the last job ended with to the
//                              work zero of the coordinate system it used (Idle)
//   $Job/ZAdjust/Limit=<mm>    largest total offset, 0.05 .. 5 (default 1.0, kept)
// Reports (all channels):
//   [MSG:ZAdjust:<offset>:<none|pending|active|ended>:0]
//     pending: the tool is still moving to it (or waits for Resume in Hold)
//     ended:   the job ended with <offset>
//   [MSG:ZAdjustErr:<text>]
#pragma once
#include <cstdint>

class Channel;

void zadj_init();                        // at boot (limit, step timing)
void zadj_poll();                        // polling task, every pass
void zadj_protocol_poll();               // protocol task, before taking a line (job-end resync)
bool zadj_command(const char* line, Channel& channel, bool on_protocol_task, bool& deferred);  // false = not ours
float zadj_shown();                      // offset the tool is at now, mm (WCO in status reports)

// Stepper interrupt (Stepper.cpp): steps wanted / made
extern volatile int32_t zadj_isr_target;
extern volatile int32_t zadj_isr_done;
extern int32_t          zadj_isr_interval;  // CPU ticks between added steps
