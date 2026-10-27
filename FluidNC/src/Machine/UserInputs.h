// Copyright (c) 2024 - Dylan Knutson
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"
#include "GCode.h"
#include "InputPin.h"

namespace Machine {

    class UserInputs : public Configuration::Configurable {
        // Analog pins are read with the ADC (analogValue()); they must be
        // ADC-capable GPIOs.

    public:
        UserInputs();
        virtual ~UserInputs();

        static InputPin digitalInput[];
        // Should be AnalogPin but we do not have such a thing yet
        static InputPin analogInput[];

        // analogN_scale / analogN_offset: value = percent x scale + offset
        static float analogScale[];
        static float analogOffset[];

        // M66 E<n> L0: 0-100 % of the ADC range (averaged), then scale and offset.
        // False if pin n is not an analog input.
        bool analogValue(size_t n, float& value);

        void init();
        void group(Configuration::HandlerBase& handler) override;
    };

}  // namespace Machine
