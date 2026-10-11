// ResumeScan.cpp - see ResumeScan.h
#include "ResumeScan.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

void ResumeScan::feed_line(const char* text) {
    ++line;

    // Upper-case copy without comments: "(...)" anywhere, ";" to end of line.
    char buf[256];
    int  n       = 0;
    bool inParen = false;
    for (const char* p = text; *p && n < (int)sizeof(buf) - 1; ++p) {
        char c = *p;
        if (inParen) {
            if (c == ')') inParen = false;
            continue;
        }
        if (c == '(') { inParen = true; continue; }
        if (c == ';') break;
        buf[n++] = (char)toupper((unsigned char)c);
    }
    buf[n] = '\0';

    float  words[4];
    bool   has[4]    = { false, false, false, false };
    bool   g53       = false;  // machine coordinates for this line
    bool   g28       = false;  // G28 / G30 / G92: position becomes unknown
    bool   toolWord  = false;
    int    newMotion = -1;
    const char* p    = buf;
    while (*p) {
        while (*p && !isalpha((unsigned char)*p)) ++p;
        if (!*p) break;
        char  letter = *p++;
        char* end;
        float v = strtof(p, &end);
        if (end == p) continue;
        p = end;
        switch (letter) {
            case 'G': {
                int g = (int)lroundf(v * 10);  // in tenths: G91.1 is not G91
                switch (g) {
                    case 0: newMotion = 0; break;
                    case 10: newMotion = 1; break;
                    case 20: newMotion = 2; break;
                    case 30: newMotion = 3; break;
                    case 170: plane = 17; break;
                    case 180: plane = 18; break;
                    case 190: plane = 19; break;
                    case 200: inch = true; break;
                    case 210: inch = false; break;
                    case 900: absolute = true; break;
                    case 910: absolute = false; break;
                    case 530: g53 = true; break;
                    case 280:
                    case 300:
                    case 920: g28 = true; break;
                    case 540: wcs = "G54"; break;
                    case 550: wcs = "G55"; break;
                    case 560: wcs = "G56"; break;
                    case 570: wcs = "G57"; break;
                    case 580: wcs = "G58"; break;
                    case 590: wcs = "G59"; break;
                    default: break;
                }
                break;
            }
            case 'M': {
                int m = (int)lroundf(v);
                if (m == 3 || m == 4 || m == 5) spindle = m;
                else if (m == 7) mist = true;
                else if (m == 8) flood = true;
                else if (m == 9) mist = flood = false;
                else if (m == 6) toolWord = true;
                break;
            }
            case 'T': tool = (int)lroundf(v); toolWord = true; break;
            case 'F': feed = v; break;
            case 'S': speed = v; break;
            case 'X': words[0] = v; has[0] = true; break;
            case 'Y': words[1] = v; has[1] = true; break;
            case 'Z': words[2] = v; has[2] = true; break;
            case 'A': words[3] = v; has[3] = true; break;
            default: break;
        }
    }

    if (newMotion >= 0) motion = newMotion;
    if (toolWord) lastToolLine = line;

    if (g28) {  // reference moves / offsets: where the tool ends up is not known here
        for (int i = 0; i < 4; ++i) {
            if (has[i] || g28) known[i] = false;
        }
        return;
    }
    float oldZ      = pos[2];
    bool  oldZKnown = known[2];
    for (int i = 0; i < 4; ++i) {
        if (!has[i]) continue;
        if (g53) {  // machine coordinates: not a work position we can restore
            known[i] = false;
            continue;
        }
        float mm = (inch && i < 3) ? words[i] * 25.4f : words[i];
        if (absolute) {
            pos[i]   = mm;
            known[i] = true;
        } else if (known[i]) {
            pos[i] += mm;
        }
    }
    // A rapid that raises Z: the tool is clear of the part after this line.
    if (has[2] && !g53 && motion == 0 && known[2] && (!oldZKnown || pos[2] > oldZ + 0.001f)) {
        lastRetract = line;
    }
}
