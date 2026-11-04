// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  Charge pump for parallel-port breakout boards (Mach3 style): many breakouts keep their stepper drivers
  disabled unless one input sees a square wave. This module makes it with LEDC (no CPU load) and stops it
  whenever the machine must not move:

    charge_pump:
      pin: gpio.37        # any output; ":low" inverts the idle level
      hz: 12500           # Mach3 uses 12.5 kHz; 5000 to 25000
      stall_ms: 100       # stop if the main loop hasn't run for this long

  Off:  until the config is loaded, in Critical/ConfigAlarm/Starting/Sleep, during a reset, with an E-stop or
        fault control pin active, after a hard limit, hard stop, control-pin or expander alarm, and when the
        main loop stalls (LEDC would otherwise keep running on a hung firmware). A crash ends in a chip reset,
        which leaves the pin idle.
  On:   every other state, including the "not homed" Alarm after power-up: a breakout without the pump keeps
        its drivers off, so homing could never move.
  The state is logged on every change ([MSG:INFO: Charge pump on] / ... off: <reason>) and shown by $CP.
*/
#pragma once

#include "Module.h"
#include "Pin.h"

class ChargePump : public ConfigurableModule {
    Pin      _pin;
    uint32_t _hz       = 12500;
    uint32_t _stall_ms = 100;

    uint32_t             _half = 0;
    volatile uint32_t    _fed_ms = 0;
    volatile bool        _on     = false;
    const char* volatile _reason = "starting";
    const char*          _reported = nullptr;
    void*                _timer    = nullptr;

    static void tick(void* arg);
    const char* why_off();

public:
    ChargePump(const char* name) : ConfigurableModule(name) {}

    void init() override;
    void group(Configuration::HandlerBase& handler) override {
        handler.item("pin", _pin);
        handler.item("hz", _hz, 5000, 25000);
        handler.item("stall_ms", _stall_ms, 20, 2000);
    }

    bool        configured() { return _timer != nullptr; }
    bool        on() { return _on; }
    const char* reason() { return _reason; }
    void        feed();  // from the protocol main loop
};

extern ChargePump* chargePump;
