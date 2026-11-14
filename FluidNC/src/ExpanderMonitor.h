// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Live I/O state of a dpCREATOR IO expander (IO16) on a uart_channel.
//
// After [EXP:MON=1] the expander sends a 5-codepoint frame on every change
// (at most every 20 ms) and once a second: U+A000 + (index << 8) + byte,
//   index 0 = IN1-8, 1 = IN9-16, 2 = OUT1-8, 3 = OUT9-16 (bit 0 = lowest number)
//   index 4 = flags (bit0 output rail on, bit1 configured, bit2 status reports
//             arriving, bit3 config error) - always last, so it publishes.
//
// This file is pure logic (no FreeRTOS, no channel) so the unit tests can use it.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

class ExpanderMonitor {
public:
    static constexpr uint32_t FrameFirst = 0xA000;
    static constexpr uint32_t FrameLast  = 0xA4FF;
    static constexpr uint32_t OfflineMs  = 3000;  // no frame for this long: shown as offline

    static bool isFrame(uint32_t cp) { return cp >= FrameFirst && cp <= FrameLast; }

    ExpanderMonitor(int channel, const std::string& id) : _channel(channel), _id(id) {}

    // One frame codepoint.  Returns true when it completed a frame that changed
    // the snapshot (or was the first one).
    bool feed(uint32_t cp, uint32_t now_ms);

    bool     valid() const { return _valid.load(); }
    bool     online(uint32_t now_ms) const;
    uint16_t inputs() const { return uint16_t(_io.load() & 0xffff); }
    uint16_t outputs() const { return uint16_t(_io.load() >> 16); }
    uint8_t  flags() const { return _flags.load(); }
    uint32_t seq() const { return _seq.load(); }
    int      channel() const { return _channel; }

    const std::string& id() const { return _id; }

    // "2,00A1,0003,07", or "2,-" when offline / no frame yet
    std::string field(uint32_t now_ms) const;
    // "[EXP:2,ID=...,IN=00A1,OUT=0003,FLAGS=07]" (OFFLINE instead of IN/OUT/FLAGS)
    std::string stateLine(uint32_t now_ms) const;

    // All monitored expanders, in the order they were registered.
    static std::vector<ExpanderMonitor*>& all();
    // The |EXP: status field value: the expanders' fields joined by ';'; "" when none.
    static std::string reportField(uint32_t now_ms);
    // Changes whenever reportField() would change (a new snapshot, going offline).
    static uint32_t reportVersion(uint32_t now_ms);

private:
    int         _channel;
    std::string _id;

    uint8_t _buf[4] = { 0, 0, 0, 0 };
    uint8_t _got    = 0;  // bit n: _buf[n] received since the last publish

    std::atomic<uint32_t> _io { 0 };  // inputs | outputs << 16
    std::atomic<uint8_t>  _flags { 0 };
    std::atomic<uint32_t> _last_ms { 0 };
    std::atomic<bool>     _valid { false };
    std::atomic<uint32_t> _seq { 0 };
};

// "uart_channel2.16:low" -> "2.16"; false for any other pin name.
bool expander_pin_id(const char* pin_name, std::string& id);
