// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Platform.h"
#include "Pin.h"
#include "Uart.h"
#include "Protocol.h"
#include "Driver/fluidnc_gpio.h"

#include <filesystem>
#include <string>

void gpio_write(pinnum_t pin, bool value) {}
// Host build: an input reads high while the file native_gpio/<pin> exists, so
// tests and Motion can drive user inputs, probe, limits ("touch native_gpio/5").
bool gpio_read(pinnum_t pin) {
    std::error_code ec;
    return std::filesystem::exists("native_gpio/" + std::to_string(int(pin)), ec);
}
void gpio_mode(pinnum_t pin, bool input, bool output, bool pullup, bool pulldown, bool opendrain) {}
void gpio_drive_strength(pinnum_t pin, uint8_t strength) {}
void gpio_route(pinnum_t pin, uint32_t signal) {}

typedef uint64_t gpio_mask_t;

// Can be used to display gpio_mask_t data for debugging
static const char* g_to_hex(gpio_mask_t n) {
    static char hexstr[24];
    snprintf(hexstr, 22, "0x%llx", n);
    return hexstr;
}

static gpio_mask_t gpios_inverted = 0;  // GPIOs that are active low
static gpio_mask_t gpios_interest = 0;  // GPIOs with an action
static gpio_mask_t gpios_current  = 0;  // The last GPIO action events that were sent

static int32_t gpio_next_event_ticks[MAX_N_GPIO + 1] = { 0 };
static int32_t gpio_deltat_ticks[MAX_N_GPIO + 1]     = { 0 };

// Do not send events for changes that occur too soon
static void gpio_set_rate_limit(int32_t gpio_num, uint32_t ms) {}

static gpio_mask_t gpio_mask(int32_t gpio_num);
static inline gpio_mask_t get_gpios() {
    // Host build: the levels come from native_gpio/<pin> files (gpio_read),
    // read only for the pins something listens to.
    gpio_mask_t raw = 0;
    for (int32_t g = 0; g <= MAX_N_GPIO && g < 64; ++g) {
        if ((gpios_interest & gpio_mask(g)) && gpio_read(g)) {
            raw |= gpio_mask(g);
        }
    }
    return raw ^ gpios_inverted;
}
static gpio_mask_t gpio_mask(int32_t gpio_num) {
    return 1ULL << gpio_num;
}
static inline bool gpio_is_active(int32_t gpio_num) {
    return get_gpios() & gpio_mask(gpio_num);
}
static void gpios_update(gpio_mask_t& gpios, int32_t gpio_num, bool active) {
    if (active) {
        gpios |= gpio_mask(gpio_num);
    } else {
        gpios &= ~gpio_mask(gpio_num);
    }
}

static void* gpioArgs[MAX_N_GPIO + 1];

void gpio_set_event(int32_t gpio_num, void* arg, bool invert) {
    gpioArgs[gpio_num] = arg;
    gpio_mask_t mask   = gpio_mask(gpio_num);
    gpios_update(gpios_interest, gpio_num, true);
    gpios_update(gpios_inverted, gpio_num, invert);
    gpio_set_rate_limit(gpio_num, 5);
    auto active = gpio_is_active(gpio_num);

    // Set current to the opposite of the current state so the first poll will send the current state
    gpios_update(gpios_current, gpio_num, !active);
}
void gpio_clear_event(int32_t gpio_num) {
    gpioArgs[gpio_num] = nullptr;
    gpios_update(gpios_interest, gpio_num, false);
}

void gpio_disarm(int32_t gpio_num) {
    gpios_update(gpios_interest, gpio_num, false);
}

void gpio_rearm(int32_t gpio_num) {
    gpio_mask_t mask = gpio_mask(gpio_num);
    if (!(gpios_interest & mask)) {
        gpios_update(gpios_interest, gpio_num, true);
        if (!(get_gpios() & mask)) {
            auto arg = gpioArgs[gpio_num];
            if (arg) {
                protocol_send_event_from_ISR(&pinInactiveEvent, arg);
            }
        }
    }
}

static void gpio_send_event(int32_t gpio_num, bool active) {
    // Host build: no rate limit; every change polled is sent once.
    auto arg = gpioArgs[gpio_num];
    if (arg) {
        protocol_send_event_from_ISR(active ? &pinActiveEvent : &pinInactiveEvent, arg);
    }
    gpios_update(gpios_current, gpio_num, active);
}

void poll_gpios() {
    gpio_mask_t gpios_active  = get_gpios();
    gpio_mask_t gpios_changed = (gpios_active ^ gpios_current) & gpios_interest;

    // Process each changed GPIO. We check gpios_changed != 0 explicitly because
    // __builtin_clzll(0) is undefined behavior - the optimizer can assume it never
    // happens and turn this into an infinite loop in release builds.
    while (gpios_changed) {
        int     zeros    = __builtin_clzll(gpios_changed);  // Safe: gpios_changed is non-zero here
        int32_t gpio_num = 63 - zeros;
        gpio_send_event(gpio_num, gpios_active & gpio_mask(gpio_num));
        // Clear the bit we just processed
        gpios_changed &= ~gpio_mask(gpio_num);
    }
}
