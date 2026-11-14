// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ExpanderMonitor.h"

#include <cstdio>
#include <cstring>

bool ExpanderMonitor::feed(uint32_t cp, uint32_t now_ms) {
    if (!isFrame(cp)) {
        return false;
    }
    uint32_t index = (cp - FrameFirst) >> 8;
    uint8_t  b     = uint8_t(cp & 0xff);
    if (index < 4) {
        _buf[index] = b;
        _got |= uint8_t(1u << index);
        return false;
    }
    // index 4: flags, the end of a frame.  A frame with a lost byte is skipped;
    // the next one (at the latest the once-a-second one) is whole again.
    bool whole = _got == 0x0f;
    _got       = 0;
    if (!whole) {
        return false;
    }
    uint32_t io      = uint32_t(_buf[0]) | uint32_t(_buf[1]) << 8 | uint32_t(_buf[2]) << 16 | uint32_t(_buf[3]) << 24;
    bool     changed = !_valid.load() || io != _io.load() || b != _flags.load();
    _io.store(io);
    _flags.store(b);
    _last_ms.store(now_ms);
    if (changed) {
        _seq.fetch_add(1);
    }
    _valid.store(true);
    return changed;
}

bool ExpanderMonitor::online(uint32_t now_ms) const {
    return _valid.load() && (now_ms - _last_ms.load()) < OfflineMs;
}

std::string ExpanderMonitor::field(uint32_t now_ms) const {
    char buf[32];
    if (online(now_ms)) {
        snprintf(buf, sizeof(buf), "%d,%04X,%04X,%02X", _channel, inputs(), outputs(), flags());
    } else {
        snprintf(buf, sizeof(buf), "%d,-", _channel);
    }
    return buf;
}

std::string ExpanderMonitor::stateLine(uint32_t now_ms) const {
    std::string s = "[EXP:" + std::to_string(_channel) + ",ID=" + _id;
    if (online(now_ms)) {
        char buf[40];
        snprintf(buf, sizeof(buf), ",IN=%04X,OUT=%04X,FLAGS=%02X", inputs(), outputs(), flags());
        s += buf;
    } else {
        s += ",OFFLINE";
    }
    s += "]";
    return s;
}

std::vector<ExpanderMonitor*>& ExpanderMonitor::all() {
    static std::vector<ExpanderMonitor*> list;
    return list;
}

std::string ExpanderMonitor::reportField(uint32_t now_ms) {
    std::string s;
    for (auto m : all()) {
        if (!s.empty()) {
            s += ';';
        }
        s += m->field(now_ms);
    }
    return s;
}

uint32_t ExpanderMonitor::reportVersion(uint32_t now_ms) {
    uint32_t v = 0;
    for (auto m : all()) {
        v = v * 31 + (m->seq() << 1) + (m->online(now_ms) ? 1 : 0);
    }
    return v;
}

bool expander_pin_id(const char* pin_name, std::string& id) {
    static const char prefix[] = "uart_channel";
    const size_t      plen     = sizeof(prefix) - 1;
    if (!pin_name || strncmp(pin_name, prefix, plen) != 0) {
        return false;
    }
    const char* p    = pin_name + plen;
    const char* end  = p;
    bool        dot  = false;
    bool        num1 = false, num2 = false;
    for (; *end && *end != ':'; ++end) {
        if (*end == '.') {
            if (dot || !num1) {
                return false;
            }
            dot = true;
        } else if (*end >= '0' && *end <= '9') {
            (dot ? num2 : num1) = true;
        } else {
            return false;
        }
    }
    if (!dot || !num2) {
        return false;
    }
    id.assign(p, end - p);
    return true;
}
