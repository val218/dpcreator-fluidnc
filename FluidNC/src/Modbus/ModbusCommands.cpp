// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Commands for the RS485 bus (see W5500_ETHERNET.md, "RS485 / Modbus commands"):
//
//   $VFD/State ($VFDS)                            [VFD:<name>,id=,answering=,rpm=,errors=,last=]
//   $Modbus/Read=<id>,<fn>,<reg>[,<count>] ($MBR)  [MODBUS:READ:<id>,<fn>,<reg>=<v1>,<v2>,...]
//   $Modbus/Write=<id>,<reg>,<v>[,<v>...] ($MBW)   [MODBUS:WROTE:<id>,<reg>]
//   $Modbus/Write=<id>,coil:<addr>,<0|1>
//   $Modbus/Raw=<hex bytes from the id> ($MBX)     [MODBUS:RX:<hex bytes>]
//   $Modbus/Scan[=<from>-<to>] ($MBS)              [MODBUS:FOUND:<id>] ... [MODBUS:SCAN:done,<n> found]
//
// They use the UART of the VFD spindle, or the modbus_bus: one when there is
// no VFD. Each request holds that UART's lock (Bus::uart_lock), so it never
// overlaps a VFD command or an I/O module's poll.
#include "ModbusBus.h"

#include "Channel.h"
#include "Logging.h"
#include "Protocol.h"  // sys.abort()
#include "Settings.h"
#include "State.h"
#include "System.h"
#include "Spindles/VFD/VFDProtocol.h"
#include "Driver/watchdog.h"  // feed_watchdog

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
    const uint32_t TIMEOUT_MS = 100;  // like the bus
    const uint32_t TRIES      = 3;    // the first try and 2 retries

    Uart* bus_uart() {
        if (Uart* u = Spindles::VFD::VFDProtocol::uart()) {
            return u;
        }
        return Modbus::bus ? Modbus::bus->uart() : nullptr;
    }

    // Moving: a request would hold the protocol task while the planner wants it
    bool moving() {
        return state_is(State::Cycle) || state_is(State::Jog) || state_is(State::Homing);
    }

    // Splits "a,b,c" (spaces allowed around the commas)
    std::vector<std::string> fields(const char* value) {
        std::vector<std::string> out;
        std::string              cur;
        for (const char* p = value; *p; ++p) {
            if (*p == ',') {
                out.push_back(cur);
                cur.clear();
            } else if (*p != ' ' && *p != '\t') {
                cur += *p;
            }
        }
        out.push_back(cur);
        return out;
    }

    // Decimal or 0x hex, within lo..hi
    bool number(const std::string& s, long lo, long hi, long& v) {
        if (s.empty()) {
            return false;
        }
        char*       end;
        const char* p   = s.c_str();
        bool        neg = *p == '-';
        if (neg) {
            ++p;
        }
        int base = (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) ? 16 : 10;
        if (base == 16) {
            p += 2;
        }
        if (!*p) {
            return false;
        }
        unsigned long u = strtoul(p, &end, base);
        if (*end || u > 0x7fffffffUL) {
            return false;
        }
        v = neg ? -long(u) : long(u);
        return v >= lo && v <= hi;
    }

    std::string hex(const uint8_t* b, size_t n) {
        std::string s;
        char        buf[4];
        for (size_t i = 0; i < n; ++i) {
            snprintf(buf, sizeof(buf), i ? " %02X" : "%02X", b[i]);
            s += buf;
        }
        return s;
    }

    // One request. Returns the answer length (no CRC), 0 after an error line.
    size_t ask(Channel& out, Uart* uart, uint8_t id, const uint8_t* tx, size_t txlen, uint8_t* rx, size_t rxmax, Error& rc) {
        const char* err;
        size_t      n = Modbus::Bus::raw(uart, tx, txlen, rx, rxmax, TIMEOUT_MS, TRIES, err);
        if (!n) {
            log_stream(out, "[MODBUS:ERR:" << int(id) << "," << err);
            rc = Error::MessageFailed;
            return 0;
        }
        return n;
    }

    // A standard answer: exception, or the function echoed. Prints the error line.
    bool standard(Channel& out, uint8_t id, uint8_t fn, const uint8_t* rx, size_t n, Error& rc) {
        if (n >= 3 && rx[1] == (fn | 0x80)) {
            log_stream(out, "[MODBUS:ERR:" << int(id) << ",exception " << int(rx[2]));
            rc = Error::MessageFailed;
            return false;
        }
        if (rx[1] != fn) {
            log_stream(out, "[MODBUS:ERR:" << int(id) << ",unexpected function " << int(rx[1]));
            rc = Error::MessageFailed;
            return false;
        }
        return true;
    }

    Error no_bus(Channel& out) {
        log_stream(out, "[MODBUS:ERR:no bus");
        return Error::MessageFailed;
    }

    // ---- $VFD/State

    Error vfd_state(const char* value, AuthenticationLevel auth_level, Channel& out) {
        Spindles::VFD::VFDProtocol::Health h;
        if (!Spindles::VFD::VFDProtocol::health(h)) {
            log_info_to(out, "No VFD spindle");
            return Error::Ok;
        }
        std::string last;
        for (char c : h.last) {
            last += (c == ']' || c == ',' || c == '|') ? ' ' : c;
        }
        log_stream(out,
                   "[VFD:" << h.name << ",id=" << int(h.id) << ",answering=" << (h.answering ? 1 : 0) << ",rpm=" << h.rpm
                           << ",errors=" << h.errors << ",last=" << last);
        return Error::Ok;
    }

    // ---- $Modbus/Read

    Error modbus_read(const char* value, AuthenticationLevel auth_level, Channel& out) {
        if (!value || !*value) {
            log_info_to(out, "Usage: $Modbus/Read=<id>,<function 1-4>,<register>[,<count 1-32>]  (register decimal or 0x hex)");
            return Error::Ok;
        }
        if (moving()) {
            return Error::IdleError;
        }
        auto f = fields(value);
        long id, fn, reg, count = 1;
        if ((f.size() != 3 && f.size() != 4) || !number(f[0], 0, 247, id) || !number(f[1], 1, 4, fn) || !number(f[2], 0, 0xffff, reg) ||
            (f.size() == 4 && !number(f[3], 1, 32, count))) {
            return Error::InvalidValue;
        }
        Uart* uart = bus_uart();
        if (!uart) {
            return no_bus(out);
        }
        uint8_t tx[6] = { uint8_t(id), uint8_t(fn), uint8_t(reg >> 8), uint8_t(reg), uint8_t(count >> 8), uint8_t(count) };
        uint8_t rx[80];
        Error   rc = Error::Ok;
        size_t  n  = ask(out, uart, tx[0], tx, sizeof(tx), rx, sizeof(rx), rc);
        if (!n || !standard(out, tx[0], tx[1], rx, n, rc)) {
            return rc;
        }
        size_t want = fn <= 2 ? size_t(count + 7) / 8 : size_t(count) * 2;
        if (rx[2] != want || n != 3 + want) {
            log_stream(out, "[MODBUS:ERR:" << id << ",unexpected length");
            return Error::MessageFailed;
        }
        std::string vals;
        for (long i = 0; i < count; ++i) {
            long v = fn <= 2 ? (rx[3 + i / 8] >> (i % 8)) & 1 : (long(rx[3 + 2 * i]) << 8) | rx[4 + 2 * i];
            if (i) {
                vals += ',';
            }
            vals += std::to_string(v);
        }
        log_stream(out, "[MODBUS:READ:" << id << "," << fn << "," << reg << "=" << vals);
        return Error::Ok;
    }

    // ---- $Modbus/Write

    Error modbus_write(const char* value, AuthenticationLevel auth_level, Channel& out) {
        if (!value || !*value) {
            log_info_to(out, "Usage: $Modbus/Write=<id>,<register>,<value>[,<value>...] or <id>,coil:<address>,<0|1>");
            return Error::Ok;
        }
        auto f = fields(value);
        long id;
        if (f.size() < 3 || !number(f[0], 0, 247, id)) {
            return Error::InvalidValue;
        }
        Uart* uart = bus_uart();
        uint8_t tx[80];
        size_t  txlen;
        long    reg;
        bool    coil = f[1].rfind("coil:", 0) == 0;
        if (coil) {
            long on;
            if (f.size() != 3 || !number(f[1].substr(5), 0, 0xffff, reg) || !number(f[2], 0, 1, on)) {
                return Error::InvalidValue;
            }
            uint8_t t[] = { uint8_t(id), 5, uint8_t(reg >> 8), uint8_t(reg), uint8_t(on ? 0xFF : 0), 0 };
            memcpy(tx, t, sizeof(t));
            txlen = sizeof(t);
        } else {
            if (!number(f[1], 0, 0xffff, reg)) {
                return Error::InvalidValue;
            }
            std::vector<uint16_t> vals;
            for (size_t i = 2; i < f.size(); ++i) {
                long v;
                if (!number(f[i], -32768, 0xffff, v)) {
                    return Error::InvalidValue;
                }
                vals.push_back(uint16_t(v));
            }
            if (vals.size() > 32) {
                return Error::InvalidValue;
            }
            tx[0] = uint8_t(id);
            tx[2] = uint8_t(reg >> 8);
            tx[3] = uint8_t(reg);
            if (vals.size() == 1) {
                tx[1] = 6;
                tx[4] = vals[0] >> 8;
                tx[5] = vals[0] & 0xFF;
                txlen = 6;
            } else {
                tx[1] = 16;
                tx[4] = 0;
                tx[5] = uint8_t(vals.size());
                tx[6] = uint8_t(vals.size() * 2);
                for (size_t i = 0; i < vals.size(); ++i) {
                    tx[7 + 2 * i] = vals[i] >> 8;
                    tx[8 + 2 * i] = vals[i] & 0xFF;
                }
                txlen = 7 + vals.size() * 2;
            }
        }
        if (!uart) {
            return no_bus(out);
        }
        uint8_t rx[16];
        Error   rc = Error::Ok;
        size_t  n  = ask(out, uart, tx[0], tx, txlen, rx, sizeof(rx), rc);
        if (!n || !standard(out, tx[0], tx[1], rx, n, rc)) {
            return rc;
        }
        log_stream(out, "[MODBUS:WROTE:" << id << "," << (coil ? "coil:" : "") << reg);
        return Error::Ok;
    }

    // ---- $Modbus/Raw

    Error modbus_raw(const char* value, AuthenticationLevel auth_level, Channel& out) {
        if (!value || !*value) {
            log_info_to(out, "Usage: $Modbus/Raw=<hex bytes from the id, without CRC>  e.g. 01 03 00 00 00 01");
            return Error::Ok;
        }
        if (moving()) {
            return Error::IdleError;
        }
        uint8_t tx[64];
        size_t  txlen = 0;
        int     nib = -1;
        for (const char* p = value; *p; ++p) {
            int d;
            if (*p >= '0' && *p <= '9') {
                d = *p - '0';
            } else if (*p >= 'a' && *p <= 'f') {
                d = *p - 'a' + 10;
            } else if (*p >= 'A' && *p <= 'F') {
                d = *p - 'A' + 10;
            } else if (*p == ' ' || *p == ',' || *p == ':') {
                if (nib >= 0) {
                    return Error::InvalidValue;  // one hex digit alone
                }
                continue;
            } else {
                return Error::InvalidValue;
            }
            if (nib < 0) {
                nib = d;
            } else {
                if (txlen >= sizeof(tx)) {
                    return Error::InvalidValue;
                }
                tx[txlen++] = uint8_t(nib << 4 | d);
                nib         = -1;
            }
        }
        if (nib >= 0 || txlen < 2) {
            return Error::InvalidValue;
        }
        Uart* uart = bus_uart();
        if (!uart) {
            return no_bus(out);
        }
        uint8_t rx[128];
        Error   rc = Error::Ok;
        size_t  n  = ask(out, uart, tx[0], tx, txlen, rx, sizeof(rx), rc);
        if (!n) {
            return rc;
        }
        log_stream(out, "[MODBUS:RX:" << hex(rx, n));
        return Error::Ok;
    }

    // ---- $Modbus/Scan

    Error modbus_scan(const char* value, AuthenticationLevel auth_level, Channel& out) {
        long from = 1, to = 16;
        if (value && *value) {
            std::string s(value);
            auto        dash = s.find('-');
            if (dash == std::string::npos || !number(s.substr(0, dash), 1, 247, from) || !number(s.substr(dash + 1), 1, 247, to) || to < from) {
                return Error::InvalidValue;
            }
        }
        Uart* uart = bus_uart();
        if (!uart) {
            return no_bus(out);
        }
        int found = 0;
        for (long id = from; id <= to; ++id) {
            if (sys.abort()) {
                break;
            }
            feed_watchdog();  // the protocol task is watched; 247 ids take ~30 s
            uint8_t     tx[6] = { uint8_t(id), 3, 0, 0, 0, 1 };
            uint8_t     rx[16];
            const char* err;
            // One try each (~120 ms for an id with nothing there). Any
            // answer from that id, an exception too, means a device is there.
            if (Modbus::Bus::raw(uart, tx, sizeof(tx), rx, sizeof(rx), TIMEOUT_MS, 1, err)) {
                log_stream(out, "[MODBUS:FOUND:" << id);
                ++found;
            }
        }
        log_stream(out, "[MODBUS:SCAN:done," << found << " found");
        return Error::Ok;
    }
}

void make_modbus_commands() {
    new ReportCommand("VFDS", "VFD/State", vfd_state, anyState);
    // These wait for the drive's answer (up to ~0.4 s, a scan longer), so
    // they run on the protocol task, and not while the machine moves (the
    // usage line, which the WebUI asks for to see that they exist, any time).
    new UserCommand("MBR", "Modbus/Read", modbus_read, anyState);
    new UserCommand("MBX", "Modbus/Raw", modbus_raw, anyState);
    // A write can change how the drive runs; a scan pauses the bus.
    new UserCommand("MBW", "Modbus/Write", modbus_write, notIdleOrAlarm);
    new UserCommand("MBS", "Modbus/Scan", modbus_scan, notIdleOrAlarm);
}
