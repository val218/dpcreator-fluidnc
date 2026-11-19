// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Probe.h"
#include "Machine/EventPin.h"
#include "Machine/MachineConfig.h"
#include "Machine/Axes.h"
#include "Machine/Axis.h"
#include "Machine/Motor.h"
#include "Machine/LimitPin.h"

extern void           protocol_do_probe(void* arg);
extern const ArgEvent probeEvent;  // external linkage: the Z limit's input sends it too (LimitPin.cpp)
const ArgEvent        probeEvent { protocol_do_probe };

Probe::ProbeEventPin::ProbeEventPin(const char* legend) : EventPin(&probeEvent, ExecAlarm::None, legend) {}

void Probe::init() {
    if ((_probePin.defined() && _probePin.slow()) || (_toolsetterPin.defined() && _toolsetterPin.slow())) {
        log_config_error("probe: the probe and toolsetter can't be Modbus extender pins (too slow, the tool would overshoot)");
        return;
    }
    _probePin.init();
    _toolsetterPin.init();

    if (_useZLimitPin) {
        auto axes = config->_axes;
        auto z    = (axes && Z_AXIS < Machine::Axes::_numberAxis) ? axes->_axis[Z_AXIS] : nullptr;
        auto m    = z ? z->_motors[0] : nullptr;
        _zLimit   = m ? m->switchPin() : nullptr;
        if (_probePin.defined()) {
            log_config_error("probe: use_z_limit_pin and pin can't both be set");
            _zLimit = nullptr;
        } else if (!_zLimit) {
            log_config_error("probe: use_z_limit_pin needs a Z limit pin (z: motor0: limit_all_pin, limit_neg_pin or limit_pos_pin)");
        } else {
            _zLimit->_alsoProbe = true;
            log_info("Probe: on the Z limit input " << _zLimit->name());
        }
    }
}

bool Probe::sharedActive() {
    return _zLimit && _zLimit->get();
}

void Probe::set_direction(bool away) {
    _away = away;
}

// Returns the probe pin state. Triggered = true. Called by gcode parser.
bool Probe::get_state() {
    return _probePin.get() || _toolsetterPin.get() || sharedActive();
}

// Returns true if the probe pin is tripped, accounting for the direction (away or not).
bool Probe::tripped() {
    return get_state() ^ _away;
}

void Probe::validate() {}

void Probe::group(Configuration::HandlerBase& handler) {
    // @config pin
    // @default NO_PIN
    // @pin_attributes input
    // The probe input signal. G38 probing moves trigger on either edge of this pin.
    handler.item("pin", _probePin);

    // @config toolsetter_pin
    // @default NO_PIN
    // @pin_attributes input
    // An optional second probe input, treated identically to pin -- G38 can't target one
    // or the other specifically, either triggers the same probe action. Having two lets
    // N.C./N.O. (or PNP/NPN) probes be wired without an external OR'ing circuit, and each
    // shows up as a separate switch in the '?' status report to help debug wiring.
    handler.item("toolsetter_pin", _toolsetterPin);

    // @config use_z_limit_pin
    // @default false
    // @tuning typical
    // The probe shares the Z limit's input (dpCREATOR R3 / R3.1: the PROBE terminal
    // and the Z limit are one line, one optocoupler, GPIO3). Leave pin at NO_PIN.
    // During a probe move (G38) a touch is the probe's and does not stop the axis
    // as a limit; otherwise the input is the Z limit, as before (homing, hard limits).
    handler.item("use_z_limit_pin", _useZLimitPin);

    // @config check_mode_start
    // @default true
    // @tuning typical
    // Only affects Grbl Check Mode ($C, dry-run parsing with no real motion): while in
    // Check Mode, a probe move reports its position as the move's un-probed target when
    // false, or leaves it at the pre-move start position when true. Has no effect on a real
    // (non-Check-Mode) probe cycle.
    handler.item("check_mode_start", _check_mode_start);

    // @config hard_stop
    // @default false
    // @tuning typical
    // On a successful probe trigger, stops with an immediate hard stop instead of
    // decelerating -- avoids the extra travel deceleration needs, at the cost of possibly
    // losing steps (and therefore position accuracy) at higher speeds. Useful with fragile
    // probes/bits that could be damaged by the overtravel a normal deceleration needs.
    handler.item("hard_stop", _hard_stop);

    // @config probe_hard_limit
    // @default false
    // @tuning typical
    // When true, the probe pin(s) also act like a hard limit switch during non-probing
    // motion (homing, jog, cycle): triggering it raises an alarm and immediately stops
    // motion, the same as a real hard limit switch would. Guards against accidental probe
    // damage from a collision during ordinary motion, not from the probing move itself.
    handler.item("probe_hard_limit", _probe_hard_limit);  // non probing motion protection
}
void protocol_do_probe(void* arg) {
    Probe* p = config->_probe;
    if (p->tripped() && probing) {
        probing = false;
        get_steps(probe_steps);
        if (p->_hard_stop) {
            Stepper::reset();
            plan_reset();
            set_state(State::Idle);
        } else {
            protocol_do_motion_cancel();
        }
    } else if (p->tripped() && p->_probe_hard_limit) {
        // trip if in homing, cycle or jog, but not probing
        if ((state_is(State::Cycle) || state_is(State::Jog) || state_is(State::Homing)) &&
            (gc_state.modal.motion != Motion::ProbeAway && gc_state.modal.motion != Motion::ProbeAwayNoError &&
             gc_state.modal.motion != Motion::ProbeToward && gc_state.modal.motion != Motion::ProbeTowardNoError)) {
            mc_critical(ExecAlarm::ProbeHardLimit);
        }
    }
}
