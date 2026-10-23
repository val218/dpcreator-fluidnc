  // ------------------------------------------------------------ boards: every terminal and the GPIO behind it
  // kind: out | in | lim (opto limit input of an axis block) ; nc/no: suffix for a normally closed / open switch
  const BOARD = {
    r3: {
      label: "dpCREATOR R3", key: "dpCREATOR R2/R3", meta: "ESP32-S3-WROOM-1-N16R8, FluidNC 4.1.1 + W5500", engine: "RMT", enable: "gpio.8", enableNote: "STEP_EN_RAW",
      blocks: { X: ["gpio.4", "gpio.5", "gpio.47"], Y: ["gpio.6", "gpio.7", "gpio.48"], Z: ["gpio.15", "gpio.16", "gpio.45"], A: ["gpio.17", "gpio.18", "gpio.38"] },
      limNC: ":low", limNO: "",
      extra: [
        { id: "PROBE", pin: "gpio.3", kind: "in", label: "PROBE terminal", nc: ":low", no: ":low" },
        { id: "PWM", pin: "gpio.21", kind: "out", label: "PWM / 0-10 V output" },
        { id: "RELAY", pin: "gpio.39", kind: "out", label: "Relay output" },
        { id: "J7TX", pin: "gpio.43", kind: "io", label: "J7 UART0 TX", nc: ":pu", no: ":pu:low", note: "UART0 console connector" },
        { id: "J7RX", pin: "gpio.44", kind: "io", label: "J7 UART0 RX", nc: ":pu", no: ":pu:low", note: "UART0 console connector" },
      ],
      pendant: { tx: "gpio.1", rx: "gpio.2", label: "PENDANT port (UART1)" },
      rs485: { tx: "gpio.42", rx: "gpio.41", rts: "gpio.40", label: "RS485 (UART2)" },
      spi: ["gpio.13", "gpio.11", "gpio.12"], sd: "gpio.10", eth: { cs: "gpio.14", int: "gpio.9", intOnBoard: true },
    },
    r2: null,
  };
  BOARD.r2 = Object.assign({}, BOARD.r3, { label: "dpCREATOR R2", extra: BOARD.r3.extra.filter((t) => t.id !== "PROBE") });
  BOARD.custom = { label: "Other ESP32 / ESP32-S3 board (your pins)" };

  // a board the customer describes: every pin is chosen by hand
  const CUSTOM_DEFAULTS = {
    esp32s3: {
      chip: "esp32s3", octal: true, name: "my_s3_board", engine: "RMT", pullups: false,
      blocks: { X: ["gpio.4", "gpio.5", "gpio.47"], Y: ["gpio.6", "gpio.7", "gpio.48"], Z: ["gpio.15", "gpio.16", "gpio.45"], A: ["gpio.17", "gpio.18", "gpio.38"] },
      enable: "gpio.8",
      extra: [{ id: "IN1", label: "Probe input", pin: "gpio.3", kind: "in" }, { id: "OUT1", label: "Spindle PWM", pin: "gpio.21", kind: "out" }, { id: "OUT2", label: "Relay", pin: "gpio.39", kind: "out" }],
      pendant: { tx: "gpio.1", rx: "gpio.2" }, rs485: { tx: "gpio.42", rx: "gpio.41", rts: "gpio.40" },
      spi: ["gpio.13", "gpio.11", "gpio.12"], sd: "gpio.10", eth: { cs: "gpio.14", int: "gpio.9" },
    },
    esp32: {
      chip: "esp32", octal: false, name: "my_esp32_board", engine: "RMT", pullups: true,
      blocks: { X: ["gpio.32", "gpio.33", "gpio.34"], Y: ["gpio.25", "gpio.26", "gpio.35"], Z: ["gpio.27", "gpio.14", "gpio.36"], A: ["gpio.4", "gpio.13", "gpio.39"] },
      enable: "gpio.21",
      extra: [{ id: "IN1", label: "Probe input", pin: "gpio.22", kind: "in" }, { id: "OUT1", label: "Spindle PWM", pin: "gpio.16", kind: "out" }, { id: "OUT2", label: "Relay", pin: "gpio.17", kind: "out" }],
      pendant: null, rs485: null,
      spi: ["gpio.19", "gpio.23", "gpio.18"], sd: "gpio.5", eth: { cs: "", int: "" },
    },
  };
  // pull-ups: ESP32 GPIO34-39 have none
  function pu(c, pin) { const n = +String(pin).split(".")[1]; return c.pullups && !(c.chip === "esp32" && n >= 34 && n <= 39) ? ":pu" : ""; }
  function customBoard(c) {
    const lim = (p) => (p ? pu(c, p) : "");
    return {
      custom: true, chip: c.chip, octal: c.octal, label: (c.chip === "esp32" ? "ESP32" : "ESP32-S3") + " board", key: c.name || "custom",
      meta: (c.chip === "esp32" ? "ESP32" : "ESP32-S3") + " board, pins set in the dpCREATOR builder", engine: c.engine || "RMT",
      enable: c.enable ? c.enable + (c.enableLow ? ":low" : "") : "NO_PIN", enableNote: "driver enable",
      blocks: c.blocks, limNC: "", limNO: ":low", limPu: lim,
      extra: c.extra.filter((t) => t.pin).map((t) => Object.assign({}, t, { nc: pu(c, t.pin), no: pu(c, t.pin) + ":low" })),
      pendant: c.pendant && c.pendant.tx && c.pendant.rx ? Object.assign({ label: "UART1 (pendant)" }, c.pendant) : null,
      rs485: c.rs485 && c.rs485.tx && c.rs485.rx ? Object.assign({ label: "UART2 (RS485)" }, c.rs485) : null,
      spi: c.spi, sd: c.sd, eth: { cs: c.eth.cs, int: c.eth.int, intOnBoard: false },
    };
  }
  function boardOf(m) { return m.board === "custom" ? customBoard(m.custom) : BOARD[m.board]; }

  // all terminals of a board as a flat list
  function terminals(b) {
    const list = [];
    for (const [k, p] of Object.entries(b.blocks)) {
      if (!p[0] || !p[1]) continue;
      const bad = b.unusable && b.unusable[k];
      list.push({ id: k + ".STEP", pin: p[0], kind: bad ? "dead" : "out", label: k + (b.custom ? " STEP" : " block STEP (pin 4)"), block: k });
      list.push({ id: k + ".DIR", pin: p[1], kind: bad ? "dead" : "out", label: k + (b.custom ? " DIR" : " block DIR (pin 6)"), block: k });
      if (p[2]) list.push({ id: k + ".LIM", pin: p[2], kind: "lim", label: k + (b.custom ? " limit input" : " block limit (pins 8/7)"), block: k });
    }
    return list.concat(b.extra);
  }

  // ------------------------------------------------------------ the machine being built
  const VFDS = ["Huanyang", "H2A", "H100", "YL620", "NowForever", "SiemensV20", "DanfossVLT2800", "DeltaMS300", "FolinnBD600", "MollomG70"];
  const SPINDLE_TYPES = [
    ["VFD", "VFD over RS485"], ["PWM", "PWM spindle"], ["10V", "0-10 V spindle"], ["Relay", "Relay on/off"], ["Laser", "Laser (PWM)"], ["PlasmaSpindle", "Plasma torch"],
  ];
  // [key, label, group, config path]
  const INPUTS = [
    ["probe", "Probe / touch plate", "Probing", "probe.pin"], ["toolsetter", "Tool setter", "Probing", "probe.toolsetter_pin"],
    ["door", "Safety door", "Safety", "control.safety_door_pin"], ["estop", "E-stop", "Safety", "control.estop_pin"], ["fault", "Driver fault / alarm", "Safety", "control.fault_pin"],
    ["reset", "Reset button", "Buttons", "control.reset_pin"], ["hold", "Feed hold button", "Buttons", "control.feed_hold_pin"], ["start", "Cycle start button", "Buttons", "control.cycle_start_pin"],
    ["homebtn", "Home button", "Buttons", "control.homing_button_pin"], ["single", "Single block switch", "Buttons", "control.single_block_pin"],
    ["macro0", "Macro 0 button", "Macro buttons", "control.macro0_pin"], ["macro1", "Macro 1 button", "Macro buttons", "control.macro1_pin"], ["macro2", "Macro 2 button", "Macro buttons", "control.macro2_pin"], ["macro3", "Macro 3 button", "Macro buttons", "control.macro3_pin"],
    ["uin0", "User input 0 (M66 P0)", "User inputs", "user_inputs.digital0_pin"], ["uin1", "User input 1 (M66 P1)", "User inputs", "user_inputs.digital1_pin"], ["uin2", "User input 2 (M66 P2)", "User inputs", "user_inputs.digital2_pin"], ["uin3", "User input 3 (M66 P3)", "User inputs", "user_inputs.digital3_pin"],
  ];
  const OUTPUTS = [
    ["flood", "Flood coolant / dust extraction (M8)", "Coolant", "coolant.flood_pin"], ["mist", "Mist coolant / air assist (M7)", "Coolant", "coolant.mist_pin"],
    ["user0", "User output 0 (M62 P0)", "User outputs", "user_outputs.digital0_pin"], ["user1", "User output 1 (M62 P1)", "User outputs", "user_outputs.digital1_pin"], ["user2", "User output 2 (M62 P2)", "User outputs", "user_outputs.digital2_pin"], ["user3", "User output 3 (M62 P3)", "User outputs", "user_outputs.digital3_pin"],
    ["analog0", "PWM output 0 (M67 E0)", "User outputs", "user_outputs.analog0_pin"],
    ["led_idle", "Idle", "Status lights", "status_outputs.idle_pin"], ["led_run", "Running", "Status lights", "status_outputs.run_pin"], ["led_hold", "Hold", "Status lights", "status_outputs.hold_pin"], ["led_alarm", "Alarm", "Status lights", "status_outputs.alarm_pin"], ["led_door", "Door open", "Status lights", "status_outputs.door_pin"],
    ["rgb", "RGB status LED (WS2812 data)", "Status lights", "rgbled.pin"],
  ];
  const EXTRA_DEFAULTS = {
    oled: { on: false, sda: "", scl: "", w: 128, h: 64, addr: "0x3c", flip: true },
    macros: { startup0: "", m0: "", m1: "", m2: "", m3: "", afterHoming: "", afterReset: "", afterUnlock: "" },
    parking: { on: false, target: -5, rate: 800, pullout: 5, pulloutRate: 250 },
    atc: { on: false, safeZ: -5, change: [0, 0, -5], ets: [0, 0, 0], etsZ: -5, seek: 200, feed: 80 },
  };
  const ax = (o) => Object.assign({ on: true, travel: 300, spm: 400, rate: 3000, accel: 200, sw: "min", motors: [{ blk: "X", lim: true, inv: false }], calc: null }, o);
  const PRESETS = {
    router: {
      label: "Router 1300 × 2500 · dual Y · VFD",
      m: {
        board: "r3", name: "router_1325", driver: "dm542", holdIdle: true, switchType: "nc", bed: false,
        axes: {
          x: ax({ travel: 1300, spm: 80, rate: 12000, accel: 600, sw: "min", motors: [{ blk: "X", lim: true }] }),
          y: ax({ travel: 2500, spm: 80, rate: 12000, accel: 500, sw: "min", motors: [{ blk: "Y", lim: true }, { blk: "A", lim: true, inv: true }] }),
          z: ax({ travel: 180, spm: 320, rate: 3000, accel: 300, sw: "max", motors: [{ blk: "Z", lim: true }] }),
          a: ax({ on: false, travel: 360, spm: 44.444, rate: 3000, sw: "none", motors: [{ blk: "A", lim: false }] }),
        },
        spindles: [{ type: "VFD", vfd: "Huanyang", modbus: 1, baud: 9600, rpm: 24000, up: 6000, down: 8000 }],
        inputs: { probe: "PROBE" }, outputs: { flood: "RELAY" },
        pendant: true, net: "eth", ethInt: true, ethHz: 8000000, sd: true, sdHz: 4000000,
      },
    },
    mixed: {
      label: "Router 600 × 900 · VFD + laser",
      m: {
        board: "r3", name: "router_6090", driver: "dm542", holdIdle: true, switchType: "nc", bed: false,
        axes: {
          x: ax({ travel: 600, spm: 320, rate: 6000, accel: 400, sw: "min", motors: [{ blk: "X", lim: true }] }),
          y: ax({ travel: 900, spm: 320, rate: 6000, accel: 400, sw: "max", motors: [{ blk: "Y", lim: true }] }),
          z: ax({ travel: 120, spm: 320, rate: 2000, accel: 200, sw: "max", motors: [{ blk: "Z", lim: true }] }),
          a: ax({ on: true, travel: 360, spm: 44.444, rate: 3600, accel: 200, sw: "none", motors: [{ blk: "A", lim: false }] }),
        },
        spindles: [{ type: "VFD", vfd: "Huanyang", modbus: 1, baud: 9600, rpm: 24000, up: 5000, down: 6000 }, { type: "Laser", out: "PWM", tools: "2", off: [62, -38, 0], hz: 5000, maxS: 1000 }],
        inputs: { probe: "PROBE", toolsetter: "A.LIM" }, outputs: { flood: "RELAY" },
        pendant: true, net: "eth", ethInt: true, ethHz: 8000000, sd: true, sdHz: 4000000,
      },
    },
    plasma: {
      label: "Plasma table 1500 × 3000 · dual Y",
      m: {
        board: "r3", name: "plasma_1530", driver: "dm542", holdIdle: true, switchType: "nc", bed: false,
        axes: {
          x: ax({ travel: 1500, spm: 80, rate: 15000, accel: 800, sw: "min", motors: [{ blk: "X", lim: true }] }),
          y: ax({ travel: 3000, spm: 80, rate: 15000, accel: 700, sw: "min", motors: [{ blk: "Y", lim: true }, { blk: "A", lim: true, inv: true }] }),
          z: ax({ travel: 100, spm: 400, rate: 3000, accel: 400, sw: "max", motors: [{ blk: "Z", lim: true }] }),
          a: ax({ on: false, travel: 360, spm: 44.444, sw: "none", motors: [{ blk: "A", lim: false }] }),
        },
        spindles: [{ type: "PlasmaSpindle", en: "RELAY", arcOk: "PROBE", arcWait: 1500 }],
        inputs: {}, outputs: {},
        pendant: true, net: "eth", ethInt: true, ethHz: 8000000, sd: true, sdHz: 4000000,
      },
    },
    laser: {
      label: "CO2 laser 1300 × 900 · door interlock",
      m: {
        board: "r3", name: "laser_1390", driver: "dm542", holdIdle: true, switchType: "nc", bed: false,
        axes: {
          x: ax({ travel: 1300, spm: 80, rate: 24000, accel: 3000, sw: "min", motors: [{ blk: "X", lim: true }] }),
          y: ax({ travel: 900, spm: 80, rate: 24000, accel: 2500, sw: "max", motors: [{ blk: "Y", lim: true }] }),
          z: ax({ travel: 200, spm: 400, rate: 1200, accel: 100, sw: "max", motors: [{ blk: "Z", lim: true }] }),
          a: ax({ on: false, travel: 360, spm: 44.444, sw: "none", motors: [{ blk: "A", lim: false }] }),
        },
        spindles: [{ type: "Laser", out: "PWM", tools: "", off: [0, 0, 0], hz: 20000, maxS: 1000 }],
        inputs: { door: "A.LIM" }, outputs: { mist: "RELAY" },
        pendant: false, net: "eth", ethInt: true, ethHz: 8000000, sd: true, sdHz: 4000000,
      },
    },
  };
  const clone = (o) => JSON.parse(JSON.stringify(o));

  // ------------------------------------------------------------ config.yaml from the machine
  function Writer() {
    const out = [];
    return {
      l(ind, key, val, com) {
        let s = " ".repeat(ind) + key + ":" + (val === undefined || val === null || val === "" ? "" : " " + val);
        if (com) s = (s.length < 31 ? s + " ".repeat(31 - s.length) : s + " ") + "# " + com;
        out.push(s);
      },
      c(ind, t) { out.push(" ".repeat(ind) + "# " + t); },
      b() { if (out.length && out[out.length - 1] !== "") out.push(""); },
      text() { return out.join("\n").replace(/\n{3,}/g, "\n\n") + "\n"; },
    };
  }
  const n3 = (v) => String(Math.round(v * 1000) / 1000);
  function lowToggle(pin) { const p = pin.split(":"); return p.includes("low") ? p.filter((x) => x !== "low").join(":") : pin + ":low"; }

  function gen(M) {
    const b = boardOf(M), T = {}, mainFw = M.fw === "main";
    terminals(b).forEach((t) => (T[t.id] = t));
    const drv = S.DRIVERS[M.driver] || S.DRIVERS.dm542;
    const w = Writer();
    const sw = M.switchType === "no" ? "no" : "nc";
    const limPin = (blk) => b.blocks[blk][2] + (b.limPu ? b.limPu(b.blocks[blk][2]) : "") + (sw === "nc" ? b.limNC : b.limNO);
    // an input terminal used for a button, probe or door: block limits follow the switch polarity, others their own
    const inPin = (id, normallyOpen) => {
      const t = T[id]; if (!t) return "NO_PIN";
      if (t.kind === "lim") return t.pin + (b.limPu ? b.limPu(t.pin) : "") + (normallyOpen ? b.limNO : sw === "nc" ? b.limNC : b.limNO);
      return t.pin + ((normallyOpen ? t.no : t.nc) || "");
    };
    const outPin = (id, low) => { const t = T[id]; if (!t) return "NO_PIN"; let p = t.pin; if (low) p = lowToggle(p); return p; };
    const usedAxes = ["x", "y", "z", "a"].filter((L) => M.axes[L].on);

    w.l(0, "board", b.key); w.l(0, "name", (M.name || "machine").replace(/\s+/g, "_")); w.l(0, "meta", b.meta + (mainFw ? ", mainstream FluidNC" : b.custom ? ", dpCREATOR FluidNC" : "")); w.b();
    w.c(0, "Built with the dpCREATOR machine builder for " + (mainFw ? "mainstream FluidNC" : "dpCREATOR FluidNC 4.1.1") + ". " + drv.label + " drivers" + (b.custom ? "." : ", common anode: PUL+/DIR+/ENA+ to +5V (pins 1/3/5), PUL- pin 4, DIR- pin 6, ENA- pin 2."));
    usedAxes.forEach((L) => {
      const a = M.axes[L];
      w.c(0, L.toUpperCase() + ": " + a.motors.map((mo, i) => "motor" + i + " on the " + mo.blk + " block" + (a.sw !== "none" && mo.lim ? " + switch" : "")).join(", ") + (a.motors.length > 1 ? " (homing squares it)" : ""));
    });
    w.b();
    w.l(0, "stepping"); w.l(2, "engine", b.engine); w.l(2, "idle_ms", M.holdIdle ? 255 : 250, M.holdIdle ? "motors stay powered (hold position)" : null);
    w.l(2, "pulse_us", drv.pulse, drv.note); w.l(2, "dir_delay_us", drv.dirDelay, drv.dirNote); w.l(2, "disable_delay_us", 0); w.b();

    const I = M.inputs || {};
    w.l(0, "probe"); w.l(2, "pin", I.probe ? inPin(I.probe, true) : "NO_PIN", I.probe ? T[I.probe].label : "no probe");
    if (I.toolsetter) w.l(2, "toolsetter_pin", inPin(I.toolsetter, true), T[I.toolsetter].label);
    w.l(2, "check_mode_start", "true"); w.b();

    w.l(0, "control");
    INPUTS.filter((x) => x[3].startsWith("control.")).forEach(([k, label, , path], i) => {
      const key = path.split(".")[1];
      const nc = k === "door" || k === "estop" || k === "fault";
      if (I[k] && T[I[k]]) w.l(2, key, inPin(I[k], !nc), T[I[k]].label + (k === "door" ? ": NC, opens with the door" : k === "estop" ? ": NC, cuts on press" : k === "fault" ? ": driver alarm, NC" : /^macro/.test(k) ? ": runs macros/" + k : ""));
      else if (i < 5 && k !== "fault" && k !== "estop") w.l(2, key, "NO_PIN");
    });
    w.b();
    const UI = INPUTS.filter((x) => x[3].startsWith("user_inputs.") && I[x[0]] && T[I[x[0]]]);
    if (UI.length) { w.l(0, "user_inputs"); UI.forEach(([k, label, , path]) => w.l(2, path.split(".")[1], inPin(I[k], true), T[I[k]].label + ": " + label)); w.b(); }

    w.l(0, "axes");
    w.l(2, "shared_stepper_disable_pin", b.enable || "NO_PIN", b.enableNote);
    const dual = usedAxes.some((L) => M.axes[L].motors.length > 1);
    if (dual) w.l(2, "homing_runs", 2);
    const fw = 1000000 / (2 * drv.pulse + drv.dirDelay), lim = Math.min(drv.hz, fw);
    for (const L of usedAxes) {
      const a = M.axes[L], homing = a.sw !== "none";
      w.b(); w.l(2, L);
      const cap = Math.floor((lim * 60) / a.spm);
      const rate = Math.min(+a.rate || cap, cap);
      w.l(4, "steps_per_mm", n3(+a.spm), L === "a" && a.rotary !== false ? "rotary: steps per degree" : null);
      w.l(4, "soft_limits", homing ? "true" : "false", homing ? "refuse moves outside the travel (needs homing)" : null);
      w.l(4, "max_rate_mm_per_min", n3(rate), rate < +a.rate ? "capped: " + drv.label + " ~" + Math.round(lim / 1000) + " kHz at " + n3(+a.spm) + " steps/mm" : null);
      w.l(4, "acceleration_mm_per_sec2", n3(+a.accel || 100));
      w.l(4, "max_travel_mm", n3(+a.travel));
      if (homing) {
        const pos = a.sw === "max";
        w.l(4, "homing");
        w.l(6, "cycle", L === "z" ? 1 : L === "a" ? 3 : 2);
        w.l(6, "mpos_mm", 0, "switch = machine 0, travel runs " + (pos ? "0 to -" : "0 to +") + n3(+a.travel));
        w.l(6, "feed_mm_per_min", rate >= 6000 ? 300 : rate >= 2000 ? 200 : 100);
        w.l(6, "seek_mm_per_min", Math.max(50, Math.round((rate * 0.35) / 50) * 50));
        w.l(6, "settle_ms", 250);
        const words = { x: ["left", "right"], y: ["front", "back"], z: ["bottom", "top"], a: ["- end", "+ end"] }[L];
        w.l(6, "positive_direction", pos ? "true" : "false", "switch" + (a.motors.filter((m) => m.lim).length > 1 ? "es" : "") + " at the " + words[pos ? 1 : 0] + (L === "y" && M.bed ? " of the tool's travel (the bed moves)" : ""));
      }
      a.motors.forEach((mo, i) => {
        w.b(); w.l(4, "motor" + i);
        w.l(6, "limit_all_pin", homing && mo.lim ? limPin(mo.blk) : "NO_PIN", homing && mo.lim ? mo.blk + " block limit (pins 8/7)" : null);
        w.l(6, "hard_limits", homing && mo.lim ? "true" : "false", homing && mo.lim ? "stop at once if a switch trips during motion" : null);
        w.l(6, "pulloff_mm", L === "z" ? 2 : 3);
        w.l(6, "standard_stepper");
        const inv = !!mo.inv !== (L === "y" && !!M.bed);
        const blk = b.blocks[mo.blk];
        w.l(8, "direction_pin", inv ? lowToggle(blk[1]) : blk[1], mo.blk + " block DIR" + (mo.inv ? ", mirrored: this motor faces the other way" : "") + (L === "y" && M.bed ? ", inverted: the bed moves" : ""));
        w.l(8, "step_pin", blk[0], mo.blk + " block STEP");
      });
    }
    w.b();

    if (M.pendant && b.pendant) {
      w.c(0, "SmartTabUI pendant on the " + b.pendant.label);
      w.l(0, "uart1"); w.l(2, "txd_pin", b.pendant.tx); w.l(2, "rxd_pin", b.pendant.rx); w.l(2, "rts_pin", "NO_PIN"); w.l(2, "cts_pin", "NO_PIN"); w.l(2, "baud", 1000000); w.l(2, "mode", "8N1"); w.b();
      w.l(0, "uart_channel1"); w.l(2, "uart_num", 1); w.l(2, "report_interval_ms", 75); w.l(2, "message_level", "Info"); w.b();
    }
    const vfd = (M.spindles || []).find((s) => s.type === "VFD");
    if (vfd && b.rs485) {
      w.c(0, "VFD on the " + b.rs485.label);
      w.l(0, "uart2"); w.l(2, "txd_pin", b.rs485.tx); w.l(2, "rxd_pin", b.rs485.rx); w.l(2, "rts_pin", b.rs485.rts || "NO_PIN", b.rs485.rts ? "RS485 DE" : null); w.l(2, "cts_pin", "NO_PIN"); w.l(2, "baud", vfd.baud || 9600); w.l(2, "mode", "8N1"); w.b();
    }
    const eth = M.net === "eth";
    if (M.sd || eth) { w.l(0, "spi"); w.l(2, "miso_pin", b.spi[0] || "NO_PIN"); w.l(2, "mosi_pin", b.spi[1] || "NO_PIN"); w.l(2, "sck_pin", b.spi[2] || "NO_PIN"); w.b(); }
    if (M.sd) { w.l(0, "sdcard"); w.l(2, "cs_pin", b.sd || "NO_PIN"); w.l(2, "card_detect_pin", "NO_PIN"); w.l(2, "frequency_hz", M.sdHz || 4000000, (M.sdHz || 4000000) < 4000000 ? "slow: long wires" : null); w.b(); }
    if (eth) {
      w.l(0, "ethernet"); w.l(2, "cs_pin", b.eth.cs || "NO_PIN");
      w.l(2, "int_pin", M.ethInt && b.eth.int ? b.eth.int : "NO_PIN", M.ethInt && b.eth.int ? (b.eth.intOnBoard ? "INT wired on the board" : "INT wire to " + b.eth.int) : "INT not wired: polled");
      w.l(2, "rst_pin", "NO_PIN"); w.l(2, "phy_type", "w5500"); w.l(2, "phy_addr", 1); w.l(2, "frequency_hz", M.ethHz || 8000000, M.sd ? "shares the SPI bus with the SD card" : null); w.b();
    } else w.c(0, "Network: Wi-Fi (set with $WiFi/... or Board tools -> Network). No W5500 section.");

    const MC = Object.assign({}, EXTRA_DEFAULTS.macros, M.macros || {});
    const q = (s) => { s = String(s || "").trim(); return !s ? "" : /[#:"']|^\s|\s$/.test(s) ? '"' + s.replace(/"/g, "'") + '"' : s; };
    w.l(0, "macros"); w.l(2, "startup_line0", q(MC.startup0)); w.l(2, "startup_line1");
    ["m0", "m1", "m2", "m3"].forEach((k, i) => w.l(2, "macro" + i, q(MC[k]), MC[k] && I["macro" + i] ? "button: " + T[I["macro" + i]].label : null));
    if (MC.afterHoming) w.l(2, "after_homing", q(MC.afterHoming));
    if (MC.afterReset) w.l(2, "after_reset", q(MC.afterReset));
    if (MC.afterUnlock) w.l(2, "after_unlock", q(MC.afterUnlock));
    w.b();
    const PK = Object.assign({}, EXTRA_DEFAULTS.parking, M.parking || {});
    if (PK.on && I.door) {
      w.c(0, "Opening the door stops the spindle and lifts Z to the parking height");
      w.l(0, "parking"); w.l(2, "enable", "true"); w.l(2, "axis", "Z"); w.l(2, "target_mpos_mm", n3(PK.target)); w.l(2, "rate_mm_per_min", n3(PK.rate)); w.l(2, "pullout_distance_mm", n3(PK.pullout)); w.l(2, "pullout_rate_mm_per_min", n3(PK.pulloutRate)); w.b();
    }
    const AT = Object.assign({}, EXTRA_DEFAULTS.atc, M.atc || {});
    const atcOn = AT.on && I.toolsetter;
    if (atcOn) {
      const v6 = (a) => [0, 1, 2, 3, 4, 5].map((i) => n3(+a[i] || 0)).join(" ");
      w.c(0, "Manual tool change: M6 moves to the change position, waits, then measures the new tool on the tool setter");
      w.l(0, "atc_manual"); w.l(2, "safe_z_mpos_mm", n3(AT.safeZ)); w.l(2, "probe_seek_rate_mm_per_min", n3(AT.seek)); w.l(2, "probe_feed_rate_mm_per_min", n3(AT.feed));
      w.l(2, "change_mpos_mm", v6(AT.change), "X Y Z where you swap the tool"); w.l(2, "ets_mpos_mm", v6(AT.ets), "X Y Z above the tool setter"); w.l(2, "ets_rapid_z_mpos_mm", n3(AT.etsZ)); w.b();
    }
    w.l(0, "start"); w.l(2, "must_home", usedAxes.filter((L) => L !== "a").every((L) => M.axes[L].sw !== "none") ? "true" : "false"); w.b();
    const O = M.outputs || {};
    w.l(0, "coolant"); w.l(2, "flood_pin", O.flood ? outPin(O.flood) : "NO_PIN", O.flood ? T[O.flood].label + " (M8)" : null); w.l(2, "mist_pin", O.mist ? outPin(O.mist) : "NO_PIN", O.mist ? T[O.mist].label + " (M7)" : null); w.l(2, "delay_ms", 0); w.b();
    const UO = OUTPUTS.filter((x) => x[3].startsWith("user_outputs.") && O[x[0]] && T[O[x[0]]]);
    if (UO.length) { w.l(0, "user_outputs"); UO.forEach(([k, label, , path]) => { w.l(2, path.split(".")[1], outPin(O[k]), T[O[k]].label + ": " + label); if (k === "analog0") w.l(2, "analog0_hz", 5000); }); w.b(); }
    const SO = OUTPUTS.filter((x) => x[3].startsWith("status_outputs.") && O[x[0]] && T[O[x[0]]]);
    if (SO.length) { w.l(0, "status_outputs"); w.l(2, "report_interval_ms", 500); SO.forEach(([k, label, , path]) => w.l(2, path.split(".")[1], outPin(O[k]), T[O[k]].label + ": lit when " + label.toLowerCase())); w.b(); }
    if (O.rgb && T[O.rgb]) { w.c(0, "WS2812 / NeoPixel status LED: its colour follows the machine state"); w.l(0, "rgbled"); w.l(2, "pin", outPin(O.rgb), T[O.rgb].label); w.l(2, "index", 0); w.b(); }
    const OL = Object.assign({}, EXTRA_DEFAULTS.oled, M.oled || {});
    if (OL.on && T[OL.sda] && T[OL.scl]) {
      w.l(0, "i2c0"); w.l(2, "sda_pin", T[OL.sda].pin, T[OL.sda].label); w.l(2, "scl_pin", T[OL.scl].pin, T[OL.scl].label); w.l(2, "frequency", 400000); w.b();
      w.l(0, "oled"); w.l(2, "i2c_num", 0); w.l(2, "i2c_address", OL.addr || "0x3c"); w.l(2, "width", OL.w); w.l(2, "height", OL.h); w.l(2, "flip", OL.flip ? "true" : "false"); w.l(2, "mirror", "false"); w.l(2, "report_interval_ms", 500); w.b();
    }

    // spindles and tools
    const sps = M.spindles || [];
    const main = sps.filter((s) => s.type !== "Laser" || sps.length === 1);
    let tn = 0;
    for (const s of sps) {
      const extra = sps.length > 1 && s !== main[0];
      const atcLine = () => { if (atcOn && s === main[0]) w.l(2, "atc", "atc_manual", "M6 runs the manual tool change"); };
      const tool = () => { atcLine(); if (extra && s.tools && mainFw) { w.l(2, "tool_num", +String(s.tools).split(/\s+/)[0], "M6 T" + String(s.tools).split(/\s+/)[0] + " switches to it"); if ((s.off || []).some((v) => +v)) w.c(2, "offset " + (s.off || []).map(n3).join(" ") + " mm: mainstream FluidNC has no offset_mm, set it in your sender or with G10/G43"); } else if (extra && s.tools) { w.l(2, "tools", String(s.tools), "M6 T" + String(s.tools).split(/\s+/)[0] + " selects it (dpCREATOR firmware)"); w.l(2, "offset_mm", (s.off || [0, 0, 0]).map(n3).join(" "), "tool point relative to the main spindle, X Y Z"); } else w.l(2, "tool_num", tn++); };
      if (s.type === "VFD") {
        w.l(0, s.vfd || "Huanyang"); w.l(2, "uart_num", 2); w.l(2, "modbus_id", s.modbus || 1); w.l(2, "spinup_ms", s.up ?? 5000); w.l(2, "spindown_ms", s.down ?? 6000); tool(); w.l(2, "speed_map", "0=0% " + (s.rpm || 24000) + "=100%");
      } else if (s.type === "PWM" || s.type === "10V") {
        w.l(0, s.type); w.l(2, "pwm_hz", s.hz || 5000); w.l(2, "output_pin", outPin(s.out, s.low), (T[s.out] || { label: "no output chosen" }).label); w.l(2, "enable_pin", s.en ? outPin(s.en) : "NO_PIN", s.en ? T[s.en].label + " switches it on" : null);
        w.l(2, "direction_pin", s.dir && T[s.dir] ? outPin(s.dir) : "NO_PIN", s.dir && T[s.dir] ? T[s.dir].label + ": M3/M4 direction" : null); if (s.type === "10V") { w.l(2, "forward_pin", "NO_PIN"); w.l(2, "reverse_pin", "NO_PIN"); }
        w.l(2, "disable_with_s0", "false"); w.l(2, "s0_with_disable", "true"); w.l(2, "spinup_ms", s.up ?? 3000); w.l(2, "spindown_ms", s.down ?? 3000); tool(); w.l(2, "speed_map", "0=0.000% " + (s.rpm || 24000) + "=100.000%");
      } else if (s.type === "Relay") {
        w.l(0, "Relay"); w.l(2, "output_pin", outPin(s.out, s.low), (T[s.out] || { label: "no output chosen" }).label); w.l(2, "direction_pin", s.dir && T[s.dir] ? outPin(s.dir) : "NO_PIN", s.dir && T[s.dir] ? T[s.dir].label + ": M3/M4 direction" : null); w.l(2, "enable_pin", "NO_PIN");
        w.l(2, "disable_with_s0", "false"); w.l(2, "s0_with_disable", "true"); w.l(2, "spinup_ms", s.up ?? 3000); w.l(2, "spindown_ms", s.down ?? 3000); tool(); w.l(2, "speed_map", "0=0.000% " + (s.rpm || 24000) + "=100.000%");
      } else if (s.type === "Laser") {
        w.l(0, "Laser"); w.l(2, "pwm_hz", s.hz || 5000); w.l(2, "output_pin", outPin(s.out, s.low), (T[s.out] || { label: "no output chosen" }).label + " - check polarity before connecting the laser");
        w.l(2, "enable_pin", s.en ? outPin(s.en) : "NO_PIN", s.en ? T[s.en].label + " enables the laser PSU" : null); w.l(2, "disable_with_s0", "false"); w.l(2, "s0_with_disable", "true"); tool(); w.l(2, "speed_map", "0=0.000% " + (s.maxS || 1000) + "=100.000%"); w.l(2, "off_on_alarm", "true");
      } else if (s.type === "PlasmaSpindle") {
        w.l(0, "PlasmaSpindle"); w.l(2, "enable_pin", outPin(s.en), (T[s.en] || { label: "no output chosen" }).label + " fires the torch"); w.l(2, "arc_ok_pin", s.arcOk ? inPin(s.arcOk, true) : "NO_PIN", s.arcOk ? T[s.arcOk].label + ": arc OK from the plasma cutter" : null);
        w.l(2, "arc_wait_ms", s.arcWait || 1000); tool(); w.l(2, "off_on_alarm", "true");
      }
      w.b();
    }
    return w.text();
  }
