// Teach.cpp - see Teach.h
#include "Teach.h"
#include "TeachGen.h"
#include "Serial.h"       // allChannels
#include "System.h"       // get_mpos
#include "Report.h"       // mpos_to_wpos
#include "State.h"        // state_is
#include "Job.h"          // Job::active
#include "PendantLink.h"  // pendant_channel
#include "UartChannel.h"
#include "FileStream.h"
#include "FluidPath.h"
#include "Config.h"  // MAX_N_AXIS

#include <Arduino.h>  // millis
#include <mutex>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <strings.h>
#include <cctype>
#include <utility>

namespace {
    const char* LIST_FILE = "/.dpc/teach.txt";
    const char* JOB_FILE  = "/retrace.nc";
    const size_t MAX_PTS  = 200;
    const uint32_t WAIT_MS  = 8000;    // a pending Add / Measure waits this long for Idle
    const uint32_t CLAIM_MS = 150000;  // a WebUI claim on pendant points lasts this long unless renewed

    std::recursive_mutex mtx;
    std::vector<TeachPt> pts;
    TeachCfg             cfg;
    bool                 loaded = false, dirty = false;

    struct Pending {
        char     kind = 0;  // 'a' add, 'm' measure
        int      n    = 0;  // measure: 1-based waypoint
        char     op   = 0;  // add: op, 0 = like the previous waypoint
        bool     fromPendant = false;
        uint32_t deadline    = 0;
    } pend;

    std::string claimName;
    uint32_t    claimUntil = 0;

    // ---- text ----
    std::string pt_line(size_t i) {
        const TeachPt& p = pts[i];
        char b[240];
        snprintf(b, sizeof(b), "[MSG:Teach:pt:%u:%s:%s:%s:%c:%s:%s:%s:%s:%d:%s:%s:%s]\n", (unsigned)(i + 1), teach_f3(p.x).c_str(),
                 teach_f3(p.y).c_str(), teach_f3(p.z).c_str(), p.op, p.ref.c_str(), teach_num(p.depth).c_str(),
                 teach_num(p.w).c_str(), teach_num(p.h).c_str(), p.passes, teach_num(p.feed).c_str(), teach_num(p.plunge).c_str(),
                 teach_num(p.ramp).c_str());
        return b;
    }
    std::string cfg_line() {
        return "[MSG:Teach:cfg:" + teach_num(cfg.safeZ) + ":" + teach_num(cfg.feed) + ":" + teach_num(cfg.plunge) + ":" +
               teach_num(cfg.rpm) + ":" + teach_num(cfg.tool) + "]\n";
    }
    std::string count_line() { return "[MSG:Teach:count:" + std::to_string(pts.size()) + "]\n"; }
    void        all(const std::string& s) { allChannels.print(s.c_str()); }
    void        err(Channel& ch, const std::string& s) { ch.print(("[MSG:Teach:err:" + s + "]\n").c_str()); }
    void        err_all(const std::string& s) { all("[MSG:Teach:err:" + s + "]\n"); }
    void        list_to(Channel& ch) {
        ch.print(cfg_line().c_str());
        for (size_t i = 0; i < pts.size(); i++) ch.print(pt_line(i).c_str());
        ch.print(count_line().c_str());
    }
    void list_all() {
        all(cfg_line());
        for (size_t i = 0; i < pts.size(); i++) all(pt_line(i));
        all(count_line());
    }

    bool valid_op(char c) { return c == 'm' || c == 'd' || c == 'r' || c == 'p'; }
    bool valid_ref(const std::string& r) { return r == "c" || r == "bl" || r == "br" || r == "tl" || r == "tr"; }

    // "key=value,key=value" -> calls f(key, value); false on a bad key
    template <class F>
    bool each_kv(const char* s, F f) {
        std::string text(s);
        size_t      pos = 0;
        while (pos <= text.size()) {
            size_t      end = text.find(',', pos);
            std::string kv  = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            pos             = end == std::string::npos ? text.size() + 1 : end + 1;
            if (kv.empty()) continue;
            size_t eq = kv.find('=');
            if (eq == std::string::npos) return false;
            if (!f(kv.substr(0, eq), kv.substr(eq + 1))) return false;
        }
        return true;
    }
    bool num(const std::string& s, double& out) {
        char* e;
        out = strtod(s.c_str(), &e);
        return !s.empty() && *e == '\0' && std::isfinite(out);
    }

    // ---- SD card ----
    void load() {
        if (loaded) return;
        loaded = true;
        std::string text;
        try {
            FileStream f(LIST_FILE, "r", SD);
            char       b[256];
            int        n;
            while ((n = f.read(b, sizeof(b))) > 0) text.append(b, n);
        } catch (...) {
            return;  // no list yet / no card
        }
        size_t pos = 0;
        while (pos < text.size()) {
            size_t      e    = text.find('\n', pos);
            std::string line = text.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
            pos              = e == std::string::npos ? text.size() : e + 1;
            char   ref[8] = "c", op = 'd';
            double a, b2, c, d, w, h, x, fd = 0, pl = 0, rp = 0;
            int    passes, nf = 0;
            if (sscanf(line.c_str(), "cfg %lf %lf %lf %lf %lf", &a, &b2, &c, &d, &w) == 5) {
                cfg.safeZ = a; cfg.feed = b2; cfg.plunge = c; cfg.rpm = d; cfg.tool = w;
            } else if (pts.size() < MAX_PTS &&
                       (nf = sscanf(line.c_str(), "pt %lf %lf %lf %c %7s %lf %lf %lf %d %lf %lf %lf", &x, &a, &b2, &op, ref, &d, &w, &h, &passes, &fd, &pl, &rp)) >= 9) {
                TeachPt p;
                p.x = x; p.y = a; p.z = b2;
                p.op     = valid_op(op) ? op : 'd';
                p.ref    = valid_ref(ref) ? ref : "c";
                p.depth  = d; p.w = w; p.h = h;
                p.passes = passes < 1 ? 1 : passes;
                if (nf >= 11) { p.feed = std::fabs(fd); p.plunge = std::fabs(pl); }  // older lists have no feeds
                if (nf >= 12) p.ramp = std::fabs(rp);
                pts.push_back(p);
            }
        }
    }
    bool save_list() {
        std::string t = "cfg " + teach_num(cfg.safeZ) + " " + teach_num(cfg.feed) + " " + teach_num(cfg.plunge) + " " +
                        teach_num(cfg.rpm) + " " + teach_num(cfg.tool) + "\n";
        for (auto& p : pts) {
            t += "pt " + teach_f3(p.x) + " " + teach_f3(p.y) + " " + teach_f3(p.z) + " " + p.op + " " + p.ref + " " +
                 teach_num(p.depth) + " " + teach_num(p.w) + " " + teach_num(p.h) + " " + std::to_string(p.passes) + " " +
                 teach_num(p.feed) + " " + teach_num(p.plunge) + " " + teach_num(p.ramp) + "\n";
        }
        try {
            std::error_code ec;
            FluidPath       dir { "/.dpc", SD, ec };
            if (!ec) stdfs::create_directory(dir, ec);
            FileStream f(LIST_FILE, "w", SD);
            f.write((const uint8_t*)t.data(), t.size());
        } catch (...) {
            return false;
        }
        dirty = false;
        return true;
    }

    bool work_xyz(double& x, double& y, double& z) {
        float p[MAX_N_AXIS];
        memcpy(p, get_mpos(), sizeof(p));
        mpos_to_wpos(p);
        x = p[0]; y = p[1]; z = p[2];
        return true;
    }
    bool from_pendant(Channel& ch) {
        UartChannel* pc = pendant_channel();
        return pc && &ch == static_cast<Channel*>(pc);
    }

    // ---- the actions behind a pending Add / Measure (machine Idle) ----
    void do_add(char op, bool fromPendant) {
        if (pts.size() >= MAX_PTS) return err_all("list is full (" + std::to_string(MAX_PTS) + " waypoints)");
        TeachPt p;
        if (!pts.empty()) p = pts.back();  // like the previous one: repeated holes need no setup
        work_xyz(p.x, p.y, p.z);
        if (!pts.empty() && std::hypot(pts.back().x - p.x, pts.back().y - p.y) < 0.05) {
            return err_all("same XY as waypoint " + std::to_string(pts.size()) + ": jog to the next mark first");
        }
        if (op) p.op = op;
        pts.push_back(p);
        dirty = true;
        all("[MSG:Teach:added:" + std::to_string(pts.size()) + ":" + (fromPendant ? "pendant" : "ui") + "]\n");
        all(pt_line(pts.size() - 1));
    }
    void do_measure(int n) {
        if (n < 1 || n > (int)pts.size()) return err_all("no waypoint " + std::to_string(n));
        TeachPt& p = pts[n - 1];
        double   x, y, z;
        work_xyz(x, y, z);
        double dx = x - p.x, dy = y - p.y;
        if (std::hypot(dx, dy) < 0.1) return err_all("jog from the mark to the edge or corner first");
        if (p.op == 'r') {
            p.w   = std::round(2 * std::hypot(dx, dy) * 1000) / 1000;
            p.ref = "c";
        } else if (p.op == 'p') {
            if (p.ref == "c") {
                p.w = std::round(2 * std::fabs(dx) * 1000) / 1000;
                p.h = std::round(2 * std::fabs(dy) * 1000) / 1000;
            } else {  // mark on a corner, measured to the opposite corner: the direction says which corner
                p.w   = std::round(std::fabs(dx) * 1000) / 1000;
                p.h   = std::round(std::fabs(dy) * 1000) / 1000;
                p.ref = dx >= 0 ? (dy >= 0 ? "bl" : "tl") : (dy >= 0 ? "br" : "tr");
            }
        } else {
            return err_all("waypoint " + std::to_string(n) + " is not a pocket");
        }
        dirty = true;
        all("[MSG:Teach:measured:" + std::to_string(n) + ":" + teach_num(p.w) + ":" + teach_num(p.h) + "]\n");
        all(pt_line(n - 1));
    }
    void queue(char kind, int n, char op, bool fromPendant) {
        pend.kind        = kind;
        pend.n           = n;
        pend.op          = op;
        pend.fromPendant = fromPendant;
        pend.deadline    = millis() + WAIT_MS;
    }
}

// $Measure/Save=x0,y0,x1,y1[,top[,zero[,by]]]: the part measured by the WebUI's
// Centre Finder or the pendant's Measure (work coordinates), kept in
// /sd/.dpc/measure.json for dpCREATOR Studio, with the work offset (WCO, machine
// coordinates of work zero) so Studio can tell when work zero moved since.
// top: work Z of the part's top, or "-" (not measured); zero: where XY zero was
// set (centre, bl, br, tl, tr, none); by: webui or pendant.
static void measure_save(const char* v, Channel& ch) {
    double x0, y0, x1, y1;
    char   top[24] = "-", zero[12] = "none", by[12] = "";
    int    n = sscanf(v, "%lf,%lf,%lf,%lf,%23[^,],%11[^,],%11s", &x0, &y0, &x1, &y1, top, zero, by);
    if (n < 4) {
        ch.print("[MSG:Measure:err:give x0,y0,x1,y1]\n");
        return;
    }
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    float m[MAX_N_AXIS], w[MAX_N_AXIS];
    memcpy(m, get_mpos(), sizeof(m));
    memcpy(w, m, sizeof(w));
    mpos_to_wpos(w);
    char* end   = nullptr;
    double tz   = strtod(top, &end);
    bool  has_t = end && end != top;
    for (char* c = zero; *c; ++c) *c = isalnum((unsigned char)*c) ? tolower(*c) : '_';
    for (char* c = by; *c; ++c) *c = isalnum((unsigned char)*c) ? tolower(*c) : '_';
    char j[420];
    snprintf(j, sizeof(j),
             "{\"x0\":%.3f,\"y0\":%.3f,\"x1\":%.3f,\"y1\":%.3f,\"w\":%.3f,\"h\":%.3f,\"cx\":%.3f,\"cy\":%.3f,"
             "\"top\":%s,\"zero\":\"%s\",\"wco\":[%.3f,%.3f,%.3f],\"by\":\"%s\",\"up_ms\":%lu}\n",
             x0, y0, x1, y1, x1 - x0, y1 - y0, (x0 + x1) / 2, (y0 + y1) / 2, has_t ? teach_f3(tz).c_str() : "null", zero,
             m[0] - w[0], m[1] - w[1], m[2] - w[2], by, (unsigned long)millis());
    try {
        std::error_code ec;
        FluidPath       dir { "/.dpc", SD, ec };
        if (!ec) stdfs::create_directory(dir, ec);
        FileStream f("/.dpc/measure.json", "w", SD);
        f.write((const uint8_t*)j, strlen(j));
    } catch (...) {
        ch.print("[MSG:Measure:err:cannot write the SD card]\n");
        return;
    }
    char msg[120];
    snprintf(msg, sizeof(msg), "[MSG:Measure:saved:%.3f:%.3f]\n", x1 - x0, y1 - y0);
    allChannels.print(msg);
}

bool teach_command(const char* line, Channel& ch, bool on_protocol_task, bool& deferred) {
    deferred = false;
    const bool isTeach = strncasecmp(line, "$Teach", 6) == 0 && (line[6] == '\0' || line[6] == '/');
    const bool isPoint = strcasecmp(line, "$Pendant/Point") == 0;
    const bool isClaim = strncasecmp(line, "$Pendant/Claim", 14) == 0 && (line[14] == '\0' || line[14] == '=');
    const bool isMeas  = strncasecmp(line, "$Measure/Save=", 14) == 0;
    if (!isTeach && !isPoint && !isClaim && !isMeas) return false;
    if (isMeas) {  // writes the SD card: on the protocol task
        if (!on_protocol_task) {
            deferred = true;
            return true;
        }
        return measure_save(line + 14, ch), true;
    }

    std::lock_guard<std::recursive_mutex> lock(mtx);

    if (isClaim) {  // a WebUI tool (Offcuts, Centre Finder) wants the double-taps
        const char* v = line[14] == '=' ? line + 15 : "";
        claimName     = v;
        claimUntil    = millis() + CLAIM_MS;
        all(("[MSG:Teach:claim:" + claimName + "]\n").c_str());  // the pendant too: it then sends double-taps straight on
        return true;
    }
    if (isPoint) {
        if (!claimName.empty() && (int32_t)(claimUntil - millis()) > 0) {
            all("[MSG:PendantPoint]\n");
            return true;
        }
        if (Job::active()) return err(ch, "not while a job runs"), true;
        queue('a', 0, 0, true);
        return true;
    }

    const char* cmd = line[6] == '/' ? line + 7 : "List";
    const char* eq  = strchr(cmd, '=');
    std::string name(cmd, eq ? eq - cmd : strlen(cmd));
    const char* val = eq ? eq + 1 : "";

    if (strcasecmp(name.c_str(), "List") == 0) {
        load();
        list_to(ch);
        return true;
    }
    if (Job::active()) return err(ch, "not while a job runs"), true;

    if (strcasecmp(name.c_str(), "Add") == 0) {
        char op = 0;
        if (*val) {
            if (!valid_op(val[0]) || val[1]) return err(ch, "op is m, d, r or p"), true;
            op = val[0];
        }
        queue('a', 0, op, from_pendant(ch));
        return true;
    }
    if (strcasecmp(name.c_str(), "Measure") == 0) {
        queue('m', atoi(val), 0, from_pendant(ch));
        return true;
    }

    // The rest writes the SD card: on the protocol task.
    if (!on_protocol_task) {
        deferred = true;
        return true;
    }
    load();

    if (strcasecmp(name.c_str(), "Set") == 0) {
        int n = atoi(val);
        if (n < 1 || n > (int)pts.size()) return err(ch, "no waypoint " + std::to_string(n)), true;
        const char* kvs = strchr(val, ',');
        TeachPt     p   = pts[n - 1];
        bool        ok  = each_kv(kvs ? kvs + 1 : "", [&](const std::string& k, const std::string& v) {
            double d;
            if (k == "op") return v.size() == 1 && valid_op(v[0]) ? (p.op = v[0], true) : false;
            if (k == "ref") return valid_ref(v) ? (p.ref = v, true) : false;
            if (!num(v, d)) return false;
            if (k == "depth") p.depth = std::fabs(d);
            else if (k == "w") p.w = std::fabs(d);
            else if (k == "h") p.h = std::fabs(d);
            else if (k == "passes") p.passes = d < 1 ? 1 : (int)d;
            else if (k == "feed") p.feed = std::fabs(d);       // 0 = the job's feed
            else if (k == "plunge") p.plunge = std::fabs(d);
            else if (k == "ramp") p.ramp = std::min(45.0, std::fabs(d));  // degrees, 0 = straight plunge
            else if (k == "x") p.x = d;
            else if (k == "y") p.y = d;
            else if (k == "z") p.z = d;
            else return false;
            return true;
        });
        if (!ok) return err(ch, "Set=n,op=m|d|r|p,ref=c|bl|br|tl|tr,depth=,w=,h=,passes=,feed=,plunge=,ramp=,x=,y=,z="), true;
        pts[n - 1] = p;
        save_list();
        all(pt_line(n - 1));
        return true;
    }
    if (strcasecmp(name.c_str(), "Del") == 0) {
        int n = atoi(val);
        if (n < 1 || n > (int)pts.size()) return err(ch, "no waypoint " + std::to_string(n)), true;
        pts.erase(pts.begin() + (n - 1));
        save_list();
        list_all();
        return true;
    }
    if (strcasecmp(name.c_str(), "Clear") == 0) {
        pts.clear();
        save_list();
        list_all();
        return true;
    }
    if (strcasecmp(name.c_str(), "Cfg") == 0) {
        TeachCfg c  = cfg;
        bool     ok = each_kv(val, [&](const std::string& k, const std::string& v) {
            double d;
            if (!num(v, d)) return false;
            if (strcasecmp(k.c_str(), "safez") == 0) c.safeZ = d;
            else if (k == "feed") c.feed = d;
            else if (k == "plunge") c.plunge = d;
            else if (k == "rpm") c.rpm = d;
            else if (k == "tool") c.tool = d;
            else return false;
            return true;
        });
        if (!ok) return err(ch, "Cfg=safez=,feed=,plunge=,rpm=,tool="), true;
        cfg = c;
        save_list();
        all(cfg_line());
        return true;
    }
    if (strcasecmp(name.c_str(), "Save") == 0) {
        if (pts.empty()) return err(ch, "no waypoints"), true;
        std::vector<std::string> problems;
        std::string              g = teach_gcode(cfg, pts, problems);
        if (!problems.empty()) return err(ch, problems[0]), true;
        try {
            FileStream f(JOB_FILE, "w", SD);
            f.write((const uint8_t*)g.data(), g.size());
        } catch (...) {
            return err(ch, "cannot write the SD card"), true;
        }
        size_t lines = 0;
        for (char c : g) lines += c == '\n';
        all("[MSG:Teach:saved:" + std::string(JOB_FILE) + ":" + std::to_string(lines) + "]\n");
        return true;
    }
    err(ch, "unknown: $Teach/" + name);
    return true;
}

void teach_poll() {
    std::unique_lock<std::recursive_mutex> lock(mtx, std::try_to_lock);
    if (!lock.owns_lock()) return;  // a command is using the list right now
    if (pend.kind) {
        if ((int32_t)(millis() - pend.deadline) >= 0) {
            pend.kind = 0;
            err_all("the machine did not stop - nothing recorded");
        } else if (state_is(State::Idle) && !Job::active()) {
            Pending p = pend;
            pend.kind = 0;
            load();
            if (p.kind == 'a') do_add(p.op, p.fromPendant);
            else do_measure(p.n);
        }
    }
    if (dirty && state_is(State::Idle) && !Job::active()) save_list();
}
