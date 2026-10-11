// MacroLib.h - user macros kept on the SD card, shared by the WebUI and the pendant.
//
// A macro is a G-code file: any number of lines, $ commands, and FluidNC's
// expressions and flow control (#<name> parameters, [maths], o-word if / while /
// repeat, (PRINT,...) / (MSG,...)). The list lives on the SD card, so it
// survives firmware updates (flashing merged-flash.bin rewrites the flash
// filesystem):
//
//   /sd/.dpc/macros.txt        one line per macro, in button order:
//                              <id>|<name>|<#rrggbb>|<flags>
//                              flags: p = on the pendant, c = ask before running,
//                                     b = button on the WebUI Control panel
//   /sd/.dpc/macros/<id>.nc    the macro itself
//
// The WebUI edits these files (WebDAV) and then sends $MacroLib/Reload. If the
// list does not exist yet, the first $MacroLib/List creates the starter set.
//
// Commands:
//   $MacroLib/List         [MSG:MacroItem:<id>:<#rrggbb>:<flags>:<name>] ... [MSG:MacroEnd:<count>]
//   $MacroLib/Run=<id>     run it (machine Idle) - like a file job, without job recovery / preview
//   $MacroLib/Reload       tell every screen the list changed ([MSG:MacroLib:changed])
#pragma once
#include <string>

class Channel;

bool        macrolib_command(const char* line, Channel& channel, std::string& runLine);  // false = not ours
bool        macrolib_is_macro_path(const std::string& path);  // a macro file (no job recovery / preview)
const char* macrolib_dir();                                   // "/.dpc" on the SD card
