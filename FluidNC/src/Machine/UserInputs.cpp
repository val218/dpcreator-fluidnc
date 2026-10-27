// Copyright (c) 2024 - Dylan Knutson
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "UserInputs.h"
#include "Driver/fluidnc_adc.h"
#include "Logging.h"

// Platforms without an ADC driver (esp32/adc.cpp, capture/adc.cpp)
WEAK_LINK int adc_read_raw(pinnum_t gpio, int samples) {
    return -1;
}

namespace Machine {
    UserInputs::UserInputs() {}
    UserInputs::~UserInputs() {}

    // General-purpose spare input pins, independent of any axis/spindle/coolant feature.
    // Exactly 8 digital + 4 analog slots -- a fixed array size (MaxUserDigitalPin/
    // MaxUserAnalogPin), not extensible via config. group() below registers these via a
    // loop (handler.item(pin.legend(), pin)), not a literal handler.item() call per slot --
    // see ItemDocs.md's "data-driven item lists" section for why these are annotated here,
    // at the point of registration, instead.

    // clang-format off
    InputPin UserInputs::digitalInput[MaxUserDigitalPin] = {
        // @config digital0_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 0.
        InputPin { "digital0_pin" },
        // @config digital1_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 1.
        InputPin { "digital1_pin" },
        // @config digital2_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 2.
        InputPin { "digital2_pin" },
        // @config digital3_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 3.
        InputPin { "digital3_pin" },
        // @config digital4_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 4.
        InputPin { "digital4_pin" },
        // @config digital5_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 5.
        InputPin { "digital5_pin" },
        // @config digital6_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 6.
        InputPin { "digital6_pin" },
        // @config digital7_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 7.
        InputPin { "digital7_pin" },
    };
    InputPin UserInputs::analogInput[MaxUserAnalogPin] = {
        // @config analog0_pin
        // @default NO_PIN
        // @pin_attributes input
        // Analog input slot 0.
        InputPin { "analog0_pin" },
        // @config analog1_pin
        // @default NO_PIN
        // @pin_attributes input
        // Analog input slot 1.
        InputPin { "analog1_pin" },
        // @config analog2_pin
        // @default NO_PIN
        // @pin_attributes input
        // Analog input slot 2.
        InputPin { "analog2_pin" },
        // @config analog3_pin
        // @default NO_PIN
        // @pin_attributes input
        // Analog input slot 3.
        InputPin { "analog3_pin" },
    };
    // clang-format on

    float UserInputs::analogScale[MaxUserAnalogPin]  = { 1.0f, 1.0f, 1.0f, 1.0f };
    float UserInputs::analogOffset[MaxUserAnalogPin] = { 0.0f, 0.0f, 0.0f, 0.0f };

    void UserInputs::group(Configuration::HandlerBase& handler) {
        for (size_t i = 0; i < MaxUserDigitalPin; i++) {
            auto& pin = digitalInput[i];
            handler.item(pin.legend(), pin);
        }
        for (size_t i = 0; i < MaxUserAnalogPin; i++) {
            auto& pin = analogInput[i];
            handler.item(pin.legend(), pin);
        }
        // @config analog0_scale
        // @default 1.0
        // M66 E0 L0 returns percent x scale + offset (percent: 0-100 of the ADC range).
        handler.item("analog0_scale", analogScale[0]);
        // @config analog0_offset
        // @default 0.0
        handler.item("analog0_offset", analogOffset[0]);
        // @config analog1_scale
        // @default 1.0
        handler.item("analog1_scale", analogScale[1]);
        // @config analog1_offset
        // @default 0.0
        handler.item("analog1_offset", analogOffset[1]);
        // @config analog2_scale
        // @default 1.0
        handler.item("analog2_scale", analogScale[2]);
        // @config analog2_offset
        // @default 0.0
        handler.item("analog2_offset", analogOffset[2]);
        // @config analog3_scale
        // @default 1.0
        handler.item("analog3_scale", analogScale[3]);
        // @config analog3_offset
        // @default 0.0
        handler.item("analog3_offset", analogOffset[3]);
    }

    bool UserInputs::analogValue(size_t n, float& value) {
        if (n >= MaxUserAnalogPin || analogInput[n].undefined()) {
            return false;
        }
        int raw = adc_read_raw(analogInput[n].index(), 8);
        if (raw < 0) {
            return false;
        }
        value = (raw * 100.0f / 4095.0f) * analogScale[n] + analogOffset[n];
        return true;
    }

    void UserInputs::init() {
        for (size_t i = 0; i < MaxUserDigitalPin; i++) {
            auto& pin = digitalInput[i];
            if (pin.defined()) {
                pin.init();
            }
        }
        for (size_t i = 0; i < MaxUserAnalogPin; i++) {
            auto& pin = analogInput[i];
            if (pin.defined()) {
                // Read with the ADC on demand: no pin events, just an input.
                if (!pin.capabilities().has(Pin::Capabilities::ADC | Pin::Capabilities::Native)) {
                    log_config_error(pin.legend() << " " << pin.name() << " is not an ADC pin (ESP32-S3: GPIO 1-20)");
                    continue;
                }
                pin.report(pin.legend());
                pin.setAttr(Pin::Attr::Input);
            }
        }
    }

}  // namespace Machine
