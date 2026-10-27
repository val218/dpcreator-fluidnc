// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ModbusBus.h"

#include "Machine/MachineConfig.h"
#include "Uart.h"
#include "Channel.h"
#include "Logging.h"
#include "Report.h"  // hex_msg
#include "Spindles/VFD/VFDProtocol.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>
#include <cstring>
#include <cstdio>

namespace Modbus {
    Bus* bus = nullptr;

    namespace {
        std::recursive_mutex bus_lock;
        TaskHandle_t         bus_task = nullptr;
        std::atomic<bool>    kicked { false };  // an output changed: write it now
    }

    void Bus::group(Configuration::HandlerBase& handler) {
        // @config uart_num
        // @default 2
        // The uartN: section of the RS485 bus (baud, tx/rx pins, rts_pin drives DE).
        // A VFD spindle on the same bus uses the same uart_num.
        handler.item("uart_num", _uart_num, 1, MAX_N_UARTS - 1);

        // @config poll_ms
        // @default 20
        // How often each Modbus I/O module is read, in milliseconds. At 9600 baud one
        // read takes 15-20 ms on the wire, so with several modules (or a VFD) the
        // real cycle is longer; use 19200 or 38400 baud for faster I/O.
        handler.item("poll_ms", _poll_ms, 5, 10000);

        // @config timeout_ms
        // @default 100
        // How long to wait for a module's answer before trying again.
        handler.item("timeout_ms", _timeout_ms, 10, 2000);

        // @config retries
        // @default 2
        // Tries per request before the poll counts as unanswered.
        handler.item("retries", _retries, 1, 10);

        // @config debug
        // @default 0
        // 1: report errors, 2: also each request and answer in hex.
        handler.item("debug", _debug, 0, 2);
    }

    void Bus::afterParse() {
        bus = this;
    }

    void Bus::init() {
        bus   = this;
        _uart = config->_uarts[_uart_num];
        if (!_uart) {
            log_error("modbus_bus: missing uart" << _uart_num << " section");
            return;
        }
        if (!_uart->configured()) {
            log_error("modbus_bus: uart" << _uart_num << " failed configuration");
            _uart = nullptr;
            return;
        }
        if (_uart->setHalfDuplex()) {
            log_warn("modbus_bus: RS485 half duplex could not be set");
        }
        log_info("Modbus bus on uart" << _uart_num << " poll:" << _poll_ms << "ms");
    }

    void Bus::add(Device* d) {
        _devices.push_back(d);
        if (!_started && _uart) {
            _started = true;
            // Same priority and core as the VFD task: the I/O is never more
            // important than motion, and the VFD jumps the queue (see task()).
            xTaskCreatePinnedToCore(task, "modbus_bus", 4096, this, 1, &bus_task, SUPPORT_TASK_CORE);  // ~4 KB, only when modules exist
        }
    }

    std::recursive_mutex* Bus::lock_for(Uart* uart) {
        return (bus && bus->_uart && bus->_uart == uart) ? &bus_lock : nullptr;
    }

    uint16_t Bus::crc(const uint8_t* buf, size_t len) {
        uint16_t c = 0xFFFF;
        for (size_t pos = 0; pos < len; pos++) {
            c ^= uint16_t(buf[pos]);
            for (int i = 0; i < 8; i++) {
                c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1;
            }
        }
        return c;
    }

    size_t Bus::transact(uint8_t* tx, size_t txlen, uint8_t* rx, size_t rxmax, size_t expect) {
        if (!_uart || txlen + 2 > 260) {
            return 0;
        }
        uint8_t msg[262];
        memcpy(msg, tx, txlen);
        uint16_t c    = crc(msg, txlen);
        msg[txlen]    = c & 0xFF;
        msg[txlen + 1] = c >> 8;
        size_t     n  = txlen + 2;
        TickType_t to = _timeout_ms / portTICK_PERIOD_MS;
        if (to == 0) {
            to = 1;
        }

        for (uint32_t attempt = 0; attempt < _retries; ++attempt) {
            // The VFD goes first: wait (up to ~0.5 s) while it has commands queued.
            for (int i = 0; i < 100; ++i) {
                if (!Spindles::VFD::VFDProtocol::commands_waiting()) {
                    break;
                }
                vTaskDelay(5 / portTICK_PERIOD_MS);
            }
            size_t got = 0;
            {
                std::lock_guard<std::recursive_mutex> l(bus_lock);
                if (_debug > 1) {
                    hex_msg(msg, "Modbus Tx:", n);
                }
                _uart->flushRx();
                _uart->write(msg, n);
                _uart->flushTxTimed(to);
                // Header first (address, function, byte count / first data byte)
                got = _uart->timedReadBytes(rx, 3, to);
                if (got == 3) {
                    size_t want;
                    if (rx[1] & 0x80) {
                        want = 5;  // exception: id, fn|0x80, code, crc
                    } else if (expect) {
                        want = expect + 2;
                    } else {
                        want = 3 + rx[2] + 2;  // read answers: byte count in rx[2]
                    }
                    if (want > rxmax) {
                        want = rxmax;
                    }
                    if (want > 3) {
                        got += _uart->timedReadBytes(rx + 3, want - 3, to);
                    }
                }
                if (_debug > 1 && got) {
                    hex_msg(rx, "Modbus Rx:", got);
                }
            }
            if (got >= 5 && rx[0] == tx[0]) {
                uint16_t rc = crc(rx, got - 2);
                if (rx[got - 2] == (rc & 0xFF) && rx[got - 1] == (rc >> 8)) {
                    if (rx[1] & 0x80) {
                        if (_debug) {
                            log_info("Modbus " << int(tx[0]) << " exception " << int(rx[2]) << " to function " << int(tx[1]));
                        }
                        return 0;  // a clear "no": retrying will not help
                    }
                    if (rx[1] == tx[1]) {
                        return got - 2;
                    }
                }
            }
            if (_debug) {
                log_info("Modbus " << int(tx[0]) << (got ? " bad answer" : " no answer") << " (try " << (attempt + 1) << ")");
            }
            vTaskDelay(5 / portTICK_PERIOD_MS);
        }
        return 0;
    }

    void Bus::task(void* arg) {
        auto self = static_cast<Bus*>(arg);
        while (true) {
            TickType_t start = xTaskGetTickCount();
            for (auto d : self->_devices) {
                d->poll();
            }
            // Wait for the rest of poll_ms, or less if an output change asks
            // for a prompt write.
            TickType_t per = self->_poll_ms / portTICK_PERIOD_MS;
            do {
                vTaskDelay(2 / portTICK_PERIOD_MS ? 2 / portTICK_PERIOD_MS : 1);
            } while (!kicked.exchange(false) && xTaskGetTickCount() - start < per);
        }
    }

    // ---- values for G-code and reports

    bool Bus::active() {
        return bus && !bus->_devices.empty();
    }
    bool Bus::get_value(const std::string& name, float& v) {
        if (!bus) {
            return false;
        }
        for (auto d : bus->_devices) {
            if (d->value(name, v)) {
                return true;
            }
        }
        return false;
    }
    bool Bus::analog_in(int index, float& v) {
        if (!bus) {
            return false;
        }
        for (auto d : bus->_devices) {
            if (d->analog_in(index, v)) {
                return true;
            }
        }
        return false;
    }
    bool Bus::has_analog_in(int index) {
        if (!bus) {
            return false;
        }
        for (auto d : bus->_devices) {
            if (d->has_analog_in(index)) {
                return true;
            }
        }
        return false;
    }
    bool Bus::analog_out(int index, float percent) {
        if (!bus) {
            return false;
        }
        for (auto d : bus->_devices) {
            if (d->analog_out(index, percent)) {
                kicked = true;
                return true;
            }
        }
        return false;
    }
    bool Bus::has_analog_out(int index) {
        if (!bus) {
            return false;
        }
        for (auto d : bus->_devices) {
            if (d->has_analog_out(index)) {
                return true;
            }
        }
        return false;
    }
    void Bus::report_values(Channel& out) {
        if (!active()) {
            log_stream(out, "[MSG:INFO: No Modbus I/O modules");
            return;
        }
        for (auto d : bus->_devices) {
            d->report(out);
        }
    }
    std::string Bus::status_values() {
        std::string s;
        if (!bus) {
            return s;
        }
        for (auto d : bus->_devices) {
            d->values_line(s);
        }
        return s;
    }

    Bus::~Bus() {
        if (bus == this) {
            bus = nullptr;
        }
    }
}

// Wake the bus task for a prompt write (an output changed).
void modbus_bus_kick() {
    Modbus::kicked = true;
}
