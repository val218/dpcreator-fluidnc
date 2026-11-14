// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Automations: rules that run on the board, without a browser open.
//
//   follow   an output follows a condition (spindle, job, motion, idle, an
//            input), with on/off delays - dust extraction, air, vacuum
//   input    an input edge starts a list of actions - a pedal, vacuum lost
//   mcode    M100-M199 runs a list of actions, in order with the moves
//   event    spindle on/off, job start/end, alarm, idle for N s
//   timer    every N s of time, motion, spindle or job time - lubrication
//   manual   only from $Automation/Run=<n>
//
// Actions: out (on/off/toggle), pulse, pwm, wait, wait_input, msg, hold
// (feed hold) and gcode (Idle only, never inside a job).  Actions that only
// switch outputs run at any time, also during a job; nothing here moves the
// machine except gcode, which is refused while a job runs.
//
// The rules come from automation.json (format: see AUTOMATION.md).  This file
// is pure logic: the board is reached through Env, so the unit tests can drive
// it with a fake one.
#pragma once

#include "JsonReader.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Automation {
    static const size_t MaxRules   = 32;
    static const size_t MaxActions = 32;

    struct Src {
        enum Kind : uint8_t { None, User, Exp } kind = None;
        int ch = 0;  // Exp: uart_channel number
        int n  = 0;  // User: digital input 0-15; Exp: IN1-IN16
    };

    enum class Type : uint8_t { Follow, Input, MCode, Event, Timer, Manual };
    enum class Cond : uint8_t { Spindle, Job, Motion, Idle, Input };
    enum class Ev : uint8_t { SpindleOn, SpindleOff, JobStart, JobEnd, Alarm, Idle };
    enum class Count : uint8_t { Always, Motion, Spindle, Job };
    enum class When : uint8_t { Always, Job, Idle };
    enum class Edge : uint8_t { Rise, Fall, Change };
    enum class Act : uint8_t { Out, Toggle, Pulse, Pwm, Wait, WaitIn, Msg, Hold, GCode };
    enum class OnTimeout : uint8_t { Stop, Continue, Hold };

    struct Action {
        Act         act = Act::Msg;
        int         n   = 0;    // output / analog output number
        float       v   = 0;    // out: 0/1, pwm: percent, wait_input: 0/1
        uint32_t    ms  = 0;    // pulse / wait / wait_input timeout (0 = no timeout)
        Src         src;        // wait_input
        std::string text;       // msg, hold, gcode
        OnTimeout   on_timeout = OnTimeout::Stop;
    };

    // What the engine needs from the board.
    struct Env {
        virtual ~Env() {}
        virtual uint32_t ms()                              = 0;
        virtual bool     spindle()                         = 0;  // spindle (or laser) on
        virtual bool     job()                             = 0;  // a file job, or a G-code program moving or held
        virtual bool     moving()                          = 0;  // Cycle, Jog, Homing
        virtual bool     idle()                            = 0;  // Idle, no job
        virtual bool     alarm()                           = 0;
        virtual int      input(const Src& s)               = 0;  // 0/1, -1 = not known (not set up, expander offline)
        virtual bool     output(int n, bool on)            = 0;  // false: output n is not set up
        virtual int      outputState(int n)                = 0;
        virtual bool     analog(int n, float percent)      = 0;
        virtual void     message(const std::string& text)  = 0;
        virtual void     hold(const std::string& text)     = 0;  // feed hold
        virtual bool     gcode(const std::string& text)    = 0;  // false: not Idle, not run
        virtual bool     sleep(uint32_t ms) { return false; }    // inline (M-code) waits; false = reset
    };

    struct Rule {
        std::string name;
        bool        enabled = true;
        Type        type    = Type::Manual;

        // follow
        Cond     cond = Cond::Spindle;
        bool     invert   = false;
        uint32_t on_delay = 0, off_delay = 0;
        int      out      = -1;

        // triggers
        Src      src;  // follow "input", input trigger
        Edge     edge     = Edge::Rise;
        uint32_t debounce = 30;
        int      mcode    = 0;
        Ev       ev       = Ev::JobStart;
        uint32_t after    = 0;  // event idle: ms
        uint32_t every    = 0;  // timer: ms
        Count    count    = Count::Always;
        When     when     = When::Always;

        std::vector<Action> actions;

        // runtime
        bool     running = false;
        size_t   step    = 0;
        uint8_t  phase   = 0;
        uint32_t until   = 0;
        int      last_in = -1, raw_in = -1;
        uint32_t raw_since  = 0;
        bool     cond_last  = false;
        uint32_t cond_since = 0;
        int      out_state  = -1;
        uint32_t accum      = 0;
        bool     idle_fired = false;
        uint32_t runs       = 0;
    };

    class Engine {
    public:
        // Replaces the rules.  On an error the old rules stay and err says why.
        bool load(const std::string& text, std::string& err);
        void clear() { _rules.clear(); _init = false; }

        void poll(Env& env);

        bool hasMCode(int code) const;
        // The actions of the enabled M<code> rules, to run inline with runInline().
        std::vector<Action> mcodeActions(int code) const;
        static bool         runInline(Env& env, const std::vector<Action>& actions);

        // $Automation/Run=<n>.  False if there is no such rule or it is disabled.
        bool trigger(Env& env, size_t n);

        size_t      size() const { return _rules.size(); }
        const Rule& rule(size_t n) const { return _rules[n]; }
        // [AUTO:<n>,<type>,<state>[,<detail>],<name>] without the closing ']'
        std::string line(size_t n) const;

    private:
        std::vector<Rule> _rules;
        bool              _init = false;
        uint32_t          _last = 0;
        bool              _sp = false, _job = false, _alarm = false, _idle = false;
        uint32_t          _idle_since = 0;

        bool whenOk(Env& env, const Rule& r) const;
        void start(Rule& r);
        void run(Env& env, Rule& r, uint32_t now);
        void fire(Ev ev);
    };

    const char* typeName(Type t);
}
