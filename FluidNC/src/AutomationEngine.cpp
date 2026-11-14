// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "AutomationEngine.h"

#include <cmath>
#include <cstdio>

namespace Automation {
    using JsonReader::JVal;

    namespace {
        // seconds (a number) -> ms
        uint32_t secs(const JVal& o, const char* k, double d = 0) {
            double s = o.num(k, d);
            if (!(s > 0)) {
                return 0;
            }
            if (s > 4.0e6) {
                s = 4.0e6;
            }
            return uint32_t(s * 1000.0 + 0.5);
        }

        bool isNum(const JVal* v) { return v && v->t == JVal::Num; }

        // {"user": 3} or {"exp": 2, "in": 5}
        bool parseSrc(const JVal* v, Src& s, std::string& err) {
            if (!v || v->t != JVal::Obj) {
                err = "input missing";
                return false;
            }
            if (isNum(v->get("user"))) {
                s.kind = Src::User;
                s.n    = int(v->num("user"));
                if (s.n < 0 || s.n > 15) {
                    err = "user input must be 0-15";
                    return false;
                }
                return true;
            }
            if (isNum(v->get("exp"))) {
                s.kind = Src::Exp;
                s.ch   = int(v->num("exp"));
                s.n    = int(v->num("in", 0));
                if (s.n < 1 || s.n > 16) {
                    err = "expander input \"in\" must be 1-16";
                    return false;
                }
                return true;
            }
            err = "input needs \"user\" or \"exp\"+\"in\"";
            return false;
        }

        bool parseOut(const JVal& o, int& n, std::string& err, const char* key = "n") {
            if (!isNum(o.get(key))) {
                err = std::string("\"") + key + "\" (output number) missing";
                return false;
            }
            n = int(o.num(key));
            if (n < 0 || n > 15) {
                err = "output number must be 0-15";
                return false;
            }
            return true;
        }

        bool parseAction(const JVal& o, Action& a, bool inline_only, std::string& err) {
            std::string d = o.str("do");
            if (d == "out") {
                if (!parseOut(o, a.n, err)) {
                    return false;
                }
                auto v = o.get("value");
                if (v && v->t == JVal::Str && v->s == "toggle") {
                    a.act = Act::Toggle;
                } else {
                    a.act = Act::Out;
                    a.v   = o.truthy("value") ? 1 : 0;
                }
            } else if (d == "pulse") {
                a.act = Act::Pulse;
                if (!parseOut(o, a.n, err)) {
                    return false;
                }
                a.ms = secs(o, "time");
                if (!a.ms) {
                    err = "pulse needs \"time\" > 0";
                    return false;
                }
            } else if (d == "pwm") {
                a.act = Act::Pwm;
                if (!isNum(o.get("n"))) {
                    err = "pwm needs \"n\" (analog output)";
                    return false;
                }
                a.n = int(o.num("n"));
                a.v = float(o.num("value", 0));
                if (a.n < 0 || a.n > 3 || a.v < 0 || a.v > 100) {
                    err = "pwm: n 0-3, value 0-100";
                    return false;
                }
            } else if (d == "wait") {
                a.act = Act::Wait;
                a.ms  = secs(o, "time");
                if (!a.ms) {
                    err = "wait needs \"time\" > 0";
                    return false;
                }
            } else if (d == "wait_input") {
                a.act = Act::WaitIn;
                if (!parseSrc(o.get("input"), a.src, err)) {
                    return false;
                }
                a.v  = o.truthy("value") ? 1 : 0;
                a.ms = secs(o, "timeout");
                if (inline_only && !a.ms) {
                    err = "wait_input in an M-code needs a \"timeout\"";
                    return false;
                }
                std::string t = o.str("on_timeout", "stop");
                a.on_timeout  = t == "continue" ? OnTimeout::Continue : t == "hold" ? OnTimeout::Hold : OnTimeout::Stop;
                a.text        = o.str("text");
            } else if (d == "msg") {
                a.act  = Act::Msg;
                a.text = o.str("text");
            } else if (d == "hold") {
                a.act  = Act::Hold;
                a.text = o.str("text");
            } else if (d == "gcode") {
                if (inline_only) {
                    err = "gcode is not allowed in an M-code";
                    return false;
                }
                a.act  = Act::GCode;
                a.text = o.str("text");
                if (a.text.empty()) {
                    err = "gcode needs \"text\"";
                    return false;
                }
            } else {
                err = "unknown action \"" + d + "\"";
                return false;
            }
            return true;
        }

        bool parseRule(const JVal& o, Rule& r, std::string& err) {
            r.name    = o.str("name", "");
            r.enabled = !o.get("enabled") || o.truthy("enabled");
            r.when    = When::Always;
            std::string only = o.str("only", "always");
            if (only == "job") {
                r.when = When::Job;
            } else if (only == "idle") {
                r.when = When::Idle;
            } else if (only != "always") {
                err = "\"only\" must be always, job or idle";
                return false;
            }
            std::string t = o.str("type");
            if (t == "follow") {
                r.type = Type::Follow;
                if (!parseOut(o, r.out, err, "out")) {
                    return false;
                }
                std::string w = o.str("when", "spindle");
                if (w == "spindle") {
                    r.cond = Cond::Spindle;
                } else if (w == "job") {
                    r.cond = Cond::Job;
                } else if (w == "motion") {
                    r.cond = Cond::Motion;
                } else if (w == "idle") {
                    r.cond = Cond::Idle;
                } else if (w == "input") {
                    r.cond = Cond::Input;
                    if (!parseSrc(o.get("input"), r.src, err)) {
                        return false;
                    }
                } else {
                    err = "follow \"when\" must be spindle, job, motion, idle or input";
                    return false;
                }
                r.invert    = o.truthy("invert");
                r.on_delay  = secs(o, "on_delay");
                r.off_delay = secs(o, "off_delay");
                return true;
            }
            if (t == "input") {
                r.type = Type::Input;
                if (!parseSrc(o.get("input"), r.src, err)) {
                    return false;
                }
                std::string e = o.str("edge", "rise");
                r.edge        = e == "fall" ? Edge::Fall : e == "change" ? Edge::Change : Edge::Rise;
                r.debounce    = o.get("debounce") ? secs(o, "debounce") : 30;
            } else if (t == "mcode") {
                r.type  = Type::MCode;
                r.mcode = int(o.num("code", 0));
                if (r.mcode < 100 || r.mcode > 199) {
                    err = "mcode \"code\" must be 100-199";
                    return false;
                }
            } else if (t == "event") {
                r.type        = Type::Event;
                std::string e = o.str("event");
                if (e == "spindle_on") {
                    r.ev = Ev::SpindleOn;
                } else if (e == "spindle_off") {
                    r.ev = Ev::SpindleOff;
                } else if (e == "job_start") {
                    r.ev = Ev::JobStart;
                } else if (e == "job_end") {
                    r.ev = Ev::JobEnd;
                } else if (e == "alarm") {
                    r.ev = Ev::Alarm;
                } else if (e == "idle") {
                    r.ev    = Ev::Idle;
                    r.after = secs(o, "after");
                } else {
                    err = "unknown event \"" + e + "\"";
                    return false;
                }
            } else if (t == "timer") {
                r.type  = Type::Timer;
                r.every = secs(o, "every");
                if (r.every < 1000) {
                    err = "timer \"every\" must be at least 1 s";
                    return false;
                }
                std::string c = o.str("count", "always");
                r.count       = c == "motion" ? Count::Motion : c == "spindle" ? Count::Spindle : c == "job" ? Count::Job : Count::Always;
            } else if (t == "manual") {
                r.type = Type::Manual;
            } else {
                err = "unknown type \"" + t + "\"";
                return false;
            }
            auto acts = o.get("actions");
            if (!acts || acts->t != JVal::Arr || acts->a.empty()) {
                err = "\"actions\" missing";
                return false;
            }
            if (acts->a.size() > MaxActions) {
                err = "too many actions";
                return false;
            }
            for (size_t i = 0; i < acts->a.size(); ++i) {
                Action a;
                if (acts->a[i].t != JVal::Obj || !parseAction(acts->a[i], a, r.type == Type::MCode, err)) {
                    err = "action " + std::to_string(i + 1) + ": " + err;
                    return false;
                }
                r.actions.push_back(a);
            }
            return true;
        }

        bool after(uint32_t now, uint32_t t) { return int32_t(now - t) >= 0; }
    }

    const char* typeName(Type t) {
        switch (t) {
            case Type::Follow:
                return "follow";
            case Type::Input:
                return "input";
            case Type::MCode:
                return "mcode";
            case Type::Event:
                return "event";
            case Type::Timer:
                return "timer";
            default:
                return "manual";
        }
    }

    bool Engine::load(const std::string& text, std::string& err) {
        JVal root;
        if (!JsonReader::parse(text, root) || root.t != JVal::Obj) {
            err = "not valid JSON";
            return false;
        }
        if (root.num("version", 0) != 1) {
            err = "\"version\" must be 1";
            return false;
        }
        auto rules = root.get("rules");
        if (!rules || rules->t != JVal::Arr) {
            err = "\"rules\" missing";
            return false;
        }
        if (rules->a.size() > MaxRules) {
            err = "more than 32 rules";
            return false;
        }
        std::vector<Rule> list;
        for (size_t i = 0; i < rules->a.size(); ++i) {
            Rule r;
            if (rules->a[i].t != JVal::Obj || !parseRule(rules->a[i], r, err)) {
                err = "rule " + std::to_string(i) + (r.name.empty() ? "" : " (" + r.name + ")") + ": " + err;
                return false;
            }
            list.push_back(std::move(r));
        }
        _rules = std::move(list);
        _init  = false;
        return true;
    }

    bool Engine::whenOk(Env& env, const Rule& r) const {
        switch (r.when) {
            case When::Job:
                return env.job();
            case When::Idle:
                return !env.job();
            default:
                return true;
        }
    }

    void Engine::start(Rule& r) {
        if (r.running) {
            return;  // a rule that is still running ignores new triggers
        }
        r.running = true;
        r.step    = 0;
        r.phase   = 0;
        ++r.runs;
    }

    void Engine::fire(Ev ev) {
        for (auto& r : _rules) {
            if (r.enabled && r.type == Type::Event && r.ev == ev && ev != Ev::Idle) {
                r.cond_last = true;  // started below, after the "when" check
            }
        }
    }

    bool Engine::hasMCode(int code) const {
        for (auto& r : _rules) {
            if (r.enabled && r.type == Type::MCode && r.mcode == code) {
                return true;
            }
        }
        return false;
    }

    std::vector<Action> Engine::mcodeActions(int code) const {
        std::vector<Action> all;
        for (auto& r : _rules) {
            if (r.enabled && r.type == Type::MCode && r.mcode == code) {
                all.insert(all.end(), r.actions.begin(), r.actions.end());
            }
        }
        return all;
    }

    bool Engine::trigger(Env& env, size_t n) {
        if (n >= _rules.size() || !_rules[n].enabled || _rules[n].type == Type::Follow || _rules[n].type == Type::MCode) {
            return false;
        }
        start(_rules[n]);  // its actions run in the next poll()
        return true;
    }

    // Runs the actions until one has to wait.
    void Engine::run(Env& env, Rule& r, uint32_t now) {
        while (r.running && r.step < r.actions.size()) {
            const Action& a = r.actions[r.step];
            switch (a.act) {
                case Act::Out:
                    env.output(a.n, a.v != 0);
                    break;
                case Act::Toggle:
                    env.output(a.n, env.outputState(a.n) != 1);
                    break;
                case Act::Pwm:
                    env.analog(a.n, a.v);
                    break;
                case Act::Msg:
                    env.message(a.text);
                    break;
                case Act::Hold:
                    env.hold(a.text);
                    break;
                case Act::GCode:
                    if (!env.gcode(a.text)) {
                        env.message(r.name + ": G-code skipped (not Idle)");
                    }
                    break;
                case Act::Pulse:
                    if (r.phase == 0) {
                        env.output(a.n, true);
                        r.until = now + a.ms;
                        r.phase = 1;
                        return;
                    }
                    if (!after(now, r.until)) {
                        return;
                    }
                    env.output(a.n, false);
                    break;
                case Act::Wait:
                    if (r.phase == 0) {
                        r.until = now + a.ms;
                        r.phase = 1;
                        return;
                    }
                    if (!after(now, r.until)) {
                        return;
                    }
                    break;
                case Act::WaitIn: {
                    if (r.phase == 0) {
                        r.until = now + a.ms;
                        r.phase = 1;
                    }
                    int v = env.input(a.src);
                    if (v != int(a.v)) {
                        if (!a.ms || !after(now, r.until)) {
                            return;
                        }
                        // timed out
                        std::string why = a.text.empty() ? r.name + ": input timeout" : a.text;
                        if (a.on_timeout == OnTimeout::Hold) {
                            env.hold(why);
                            r.running = false;
                            return;
                        }
                        if (a.on_timeout == OnTimeout::Stop) {
                            env.message(why);
                            r.running = false;
                            return;
                        }
                    }
                    break;
                }
            }
            r.phase = 0;
            ++r.step;
        }
        r.running = false;
    }

    bool Engine::runInline(Env& env, const std::vector<Action>& actions) {
        for (auto& a : actions) {
            switch (a.act) {
                case Act::Out:
                    env.output(a.n, a.v != 0);
                    break;
                case Act::Toggle:
                    env.output(a.n, env.outputState(a.n) != 1);
                    break;
                case Act::Pwm:
                    env.analog(a.n, a.v);
                    break;
                case Act::Msg:
                    env.message(a.text);
                    break;
                case Act::Hold:
                    env.hold(a.text);
                    break;
                case Act::Pulse:
                    env.output(a.n, true);
                    if (!env.sleep(a.ms)) {
                        env.output(a.n, false);
                        return false;
                    }
                    env.output(a.n, false);
                    break;
                case Act::Wait:
                    if (!env.sleep(a.ms)) {
                        return false;
                    }
                    break;
                case Act::WaitIn: {
                    uint32_t start = env.ms();
                    while (env.input(a.src) != int(a.v)) {
                        if (env.ms() - start >= a.ms) {
                            std::string why = a.text.empty() ? std::string("M-code: input timeout") : a.text;
                            if (a.on_timeout == OnTimeout::Continue) {
                                break;
                            }
                            if (a.on_timeout == OnTimeout::Hold) {
                                env.hold(why);
                            } else {
                                env.message(why);
                            }
                            return true;  // the M-code ends here; not an error
                        }
                        if (!env.sleep(5)) {
                            return false;
                        }
                    }
                    break;
                }
                case Act::GCode:
                    break;  // refused by load()
            }
        }
        return true;
    }

    void Engine::poll(Env& env) {
        uint32_t now = env.ms();
        bool     sp = env.spindle(), job = env.job(), al = env.alarm(), idle = env.idle(), mv = env.moving();
        uint32_t dt = _init ? now - _last : 0;
        if (dt > 5000) {
            dt = 0;  // stalled: do not count it
        }
        _last = now;

        if (!_init) {
            _init = true;
            _sp = sp, _job = job, _alarm = al, _idle = idle;
            _idle_since = now;
            for (auto& r : _rules) {
                r.running   = false;
                r.cond_last = false;
                r.out_state = -1;
                r.last_in = r.raw_in = (r.type == Type::Input) ? env.input(r.src) : -1;
                r.raw_since  = now;
                r.cond_since = now;
                r.idle_fired = false;
            }
        }

        for (auto& r : _rules) {
            if (r.type == Type::Event) {
                r.cond_last = false;  // "fired" flag for this pass
            }
        }
        if (al && !_alarm) {
            // An alarm stops every running sequence (the alarm event rules start below)
            for (auto& r : _rules) {
                r.running = false;
            }
            fire(Ev::Alarm);
        }
        if (sp != _sp) {
            fire(sp ? Ev::SpindleOn : Ev::SpindleOff);
        }
        if (job != _job) {
            fire(job ? Ev::JobStart : Ev::JobEnd);
        }
        if (idle && !_idle) {
            _idle_since = now;
        }
        _sp = sp, _job = job, _alarm = al, _idle = idle;

        for (auto& r : _rules) {
            if (!r.enabled) {
                continue;
            }
            switch (r.type) {
                case Type::Follow: {
                    bool c = false;
                    switch (r.cond) {
                        case Cond::Spindle:
                            c = sp;
                            break;
                        case Cond::Job:
                            c = job;
                            break;
                        case Cond::Motion:
                            c = mv;
                            break;
                        case Cond::Idle:
                            c = idle;
                            break;
                        case Cond::Input:
                            c = env.input(r.src) == 1;
                            break;
                    }
                    if (r.invert) {
                        c = !c;
                    }
                    if (al) {
                        c = false;  // nothing is switched on in an alarm
                    }
                    if (c != r.cond_last || r.out_state < 0) {
                        if (c != r.cond_last) {
                            r.cond_since = now;
                        }
                        r.cond_last = c;
                    }
                    int target = r.out_state;
                    if (c && now - r.cond_since >= r.on_delay) {
                        target = 1;
                    } else if (!c && (now - r.cond_since >= r.off_delay || r.out_state < 0)) {
                        target = 0;
                    }
                    if (target >= 0 && target != r.out_state) {
                        env.output(r.out, target == 1);
                        r.out_state = target;
                    }
                    continue;
                }
                case Type::Input: {
                    int v = env.input(r.src);
                    if (v != r.raw_in) {
                        r.raw_in    = v;
                        r.raw_since = now;
                    }
                    if (v >= 0 && v != r.last_in && now - r.raw_since >= r.debounce) {
                        int prev  = r.last_in;
                        r.last_in = v;
                        bool hit  = prev >= 0 && (r.edge == Edge::Change || (r.edge == Edge::Rise) == (v == 1));
                        if (hit && !al && whenOk(env, r)) {
                            start(r);
                        }
                    }
                    break;
                }
                case Type::Event:
                    if (r.ev == Ev::Idle) {
                        if (!idle) {
                            r.idle_fired = false;
                        } else if (!r.idle_fired && now - _idle_since >= r.after) {
                            r.idle_fired = true;
                            start(r);
                        }
                    } else if (r.cond_last && (r.ev == Ev::Alarm || (!al && whenOk(env, r)))) {
                        start(r);
                    }
                    break;
                case Type::Timer: {
                    bool counting = r.count == Count::Always || (r.count == Count::Motion && mv) || (r.count == Count::Spindle && sp) ||
                                    (r.count == Count::Job && job);
                    if (counting) {
                        r.accum += dt;
                    }
                    if (r.accum >= r.every) {
                        r.accum -= r.every;
                        if (r.accum >= r.every) {
                            r.accum = 0;
                        }
                        if (!al && whenOk(env, r)) {
                            start(r);
                        }
                    }
                    break;
                }
                default:
                    break;
            }
            if (r.running) {
                run(env, r, now);
            }
        }
    }

    std::string Engine::line(size_t n) const {
        const Rule& r = _rules[n];
        std::string s = "[AUTO:" + std::to_string(n) + "," + typeName(r.type) + ",";
        if (!r.enabled) {
            s += "disabled";
        } else if (r.type == Type::Follow) {
            s += r.out_state == 1 ? "on" : "off";
        } else {
            s += r.running ? "running" : "ready";
        }
        if (r.type == Type::Timer) {
            s += "," + std::to_string(r.accum / 1000) + "/" + std::to_string(r.every / 1000);
        } else if (r.type == Type::MCode) {
            s += ",M" + std::to_string(r.mcode);
        }
        s += ",";
        for (char c : r.name) {
            s += (c == ']' || c == '|' || c == '\n' || c == '\r') ? ' ' : c;
        }
        return s;
    }
}
