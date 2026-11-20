// FwUpdate.cpp - see FwUpdate.h
#include "FwUpdate.h"

#include "Channel.h"
#include "Logging.h"
#include "Report.h"  // git_info
#include "Settings.h"
#include "PendantLink.h"
#include "ExpanderMonitor.h"
#include "Driver/board_id.h"

#include <cctype>
#include <cstring>
#include <mutex>
#include <vector>

#ifdef ARDUINO_ARCH_ESP32
#    include "FwUpdateKey.h"
#    include <Arduino.h>  // millis
#    include <esp_ota_ops.h>
#    include <esp_image_format.h>
#    include <esp_system.h>
#    include <esp_timer.h>
#    include <nvs.h>
#    include <mbedtls/sha256.h>
#    include <mbedtls/pk.h>
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#    define FW_PRODUCT "fluidnc-s3"
#    define FW_CHIP "esp32s3"
#elif defined(CONFIG_IDF_TARGET_ESP32)
#    define FW_PRODUCT "fluidnc-wroom32"
#    define FW_CHIP "esp32"
#else
#    define FW_PRODUCT "fluidnc-host"
#    define FW_CHIP "host"
#endif

namespace FwUpdate {
    // Keep only what can't break a [UPD:...] line or a log line.
    static std::string clean(const std::string& s, size_t max = 40) {
        std::string r;
        for (char c : s) {
            if (r.size() >= max) {
                break;
            }
            if (isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_' || c == '+' || c == ' ' || c == '(' || c == ')') {
                r += c;
            }
        }
        return r;
    }

    void list(Channel& out) {
        std::string ver = clean(git_info, 60);
        log_stream(out, "[UPD:host," FW_PRODUCT "," FW_CHIP "," << board_id() << "," << ver << "," << how());

        // SmartTabUI pendant: "<chip>:<version>" from its hello. It can't take
        // an update through the board yet ("no": the WebUI offers USB).
        std::string p = pendant_info();
        if (!p.empty()) {
            size_t c = p.find(':');
            std::string chip = c == std::string::npos ? p : p.substr(0, c);
            std::string pv   = c == std::string::npos ? "" : p.substr(c + 1);
            log_stream(out, "[UPD:pendant,smarttabui,esp32," << clean(chip, 12) << "," << clean(pv) << ",no");
        }

        // IO16 expanders: "dpCREATOR IO16 v1.1.0" -> 1.1.0. Through the board: next step.
        for (auto* m : ExpanderMonitor::all()) {
            const std::string& id  = m->id();
            size_t             v   = id.rfind(" v");
            std::string        ver = v == std::string::npos ? "" : id.substr(v + 2);
            bool               on  = m->online(millis());
            log_stream(out, "[UPD:io16:" << m->channel() << ",io16,stm32,," << clean(ver) << ",no" << (on ? "" : ",offline"));
        }
    }

    static Error cmd_list(const char* value, AuthenticationLevel, Channel& out) {
        if (value && *value) {
            return Error::InvalidStatement;
        }
        list(out);
        return Error::Ok;
    }

    void make_commands() { new UserCommand("UPL", "Update/List", cmd_list, anyState); }

#ifdef ARDUINO_ARCH_ESP32
    // ---- NVS "dpc_upd": the trial after an update
    //   trial (u8)  crash starts of the new firmware so far (key there = on trial)
    //   prev  (str) label of the slot it came from
    //   ver   (str) the new version (as the WebUI named it)
    //   rolled(str) a version that failed and was left
    static const char* NS = "dpc_upd";

    static std::string nvs_str(nvs_handle_t h, const char* key) {
        char   buf[64] = "";
        size_t n       = sizeof(buf);
        if (nvs_get_str(h, key, buf, &n) != ESP_OK) {
            return "";
        }
        return buf;
    }

    static void confirm_cb(void*) {
        nvs_handle_t h;
        if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
            return;
        }
        std::string ver = nvs_str(h, "ver");
        nvs_erase_key(h, "trial");
        nvs_erase_key(h, "prev");
        nvs_erase_key(h, "rolled");
        nvs_commit(h);
        nvs_close(h);
        log_info("Firmware updated to " << ver);
    }

    void boot() {
        nvs_handle_t h;
        if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
            return;
        }
        uint8_t trial = 0;
        if (nvs_get_u8(h, "trial", &trial) != ESP_OK) {
            nvs_close(h);
            return;  // not just updated
        }
        // Only crashes count (a panic, a watchdog, a brown-out), not power-off or $Bye.
        esp_reset_reason_t why   = esp_reset_reason();
        bool               crash = why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT ||
                     why == ESP_RST_BROWNOUT;
        if (crash) {
            trial++;
        }
        if (trial >= 2) {
            // Back to where it came from. Before anything else runs (config, network).
            const esp_partition_t* prev = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nvs_str(h, "prev").c_str());
            nvs_set_str(h, "rolled", nvs_str(h, "ver").c_str());
            nvs_erase_key(h, "trial");
            nvs_commit(h);
            nvs_close(h);
            if (prev && esp_ota_set_boot_partition(prev) == ESP_OK) {
                esp_restart();
            }
            return;  // the old slot can't be used: stay (and keep crashing to the default config)
        }
        nvs_set_u8(h, "trial", trial);
        nvs_commit(h);
        nvs_close(h);
        // Ran a minute: the new firmware is good.
        static esp_timer_handle_t t;
        esp_timer_create_args_t   a = {};
        a.callback                  = confirm_cb;
        a.name                      = "fw_confirm";
        if (esp_timer_create(&a, &t) == ESP_OK) {
            esp_timer_start_once(t, 60ULL * 1000 * 1000);
        }
    }

    std::string rolled() {
        nvs_handle_t h;
        if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
            return "";
        }
        std::string r = nvs_str(h, "rolled");
        nvs_close(h);
        return r;
    }

    size_t slot_size() {
        const esp_partition_t* p = esp_ota_get_next_update_partition(NULL);
        return p ? p->size : 0;
    }
    bool        have_key() { return DPC_FW_PUBKEY[0] != 0; }
    const char* how() { return slot_size() && have_key() ? "slot" : "usb"; }
    const char* running() {
        const esp_partition_t* p = esp_ota_get_running_partition();
        return p ? p->label : "";
    }

    // ---- one update
    static std::recursive_mutex     mtx;
    static bool                     active = false;
    static esp_ota_handle_t         ota    = 0;
    static const esp_partition_t*   part   = nullptr;
    static mbedtls_sha256_context   sha;
    static size_t                   want = 0, got = 0;
    static std::vector<uint8_t>     sig;
    static std::string              newver;

    bool busy() { return active; }

    static bool unhex(const std::string& s, std::vector<uint8_t>& out) {
        out.clear();
        if (s.size() % 2 || s.size() > 2 * 80) {
            return false;
        }
        for (size_t i = 0; i < s.size(); i += 2) {
            auto d = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
            int  a = d(s[i]), b = d(s[i + 1]);
            if (a < 0 || b < 0) {
                return false;
            }
            out.push_back(uint8_t(a * 16 + b));
        }
        return !out.empty();
    }

    void abort() {
        std::lock_guard<std::recursive_mutex> l(mtx);
        if (!active) {
            return;
        }
        esp_ota_abort(ota);
        mbedtls_sha256_free(&sha);
        active = false;
        log_info("Firmware update stopped");
    }

    bool begin(size_t size, const std::string& sig_hex, const std::string& version, std::string& err) {
        std::lock_guard<std::recursive_mutex> l(mtx);
        if (active) {
            return err = "already updating", false;
        }
        part = esp_ota_get_next_update_partition(NULL);
        if (!part) {
            return err = "USB only on this board", false;
        }
        if (!have_key()) {
            return err = "no update key in this build", false;
        }
        if (!unhex(sig_hex, sig)) {
            return err = "not signed by dpCREATOR", false;
        }
        if (!size || size > part->size) {
            return err = "too big for the program slot", false;
        }
        if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
            return err = "can't open the program slot", false;
        }
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts(&sha, 0);
        want = size, got = 0, newver = clean(version), active = true;
        log_info("Firmware update " << newver << " (" << size << " bytes) into " << part->label);
        return true;
    }

    bool write(const uint8_t* data, size_t len, std::string& err) {
        std::lock_guard<std::recursive_mutex> l(mtx);
        if (!active) {
            return err = "no update started", false;
        }
        if (got == 0 && len >= sizeof(esp_image_header_t)) {
            auto* hd = reinterpret_cast<const esp_image_header_t*>(data);
            if (hd->magic != ESP_IMAGE_HEADER_MAGIC) {
                abort();
                return err = "not a firmware file", false;
            }
            if (hd->chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID) {
                abort();
                return err = "this file is for another chip", false;
            }
        }
        if (got + len > want) {
            abort();
            return err = "the file is bigger than it said", false;
        }
        if (esp_ota_write(ota, data, len) != ESP_OK) {
            abort();
            return err = "writing the program slot failed", false;
        }
        mbedtls_sha256_update(&sha, data, len);
        uint32_t before = (100 * got) / want;
        got += len;
        uint32_t now = (100 * got) / want;
        if (now / 10 != before / 10) {
            log_info("Firmware update " << now << "%");
        }
        return true;
    }

    static bool signed_ok(const uint8_t hash[32]) {
        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);
        bool ok = mbedtls_pk_parse_public_key(&pk, (const unsigned char*)DPC_FW_PUBKEY, strlen(DPC_FW_PUBKEY) + 1) == 0 &&
                  mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, sig.data(), sig.size()) == 0;
        mbedtls_pk_free(&pk);
        return ok;
    }

    bool finish(std::string& err) {
        std::lock_guard<std::recursive_mutex> l(mtx);
        if (!active) {
            return err = "no update started", false;
        }
        if (got != want) {
            abort();
            return err = "the file arrived incomplete", false;
        }
        uint8_t hash[32];
        mbedtls_sha256_finish(&sha, hash);
        if (!signed_ok(hash)) {
            abort();
            return err = "not signed by dpCREATOR", false;
        }
        active = false;
        mbedtls_sha256_free(&sha);
        if (esp_ota_end(ota) != ESP_OK) {  // checks the image
            return err = "not a valid firmware image", false;
        }
        nvs_handle_t h;
        if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_u8(h, "trial", 0);
            nvs_set_str(h, "prev", running());
            nvs_set_str(h, "ver", newver.c_str());
            nvs_erase_key(h, "rolled");
            nvs_commit(h);
            nvs_close(h);
        }
        if (esp_ota_set_boot_partition(part) != ESP_OK) {
            return err = "can't switch to the new firmware", false;
        }
        log_info("Firmware update checked and written: restarting into " << newver);
        return true;
    }
#else
    // Host build: no program slots.
    void        boot() {}
    std::string rolled() { return ""; }
    size_t      slot_size() { return 0; }
    bool        have_key() { return false; }
    const char* how() { return "usb"; }
    const char* running() { return ""; }
    bool        busy() { return false; }
    void        abort() {}
    bool        begin(size_t, const std::string&, const std::string&, std::string& err) { return err = "USB only on this board", false; }
    bool        write(const uint8_t*, size_t, std::string& err) { return err = "no update started", false; }
    bool        finish(std::string& err) { return err = "no update started", false; }
#endif
}
