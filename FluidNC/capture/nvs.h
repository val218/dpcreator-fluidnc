// Host build: ESP-IDF NVS (the C API) as files in ./native_nvs, one per key,
// next to the C++ NVS emulation in nvsfile.cpp.
#pragma once
#include "esp_err.h"
#include <cstddef>
#include <cstdint>

typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;

esp_err_t nvs_open(const char* ns, nvs_open_mode_t mode, nvs_handle_t* out);
void      nvs_close(nvs_handle_t h);
esp_err_t nvs_commit(nvs_handle_t h);
esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* len);
esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* value, size_t len);
esp_err_t nvs_get_u32(nvs_handle_t h, const char* key, uint32_t* out);
esp_err_t nvs_set_u32(nvs_handle_t h, const char* key, uint32_t value);
esp_err_t nvs_get_i32(nvs_handle_t h, const char* key, int32_t* out);
esp_err_t nvs_set_i32(nvs_handle_t h, const char* key, int32_t value);
esp_err_t nvs_get_str(nvs_handle_t h, const char* key, char* out, size_t* len);
esp_err_t nvs_set_str(nvs_handle_t h, const char* key, const char* value);
esp_err_t nvs_erase_key(nvs_handle_t h, const char* key);
esp_err_t nvs_erase_all(nvs_handle_t h);
