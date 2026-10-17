// TeachGen.h - Record & Retrace G-code generator (pure C++, no FluidNC deps,
// so it can be compiled and tested on a PC). Same algorithm and output as
// the dpCREATOR WebUI's Retrace generator.
//
// A waypoint is a position recorded by jogging to a pencil mark, plus what
// to do there. Coordinates are work coordinates, Z0 = top of the part.
#pragma once
#include <string>
#include <vector>

struct TeachCfg {
    double safeZ  = 5;      // travel height above Z0, must be > 0
    double feed   = 500;    // cutting feed mm/min
    double plunge = 150;    // Z feed mm/min
    double rpm    = 12000;
    double tool   = 3;      // cutter diameter mm
};

// op:  'm' move only   'd' drill   'r' round pocket   'p' rectangle pocket
// ref: where the mark is on the pocket: "c" centre, "bl" "br" "tl" "tr" corner
struct TeachPt {
    double      x = 0, y = 0, z = 0;
    char        op     = 'd';
    std::string ref    = "c";
    double      depth  = 5;   // below Z0, positive
    double      w      = 0;   // round: diameter; rectangle: width (X)
    double      h      = 0;   // rectangle: height (Y); 0 = square (h = w)
    int         passes = 1;   // depth passes (drill: pecks)
    double      feed   = 0;   // cutting feed mm/min here; 0 = the job's (TeachCfg)
    double      plunge = 0;   // Z feed mm/min here; 0 = the job's
    double      ramp   = 0;   // pockets: enter on a helix down at this angle (degrees); 0 = straight plunge
};

// Builds the program. Problems (missing depth, pocket smaller than the tool,
// bad safe Z) are returned in `problems`; such waypoints are skipped in the
// output with a comment - the caller should refuse to run it.
std::string teach_gcode(const TeachCfg& cfg, const std::vector<TeachPt>& pts, std::vector<std::string>& problems);

// The mark position -> pocket centre, for a mark on a corner.
void teach_ref_offset(const TeachPt& p, double& cx, double& cy);

// Number formatting shared with the protocol: like JavaScript's String(n)
// for the values a user types (500, 0.5, 12.25), and toFixed(3).
std::string teach_num(double v);
std::string teach_f3(double v);
