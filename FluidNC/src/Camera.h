// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// A camera on the head as a crosshair: address, calibration, aim (Camera.cpp).
#pragma once
#include "Channel.h"

void make_camera_settings();                      // from make_settings()
bool camera_command(const char* line, Channel& out);  // $Camera, $Camera/Aim=; true = handled
bool camera_offset(float& ox, float& oy);         // calibrated tool -> crosshair offset, mm
bool camera_aim_offset(float& ox, float& oy);     // the offset while the WebUI aims with the crosshair
