// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ModbusIO.h"

#include "Extenders.h"
#include "Machine/InputPin.h"
#include "Machine/MachineConfig.h"
#include "Protocol.h"       // protocol_send_event, pinActiveEvent
#include "MotionControl.h"  // mc_critical
#include "Channel.h"
#include "Logging.h"
#include "string_util.h"

#include <cstdio>
#include <cmath>

void modbus_bus_kick();

namespace Extenders {
    void ModbusIO::AnalogIn::group(Configuration::HandlerBase& handler) {
        // @config register
        // @default -1
        // Register number (0-based) to read.
        handler.item("register", _register, -1, 65535);
        // @config function
        // @default 4
        // 4 reads an input register, 3 a holding register.
        handler.item("function", _function, 3, 4);
        // @config name
        // @default ""
        // G-code parameter name: the value is #<_mb_NAME>, and $Modbus/Values shows it.
        handler.item("name", _name, 0, 24);
        // @config scale
        // @default 1.0
        // value = raw x scale + offset
        handler.item("scale", _scale);
        // @config offset
        // @default 0.0
        handler.item("offset", _offset);
        // @config signed
        // @default false
        // The register holds a signed (two's complement) number.
        handler.item("signed", _signed);
        // @config index
        // @default -1
        // Also readable with M66 E<index> L0 (0-3); -1: by name only.
        handler.item("index", _index, -1, 3);
    }

    void ModbusIO::AnalogOut::group(Configuration::HandlerBase& handler) {
        // @config register
        // @default -1
        // Holding register (0-based) written with function 6.
        handler.item("register", _register, -1, 65535);
        // @config index
        // @default 0
        // M67 / M68 E<index> Q<percent> writes it (0-3).
        handler.item("index", _index, 0, 3);
        // @config max
        // @default 1000
        // The register value for Q100 (Q0 writes 0).
        handler.item("max", _max, 1, 65535);
    }

    void ModbusIO::group(Configuration::HandlerBase& handler) {
        // @config modbus_id
        // @default 1
        // The module's Modbus address on the bus (different from the VFD's).
        handler.item("modbus_id", _id, 1, 247);
        // @config inputs
        // @default 8
        // Number of digital inputs: pins pinextN.0 .. inputs-1.
        handler.item("inputs", _inputs, 0, 32);
        // @config outputs
        // @default 8
        // Number of digital outputs: the pins after the inputs.
        handler.item("outputs", _outputs, 0, 32);
        // @config input_function
        // @default 2
        // 2 read discrete inputs, 1 read coils, 4 input registers / 3 holding registers (16 inputs per register, bit 0 first).
        handler.item("input_function", _input_function, 1, 4);
        // @config input_address
        // @default 0
        // First input / register number on the module.
        handler.item("input_address", _input_address, 0, 65535);
        // @config output_function
        // @default 15
        // 15 write multiple coils, 5 write single coil, 6 / 16 holding register(s) (16 outputs per register).
        handler.item("output_function", _output_function, 5, 16);
        // @config output_address
        // @default 0
        // First output coil / register number on the module.
        handler.item("output_address", _output_address, 0, 65535);
        // @config fail_alarm
        // @default false
        // true: a module that stops answering raises an alarm instead of only a message.
        handler.item("fail_alarm", _fail_alarm);
        for (int i = 0; i < MAX_AIN; ++i) {
            handler.section(("ain" + std::to_string(i)).c_str(), _ain[i], i);
        }
        for (int i = 0; i < MAX_AOUT; ++i) {
            handler.section(("aout" + std::to_string(i)).c_str(), _aout[i], i);
        }
    }

    void ModbusIO::validate() {
        if (_inputs + _outputs > MAX_PINS) {
            log_config_error("modbus_io: at most " << MAX_PINS << " pins");
        }
        if (!(_output_function == 5 || _output_function == 15 || _output_function == 6 || _output_function == 16)) {
            log_config_error("modbus_io: output_function must be 5, 15, 6 or 16");
        }
    }

    void ModbusIO::init() {
        if (!Modbus::bus) {
            log_error("modbus_io " << _id << ": needs a modbus_bus: section");
            return;
        }
        log_info("Modbus I/O id:" << _id << " inputs:" << _inputs << " outputs:" << _outputs);
        for (auto a : _ain) {
            if (a && a->_register >= 0) {
                log_info("  ain register:" << a->_register << " name:" << a->_name << (a->_index >= 0 ? " M66 E" : "")
                                           << (a->_index >= 0 ? std::to_string(a->_index) : ""));
            }
        }
        for (auto a : _aout) {
            if (a && a->_register >= 0) {
                log_info("  aout register:" << a->_register << " M67/M68 E" << a->_index << " max:" << a->_max);
            }
        }
        Modbus::bus->add(this);
    }

    void ModbusIO::claim(pinnum_t index) {
        Assert(index < _inputs + _outputs, "modbus_io %d has no pin %d (inputs:%d outputs:%d)", int(_id), int(index), int(_inputs), int(_outputs));
        Assert(!(_claimed & (1ULL << index)), "modbus_io pin %d is used twice", int(index));
        _claimed |= 1ULL << index;
    }
    void ModbusIO::free(pinnum_t index) {
        _claimed &= ~(1ULL << index);
    }

    void ModbusIO::setupPin(pinnum_t index, Pins::PinAttributes attr) {
        bool input = index < _inputs;
        if (attr.has(Pins::PinAttributes::Input)) {
            Assert(input, "modbus_io pin %d is an output (inputs are 0-%d)", int(index), int(_inputs) - 1);
            _is_input |= 1ULL << index;
        }
        if (attr.has(Pins::PinAttributes::Output)) {
            Assert(!input, "modbus_io pin %d is an input (outputs start at %d)", int(index), int(_inputs));
        }
    }

    void ModbusIO::writePin(pinnum_t index, bool high) {
        if (index < _inputs) {
            return;
        }
        uint64_t bit = 1ULL << (index - _inputs);
        if (high) {
            _out_want |= bit;
        } else {
            _out_want &= ~bit;
        }
    }
    bool ModbusIO::readPin(pinnum_t index) {
        return index < _inputs && ((_in_bits.load() >> index) & 1);
    }
    void ModbusIO::flushWrites() {
        modbus_bus_kick();  // write it now instead of at the next poll
    }
    void ModbusIO::registerEvent(pinnum_t index, InputPin* obj) {
        if (index < MAX_PINS) {
            _events[index] = obj;
        }
    }

    // ---- bus task

    bool ModbusIO::read_inputs() {
        if (!_inputs) {
            return true;
        }
        uint8_t  tx[6], rx[80];
        bool     regs  = _input_function >= 3;
        uint16_t count = regs ? (_inputs + 15) / 16 : _inputs;
        tx[0]          = _id;
        tx[1]          = _input_function;
        tx[2]          = _input_address >> 8;
        tx[3]          = _input_address & 0xFF;
        tx[4]          = count >> 8;
        tx[5]          = count & 0xFF;
        size_t n       = Modbus::bus->transact(tx, 6, rx, sizeof(rx), 0);
        if (n < 3 || n < size_t(3 + rx[2])) {
            return false;
        }
        uint64_t bits = 0;
        if (regs) {
            for (int r = 0; r < count; ++r) {
                uint16_t v = (rx[3 + 2 * r] << 8) | rx[4 + 2 * r];
                bits |= uint64_t(v) << (16 * r);
            }
        } else {
            for (int b = 0; b < rx[2]; ++b) {
                bits |= uint64_t(rx[3 + b]) << (8 * b);
            }
        }
        if (_inputs < 64) {
            bits &= (1ULL << _inputs) - 1;
        }
        uint64_t old = _in_bits.exchange(bits);
        uint64_t chg = _first_read ? ((1ULL << _inputs) - 1) : (old ^ bits);
        _first_read  = false;
        for (uint32_t i = 0; i < _inputs && chg; ++i) {
            if ((chg >> i) & 1) {
                chg &= ~(1ULL << i);
                if (auto p = _events[i]) {
                    bool active = (bits >> i) & 1;
                    if (p->getAttr().has(Pins::PinAttributes::ActiveLow)) {
                        active = !active;
                    }
                    protocol_send_event(active ? &pinActiveEvent : &pinInactiveEvent, p);
                }
            }
        }
        return true;
    }

    bool ModbusIO::write_outputs() {
        if (!_outputs) {
            return true;
        }
        uint64_t want = _out_want.load();
        if (!_out_never && want == _out_sent) {
            return true;
        }
        uint8_t rx[16];
        if (_output_function == 5) {
            // one coil per request, only the ones that changed
            uint64_t chg = _out_never ? ((1ULL << _outputs) - 1) : (want ^ _out_sent);
            for (uint32_t i = 0; i < _outputs; ++i) {
                if (!((chg >> i) & 1)) {
                    continue;
                }
                uint16_t addr = _output_address + i;
                bool     on   = (want >> i) & 1;
                uint8_t  tx[6] = { uint8_t(_id), 5, uint8_t(addr >> 8), uint8_t(addr & 0xFF), uint8_t(on ? 0xFF : 0), 0 };
                if (!Modbus::bus->transact(tx, 6, rx, sizeof(rx), 6)) {
                    return false;
                }
                _out_sent = (_out_sent & ~(1ULL << i)) | (uint64_t(on) << i);
            }
        } else if (_output_function == 15) {
            uint8_t nbytes = (_outputs + 7) / 8;
            uint8_t tx[7 + 8];
            tx[0] = _id;
            tx[1] = 15;
            tx[2] = _output_address >> 8;
            tx[3] = _output_address & 0xFF;
            tx[4] = _outputs >> 8;
            tx[5] = _outputs & 0xFF;
            tx[6] = nbytes;
            for (int b = 0; b < nbytes; ++b) {
                tx[7 + b] = (want >> (8 * b)) & 0xFF;
            }
            if (!Modbus::bus->transact(tx, 7 + nbytes, rx, sizeof(rx), 6)) {
                return false;
            }
            _out_sent = want;
        } else {
            // holding registers, 16 outputs each
            int nregs = (_outputs + 15) / 16;
            if (_output_function == 6) {
                for (int r = 0; r < nregs; ++r) {
                    uint16_t v     = (want >> (16 * r)) & 0xFFFF;
                    uint16_t addr  = _output_address + r;
                    uint8_t  tx[6] = { uint8_t(_id), 6, uint8_t(addr >> 8), uint8_t(addr & 0xFF), uint8_t(v >> 8), uint8_t(v & 0xFF) };
                    if (!Modbus::bus->transact(tx, 6, rx, sizeof(rx), 6)) {
                        return false;
                    }
                }
            } else {
                uint8_t tx[7 + 4];
                tx[0] = _id;
                tx[1] = 16;
                tx[2] = _output_address >> 8;
                tx[3] = _output_address & 0xFF;
                tx[4] = 0;
                tx[5] = nregs;
                tx[6] = nregs * 2;
                for (int r = 0; r < nregs; ++r) {
                    uint16_t v     = (want >> (16 * r)) & 0xFFFF;
                    tx[7 + 2 * r]  = v >> 8;
                    tx[8 + 2 * r]  = v & 0xFF;
                }
                if (!Modbus::bus->transact(tx, 7 + nregs * 2, rx, sizeof(rx), 6)) {
                    return false;
                }
            }
            _out_sent = want;
        }
        _out_never = false;
        return true;
    }

    bool ModbusIO::read_analog() {
        bool ok = true;
        for (auto a : _ain) {
            if (!a || a->_register < 0) {
                continue;
            }
            uint8_t tx[6] = { uint8_t(_id), uint8_t(a->_function), uint8_t(a->_register >> 8), uint8_t(a->_register & 0xFF), 0, 1 };
            uint8_t rx[16];
            size_t  n = Modbus::bus->transact(tx, 6, rx, sizeof(rx), 0);
            if (n < 5 || rx[2] < 2) {
                ok = false;
                continue;
            }
            uint16_t raw = (rx[3] << 8) | rx[4];
            float    v   = a->_signed ? float(int16_t(raw)) : float(raw);
            a->_value    = v * a->_scale + a->_offset;
            a->_have     = true;
        }
        return ok;
    }

    bool ModbusIO::write_analog() {
        bool ok = true;
        for (auto a : _aout) {
            if (!a || a->_register < 0) {
                continue;
            }
            uint32_t want = a->_want.load();
            if (want == a->_sent) {
                continue;
            }
            uint8_t tx[6] = { uint8_t(_id), 6, uint8_t(a->_register >> 8), uint8_t(a->_register & 0xFF), uint8_t(want >> 8), uint8_t(want & 0xFF) };
            uint8_t rx[16];
            if (Modbus::bus->transact(tx, 6, rx, sizeof(rx), 6)) {
                a->_sent = want;
            } else {
                ok = false;
            }
        }
        return ok;
    }

    void ModbusIO::poll() {
        bool ok = write_outputs();
        ok      = write_analog() && ok;
        ok      = read_inputs() && ok;
        ok      = read_analog() && ok;
        if (ok) {
            if (_misses >= 3 && _reported) {
                log_info("modbus_io " << _id << " answering again");
            }
            _misses   = 0;
            _reported = false;
            return;
        }
        if (++_misses >= 3 && !_reported) {
            // Inputs keep their last value.
            _reported = true;
            log_error("modbus_io " << _id << " not answering");
            if (_fail_alarm) {
                mc_critical(ExecAlarm::ExpanderReset);
            }
        }
    }

    // ---- values

    bool ModbusIO::value(const std::string& name, float& v) {
        for (auto a : _ain) {
            if (a && a->_register >= 0 && !a->_name.empty() && string_util::equal_ignore_case(a->_name, name)) {
                v = a->_value;
                return true;
            }
        }
        return false;
    }
    bool ModbusIO::analog_in(int index, float& v) {
        for (auto a : _ain) {
            if (a && a->_register >= 0 && a->_index == index) {
                v = a->_value;
                return true;
            }
        }
        return false;
    }
    bool ModbusIO::has_analog_in(int index) const {
        for (auto a : _ain) {
            if (a && a->_register >= 0 && a->_index == index) {
                return true;
            }
        }
        return false;
    }
    bool ModbusIO::analog_out(int index, float percent) {
        for (auto a : _aout) {
            if (a && a->_register >= 0 && a->_index == index) {
                if (percent < 0) {
                    percent = 0;
                } else if (percent > 100) {
                    percent = 100;
                }
                a->_want = uint32_t(percent * a->_max / 100.0f + 0.5f);
                return true;
            }
        }
        return false;
    }
    bool ModbusIO::has_analog_out(int index) const {
        for (auto a : _aout) {
            if (a && a->_register >= 0 && a->_index == index) {
                return true;
            }
        }
        return false;
    }
    void ModbusIO::values_line(std::string& s) {
        char buf[48];
        for (auto a : _ain) {
            if (a && a->_register >= 0 && !a->_name.empty()) {
                snprintf(buf, sizeof(buf), "%s%s=%.1f", s.empty() ? "" : ",", a->_name.c_str(), double(a->_value.load()));
                s += buf;
            }
        }
    }
    void ModbusIO::report(Channel& out) {
        std::string in, o;
        uint64_t    ib = _in_bits.load(), ob = _out_want.load();
        for (uint32_t i = 0; i < _inputs; ++i) {
            in += ((ib >> i) & 1) ? '1' : '0';
        }
        for (uint32_t i = 0; i < _outputs; ++i) {
            o += ((ob >> i) & 1) ? '1' : '0';
        }
        log_stream(out,
                   "[MODBUS:" << _id << "|" << (_reported ? "not answering" : "ok") << "|IN:" << in << "|OUT:" << o);
        for (auto a : _ain) {
            if (a && a->_register >= 0) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.3f", double(a->_value.load()));
                log_stream(out,
                           "[MODBUS:" << _id << "|" << (a->_name.empty() ? std::string("ain") : a->_name) << "=" << buf
                                      << "|reg:" << a->_register << (a->_index >= 0 ? "|E" + std::to_string(a->_index) : ""));
            }
        }
        for (auto a : _aout) {
            if (a && a->_register >= 0) {
                log_stream(out, "[MODBUS:" << _id << "|aout E" << a->_index << "=" << a->_want.load() << "|reg:" << a->_register);
            }
        }
    }

    ModbusIO::~ModbusIO() {
        for (auto a : _ain) {
            delete a;
        }
        for (auto a : _aout) {
            delete a;
        }
    }

    namespace {
        PinExtenderFactory::InstanceBuilder<ModbusIO> registration("modbus_io");
    }
}
