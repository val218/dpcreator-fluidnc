// RewindPath.h - the executed toolpath for handwheel rewind (Rewind.h).
// Plain C++ (no FluidNC headers), so the path math is tested on a PC.
#pragma once
#include <cstdint>
#include <vector>

// One planner block of the running job, recorded when it was planned:
// where it ends (motor positions, mm) and how it was cut.
struct RwEntry {
    float   p[4];      // X Y Z A at the end of the block
    float   feed;      // mm/min (0 = rapid)
    float   speed;     // spindle rpm
    int32_t line;      // file line
    uint8_t spindle;   // spindle state (3 cw, 4 ccw, 5 off)
};

class RwRing {
public:
    void           init(RwEntry* storage, int capacity);
    void           clear() { _w = 0; _count = 0; }
    void           push(const RwEntry& e);
    int            count() const { return _count; }
    int            capacity() const { return _cap; }
    const RwEntry* back(int k) const;  // k = 0: newest

private:
    RwEntry* _buf   = nullptr;
    int      _cap   = 0;
    int      _w     = 0;
    int      _count = 0;
};

// The way back from the hold point: vertex 0 is where the tool stopped, vertex
// i+1 is the start of the stretch before it. Segment i (vertex i -> i+1) was
// cut with segFeed[i] on file line segLine[i].
class RwPath {
public:
    // queued = planner blocks still queued at the hold (incl. the one the tool
    // stopped in). zShift = offset to add to recorded Z (live Z adjust made).
    // Stops at the first block cut with another spindle state / speed (tool
    // change, spindle start) or at the oldest recorded block.
    void build(const RwRing& ring, int queued, const float hold[4], float zShift, int maxVerts = 1 << 30);

    int   vertices() const { return (int)_d.size(); }
    float length() const { return _d.empty() ? 0.0f : _d.back(); }  // how far back it goes, mm
    // Point s mm back along the path (0 = hold point), clamped to the path.
    void    at(float s, float out[4]) const;
    int32_t lineAt(float s) const;   // file line being retraced at s
    float   feedAt(float s) const;   // programmed feed there (0 = rapid)
    // The vertex boundary next to s in the direction of target (to stop at corners).
    float nextStop(float s, float target) const;

private:
    int                  segment(float s) const;  // i with d[i] <= s < d[i+1]
    std::vector<float>   _x, _y, _z, _a, _d;
    std::vector<int32_t> _segLine;
    std::vector<float>   _segFeed;
};
