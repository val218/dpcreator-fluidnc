// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Camera.cpp - a camera on the head (an IP / ESP32 Wi-Fi camera) as a crosshair.
//
// The board keeps the camera's address and its calibration, so every WebUI and
// the pendant use the same ones:
//   $Camera/URL=http://192.168.1.50/capture    (snapshot or MJPEG stream address)
//   $Camera/Calib=ox,oy,a,b,c,d,mz              (written by the WebUI's calibration)
//   ($Camera/Calib=- clears it)
//      ox, oy: from the tool to the point under the crosshair, mm: the crosshair
//              sees the point (tool position + ox, oy)
//      a,b,c,d: image pixels -> mm (dx = a*px + b*py, dy = c*px + d*py), for
//              click-to-jog and measuring on the picture
//      mz:     machine Z at the calibration (the offset and scale are exact at
//              that height)
// Aim: while the WebUI's Camera panel aims with the crosshair, Record & Retrace
// waypoints ($Teach/Add - also the pendant's Record) are taken at the crosshair
// instead of under the tool:
//   $Camera/Aim=1   (renewed by the WebUI; ends by itself after 2.5 minutes)
//   $Camera/Aim=0
//   $Camera         -> [MSG:Camera:url:...] [MSG:Camera:calib:...] [MSG:Camera:aim:0|1]
// Every aim change goes to every channel as [MSG:Camera:aim:0|1].
#include "Camera.h"

#include "Settings.h"
#include "Serial.h"  // allChannels
#include "Logging.h"

#include <Arduino.h>  // millis
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace {
    StringSetting* cam_url   = nullptr;
    StringSetting* cam_calib = nullptr;

    const uint32_t AIM_MS    = 150000;
    bool           aim       = false;
    uint32_t       aimUntil  = 0;

    void tell_aim(bool on) {
        std::string m = std::string("[MSG:Camera:aim:") + (on ? "1" : "0") + "]\n";
        allChannels.print(m.c_str());
    }
}

void make_camera_settings() {
    cam_url   = new StringSetting("Camera address (snapshot / MJPEG URL)", WEBSET, WG, NULL, "Camera/URL", "", 0, 120);
    cam_calib = new StringSetting("Camera calibration: ox,oy,a,b,c,d,mz", WEBSET, WG, NULL, "Camera/Calib", "", 0, 120);
}

bool camera_offset(float& ox, float& oy) {
    if (!cam_calib || !*cam_calib->get()) {
        return false;
    }
    return sscanf(cam_calib->get(), "%f,%f", &ox, &oy) == 2;
}

bool camera_aim_offset(float& ox, float& oy) {
    if (aim && (int32_t)(aimUntil - millis()) <= 0) {
        aim = false;
        tell_aim(false);
    }
    return aim && camera_offset(ox, oy);
}

bool camera_command(const char* line, Channel& out) {
    if (strncasecmp(line, "$Camera", 7) != 0 || (line[7] != 0 && line[7] != '/')) {
        return false;
    }
    const char* cmd = line[7] ? line + 8 : "";
    if (!*cmd) {
        log_stream(out, "[MSG:Camera:url:" << (cam_url ? cam_url->get() : ""));
        log_stream(out, "[MSG:Camera:calib:" << (cam_calib ? cam_calib->get() : ""));
        float ox, oy;
        log_stream(out, "[MSG:Camera:aim:" << (camera_aim_offset(ox, oy) ? 1 : 0));
        return true;
    }
    if (strncasecmp(cmd, "Aim", 3) == 0 && (cmd[3] == 0 || cmd[3] == '=')) {
        bool on = cmd[3] == '=' && atoi(cmd + 4) != 0;
        float ox, oy;
        if (on && !camera_offset(ox, oy)) {
            log_stream(out, "[MSG:Camera:err:calibrate the camera first");
            return true;
        }
        bool was = aim;
        aim      = on;
        aimUntil = millis() + AIM_MS;
        if (was != aim) {
            tell_aim(aim);
        }
        return true;
    }
    return false;  // Camera/URL, Camera/Calib: ordinary settings
}
