// ResumeScan.h - reads G-code lines without moving and keeps the modal state
// and tool position, so a job can be continued from any line: everything the
// controller would have set up by that line (coordinate system, units,
// distance mode, plane, feed, spindle, coolant, tool) and where the tool was.
// Pure code (no FluidNC dependencies) so it can be tested on a PC.
#pragma once
#include <cstdint>
#include <string>

struct ResumeScan {
    // Modal state after the lines fed so far
    int         motion   = 0;      // 0 G0, 1 G1, 2 G2, 3 G3
    bool        inch     = false;  // G20
    bool        absolute = true;   // G90 / G91
    int         plane    = 17;     // G17 / G18 / G19
    std::string wcs      = "G54";
    float       feed     = 0;      // in file units / min
    float       speed    = 0;      // S
    int         spindle  = 5;      // 3, 4 or 5
    bool        mist     = false;  // M7
    bool        flood    = false;  // M8
    int         tool     = -1;     // last T word
    float       pos[4]   = { 0, 0, 0, 0 };  // X Y Z A, work coordinates, mm (A in degrees)
    bool        known[4] = { false, false, false, false };

    // Safe restart points seen so far (line numbers, 1-based; 0 = none)
    uint32_t line         = 0;  // lines fed
    uint32_t lastRetract  = 0;  // a rapid that raised Z (resume on the line after it)
    uint32_t lastToolLine = 0;  // T word or M6 (resume on the line after it)

    // Feed one line (as read from the file, comments allowed).
    void feed_line(const char* text);
};
