#include "gtest/gtest.h"
#include "SCurve.h"

using namespace SCurve;

TEST(SCurve, ShapeEnds) {
    for (float k : { 0.0f, 0.5f, 1.0f }) {
        EXPECT_FLOAT_EQ(shape(k, 0), 0);
        EXPECT_FLOAT_EQ(shape(k, 1), 1);
        EXPECT_FLOAT_EQ(area(k, 1), 0.5f);  // same distance as the straight ramp for every strength
        EXPECT_NEAR(shape(k, 0.5f), 0.5f, 1e-6);
    }
}

TEST(SCurve, AccelerationProfile) {
    EXPECT_FLOAT_EQ(accelFactor(1, 0), 0);    // Full: from zero acceleration
    EXPECT_FLOAT_EQ(accelFactor(1, 1), 0);
    EXPECT_FLOAT_EQ(accelFactor(1, 0.5f), 1.5f);
    EXPECT_FLOAT_EQ(accelFactor(0.5f, 0), 0.5f);
    EXPECT_FLOAT_EQ(accelFactor(0.5f, 0.5f), 1.25f);
    EXPECT_FLOAT_EQ(accelFactor(0, 0.3f), 1);
    // numeric derivative of shape() matches
    for (float s = 0.05f; s < 1; s += 0.1f) {
        float h = 1e-3f, d = (shape(1, s + h) - shape(1, s - h)) / (2 * h);
        EXPECT_NEAR(d, accelFactor(1, s), 2e-3);
    }
}

TEST(SCurve, RampMatchesStraightRamp) {
    // 0 -> 3000 mm/min at 200 mm/s^2 = 720000 mm/min^2
    Ramp r;
    r.start(1.0f, 0, 3000, 720000, 50);
    EXPECT_NEAR(r.T * 60, 0.25f, 1e-5);                           // 0.25 s, as the straight ramp
    EXPECT_NEAR(r.length(), 3000.0f * 3000.0f / (2 * 720000), 1e-3);  // 6.25 mm
    EXPECT_NEAR(r.distAt(r.T), r.length(), 1e-4);
    EXPECT_NEAR(r.endMm(), 50 - 6.25f, 1e-3);
    EXPECT_NEAR(r.speedAt(r.T), 3000, 1e-2);
    // slower at the start, faster later, never above the target
    EXPECT_LT(r.speedAt(r.T * 0.1f), 3000 * 0.1f);
    EXPECT_GT(r.speedAt(r.T * 0.9f), 3000 * 0.9f);
    float last = 0;
    for (int i = 1; i <= 100; ++i) {
        float v = r.speedAt(r.T * i / 100);
        EXPECT_GE(v, last);
        EXPECT_LE(v, 3000.01f);
        last = v;
    }
}

TEST(SCurve, Deceleration) {
    Ramp r;
    r.start(1.0f, 2400, 600, 720000, 10);
    EXPECT_NEAR(r.length(), (2400.0f * 2400 - 600.0f * 600) / (2 * 720000), 1e-3);
    EXPECT_NEAR(r.speedAt(0), 2400, 1e-3);
    EXPECT_NEAR(r.speedAt(r.T), 600, 1e-2);
    // an S-deceleration is ahead of the straight one at the same time (still faster)
    float t = r.T * 0.3f;
    EXPECT_GT(r.speedAt(t), 2400 - 720000 * t);
}
