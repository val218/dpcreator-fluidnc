// DpcAuth.cpp - see DpcAuth.h
#include "DpcAuth.h"

#include "Channel.h"
#include "Logging.h"
#include "string_util.h"

#include <ESPAsyncWebServer.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>
#include <nvs.h>
#include <Arduino.h>  // millis

#include <cstring>
#include <mutex>
#include <string>

namespace {
    const char*        NS     = "dpcauth";
    constexpr int      ROUNDS = 2000;  // password hashing: ~ten milliseconds, once per login
    constexpr uint8_t  VER    = 1;

    std::recursive_mutex mtx;
    bool                 loaded = false;
    bool                 have   = false;
    uint8_t              salt[16], hash[32], secret[32];
    std::string          basicOk;  // last Authorization header that matched (WebDAV sends it on every request)

    // Lock-out after wrong passwords: 5 tries, then 30 s, doubling up to 5 min.
    int      fails     = 0;
    uint32_t lockUntil = 0;
    uint32_t lockStep  = 30000;

    void pw_hash(const uint8_t s[16], const std::string& pw, uint8_t out[32]) {
        mbedtls_sha256_context c;
        mbedtls_sha256_init(&c);
        mbedtls_sha256_starts(&c, 0);
        mbedtls_sha256_update(&c, &VER, 1);
        mbedtls_sha256_update(&c, s, 16);
        mbedtls_sha256_update(&c, (const uint8_t*)pw.data(), pw.size());
        mbedtls_sha256_finish(&c, out);
        for (int i = 0; i < ROUNDS; ++i) {
            mbedtls_sha256_starts(&c, 0);
            mbedtls_sha256_update(&c, out, 32);
            mbedtls_sha256_update(&c, s, 16);
            mbedtls_sha256_update(&c, (const uint8_t*)pw.data(), pw.size());
            mbedtls_sha256_finish(&c, out);
        }
        mbedtls_sha256_free(&c);
    }
    bool same(const uint8_t* a, const uint8_t* b, size_t n) {  // constant time
        uint8_t d = 0;
        for (size_t i = 0; i < n; ++i) d |= a[i] ^ b[i];
        return d == 0;
    }
    bool get_blob(nvs_handle_t h, const char* key, void* out, size_t n) {
        size_t len = n;
        return nvs_get_blob(h, key, out, &len) == ESP_OK && len == n;
    }
    void load() {
        if (loaded) return;
        loaded = true;
        have   = false;
        nvs_handle_t h;
        if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
        have = get_blob(h, "salt", salt, sizeof(salt)) && get_blob(h, "hash", hash, sizeof(hash)) && get_blob(h, "secret", secret, sizeof(secret));
        nvs_close(h);
    }
    bool store() {
        nvs_handle_t h;
        if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
        bool ok = nvs_set_blob(h, "salt", salt, sizeof(salt)) == ESP_OK && nvs_set_blob(h, "hash", hash, sizeof(hash)) == ESP_OK &&
                  nvs_set_blob(h, "secret", secret, sizeof(secret)) == ESP_OK && nvs_commit(h) == ESP_OK;
        nvs_close(h);
        return ok;
    }
    std::string hex(const uint8_t* p, size_t n) {
        static const char* H = "0123456789abcdef";
        std::string        s;
        s.reserve(n * 2);
        for (size_t i = 0; i < n; ++i) {
            s += H[p[i] >> 4];
            s += H[p[i] & 15];
        }
        return s;
    }
    bool unhex(const std::string& s, uint8_t* out, size_t n) {
        if (s.size() != n * 2) return false;
        auto v = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
        for (size_t i = 0; i < n; ++i) {
            int a = v(s[2 * i]), b = v(s[2 * i + 1]);
            if (a < 0 || b < 0) return false;
            out[i] = (uint8_t)(a * 16 + b);
        }
        return true;
    }
    void mac(const uint8_t id[8], uint8_t out[32]) {
        mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), secret, sizeof(secret), id, 8, out);
    }
    std::string cookie(AsyncWebServerRequest* r, const char* name) {
        if (!r->hasHeader("Cookie")) return "";
        std::string c   = r->getHeader("Cookie")->value().c_str();
        std::string key = std::string(name) + "=";
        size_t      p   = 0;
        while ((p = c.find(key, p)) != std::string::npos) {
            if (p == 0 || c[p - 1] == ' ' || c[p - 1] == ';') {
                size_t e = c.find(';', p);
                return c.substr(p + key.size(), e == std::string::npos ? std::string::npos : e - p - key.size());
            }
            p += key.size();
        }
        return "";
    }
    std::string b64decode(const std::string& in) {
        auto v = [](char c) {
            return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
        };
        std::string out;
        uint32_t    acc = 0;
        int         bits = 0;
        for (char c : in) {
            int x = v(c);
            if (x < 0) continue;
            acc = (acc << 6) | (uint32_t)x;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out += (char)((acc >> bits) & 0xFF);
            }
        }
        return out;
    }
    bool physical(Channel& out) {  // USB or a UART: someone at the machine
        std::string n = out.name();
        return n.rfind("uart_channel", 0) == 0 || n == "usbcdc";
    }
    void say(Channel& out, const std::string& s) { out.print(s.c_str()); }
    bool ends_with_ci(const std::string& s, const char* e) {
        size_t n = strlen(e);
        return s.size() >= n && strcasecmp(s.c_str() + s.size() - n, e) == 0;
    }
}

namespace DpcAuth {
    bool is_set() {
        std::lock_guard<std::recursive_mutex> l(mtx);
        load();
        return have;
    }

    bool set_password(const std::string& pw, std::string& err) {
        if (pw.size() < MIN_LEN) { err = "at least " + std::to_string(MIN_LEN) + " characters"; return false; }
        if (pw.size() > MAX_LEN) { err = "at most " + std::to_string(MAX_LEN) + " characters"; return false; }
        for (char c : pw) if ((unsigned char)c < 0x20) { err = "no control characters"; return false; }
        std::lock_guard<std::recursive_mutex> l(mtx);
        load();
        esp_fill_random(salt, sizeof(salt));
        esp_fill_random(secret, sizeof(secret));  // every session key so far ends
        pw_hash(salt, pw, hash);
        basicOk.clear();
        if (!store()) { loaded = false; load(); err = "could not save it (NVS)"; return false; }
        have  = true;
        fails = 0;
        return true;
    }

    bool check_password(const std::string& pw) {
        std::lock_guard<std::recursive_mutex> l(mtx);
        load();
        if (!have || lockout_ms()) return false;
        uint8_t h[32];
        pw_hash(salt, pw, h);
        if (same(h, hash, 32)) {
            fails    = 0;
            lockStep = 30000;
            return true;
        }
        if (++fails >= 5) {
            fails     = 0;
            lockUntil = millis() + lockStep;
            if (lockStep < 300000) lockStep *= 2;
            log_warn("Login: too many wrong passwords, locked for " << lockStep / 2000 << " s");
        }
        return false;
    }

    uint32_t lockout_ms() {
        int32_t left = (int32_t)(lockUntil - millis());
        return left > 0 ? (uint32_t)left : 0;
    }

    bool clear() {
        std::lock_guard<std::recursive_mutex> l(mtx);
        nvs_handle_t h;
        if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_all(h);
            nvs_commit(h);
            nvs_close(h);
        }
        loaded = false;
        have   = false;
        basicOk.clear();
        return true;
    }

    std::string new_key() {
        std::lock_guard<std::recursive_mutex> l(mtx);
        load();
        uint8_t id[8], m[32];
        esp_fill_random(id, sizeof(id));
        mac(id, m);
        return hex(id, 8) + hex(m, 16);
    }

    bool key_ok(const std::string& key) {
        std::lock_guard<std::recursive_mutex> l(mtx);
        load();
        if (!have || key.size() != 48) return false;
        uint8_t id[8], got[16], m[32];
        if (!unhex(key.substr(0, 16), id, 8) || !unhex(key.substr(16), got, 16)) return false;
        mac(id, m);
        return same(m, got, 16);
    }

    bool request_ok(AsyncWebServerRequest* r) {
        if (!is_set()) return false;
        std::string k = cookie(r, "dpc_key");
        if (k.empty() && r->hasHeader("X-DPC-Key")) k = r->getHeader("X-DPC-Key")->value().c_str();
        if (k.empty() && r->hasParam("dpck")) k = r->getParam("dpck")->value().c_str();
        if (!k.empty() && key_ok(k)) return true;
        // HTTP Basic (WebDAV drive on the PC, scripts): any user name, the password
        if (r->hasHeader("Authorization")) {
            std::string a = r->getHeader("Authorization")->value().c_str();
            if (a.rfind("Basic ", 0) == 0) {
                {
                    std::lock_guard<std::recursive_mutex> l(mtx);
                    if (!basicOk.empty() && a == basicOk) return true;
                }
                std::string up = b64decode(a.substr(6));
                size_t      c  = up.find(':');
                if (c != std::string::npos && check_password(up.substr(c + 1))) {
                    std::lock_guard<std::recursive_mutex> l(mtx);
                    basicOk = a;
                    return true;
                }
            }
        }
        return false;
    }

    bool is_public(AsyncWebServerRequest* r) {
        std::string url = r->url().c_str();
        if (url.rfind("/dpc/auth", 0) == 0) return true;  // the routes check for themselves
        bool get = r->method() == HTTP_GET || r->method() == HTTP_HEAD;
        if (!get || r->isWebSocketUpgrade()) return false;
        if (url == "/" || url == "/generate_204" || url == "/fwlink/") return true;  // the page (it asks for a login)
        if (url.rfind("/sd/", 0) == 0) return false;
        std::string u = url;
        if (ends_with_ci(u, ".gz")) u.resize(u.size() - 3);
        // the WebUI / Studio pages and their files; never config.yaml, tools.json, G-code...
        static const char* const ext[] = { ".html", ".htm", ".js", ".css", ".png", ".jpg", ".jpeg", ".svg", ".ico", ".gif", ".webp", ".woff", ".woff2", ".ttf", ".webmanifest" };
        for (auto e : ext)
            if (ends_with_ci(u, e)) return true;
        return false;
    }

    bool secret_line(const char* line) {
        std::string s = line;
        for (auto& c : s) c = (char)tolower((unsigned char)c);
        return s.find("$auth/set") != std::string::npos || s.find("$auth/change") != std::string::npos || s.find("password") != std::string::npos ||
               s.find("[esp101]") != std::string::npos || s.find("[esp106]") != std::string::npos || s.find("[esp555]") != std::string::npos;
    }

    bool command(const char* line, Channel& out) {
        if (strncasecmp(line, "$Auth", 5) != 0 || (line[5] != 0 && line[5] != '/')) return false;
        const char* cmd = line[5] ? line + 6 : "Status";
        if (strcasecmp(cmd, "Status") == 0) {
            say(out, std::string("[MSG:Auth:") + (is_set() ? "set" : "unset") + "]\n");
            return true;
        }
        if (strncasecmp(cmd, "Set=", 4) == 0) {
            // At the machine (USB / UART) any time; from the network only while
            // none is set (first start) - else the WebUI's change, with the old one.
            if (is_set() && !physical(out)) {
                say(out, "[MSG:Auth:err:a password is set - change it in the WebUI settings, or over USB]\n");
                return true;
            }
            std::string err;
            if (set_password(cmd + 4, err)) {
                say(out, "[MSG:Auth:set]\n");
                log_info("Login password set" << (physical(out) ? " over " : " from ") << out.name());
            } else {
                say(out, "[MSG:Auth:err:" + err + "]\n");
            }
            return true;
        }
        if (strcasecmp(cmd, "Clear") == 0) {
            if (!physical(out)) {
                say(out, "[MSG:Auth:err:only over USB, at the machine]\n");
                return true;
            }
            clear();
            say(out, "[MSG:Auth:unset]\n");
            log_info("Login password removed: the WebUI asks for a new one");
            return true;
        }
        say(out, "[MSG:Auth:err:$Auth/Status, $Auth/Set=<password>, $Auth/Clear]\n");
        return true;
    }
}

// The hook ProcessSettings.cpp calls (weak there, for builds without the WebUI).
bool dpc_auth_command(const char* line, Channel& out) {
    return DpcAuth::command(line, out);
}
