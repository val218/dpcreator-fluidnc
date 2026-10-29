// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
//
// dpCREATOR head camera - an ESP32 camera module as the WebUI's camera crosshair.
//
//   http://<camera>/capture      one JPEG, the newest frame (snapshots for the WebUI)
//   http://<camera>:81/stream    MJPEG
//   http://<camera>/             preview and settings
//   http://<camera>/status       settings as JSON
//   http://<camera>/control?var=framesize&val=8    (framesize, quality, vflip, hmirror, rotate180,
//                                brightness, contrast, led)
// Every answer has Access-Control-Allow-Origin: *, so a WebUI page on the
// controller can use the pictures (also read their pixels).
//
// Wi-Fi: type on the USB serial port (115200)
//   wifi <network> <password>       (a network name with spaces: wifi "My Net" password)
//   host <name>                     mDNS name, default dpc-camera -> http://dpc-camera.local
//   status | restart | forget | help
// With no Wi-Fi set, or when it can't connect, the camera opens its own network
// "dpc-camera-XXXX" (password dpccamera); http://192.168.4.1 then sets the Wi-Fi.
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"

// ------------------------------------------------------------------ camera pins
#if defined(CAM_S3_EYE)  // ESP32-S3-EYE, Freenove ESP32-S3-WROOM CAM
static const char* BOARD = "ESP32-S3-EYE / Freenove S3 CAM";
#    define PWDN -1
#    define RESET -1
#    define XCLK 15
#    define SIOD 4
#    define SIOC 5
#    define Y9 16
#    define Y8 17
#    define Y7 18
#    define Y6 12
#    define Y5 10
#    define Y4 8
#    define Y3 9
#    define Y2 11
#    define VSYNC 6
#    define HREF 7
#    define PCLK 13
#elif defined(CAM_XIAO_S3)  // Seeed XIAO ESP32S3 Sense
static const char* BOARD = "XIAO ESP32S3 Sense";
#    define PWDN -1
#    define RESET -1
#    define XCLK 10
#    define SIOD 40
#    define SIOC 39
#    define Y9 48
#    define Y8 11
#    define Y7 12
#    define Y6 14
#    define Y5 16
#    define Y4 18
#    define Y3 17
#    define Y2 15
#    define VSYNC 38
#    define HREF 47
#    define PCLK 13
#elif defined(CAM_AI_THINKER)  // AI-Thinker ESP32-CAM
static const char* BOARD = "AI-Thinker ESP32-CAM";
#    define PWDN 32
#    define RESET -1
#    define XCLK 0
#    define SIOD 26
#    define SIOC 27
#    define Y9 35
#    define Y8 34
#    define Y7 39
#    define Y6 36
#    define Y5 21
#    define Y4 19
#    define Y3 18
#    define Y2 5
#    define VSYNC 25
#    define HREF 23
#    define PCLK 22
#    define LED_PIN 4  // the bright flash LED: light for the work area
#else
#    error "Choose a camera board: -DCAM_S3_EYE, -DCAM_XIAO_S3 or -DCAM_AI_THINKER"
#endif

static const char* VERSION = "1.0";
static const char* AP_PASS = "dpccamera";

// ------------------------------------------------------------------ settings
struct Settings {
    String ssid, pass, host = "dpc-camera";
    int    framesize = FRAMESIZE_VGA;  // 640 x 480
    int    quality   = 12;             // JPEG 4 (best) .. 63
    bool   vflip = false, hmirror = false;
    int    brightness = 0, contrast = 0;
    int    led        = 0;  // 0..255
} S;
static Preferences prefs;

static void load() {
    prefs.begin("dpccam", true);
    S.ssid       = prefs.getString("ssid", "");
    S.pass       = prefs.getString("pass", "");
    S.host       = prefs.getString("host", "dpc-camera");
    S.framesize  = prefs.getInt("fs", FRAMESIZE_VGA);
    S.quality    = prefs.getInt("q", 12);
    S.vflip      = prefs.getBool("vf", false);
    S.hmirror    = prefs.getBool("hm", false);
    S.brightness = prefs.getInt("br", 0);
    S.contrast   = prefs.getInt("ct", 0);
    S.led        = prefs.getInt("led", 0);
    prefs.end();
}
static void save() {
    prefs.begin("dpccam", false);
    prefs.putString("ssid", S.ssid);
    prefs.putString("pass", S.pass);
    prefs.putString("host", S.host);
    prefs.putInt("fs", S.framesize);
    prefs.putInt("q", S.quality);
    prefs.putBool("vf", S.vflip);
    prefs.putBool("hm", S.hmirror);
    prefs.putInt("br", S.brightness);
    prefs.putInt("ct", S.contrast);
    prefs.putInt("led", S.led);
    prefs.end();
}

// ------------------------------------------------------------------ output to every console
static void say(const String& s) {
    Serial.println(s);
#if ARDUINO_USB_CDC_ON_BOOT && defined(CONFIG_IDF_TARGET_ESP32S3)
    Serial0.println(s);  // boards with a USB-UART chip as well (Freenove)
#endif
}

// ------------------------------------------------------------------ camera
static bool camOk = false;

static void applyCamera() {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        return;
    }
    s->set_framesize(s, (framesize_t)S.framesize);
    s->set_quality(s, S.quality);
    s->set_vflip(s, S.vflip);
    s->set_hmirror(s, S.hmirror);
    s->set_brightness(s, S.brightness);
    s->set_contrast(s, S.contrast);
#ifdef LED_PIN
    analogWrite(LED_PIN, S.led);
#endif
}

static bool startCamera() {
    camera_config_t c = {};
    c.ledc_channel    = LEDC_CHANNEL_0;
    c.ledc_timer      = LEDC_TIMER_0;
    c.pin_d0          = Y2;
    c.pin_d1          = Y3;
    c.pin_d2          = Y4;
    c.pin_d3          = Y5;
    c.pin_d4          = Y6;
    c.pin_d5          = Y7;
    c.pin_d6          = Y8;
    c.pin_d7          = Y9;
    c.pin_xclk        = XCLK;
    c.pin_pclk        = PCLK;
    c.pin_vsync       = VSYNC;
    c.pin_href        = HREF;
    c.pin_sccb_sda    = SIOD;
    c.pin_sccb_scl    = SIOC;
    c.pin_pwdn        = PWDN;
    c.pin_reset       = RESET;
    c.xclk_freq_hz    = 20000000;
    c.pixel_format    = PIXFORMAT_JPEG;
    c.frame_size      = FRAMESIZE_UXGA;  // buffers big enough for every size
    c.jpeg_quality    = S.quality;
    if (psramFound()) {
        c.fb_count    = 2;
        c.fb_location = CAMERA_FB_IN_PSRAM;
        c.grab_mode   = CAMERA_GRAB_LATEST;  // /capture gets the newest frame, not an old one
    } else {
        c.frame_size  = FRAMESIZE_VGA;
        c.fb_count    = 1;
        c.fb_location = CAMERA_FB_IN_DRAM;
        c.grab_mode   = CAMERA_GRAB_WHEN_EMPTY;
    }
    esp_err_t err = esp_camera_init(&c);
    if (err != ESP_OK) {
        say("[cam] camera start failed: 0x" + String(err, HEX) + " - check the board choice / ribbon cable");
        return false;
    }
    applyCamera();
    return true;
}

// ------------------------------------------------------------------ HTTP
static httpd_handle_t web = nullptr, streamer = nullptr;

static void common(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

static esp_err_t capture(httpd_req_t* req) {
    common(req);
    if (!camOk) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no camera");
        return ESP_FAIL;
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no frame");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    char ts[24];
    snprintf(ts, sizeof ts, "%lld", (long long)(esp_timer_get_time() / 1000));
    httpd_resp_set_hdr(req, "X-Timestamp-Ms", ts);
    esp_err_t r = httpd_resp_send(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return r;
}

#define BOUNDARY "dpcframe"
static esp_err_t stream(httpd_req_t* req) {
    common(req);
    if (!camOk) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no camera");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" BOUNDARY);
    char head[96];
    esp_err_t r = ESP_OK;
    while (r == ESP_OK) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            vTaskDelay(10);
            continue;
        }
        int n = snprintf(head, sizeof head, "\r\n--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", (unsigned)fb->len);
        r     = httpd_resp_send_chunk(req, head, n);
        if (r == ESP_OK) {
            r = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
        }
        esp_camera_fb_return(fb);
    }
    return r;
}

static String statusJson() {
    String j = "{";
    j += "\"board\":\"" + String(BOARD) + "\",\"version\":\"" + VERSION + "\"";
    j += ",\"camera\":" + String(camOk ? "true" : "false");
    j += ",\"framesize\":" + String(S.framesize) + ",\"quality\":" + String(S.quality);
    j += ",\"vflip\":" + String(S.vflip) + ",\"hmirror\":" + String(S.hmirror);
    j += ",\"brightness\":" + String(S.brightness) + ",\"contrast\":" + String(S.contrast);
#ifdef LED_PIN
    j += ",\"led\":" + String(S.led);
#endif
    j += ",\"ssid\":\"" + S.ssid + "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"host\":\"" + S.host + "\"";
    j += ",\"rssi\":" + String(WiFi.RSSI()) + ",\"setup_network\":" + String((WiFi.getMode() & WIFI_AP) ? "true" : "false");
    j += "}";
    return j;
}

static esp_err_t status(httpd_req_t* req) {
    common(req);
    httpd_resp_set_type(req, "application/json");
    String j = statusJson();
    return httpd_resp_send(req, j.c_str(), j.length());
}

static bool queryValue(httpd_req_t* req, const char* key, char* out, size_t len) {
    size_t qn = httpd_req_get_url_query_len(req);
    if (!qn || qn > 200) {
        return false;
    }
    char q[201];
    if (httpd_req_get_url_query_str(req, q, sizeof q) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(q, key, out, len) == ESP_OK;
}

static esp_err_t control(httpd_req_t* req) {
    common(req);
    char var[24], val[16];
    if (!queryValue(req, "var", var, sizeof var) || !queryValue(req, "val", val, sizeof val)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "var= and val= needed");
        return ESP_FAIL;
    }
    int  v  = atoi(val);
    bool ok = true;
    if (!strcmp(var, "framesize") && v >= 0 && v <= FRAMESIZE_UXGA) {
        S.framesize = v;
    } else if (!strcmp(var, "quality") && v >= 4 && v <= 63) {
        S.quality = v;
    } else if (!strcmp(var, "vflip")) {
        S.vflip = v;
    } else if (!strcmp(var, "hmirror")) {
        S.hmirror = v;
    } else if (!strcmp(var, "rotate180")) {
        S.vflip = S.hmirror = v;
    } else if (!strcmp(var, "brightness") && v >= -2 && v <= 2) {
        S.brightness = v;
    } else if (!strcmp(var, "contrast") && v >= -2 && v <= 2) {
        S.contrast = v;
#ifdef LED_PIN
    } else if (!strcmp(var, "led") && v >= 0 && v <= 255) {
        S.led = v;
#endif
    } else {
        ok = false;
    }
    if (!ok) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown setting or value");
        return ESP_FAIL;
    }
    applyCamera();
    save();
    httpd_resp_set_type(req, "application/json");
    String j = statusJson();
    return httpd_resp_send(req, j.c_str(), j.length());
}

static void restartSoon();

// Wi-Fi from the setup network only (not from the workshop network)
static esp_err_t wifiPost(httpd_req_t* req) {
    common(req);
    if (!(WiFi.getMode() & WIFI_AP)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Wi-Fi is set over USB, or from the camera's own setup network");
        return ESP_FAIL;
    }
    char body[256] = { 0 };
    int  n         = httpd_req_recv(req, body, sizeof body - 1);
    if (n <= 0) {
        return ESP_FAIL;
    }
    char ssid[64] = { 0 }, pass[72] = { 0 };
    httpd_query_key_value(body, "ssid", ssid, sizeof ssid);
    httpd_query_key_value(body, "pass", pass, sizeof pass);
    // form encoding: + is a space, %xx a byte
    auto decode = [](char* s) {
        char* o = s;
        for (char* p = s; *p; ++p) {
            if (*p == '+') {
                *o++ = ' ';
            } else if (*p == '%' && p[1] && p[2]) {
                char h[3] = { p[1], p[2], 0 };
                *o++      = (char)strtol(h, nullptr, 16);
                p += 2;
            } else {
                *o++ = *p;
            }
        }
        *o = 0;
    };
    decode(ssid);
    decode(pass);
    if (!*ssid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "network name missing");
        return ESP_FAIL;
    }
    S.ssid = ssid;
    S.pass = pass;
    save();
    httpd_resp_set_type(req, "text/html");
    const char* ok = "<meta name=viewport content='width=device-width'><body style='font-family:sans-serif'>"
                     "<p>Saved. The camera restarts and joins the network; find it at http://dpc-camera.local "
                     "or at the address your router gives it (also shown on the USB serial port).</p>";
    httpd_resp_send(req, ok, strlen(ok));
    restartSoon();
    return ESP_OK;
}

static const char PAGE[] = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>dpCREATOR camera</title>
<style>body{font-family:system-ui,sans-serif;background:#111;color:#ddd;margin:0;padding:12px}
img{max-width:100%;background:#000;display:block;margin:8px 0}label{display:inline-block;margin:4px 12px 4px 0}
select,input,button{font:inherit;padding:4px}code{color:#7cf}fieldset{border:1px solid #444;margin:10px 0}</style></head><body>
<h3>dpCREATOR head camera</h3><div id=info></div>
<img id=pic src="/capture"><div>
<label>Size <select id=framesize><option value=5>320x240</option><option value=8>640x480</option><option value=9>800x600</option><option value=10>1024x768</option><option value=11>1280x720</option><option value=13>1600x1200</option></select></label>
<label>Quality <input id=quality type=number min=4 max=63 style="width:4em"></label>
<label><input type=checkbox id=vflip> flip</label><label><input type=checkbox id=hmirror> mirror</label>
<label>Brightness <input id=brightness type=number min=-2 max=2 style="width:3em"></label>
<label>Contrast <input id=contrast type=number min=-2 max=2 style="width:3em"></label>
<label id=ledl hidden>Light <input id=led type=range min=0 max=255></label></div>
<p>WebUI camera address: <code id=cap></code> (snapshots) or <code id=str></code> (stream)</p>
<fieldset id=wf hidden><legend>Wi-Fi</legend><form method=post action=/wifi>
<label>Network <input name=ssid required></label><label>Password <input name=pass type=password></label>
<button>Save and restart</button></form></fieldset>
<script>
const $=id=>document.getElementById(id);let st={};
function show(s){st=s;for(const k of["framesize","quality","brightness","contrast","led"])if($(k)&&s[k]!==undefined)$(k).value=s[k];
$("vflip").checked=!!s.vflip;$("hmirror").checked=!!s.hmirror;$("ledl").hidden=s.led===undefined;$("wf").hidden=!s.setup_network;
$("info").textContent=s.board+" · v"+s.version+(s.ssid?" · "+s.ssid+" "+s.rssi+" dBm":"")+(s.camera?"":" · CAMERA NOT FOUND");
const h=location.hostname;$("cap").textContent="http://"+h+"/capture";$("str").textContent="http://"+h+":81/stream";}
fetch("/status").then(r=>r.json()).then(show);
for(const k of["framesize","quality","brightness","contrast","led","vflip","hmirror"])$(k).onchange=e=>{
const v=e.target.type=="checkbox"?(e.target.checked?1:0):e.target.value;fetch("/control?var="+k+"&val="+v).then(r=>r.json()).then(show);};
const pic=$("pic");pic.onload=()=>setTimeout(()=>pic.src="/capture?t="+Date.now(),150);pic.onerror=()=>setTimeout(()=>pic.src="/capture?t="+Date.now(),1500);
</script></body></html>)HTML";

static esp_err_t rootPage(httpd_req_t* req) {
    common(req);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGE, sizeof PAGE - 1);
}

static void startWeb() {
    httpd_config_t c   = HTTPD_DEFAULT_CONFIG();
    c.max_uri_handlers = 8;   // the stream is on its own server (port 81), so /capture never waits for it
    c.lru_purge_enable = true;  // a WebUI polling /capture leaves sockets; reuse the oldest
    if (httpd_start(&web, &c) == ESP_OK) {
        httpd_uri_t u[] = {
            { "/", HTTP_GET, rootPage, nullptr },
            { "/capture", HTTP_GET, capture, nullptr },
            { "/status", HTTP_GET, status, nullptr },
            { "/control", HTTP_GET, control, nullptr },
            { "/wifi", HTTP_POST, wifiPost, nullptr },
        };
        for (auto& h : u) {
            httpd_register_uri_handler(web, &h);
        }
    }
    c.server_port = 81;
    c.ctrl_port += 1;
    c.max_open_sockets = 3;
    if (httpd_start(&streamer, &c) == ESP_OK) {
        httpd_uri_t s = { "/stream", HTTP_GET, stream, nullptr };
        httpd_register_uri_handler(streamer, &s);
    }
}

// ------------------------------------------------------------------ Wi-Fi
static uint32_t restartAt = 0, apOffAt = 0;
static void     restartSoon() { restartAt = millis() + 1500; }

static String apName() {
    uint8_t m[6];
    WiFi.macAddress(m);
    char n[24];
    snprintf(n, sizeof n, "dpc-camera-%02X%02X", m[4], m[5]);
    return n;
}

static void startAp() {
    if (WiFi.getMode() & WIFI_AP) {
        return;
    }
    WiFi.mode(S.ssid.length() ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(apName().c_str(), AP_PASS);
    say("[cam] setup network " + apName() + " (password " + AP_PASS + "): open http://192.168.4.1");
}

static void startWifi() {
    WiFi.persistent(false);
    WiFi.setHostname(S.host.c_str());
    if (!S.ssid.length()) {
        say("[cam] no Wi-Fi set: type   wifi <network> <password>   here, or use the setup network");
        startAp();
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(S.ssid.c_str(), S.pass.c_str());
    WiFi.setSleep(false);  // steady frame rate
    say("[cam] joining " + S.ssid + " ...");
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
        delay(200);
    }
    if (WiFi.status() != WL_CONNECTED) {
        say("[cam] could not join " + S.ssid + " - still trying; meanwhile the setup network is open");
        startAp();
    }
}

static bool wasUp = false;
static void wifiLoop() {
    bool up = WiFi.status() == WL_CONNECTED;
    if (up && !wasUp) {
        say("[cam] on " + S.ssid + ": http://" + WiFi.localIP().toString() + "/capture   or   http://" + S.host + ".local/capture");
        MDNS.end();
        if (MDNS.begin(S.host.c_str())) {
            MDNS.addService("http", "tcp", 80);
        }
        if (WiFi.getMode() & WIFI_AP) {
            apOffAt = millis() + 60000;  // let a phone on the setup network see the result first
        }
    }
    wasUp = up;
    if (apOffAt && (int32_t)(millis() - apOffAt) >= 0) {
        apOffAt = 0;
        if (WiFi.status() == WL_CONNECTED) {
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            say("[cam] setup network closed");
        }
    }
}

// ------------------------------------------------------------------ serial commands
static void printStatus() {
    say(String("[cam] ") + BOARD + " v" + VERSION + ", camera " + (camOk ? "ok" : "NOT FOUND") + ", PSRAM " + (psramFound() ? "yes" : "no"));
    if (WiFi.status() == WL_CONNECTED) {
        say("[cam] " + S.ssid + " " + String(WiFi.RSSI()) + " dBm: http://" + WiFi.localIP().toString() + "/capture  http://" +
            WiFi.localIP().toString() + ":81/stream  (http://" + S.host + ".local)");
    } else {
        say("[cam] Wi-Fi " + (S.ssid.length() ? S.ssid + " not connected" : String("not set")));
    }
}

// next word; "quoted words" stay together
static String word(String& rest) {
    rest.trim();
    String w;
    if (rest.startsWith("\"")) {
        int e = rest.indexOf('"', 1);
        if (e < 0) {
            e = rest.length();
        }
        w    = rest.substring(1, e);
        rest = rest.substring(min((int)rest.length(), e + 1));
    } else {
        int e = rest.indexOf(' ');
        if (e < 0) {
            e = rest.length();
        }
        w    = rest.substring(0, e);
        rest = rest.substring(e);
    }
    return w;
}

static void command(String line) {
    line.trim();
    if (!line.length()) {
        return;
    }
    String rest = line, cmd = word(rest);
    cmd.toLowerCase();
    if (cmd == "wifi") {
        String ssid = word(rest);
        rest.trim();
        String pass = rest.startsWith("\"") ? word(rest) : rest;
        if (!ssid.length()) {
            return say("[cam] wifi <network> <password>");
        }
        S.ssid = ssid;
        S.pass = pass;
        save();
        say("[cam] Wi-Fi saved, restarting");
        restartSoon();
    } else if (cmd == "host") {
        String h = word(rest);
        if (!h.length()) {
            return say("[cam] host <name>   (now " + S.host + ")");
        }
        S.host = h;
        save();
        say("[cam] name saved: http://" + h + ".local after the restart");
        restartSoon();
    } else if (cmd == "forget") {
        S.ssid = S.pass = "";
        save();
        say("[cam] Wi-Fi forgotten, restarting");
        restartSoon();
    } else if (cmd == "restart") {
        restartSoon();
    } else if (cmd == "status") {
        printStatus();
    } else {
        say("[cam] commands: wifi <network> <password> | host <name> | status | forget | restart");
    }
}

static void readConsole(Stream& s, String& buf) {
    while (s.available()) {
        char c = s.read();
        if (c == '\n' || c == '\r') {
            if (buf.length()) {
                command(buf);
            }
            buf = "";
        } else if (buf.length() < 160) {
            buf += c;
        }
    }
}

// ------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT && defined(CONFIG_IDF_TARGET_ESP32S3)
    Serial0.begin(115200);
#endif
    delay(300);
    load();
#ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT);
    analogWrite(LED_PIN, 0);
#endif
    camOk = startCamera();
    startWifi();
    startWeb();
    printStatus();
}

void loop() {
    static String b1, b2;
    readConsole(Serial, b1);
#if ARDUINO_USB_CDC_ON_BOOT && defined(CONFIG_IDF_TARGET_ESP32S3)
    readConsole(Serial0, b2);
#endif
    wifiLoop();
    if (restartAt && (int32_t)(millis() - restartAt) >= 0) {
        ESP.restart();
    }
    delay(20);
}
