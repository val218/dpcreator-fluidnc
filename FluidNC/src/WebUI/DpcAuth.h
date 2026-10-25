// DpcAuth.h - dpCREATOR login for the board's network side.
//
// One admin password, chosen by the user (no default password exists):
//   - over USB / a UART at any time:      $Auth/Set=<password>   (physical access)
//   - on the first visit to the WebUI, while none is set:  POST /dpc/auth/setup
//   - changed in the WebUI's settings:    POST /dpc/auth/change  (old + new)
//   - removed over USB / a UART only:     $Auth/Clear
// Only a salted, iterated SHA-256 of it is stored (NVS). A login gives a
// session key, an HMAC of a random id with a secret kept in NVS: keys survive a
// restart and all of them end when the password changes.
// Every HTTP route, the WebSocket, WebDAV and telnet need a key or the
// password (WebUIServer.cpp middleware); only the pages themselves and the
// login routes are open, so the login page can load.
#pragma once
#include <cstdint>
#include <string>

class Channel;
class AsyncWebServerRequest;

namespace DpcAuth {
    constexpr size_t MIN_LEN = 8;
    constexpr size_t MAX_LEN = 64;

    bool        is_set();
    bool        set_password(const std::string& pw, std::string& err);  // stores it, ends every session
    bool        check_password(const std::string& pw);                  // with lock-out after repeated failures
    bool        clear();                                                // remove the password (USB only)
    std::string new_key();                                              // a session key
    bool        key_ok(const std::string& key);
    uint32_t    lockout_ms();                                           // >0: too many wrong passwords, wait

    // Is this request allowed? (session cookie, X-DPC-Key header, dpck param, or HTTP Basic)
    bool request_ok(AsyncWebServerRequest* request);
    // Pages and login routes that load without a login.
    bool is_public(AsyncWebServerRequest* request);

    // $Auth/... commands; true = it was one (handled).
    bool command(const char* line, Channel& out);
    // A line that must not be kept in logs / crash trails (it holds a password).
    bool secret_line(const char* line);
}
