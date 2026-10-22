import { chromium } from "/tmp/claude-0/scr/node_modules/playwright/index.mjs";
import fs from "fs";
const C = (await import("/tmp/claude-0/scr/setup/dpc-setup-core.js")).default;
const b = await chromium.launch({ executablePath: "/opt/pw-browsers/chromium-1194/chrome-linux/chrome" });
const p = await b.newPage({ viewport: { width: 1360, height: 900 } });
const errs = [];
p.on("pageerror", (e) => errs.push(e.message));
p.on("console", (m) => { if (m.type() === "error") errs.push(m.text()); });
await p.goto("http://127.0.0.1:8811/test.html");
await p.screenshot({ path: "/tmp/claude-0/scr/setup/s0.png" });
await p.click('[data-choice="build"]');
const next = () => p.click("#dpcs-modal [data-next]");
const shot = (n) => p.screenshot({ path: "/tmp/claude-0/scr/setup/b" + n + ".png" });
await shot(1);
await p.click('#dpcs-modal [data-set="board"][data-val="legacy"]');
await next();               // machine
await p.click('#dpcs-modal [data-set="type"][data-val="router+laser"]');
await p.fill('#dpcs-modal [data-k="machineName"]', "3018"); await p.press('#dpcs-modal [data-k="machineName"]', "Tab");
await shot(2);
await next();               // blocks
await shot(3);
await next();               // moves
await p.click('#dpcs-modal [data-set="axes.1.moves"][data-val="bed"]');
await shot(4);
await next();               // drivers
await shot(5);
await next();               // steps
for (const i of [0, 1, 2]) { await p.fill('#dpcs-modal [data-k="axes.' + i + '.spmOverride"]', "1600"); await p.press('#dpcs-modal [data-k="axes.' + i + '.spmOverride"]', "Tab"); }
await p.fill('#dpcs-modal [data-k="axes.2.wantRate"]', "500"); await p.press('#dpcs-modal [data-k="axes.2.wantRate"]', "Tab");
await shot(6);
await next();               // switches
for (const [i, v] of [[0, 300], [1, 180], [2, 45]]) { await p.fill('#dpcs-modal [data-k="axes.' + i + '.travel"]', String(v)); await p.press('#dpcs-modal [data-k="axes.' + i + '.travel"]', "Tab"); }
await shot(7);
await next();               // controls
await p.click('#dpcs-modal input[data-k="controls.reset_pin"]');
await p.click('#dpcs-modal input[data-k="probe"]');
await shot(8);
await next();               // spindles
await shot(9);
await next();               // extras
await shot(10);
await next();               // review
await p.waitForTimeout(1500);
await shot(11);
const yaml = await p.evaluate(() => document.querySelector("#dpcs-modal .dpcs-yaml pre").textContent);
fs.writeFileSync("/tmp/claude-0/scr/setup/ui3018.yaml", yaml);
await p.click("#dpcs-modal [data-use]");
await p.waitForTimeout(1500);
await shot(12);
// file path: bad config
await p.evaluate(() => window.DPCSetupUI.useConfig("stepping:\n  engine: TIMED\ncontrol:\n  delay_ms: 5\naxes:\n  x:\n    steps_per_mm: 80\n      max_rate_mm_per_min: 100\n", "config.yaml", "your file test.yaml"));
await p.waitForTimeout(800);
await shot(13);
await p.setViewportSize({ width: 390, height: 844 });
await p.click('[data-choice="build"]');
await p.waitForTimeout(400);
await shot(14);
await p.click('#dpcs-modal [data-close]');
await p.evaluate(() => document.documentElement.classList.add("dpc-light"));
await p.setViewportSize({ width: 1360, height: 900 });
await p.click('[data-choice="build"]'); await p.click("#dpcs-modal [data-go='7']").catch(()=>{});
await p.waitForTimeout(300);
await shot(15);
console.log("errors:", JSON.stringify(errs));
await b.close();
// compare
const flat = (text) => { const t = C.parseConfig(text); const out = []; (function w(n, pp) { for (const c of n.children) { const q = pp.concat(c.key); out.push(q.join(".") + "=" + (c.value ?? "")); w(c, q); } })(t.root, []); return out; };
const a = flat(fs.readFileSync("/tmp/claude-0/scr/setup/sec7.yaml", "utf8")), g = flat(yaml);
const d1 = a.filter((x) => !g.includes(x)), d2 = g.filter((x) => !a.includes(x));
console.log("UI vs section 7:", d1.length || d2.length ? "DIFF\n want " + d1.join("\n want ") + "\n got  " + d2.join("\n got  ") : "IDENTICAL (" + a.length + " keys)");
