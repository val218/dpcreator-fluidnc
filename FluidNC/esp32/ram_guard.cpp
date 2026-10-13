// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// See Driver/ram_guard.h.

#include "Driver/ram_guard.h"
#include "CrashTrail.h"
#include "Logging.h"

#include <Arduino.h>  // millis
#include <atomic>
#include <cstdio>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
    // Internal DMA RAM kept back for the moment an SPI transfer cannot get any.
    constexpr size_t RESERVE_BYTES = 8 * 1024;
    // Take the reserve again once a block this big is free (the reserve plus
    // room to spare), so taking it back does not itself cause the next miss.
    constexpr size_t RETAKE_LARGEST = RESERVE_BYTES + 8 * 1024;
    // Previews wait while the reserve is out (an SPI transfer could not get
    // RAM) or while the largest internal DMA block is this small.  No fixed
    // "free" threshold: how much a healthy board has free is not known for
    // every config, and a guess too high would stop previews for good.
    constexpr size_t TIGHT_LARGEST = 2 * 1024;

    std::atomic<void*>    s_reserve { nullptr };
    std::atomic<uint32_t> s_fails { 0 };
    std::atomic<uint32_t> s_rescued { 0 };
    std::atomic<uint32_t> s_reserve_spent { 0 };
    uint32_t              s_reported_fails = 0;
    uint32_t              s_last_poll_ms   = 0;
    bool                  s_tight          = false;

    // Kept across a crash restart (not across a power cut).
    struct RtcRam {
        uint32_t magic;
        uint32_t uptime_s;
        uint32_t int_free;
        uint32_t int_min_free;
        uint32_t dma_largest;
        uint32_t fails;
        uint32_t rescued;
        uint32_t reserve_spent;
    };
    constexpr uint32_t     RTC_MAGIC = 0x52414D31;  // "RAM1"
    RTC_NOINIT_ATTR RtcRam s_rtc;
    RtcRam                 s_prev;
    bool                   s_have_prev = false;

    constexpr uint32_t CAP_INT = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    constexpr uint32_t CAP_DMA = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;

    void take_reserve() {
        if (s_reserve.load()) {
            return;
        }
        void* r = heap_caps_malloc(RESERVE_BYTES, CAP_DMA);
        void* expected = nullptr;
        if (r && !s_reserve.compare_exchange_strong(expected, r)) {
            heap_caps_free(r);
        }
    }

    void rtc_update() {
        s_rtc.magic         = RTC_MAGIC;
        s_rtc.uptime_s      = millis() / 1000;
        s_rtc.int_free      = heap_caps_get_free_size(CAP_INT);
        s_rtc.int_min_free  = heap_caps_get_minimum_free_size(CAP_INT);
        s_rtc.dma_largest   = heap_caps_get_largest_free_block(CAP_DMA);
        s_rtc.fails         = s_fails.load();
        s_rtc.rescued       = s_rescued.load();
        s_rtc.reserve_spent = s_reserve_spent.load();
    }
}

// ---------------------------------------------------------------------------
// heap_caps_aligned_alloc() wrapper (-Wl,--wrap=heap_caps_aligned_alloc).
//
// ESP-IDF's SPI master driver gets a temporary DMA-capable copy of any buffer
// that is not (word-aligned internal RAM) with heap_caps_aligned_alloc().  If
// that fails it "cleans up" by copying from a NULL pointer and the board
// crashes.  So when a DMA allocation fails here: hand back the reserve, and,
// where the caller may wait, give the other tasks (network, output) a moment
// to free RAM, then try again.  Only the failure path does anything extra.
// ---------------------------------------------------------------------------
#ifdef FNC_DMA_ALLOC_GUARD
extern "C" void* __real_heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps);

static void* __attribute__((noinline)) dma_alloc_retry(size_t alignment, size_t size, uint32_t caps) {
    // 1. The reserve: no waiting, so the transfer (and the job) goes on.
    void* r = s_reserve.exchange(nullptr);
    if (r) {
        heap_caps_free(r);
        s_reserve_spent.fetch_add(1);
        void* p = __real_heap_caps_aligned_alloc(alignment, size, caps);
        if (p) {
            s_rescued.fetch_add(1);
            return p;
        }
    }
    // 2. Wait for RAM, up to ~100 ms (the SPI transfer has not started yet).
    if (!xPortCanYield() || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return nullptr;
    }
    for (int i = 0; i < 50; ++i) {
        vTaskDelay(pdMS_TO_TICKS(2));
        void* p = __real_heap_caps_aligned_alloc(alignment, size, caps);
        if (p) {
            s_rescued.fetch_add(1);
            return p;
        }
    }
    return nullptr;
}

extern "C" void* IRAM_ATTR __wrap_heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
    void* p = __real_heap_caps_aligned_alloc(alignment, size, caps);
    if (p || !(caps & MALLOC_CAP_DMA) || xPortInIsrContext()) {
        return p;
    }
    s_fails.fetch_add(1);
    p = dma_alloc_retry(alignment, size, caps);
    s_rtc.fails   = s_fails.load();
    s_rtc.rescued = s_rescued.load();
    return p;
}
#endif

void ram_guard_init() {
    s_have_prev = s_rtc.magic == RTC_MAGIC;
    if (s_have_prev) {
        s_prev = s_rtc;
    }
    take_reserve();
    rtc_update();
    log_info("RAM: " << ram_report());
}

void ram_guard_poll() {
    uint32_t now = millis();
    if (now - s_last_poll_ms < 1000) {
        return;
    }
    s_last_poll_ms = now;

    size_t dma_free    = heap_caps_get_free_size(CAP_DMA);
    size_t dma_largest = heap_caps_get_largest_free_block(CAP_DMA);
    if (!s_reserve.load() && dma_largest >= RETAKE_LARGEST) {
        take_reserve();
        if (s_reserve.load()) {
            log_info("Internal RAM recovered (" << dma_free / 1024 << " KB free)");
        }
    }
    s_tight = !s_reserve.load() || dma_largest < TIGHT_LARGEST;

    uint32_t fails = s_fails.load();
    if (fails != s_reported_fails) {
        uint32_t n       = fails - s_reported_fails;
        s_reported_fails = fails;
        log_warn("Internal RAM ran out: " << n << " SPI transfer(s) had to wait (" << ram_report() << ")");
        char note[40];
        snprintf(note, sizeof(note), "!RAM low: dma %u free", (unsigned)dma_free);
        crash_trail_note(note);
    }
    rtc_update();
}

bool ram_tight() {
    return s_tight;
}

RamInfo ram_info() {
    RamInfo i;
    i.int_free     = heap_caps_get_free_size(CAP_INT);
    i.int_largest  = heap_caps_get_largest_free_block(CAP_INT);
    i.int_min_free = heap_caps_get_minimum_free_size(CAP_INT);
    i.dma_free     = heap_caps_get_free_size(CAP_DMA);
    i.dma_largest  = heap_caps_get_largest_free_block(CAP_DMA);
    i.psram_free   = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    i.alloc_fails  = s_fails.load();
    i.rescued      = s_rescued.load();
    i.reserve_held = s_reserve.load() != nullptr;
    return i;
}

std::string ram_report() {
    RamInfo i = ram_info();
    char    b[200];
    snprintf(b,
             sizeof(b),
             "internal %u KB free (lowest %u KB), largest block %u KB, DMA %u KB, PSRAM %u KB, DMA alloc fails %u (saved %u), reserve %s",
             (unsigned)(i.int_free / 1024),
             (unsigned)(i.int_min_free / 1024),
             (unsigned)(i.int_largest / 1024),
             (unsigned)(i.dma_free / 1024),
             (unsigned)(i.psram_free / 1024),
             (unsigned)i.alloc_fails,
             (unsigned)i.rescued,
             i.reserve_held ? "held" : "in use");
    return b;
}

std::string ram_crash_report() {
    if (!s_have_prev) {
        return "";
    }
    char b[200];
    snprintf(b,
             sizeof(b),
             "up %us, internal %u KB free (lowest %u KB), DMA largest %u KB, DMA alloc fails %u (saved %u), reserve used %u",
             (unsigned)s_prev.uptime_s,
             (unsigned)(s_prev.int_free / 1024),
             (unsigned)(s_prev.int_min_free / 1024),
             (unsigned)(s_prev.dma_largest / 1024),
             (unsigned)s_prev.fails,
             (unsigned)s_prev.rescued,
             (unsigned)s_prev.reserve_spent);
    return b;
}
