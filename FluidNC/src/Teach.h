// Teach.h - Record & Retrace waypoint list kept on the board, so the TabUI
// pendant and the dpCREATOR WebUI edit the same list (and the pendant works
// without a PC). The list is saved on the SD card (/.dpc/teach.txt);
// $Teach/Save writes the job /retrace.nc (TeachGen.h).
//
//   $Teach  /  $Teach/List         -> cfg line, one pt line per waypoint, count
//   $Teach/Add[=op]                -> waypoint at the current work position, once Idle
//   $Teach/Set=n,op=r,ref=c,depth=5,w=30,h=20,passes=2,x=..,y=..,z=..
//   $Teach/Del=n   $Teach/Clear
//   $Teach/Cfg=safez=5,feed=500,plunge=150,rpm=12000,tool=3
//   $Teach/Measure=n               -> size of waypoint n from the current position, once Idle
//                                     (round: diameter = 2 x distance; rectangle from the
//                                     centre: 2|dX| x 2|dY|, from a corner mark: |dX| x |dY|)
//   $Teach/Save                    -> /sd/retrace.nc (refused while the list has problems)
//   $Pendant/Point                 -> enable double-tap on the pendant: to the WebUI tool that
//                                     claimed it ($Pendant/Claim=<name>, renewed every minute,
//                                     $Pendant/Claim= to release), otherwise $Teach/Add
//
// Broadcast to every channel (numbers 1-based, op m|d|r|p, ref c|bl|br|tl|tr):
//   [MSG:Teach:cfg:safez:feed:plunge:rpm:tool]
//   [MSG:Teach:pt:n:x:y:z:op:ref:depth:w:h:passes]
//   [MSG:Teach:count:n]                       after a full list / delete / clear
//   [MSG:Teach:added:n:pendant|ui]            followed by its pt line
//   [MSG:Teach:measured:n:w:h]                followed by its pt line
//   [MSG:Teach:saved:/retrace.nc:lines]
//   [MSG:Teach:err:text]
//   [MSG:PendantPoint]                        double-tap for a claiming WebUI tool
#pragma once
#include "Channel.h"

// true = the line was a Teach / $Pendant/Point / $Pendant/Claim command.
// deferred = run it again on the protocol task (it writes the SD card).
bool teach_command(const char* line, Channel& channel, bool on_protocol_task, bool& deferred);
void teach_poll();  // from the polling loop: pending Add / Measure once Idle, saving
