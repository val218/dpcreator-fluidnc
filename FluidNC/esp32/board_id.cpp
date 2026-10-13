#include "Driver/board_id.h"

// ESP-IDF 5 (ESP32-S3 builds) has esp_mac.h; ESP-IDF 4.4 (classic ESP32
// Arduino 2.x builds) declares esp_efuse_mac_get_default() in esp_system.h.
#if __has_include(<esp_mac.h>)
#    include <esp_mac.h>
#else
#    include <esp_system.h>
#endif
#include <cstdio>

const char* board_id() {
    static char id[13] = { 0 };
    if (!id[0]) {
        uint8_t mac[6] = { 0 };
        if (esp_efuse_mac_get_default(mac) != ESP_OK) {
            return "000000000000";
        }
        snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    return id;
}
