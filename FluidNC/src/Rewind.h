// Rewind.h - handwheel rewind in Hold ("manual handle retrace").
//
// While a file job is in Hold, the tool can be moved back along the path it
// has cut - live, with the pendant handwheel or the WebUI - and forward again,
// the Z height can be changed there, and Continue re-cuts from that point to
// where the job stopped at the new height and resumes the job.
//
// How: every planner block of a running job is recorded (end point, feed,
// line, spindle) when it is planned. In Hold the blocks still queued are
// dropped from that list, so the path starts exactly where the tool stopped.
// The moves are "system motions" - the mechanism FluidNC uses to park for the
// safety door - so the held job, its planner and the spindle stay as they
// are. Continue is only allowed once the tool is back at the hold point (X/Y
// exact, Z = hold Z + the new offset); the Z offset then becomes the job's
// live Z adjust (ZAdjust.h), so the rest of the job is cut at that height.
//
// Commands (any channel):
//   $Rewind/Start              enter rewind (job in Hold)
//   $Rewind/Move=-0.5          go 0.5 mm back along the path (+ = forward, never past the hold point)
//   $Rewind/Z=+0.05            raise (+) / lower (-) the tool there
//   $Rewind/Continue           re-cut to the hold point and resume (also: cycle start ~)
//   $Rewind/Cancel             return to the hold point at the old height, stay in Hold
//   $Rewind                    report
// Reports (all channels):
//   [MSG:Rewind:<off|active|continuing|returning>:<back_mm>:<z_mm>:<line>:<max_back_mm>]
//   [MSG:RewindErr:<text>]
#pragma once

struct plan_line_data_t;
class Channel;

void rewind_init();                                          // at boot (record buffer)
void rewind_record(const float* target, const plan_line_data_t* pl);  // planner: a block was queued
void rewind_job_start();                                     // a new top-level file job starts
void rewind_suspend_poll();                                  // protocol task, Hold suspend loop
bool rewind_cycle_start();                                   // cycle start in Hold: true = taken (Continue)
void rewind_reset();                                         // reset / abort
bool rewind_command(const char* line, Channel& channel);     // $Rewind...; false = not ours
bool rewind_active();
