// Host build: ESP-IDF error codes.
#pragma once
typedef int esp_err_t;
#ifndef ESP_OK
#    define ESP_OK 0
#    define ESP_FAIL -1
#endif
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NVS_BASE 0x1100
#define ESP_ERR_NVS_NOT_FOUND (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_INVALID_LENGTH (ESP_ERR_NVS_BASE + 0x0c)
