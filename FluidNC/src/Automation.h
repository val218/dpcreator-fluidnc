// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Error.h"

void  automation_poll();               // polling task
bool  automation_has_mcode(int code);  // G-code parser: is M<code> (100-199) an automation?
Error automation_mcode(int code);      // G-code execution, after protocol_buffer_synchronize()
void  make_automation_commands();      // $Automation/...
