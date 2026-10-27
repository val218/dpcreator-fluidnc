// Host build: ESP-IDF random numbers from the OS.
#pragma once
#include <cstddef>
#include <cstdint>
#include <random>
inline uint32_t esp_random() {
    static std::random_device rd;
    return rd();
}
inline void esp_fill_random(void* buf, size_t len) {
    auto* p = static_cast<uint8_t*>(buf);
    for (size_t i = 0; i < len; ++i) p[i] = (uint8_t)esp_random();
}
