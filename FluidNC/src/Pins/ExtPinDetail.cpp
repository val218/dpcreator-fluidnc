// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ExtPinDetail.h"

namespace Pins {
    ExtPinDetail::ExtPinDetail(uint32_t device, pinnum_t index, const PinOptionsParser& options) :
        PinDetail(index), _device(device), _capabilities(PinCapabilities::Output | PinCapabilities::Input | PinCapabilities::ISR),
        _attributes(Pins::PinAttributes::Undefined) {
        // @pin_namespace pinext<n>
        _name = "pinext";
        _name += std::to_string(_device);
        _name += ".";
        _name += std::to_string(_index);

        // User defined pin capabilities
        for (auto opt : options) {
            if (opt.is("low")) {
                _attributes = _attributes | PinAttributes::ActiveLow;
                _name += ":low";
            } else if (opt.is("high")) {
                // Default: Active HIGH.
            } else {
                Assert(false, "Unsupported pin extender option '%s'", opt());
            }
        }
    }

    PinCapabilities ExtPinDetail::capabilities() const {
        return PinCapabilities::Input | PinCapabilities::Output | PinCapabilities::ISR;
    }

    // I/O:
    Extenders::PinExtenderDriver* ExtPinDetail::owner() {
        if (_owner == nullptr) {
            auto ext = config->_extenders;
            if (ext != nullptr && ext->_pinDrivers[_device] != nullptr && ext->_pinDrivers[_device]->_driver != nullptr) {
                _owner = ext->_pinDrivers[_device]->_driver;
                _owner->claim(_index);
            } else {
                Assert(false, "Cannot find pin extender definition in configuration for pin pinext%d.%d", _device, _index);
            }
        }
        return _owner;
    }

    void ExtPinDetail::write(bool high) {
        Assert(_owner != nullptr, "Cannot write to uninitialized pin");
        _owner->writePin(_index, _attributes.has(PinAttributes::ActiveLow) ? !high : high);
    }

    void ExtPinDetail::synchronousWrite(bool high) {
        Assert(_owner != nullptr, "Cannot write to uninitialized pin");
        _owner->writePin(_index, _attributes.has(PinAttributes::ActiveLow) ? !high : high);
        _owner->flushWrites();
    }

    bool ExtPinDetail::read() {
        Assert(_owner != nullptr, "Cannot read from uninitialized pin");
        bool raw = _owner->readPin(_index);
        return _attributes.has(PinAttributes::ActiveLow) ? !raw : raw;
    }

    void ExtPinDetail::registerEvent(InputPin* obj) {
        owner()->registerEvent(_index, obj);
    }

    bool ExtPinDetail::slow() const {
        auto ext = config->_extenders;
        if (ext != nullptr && ext->_pinDrivers[_device] != nullptr && ext->_pinDrivers[_device]->_driver != nullptr) {
            return ext->_pinDrivers[_device]->_driver->slow();
        }
        return false;
    }

    void ExtPinDetail::setAttr(PinAttributes value, uint32_t frequency) {
        // We setup the driver in setAttr. Before this time, the owner might not be valid.

        // Check the attributes first:
        Assert(value.has(PinAttributes::Input) || value.has(PinAttributes::Output), "Pin extender pins can be used as either input or output");
        Assert(value.has(PinAttributes::Input) != value.has(PinAttributes::Output), "Pin extender pins can be used as either input or output");
        Assert(value.validateWith(this->_capabilities), "Requested attributes do not match the pin extender pin capabilities");
        Assert(!_attributes.conflictsWith(value), "Attributes on this pin have been set before, and there's a conflict");

        // keep ActiveLow from the pin name (":low"), add what the user asked for
        bool low    = _attributes.has(PinAttributes::ActiveLow);
        _attributes = value;
        if (low) {
            _attributes = _attributes | PinAttributes::ActiveLow;
        }

        owner()->setupPin(_index, _attributes);
        if (_attributes.has(PinAttributes::Output)) {
            write(value.has(PinAttributes::InitialOn));
        }
    }

    PinAttributes ExtPinDetail::getAttr() const {
        return _attributes;
    }

#if 0
    void ExtPinDetail::attachInterrupt(void (*callback)(void*, bool), void* arg, uint8_t mode) {
        Assert(_owner != nullptr, "Cannot attach ISR on uninitialized pin");
        _owner->attachInterrupt(_index, callback, arg, mode);
    }
    void ExtPinDetail::detachInterrupt() {
        Assert(_owner != nullptr, "Cannot detach ISR on uninitialized pin");
        _owner->detachInterrupt(_index);
    }
#endif

    ExtPinDetail::~ExtPinDetail() {
        if (_owner) {
            _owner->free(_index);
        }
    }
}
