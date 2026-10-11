// RewindPath.cpp - see RewindPath.h
#include "RewindPath.h"

#include <cmath>
#include <algorithm>

void RwRing::init(RwEntry* storage, int capacity) {
    _buf   = storage;
    _cap   = capacity;
    _w     = 0;
    _count = 0;
}

void RwRing::push(const RwEntry& e) {
    if (!_buf || _cap <= 0) return;
    _buf[_w] = e;
    _w       = (_w + 1) % _cap;
    if (_count < _cap) _count++;
}

const RwEntry* RwRing::back(int k) const {
    if (k < 0 || k >= _count) return nullptr;
    int i = (_w - 1 - k) % _cap;
    if (i < 0) i += _cap;
    return &_buf[i];
}

void RwPath::build(const RwRing& ring, int queued, const float hold[4], float zShift, int maxVerts) {
    _x.clear(); _y.clear(); _z.clear(); _a.clear(); _d.clear();
    _segLine.clear(); _segFeed.clear();

    _x.push_back(hold[0]); _y.push_back(hold[1]); _z.push_back(hold[2]); _a.push_back(hold[3]);
    _d.push_back(0.0f);
    if (ring.count() == 0) return;

    // The block the tool stopped in (back index); -1 = it stopped at the end
    // of the newest block (planner empty).
    int            kCur = queued > 0 ? queued - 1 : -1;
    const RwEntry* ref  = ring.back(kCur >= 0 ? kCur : 0);
    if (!ref) return;  // the stopped block is not recorded (should not happen)

    for (int j = 0; (int)_d.size() < maxVerts; ++j) {
        int            kSeg = kCur + j;           // block cut along segment j
        const RwEntry* seg  = ring.back(kSeg >= 0 ? kSeg : 0);
        const RwEntry* v    = ring.back(kCur + 1 + j);  // its start = end of the block before
        if (!seg || !v) break;
        if (seg->spindle != ref->spindle || std::fabs(seg->speed - ref->speed) > 0.5f) break;  // tool change, spindle start
        float px = _x.back(), py = _y.back(), pz = _z.back(), pa = _a.back();
        float nx = v->p[0], ny = v->p[1], nz = v->p[2] + zShift, na = v->p[3];
        float dx = nx - px, dy = ny - py, dz = nz - pz;
        float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len < 1e-5f && std::fabs(na - pa) < 1e-5f) continue;  // zero-length: skip, keep going
        if (len < 1e-5f) len = std::fabs(na - pa);                // rotary-only move
        _x.push_back(nx); _y.push_back(ny); _z.push_back(nz); _a.push_back(na);
        _d.push_back(_d.back() + len);
        _segLine.push_back(seg->line);
        _segFeed.push_back(seg->feed);
    }
}

int RwPath::segment(float s) const {
    int n = (int)_d.size();
    if (n < 2) return -1;
    if (s <= 0.0f) return 0;
    if (s >= _d.back()) return n - 2;
    // first d[i] > s, minus one
    int i = int(std::upper_bound(_d.begin(), _d.end(), s) - _d.begin()) - 1;
    return std::max(0, std::min(i, n - 2));
}

void RwPath::at(float s, float out[4]) const {
    int i = segment(s);
    if (i < 0) {
        out[0] = _x[0]; out[1] = _y[0]; out[2] = _z[0]; out[3] = _a[0];
        return;
    }
    s       = std::max(0.0f, std::min(s, _d.back()));
    float L = _d[i + 1] - _d[i];
    float t = L > 0 ? (s - _d[i]) / L : 0.0f;
    out[0]  = _x[i] + (_x[i + 1] - _x[i]) * t;
    out[1]  = _y[i] + (_y[i + 1] - _y[i]) * t;
    out[2]  = _z[i] + (_z[i + 1] - _z[i]) * t;
    out[3]  = _a[i] + (_a[i + 1] - _a[i]) * t;
}

int32_t RwPath::lineAt(float s) const {
    int i = segment(s);
    return i < 0 ? 0 : _segLine[i];
}

float RwPath::feedAt(float s) const {
    int i = segment(s);
    return i < 0 ? 0.0f : _segFeed[i];
}

float RwPath::nextStop(float s, float target) const {
    if (_d.size() < 2) return 0.0f;
    if (target > s) {  // going back: next vertex beyond s
        auto it = std::upper_bound(_d.begin(), _d.end(), s + 1e-5f);
        float v = it == _d.end() ? _d.back() : *it;
        return std::min(v, target);
    }
    // going forward: previous vertex below s
    auto it = std::lower_bound(_d.begin(), _d.end(), s - 1e-5f);
    float v = (it == _d.begin()) ? 0.0f : *(it - 1);
    return std::max(v, target);
}
