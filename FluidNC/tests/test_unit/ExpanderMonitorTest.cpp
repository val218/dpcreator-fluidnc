#include "gtest/gtest.h"
#include "ExpanderMonitor.h"

namespace {
    void frame(ExpanderMonitor& m, uint8_t i0, uint8_t i1, uint8_t o0, uint8_t o1, uint8_t f, uint32_t now) {
        uint8_t b[5] = { i0, i1, o0, o1, f };
        for (uint32_t i = 0; i < 5; ++i) {
            m.feed(0xA000 + (i << 8) + b[i], now);
        }
    }
}

TEST(ExpanderMonitor, NoFrameIsOffline) {
    ExpanderMonitor m(2, "dpCREATOR IO16 v1.0.0");
    EXPECT_FALSE(m.valid());
    EXPECT_EQ(m.field(0), "2,-");
    EXPECT_EQ(m.stateLine(0), "[EXP:2,ID=dpCREATOR IO16 v1.0.0,OFFLINE]");
}

TEST(ExpanderMonitor, FramePublishesOnFlags) {
    ExpanderMonitor m(2, "dpCREATOR IO16 v1.0.0");
    EXPECT_FALSE(m.feed(0xA0A1, 100));
    EXPECT_FALSE(m.feed(0xA100, 100));
    EXPECT_FALSE(m.feed(0xA203, 100));
    EXPECT_FALSE(m.feed(0xA300, 100));
    EXPECT_FALSE(m.valid());
    EXPECT_TRUE(m.feed(0xA407, 100));
    EXPECT_EQ(m.inputs(), 0x00A1);
    EXPECT_EQ(m.outputs(), 0x0003);
    EXPECT_EQ(m.flags(), 0x07);
    EXPECT_EQ(m.field(200), "2,00A1,0003,07");
    EXPECT_EQ(m.stateLine(200), "[EXP:2,ID=dpCREATOR IO16 v1.0.0,IN=00A1,OUT=0003,FLAGS=07]");
}

TEST(ExpanderMonitor, HighBytesAndChangeCounting) {
    ExpanderMonitor m(3, "x");
    frame(m, 0x01, 0x80, 0x00, 0xFF, 0x0F, 0);
    EXPECT_EQ(m.field(10), "3,8001,FF00,0F");
    uint32_t s = m.seq();
    frame(m, 0x01, 0x80, 0x00, 0xFF, 0x0F, 1000);  // the once-a-second repeat
    EXPECT_EQ(m.seq(), s);
    frame(m, 0x01, 0x80, 0x01, 0xFF, 0x0F, 1020);
    EXPECT_EQ(m.seq(), s + 1);
}

TEST(ExpanderMonitor, IncompleteFrameIsSkipped) {
    ExpanderMonitor m(2, "x");
    frame(m, 1, 0, 0, 0, 7, 0);
    m.feed(0xA0FF, 10);  // IN1-8 only, then flags
    EXPECT_FALSE(m.feed(0xA407, 10));
    EXPECT_EQ(m.inputs(), 1);
    frame(m, 2, 0, 0, 0, 7, 20);  // next whole frame is taken again
    EXPECT_EQ(m.inputs(), 2);
}

TEST(ExpanderMonitor, GoesOfflineAfterThreeSeconds) {
    ExpanderMonitor m(2, "x");
    frame(m, 0, 0, 0, 0, 1, 1000);
    EXPECT_TRUE(m.online(3999));
    EXPECT_FALSE(m.online(4000));
    EXPECT_EQ(m.field(5000), "2,-");
}

TEST(ExpanderMonitor, NotAFrame) {
    ExpanderMonitor m(2, "x");
    EXPECT_FALSE(ExpanderMonitor::isFrame(0x9FFF));
    EXPECT_FALSE(ExpanderMonitor::isFrame(0xA500));
    EXPECT_TRUE(ExpanderMonitor::isFrame(0xA4FF));
    EXPECT_FALSE(m.feed(0x140, 0));
}

TEST(ExpanderMonitor, ReportFieldAndVersion) {
    auto& all = ExpanderMonitor::all();
    all.clear();
    EXPECT_EQ(ExpanderMonitor::reportField(0), "");
    ExpanderMonitor a(2, "a"), b(3, "b");
    all.push_back(&a);
    all.push_back(&b);
    frame(a, 0xA1, 0, 3, 0, 7, 0);
    EXPECT_EQ(ExpanderMonitor::reportField(100), "2,00A1,0003,07;3,-");
    uint32_t v = ExpanderMonitor::reportVersion(100);
    EXPECT_EQ(ExpanderMonitor::reportVersion(200), v);
    frame(a, 0xA0, 0, 3, 0, 7, 300);
    uint32_t v2 = ExpanderMonitor::reportVersion(300);
    EXPECT_NE(v2, v);
    EXPECT_NE(ExpanderMonitor::reportVersion(3300), v2);  // went offline
    all.clear();
}

TEST(ExpanderMonitor, PinId) {
    std::string id;
    EXPECT_TRUE(expander_pin_id("uart_channel2.16:low", id));
    EXPECT_EQ(id, "2.16");
    EXPECT_TRUE(expander_pin_id("uart_channel10.0", id));
    EXPECT_EQ(id, "10.0");
    EXPECT_FALSE(expander_pin_id("gpio.16", id));
    EXPECT_FALSE(expander_pin_id("uart_channel2", id));
    EXPECT_FALSE(expander_pin_id("uart_channel.3", id));
    EXPECT_FALSE(expander_pin_id("uart_channel2.x", id));
    EXPECT_FALSE(expander_pin_id(nullptr, id));
}
