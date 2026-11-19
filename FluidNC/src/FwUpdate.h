// FwUpdate.h - firmware updates from the dpCREATOR WebUI (System update).
//
// The WebUI downloads a build from dpcreator.eu and posts it to the board
// (POST /dpc/update, WebUIServer.cpp). The board:
//   - takes it only when Idle, logged in, and signed with the dpCREATOR key
//     (ECDSA P-256 over SHA-256 of the file; public key in FwUpdateKey.h);
//   - writes it into the spare program slot (boards with two: the R3 /
//     ESP32-S3 8 MB+; the WROOM-32 build has one slot: USB only);
//   - switches only after the signature and the image check, restarts;
//   - goes back to the previous slot when the new firmware crashes twice
//     before it has run for a minute.
//
//   $Update/List ($UPL)  what can be updated here, for the WebUI:
//     [UPD:host,<product>,<chip>,<id>,<version>,<slot|usb>]
//     [UPD:pendant,smarttabui,esp32,<chip id>,<version>,no]
//     [UPD:io16:<ch>,io16,stm32,,<version>,no[,offline]]
//
// Protocol for all devices: dpcreator-webui docs/UPDATES.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

class Channel;

namespace FwUpdate {
    void boot();  // first thing in setup(): count a trial start, go back after two crashes
    void list(Channel& out);

    // The spare program slot: its size, 0 = none (USB updates only).
    size_t      slot_size();
    bool        have_key();
    const char* how();      // "slot" or "usb"
    const char* running();  // label of the running slot ("app0")
    std::string rolled();   // the version that failed to start and was left, "" if none

    // One update at a time. Each returns false with the reason in words.
    bool begin(size_t size, const std::string& sig_hex, const std::string& version, std::string& err);
    bool write(const uint8_t* data, size_t len, std::string& err);
    bool finish(std::string& err);  // checks size, signature, image; switches slot
    void abort();
    bool busy();

    void make_commands();
}
