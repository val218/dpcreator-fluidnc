// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Extenders.h"

#include <string>

namespace Extenders {
    PinExtender::PinExtender() : _driver(nullptr) {}

    void PinExtender::group(Configuration::HandlerBase& handler) { PinExtenderFactory::factory(handler, _driver); }
    void PinExtender::init() {
        if (_driver) {
            _driver->init();
        }
    }

    PinExtender::~PinExtender() { delete _driver; }

    Extenders::Extenders() {
        for (int i = 0; i < 16; ++i) {
            _pinDrivers[i] = nullptr;
        }
    }

    void Extenders::group(Configuration::HandlerBase& handler) {
        // pinext0: .. pinext9: (the name the pins use, pinext0.3), or the older pinextender0: ..
        for (size_t i = 0; i < 10; ++i) {
            std::string a = "pinext" + std::to_string(i);
            std::string b = "pinextender" + std::to_string(i);
            handler.section(a.c_str(), _pinDrivers[i]);
            if (handler.handlerType() == Configuration::HandlerType::Parser) {
                handler.section(b.c_str(), _pinDrivers[i]);  // read the old name; $Config/Dump writes pinextN
            }
        }
    }

    void Extenders::init() {
        for (int i = 0; i < 16; ++i) {
            if (_pinDrivers[i] != nullptr) {
                _pinDrivers[i]->init();
            }
        }
    }

    Extenders::~Extenders() {
        for (int i = 0; i < 16; ++i) {
            delete _pinDrivers[i];
        }
    }
}
