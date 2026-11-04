// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ChargePump.h"

#include "Alarm.h"
#include "Machine/MachineConfig.h"
#include "System.h"

#include <esp_timer.h>

ChargePump* chargePump = nullptr;

static uint32_t now_ms() {
    return uint32_t(esp_timer_get_time() / 1000);
}

void ChargePump::init() {
    if (!_pin.defined()) {
        return;
    }
    _pin.setAttr(Pin::Attr::PWM, _hz);
    _pin.setDuty(0);  // idle level until the first tick allows it
    _half   = _pin.maxDuty() / 2;
    _fed_ms = now_ms();

    esp_timer_create_args_t args = {};
    args.callback                = &ChargePump::tick;
    args.arg                     = this;
    args.name                    = "chargepump";
    esp_timer_handle_t t         = nullptr;
    if (esp_timer_create(&args, &t) != ESP_OK || esp_timer_start_periodic(t, 10 * 1000) != ESP_OK) {
        log_error("Charge pump: no timer, output stays off");
        return;
    }
    _timer    = t;
    chargePump = this;
    log_info("Charge pump on " << _pin.name() << " " << _hz << "Hz, stall " << _stall_ms << "ms");
}

// nullptr = may run
const char* ChargePump::why_off() {
    if (now_ms() - _fed_ms > _stall_ms) {
        return "main loop stalled";
    }
    if (sys.abort()) {
        return "reset";
    }
    if (config->_control && config->_control->pins_block_unlock()) {
        return "E-stop or fault input";
    }
    switch (sys.state()) {
        case State::Starting:
        case State::ConfigAlarm:
        case State::Critical:
            return "not ready";
        case State::Sleep:
            return "sleep";
        case State::Alarm:
            switch (lastAlarm) {
                case ExecAlarm::HardLimit:
                case ExecAlarm::HardStop:
                case ExecAlarm::ProbeHardLimit:
                case ExecAlarm::StartupPin:
                case ExecAlarm::ExpanderReset:
                    return "alarm";
                default:
                    return nullptr;  // e.g. not homed yet: homing has to be able to move
            }
        default:
            return nullptr;
    }
}

// esp_timer task, every 10 ms: decide and switch LEDC; nothing here blocks or logs
void ChargePump::tick(void* arg) {
    auto        self = static_cast<ChargePump*>(arg);
    const char* off  = self->why_off();
    bool        want = off == nullptr;
    if (want != self->_on) {
        self->_pin.setDuty(want ? self->_half : 0);
        self->_on = want;
    }
    self->_reason = want ? "on" : off;
}

// protocol main loop: proves the loop is alive, and reports changes
void ChargePump::feed() {
    _fed_ms        = now_ms();
    const char* r  = _reason;
    if (r != _reported) {
        _reported = r;
        if (_on) {
            log_info("Charge pump on");
        } else {
            log_info("Charge pump off: " << r);
        }
    }
}

namespace {
    ConfigurableModuleFactory::InstanceBuilder<ChargePump> registration("charge_pump");
}
