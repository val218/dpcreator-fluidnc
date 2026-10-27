// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// ModbusIO - an off-the-shelf RS485 Modbus RTU I/O module as pins.
//
//   modbus_bus:
//     uart_num: 2
//   extenders:
//     pinext0:
//       modbus_io:
//         modbus_id: 3
//         inputs: 8              # pinext0.0 .. pinext0.7
//         outputs: 8             # pinext0.8 .. pinext0.15
//         input_function: 2      # 2 discrete inputs, 1 coils, 3/4 registers bit-packed
//         output_function: 15    # 15 write coils, 5 one coil at a time, 6/16 holding register bit-packed
//         ain0:                  # analog values, ain0 .. ain7
//           register: 0
//           name: arm_j1         # #<_mb_arm_j1>
//           scale: 0.088
//           offset: -180
//           index: 0             # M66 E0 L0 (optional)
//         aout0:                 # aout0 .. aout3
//           register: 10
//           index: 0             # M67 / M68 E0 Q<percent>
//           max: 1000
//
// The pins work anywhere a slow input or output pin works (user_inputs,
// user_outputs, control: buttons, coolant...), but not for step, direction or
// limit/homing switches: a Modbus round trip takes tens of milliseconds.
#pragma once

#include "PinExtenderDriver.h"
#include "Modbus/ModbusBus.h"

#include <atomic>
#include <string>

class InputPin;

namespace Extenders {
    class ModbusIO : public PinExtenderDriver, public Modbus::Device {
    public:
        static const int MAX_PINS = 64;
        static const int MAX_AIN  = 8;
        static const int MAX_AOUT = 4;

        class AnalogIn : public Configuration::Configurable {
        public:
            int32_t     _register = -1;
            int32_t     _function = 4;  // 4 input register, 3 holding register
            std::string _name;
            float       _scale  = 1.0f;
            float       _offset = 0.0f;
            bool        _signed = false;
            int32_t     _index  = -1;  // M66 E<index>

            std::atomic<float> _value { 0.0f };
            bool               _have = false;

            AnalogIn(int) {}
            void group(Configuration::HandlerBase& handler) override;
        };
        class AnalogOut : public Configuration::Configurable {
        public:
            int32_t  _register = -1;
            int32_t  _index    = 0;  // M67 / M68 E<index>
            uint32_t _max      = 1000;

            std::atomic<uint32_t> _want { 0 };
            uint32_t              _sent  = 0xFFFFFFFF;

            AnalogOut(int) {}
            void group(Configuration::HandlerBase& handler) override;
        };

    private:
        uint32_t _id              = 1;
        uint32_t _inputs          = 8;
        uint32_t _outputs         = 8;
        uint32_t _input_function  = 2;
        uint32_t _output_function = 15;
        uint32_t _input_address   = 0;
        uint32_t _output_address  = 0;
        bool     _fail_alarm      = false;

        AnalogIn*  _ain[MAX_AIN]   = { nullptr };
        AnalogOut* _aout[MAX_AOUT] = { nullptr };

        std::atomic<uint64_t> _in_bits { 0 };   // last inputs read
        std::atomic<uint64_t> _out_want { 0 };  // outputs as set by FluidNC
        uint64_t              _out_sent  = 0;
        bool                  _out_never = true;  // write all once at start
        uint64_t              _claimed   = 0;
        uint64_t              _is_input  = 0;
        InputPin*             _events[MAX_PINS] = { nullptr };
        bool                  _first_read       = true;

        int  _misses   = 0;
        bool _reported = false;

        bool read_inputs();
        bool write_outputs();
        bool read_analog();
        bool write_analog();

    public:
        ModbusIO(const char* name = "modbus_io") {}

        void group(Configuration::HandlerBase& handler) override;
        void validate() override;

        // PinExtenderDriver
        void        init() override;
        void        claim(pinnum_t index) override;
        void        free(pinnum_t index) override;
        void        setupPin(pinnum_t index, Pins::PinAttributes attr) override;
        void        writePin(pinnum_t index, bool high) override;
        bool        readPin(pinnum_t index) override;
        void        flushWrites() override;
        void        registerEvent(pinnum_t index, InputPin* obj) override;
        bool        slow() const override { return true; }
        const char* name() const override { return "modbus_io"; }

        // Modbus::Device
        void    poll() override;
        void    report(Channel& out) override;
        uint8_t id() const override { return uint8_t(_id); }
        bool    value(const std::string& name, float& v) override;
        bool    analog_in(int index, float& v) override;
        bool    analog_out(int index, float percent) override;
        bool    has_analog_out(int index) const override;
        bool    has_analog_in(int index) const override;
        void    values_line(std::string& s) override;

        ~ModbusIO();
    };
}
