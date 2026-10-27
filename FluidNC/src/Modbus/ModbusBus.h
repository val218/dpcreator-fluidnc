// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// ModbusBus - one RS485 (Modbus RTU) bus shared by a VFD spindle and any
// number of Modbus I/O modules (extenders: pinextN: modbus_io:).
//
//   modbus_bus:
//     uart_num: 2      # the uartN: section (baud, pins, rts_pin = DE)
//     poll_ms: 20      # how often the I/O modules are read
//
// The bus owns the UART's time: every transaction (a VFD command or an I/O
// read/write) holds the bus lock, so requests never overlap on the wire.
// The VFD goes first: before each I/O transaction the poller steps aside
// while the VFD has commands waiting, so a spindle command waits at most for
// one I/O transaction already on the wire. Without a modbus_bus: section
// nothing changes (the VFD has its UART to itself, as before).
#pragma once

#include "Configuration/Configurable.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class Uart;
class Channel;

namespace Modbus {
    // Something polled on the bus: a Modbus I/O module.
    class Device {
    public:
        virtual void        poll()       = 0;  // read inputs, write changed outputs (bus task)
        virtual void        report(Channel& out) = 0;  // $Modbus/Values
        virtual uint8_t     id() const   = 0;
        // Analog values (Request 2)
        virtual bool        value(const std::string& name, float& v) { return false; }
        virtual bool        analog_in(int index, float& v) { return false; }
        virtual bool        analog_out(int index, float percent) { return false; }
        virtual bool        has_analog_out(int index) const { return false; }
        virtual bool        has_analog_in(int index) const { return false; }
        virtual void        values_line(std::string& s) {}  // name=value,... for |MB:
        virtual ~Device() {}
    };

    class Bus : public Configuration::Configurable {
        int32_t  _uart_num = 2;
        uint32_t _poll_ms  = 20;
        uint32_t _timeout_ms = 100;
        uint32_t _retries  = 2;
        int32_t  _debug    = 0;

        Uart*                _uart = nullptr;
        std::vector<Device*> _devices;
        bool                 _started = false;

        static void task(void* arg);

    public:
        Bus() = default;

        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;
        void init();

        // Modbus I/O modules register here when they are set up.
        void add(Device* d);
        void start();  // the poll task, once every module is added (Main.cpp)

        // One request/response on the wire: adds the address byte's CRC,
        // retries, checks the answer (address, function, length, CRC).
        // `expect` is the answer length without the CRC (0: variable, read
        // the byte count from rx[2]). Returns the answer length without CRC,
        // or 0 on no / bad answer. A Modbus exception answer returns 0.
        size_t transact(uint8_t* tx, size_t txlen, uint8_t* rx, size_t rxmax, size_t expect);

        uint32_t poll_ms() const { return _poll_ms; }
        int32_t  debug() const { return _debug; }
        Uart*    uart() const { return _uart; }

        // The lock a VFD on this UART must hold for each transaction (nullptr: no shared bus).
        static std::recursive_mutex* lock_for(Uart* uart);

        // Values for G-code / reports, from every module.
        static bool get_value(const std::string& name, float& v);  // #<_mb_name>
        static bool analog_in(int index, float& v);                 // M66 E<n>
        static bool analog_out(int index, float percent);           // M67 / M68 E<n>
        static bool has_analog_out(int index);
        static bool has_analog_in(int index);
        static void report_values(Channel& out);                    // $Modbus/Values
        static std::string status_values();                         // for |MB:
        static bool        active();

        static uint16_t crc(const uint8_t* buf, size_t len);

        ~Bus();
    };

    extern Bus* bus;  // the configured bus, or nullptr
}
