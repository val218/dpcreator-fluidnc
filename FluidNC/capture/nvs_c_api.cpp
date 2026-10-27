// Host build: see nvs.h
#include "nvs.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace stdfs = std::filesystem;

namespace {
    std::mutex                         mtx;
    std::map<nvs_handle_t, std::string> open_ns;
    nvs_handle_t                       next_handle = 1;

    stdfs::path file(nvs_handle_t h, const char* key) {
        return stdfs::path("native_nvs") / (open_ns[h] + "." + key + ".nvs");
    }
    bool read_all(const stdfs::path& p, std::vector<char>& out) {
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), {});
        return true;
    }
    esp_err_t write_all(const stdfs::path& p, const void* data, size_t n) {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f) return ESP_FAIL;
        f.write(static_cast<const char*>(data), n);
        return f ? ESP_OK : ESP_FAIL;
    }
    template <typename T>
    esp_err_t get_num(nvs_handle_t h, const char* key, T* out) {
        std::vector<char> v;
        if (!read_all(file(h, key), v)) return ESP_ERR_NVS_NOT_FOUND;
        if (v.size() != sizeof(T)) return ESP_ERR_NVS_INVALID_LENGTH;
        memcpy(out, v.data(), sizeof(T));
        return ESP_OK;
    }
}

esp_err_t nvs_open(const char* ns, nvs_open_mode_t, nvs_handle_t* out) {
    std::lock_guard<std::mutex> l(mtx);
    stdfs::create_directories("native_nvs");
    *out          = next_handle++;
    open_ns[*out] = ns;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) {
    std::lock_guard<std::mutex> l(mtx);
    open_ns.erase(h);
}
esp_err_t nvs_commit(nvs_handle_t) {
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* len) {
    std::lock_guard<std::mutex> l(mtx);
    std::vector<char>           v;
    if (!read_all(file(h, key), v)) return ESP_ERR_NVS_NOT_FOUND;
    if (out) {
        if (*len < v.size()) return ESP_ERR_NVS_INVALID_LENGTH;
        memcpy(out, v.data(), v.size());
    }
    *len = v.size();
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* value, size_t len) {
    std::lock_guard<std::mutex> l(mtx);
    return write_all(file(h, key), value, len);
}
esp_err_t nvs_get_u32(nvs_handle_t h, const char* key, uint32_t* out) {
    std::lock_guard<std::mutex> l(mtx);
    return get_num(h, key, out);
}
esp_err_t nvs_set_u32(nvs_handle_t h, const char* key, uint32_t value) {
    std::lock_guard<std::mutex> l(mtx);
    return write_all(file(h, key), &value, sizeof(value));
}
esp_err_t nvs_get_i32(nvs_handle_t h, const char* key, int32_t* out) {
    std::lock_guard<std::mutex> l(mtx);
    return get_num(h, key, out);
}
esp_err_t nvs_set_i32(nvs_handle_t h, const char* key, int32_t value) {
    std::lock_guard<std::mutex> l(mtx);
    return write_all(file(h, key), &value, sizeof(value));
}
esp_err_t nvs_get_str(nvs_handle_t h, const char* key, char* out, size_t* len) {
    std::lock_guard<std::mutex> l(mtx);
    std::vector<char>           v;
    if (!read_all(file(h, key), v)) return ESP_ERR_NVS_NOT_FOUND;
    if (out) {
        if (*len < v.size() + 1) return ESP_ERR_NVS_INVALID_LENGTH;
        memcpy(out, v.data(), v.size());
        out[v.size()] = 0;
    }
    *len = v.size() + 1;
    return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h, const char* key, const char* value) {
    std::lock_guard<std::mutex> l(mtx);
    return write_all(file(h, key), value, strlen(value));
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char* key) {
    std::lock_guard<std::mutex> l(mtx);
    std::error_code             ec;
    return stdfs::remove(file(h, key), ec) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_erase_all(nvs_handle_t h) {
    std::lock_guard<std::mutex> l(mtx);
    std::string                 prefix = open_ns[h] + ".";
    std::error_code             ec;
    for (auto& e : stdfs::directory_iterator("native_nvs", ec)) {
        std::string n = e.path().filename().string();
        if (n.rfind(prefix, 0) == 0 && n.size() > 4 && n.compare(n.size() - 4, 4, ".nvs") == 0) stdfs::remove(e.path(), ec);
    }
    return ESP_OK;
}
