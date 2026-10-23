// Tests for dpc-setup-core.js: node test.js
const fs = require("fs");
const yaml = require("js-yaml");
const C = require("./dpc-setup-core.js");

const items = yaml.load(fs.readFileSync("/tmp/claude-0/fnc/FluidNC/docs/config_items.yaml", "utf8"));
const schema = C.Schema(items);
let fails = 0;
function ok(cond, msg) { if (!cond) { fails++; console.log("FAIL:", msg); } else console.log("ok  ", msg); }

// flatten a parsed tree into ordered [path, value] pairs
function flat(text) {
  const t = C.parseConfig(text);
  const out = [];
  (function w(n, p) { for (const c of n.children) { const q = p.concat(c.key); out.push([q.join("."), c.value === null ? "" : String(c.value)]); w(c, q); } })(t.root, []);
  return out;
}

// --- 3018 answers (brief section 7)
const answers3018 = {
  board: "legacy", machineName: "3018", driver: "tb6600", holdIdle: false, switchType: "nc", type: "router+laser",
  axes: [
    { axis: "x", block: "X", limitBlock: "X", moves: "tool", switchAt: "min", travel: 300, spm: 1600, spmNote: "measured on this 3018 (2 mm lead at 1/16). TB6600 at 1/4 -> 400", wantRate: 3000, accel: 100 },
    { axis: "y", block: "Y", limitBlock: "Y", moves: "bed", switchAt: "min", travel: 180, spm: 1600, spmNote: "measured on this 3018 (2 mm lead at 1/16). TB6600 at 1/4 -> 400", wantRate: 3000, accel: 100 },
    { axis: "z", block: "A", limitBlock: "A", moves: "tool", switchAt: "max", travel: 45, spm: 1600, spmNote: "measured on this 3018 (2 mm lead at 1/16). TB6600 at 1/4 -> 400", wantRate: 500, accel: 100 },
  ],
  probe: true, controls: { safety_door_pin: false, reset_pin: true, feed_hold_pin: false },
  spindles: [
    { kind: "relay", output: "relay", spinup: 3000, spindown: 3000, maxRpm: 24000 },
    { kind: "laser", output: "pwm", tools: "2", offset: [0, 0, 0], pwmHz: 5000, maxS: 255 },
  ],
  pendant: false, network: "ethernet", sd: true, sdHz: 4000000, mustHome: true,
};
const gen = C.generate(answers3018);
fs.writeFileSync("out3018.yaml", gen);
const want = flat(fs.readFileSync("sec7.yaml", "utf8"));
const got = flat(gen);
const wm = new Map(want), gm = new Map(got);
const missing = want.filter(([k]) => !gm.has(k)), extra = got.filter(([k]) => !wm.has(k));
const diff = want.filter(([k, v]) => gm.has(k) && gm.get(k) !== v);
missing.forEach(([k, v]) => console.log("   missing", k, v));
extra.forEach(([k, v]) => console.log("   extra  ", k, v));
diff.forEach(([k, v]) => console.log("   differs", k, "want", JSON.stringify(v), "got", JSON.stringify(gm.get(k))));
ok(!missing.length && !extra.length && !diff.length, "3018 answers produce the section 7 config (" + want.length + " keys)");
const order = (l) => l.map(([k]) => k).join("|");
ok(order(want) === order(got), "same key order as section 7");

// --- validator on good configs
function report(name, r) { r.problems.filter((p) => p.level !== "info").forEach((p) => console.log("   ", name, p.level, "line", p.line, p.msg)); }
const v7 = C.validate(fs.readFileSync("sec7.yaml", "utf8"), schema, { chip: "esp32s3", driverHz: 20000 });
report("sec7", v7);
ok(v7.ok, "section 7 passes the validator");
const vGen = C.validate(gen, schema, { chip: "esp32s3", driverHz: 20000 });
report("gen", vGen);
ok(vGen.ok, "generated 3018 config passes the validator");
const vR2 = C.validate(fs.readFileSync("/tmp/claude-0/fnc/example_configs/dpcreator_r2_r3_w5500.yaml", "utf8"), schema, { chip: "esp32s3" });
report("r2", vR2);
ok(vR2.ok, "R2/R3 example config passes the validator");

// --- validator on bad configs
function bad(name, text, re, opts) {
  const r = C.validate(text, schema, Object.assign({ chip: "esp32s3" }, opts || {}));
  const hit = r.problems.find((p) => p.level === "error" && re.test(p.msg));
  ok(!r.ok && hit, name + (hit ? " -> line " + hit.line + ": " + hit.msg : " (got: " + r.problems.map((p) => p.msg).join(" / ") + ")"));
}
function warnOnly(name, text, re) {
  const r = C.validate(text, schema, { chip: "esp32s3" });
  const hit = r.problems.find((p) => p.level === "warn" && re.test(p.msg));
  ok(hit && !r.problems.some((p) => p.level === "error" && re.test(p.msg)), name + " is a warning (the firmware accepts it)" + (hit ? " -> " + hit.msg : ""));
}
const base = fs.readFileSync("sec7.yaml", "utf8");
{ const r = C.validate(base.replace("stepping:", "Stepping:").replace("  x:\n", "  X:\n"), schema, { chip: "esp32s3" }); ok(r.ok, "keys match without regard to case, like FluidNC (" + r.errors + " errors)"); }
bad("unknown key under control", base.replace("  feed_hold_pin: NO_PIN", "  feed_hold_pin: NO_PIN\n  delay_ms: 100"), /Unknown key "delay_ms"/);
bad("over-indented key", base.replace("    max_travel_mm: 300", "      max_travel_mm: 300"), /indent|siblings/i);
bad("GPIO used twice", base.replace("step_pin: gpio.18", "step_pin: gpio.16"), /used more than once/);
bad("USB pin on S3", base.replace("step_pin: gpio.18", "step_pin: gpio.19"), /native USB/);
bad("octal PSRAM pin", base.replace("step_pin: gpio.18", "step_pin: gpio.36"), /octal PSRAM/);
bad("flash pin", base.replace("step_pin: gpio.18", "step_pin: gpio.27"), /flash chip/);
bad("too fast for the TB6600", base.replace("max_rate_mm_per_min: 750", "max_rate_mm_per_min: 3000"), /kHz/, { driverHz: 20000 });
bad("too fast for the firmware", base.replace("max_rate_mm_per_min: 750", "max_rate_mm_per_min: 3000").replace("steps_per_mm: 1600", "steps_per_mm: 3200"), /firmware/);
bad("two spindles on one pin", base.replace("output_pin: gpio.13", "output_pin: gpio.48"), /two spindles/);
bad("tool number twice", base.replace("tool_num: 0                    # every", "tools: 2\n  tool_num: 0    # every"), /already used/);
warnOnly("soft limits without homing", base.replace(/  z:\n([\s\S]*?)    homing:\n      cycle: 1/, "  z:\n$1    homing:\n      cycle: 0"), /soft_limits needs homing/);
bad("bad enum", base.replace("engine: TIMED", "engine: FAST"), /must be one of/);
bad("tab indent", base.replace("  engine: TIMED", "\tengine: TIMED"), /Tab/);
bad("value on a section", base.replace("stepping:", "stepping: yes"), /is a section/);
bad("not a number", base.replace("pulloff_mm: 2", "pulloff_mm: two"), /must be a number/);
warnOnly("must_home without homing", "axes:\n  x:\n    steps_per_mm: 80\nstart:\n  must_home: true\n", /must_home/);

// --- steps/mm calculator (brief 2.5 table)
const t = [[2, 4, 400], [2, 8, 800], [2, 16, 1600], [4, 4, 200], [4, 16, 800], [8, 8, 200], [8, 16, 400]];
ok(t.every(([lead, micro, w]) => C.stepsPerMm({ motorSteps: 200, micro, drive: "screw", lead }) === w), "steps/mm table matches the brief");
ok(C.maxRateFor(1600, 20000, 5, 5) === 750, "TB6600 at 1600 steps/mm -> 750 mm/min");

if (fails) { console.log("\n" + fails + " FAILED"); process.exit(1); } else console.log("\nall passed");
