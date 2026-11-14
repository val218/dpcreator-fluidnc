// Copyright (c) 2026 - dpCREATOR
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Automations on the board (see AutomationEngine.h, AUTOMATION.md).
//
//   automation.json on the local filesystem holds the rules; the WebUI writes it.
//   $Automation/List   ($AUL)    [AUTO:<n>,<type>,<state>[,<detail>],<name>] per rule
//   $Automation/Reload ($AUR)    read automation.json again
//   $Automation/Run=<n> ($AUX)   start rule n (input/event/timer/manual rules)
//
// The rules run on the polling task, about every 10 ms.  M100-M199 rules run
// inline in the G-code, after the moves before them (like M62/M66).
#include "Automation.h"
#include "AutomationEngine.h"
#include "ExpanderMonitor.h"
#include "FileStream.h"
#include "GCode.h"
#include "Job.h"
#include "Logging.h"
#include "Machine/MachineConfig.h"
#include "Machine/Macros.h"
#include "Modbus/ModbusBus.h"
#include "Protocol.h"
#include "Settings.h"
#include "State.h"
#include "System.h"

#include <Arduino.h>  // millis
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
    const char* FILE_NAME = "automation.json";

    Automation::Engine engine;
    SemaphoreHandle_t  lock         = nullptr;
    bool               loaded       = false;
    volatile bool      reload_asked = false;

    struct Lock {
        Lock() { xSemaphoreTake(lock, portMAX_DELAY); }
        ~Lock() { xSemaphoreGive(lock); }
    };

    // A G-code action runs as a macro on the protocol task, when nothing else runs.
    Macro gcode_macro("automation");
    void  run_gcode(void* arg) {
        auto text = static_cast<std::string*>(arg);
        if (Job::active() || !state_is(State::Idle)) {
            log_warn("Automation: G-code skipped, not Idle");
        } else {
            gcode_macro.set(*text);
            gcode_macro.run(nullptr);
        }
        delete text;
    }
    const ArgEvent gcodeEvent { run_gcode };

    struct BoardEnv : public Automation::Env {
        uint32_t ms() override { return millis(); }
        bool     spindle() override {
            return gc_state.modal.spindle != SpindleState::Disable && !alarm();
        }
        bool job() override { return Job::active() || state_is(State::Cycle) || state_is(State::Hold); }
        bool moving() override { return state_is(State::Cycle) || state_is(State::Jog) || state_is(State::Homing); }
        bool idle() override { return state_is(State::Idle) && !Job::active(); }
        bool alarm() override { return state_is(State::Alarm) || state_is(State::ConfigAlarm) || state_is(State::Critical); }
        int  input(const Automation::Src& s) override {
            if (s.kind == Automation::Src::User) {
                if (!config->_userInputs || s.n >= MaxUserDigitalPin) {
                    return -1;
                }
                auto& pin = config->_userInputs->digitalInput[s.n];
                return pin.defined() ? (pin.get() ? 1 : 0) : -1;
            }
            if (s.kind == Automation::Src::Exp) {
                uint32_t now = millis();
                for (auto m : ExpanderMonitor::all()) {
                    if (m->channel() == s.ch) {
                        return m->online(now) ? ((m->inputs() >> (s.n - 1)) & 1) : -1;
                    }
                }
            }
            return -1;
        }
        bool output(int n, bool on) override {
            if (!config->_userOutputs || n >= MaxUserDigitalPin) {
                return false;
            }
            return config->_userOutputs->setDigital(n, on);
        }
        int outputState(int n) override {
            if (!config->_userOutputs || n >= MaxUserDigitalPin) {
                return -1;
            }
            return (config->_userOutputs->digitalState() >> n) & 1;
        }
        bool analog(int n, float percent) override {
            if (!config->_userOutputs || n >= MaxUserAnalogPin) {
                return false;
            }
            if (config->_userOutputs->_analogOutput[n].undefined()) {
                return Modbus::Bus::analog_out(n, percent);
            }
            return config->_userOutputs->setAnalogPercent(n, percent);
        }
        void message(const std::string& text) override { log_info("Automation: " << text); }
        void hold(const std::string& text) override {
            log_warn("Automation: " << (text.empty() ? std::string("feed hold") : text));
            protocol_send_event(&feedHoldEvent);
        }
        bool gcode(const std::string& text) override {
            if (!idle()) {
                return false;
            }
            protocol_send_event(&gcodeEvent, new std::string(text));
            return true;
        }
        bool sleep(uint32_t ms) override {
            uint32_t start = millis();
            while (millis() - start < ms) {
                protocol_execute_realtime();
                if (sys.abort()) {
                    return false;
                }
                delay_ms(1);
            }
            return true;
        }
    } env;

    // Reads automation.json.  Returns what to log; the caller logs it after
    // releasing the lock (logging from another task can wait for the polling
    // task, which may be waiting for the lock).
    std::string load(bool& error) {
        loaded = true;
        error  = false;
        std::string text;
        try {
            FileStream f(FILE_NAME, "r");
            char       buf[512];
            int        n;
            while ((n = f.read(buf, sizeof(buf))) > 0) {
                text.append(buf, n);
                if (text.size() > 32768) {
                    error = true;
                    return std::string(FILE_NAME) + " is larger than 32 KB";
                }
            }
        } catch (...) {
            engine.clear();  // no file: no automations
            return "";
        }
        std::string err;
        if (engine.load(text, err)) {
            return std::to_string(engine.size()) + " rule(s) from " + FILE_NAME;
        }
        error = true;
        return std::string(FILE_NAME) + " " + err;
    }

    void report(const std::string& msg, bool error) {
        if (msg.empty()) {
            return;
        }
        if (error) {
            log_error("Automation: " << msg);
        } else {
            log_info("Automation: " << msg);
        }
    }

    Error list(const char* value, AuthenticationLevel auth_level, Channel& out) {
        std::vector<std::string> lines;
        {
            Lock l;
            for (size_t i = 0; i < engine.size(); ++i) {
                lines.push_back(engine.line(i));
            }
        }
        for (auto& line : lines) {
            log_stream(out, line);  // LogStream adds the closing ']'
        }
        if (lines.empty()) {
            log_info_to(out, "Automation: no rules (" << FILE_NAME << ")");
        }
        return Error::Ok;
    }

    Error reload(const char* value, AuthenticationLevel auth_level, Channel& out) {
        std::string msg;
        bool        error;
        {
            Lock l;
            msg = load(error);
        }
        report(msg.empty() ? std::string("no ") + FILE_NAME : msg, error);
        return error ? Error::InvalidValue : Error::Ok;
    }

    Error run(const char* value, AuthenticationLevel auth_level, Channel& out) {
        if (!value || !*value) {
            return Error::InvalidValue;
        }
        bool ok;
        {
            Lock l;
            ok = engine.trigger(env, size_t(atoi(value)));
        }
        if (!ok) {
            log_error_to(out, "Automation: no such rule, or not one that can be run by hand");
            return Error::InvalidValue;
        }
        return Error::Ok;
    }
}

void automation_poll() {
    static uint32_t last = 0;
    uint32_t        now  = millis();
    if (lock == nullptr || now - last < 10) {
        return;
    }
    last = now;
    std::string msg;
    bool        error = false;
    {
        Lock l;
        if (!loaded) {
            msg = load(error);
        }
        engine.poll(env);  // logs only from this (the polling) task
    }
    report(msg, error);
}

bool automation_has_mcode(int code) {
    if (lock == nullptr) {
        return false;
    }
    std::string msg;
    bool        error = false, has;
    {
        Lock l;
        if (!loaded) {
            msg = load(error);
        }
        has = engine.hasMCode(code);
    }
    report(msg, error);
    return has;
}

Error automation_mcode(int code) {
    std::vector<Automation::Action> actions;
    {
        Lock l;
        actions = engine.mcodeActions(code);
    }
    // Not under the lock: the actions can wait, and the polling task keeps the
    // other rules (and the expander's input states) going meanwhile.
    Automation::Engine::runInline(env, actions);
    return Error::Ok;
}

void make_automation_commands() {
    lock = xSemaphoreCreateMutex();
    new ReportCommand("AUL", "Automation/List", list, anyState);
    new ReportCommand("AUR", "Automation/Reload", reload, anyState);
    new UserCommand("AUX", "Automation/Run", run, anyState);
}
