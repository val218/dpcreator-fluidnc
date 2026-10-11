// ZAdjust.h - live Z adjustment while a job runs ("babystepping").
//
// A temporary Z offset for the running job, set from the WebUI or the pendant
// while the machine cuts or is in Hold. Like a wear offset on an industrial
// control it is applied by the G-code parser to the moves that follow, it is
// never written to the work coordinates, and it ends with the job. After the
// job the offset can be added to the work zero on request, so the next run
// cuts at the corrected depth.
//
// Positive = tool higher = shallower cut.
//
// How it reaches the tool: the parser adds it to Z of the next G0/G1 (or G2/G3
// in the XY plane) it reads. FluidNC plans moves ahead, so the machine gets
// there after the moves already planned; the report says "pending" until the
// machine executes that line, then "active". The work position shown (WCO)
// includes the offset, so the DRO and the 3D view keep showing the file's Z.
//
// Commands (any channel, any time):
//   $Job/ZAdjust               report
//   $Job/ZAdjust=+0.05         add (sign given); job must be running or in Hold
//   $Job/ZAdjust=0.2           set (no sign); =0 clears
//   $Job/ZAdjust/Apply         add the offset the last job ended with to the
//                              work zero of the coordinate system it used (Idle)
//   $Job/ZAdjust/Limit=<mm>    largest total offset, 0.05 .. 5 (default 1.0, kept)
// Reports (all channels):
//   [MSG:ZAdjust:<offset>:<none|pending|active|ended>:<line>]
//     pending: applies from file line <line>; ended: the job ended with <offset>
//   [MSG:ZAdjustErr:<text>]
#pragma once
#include <cstdint>

class Channel;

void zadj_init();                        // at boot (loads the limit)
void zadj_poll();                        // polling task, every pass (runs while the protocol task waits on the planner)
bool zadj_command(const char* line, Channel& channel, bool on_protocol_task, bool& deferred);  // false = not ours

// G-code parser hooks (protocol task)
float zadj_applied();                    // offset included in the parser position (G92, G10 L20)
float zadj_shown();                      // offset the machine is cutting with now (WCO in status reports)
float zadj_target(bool canApply);        // offset to put into this move
void  zadj_moved(float applied);         // the move was made with this offset
void  zadj_reset_position();             // the parser position no longer includes it (G53 Z, G28/G30)
