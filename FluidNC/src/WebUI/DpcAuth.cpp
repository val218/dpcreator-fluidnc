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
#include <esp_timer.h>
#include <ctime>

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

    // Sessions: kept on the board (NVS), so logging out ends the key itself
    // (not just the browser's copy) and every key expires SESSION_MIN after
    // the login. The browser's cookie expires at the same time.
    struct Sess {
        uint8_t  id[8];
        uint32_t issued;  // board clock, minutes; 0 = free slot
        uint32_t life;    // minutes it lasts (30 days "stay logged in", else a shift)
    };
    constexpr int SLOTS = 16;
    Sess          sess[SLOTS];

    // Board clock in minutes: real time when the board has it (NTP), else the
    // last saved value plus the time since power-on (saved every 10 min), so
    // it never goes back and survives restarts. Without NTP it only counts
    // the time the board is on; the cookie's own expiry still counts days.
    uint32_t clockBase  = 1;  // board clock at power-on
    uint32_t clockSaved = 0;
    uint32_t now_min() {
        uint32_t t = clockBase + (uint32_t)(esp_timer_get_time() / 60000000LL);
        time_t   w = time(nullptr);
        if (w > 1700000000) {  // NTP time is set
            uint32_t wm = (uint32_t)(w / 60);
            if (wm > t) {
                clockBase += wm - t;  // move forward to real time (never back)
                t = wm;
            }
        }
        return t;
    }

    // Lock-out after wrong passwords, per client (IP address), so a stranger's
    // guesses never lock the owner out: 5 tries, then 30 s, doubling up to 5 min.
    struct Lock {
        uint32_t ip;
        int      fails;
        uint32_t until, step, seen;
    };
    constexpr int LOCKS = 8;
    Lock          locks[LOCKS];
    Lock&         lock_for(uint32_t ip) {
        int pick = -1;
        for (int i = 0; i < LOCKS; ++i)
            if (locks[i].step && locks[i].ip == ip) return locks[i];
        // a free entry, else the one least recently seen that is not locked out
        uint32_t now = millis(), best = 0;
        for (int i = 0; i < LOCKS; ++i) {
            if (!locks[i].step) { pick = i; break; }
            bool     locked = (int32_t)(locks[i].until - now) > 0;
            uint32_t age    = now - locks[i].seen + (locked ? 0 : 0x40000000u);
            if (pick < 0 || age > best) { pick = i; best = age; }
        }
        locks[pick] = { ip, 0, 0, 30000, now };
        return locks[pick];
    }

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
    void save_clock() {
        uint32_t t = now_min();
        if (t == clockSaved) return;
        nvs_handle_t h;
        if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
        if (nvs_set_u32(h, "clock", t) == ESP_OK && nvs_commit(h) == ESP_OK) clockSaved = t;
        nvs_close(h);
    }
    void store_sessions() {
        nvs_handle_t h;
        if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
        nvs_set_blob(h, "sess", sess, sizeof(sess));
        nvs_commit(h);
        nvs_close(h);
    }
    void load() {
        if (loaded) return;
        loaded = true;
        have   = false;
        memset(sess, 0, sizeof(sess));
        nvs_handle_t h;
        if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
        have = get_blob(h, "salt", salt, sizeof(salt)) && get_blob(h, "hash", hash, sizeof(hash)) && get_blob(h, "secret", secret, sizeof(secret));
        if (!get_blob(h, "sess", sess, sizeof(sess))) memset(sess, 0, sizeof(sess));
        static bool clockRead = false;
        if (!clockRead) {
            clockRead = true;
            uint32_t c = 0;
            if (nvs_get_u32(h, "clock", &c) == ESP_OK && c > clockBase) clockBase = c;
            clockSaved = clockBase;
        }
        nvs_close(h);
        // keep the board clock going across restarts
        static esp_timer_handle_t tick = nullptr;
        if (!tick) {
            esp_timer_create_args_t a = {};
            a.callback                = [](void*) {
                std::lock_guard<std::recursive_mutex> l(mtx);
                save_clock();
            };
            a.name = "dpcauth";
            if (esp_timer_create(&a, &tick) == ESP_OK) esp_timer_start_periodic(tick, 10ULL * 60 * 1000000);
        }
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
        have = true;
        memset(sess, 0, sizeof(sess));  // every session ends
        store_sessions();
        return true;
    }

    bool check_password(const std::string& pw, uint32_t client) {
        std::lock_guard<std::recursive_mutex> l(mtx);
        load();
        if (!have || lockout_ms(client)) return false;
        uint8_t h[32];
        pw_hash(salt, pw, h);
        Lock& k = lock_for(client);
        k.seen  = millis();
        if (same(h, hash, 32)) {
            k.fails = 0;
            k.step  = 30000;
            return true;
        }
        if (++k.fails >= 5) {
            k.fails = 0;
            k.until = millis() + k.step;
            log_warn("Login: too many wrong passwords from " << IPAddress(client) << ", locked for " << k.step / 1000 << " s");
            if (k.step < 300000) k.step *= 2;
        }
        return false;
    }

    uint32_t lockout_ms(uint32_t client) {
        std::lock_guard<std::recursive_mutex> l(mtx);
        for (auto& k : locks)
            if (k.step && k.ip == client) {
                int32_t left = (int32_t)(k.until - millis());
                return left > 0 ? (uint32_t)left : 0;
            }
        return 0;
    }

    uint32_t client(AsyncWebServerRequest* r) {
        AsyncClient* c = r->client();
        return c ? (uint32_t)c->remoteIP() : 0;
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
        memset(sess, 0, sizeof(sess));
        basicOk.clear();
        return true;
    }

    std::string new_key(uint32_t minutes) {
        std::lock_guard<std::recursive_mutex> l(mtx);
        load();
        uint32_t now = now_min();
        // a free or expired slot, else the oldest login makes room
        int pick = 0;
        for (int i = 0; i < SLOTS; ++i) {
            if (!sess[i].issued || now - sess[i].issued >= sess[i].life) { pick = i; break; }
            if (sess[i].issued < sess[pick].issued) pick = i;
        }
        uint8_t m[32];
        esp_fill_random(sess[pick].id, 8);
        sess[pick].issued = now;
        sess[pick].life   = minutes ? minutes : SESSION_MIN;
        store_sessions();
        save_clock();
        mac(sess[pick].id, m);
        return hex(sess[pick].id, 8) + hex(m, 16);
    }

    namespace {
        int find_key(const std::string& key) {  // slot of a valid, unexpired key, or -1
            if (!have || key.size() != 48) return -1;
            uint8_t id[8], got[16], m[32];
            if (!unhex(key.substr(0, 16), id, 8) || !unhex(key.substr(16), got, 16)) return -1;
            mac(id, m);
            if (!same(m, got, 16)) return -1;
            for (int i = 0; i < SLOTS; ++i) {
                if (sess[i].issued && same(sess[i].id, id, 8)) {
                    if (now_min() - sess[i].issued < sess[i].life) return i;
                    sess[i] = {};  // expired
                    store_sessions();
                    return -1;
                }
            }
            return -1;  // logged out, or pushed out by newer logins
        }
        std::string request_key(AsyncWebServerRequest* r) {
            std::string k = cookie(r, "dpc_key");
            if (k.empty() && r->hasHeader("X-DPC-Key")) k = r->getHeader("X-DPC-Key")->value().c_str();
            if (k.empty() && r->hasParam("dpck")) k = r->getParam("dpck")->value().c_str();
            return k;
        }
    }

    bool key_ok(const std::string& key) {
        std::lock_guard<std::recursive_mutex> l(mtx);
        load();
        return find_key(key) >= 0;
    }

    void end_session(AsyncWebServerRequest* r) {
        std::lock_guard<std::recursive_mutex> l(mtx);
        load();
        int i = find_key(request_key(r));
        if (i >= 0) {
            sess[i] = {};
            store_sessions();
        }
    }

    bool request_ok(AsyncWebServerRequest* r) {
        if (!is_set()) return false;
        std::string k = request_key(r);
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
                if (c != std::string::npos && check_password(up.substr(c + 1), client(r))) {
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
