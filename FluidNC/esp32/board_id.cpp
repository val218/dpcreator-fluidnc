#include "Driver/board_id.h"

#include <esp_mac.h>
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
