/* dpCREATOR machine setup: config.yaml core (no DOM).
 *
 *   parseConfig(text)              FluidNC-style YAML -> tree with line numbers, plus syntax problems
 *   Schema(itemsDoc)               the firmware's own key list (FluidNC/docs/config_items.yaml, parsed)
 *   validate(text, schema, opts)   -> {ok, problems:[{level, line, msg}], tree}
 *   BOARDS                         controller pin tables (terminal block -> GPIO)
 *   DRIVERS                        stepper driver presets
 *   stepsPerMm(m)                  steps/rev x microstepping / mm per rev
 *   maxRateFor(stepsPerMm, ...)    fastest feed the driver and the firmware allow
 *   generate(answers)              builder answers -> commented config.yaml text
 *
 * Copyright (c) 2026 dpCREATOR (Val). All rights reserved.
 */
(function (root) {
  "use strict";

  // ---------------------------------------------------------------- parser
  // Same rules as the firmware's Tokenizer: '#' after whitespace (or at the
  // start) begins a comment unless quoted, no tabs, "key: value", children
  // are the following lines indented deeper than their parent.
  function stripComment(line) {
    let q = "";
    for (let i = 0; i < line.length; i++) {
      const c = line[i];
      if (q) { if (c === q) q = ""; continue; }
      if (c === '"' || c === "'") { q = c; continue; }
      if (c === "#" && (i === 0 || line[i - 1] === " " || line[i - 1] === "\t")) return line.slice(0, i);
    }
    return line;
  }
  function unquote(v) {
    v = v.trim();
    if (v.length >= 2 && (v[0] === '"' || v[0] === "'") && v[v.length - 1] === v[0]) return v.slice(1, -1);
    return v;
  }

  function parseConfig(text) {
    const problems = [];
    const rootNode = { key: "", line: 0, indent: -1, children: [], value: null };
    const stack = [rootNode];
    const lines = String(text).replace(/\r\n?/g, "\n").split("\n");
    let prev = null;
    lines.forEach((raw, i) => {
      const n = i + 1;
      if (/^\s*\t/.test(raw) || /^ *\t/.test(raw)) { problems.push({ level: "error", line: n, msg: "Tab character in the indentation. Use spaces only (two per level)." }); }
      const noTab = raw.replace(/\t/g, "    ");
      const body = stripComment(noTab).replace(/\s+$/, "");
      if (!body.trim()) return;
      const indent = body.length - body.replace(/^ +/, "").length;
      const t = body.slice(indent);
      let key, val;
      if (t[0] === '"' || t[0] === "'") {
        const e = t.indexOf(t[0], 1);
        if (e < 0) { problems.push({ level: "error", line: n, msg: "Missing closing quote." }); return; }
        key = t.slice(1, e);
        const rest = t.slice(e + 1).trimStart();
        if (rest[0] !== ":") { problems.push({ level: "error", line: n, msg: 'Key "' + key + '" must be followed by ":".' }); return; }
        val = rest.slice(1);
      } else {
        if (!/^[A-Za-z0-9_]/.test(t)) { problems.push({ level: "error", line: n, msg: "Invalid character at the start of the line: " + t[0] }); return; }
        const c = t.indexOf(":");
        if (c < 0) { problems.push({ level: "error", line: n, msg: 'Key "' + t + '" must be followed by ":".' }); return; }
        key = t.slice(0, c).trim();
        val = t.slice(c + 1);
      }
      val = val.trim();
      const node = { key, line: n, indent, value: val === "" ? null : unquote(val), rawValue: val, children: [] };
      while (stack.length > 1 && stack[stack.length - 1].indent >= indent) stack.pop();
      const parent = stack[stack.length - 1];
      if (parent !== rootNode && parent.value !== null) {
        problems.push({ level: "error", line: n, msg: '"' + key + '" is indented under "' + parent.key + '", which already has a value (line ' + parent.line + "). It is probably over-indented: siblings must line up." });
      }
      if (prev && indent > prev.indent && prev !== parent && parent === prev) { /* normal child */ }
      if (indent % 2) problems.push({ level: "warn", line: n, msg: "Indented by " + indent + " spaces. Use two spaces per level." });
      if (parent !== rootNode && parent.children.length && parent.children[0].indent !== indent) {
        problems.push({ level: "error", line: n, msg: '"' + key + '" is indented ' + indent + " spaces but its siblings under \"" + parent.key + '" use ' + parent.children[0].indent + ". Siblings must line up." });
      }
      if (parent.children.some((c) => c.key === key)) problems.push({ level: "error", line: n, msg: '"' + key + '" appears twice under "' + (parent.key || "the top level") + '".' });
      node.parent = parent;
      parent.children.push(node);
      stack.push(node);
      prev = node;
    });
    return { root: rootNode, problems };
  }

  function nodePath(node) {
    const p = [];
    for (let x = node; x && x.key; x = x.parent) p.unshift(x.key);
    return p;
  }
  function child(node, key) { return node && node.children.find((c) => c.key === key); }
  function get(node, path) {
    let x = node;
    for (const k of path.split(".")) { x = child(x, k); if (!x) return undefined; }
    return x;
  }

  // ---------------------------------------------------------------- schema
  // itemsDoc = config_items.yaml as a plain object (parsed with js-yaml).
  function Schema(doc) {
    const meta = doc.section_meta || {};
    const spindles = new Set(doc.spindle_sections || []);
    const vfdNamed = new Set(doc.vfd_named_types || []);
    const vfdProto = new Set(doc.vfd_protocol_fields || []);
    const sections = {};   // template path -> {fields}
    const skip = new Set(["enum_types", "spindle_sections", "vfd_protocol_fields", "vfd_named_types", "pin_namespaces", "section_meta"]);
    for (const k of Object.keys(doc)) {
      if (skip.has(k)) continue;
      const name = k === "(top-level machine items)" ? "" : k;
      sections[name] = doc[k] || {};
    }
    // named VFDs: ModbusVFD's fields minus the raw protocol ones
    if (sections.ModbusVFD) {
      const f = {};
      for (const [k, v] of Object.entries(sections.ModbusVFD)) if (!vfdProto.has(k)) f[k] = v;
      for (const n of vfdNamed) if (!sections[n]) sections[n] = f;
    }
    // placeholder segments and their regexes
    const ph = {};
    for (const [tpl, m] of Object.entries(meta)) {
      const segs = tpl.split(".");
      const last = segs[segs.length - 1];
      if (m.key_pattern) ph[last] = new RegExp("^(?:" + m.key_pattern + ")$");
      if (m.child_types) ph[last] = new RegExp("^(?:" + m.child_types.join("|") + ")$");
    }
    const templates = Object.keys(sections).filter((t) => t !== "").map((t) => t.split("."));

    function segMatches(seg, key) {
      if (ph[seg]) return ph[seg].test(key);
      return seg === key;
    }
    // The template for a section path (array of keys), or null.
    function sectionFor(path) {
      const t = templates.find((tp) => tp.length === path.length && tp.every((s, i) => segMatches(s, path[i])));
      return t ? t.join(".") : null;
    }
    // Is path a prefix of some template (a container such as "kinematics" or "axes.x")?
    function isContainer(path) {
      return path.length === 0 || templates.some((tp) => tp.length > path.length && path.every((k, i) => segMatches(tp[i], k)));
    }
    function fields(tpl) { return sections[tpl] || {}; }
    return {
      sectionFor, isContainer, fields, spindles, vfdNamed,
      isSpindle: (k) => spindles.has(k) || vfdNamed.has(k) || k === "ModbusVFD",
      pinNamespaces: Object.values(doc.pin_namespaces || {}).map((p) => new RegExp("^" + p.pattern + "$")),
      raw: doc,
    };
  }

  // ---------------------------------------------------------------- pins
  const CHIPS = {
    esp32s3: {
      label: "ESP32-S3",
      valid: (n) => (n >= 0 && n <= 21) || (n >= 26 && n <= 48),
      rules(n, octal) {
        if (n === 19 || n === 20) return { level: "error", msg: "IO" + n + " is the ESP32-S3's native USB" + (n === 19 ? " D-" : " D+") + ". It can't be used as a machine pin while USB is in use." };
        if (n >= 26 && n <= 32) return { level: "error", msg: "IO" + n + " is used by the module's flash chip." };
        if (octal && n >= 35 && n <= 37) return { level: "error", msg: "IO" + n + " is taken by the octal PSRAM on N8R8/N16R8/N32R8V modules (the firmware needs that PSRAM)." };
        if (n === 0 || n === 3 || n === 45 || n === 46) return { level: "info", msg: "IO" + n + " is a strapping pin, read at power-up. Fine if the board was designed for it; otherwise it can stop the board from booting." };
        return null;
      },
    },
    esp32: {
      label: "ESP32",
      valid: (n) => (n >= 0 && n <= 39) && !(n >= 24 && n <= 24) && n !== 20 && !(n >= 28 && n <= 31),
      rules(n, octal, dir) {
        if (n >= 6 && n <= 11) return { level: "error", msg: "IO" + n + " is used by the module's flash chip." };
        if (n >= 34 && n <= 39 && dir === "output") return { level: "error", msg: "IO" + n + " is input only on the ESP32." };
        if (n >= 34 && n <= 39 && /:p[ud]/.test(dir || "")) return { level: "error", msg: "IO" + n + " has no internal pull-up/down on the ESP32." };
        if (n === 12) return { level: "warn", msg: "IO12 is a strapping pin (flash voltage). Pulled high at power-up, the board won't boot." };
        if (n === 0 || n === 2 || n === 5 || n === 15) return { level: "info", msg: "IO" + n + " is a strapping pin. Fine if the board was designed for it." };
        return null;
      },
    },
  };

  function parsePin(v) {
    const s = String(v == null ? "" : v).trim();
    if (!s || /^no_pin$/i.test(s) || /^void$/i.test(s)) return { none: true, text: s };
    const parts = s.split(":");
    const head = parts[0].toLowerCase();
    const attrs = parts.slice(1).map((a) => a.toLowerCase());
    let m = /^gpio\.(\d+)$/.exec(head);
    if (m) return { gpio: +m[1], attrs, text: s };
    return { other: head, attrs, text: s };
  }

  // ---------------------------------------------------------------- validator
  const SPI_BUS = new Set(["spi.miso_pin", "spi.mosi_pin", "spi.sck_pin"]);

  function checkValue(f, v, where, add) {
    const t = f.type || "";
    const s = v == null ? "" : String(v).trim();
    if (t === "integer") {
      if (!/^[-+]?\d+$/.test(s)) return add("error", where + " must be a whole number (got \"" + s + "\").");
      const n = +s;
      if (f.min != null && n < f.min) add("error", where + " is " + n + ", below the minimum " + f.min + ".");
      if (f.max != null && n > f.max) add("error", where + " is " + n + ", above the maximum " + f.max + ".");
    } else if (t === "float") {
      if (!/^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$/.test(s)) return add("error", where + " must be a number (got \"" + s + "\").");
      const n = +s;
      if (f.min != null && n < f.min) add("error", where + " is " + n + ", below the minimum " + f.min + ".");
      if (f.max != null && n > f.max) add("error", where + " is " + n + ", above the maximum " + f.max + ".");
    } else if (t === "boolean") {
      if (!/^(true|false)$/i.test(s)) add("error", where + " must be true or false (got \"" + s + "\").");
    } else if (t === "enum") {
      const vals = (f.values || []).map(String);
      if (vals.length && !vals.some((x) => x.toLowerCase() === s.toLowerCase())) add("error", where + " must be one of " + vals.join(", ") + ' (got "' + s + '").');
    } else if (t === "axis") {
      if (!/^[xyzabcuvw]$/i.test(s)) add("error", where + " must be an axis letter (got \"" + s + "\").");
    } else if (t === "uart_mode") {
      if (!/^[5-8][NEO](1|1\.5|2)$/i.test(s)) add("error", where + ' must look like 8N1 (got "' + s + '").');
    } else if (t === "std::vector<float>") {
      if (s && !s.split(/\s+/).every((x) => /^[-+]?(\d+\.?\d*|\.\d+)$/.test(x))) add("error", where + " must be numbers separated by spaces (got \"" + s + "\").");
    } else if (/speedEntry/.test(t)) {
      if (s && !s.split(/\s+/).every((x) => /^\d+(\.\d+)?=\d+(\.\d+)?%$/.test(x))) add("error", where + ' must look like "0=0% 24000=100%" (got "' + s + '").');
    } else if (t === "string" && f.max != null && s.length > f.max) {
      add("error", where + " is longer than " + f.max + " characters.");
    }
  }

  function validate(text, schema, opts) {
    opts = opts || {};
    const chip = CHIPS[opts.chip || "esp32s3"];
    const octal = opts.octal !== false;
    const parsed = parseConfig(text);
    const problems = parsed.problems.slice();
    const add = (level, line, msg) => problems.push({ level, line, msg });
    const pins = [];        // {gpio, where, line, dir, node}
    const leafs = {};       // "a.b.c" -> node

    function walk(node) {
      const path = nodePath(node);
      if (node !== parsed.root && node.value === null && node.children.length === 0) {
        // empty key: fine for an empty section (e.g. "Cartesian:", "NoSpindle:") or an empty value
      }
      for (const c of node.children) {
        const cpath = path.concat(c.key);
        const where = cpath.join("/");
        const tplHere = path.length === 0 ? "" : schema.sectionFor(path);
        const fieldsHere = tplHere !== null ? schema.fields(tplHere) : null;
        const f = fieldsHere && fieldsHere[c.key];
        const sub = schema.sectionFor(cpath);
        const container = schema.isContainer(cpath);
        if (f && !c.children.length) {
          leafs[cpath.join(".")] = c;
          if (c.value === null) {
            if (f.type !== "macro" && f.type !== "string" && f.type !== "pin") add("warn", c.line, where + " has no value. Remove the line or set a value.");
          } else if (f.type === "pin") {
            const p = parsePin(c.value);
            if (!p.none) {
              if (p.gpio !== undefined) {
                if (!chip.valid(p.gpio)) add("error", c.line, where + ": gpio." + p.gpio + " does not exist on the " + chip.label + ".");
                pins.push({ gpio: p.gpio, where, key: cpath.join("."), line: c.line, dir: f.pin_attributes || "", attrs: p.attrs });
                const bad = p.attrs.filter((a) => !/^(pu|pd|low|high)$/.test(a) && !/^ds[0-3]$/.test(a));
                if (bad.length) add("error", c.line, where + ": unknown pin attribute :" + bad.join(":") + " (allowed :pu, :pd, :low, :high).");
              } else if (!schema.pinNamespaces.some((re) => re.test(p.other))) {
                add("error", c.line, where + ': "' + c.value + '" is not a pin (use gpio.N, NO_PIN, or a pin of a configured extender/UART channel).');
              }
            }
          } else {
            checkValue(f, c.value, where, (lvl, msg) => add(lvl, c.line, msg));
          }
          continue;
        }
        if (f && c.children.length && f.type !== "pin") {
          add("error", c.line, where + " is a value, not a section. The lines under it (from line " + c.children[0].line + ") will be ignored.");
          continue;
        }
        if (sub || container) {
          if (c.value !== null) add("error", c.line, where + " is a section; it should have no value after the colon.");
          walk(c);
          continue;
        }
        // unknown
        const hint = path.length === 0 ? "" : " (not a setting of " + path.join("/") + ")";
        add("error", c.line, 'Unknown key "' + c.key + '"' + hint + ". The firmware reports [MSG:ERR: Ignored key " + c.key + "] and marks the whole config invalid.");
      }
    }
    walk(parsed.root);

    // pins used twice
    const byGpio = new Map();
    for (const p of pins) {
      if (!byGpio.has(p.gpio)) byGpio.set(p.gpio, []);
      byGpio.get(p.gpio).push(p);
      const r = chip.rules(p.gpio, octal, p.dir === "output" || p.dir === "pwm" ? "output" : p.attrs.join(":"));
      if (r && !(opts.boardPins && opts.boardPins.has(p.gpio) && r.level !== "error")) add(r.level, p.line, p.where + ": " + r.msg);
    }
    for (const [g, list] of byGpio) {
      const real = list.filter((p) => !SPI_BUS.has(p.key));
      if (list.length > 1 && !(list.every((p) => SPI_BUS.has(p.key)))) {
        const names = list.map((p) => p.where + " (line " + p.line + ")").join(", ");
        const spindleOut = list.filter((p) => schema.isSpindle(p.key.split(".")[0]) && /output_pin$/.test(p.key));
        const msg = spindleOut.length > 1
          ? "gpio." + g + " drives two spindles: " + names + ". Two spindles can't share an output pin."
          : "gpio." + g + " is used more than once: " + names + ".";
        add("error", list[1].line, msg);
      }
      void real;
    }

    // steps: firmware and driver step-rate limit
    const root2 = parsed.root;
    const pulse = num(get(root2, "stepping.pulse_us"), 4);
    const dirDelay = num(get(root2, "stepping.dir_delay_us"), 0);
    const fwLimit = 1000000 / (2 * pulse + dirDelay);
    const drvLimit = opts.driverHz || null;
    const axes = child(root2, "axes");
    const homed = [];
    if (axes) {
      for (const ax of axes.children) {
        if (!/^[xyzabcuvw]$/.test(ax.key)) continue;
        const spm = num(child(ax, "steps_per_mm"), null);
        const rate = num(child(ax, "max_rate_mm_per_min"), null);
        if (spm && rate) {
          const hz = (spm * rate) / 60;
          const lim = drvLimit ? Math.min(drvLimit, fwLimit) : fwLimit;
          const who = drvLimit && drvLimit < fwLimit ? "the driver (about " + Math.round(drvLimit / 1000) + " kHz)" : "the firmware at pulse_us " + pulse + " / dir_delay_us " + dirDelay + " (about " + Math.round(fwLimit / 1000) + " kHz)";
          if (hz > lim * 1.001) {
            const line = child(ax, "max_rate_mm_per_min").line;
            add("error", line, "axes/" + ax.key + ": " + rate + " mm/min at " + spm + " steps/mm needs " + Math.round(hz / 100) / 10 + " kHz, more than " + who + " can do. Highest: " + Math.floor((lim * 60) / spm) + " mm/min. Lower the microstepping to go faster.");
          } else if (!drvLimit && hz > 20000) {
            add("info", child(ax, "max_rate_mm_per_min").line, "axes/" + ax.key + ": " + Math.round(hz / 100) / 10 + " kHz step rate. Fine for DM542-class drivers; a TB6600 tops out near 20 kHz (" + Math.floor((20000 * 60) / spm) + " mm/min here).");
          }
        }
        const homing = child(ax, "homing");
        const cycle = homing ? num(child(homing, "cycle"), 0) : 0;
        const soft = child(ax, "soft_limits");
        const hasSwitch = ax.children.some((m) => /^motor[01]$/.test(m.key) && ["limit_all_pin", "limit_neg_pin", "limit_pos_pin"].some((k) => { const n = child(m, k); return n && !parsePin(n.value).none; }));
        if (soft && /^true$/i.test(soft.value || "") && !(homing && cycle > 0)) add("error", soft.line, "axes/" + ax.key + ": soft_limits needs homing (a homing: section with cycle 1 or more).");
        if (homing && cycle > 0 && !hasSwitch) add("error", child(homing, "cycle").line, "axes/" + ax.key + ": homing is on but the axis has no limit switch pin.");
        if (homing && cycle > 0) homed.push(ax.key);
        for (const m of ax.children) {
          if (!/^motor[01]$/.test(m.key)) continue;
          const all = child(m, "limit_all_pin");
          if (all && !parsePin(all.value).none && ["limit_neg_pin", "limit_pos_pin"].some((k) => { const n = child(m, k); return n && !parsePin(n.value).none; })) add("error", all.line, "axes/" + ax.key + "/" + m.key + ": limit_all_pin can't be used together with limit_neg_pin / limit_pos_pin.");
          const hl = child(m, "hard_limits");
          if (hasSwitch && (!hl || !/^true$/i.test(hl.value || ""))) add("warn", (hl || m).line, "axes/" + ax.key + "/" + m.key + ": hard_limits is off, so the switches are only used for homing and a job can run through them.");
        }
      }
    } else {
      add("warn", 0, "No axes: section. The machine will not move.");
    }
    const mh = get(root2, "start.must_home");
    if (mh && /^true$/i.test(mh.value || "") && !homed.length) add("error", mh.line, "start/must_home is true but no axis has homing configured: the machine would stay locked.");

    // spindles
    const sp = root2.children.filter((c) => schema.isSpindle(c.key));
    const toolsSeen = new Map();
    let hasLaser = false, pwmDir = false;
    for (const s of sp) {
      if (s.key === "Laser") hasLaser = true;
      if (s.key === "PWM") { const d = child(s, "direction_pin"); if (d && !parsePin(d.value).none) pwmDir = true; }
      const tl = child(s, "tools");
      if (tl && tl.value) {
        for (const t of String(tl.value).split(/\s+/)) {
          if (!/^\d+$/.test(t)) { add("error", tl.line, s.key + '/tools: "' + t + '" is not a tool number.'); continue; }
          if (toolsSeen.has(t)) add("error", tl.line, s.key + "/tools: T" + t + " is already used by " + toolsSeen.get(t) + ".");
          toolsSeen.set(t, s.key);
        }
      }
      const off = child(s, "offset_mm");
      if (off && off.value && String(off.value).trim().split(/\s+/).length !== 3) add("error", off.line, s.key + "/offset_mm needs three numbers: X Y Z.");
    }
    if (sp.length > 1) {
      const nums = sp.filter((s) => !(child(s, "tools") && child(s, "tools").value)).map((s) => num(child(s, "tool_num"), 0));
      if (new Set(nums).size !== nums.length) add("warn", sp[1].line, "Several spindles share the same tool_num. Give each a distinct tool_num, or select the extra ones with tools: (dpCREATOR firmware).");
    }
    if (sp.length && !hasLaser && !pwmDir) add("info", sp[0].line, "M4 (laser mode, power follows speed) needs a Laser: spindle or a PWM: spindle with a direction_pin.");

    // network / bus
    const eth = child(root2, "ethernet");
    if (eth) {
      const ip = child(eth, "int_pin");
      if (ip && !parsePin(ip.value).none) add("info", ip.line, "ethernet/int_pin " + ip.value + ": only if the W5500 INT wire is really connected there. A wrong INT pin brings the link up but no packets pass. Unsure: NO_PIN.");
    }
    if ((eth || child(root2, "sdcard")) && !child(root2, "spi")) add("error", (eth || child(root2, "sdcard")).line, "The SD card and the W5500 need an spi: section with miso/mosi/sck pins.");
    const door = get(root2, "control.safety_door_pin");
    if (door && !parsePin(door.value).none) add("info", door.line, "control/safety_door_pin is set. Without a door switch connected the input reads \"open\": error:13 Safety door open and ALARM:11 at every start. No door: NO_PIN.");

    problems.sort((a, b) => (a.line || 0) - (b.line || 0));
    const errors = problems.filter((p) => p.level === "error").length;
    return { ok: errors === 0, errors, warnings: problems.filter((p) => p.level === "warn").length, problems, tree: parsed.root };
  }
  function num(node, dflt) {
    if (!node || node.value == null || node.value === "") return dflt;
    const n = parseFloat(node.value);
    return isNaN(n) ? dflt : n;
  }

  // ---------------------------------------------------------------- boards
  // Terminal blocks are the same 8-pin layout on every axis: 1 +5V, 2 ENA (shared),
  // 3 +5V, 4 STEP, 5 +5V, 6 DIR, 7 isolated GND, 8 limit input.
  const BOARDS = {
    r3: {
      id: "r3", label: "dpCREATOR R3", short: "R3", chip: "esp32s3", octal: true,
      boardKey: "dpCREATOR R2/R3", namePrefix: "dpcreator_r3", meta: "ESP32-S3-WROOM-1-N16R8, FluidNC 4.1.1 + W5500",
      header: ["UART0 (J7, GPIO43/44) is the FluidNC console, fixed in firmware. USB works too."],
      engine: "RMT", pulseMin: 2,
      blocks: {
        X: { step: "gpio.4", dir: "gpio.5", limit: "gpio.47" },
        Y: { step: "gpio.6", dir: "gpio.7", limit: "gpio.48" },
        Z: { step: "gpio.15", dir: "gpio.16", limit: "gpio.45" },
        A: { step: "gpio.17", dir: "gpio.18", limit: "gpio.38" },
      },
      limitNC: ":low", limitNO: "",
      enable: "gpio.8", enableNote: "STEP_EN_RAW - add :low if drivers enable on low",
      probe: { pin: "gpio.3:low", where: "PROBE terminal" },
      controls: [
        { key: "safety_door_pin", pin: null }, { key: "reset_pin", pin: null }, { key: "feed_hold_pin", pin: null }, { key: "cycle_start_pin", pin: null },
      ],
      outputs: { pwm: { pin: "gpio.21", label: "PWM / 0-10 V output" }, relay: { pin: "gpio.39", label: "relay" } },
      spi: { miso: "gpio.13", mosi: "gpio.11", sck: "gpio.12" },
      sd: { cs: "gpio.10" },
      eth: { cs: "gpio.14", int: "gpio.9", rst: null, intWired: true },
      pendant: { tx: "gpio.1", rx: "gpio.2" },
      rs485: { tx: "gpio.42", rx: "gpio.41", rts: "gpio.40" },
    },
    r2: null,   // filled below (R3 with the probe shared with the Z limit)
    legacy: {
      id: "legacy", label: "Legacy v4.0.2 / XYZ50 / XYZ77 with ESP32-S3 swap", short: "v4.0.2 + S3", chip: "esp32s3", octal: true,
      boardKey: "3dplv_v4_XYZ50_S3", namePrefix: "3dplv_v4_S3",
      meta: "by 3dp.lv LTD UK 2024 - v4.0.2 with ESP32-S3-WROOM-1-N16R8 module swap + external W5500",
      header: [
        "Pin remap: WROOM-32 PCB pad -> ESP32-S3 GPIO on the same pad.",
        "PCB pads 26/27/28 cut (N16R8 octal PSRAM sits on IO35-37).",
        "Bodge wires:",
        "  reset  (old IO4,  pad 26 net) -> S3 IO10 (PCB pad 17, was SD2)",
        "  probe  (old IO16, pad 27 net) -> S3 IO11 (PCB pad 18, was SD3)",
        "W5500 (shares the SD card SPI bus):",
        "  W5500 GND/MISO/SCK/MOSI/3V3 -> J4 pins 1/2/3/4/6 (micro-SD header)",
        "  MISO: IO40 is dead -> wire J4 pin 2 to S3 castellation PCB pad 22 (IO21)",
        "  SD module MISO goes through a 2k series resistor (card otherwise holds MISO)",
        "  CS  -> S3 IO12 (PCB pad 19, was CMD)",
        "  INT -> not connected (int_pin: NO_PIN, polling)",
        "Spindle relay K1: driven by ULN U3 pin 7 (in7 -> out12 -> K1 coil), was module pad 28 (old IO17, cut).",
        "  Bodge: U3 pin 7 -> S3 castellation PCB pad 20 (IO13). Relay contacts on the 2-pin terminal J2 (COM / NO).",
        "Laser on the board's PWM output: S3 IO48 (pad 24, old IO2) -> ULN U3 in5/in6 ->",
        "  J5PWR1 pin 1 + PWM header pins 1-2 (non-inverted, open collector) and PWM header pin 3 (inverted).",
        "  :low as in the original XYZ77 config - check polarity with a meter before connecting the laser.",
      ],
      engine: "TIMED", pulseMin: 2,
      blocks: {
        X: { step: "gpio.16", dir: "gpio.17", limit: "gpio.4", was: { step: "gpio.33", dir: "gpio.25", limit: "gpio.36" } },
        Y: { step: "gpio.18", dir: "gpio.8", limit: "gpio.5" },
        Z: { step: "gpio.19", dir: "gpio.20", limit: "gpio.6", unusable: "The Z block's STEP/DIR sit on IO19/IO20, the S3's USB pins. Use the A block for Z." },
        A: { step: "gpio.9", dir: "gpio.15", limit: "gpio.7" },
      },
      limitNC: "", limitNO: ":low",
      enable: "gpio.47:pu", enableNote: "was gpio.15",
      probe: { pin: "gpio.11:pu:low", where: "J5PWR1 (bodge to IO11)", note: "was gpio.16 (bodge)" },
      controls: [
        { key: "safety_door_pin", pin: "gpio.42:pu", note: "closed door = input to GND" },
        { key: "reset_pin", pin: "gpio.10:pu:low", note: "was gpio.4 (bodge)" },
        { key: "feed_hold_pin", pin: null },
      ],
      outputs: {
        pwm: { pin: "gpio.48", label: "PWM output (J5PWR1 pin 1)", lowDefault: true, note: "board PWM output (old gpio.2:low)" },
        relay: { pin: "gpio.13", label: "relay K1 (J2)", note: "relay K1 via ULN U3 (bodge U3 pin 7 -> pad 20)" },
        ttl: { pin: "gpio.2", label: "laser TTL (J5PWR1 pin 5)" },
      },
      spi: { miso: "gpio.21", mosi: "gpio.1", sck: "gpio.39", notes: { miso: "was gpio.19 - moved off IO40: wire J4 pin 2 -> S3 castellation PCB pad 22 (IO21)", mosi: "was gpio.23", sck: "was gpio.18" } },
      sd: { cs: "gpio.38", note: "was gpio.5" },
      eth: { cs: "gpio.12", int: null, intLater: "gpio.41", rst: null, intWired: false, note: "PCB pad 19" },
      pendant: null,
      rs485: null,
      free: ["gpio.14", "gpio.41"],
    },
    generic: {
      id: "generic", label: "Other ESP32 / FluidNC board", short: "generic", chip: "esp32s3", octal: true,
      boardKey: "generic", namePrefix: "fluidnc", meta: "", header: [], engine: "RMT", pulseMin: 2,
      blocks: { X: {}, Y: {}, Z: {}, A: {} }, limitNC: "", limitNO: ":low", enable: "", probe: { pin: "" },
      controls: [{ key: "safety_door_pin", pin: "" }, { key: "reset_pin", pin: "" }, { key: "feed_hold_pin", pin: "" }, { key: "cycle_start_pin", pin: "" }],
      outputs: { pwm: { pin: "", label: "PWM output" }, relay: { pin: "", label: "relay output" } },
      spi: { miso: "", mosi: "", sck: "" }, sd: { cs: "" }, eth: { cs: "", int: null, rst: null }, pendant: null, rs485: null, editable: true,
    },
  };
  BOARDS.r2 = Object.assign({}, BOARDS.r3, {
    id: "r2", label: "dpCREATOR R2", short: "R2", namePrefix: "dpcreator_r2",
    probe: { pin: "gpio.45:low", sharedWithZ: true, where: "LIM_Z_PROBE (shared with the Z limit)" },
  });

  const DRIVERS = {
    tb6600: { label: "TB6600", pulse: 5, dirDelay: 5, hz: 20000, note: "TB6600 needs >2.5 us pulses", dirNote: "TB6600 needs ~5 us DIR setup" },
    dm542: { label: "DM542 / DM556", pulse: 3, dirDelay: 5, hz: 200000, note: "DM542 needs >=2.5 us pulses", dirNote: "DIR setup time" },
    opto: { label: "Other opto-isolated driver", pulse: 5, dirDelay: 5, hz: 20000, note: "safe value for opto inputs", dirNote: "DIR setup time" },
    stepstick: { label: "Onboard / StepStick (A4988, DRV8825)", pulse: 2, dirDelay: 1, hz: 100000, note: "StepStick drivers accept short pulses", dirNote: "DIR setup time" },
  };

  function stepsPerMm(m) {
    const steps = +m.motorSteps || 200, micro = +m.micro || 1;
    let perRev;
    if (m.drive === "belt") perRev = (+m.teeth || 20) * (+m.pitch || 2);
    else if (m.drive === "rack") perRev = Math.PI * (+m.pinionDia || 20);
    else if (m.drive === "rotary") return (steps * micro * (+m.ratio || 1)) / 360;
    else perRev = +m.lead || 8;
    return (steps * micro) / perRev;
  }
  function fwStepHz(pulse, dirDelay) { return 1000000 / (2 * pulse + dirDelay); }
  function maxRateFor(spm, driverHz, pulse, dirDelay) {
    const lim = Math.min(driverHz || Infinity, fwStepHz(pulse, dirDelay));
    return Math.floor((lim * 60) / spm);
  }

  // ---------------------------------------------------------------- generator
  function fmt(n) {
    if (typeof n !== "number") return String(n);
    return Number.isInteger(n) ? String(n) : String(+n.toFixed(3));
  }
  function Writer() {
    const out = [];
    const COL = 31;
    function line(indent, key, value, comment) {
      let s = " ".repeat(indent) + key + ":" + (value === undefined || value === null || value === "" ? "" : " " + value);
      if (comment) s = (s.length < COL ? s + " ".repeat(COL - s.length) : s + " ") + "# " + comment;
      out.push(s);
    }
    return {
      line,
      comment(indent, text) { out.push(" ".repeat(indent) + "# " + text); },
      blank() { if (out.length && out[out.length - 1] !== "") out.push(""); },
      raw(s) { out.push(s); },
      text() { return out.join("\n").replace(/\n{3,}/g, "\n\n") + "\n"; },
    };
  }

  function pinWith(pin, suffix) {
    if (!pin) return "NO_PIN";
    if (!suffix) return pin;
    // toggle :low
    const parts = pin.split(":");
    const has = parts.includes("low");
    if (suffix === ":low") return has ? parts.filter((p) => p !== "low").join(":") : pin + ":low";
    return pin + suffix;
  }

  /* answers = {
   *   board: "legacy" | "r2" | "r3" | "generic", custom: {X:{step,dir,limit},..., enable, probe, pwm, relay, ...} (generic only),
   *   machineName, type: "router" | "laser" | "router+laser" | "plotter" | "plasma",
   *   driver: "tb6600" | "dm542" | "opto" | "stepstick", holdIdle: bool,
   *   switchType: "nc" | "no",
   *   axes: [{ axis: "x", block: "X", limitBlock: "X", moves: "tool" | "bed", flipDir: bool,
   *            switchAt: "min" | "max" | "none", travel: 300, spm: 1600 (or calc: {...}),
   *            wantRate: 3000, accel: 100 }],
   *   probe: bool, controls: { safety_door_pin: bool, reset_pin: bool, feed_hold_pin: bool, cycle_start_pin: bool },
   *   spindles: [{ kind: "relay" | "pwm" | "10v" | "vfd" | "laser", output: "relay" | "pwm" | "ttl",
   *                low: bool, spinup: 3000, spindown: 3000, maxRpm: 24000, maxS: 255, tools: "2", offset: [x,y,z],
   *                vfd: "Huanyang", modbusId: 1, pwmHz: 5000 }],
   *   pendant: bool, network: "ethernet" | "wifi", sd: bool, sdHz: 4000000, mustHome: bool
   * }
   */
  function generate(a) {
    const b = BOARDS[a.board] || BOARDS.r3;
    const custom = a.custom || {};
    const B = (blk) => Object.assign({}, b.blocks[blk] || {}, (custom.blocks || {})[blk] || {});
    const drv = DRIVERS[a.driver] || DRIVERS.tb6600;
    const w = Writer();
    const legacy = b.id === "legacy";

    w.line(0, "board", b.boardKey);
    w.line(0, "name", a.name || [b.namePrefix, a.machineName, drv.label.split(" ")[0]].filter(Boolean).join("_").replace(/\s+/g, "_"));
    if (b.meta) w.line(0, "meta", b.meta);
    w.blank();
    if (b.header && b.header.length) {
      b.header.forEach((h) => w.comment(0, h));
    }
    // wiring summary
    w.comment(0, a.machineName ? a.machineName + " with " + drv.label + " drivers, common-anode wiring: PUL+/DIR+/ENA+ -> terminal +5V (pins 1/3/5)," : drv.label + " drivers, common-anode wiring: PUL+/DIR+/ENA+ -> terminal +5V (pins 1/3/5),");
    w.comment(0, "  PUL- -> pin 4, DIR- -> pin 6, ENA- -> pin 2 (or leave ENA unconnected = always enabled).");
    const laser = (a.spindles || []).find((s) => s.kind === "laser");
    const router = (a.spindles || []).find((s) => s.kind !== "laser");
    if (laser && router && laser.tools) w.comment(0, "Tools: M6 T" + laser.tools.split(/\s+/)[0] + " = laser, any other tool (M6 T1) = spindle on the " + (router.kind === "relay" ? "relay" : router.kind === "vfd" ? "VFD" : "PWM output") + ".");
    const wiring = (a.axes || []).map((x) => {
      const L = x.axis.toUpperCase();
      const lim = x.switchAt === "none" ? "" : (x.limitBlock === x.block ? " + limit" : " (limit on the " + x.limitBlock + " block)");
      return L + " driver" + lim + " on the " + x.block + " block";
    });
    if (wiring.length) {
      w.comment(0, (a.machineName || "Machine") + " wiring: " + wiring.join("; ") + ".");
      const bad = Object.entries(b.blocks).filter(([, v]) => v.unusable).map(([k]) => k);
      if (bad.length) w.comment(0, "  " + b.blocks[bad[0]].unusable);
      w.comment(0, "  Each block: pin 4 = STEP, pin 6 = DIR, pin 2 = enable (shared), pins 8/7 = limit switch (" + (a.switchType === "no" ? "NO" : "NC") + ", isolated).");
    }
    w.blank();

    // stepping
    w.line(0, "stepping");
    w.line(2, "engine", b.engine);
    w.line(2, "idle_ms", a.holdIdle ? 255 : 250, a.holdIdle ? "255 = motors stay powered (hold position)" : null);
    w.line(2, "pulse_us", drv.pulse, drv.note);
    w.line(2, "dir_delay_us", drv.dirDelay, drv.dirNote);
    w.line(2, "disable_delay_us", 0);
    w.blank();

    // probe
    w.line(0, "probe");
    const probePin = custom.probe || (b.probe && b.probe.pin);
    w.line(2, "pin", a.probe && probePin ? probePin : "NO_PIN", a.probe && b.probe && b.probe.note ? b.probe.note : (a.probe ? "" : "no probe / touch plate"));
    w.line(2, "check_mode_start", "true");
    w.blank();

    // control
    w.line(0, "control");
    for (const c of b.controls) {
      const pin = (custom.controls || {})[c.key] || c.pin;
      const want = (a.controls || {})[c.key];
      if (want && pin) w.line(2, c.key, pin, c.note);
      else if (c.key === "safety_door_pin" && pin) w.line(2, c.key, "NO_PIN", "door disabled - to use a door switch: " + pin + (c.note ? " (" + c.note + ")" : ""));
      else w.line(2, c.key, "NO_PIN");
    }
    w.blank();

    // axes
    w.line(0, "axes");
    const en = custom.enable || b.enable;
    w.line(2, "shared_stepper_disable_pin", en || "NO_PIN", en && b.enableNote ? b.enableNote : null);
    const anyBed = (a.axes || []).some((x) => x.moves === "bed");
    for (const x of a.axes || []) {
      w.blank();
      const L = x.axis;
      const drvBlk = B(x.block), limBlk = B(x.limitBlock || x.block);
      const spm = +x.spm || stepsPerMm(x.calc || {});
      const lim = maxRateFor(spm, drv.hz, drv.pulse, drv.dirDelay);
      const rate = Math.min(+x.wantRate || lim, lim);
      const homing = x.switchAt && x.switchAt !== "none";
      const rot = x.axis === "a" && x.rotary;
      w.line(2, L);
      w.line(4, "steps_per_mm", fmt(spm), x.spmNote || (x.calc ? spmComment(x.calc, spm) : null));
      w.line(4, "soft_limits", homing ? "true" : "false", homing ? "refuse moves outside the travel (needs homing)" : null);
      w.line(4, "max_rate_mm_per_min", fmt(rate), rate < (+x.wantRate || 0) || rate === lim ? drv.label + " ~" + Math.round(drv.hz / 1000) + " kHz limit at " + fmt(spm) + " steps/mm" : null);
      w.line(4, "acceleration_mm_per_sec2", fmt(+x.accel || 100));
      w.line(4, "max_travel_mm", fmt(+x.travel || (rot ? 360 : 100)));
      if (homing) {
        const seek = Math.round((rate * 0.8) / 50) * 50 || 50;
        w.line(4, "homing");
        w.line(6, "cycle", x.axis === "z" ? 1 : 2);
        w.line(6, "mpos_mm", 0, "switch position = machine 0, travel runs " + (x.switchAt === "max" ? "0 to -" : "0 to +") + fmt(+x.travel || 100));
        w.line(6, "feed_mm_per_min", rate >= 2000 ? 200 : 100);
        w.line(6, "seek_mm_per_min", seek);
        w.line(6, "settle_ms", 250);
        const pos = x.switchAt === "max";
        let why;
        if (x.axis === "z") why = pos ? "switch at the top" : "switch at the bottom";
        else if (x.moves === "bed") why = "bed switch: triggers with the bed at the " + (pos ? "front" : "back") + " = tool at the " + (pos ? "back" : "front") + " (" + x.axis.toUpperCase() + (pos ? " max)" : " min)");
        else if (x.axis === "x") why = pos ? "switch at the right" : "switch at the left";
        else if (x.axis === "y") why = pos ? "switch at the back" : "switch at the front";
        else why = pos ? "switch at the + end" : "switch at the - end";
        w.line(6, "positive_direction", pos ? "true" : "false", why);
      }
      w.blank();
      w.line(4, "motor0");
      const limPin = homing ? pinWith(limBlk.limit, a.switchType === "no" ? (b.limitNO || "") : (b.limitNC || "")) : "NO_PIN";
      let limNote = null;
      if (homing) {
        if (x.limitBlock && x.limitBlock !== x.block) limNote = x.axis.toUpperCase() + " limit on the " + x.limitBlock + " block (pins 8/7). " + x.block + " block limit input = " + (drvBlk.limit || "?");
        else if (legacy && limBlk.was) limNote = "was " + limBlk.was.limit;
        else if (x.axis === "y" && legacy) limNote = x.block + " block limit (pins 8/7). A block limit input = " + B("A").limit;
      }
      w.line(6, "limit_all_pin", limPin, limNote);
      w.line(6, "hard_limits", homing ? "true" : "false", homing ? "stop at once if a switch trips during motion" : null);
      w.line(6, "pulloff_mm", 2);
      w.line(6, "standard_stepper");
      const bed = x.moves === "bed";
      const dirInvert = bed !== !!x.flipDir;
      const dirPin = dirInvert ? pinWith(drvBlk.dir, ":low") : drvBlk.dir;
      let dn = null, sn = null;
      if (legacy && drvBlk.was) { dn = "was " + drvBlk.was.dir; sn = "was " + drvBlk.was.step; }
      else if (x.block !== x.axis.toUpperCase()) { dn = x.axis.toUpperCase() + " driver on the " + x.block + " block: " + x.block + " DIR (terminal pin 6)"; sn = x.axis.toUpperCase() + " driver on the " + x.block + " block: " + x.block + " STEP (terminal pin 4)"; }
      else if (bed) { dn = x.block + " block DIR - inverted: on the " + (a.machineName || "machine") + " the BED moves, " + x.axis.toUpperCase() + "+ = bed toward you"; sn = x.block + " block STEP (terminal pin 4)"; }
      else if (x.flipDir) dn = "inverted after the first-run direction test";
      w.line(8, "direction_pin", dirPin || "NO_PIN", dn);
      w.line(8, "step_pin", drvBlk.step || "NO_PIN", sn);
    }
    void anyBed;
    w.blank();

    // pendant (dpCREATOR R2/R3)
    if (a.pendant && b.pendant) {
      w.line(0, "uart1");
      w.line(2, "txd_pin", b.pendant.tx);
      w.line(2, "rxd_pin", b.pendant.rx);
      w.line(2, "rts_pin", "NO_PIN");
      w.line(2, "cts_pin", "NO_PIN");
      w.line(2, "baud", 1000000, "SmartTabUI pendant firmware runs at 1000000");
      w.line(2, "mode", "8N1");
      w.blank();
      w.line(0, "uart_channel1");
      w.line(2, "uart_num", 1);
      w.line(2, "report_interval_ms", 75);
      w.line(2, "message_level", "Info");
      w.blank();
    }
    const vfd = (a.spindles || []).find((s) => s.kind === "vfd");
    if (vfd && b.rs485) {
      w.line(0, "uart2");
      w.line(2, "txd_pin", b.rs485.tx);
      w.line(2, "rxd_pin", b.rs485.rx);
      w.line(2, "rts_pin", b.rs485.rts, "RS485 DE");
      w.line(2, "cts_pin", "NO_PIN");
      w.line(2, "baud", vfd.baud || 9600);
      w.line(2, "mode", vfd.mode || "8N1");
      w.blank();
    }

    // spi / sd / ethernet
    const eth = a.network === "ethernet" && (custom.eth || b.eth) && (custom.eth || b.eth).cs;
    if (a.sd || eth) {
      const s = Object.assign({}, b.spi, custom.spi || {});
      const n = (b.spi && b.spi.notes) || {};
      w.line(0, "spi");
      w.line(2, "miso_pin", s.miso || "NO_PIN", n.miso);
      w.line(2, "mosi_pin", s.mosi || "NO_PIN", n.mosi);
      w.line(2, "sck_pin", s.sck || "NO_PIN", n.sck);
      w.blank();
    }
    if (a.sd) {
      w.line(0, "sdcard");
      w.line(2, "cs_pin", (custom.sd && custom.sd.cs) || b.sd.cs || "NO_PIN", b.sd.note);
      w.line(2, "card_detect_pin", "NO_PIN");
      w.line(2, "frequency_hz", a.sdHz || 4000000);
      w.blank();
    }
    if (eth) {
      const e = Object.assign({}, b.eth, custom.eth || {});
      w.line(0, "ethernet");
      w.line(2, "cs_pin", e.cs, e.note);
      w.line(2, "int_pin", e.int && e.intWired !== false ? e.int : "NO_PIN", !e.int || e.intWired === false ? (e.intLater ? e.intLater + " once INT is wired to PCB pad 32" : "INT not wired: polling") : null);
      w.line(2, "rst_pin", e.rst || "NO_PIN");
      w.line(2, "phy_type", "w5500");
      w.line(2, "phy_addr", 1);
      w.line(2, "frequency_hz", 8000000, a.sd ? "shared bus with the SD card" : null);
      w.blank();
    }

    w.line(0, "macros");
    ["startup_line0", "startup_line1", "macro0", "macro1", "macro2", "macro3"].forEach((k) => w.line(2, k));
    w.blank();
    w.line(0, "start");
    const homedAll = (a.axes || []).length && (a.axes || []).every((x) => x.switchAt && x.switchAt !== "none" || x.rotary);
    w.line(2, "must_home", a.mustHome !== undefined ? String(!!a.mustHome) : String(!!homedAll));
    w.blank();
    w.line(0, "coolant");
    w.line(2, "mist_pin", "NO_PIN");
    w.line(2, "flood_pin", "NO_PIN");
    w.blank();

    // spindles
    let toolNum = 0;
    for (const s of a.spindles || []) {
      const outDef = (b.outputs || {})[s.output] || {};
      const outPin = (custom.outputs || {})[s.output] || outDef.pin || "NO_PIN";
      const low = s.low !== undefined ? s.low : !!outDef.lowDefault;
      const pin = low ? pinWith(outPin, ":low") : outPin;
      if (s.kind === "relay") {
        w.line(0, "Relay");
        w.line(2, "output_pin", pin, outDef.note);
        w.line(2, "direction_pin", "NO_PIN");
        w.line(2, "enable_pin", "NO_PIN");
        w.line(2, "disable_with_s0", "false");
        w.line(2, "s0_with_disable", "true");
        w.line(2, "spinup_ms", s.spinup != null ? s.spinup : 3000, "wait after M3 for the spindle to reach speed");
        w.line(2, "spindown_ms", s.spindown != null ? s.spindown : 3000, "wait after M5 for it to stop");
        spindleTool(w, s, laser, toolNum++);
        w.line(2, "speed_map", "0=0.000% " + (s.maxRpm || 24000) + "=100.000%");
      } else if (s.kind === "pwm" || s.kind === "10v") {
        w.line(0, s.kind === "pwm" ? "PWM" : "10V");
        w.line(2, "pwm_hz", s.pwmHz || 5000);
        w.line(2, "output_pin", pin, outDef.note);
        w.line(2, "enable_pin", s.enable ? (b.outputs.relay && b.outputs.relay.pin) || "NO_PIN" : "NO_PIN", s.enable ? "relay switches the spindle on/off" : null);
        w.line(2, "direction_pin", "NO_PIN");
        w.line(2, "disable_with_s0", "false");
        w.line(2, "s0_with_disable", "true");
        w.line(2, "spinup_ms", s.spinup != null ? s.spinup : 2000);
        w.line(2, "spindown_ms", s.spindown != null ? s.spindown : 2000);
        spindleTool(w, s, laser, toolNum++);
        w.line(2, "speed_map", "0=0.000% " + (s.maxRpm || 24000) + "=100.000%");
      } else if (s.kind === "vfd") {
        w.line(0, s.vfd || "Huanyang");
        w.line(2, "uart_num", 2);
        w.line(2, "modbus_id", s.modbusId || 1);
        w.line(2, "spinup_ms", s.spinup != null ? s.spinup : 5000);
        w.line(2, "spindown_ms", s.spindown != null ? s.spindown : 5000);
        spindleTool(w, s, laser, toolNum++);
        w.line(2, "speed_map", "0=0% " + (s.maxRpm || 24000) + "=100%");
      } else if (s.kind === "laser") {
        w.line(0, "Laser");
        w.line(2, "pwm_hz", s.pwmHz || 5000);
        w.line(2, "output_pin", pin, outDef.note);
        w.line(2, "enable_pin", "NO_PIN");
        w.line(2, "disable_with_s0", "false");
        w.line(2, "s0_with_disable", "true");
        if (router) {
          w.line(2, "tools", s.tools || "2", "M6 T" + (s.tools || "2") + " selects the laser (dpCREATOR firmware)");
          const off = (s.offset || [0, 0, 0]).map(fmt).join(" ");
          w.line(2, "offset_mm", off, off === "0 0 0" ? "laser spot relative to the spindle bit, X Y Z - measure and set" : "laser spot relative to the spindle bit, X Y Z");
        } else {
          w.line(2, "tool_num", 0);
        }
        w.line(2, "speed_map", "0=0.000% " + (s.maxS || 255) + "=100.000%");
        w.line(2, "off_on_alarm", "true");
      }
      w.blank();
    }
    return w.text();
  }
  function spindleTool(w, s, laser, n) {
    if (laser) w.line(2, "tool_num", n, n === 0 ? "every tool except the laser's" : null);
    else w.line(2, "tool_num", n);
  }
  function spmComment(c, spm) {
    if (c.drive === "belt") return (c.teeth || 20) + "T pulley, " + (c.pitch || 2) + " mm belt at 1/" + c.micro + " = " + fmt(spm);
    if (c.drive === "rack") return "rack and pinion, " + (c.pinionDia || 20) + " mm pinion at 1/" + c.micro;
    if (c.drive === "rotary") return "rotary: steps per degree";
    return (c.measured ? "measured; " : "") + (c.lead || 8) + " mm lead at 1/" + c.micro + " = " + fmt(stepsPerMm(c));
  }

  // Change one value in config text, keeping the line's comment. path: "axes.x.steps_per_mm"
  function setValue(text, path, value) {
    const t = parseConfig(text);
    const node = get(t.root, path);
    if (!node) return null;
    const lines = String(text).replace(/\r\n?/g, "\n").split("\n");
    const raw = lines[node.line - 1];
    const body = stripComment(raw);
    const comment = raw.slice(body.replace(/\s+$/, "").length);
    const c = body.indexOf(":");
    lines[node.line - 1] = body.slice(0, c + 1) + " " + value + comment;
    return lines.join("\n");
  }

  const api = { parseConfig, Schema, validate, setValue, parsePin, CHIPS, BOARDS, DRIVERS, stepsPerMm, maxRateFor, fwStepHz, generate, nodePath, child, get };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.DPCSetup = api;
})(typeof window !== "undefined" ? window : globalThis);
