// MacroLib.cpp - see MacroLib.h
#include "MacroLib.h"

#include "FileStream.h"
#include "FluidPath.h"
#include "Serial.h"
#include "Channel.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <strings.h>
#include <vector>

namespace {
    const char* DIR   = "/.dpc";
    const char* INDEX = "/.dpc/macros.txt";
    const char* MDIR  = "/.dpc/macros";

    struct Starter {
        const char* name;
        const char* color;
        const char* flags;
        const char* body;
    };
    // The pendant's old built-in buttons, as the first macros
    const Starter STARTERS[] = {
        { "Park", "#22d3ee", "p", "(Park: Z up to the top in machine coordinates)\nG53 G0 Z0\n" },
        { "Zero X", "#ef4444", "p", "G10 L20 P0 X0\n" },
        { "Zero Y", "#22c55e", "p", "G10 L20 P0 Y0\n" },
        { "Zero Z", "#3b82f6", "pc", "G10 L20 P0 Z0\n" },
        { "Zero all", "#f97316", "pc", "G10 L20 P0 X0 Y0 Z0\n" },
        { "Coolant on", "#3b82f6", "p", "M8\n" },
        { "Coolant off", "#3b82f6", "p", "M9\n" },
        { "Spindle CW", "#22c55e", "pc", "(Spindle on, clockwise)\nM3 S10000\n" },
        { "Spindle off", "#94a3b8", "p", "M5\n" },
        { "Warm-up", "#eab308", "pc",
          "(Spindle warm-up: 2 minutes per step, slow to fast)\n"
          "#<rpm> = 6000\n"
          "o100 repeat [4]\n"
          "  M3 S#<rpm>\n"
          "  (PRINT, warm-up at %.0f rpm, #<rpm>)\n"
          "  G4 P120\n"
          "  #<rpm> = [#<rpm> + 4000]\n"
          "o100 endrepeat\n"
          "M5\n" },
    };

    bool read_all(const char* path, std::string& out) {
        out.clear();
        try {
            FileStream f(path, "r", SD);
            char       buf[256];
            int        n;
            while ((n = f.read(buf, sizeof(buf))) > 0) out.append(buf, n);
            return true;
        } catch (...) {
            return false;
        }
    }
    bool write_all(const char* path, const std::string& text) {
        try {
            FileStream f(path, "w", SD);
            f.write((const uint8_t*)text.data(), text.size());
            return true;
        } catch (...) {
            return false;
        }
    }
    void make_dir(const char* d) {
        std::error_code ec;
        FluidPath       p { d, SD, ec };
        if (!ec) stdfs::create_directory(p, ec);
    }

    // The starter set, when there is no list yet
    bool create_starters() {
        make_dir(DIR);
        make_dir(MDIR);
        std::string index;
        int         id = 1;
        for (const auto& s : STARTERS) {
            char path[48];
            snprintf(path, sizeof(path), "%s/%d.nc", MDIR, id);
            if (!write_all(path, s.body)) return false;
            index += std::to_string(id) + "|" + s.name + "|" + s.color + "|" + s.flags + "\n";
            ++id;
        }
        return write_all(INDEX, index);
    }
}

const char* macrolib_dir() { return DIR; }

bool macrolib_is_macro_path(const std::string& path) {
    return path.find("/.dpc/") != std::string::npos;
}

bool macrolib_command(const char* line, Channel& channel, std::string& runLine) {
    runLine.clear();
    if (strncasecmp(line, "$MacroLib/", 10) != 0) return false;
    const char* c = line + 10;

    if (strcasecmp(c, "List") == 0) {
        std::string index;
        if (!read_all(INDEX, index)) {
            if (!create_starters() || !read_all(INDEX, index)) {
                channel.print("[MSG:MacroErr:no SD card - macros are kept on it]\n");
                channel.print("[MSG:MacroEnd:0]\n");
                return true;
            }
        }
        make_dir(MDIR);
        int    count = 0;
        size_t pos   = 0;
        while (pos < index.size()) {
            size_t eol = index.find('\n', pos);
            if (eol == std::string::npos) eol = index.size();
            std::string l = index.substr(pos, eol - pos);
            pos           = eol + 1;
            if (!l.empty() && l.back() == '\r') l.pop_back();
            // id|name|color|flags
            size_t a = l.find('|'), b = a == std::string::npos ? a : l.find('|', a + 1),
                   d = b == std::string::npos ? b : l.find('|', b + 1);
            if (a == std::string::npos || b == std::string::npos) continue;
            int id = atoi(l.substr(0, a).c_str());
            if (id <= 0) continue;
            std::string name  = l.substr(a + 1, b - a - 1);
            std::string color = d == std::string::npos ? l.substr(b + 1) : l.substr(b + 1, d - b - 1);
            std::string flags = d == std::string::npos ? "" : l.substr(d + 1);
            for (auto& ch : name) {
                if (ch == ']' || ch == '\n') ch = ' ';
            }
            std::string m = "[MSG:MacroItem:" + std::to_string(id) + ":" + color + ":" + flags + ":" + name + "]\n";
            channel.print(m.c_str());
            ++count;
        }
        std::string end = "[MSG:MacroEnd:" + std::to_string(count) + "]\n";
        channel.print(end.c_str());
        return true;
    }
    if (strcasecmp(c, "Reload") == 0) {
        allChannels.print("[MSG:MacroLib:changed]\n");
        return true;
    }
    if (strncasecmp(c, "Run=", 4) == 0) {
        int id = atoi(c + 4);
        if (id <= 0) {
            channel.print("[MSG:MacroErr:bad macro number]\n");
            return true;
        }
        // Run it like a file: $SD/Run=/.dpc/macros/<id>.nc (the caller executes runLine)
        runLine = std::string("$SD/Run=") + MDIR + "/" + std::to_string(id) + ".nc";
        return true;
    }
    channel.print("[MSG:MacroErr:use $MacroLib/List, /Run=<id>, /Reload]\n");
    return true;
}
