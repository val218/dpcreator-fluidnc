// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// A small JSON reader for the board's own files (motion.json, automation.json):
// objects, arrays, strings (\uXXXX to UTF-8), numbers, true/false/null.
// Header-only and free of FluidNC dependencies, so the unit tests can use it.
#pragma once

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace JsonReader {
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


    // Parses text; false if it is not valid JSON.
    inline bool parse(const std::string& text, JVal& out) {
        Reader r { text.data(), text.data() + text.size() };
        out = r.value();
        r.ws();
        return r.ok && r.p == r.e;
    }
}
