// Host build: ESP-IDF esp_timer on std::chrono and a thread per periodic timer.
#pragma once
#include "esp_err.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

inline int64_t esp_timer_get_time() {
    static const auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
}

typedef void (*esp_timer_cb_t)(void* arg);
typedef enum { ESP_TIMER_TASK = 0 } esp_timer_dispatch_t;
typedef struct {
    esp_timer_cb_t       callback;
    void*                arg;
    esp_timer_dispatch_t dispatch_method;
    const char*          name;
    bool                 skip_unhandled_events;
} esp_timer_create_args_t;

struct esp_timer {
    esp_timer_create_args_t args;
    std::atomic<bool>       run { false };
    std::atomic<uint64_t>   gen { 0 };
};
typedef esp_timer* esp_timer_handle_t;

inline esp_err_t esp_timer_create(const esp_timer_create_args_t* a, esp_timer_handle_t* out) {
    *out         = new esp_timer();
    (*out)->args = *a;
    return ESP_OK;
}
inline esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t period_us) {
    t->run    = true;
    uint64_t g = ++t->gen;
    std::thread([t, period_us, g] {
        while (t->run && t->gen == g) {
            std::this_thread::sleep_for(std::chrono::microseconds(period_us));
            if (t->run && t->gen == g) t->args.callback(t->args.arg);
        }
    }).detach();
    return ESP_OK;
}
inline esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t timeout_us) {
    t->run    = true;
    uint64_t g = ++t->gen;
    std::thread([t, timeout_us, g] {
        std::this_thread::sleep_for(std::chrono::microseconds(timeout_us));
        if (t->run && t->gen == g) {
            t->run = false;
            t->args.callback(t->args.arg);
        }
    }).detach();
    return ESP_OK;
}
inline esp_err_t esp_timer_stop(esp_timer_handle_t t) {
    t->run = false;
    ++t->gen;
    return ESP_OK;
}
inline esp_err_t esp_timer_delete(esp_timer_handle_t t) {
    esp_timer_stop(t);
    return ESP_OK;  // leaked on purpose: a sleeping thread may still look at it
}
