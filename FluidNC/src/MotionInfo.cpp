// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// MotionInfo - dpCREATOR Motion's /sd/.dpc/motion.json for the pendant.
//
// Motion writes the file (machine and axis names, inputs, outputs, programs
// with their step texts; up to 16 KB) whenever it sends a program. The
// pendant has little RAM and a 12 KB answer buffer, so the board reads the
// file and hands it over as short lines:
//
//   $Motion/Show            [MSG:Motion:M|<v>|<machine>]
//                           [MSG:Motion:A|<letter>|<name>|<unit>|<kind>|<min>|<max>]   (min/max empty when not set)
//                           [MSG:Motion:I|<user input>|<name>]
//                           [MSG:Motion:O|<index>|<kind>|<name>]
//                           [MSG:Motion:P|<file>|<name>|<loops 0/1>|<home 0/1>|<steps>]
//                           [MSG:Motion:E]                            (end)
//                           [MSG:Motion:none]                         (no file, or a version it can't read)
//   $Motion/Steps=<file>    [MSG:Motion:S|<i>|<text>] ... [MSG:Motion:SE|<file>]
//
// Texts lose '|' and ']' and are cut to 60 characters.
#include "Settings.h"
#include "FileStream.h"
#include "Logging.h"
#include "Channel.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
    // ---- a small JSON reader: enough for motion.json
    struct JVal {
        enum Type : uint8_t { Null, Bool, Num, Str, Arr, Obj } t = Null;
        double                                    n = 0;
        bool                                      b = false;
        std::string                               s;
        std::vector<JVal>                         a;
        std::vector<std::pair<std::string, JVal>> o;

        const JVal* get(const char* k) const {
            for (auto& kv : o) {
                if (kv.first == k) {
                    return &kv.second;
                }
            }
            return nullptr;
        }
        std::string str(const char* k, const char* d = "") const {
            auto v = get(k);
            return v && v->t == Str ? v->s : std::string(d);
        }
        bool has_num(const char* k) const {
            auto v = get(k);
            return v && v->t == Num;
        }
        double num(const char* k, double d = 0) const {
            auto v = get(k);
            return v && v->t == Num ? v->n : d;
        }
        bool truthy(const char* k) const {
            auto v = get(k);
            return v && ((v->t == Bool && v->b) || (v->t == Num && v->n != 0));
        }
    };

    struct Reader {
        const char* p;
        const char* e;
        int         depth = 0;
        bool        ok    = true;

        void ws() {
            while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
                ++p;
            }
        }
        bool lit(const char* w) {
            size_t n = strlen(w);
            if (size_t(e - p) >= n && !strncmp(p, w, n)) {
                p += n;
                return true;
            }
            return false;
        }
        std::string string() {
            std::string out;
            ++p;  // "
            while (p < e && *p != '"') {
                char c = *p++;
                if (c == '\\' && p < e) {
                    char x = *p++;
                    switch (x) {
                        case 'n':
                        case 'r':
                        case 't':
                            c = ' ';
                            break;
                        case 'u': {
                            unsigned v = 0;
                            for (int i = 0; i < 4 && p < e; ++i, ++p) {
                                v = v * 16 + (isdigit((unsigned char)*p) ? *p - '0' : (tolower(*p) - 'a' + 10));
                            }
                            // UTF-8 for the BMP (the pendant's font shows Latin-1 at best)
                            if (v < 0x80) {
                                out += char(v);
                            } else if (v < 0x800) {
                                out += char(0xC0 | (v >> 6));
                                out += char(0x80 | (v & 0x3F));
                            } else {
                                out += char(0xE0 | (v >> 12));
                                out += char(0x80 | ((v >> 6) & 0x3F));
                                out += char(0x80 | (v & 0x3F));
                            }
                            continue;
                        }
                        default:
                            c = x;
                    }
                }
                out += c;
            }
            if (p < e) {
                ++p;
            } else {
                ok = false;
            }
            return out;
        }
        JVal value() {
            JVal v;
            ws();
            if (p >= e || ++depth > 20) {
                ok = false;
                return v;
            }
            if (*p == '{') {
                v.t = JVal::Obj;
                ++p;
                ws();
                if (p < e && *p == '}') {
                    ++p;
                } else {
                    while (ok && p < e) {
                        ws();
                        if (p >= e || *p != '"') {
                            ok = false;
                            break;
                        }
                        std::string k = string();
                        ws();
                        if (p >= e || *p != ':') {
                            ok = false;
                            break;
                        }
                        ++p;
                        v.o.emplace_back(k, value());
                        ws();
                        if (p < e && *p == ',') {
                            ++p;
                            continue;
                        }
                        if (p < e && *p == '}') {
                            ++p;
                            break;
                        }
                        ok = false;
                    }
                }
            } else if (*p == '[') {
                v.t = JVal::Arr;
                ++p;
                ws();
                if (p < e && *p == ']') {
                    ++p;
                } else {
                    while (ok && p < e) {
                        v.a.push_back(value());
                        ws();
                        if (p < e && *p == ',') {
                            ++p;
                            continue;
                        }
                        if (p < e && *p == ']') {
                            ++p;
                            break;
                        }
                        ok = false;
                    }
                }
            } else if (*p == '"') {
                v.t = JVal::Str;
                v.s = string();
            } else if (lit("true")) {
                v.t = JVal::Bool;
                v.b = true;
            } else if (lit("false")) {
                v.t = JVal::Bool;
            } else if (lit("null")) {
                v.t = JVal::Null;
            } else {
                char* end = nullptr;
                v.n       = strtod(p, &end);
                if (end == p) {
                    ok = false;
                } else {
                    v.t = JVal::Num;
                    p   = end;
                }
            }
            --depth;
            return v;
        }
    };

    std::string clean(const std::string& s, size_t max = 60) {
        std::string o;
        for (char c : s) {
            if (o.size() >= max) {
                break;
            }
            o += (c == '|' || c == ']' || c == '\n' || c == '\r') ? ' ' : c;
        }
        return o;
    }
    std::string fmt(double v) {
        char b[24];
        snprintf(b, sizeof(b), "%g", v);
        return b;
    }

    const char* MOTION_FILE = "/sd/.dpc/motion.json";

    bool load(JVal& root) {
        std::string text;
        try {
            FileStream f(MOTION_FILE, "r", SD);
            char       buf[512];
            int        n;
            while ((n = f.read(buf, sizeof(buf))) > 0 && text.size() < 32768) {
                text.append(buf, n);
            }
        } catch (...) {
            return false;
        }
        Reader r { text.data(), text.data() + text.size() };
        root = r.value();
        return r.ok && root.t == JVal::Obj && root.num("v", 0) == 1;
    }

    Error motion_show(const char* value, AuthenticationLevel auth_level, Channel& out) {
        JVal root;
        if (!load(root)) {
            log_stream(out, "[MSG:Motion:none");
            return Error::Ok;
        }
        log_stream(out, "[MSG:Motion:M|1|" << clean(root.str("machine")));
        if (auto axes = root.get("axes"); axes && axes->t == JVal::Arr) {
            for (auto& a : axes->a) {
                std::string letter = clean(a.str("letter"), 1);
                if (letter.empty()) {
                    continue;
                }
                log_stream(out,
                           "[MSG:Motion:A|" << letter << "|" << clean(a.str("name"), 20) << "|" << clean(a.str("unit", "mm"), 6) << "|"
                                            << clean(a.str("kind", "linear"), 10) << "|" << (a.has_num("min") ? fmt(a.num("min")) : "") << "|"
                                            << (a.has_num("max") ? fmt(a.num("max")) : ""));
            }
        }
        if (auto ins = root.get("inputs"); ins && ins->t == JVal::Arr) {
            for (auto& i : ins->a) {
                log_stream(out, "[MSG:Motion:I|" << int(i.num("user", -1)) << "|" << clean(i.str("name"), 24));
            }
        }
        if (auto outs = root.get("outputs"); outs && outs->t == JVal::Arr) {
            for (auto& o : outs->a) {
                log_stream(out,
                           "[MSG:Motion:O|" << int(o.num("index", -1)) << "|" << clean(o.str("kind", "digital"), 10) << "|"
                                            << clean(o.str("name"), 24));
            }
        }
        if (auto progs = root.get("programs"); progs && progs->t == JVal::Arr) {
            for (auto& p : progs->a) {
                auto   steps = p.get("steps");
                size_t ns    = steps && steps->t == JVal::Arr ? steps->a.size() : 0;
                bool   home  = p.truthy("home") || p.truthy("homeFirst") || p.truthy("home_first");
                log_stream(out,
                           "[MSG:Motion:P|" << clean(p.str("file"), 60) << "|" << clean(p.str("name"), 32) << "|" << (p.truthy("loops") ? 1 : 0)
                                            << "|" << (home ? 1 : 0) << "|" << ns);
            }
        }
        log_stream(out, "[MSG:Motion:E");
        return Error::Ok;
    }

    Error motion_steps(const char* value, AuthenticationLevel auth_level, Channel& out) {
        if (!value || !*value) {
            return Error::InvalidValue;
        }
        std::string want = value;
        auto        base = [](const std::string& f) {
            auto s = f.rfind('/');
            return s == std::string::npos ? f : f.substr(s + 1);
        };
        JVal root;
        if (load(root)) {
            if (auto progs = root.get("programs"); progs && progs->t == JVal::Arr) {
                for (auto& p : progs->a) {
                    if (base(p.str("file")) != base(want)) {
                        continue;
                    }
                    if (auto steps = p.get("steps"); steps && steps->t == JVal::Arr) {
                        int i = 0;
                        for (auto& s : steps->a) {
                            log_stream(out, "[MSG:Motion:S|" << i++ << "|" << clean(s.t == JVal::Str ? s.s : std::string()));
                        }
                    }
                    break;
                }
            }
        }
        log_stream(out, "[MSG:Motion:SE|" << clean(base(want)));
        return Error::Ok;
    }
}

void make_motion_commands() {
    new ReportCommand("MOS", "Motion/Show", motion_show, anyState);  // also while a job runs
    new ReportCommand("MOT", "Motion/Steps", motion_steps, anyState);
}
