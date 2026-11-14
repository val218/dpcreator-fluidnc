#include "gtest/gtest.h"
#include "AutomationEngine.h"
#include "JsonReader.h"

using namespace Automation;

namespace {
    struct FakeEnv : Env {
        uint32_t                 t = 1000;
        bool                     sp = false, jb = false, mv = false, idl = true, al = false;
        int                      in[16]  = {};
        int                      exp[17] = {};
        bool                     exp_online = true;
        int                      out[16]    = {};
        float                    pwm[4]     = {};
        std::vector<std::string> msgs, holds, gcodes;
        int                      writes = 0;

        uint32_t ms() override { return t; }
        bool     spindle() override { return sp; }
        bool     job() override { return jb; }
        bool     moving() override { return mv; }
        bool     idle() override { return idl; }
        bool     alarm() override { return al; }
        int      input(const Src& s) override {
            if (s.kind == Src::User) {
                return in[s.n];
            }
            return exp_online ? exp[s.n] : -1;
        }
        bool output(int n, bool on) override {
            out[n] = on;
            ++writes;
            return true;
        }
        int  outputState(int n) override { return out[n]; }
        bool analog(int n, float p) override {
            pwm[n] = p;
            return true;
        }
        void message(const std::string& s) override { msgs.push_back(s); }
        void hold(const std::string& s) override { holds.push_back(s); }
        bool gcode(const std::string& s) override {
            if (!idl) {
                return false;
            }
            gcodes.push_back(s);
            return true;
        }
        bool sleep(uint32_t ms) override {
            t += ms;
            return true;
        }
        void run(Engine& e, uint32_t ms, uint32_t step = 10) {
            for (uint32_t i = 0; i < ms; i += step) {
                t += step;
                e.poll(*this);
            }
        }
    };

    Engine load(const char* json) {
        Engine      e;
        std::string err;
        EXPECT_TRUE(e.load(json, err)) << err;
        return e;
    }
}

TEST(JsonReader, Parses) {
    JsonReader::JVal v;
    EXPECT_TRUE(JsonReader::parse(R"({"a":[1,2.5,"xé"],"b":true,"c":null})", v));
    EXPECT_EQ(v.get("a")->a.size(), 3u);
    EXPECT_EQ(v.get("a")->a[2].s, "x\xc3\xa9");
    EXPECT_TRUE(v.truthy("b"));
    EXPECT_FALSE(JsonReader::parse(R"({"a":1)", v));
    EXPECT_FALSE(JsonReader::parse(R"({"a":1} x)", v));
}

TEST(Automation, LoadErrors) {
    Engine      e;
    std::string err;
    EXPECT_FALSE(e.load("nope", err));
    EXPECT_FALSE(e.load(R"({"version":2,"rules":[]})", err));
    EXPECT_FALSE(e.load(R"({"version":1,"rules":[{"type":"mcode","code":99,"actions":[{"do":"msg"}]}]})", err));
    EXPECT_FALSE(e.load(R"({"version":1,"rules":[{"type":"mcode","code":110,"actions":[{"do":"gcode","text":"G0"}]}]})", err));
    EXPECT_FALSE(e.load(R"({"version":1,"rules":[{"type":"input","input":{"exp":2,"in":17},"actions":[{"do":"msg"}]}]})", err));
    EXPECT_FALSE(e.load(R"({"version":1,"rules":[{"type":"follow","when":"spindle"}]})", err));
    EXPECT_NE(err.find("rule 0"), std::string::npos);
    EXPECT_TRUE(e.load(R"({"version":1,"rules":[]})", err));
}

TEST(Automation, FollowWithOffDelay) {
    auto    e = load(R"({"version":1,"rules":[{"name":"Dust","type":"follow","when":"spindle","out":2,"off_delay":5}]})");
    FakeEnv env;
    env.run(e, 100);
    EXPECT_EQ(env.out[2], 0);
    env.sp = true;
    env.run(e, 20);
    EXPECT_EQ(env.out[2], 1);
    env.sp = false;
    env.run(e, 4900);
    EXPECT_EQ(env.out[2], 1);  // run-on
    env.sp = true;             // back on during the run-on: stays on
    env.run(e, 100);
    env.sp = false;
    env.run(e, 4900);
    EXPECT_EQ(env.out[2], 1);
    env.run(e, 200);
    EXPECT_EQ(env.out[2], 0);
    EXPECT_EQ(e.line(0), "[AUTO:0,follow,off,Dust");
}

TEST(Automation, FollowOffInAlarm) {
    auto    e = load(R"({"version":1,"rules":[{"type":"follow","when":"input","input":{"user":1},"out":0}]})");
    FakeEnv env;
    env.in[1] = 1;
    env.run(e, 50);
    EXPECT_EQ(env.out[0], 1);
    env.al = true;
    env.run(e, 50);
    EXPECT_EQ(env.out[0], 0);
}

TEST(Automation, InputEdgeDebounceAndToggle) {
    auto e = load(R"({"version":1,"rules":[{"type":"input","input":{"user":3},"edge":"rise","debounce":0.05,
                     "actions":[{"do":"out","n":5,"value":"toggle"}]}]})");
    FakeEnv env;
    env.run(e, 100);
    env.in[3] = 1;
    env.run(e, 20);  // a 20 ms blip is not enough
    env.in[3] = 0;
    env.run(e, 100);
    EXPECT_EQ(env.out[5], 0);
    env.in[3] = 1;
    env.run(e, 100);
    EXPECT_EQ(env.out[5], 1);
    env.in[3] = 0;
    env.run(e, 100);
    env.in[3] = 1;
    env.run(e, 100);
    EXPECT_EQ(env.out[5], 0);
}

TEST(Automation, ExpanderInputOnlyDuringJob) {
    auto e = load(R"({"version":1,"rules":[{"type":"input","input":{"exp":2,"in":4},"edge":"fall","only":"job",
                     "actions":[{"do":"hold","text":"Vacuum lost"}]}]})");
    FakeEnv env;
    env.exp[4] = 1;
    env.run(e, 100);
    env.exp[4] = 0;  // not in a job: nothing
    env.run(e, 100);
    EXPECT_TRUE(env.holds.empty());
    env.exp[4] = 1;
    env.run(e, 100);
    env.jb  = true;
    env.idl = false;
    env.exp[4] = 0;
    env.run(e, 100);
    ASSERT_EQ(env.holds.size(), 1u);
    EXPECT_EQ(env.holds[0], "Vacuum lost");
    env.exp_online = false;  // offline is not a falling edge
    env.run(e, 100);
    EXPECT_EQ(env.holds.size(), 1u);
}

TEST(Automation, TimerCountsMotionAndPulses) {
    auto e = load(R"({"version":1,"rules":[{"type":"timer","every":10,"count":"motion","actions":[{"do":"pulse","n":1,"time":2}]}]})");
    FakeEnv env;
    env.run(e, 20000);
    EXPECT_EQ(env.writes, 0);  // no motion, no lube
    env.mv = true;
    env.run(e, 10010);
    EXPECT_EQ(env.out[1], 1);
    env.mv = false;
    env.run(e, 2100);
    EXPECT_EQ(env.out[1], 0);
    EXPECT_NE(e.line(0).find(",timer,ready,"), std::string::npos);
}

TEST(Automation, EventsAndSequence) {
    auto e = load(R"({"version":1,"rules":[
        {"type":"event","event":"job_end","actions":[{"do":"wait","time":1},{"do":"out","n":0,"value":0},{"do":"msg","text":"done"}]},
        {"type":"event","event":"idle","after":60,"actions":[{"do":"pwm","n":1,"value":0}]},
        {"type":"event","event":"spindle_on","actions":[{"do":"gcode","text":"G0X0"}]}]})");
    FakeEnv env;
    env.out[0] = 1;
    env.pwm[1] = 50;
    env.jb = true, env.idl = false;
    env.run(e, 100);
    env.jb = false, env.idl = true;
    env.run(e, 500);
    EXPECT_EQ(env.out[0], 1);
    env.run(e, 600);
    EXPECT_EQ(env.out[0], 0);
    ASSERT_EQ(env.msgs.size(), 1u);
    env.run(e, 58000);
    EXPECT_EQ(env.pwm[1], 50);
    env.run(e, 1000);
    EXPECT_EQ(env.pwm[1], 0);
    env.sp  = true;  // spindle on while not idle: the G-code is skipped with a message
    env.idl = false;
    env.run(e, 20);
    EXPECT_TRUE(env.gcodes.empty());
    EXPECT_EQ(env.msgs.size(), 2u);
}

TEST(Automation, AlarmStopsSequences) {
    auto    e = load(R"({"version":1,"rules":[{"type":"manual","actions":[{"do":"wait","time":5},{"do":"out","n":3,"value":1}]},
                                              {"type":"event","event":"alarm","actions":[{"do":"out","n":4,"value":1}]}]})");
    FakeEnv env;
    env.run(e, 20);
    EXPECT_TRUE(e.trigger(env, 0));
    env.run(e, 1000);
    env.al = true;
    env.run(e, 5000);
    EXPECT_EQ(env.out[3], 0);
    EXPECT_EQ(env.out[4], 1);
    EXPECT_FALSE(e.trigger(env, 5));
}

TEST(Automation, MCodeInline) {
    auto e = load(R"({"version":1,"rules":[{"type":"mcode","code":110,"actions":[{"do":"out","n":1,"value":1},
                       {"do":"wait_input","input":{"user":2},"value":1,"timeout":3,"on_timeout":"hold","text":"No vacuum"}]}]})");
    FakeEnv env;
    EXPECT_TRUE(e.hasMCode(110));
    EXPECT_FALSE(e.hasMCode(111));
    EXPECT_TRUE(Engine::runInline(env, e.mcodeActions(110)));
    EXPECT_EQ(env.out[1], 1);
    ASSERT_EQ(env.holds.size(), 1u);  // input never came
    env.in[2] = 1;
    EXPECT_TRUE(Engine::runInline(env, e.mcodeActions(110)));
    EXPECT_EQ(env.holds.size(), 1u);
}
