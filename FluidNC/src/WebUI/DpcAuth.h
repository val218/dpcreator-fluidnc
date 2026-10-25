// DpcAuth.h - dpCREATOR login for the board's network side.
//
// One admin password, chosen by the user (no default password exists):
//   - over USB / a UART at any time:      $Auth/Set=<password>   (physical access)
//   - on the first visit to the WebUI, while none is set:  POST /dpc/auth/setup
//   - changed in the WebUI's settings:    POST /dpc/auth/change  (old + new)
//   - removed over USB / a UART only:     $Auth/Clear
// Only a salted, iterated SHA-256 of it is stored (NVS). A login gives a
// session key (a random id + its HMAC with a secret kept in NVS). The board
// keeps the list of logins (NVS, up to 16): a key ends when that browser logs
// out, 12 h after the login (30 days with "stay logged in"), and all of them
// when the password changes.
// Wrong passwords lock out only the address they come from.
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
    constexpr uint32_t SESSION_MIN = 30 * 24 * 60;  // "stay logged in": 30 days
    constexpr uint32_t SHIFT_MIN   = 12 * 60;       // otherwise: 12 hours, and the browser forgets it when closed

    bool        is_set();
    bool        set_password(const std::string& pw, std::string& err);  // stores it, ends every session
    bool        check_password(const std::string& pw, uint32_t client);  // with lock-out per client (IP) after repeated failures
    bool        clear();                                                // remove the password (USB only)
    std::string new_key(uint32_t minutes = SESSION_MIN);                 // a session key lasting that long
    bool        key_ok(const std::string& key);
    void        end_session(AsyncWebServerRequest* request);           // log out: this request's key stops working
    uint32_t    lockout_ms(uint32_t client);                            // >0: too many wrong passwords from there, wait
    uint32_t    client(AsyncWebServerRequest* request);                // the requester's IP, for the lock-out

    // Is this request allowed? (session cookie, X-DPC-Key header, dpck param, or HTTP Basic)
    bool request_ok(AsyncWebServerRequest* request);
    // Pages and login routes that load without a login.
    bool is_public(AsyncWebServerRequest* request);

    // $Auth/... commands; true = it was one (handled).
    bool command(const char* line, Channel& out);
    // A line that must not be kept in logs / crash trails (it holds a password).
    bool secret_line(const char* line);
}
