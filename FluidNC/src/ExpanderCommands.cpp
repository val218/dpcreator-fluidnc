// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// IO expander commands for the WebUI's "I/O Expander" page.
//
//   $Expander/State  ($EXP)   [EXP:CAPS=M66WAIT,UIO=16] (what this firmware can do), then
//                             one line per expander that answered the ID query:
//       [EXP:2,ID=dpCREATOR IO16 v1.0.0,IN=00A1,OUT=0003,FLAGS=07]
//       [EXP:2,ID=dpCREATOR IO16 v1.0.0,OFFLINE]     (monitored, silent for 3 s)
//       [EXP:3,ID=<other expander>,NOMON]            (no live monitor)
//
//   $Expander/Pins   ($EXPP)  every config.yaml item that uses an expander pin:
//       [EXP:PIN=2.0,control/safety_door_pin,in,low]
//       [EXP:PIN=2.16,coolant/flood_pin,out]
//       [EXP:PIN=2.22,user_outputs/analog0_pin,pwm]
//   (path from the YAML section names; direction from how FluidNC set the pin up)
#include "ExpanderMonitor.h"
#include "UartChannel.h"
#include "Settings.h"
#include "Logging.h"
#include "Machine/MachineConfig.h"
#include "Configuration/HandlerBase.h"
#include "Configuration/Configurable.h"
#include "Pins/PinAttributes.h"
#include "GCode.h"  // MaxUserDigitalPin

#include <algorithm>
#include <string>
#include <vector>

namespace {
    struct ExpanderPin {
        std::string id;  // "2.16"
        std::string path;
        std::string kind;  // in / out / pwm
        bool        low;
        int         chan, index;
    };

    class PinCollector : public Configuration::HandlerBase {
        std::string _path;

        void pin(const char* name, Pin& p, const char* fallback_kind) {
            std::string id;
            if (!p.defined() || !expander_pin_id(p.name(), id)) {
                return;
            }
            ExpanderPin e;
            e.id   = id;
            e.path = _path + name;
            auto a = p.getAttr();
            if (a.has(Pins::PinAttributes::PWM)) {
                e.kind = "pwm";
            } else if (a.has(Pins::PinAttributes::Input)) {
                e.kind = "in";
            } else if (a.has(Pins::PinAttributes::Output)) {
                e.kind = "out";
            } else {
                e.kind = fallback_kind;
            }
            e.low   = a.has(Pins::PinAttributes::ActiveLow);
            e.chan  = atoi(id.c_str());
            e.index = atoi(id.c_str() + id.find('.') + 1);
            pins.push_back(e);
        }

    protected:
        void enterSection(const char* name, Configuration::Configurable* value) override {
            auto previous = _path;
            _path += name;
            _path += "/";
            value->group(*this);
            _path = previous;
        }
        bool matchesUninitialized(const char* name) override { return false; }

    public:
        std::vector<ExpanderPin> pins;

        void item(const char* name, bool& value) override {}
        void item(const char* name, int32_t& value, const int32_t minValue, const int32_t maxValue) override {}
        void item(const char* name, uint32_t& value, const uint32_t minValue, const uint32_t maxValue) override {}
        void item(const char* name, float& value, const float minValue, const float maxValue) override {}
        void item(const char* name, std::vector<Configuration::speedEntry>& value) override {}
        void item(const char* name, std::vector<float>& value) override {}
        void item(const char* name, UartData& wordLength, UartParity& parity, UartStop& stopBits) override {}
        void item(const char* name, std::string& value, const int minLength, const int maxLength) override {}
        void item(const char* name, EventPin& value) override { pin(name, value, "in"); }
        void item(const char* name, InputPin& value) override { pin(name, value, "in"); }
        void item(const char* name, Pin& value) override { pin(name, value, "-"); }
        void item(const char* name, Macro& value) override {}
        void item(const char* name, IPAddress& value) override {}
        void item(const char* name, step_engine*& value) override {}
        void item(const char* name, uint32_t& value, const EnumItem* e) override {}
        void item(const char* name, axis_t& value) override {}

        // Walks like the tab completer: read-only, no side effects in group()
        Configuration::HandlerType handlerType() override { return Configuration::HandlerType::Completer; }
    };

    Error expander_state(const char* value, AuthenticationLevel auth_level, Channel& out) {
        // What this firmware can do, for the WebUI's macro builder:
        //   M66WAIT  M66 P<n> L1-L4 Q<s> waits (#5399 = -1 on timeout), L0 reads after motion
        //   UIO=<n>  user digital inputs/outputs P0..P<n-1> (M62-M66)
        log_stream(out, "[EXP:CAPS=M66WAIT,UIO=" << MaxUserDigitalPin);
        bool     any = false;
        uint32_t now = millis();
        for (size_t n = 0; n < MAX_N_UARTS; ++n) {
            auto ch = config->_uart_channels[n];
            if (!ch || ch->expander_id().empty()) {
                continue;
            }
            any = true;
            if (auto m = ch->monitor()) {
                auto line = m->stateLine(now);
                line.pop_back();  // LogStream adds the closing ']' to a line that starts with '['
                log_stream(out, line);
                ch->requestState();  // so the next status report is fresh
            } else {
                log_stream(out, "[EXP:" << int(ch->num()) << ",ID=" << ch->expander_id() << ",NOMON");
            }
        }
        if (!any) {
            log_info_to(out, "No IO expander");
        }
        return Error::Ok;
    }

    Error expander_pins(const char* value, AuthenticationLevel auth_level, Channel& out) {
        PinCollector c;
        config->group(c);
        std::stable_sort(c.pins.begin(), c.pins.end(), [](const ExpanderPin& a, const ExpanderPin& b) {
            return a.chan != b.chan ? a.chan < b.chan : a.index < b.index;
        });
        for (auto& p : c.pins) {
            log_stream(out, "[EXP:PIN=" << p.id << "," << p.path << "," << p.kind << (p.low ? ",low" : ""));
        }
        return Error::Ok;
    }
}

void make_expander_commands() {
    new ReportCommand("EXP", "Expander/State", expander_state, anyState);
    new ReportCommand("EXPP", "Expander/Pins", expander_pins, anyState);
}
