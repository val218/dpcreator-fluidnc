// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// User outputs from any screen, and the WebUI's custom panels for the pendant.
//
//   $Output/Set=<n>,<0|1> ($OUTS)   switch user output n now
//   $Output/Toggle=<n>    ($OUTT)   -> [OUT:<n>,<0|1>]
//   $Panel/List           ($PNL)    the buttons of /.dpc/panels/*.json, with the lines to send:
//       [PANEL:<id>|<title>]
//       [PANELBTN:<id>|<n>|<color>|<label>|<when>|<confirm>]
//       [PANELCMD:<id>|<n>|<line>]            (one per line, in order)
//       [PANEL:end|<count>]
//
// The output commands only switch a user output (relay, vacuum, light), never
// motion, so they run on the polling task and work while a job runs, from
// any channel - other commands from a second screen are refused during a job.
// The panel format is the WebUI's (docs/PANEL_FORMAT.md, dpcreator-panel/1).
#include "Channel.h"
#include "FileStream.h"
#include "FluidPath.h"
#include "JsonReader.h"
#include "Logging.h"
#include "Machine/MachineConfig.h"
#include "Machine/UserOutputs.h"
#include "Settings.h"
#include "Driver/watchdog.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
    using JsonReader::JVal;

    Machine::UserOutputs* outputs() { return config ? config->_userOutputs : nullptr; }

    bool output_number(const char* s, int& n) {
        char* end;
        long  v = strtol(s, &end, 10);
        if (end == s || v < 0 || v >= MaxUserDigitalPin) {
            return false;
        }
        n = int(v);
        return true;
    }

    Error output_set(const char* value, AuthenticationLevel auth_level, Channel& out) {
        auto uo = outputs();
        int  n;
        if (!value || !uo || !output_number(value, n)) {
            return Error::InvalidValue;
        }
        const char* comma = strchr(value, ',');
        if (!comma || (comma[1] != '0' && comma[1] != '1') || comma[2]) {
            return Error::InvalidValue;
        }
        bool on = comma[1] == '1';
        if (!uo->setDigital(n, on)) {
            log_error_to(out, "No user output " << n);
            return Error::InvalidValue;
        }
        log_stream(out, "[OUT:" << n << "," << (on ? 1 : 0));
        return Error::Ok;
    }

    Error output_toggle(const char* value, AuthenticationLevel auth_level, Channel& out) {
        auto uo = outputs();
        int  n;
        if (!value || !uo || !output_number(value, n)) {
            return Error::InvalidValue;
        }
        bool on = !((uo->digitalState() >> n) & 1);
        if (!uo->setDigital(n, on)) {
            log_error_to(out, "No user output " << n);
            return Error::InvalidValue;
        }
        log_stream(out, "[OUT:" << n << "," << (on ? 1 : 0));
        return Error::Ok;
    }

    // Text for a [...|...] line: no '|' or ']' inside, no line breaks
    std::string clean(const std::string& s) {
        std::string o;
        for (char c : s) {
            o += (c == '|' || c == ']' || c == '\n' || c == '\r') ? ' ' : c;
        }
        return o;
    }

    std::string number(double v) {
        char b[32];
        snprintf(b, sizeof(b), "%.4f", v);
        std::string s(b);  // trim trailing zeros
        while (s.size() > 1 && s.back() == '0') {
            s.pop_back();
        }
        if (s.back() == '.') {
            s.pop_back();
        }
        return s;
    }

    bool read_file(const std::string& path, std::string& text) {
        try {
            FileStream f(path.c_str(), "r", SD);
            char       buf[512];
            int        n;
            while ((n = f.read(buf, sizeof(buf))) > 0) {
                text.append(buf, n);
                if (text.size() > 32768) {
                    return false;
                }
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    void list_panel(Channel& out, const JVal& p, int& count) {
        std::string id = p.str("id");
        if (id.empty() || p.str("format") != "dpcreator-panel/1") {
            return;
        }
        id = clean(id);
        log_stream(out, "[PANEL:" << id << "|" << clean(p.str("title", id.c_str())));

        // Field defaults: what the WebUI sends before a macro (the pendant has no fields)
        std::vector<std::string> sets;
        if (auto fields = p.get("fields"); fields && fields->t == JVal::Arr) {
            for (auto& f : fields->a) {
                std::string fid = f.str("id");
                if (fid.empty()) {
                    continue;
                }
                const JVal* d = f.get("default");
                double      v = 0;
                if (d && d->t == JVal::Num) {
                    v = d->n;
                } else if (d && d->t == JVal::Bool) {
                    v = d->b ? 1 : 0;
                }
                sets.push_back("#<_panel_" + clean(fid) + ">=" + number(v));
            }
        }

        auto buttons = p.get("buttons");
        if (!buttons || buttons->t != JVal::Arr) {
            return;
        }
        int n = 0;
        for (auto& b : buttons->a) {
            feed_watchdog();
            std::vector<std::string> lines;
            std::string              when;
            if (auto w = b.get("when"); w && w->t == JVal::Arr) {
                for (auto& s : w->a) {
                    if (s.t == JVal::Str) {
                        when += (when.empty() ? "" : ",") + clean(s.s);
                    }
                }
            }
            if (b.get("output") && b.get("output")->t == JVal::Num) {
                lines.push_back("$Output/Toggle=" + std::to_string(int(b.num("output"))));
                if (when.empty()) {
                    when = "any";  // no motion: works in every state, a job included
                }
            } else if (!b.str("macro").empty()) {
                lines = sets;
                lines.push_back("$SD/Run=/.dpc/panels/" + id + "/" + clean(b.str("macro")) + ".nc");
            } else if (auto g = b.get("gcode"); g && g->t == JVal::Arr) {
                for (auto& l : g->a) {
                    if (l.t == JVal::Str && !l.s.empty()) {
                        lines.push_back(clean(l.s));
                    }
                }
            }
            if (lines.empty()) {
                ++n;
                continue;
            }
            log_stream(out,
                       "[PANELBTN:" << id << "|" << n << "|" << clean(b.str("color", "#94a3b8")) << "|" << clean(b.str("label", "?")) << "|"
                                    << (when.empty() ? "idle" : when) << "|" << clean(b.str("confirm")));
            for (auto& l : lines) {
                log_stream(out, "[PANELCMD:" << id << "|" << n << "|" << l);
            }
            ++n;
            ++count;
        }
    }

    Error panel_list(const char* value, AuthenticationLevel auth_level, Channel& out) {
        int             count = 0;
        std::error_code ec;
        FluidPath       dir { "/.dpc/panels", SD, ec };
        std::vector<std::string> files;
        if (!ec) {
            auto iter = stdfs::directory_iterator { dir, ec };
            if (!ec) {
                for (auto const& e : iter) {
                    feed_watchdog();
                    auto fn = e.path().filename().string();
                    if (!e.is_directory() && fn.size() > 5 && fn.compare(fn.size() - 5, 5, ".json") == 0) {
                        files.push_back(fn);
                    }
                }
            }
        }
        std::sort(files.begin(), files.end());
        for (auto& fn : files) {
            std::string text;
            if (!read_file("/.dpc/panels/" + fn, text)) {
                continue;
            }
            JVal root;
            if (!JsonReader::parse(text, root) || root.t != JVal::Obj) {
                log_stream(out, "[PANEL:error|" << clean(fn) << " is not valid JSON");
                continue;
            }
            list_panel(out, root, count);
        }
        log_stream(out, "[PANEL:end|" << count);
        return Error::Ok;
    }
}

void make_panel_commands() {
    new ReportCommand("OUTS", "Output/Set", output_set, anyState);
    new ReportCommand("OUTT", "Output/Toggle", output_toggle, anyState);
    new ReportCommand("PNL", "Panel/List", panel_list, anyState);
}
