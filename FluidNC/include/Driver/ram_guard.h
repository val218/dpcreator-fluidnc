// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Internal RAM guard.
//
// The ESP32-S3 has two kinds of RAM: ~300 KB of internal SRAM and the 8 MB
// PSRAM.  "Free memory" counts both, so it always looks healthy, while the
// internal part - the only RAM SPI DMA can use - can run out on its own.
// The SD card and the W5500 share one SPI bus; every SD command needs a few
// bytes of internal DMA RAM, and when that allocation fails, ESP-IDF's SPI
// driver (spi_master.c, setup_priv_desc -> uninstall_priv_desc) copies from a
// NULL pointer and the board crashes (LoadProhibited).  That is the crash seen
// while a job ran from the card and the pendant preview was being sent.
//
// This module
//   - keeps a small internal reserve that is handed back when such an
//     allocation fails, and waits briefly for RAM to free up, instead of
//     letting the driver crash;
//   - tells the preview builder/sender to pause while internal RAM is short,
//     so the running job keeps the card and the RAM it needs;
//   - records internal RAM low-water marks across a crash restart ($Crash)
//     and reports them in $Heap/Show.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

void ram_guard_init();   // at startup: take the reserve
void ram_guard_poll();   // often, from the protocol loop (does work once a second)
bool ram_tight();        // true: background work (previews) should wait

struct RamInfo {
    uint32_t int_free;      // internal RAM free now
    uint32_t int_largest;   // largest internal block now
    uint32_t int_min_free;  // lowest internal free since boot
    uint32_t dma_free;      // internal DMA-capable RAM free now
    uint32_t dma_largest;   // largest internal DMA-capable block now
    uint32_t psram_free;    // PSRAM free now (0 without PSRAM)
    uint32_t alloc_fails;   // DMA allocations that failed at first since boot
    uint32_t rescued;       // ... of those, saved by waiting / the reserve
    bool     reserve_held;  // the reserve is in place
};
RamInfo     ram_info();
std::string ram_report();  // one line for $Heap/Show and logs

// The previous run's numbers, kept in RTC RAM across a crash restart.
// Empty string when there is nothing (power-on, or first boot after flashing).
std::string ram_crash_report();
