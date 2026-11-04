#include "Axes.h"

#include "Motors/MotorDriver.h"
#include "Motors/NullMotor.h"
#include "Config.h"
#include "MotionControl.h"
#include "Stepper.h"  // stepper_id_t
#include "Limit.h"
#include "MachineConfig.h"  // config->

#include <strings.h>  // strcasecmp

namespace Machine {
    MotorMask Axes::posLimitMask = 0;
    MotorMask Axes::negLimitMask = 0;
    MotorMask Axes::limitMask    = 0;
    MotorMask Axes::motorMask    = 0;

    AxisMask Axes::homingMask = 0;

    bool Axes::disabled = false;

    const char* Axes::_axisNames[] = {
        "X", "Y", "Z", "A", "B", "C", "U", "V", "W",
    };
    axis_t Axes::axisNum(std::string_view name) {
        for (axis_t axis = X_AXIS; axis < MAX_N_AXIS; axis++) {
            if (string_util::equal_ignore_case(name, axisName(axis))) {
                return axis;
            }
        }
        return INVALID_AXIS;
    }

    Pin Axes::_sharedStepperDisable;
    Pin Axes::_sharedStepperReset;

    uint32_t Axes::_homing_runs = 2;  // Number of Approach/Pulloff cycles

    axis_t Axes::_numberAxis = X_AXIS;

    Axis* Axes::_axis[MAX_N_AXIS] = { nullptr };

    Axes::Axes() {}

    void Axes::init() {
        log_info("Axis count " << Axes::_numberAxis);

        if (_sharedStepperDisable.defined()) {
            _sharedStepperDisable.setAttr(Pin::Attr::Output);
            _sharedStepperDisable.report("Shared stepper disable");
        }

        if (_sharedStepperReset.defined()) {
            _sharedStepperReset.setAttr(Pin::Attr::Output | Pin::Attr::InitialOn);
            _sharedStepperReset.on();
            _sharedStepperReset.report("Shared stepper reset");
        }

        // certain motors need features to be turned on. Check them here
        for (axis_t axis = X_AXIS; axis < _numberAxis; axis++) {
            auto a = _axis[axis];
            if (a) {
                log_info("Axis " << axisName(axis) << " (" << limitsMinPosition(axis) << "," << limitsMaxPosition(axis) << ")");
                a->init();
            }
            auto homing = a->_homing;
            if (homing && !homing->_positiveDirection) {
                set_bitnum(Homing::direction_mask, axis);
            }
        }

        link_shared_limits();
        config_motors();
    }

    // Motors with "limit_shared_with: <axis>" join that axis's limit_all_pin. One input, several axes:
    // only safe when they home one after the other, so axes in the same homing cycle are refused.
    void Axes::link_shared_limits() {
        for (axis_t axis = X_AXIS; axis < _numberAxis; axis++) {
            auto a = _axis[axis];
            if (!a) {
                continue;
            }
            for (motor_t motor = 0; motor < Axis::MAX_MOTORS_PER_AXIS; motor++) {
                auto m = a->_motors[motor];
                if (!m || m->_sharedWith.empty()) {
                    continue;
                }
                axis_t owner = INVALID_AXIS;
                for (axis_t o = X_AXIS; o < _numberAxis; o++) {
                    if (strcasecmp(axisName(o), m->_sharedWith.c_str()) == 0) {
                        owner = o;
                    }
                }
                auto oa = owner == INVALID_AXIS ? nullptr : _axis[owner];
                auto om = oa ? oa->_motors[0] : nullptr;
                const char* err = nullptr;
                if (!om || owner == axis) {
                    err = "names no other axis";
                } else if (!om->allLimitPin().defined()) {
                    err = "needs limit_all_pin on that axis's motor0";
                } else if (!om->_sharedWith.empty()) {
                    err = "points at an axis that shares a switch itself";
                } else if (a->_homing && oa->_homing && a->_homing->_cycle > 0 && a->_homing->_cycle == oa->_homing->_cycle) {
                    err = "must home in a different cycle than the axis it shares with";
                } else {
                    for (axis_t o = X_AXIS; o < _numberAxis && !err; o++) {
                        auto x = _axis[o];
                        if (o == axis || !x || !x->_homing || !a->_homing || a->_homing->_cycle <= 0) {
                            continue;
                        }
                        for (motor_t k = 0; k < Axis::MAX_MOTORS_PER_AXIS; k++) {
                            auto xm = x->_motors[k];
                            if (xm && xm->_sharedWith == m->_sharedWith && x->_homing->_cycle == a->_homing->_cycle) {
                                err = "must home in a different cycle than the other axes on the same switch";
                            }
                        }
                    }
                }
                if (err) {
                    log_error("Axis " << axisName(axis) << " limit_shared_with: " << m->_sharedWith << " " << err << ": no switch for this axis");
                    m->_sharedWith.clear();
                    continue;
                }
                om->allLimitPin().addSharedMotor(axis, motor);
                if (m->_hardLimits != om->_hardLimits) {
                    log_warn("Axis " << axisName(axis) << " shares " << axisName(owner) << "'s switch: hard_limits follows " << axisName(owner));
                }
                log_info("Axis " << axisName(axis) << " limit switch shared with " << axisName(owner));
            }
        }
    }

    void IRAM_ATTR Axes::set_disable(axis_t axis, bool disable, bool manual_override) {
        if (disable && !manual_override && !_axis[axis]->_idleDisable) {  //skip if idle disable is not allowed for this axis
            return;
        }
        for (motor_t motor = 0; motor < Axis::MAX_MOTORS_PER_AXIS; motor++) {
            auto m = _axis[axis]->_motors[motor];
            if (m) {
                m->_driver->set_disable(disable);
            }
        }
        if (disable) {  // any disable, !disable does not change anything here
            disabled = true;
        }
    }

    void IRAM_ATTR Axes::set_disable(bool disable, bool manual_override) {
        for (axis_t axis = X_AXIS; axis < _numberAxis; axis++) {
            set_disable(axis, disable, manual_override);
        }

        _sharedStepperDisable.synchronousWrite(disable);

        if (!disable && disabled) {
            disabled = false;
            if (Stepping::_disableDelayUsecs) {  // wait for the enable delay
                delay_us(Stepping::_disableDelayUsecs);
            }
        }
    }

    // Put the motors in the given axes into homing mode, returning a
    // mask of which motors can do homing.
    MotorMask Axes::set_homing_mode(AxisMask axisMask, bool isHoming) {
        MotorMask motorsCanHome = 0;

        for (axis_t axis = X_AXIS; axis < _numberAxis; axis++) {
            if (bitnum_is_true(axisMask, axis)) {
                auto a = _axis[axis];
                if (a != nullptr) {
                    for (motor_t motor = 0; motor < Axis::MAX_MOTORS_PER_AXIS; motor++) {
                        Stepping::unblock(axis, motor);
                        auto m = _axis[axis]->_motors[motor];
                        if (m) {
                            if (m->_driver->set_homing_mode(isHoming)) {
                                set_bitnum(motorsCanHome, motor_bit(axis, motor));
                            }
                        }
                    }
                }
            }
        }

        return motorsCanHome;
    }

    void Axes::config_motors() {
        for (axis_t axis = X_AXIS; axis < _numberAxis; ++axis) {
            _axis[axis]->config_motors();
        }
    }

    // Some small helpers to find the axis index and axis motor index for a given motor. This
    // is helpful for some motors that need this info, as well as debug information.
    axis_t Axes::findAxisIndex(const MotorDrivers::MotorDriver* const driver) {
        for (axis_t axis = X_AXIS; axis < _numberAxis; ++axis) {
            if (_axis[axis] != nullptr && _axis[axis]->hasMotor(driver)) {
                return axis;
            }
        }

        Assert(false, "Cannot find axis for motor driver");
        return INVALID_AXIS;
    }

    motor_t Axes::findAxisMotor(const MotorDrivers::MotorDriver* const driver) {
        for (axis_t axis = X_AXIS; axis < _numberAxis; ++axis) {
            if (_axis[axis] != nullptr && _axis[axis]->hasMotor(driver)) {
                for (motor_t motor = 0; motor < Axis::MAX_MOTORS_PER_AXIS; ++motor) {
                    auto m = _axis[axis]->_motors[motor];
                    if (m && m->_driver == driver) {
                        return motor;
                    }
                }
            }
        }

        Assert(false, "Cannot find axis for motor");
        return INVALID_MOTOR;
    }

    // Configuration helpers:

    void Axes::group(Configuration::HandlerBase& handler) {
        handler.item("shared_stepper_disable_pin", _sharedStepperDisable);
        handler.item("shared_stepper_reset_pin", _sharedStepperReset);
        handler.item("homing_runs", _homing_runs, 1, 5);

        // During the initial configuration parsing phase, _numberAxis is 0 so
        // we try for all the axes.  Subsequently we use the number of axes
        // that are actually present.
        axis_t n_axis = _numberAxis ? _numberAxis : MAX_N_AXIS;
        for (axis_t axis = X_AXIS; axis < n_axis; ++axis) {
            handler.section(_axisNames[axis], _axis[axis], axis);
        }
    }

    void Axes::afterParse() {
        // Find the last axis that was declared and set _numberAxis accordingly
        for (axis_t axis = MAX_N_AXIS; axis > 0; --axis) {
            if (_axis[axis - 1] != nullptr) {
                _numberAxis = axis;
                break;
            }
        }
        // Senders might assume 3 axes in reports
        if (_numberAxis < A_AXIS) {
            _numberAxis = A_AXIS;
        }

        for (axis_t axis = X_AXIS; axis < _numberAxis; ++axis) {
            if (_axis[axis] == nullptr) {
                _axis[axis] = new Axis(axis);
            }
        }
    }

    std::string Axes::maskToNames(AxisMask mask) {
        std::string retval("");
        auto        n_axis = _numberAxis;
        for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
            if (bitnum_is_true(mask, axis)) {
                retval += axisName(axis);
            }
        }
        return retval;
    }
    std::string Axes::motorMaskToNames(MotorMask mask) {
        std::string retval("");
        for (axis_t axis = X_AXIS; axis < MAX_N_AXIS; axis++) {
            if (bitnum_is_true(mask, axis)) {
                retval += " ";
                retval += axisName(axis);
            }
        }
        mask >>= 16;
        for (axis_t axis = X_AXIS; axis < MAX_N_AXIS; axis++) {
            if (bitnum_is_true(mask, axis)) {
                retval += " ";
                retval += axisName(axis);
                retval += "2";
            }
        }
        return retval;
    }

    MotorMask Axes::hardLimitMask() {
        MotorMask mask = 0;
        for (axis_t axis = X_AXIS; axis < _numberAxis; ++axis) {
            auto a = _axis[axis];

            for (motor_t motor = 0; motor < Axis::MAX_MOTORS_PER_AXIS; ++motor) {
                auto m = a->_motors[motor];
                if (m && m->_hardLimits) {
                    set_bitnum(mask, axis);
                }
            }
        }
        return mask;
    }

    bool Axes::namesToMask(const char* names, AxisMask& mask) {
        bool       retval      = true;
        const auto lenNames    = strlen(names);
        char       axisName[2] = {};
        for (size_t i = 0; i < lenNames; i++) {
            axisName[0] = names[i];
            axis_t axis = axisNum(axisName);
            if (axis == INVALID_AXIS) {
                log_error("Invalid axis name " << axisName);
                retval = false;
            }
            set_bitnum(mask, axis);
        }

        return retval;
    }

    Axes::~Axes() {
        for (axis_t axis = X_AXIS; axis < MAX_N_AXIS; ++axis) {
            if (_axis[axis] != nullptr) {
                delete _axis[axis];
            }
        }
    }
}
