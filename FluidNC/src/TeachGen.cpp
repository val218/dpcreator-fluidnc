// TeachGen.cpp - see TeachGen.h. Keep in step with generateGcode() in the
// dpCREATOR WebUI (src/app.js, Record & Retrace): same lines, same numbers.
#include "TeachGen.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

// Like JavaScript's v.toFixed(3): an exact half rounds away from zero
// (printf would round it to even: -96.3125 -> -96.312 instead of -96.313),
// and any negative value keeps its sign (-0.0004 -> "-0.000", -0 -> "0.000").
std::string teach_f3(double v) {
    if (!std::isfinite(v)) return "NaN";
    char digits[400];
    snprintf(digits, sizeof(digits), "%.60f", std::fabs(v));  // exact decimal expansion
    std::string s(digits);
    size_t      dot = s.find('.');
    std::string keep = s.substr(0, dot + 4);  // "123.456"
    if (s[dot + 4] >= '5') {                  // round up the magnitude
        int i = (int)keep.size() - 1;
        for (; i >= 0; i--) {
            if (keep[i] == '.') continue;
            if (keep[i] == '9') { keep[i] = '0'; continue; }
            keep[i]++;
            break;
        }
        if (i < 0) keep = "1" + keep;
    }
    return (v < 0 ? "-" : "") + keep;
}

std::string teach_num(double v) {
    char b[40];
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) {
        snprintf(b, sizeof(b), "%.0f", v + 0.0);
        return b;
    }
    for (int prec = 1; prec <= 17; prec++) {  // shortest text that reads back the same
        snprintf(b, sizeof(b), "%.*g", prec, v);
        if (std::strtod(b, nullptr) == v) break;
    }
    std::string s(b);  // JS writes exponents without leading zeros: 1e-9, not 1e-09
    size_t      e = s.find('e');
    if (e != std::string::npos) {
        size_t d = e + 2;
        while (d < s.size() - 1 && s[d] == '0') s.erase(d, 1);
    }
    return s;
}

static const char* op_label(char op) {
    switch (op) {
        case 'm': return "Move only";
        case 'd': return "Drill";
        case 'r': return "Pocket (round)";
        case 'p': return "Pocket (rectangle)";
    }
    return "?";
}

void teach_ref_offset(const TeachPt& p, double& cx, double& cy) {
    double hx = p.w / 2, hy = (p.op == 'p' && p.h > 0 ? p.h : p.w) / 2;
    cx = p.x;
    cy = p.y;
    if (p.ref == "bl") { cx += hx; cy += hy; }
    else if (p.ref == "br") { cx -= hx; cy += hy; }
    else if (p.ref == "tl") { cx += hx; cy -= hy; }
    else if (p.ref == "tr") { cx -= hx; cy -= hy; }
}

// Concentric clearing offsets from the centre out to `max`, ending exactly on it.
static std::vector<double> rings(double max, double step) {
    std::vector<double> out;
    for (double r = step; r < max - 1e-6; r += step) out.push_back(r);
    out.push_back(max);
    return out;
}

std::string teach_gcode(const TeachCfg& cfg, const std::vector<TeachPt>& pts, std::vector<std::string>& problems) {
    const double safeZ = cfg.safeZ, toolD = cfg.tool, toolR = toolD / 2;
    const double step  = std::max(0.05, toolD * 0.4);  // 40% stepover
    const std::string F = teach_num(cfg.feed), P = teach_num(cfg.plunge);
    problems.clear();
    if (!(safeZ > 0)) problems.push_back("Safe Z is " + teach_num(safeZ) + ": it must be above the work surface (Z0), e.g. 5");
    if (!(toolD > 0)) problems.push_back("Tool diameter must be above 0");

    std::string g = "; dpCREATOR Retrace path (work coordinates, Z0 = top of the part)\n";
    g += "; Safe Z " + teach_f3(safeZ) + " | Tool \xC3\x98" + teach_num(toolD) + " | Feed " + F + " | Plunge " + P +
         " | Spindle " + teach_num(cfg.rpm) + "\n\n";
    g += "G21 G90 G17\n";
    g += "G0 Z" + teach_f3(safeZ) + "         ; lift to safe Z first\n";
    g += "M3 S" + teach_num(cfg.rpm) + "          ; spindle on\n\n";

    for (size_t i = 0; i < pts.size(); i++) {
        const TeachPt& wp = pts[i];
        const std::string n = std::to_string(i + 1);
        const double depth = std::fabs(wp.depth);
        const int    passes = std::max(1, wp.passes);
        const bool   pocket = wp.op == 'r' || wp.op == 'p';
        const double w = wp.w, h = (wp.op == 'p' && wp.h > 0) ? wp.h : wp.w;
        double cx, cy;
        teach_ref_offset(wp, cx, cy);
        g += std::string("; --- waypoint ") + n + " (" + op_label(wp.op) + ") ---\n";
        if ((wp.op == 'd' || pocket) && !(depth > 0)) {
            problems.push_back("Waypoint " + n + ": set a depth");
            g += "; SKIPPED: no depth set\n\n";
            continue;
        }
        if (pocket && !(w > toolD && h > toolD)) {
            problems.push_back(w > 0 ? "Waypoint " + n + ": size " + teach_num(w) + (wp.op == 'p' ? "x" + teach_num(h) : "") +
                                           " mm must be bigger than the tool (\xC3\x98" + teach_num(toolD) + " mm)"
                                     : "Waypoint " + n + ": set the pocket size");
            g += "; SKIPPED: pocket size not set or not bigger than the tool\n\n";
            continue;
        }
        g += "G0 X" + teach_f3(cx) + " Y" + teach_f3(cy) + "  ; rapid XY\n";

        if (wp.op == 'm') {
            g += "G0 Z" + teach_f3(wp.z) + "             ; recorded Z\n";
            g += "G0 Z" + teach_f3(safeZ) + "\n";
        } else if (wp.op == 'd') {
            const double dStep = depth / passes;
            for (int p = 1; p <= passes; p++) {
                g += "G1 Z" + teach_f3(-dStep * p) + " F" + P + "   ; drill " + std::to_string(p) + "/" + std::to_string(passes) + "\n";
                if (p < passes) g += "G0 Z1.000\n";
            }
            g += "G0 Z" + teach_f3(safeZ) + "             ; retract\n";
        } else {
            // tool-centre offsets of the finished walls; a rectangle grows
            // from a centre slot so its rings reach both walls together
            const double hx = w / 2 - toolR, hy = h / 2 - toolR, m = std::min(hx, hy);
            const double dStep = depth / passes;
            for (int p = 1; p <= passes; p++) {
                g += "G0 X" + teach_f3(cx) + " Y" + teach_f3(cy) + "\n";
                g += "G1 Z" + teach_f3(-dStep * p) + " F" + P + "     ; pass " + std::to_string(p) + "/" + std::to_string(passes) + "\n";
                for (double r : rings(m, step)) {
                    if (wp.op == 'r') {
                        g += "G1 X" + teach_f3(cx + r) + " Y" + teach_f3(cy) + " F" + F + "\n";
                        g += "G3 X" + teach_f3(cx + r) + " Y" + teach_f3(cy) + " I" + teach_f3(-r) + " J0\n";  // full circle, climb
                    } else {
                        const double ax = r + (hx - m), ay = r + (hy - m);
                        g += "G1 X" + teach_f3(cx + ax) + " Y" + teach_f3(cy - ay) + " F" + F + "\n";
                        g += "G1 X" + teach_f3(cx + ax) + " Y" + teach_f3(cy + ay) + "\n";
                        g += "G1 X" + teach_f3(cx - ax) + " Y" + teach_f3(cy + ay) + "\n";
                        g += "G1 X" + teach_f3(cx - ax) + " Y" + teach_f3(cy - ay) + "\n";
                        g += "G1 X" + teach_f3(cx + ax) + " Y" + teach_f3(cy - ay) + "\n";
                    }
                }
            }
            g += "G0 Z" + teach_f3(safeZ) + "\n";
        }
        g += "\n";
    }
    g += "M5           ; spindle off\n";
    g += "G0 Z" + teach_f3(safeZ) + "   ; final lift\n";
    g += "M2           ; end\n";
    if (!problems.empty()) {
        std::string head;
        for (auto& p : problems) head += "; ! " + p + "\n";
        g = head + "\n" + g;
    }
    return g;
}
