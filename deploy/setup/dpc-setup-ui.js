/* dpCREATOR machine setup on the Flash Firmware page.
 *
 *   1  Machine configuration: pick a config.yaml (checked at once), build one, or keep the board's
 *   2  Flash (ESP Web Tools dialog, as before) - warns that "Erase device" also wipes the config
 *   3  Write config.yaml back over USB (XModem, like Board tools > Files), restart, read the boot log
 *   4  First-run checks on the machine
 *
 * Needs on the page: <div id="dpcs-flow" data-manifest=".." data-name=".."></div>,
 * window.DPCBoardTools (Board tools script), dpcFlash() (page script).
 * Copyright (c) 2026 dpCREATOR (Val). All rights reserved.
 */
(function () {
  "use strict";
  const S = window.DPCSetup, G = window.DPCSetupSVG;
  const mount = document.getElementById("dpcs-flow");
  if (!S || !G || !mount) return;
  const BASE = (document.currentScript && document.currentScript.src || "").replace(/[^/]*$/, "") || "/catalog/view/javascript/dpc-setup/";
  const ITEMS_URL = "https://raw.githubusercontent.com/val218/dpcreator-fluidnc/fluidnc-4.1.1-w5500/FluidNC/docs/config_items.yaml";
  const $ = (sel, el) => (el || document).querySelector(sel);
  const $$ = (sel, el) => Array.prototype.slice.call((el || document).querySelectorAll(sel));
  const esc = (s) => String(s == null ? "" : s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const hasSerial = "serial" in navigator;

  // ------------------------------------------------------------ schema (firmware key list)
  let schemaP = null, schemaSrc = "";
  function loadScript(src) {
    return new Promise((res, rej) => { const s = document.createElement("script"); s.src = src; s.onload = res; s.onerror = rej; document.head.appendChild(s); });
  }
  function schema() {
    if (schemaP) return schemaP;
    schemaP = (async () => {
      if (!window.jsyaml) await loadScript(BASE + "js-yaml.min.js");
      let doc = null;
      try {
        const r = await fetch(ITEMS_URL, { cache: "no-cache" });
        if (r.ok) { doc = window.jsyaml.load(await r.text()); schemaSrc = "firmware repo (latest)"; }
      } catch (e) { /* offline or blocked: use the copy shipped with the page */ }
      if (!doc) { doc = await (await fetch(BASE + "config_items.json")).json(); schemaSrc = "copy shipped with this page"; }
      return S.Schema(doc);
    })();
    return schemaP;
  }
  async function check(text, opts) {
    const sc = await schema();
    return S.validate(text, sc, Object.assign({ chip: "esp32s3" }, opts || {}));
  }

  // ------------------------------------------------------------ styles
  const css = document.createElement("style");
  css.textContent = `
#dpcs-flow,#dpcs-modal{--c-bg:#0f1318;--c-card:#161c23;--c-card2:#1b232c;--c-line:#28323d;--c-text:#dde3e8;--c-mute:#8e9ba7;--c-acc:#2b8fe0;--c-acc2:#4fb4ee;--c-ok:#2fb36a;--c-warn:#d9a21b;--c-err:#e0564c;--c-pos:#e0564c;--c-sig:#4fb4ee}
html.dpc-light #dpcs-flow,html.dpc-light #dpcs-modal{--c-bg:#f6f7f9;--c-card:#fff;--c-card2:#f1f4f7;--c-line:#d8dee5;--c-text:#1d252c;--c-mute:#5d6975;--c-acc:#1f6fb8;--c-acc2:#1f6fb8;--c-ok:#17803f;--c-warn:#8a6100;--c-err:#c0392b;--c-pos:#c0392b;--c-sig:#1f6fb8}
#dpcs-flow{margin:14px 0 4px;color:var(--c-text)}
#dpcs-flow ol.dpcs-steps{list-style:none;margin:0;padding:0;counter-reset:s}
#dpcs-flow .dpcs-step{position:relative;padding:0 0 18px 40px;margin:0;border-left:2px solid var(--c-line);margin-left:13px}
#dpcs-flow .dpcs-step:last-child{border-left-color:transparent}
#dpcs-flow .dpcs-step::before{counter-increment:s;content:counter(s);position:absolute;left:-15px;top:-2px;width:28px;height:28px;border-radius:50%;background:var(--c-card2);border:2px solid var(--c-line);font:700 14px/24px Barlow,system-ui,sans-serif;text-align:center;color:var(--c-mute)}
#dpcs-flow .dpcs-step.done::before{content:"✓";background:var(--c-ok);border-color:var(--c-ok);color:#fff}
#dpcs-flow .dpcs-step.now::before{border-color:var(--c-acc);color:var(--c-text)}
#dpcs-flow h4{margin:0 0 6px;font:600 17px/1.3 "Barlow Condensed",Barlow,sans-serif;color:var(--c-text);text-transform:none;letter-spacing:0}
#dpcs-flow p{margin:0 0 10px;color:var(--c-mute);font-size:14px;line-height:1.5}
#dpcs-flow .dpcs-choices{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;margin:8px 0 10px}
@media (max-width:760px){#dpcs-flow .dpcs-choices{grid-template-columns:1fr}}
.dpcs-choice{display:flex;flex-direction:column;gap:4px;text-align:left;padding:12px 13px;border:1px solid var(--c-line);border-radius:8px;background:var(--c-card);color:var(--c-text);font:inherit;cursor:pointer;transition:border-color .15s,background-color .15s}
.dpcs-choice b{font-size:14.5px}.dpcs-choice span{font-size:12.5px;color:var(--c-mute);line-height:1.4}
.dpcs-choice:hover{border-color:var(--c-acc)}.dpcs-choice[aria-pressed=true]{border-color:var(--c-acc);background:var(--c-card2);box-shadow:inset 3px 0 0 var(--c-acc)}
.dpcs-choice:focus-visible,.dpcs-btn:focus-visible,#dpcs-modal button:focus-visible,#dpcs-modal .tgt:focus-visible{outline:2px solid var(--c-acc);outline-offset:2px}
.dpcs-btn{display:inline-flex;align-items:center;gap:6px;padding:9px 16px;border-radius:6px;border:1px solid var(--c-acc);background:var(--c-acc);color:#fff;font:600 14px Barlow,system-ui,sans-serif;cursor:pointer;text-decoration:none!important}
.dpcs-btn:hover{filter:brightness(1.1)}.dpcs-btn[disabled]{opacity:.45;cursor:default;filter:none}
.dpcs-btn.ghost{background:transparent;color:var(--c-text);border-color:var(--c-line)}.dpcs-btn.ghost:hover{border-color:var(--c-acc)}
.dpcs-btn.small{padding:5px 10px;font-size:13px}
.dpcs-row{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:8px 0}
.dpcs-panel{border:1px solid var(--c-line);border-radius:8px;background:var(--c-card);padding:12px 14px;margin:6px 0 10px}
.dpcs-note{padding:9px 12px;border-radius:6px;border-left:3px solid var(--c-acc);background:rgba(43,143,224,.1);font-size:13.5px;line-height:1.45;margin:8px 0;color:var(--c-text)}
.dpcs-note.warn{border-left-color:var(--c-warn);background:rgba(217,162,27,.12)}.dpcs-note.err{border-left-color:var(--c-err);background:rgba(224,86,76,.12)}.dpcs-note.ok{border-left-color:var(--c-ok);background:rgba(47,179,106,.12)}
.dpcs-probs{list-style:none;margin:6px 0;padding:0;max-height:260px;overflow:auto;font-size:13px}
.dpcs-probs li{display:grid;grid-template-columns:62px 1fr;gap:8px;padding:6px 0;border-bottom:1px solid var(--c-line);line-height:1.4}
.dpcs-probs .lv{font-weight:700;font-size:11.5px}.dpcs-probs .error .lv{color:var(--c-err)}.dpcs-probs .warn .lv{color:var(--c-warn)}.dpcs-probs .info .lv{color:var(--c-mute)}
.dpcs-probs a{color:var(--c-acc2);cursor:pointer}
.dpcs-sum{display:flex;flex-wrap:wrap;gap:10px;align-items:center;font-size:14px}
.dpcs-pill{display:inline-block;padding:2px 9px;border-radius:20px;font-size:12px;font-weight:700;border:1px solid var(--c-line)}
.dpcs-pill.ok{color:var(--c-ok);border-color:var(--c-ok)}.dpcs-pill.err{color:var(--c-err);border-color:var(--c-err)}.dpcs-pill.warn{color:var(--c-warn);border-color:var(--c-warn)}
.dpcs-edit{width:100%;min-height:260px;font:12.5px/1.45 ui-monospace,Menlo,Consolas,monospace;background:var(--c-bg);color:var(--c-text);border:1px solid var(--c-line);border-radius:6px;padding:10px;white-space:pre;tab-size:2}
.dpcs-log{max-height:220px;overflow:auto;background:var(--c-bg);border:1px solid var(--c-line);border-radius:6px;padding:8px 10px;font:12px/1.45 ui-monospace,Menlo,Consolas,monospace;white-space:pre-wrap;margin:8px 0;color:var(--c-mute)}
.dpcs-log .e{color:var(--c-err)}.dpcs-log .o{color:var(--c-acc2)}
.dpcs-bar{height:6px;background:var(--c-line);border-radius:3px;overflow:hidden;margin:8px 0}.dpcs-bar div{height:100%;width:0;background:var(--c-acc);transition:width .2s}
.dpcs-checks{list-style:none;margin:6px 0;padding:0;counter-reset:c}
.dpcs-checks>li{counter-increment:c;border:1px solid var(--c-line);border-radius:8px;background:var(--c-card);padding:10px 12px 10px 40px;margin:0 0 8px;position:relative}
.dpcs-checks>li::before{content:counter(c);position:absolute;left:12px;top:10px;width:20px;height:20px;border-radius:50%;background:var(--c-card2);font:700 12px/20px Barlow,sans-serif;text-align:center;color:var(--c-mute)}
.dpcs-checks>li.pass::before{content:"✓";background:var(--c-ok);color:#fff}.dpcs-checks>li.fail::before{content:"!";background:var(--c-err);color:#fff}
.dpcs-checks b{display:block;margin-bottom:3px;font-size:14px}.dpcs-checks p{margin:0 0 6px!important;font-size:13px!important}
.dpcs-checks code,#dpcs-modal code{font:12.5px ui-monospace,Menlo,Consolas,monospace;background:var(--c-card2);padding:1px 5px;border-radius:4px}
.dpcs-pn{display:flex;gap:6px;flex-wrap:wrap;margin:6px 0}.dpcs-pn span{min-width:30px;padding:4px 8px;border-radius:5px;border:1px solid var(--c-line);text-align:center;font-weight:700;color:var(--c-mute)}.dpcs-pn span.on{background:var(--c-err);border-color:var(--c-err);color:#fff}
.dpcs-hidden{display:none!important}
/* builder */
#dpcs-modal[hidden]{display:none!important}
#dpcs-modal{position:fixed;inset:0;z-index:100001;background:var(--c-bg);color:var(--c-text);display:flex;flex-direction:column;font:15px/1.5 Barlow,system-ui,sans-serif}
#dpcs-modal header.dpcs-top{display:flex;align-items:center;gap:12px;padding:10px 18px;border-bottom:1px solid var(--c-line);background:var(--c-card);position:static}
@media (max-width:520px){#dpcs-modal .dpcs-prog{display:none}#dpcs-modal header h3{font-size:17px}}
#dpcs-modal header h3{margin:0;font:600 19px "Barlow Condensed",Barlow,sans-serif;color:var(--c-text);flex:1;text-transform:none;letter-spacing:0}
#dpcs-modal .dpcs-prog{display:flex;gap:3px;flex:2;max-width:520px}#dpcs-modal .dpcs-prog i{flex:1;height:5px;border-radius:3px;background:var(--c-line)}#dpcs-modal .dpcs-prog i.on{background:var(--c-acc)}
#dpcs-modal .dpcs-main{flex:1;display:grid;grid-template-columns:minmax(0,1fr) minmax(320px,40%);min-height:0}
#dpcs-modal .dpcs-q{overflow:auto;padding:22px 26px 30px}
#dpcs-modal .dpcs-yaml{border-left:1px solid var(--c-line);background:var(--c-card);display:flex;flex-direction:column;min-height:0}
#dpcs-modal .dpcs-yaml .hd{display:flex;align-items:center;gap:8px;padding:8px 12px;border-bottom:1px solid var(--c-line);font-size:13px;color:var(--c-mute)}
#dpcs-modal .dpcs-yaml pre{position:relative;flex:1;overflow:auto;margin:0;padding:10px 12px;font:12px/1.5 ui-monospace,Menlo,Consolas,monospace;color:var(--c-text);background:transparent;border:0;white-space:pre}
#dpcs-modal .dpcs-yaml pre .c{color:var(--c-mute)}#dpcs-modal .dpcs-yaml pre .k{color:var(--c-acc2)}#dpcs-modal .dpcs-yaml pre .chg{background:rgba(43,143,224,.18)}
#dpcs-modal footer{display:flex;gap:10px;align-items:center;padding:10px 18px;border-top:1px solid var(--c-line);background:var(--c-card);position:static}
#dpcs-modal footer .sp{flex:1;color:var(--c-mute);font-size:13px}
#dpcs-modal h2{font:600 26px/1.2 "Barlow Condensed",Barlow,sans-serif;margin:0 0 6px;color:var(--c-text);text-transform:none;letter-spacing:0}
#dpcs-modal h5{font:600 16px "Barlow Condensed",Barlow,sans-serif;margin:18px 0 6px;color:var(--c-text);text-transform:none;letter-spacing:0}
#dpcs-modal .lead{color:var(--c-mute);margin:0 0 16px;max-width:62ch}
#dpcs-modal .opts{display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:8px;margin:8px 0 14px}
#dpcs-modal .opt{text-align:left;padding:11px 12px;border:1px solid var(--c-line);border-radius:8px;background:var(--c-card);color:var(--c-text);font:inherit;cursor:pointer}
#dpcs-modal .opt b{display:block;font-size:14.5px}#dpcs-modal .opt span{display:block;font-size:12.5px;color:var(--c-mute);line-height:1.35;margin-top:2px}
#dpcs-modal .opt:hover{border-color:var(--c-acc)}#dpcs-modal .opt[aria-pressed=true]{border-color:var(--c-acc);box-shadow:inset 3px 0 0 var(--c-acc);background:var(--c-card2)}
#dpcs-modal .opt[disabled]{opacity:.45;cursor:not-allowed}
#dpcs-modal label.f{display:flex;flex-direction:column;gap:3px;font-size:13px;color:var(--c-mute)}
#dpcs-modal input[type=text],#dpcs-modal input[type=number],#dpcs-modal select{background:var(--c-bg);color:var(--c-text);border:1px solid var(--c-line);border-radius:6px;padding:7px 9px;font:14px Barlow,system-ui,sans-serif;min-width:0}
#dpcs-modal .grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px;margin:8px 0}
#dpcs-modal .axcard{border:1px solid var(--c-line);border-radius:8px;background:var(--c-card);padding:12px 14px;margin:0 0 12px}
#dpcs-modal .axcard h6{margin:0 0 6px;font:700 15px Barlow,sans-serif;color:var(--c-text);text-transform:none;letter-spacing:0}
#dpcs-modal .seg{display:inline-flex;border:1px solid var(--c-line);border-radius:7px;overflow:hidden;flex-wrap:wrap}
#dpcs-modal .seg button{background:transparent;border:0;border-right:1px solid var(--c-line);color:var(--c-text);padding:6px 12px;font:600 13.5px Barlow,sans-serif;cursor:pointer}
#dpcs-modal .seg button:last-child{border-right:0}#dpcs-modal .seg button[aria-pressed=true]{background:var(--c-acc);color:#fff}#dpcs-modal .seg button[disabled]{opacity:.4;cursor:not-allowed}
#dpcs-modal .chk{display:flex;gap:9px;align-items:flex-start;padding:9px 0;border-bottom:1px solid var(--c-line);cursor:pointer}
#dpcs-modal .chk input{margin-top:4px;width:17px;height:17px;accent-color:var(--c-acc)}#dpcs-modal .chk b{display:block;font-size:14.5px}#dpcs-modal .chk span{font-size:13px;color:var(--c-mute)}
#dpcs-modal .res{font:700 22px "Barlow Condensed",sans-serif;color:var(--c-acc2)}
#dpcs-modal table.t{border-collapse:collapse;font-size:13px;margin:6px 0}#dpcs-modal table.t td,#dpcs-modal table.t th{border:1px solid var(--c-line);padding:4px 10px;text-align:center}
.dpcs-svg{width:100%;max-width:560px;height:auto;display:block;margin:8px 0;font-family:Barlow,system-ui,sans-serif}
.dpcs-svg-z{max-width:240px}.dpcs-svg-sm{max-width:440px}
.dpcs-svg .pcb{fill:#173a2a;stroke:#2c5c43}.dpcs-svg .pcb-t,.dpcs-svg .chip-t{fill:#cfe7da;font-size:13px;font-weight:700}
.dpcs-svg .chip{fill:#222;stroke:#444}.dpcs-svg .blk{fill:#2a6a43;stroke:#3f8f5d}.dpcs-svg .blk.on{fill:#1f5f99;stroke:#4fb4ee;stroke-width:2}.dpcs-svg .blk.off{fill:#3a3f44;stroke:#555}
.dpcs-svg .pin{fill:#c9a24a}.dpcs-svg .pin-t{fill:#d9e6de;font-size:9.5px}.dpcs-svg .pin-t.big{font-size:12.5px;fill:var(--c-text)}
.dpcs-svg .blk-t{fill:var(--c-text);font-size:13px;font-weight:700}.dpcs-svg .ax-t{fill:#4fb4ee;font-size:13px;font-weight:700}.dpcs-svg .lim-t{fill:#d9a21b;font-size:10.5px}
.dpcs-svg .x{stroke:#e0564c;stroke-width:3}.dpcs-svg .bad text{fill:#9aa}
.dpcs-svg .tgt{cursor:pointer}.dpcs-svg .tgt:hover .blk:not(.off),.dpcs-svg .tgt:hover .sw{stroke:#4fb4ee;stroke-width:3}
.dpcs-svg .frame{fill:none;stroke:var(--c-mute);stroke-width:2}.dpcs-svg .bed{fill:rgba(127,140,155,.18);stroke:var(--c-mute)}.dpcs-svg .bed.moving{fill:rgba(217,162,27,.18);stroke:#d9a21b}
.dpcs-svg .gantry{fill:var(--c-card2);stroke:var(--c-mute);stroke-width:1.5}.dpcs-svg .tool{fill:#2b8fe0}.dpcs-svg .tool-t{fill:#fff;font-size:9px;font-weight:700}
.dpcs-svg .arw{stroke:#2fb36a;stroke-width:3;fill:none}.dpcs-svg .ar{fill:#2fb36a}.dpcs-svg .arw-t{fill:#2fb36a;font-size:12px;font-weight:700}.dpcs-svg .bedarw{stroke:#d9a21b}
.dpcs-svg .lbl{fill:var(--c-mute);font-size:11.5px}.dpcs-svg .sw{fill:var(--c-card);stroke:#d9a21b;stroke-width:2}.dpcs-svg .tgt.on .sw{fill:#d9a21b}.dpcs-svg .sw-t{fill:#fff;font-size:13px;font-weight:700}
.dpcs-svg .wire{fill:none;stroke-width:3}.dpcs-svg .wire.pos{stroke:var(--c-pos)}.dpcs-svg .wire.sig{stroke:var(--c-sig)}.dpcs-svg .wire.bad{stroke:var(--c-mute);stroke-dasharray:5 4}
.dpcs-svg .drv{fill:var(--c-card2);stroke:var(--c-mute)}.dpcs-svg .thread{stroke:var(--c-mute)}.dpcs-svg .track{stroke:var(--c-line);stroke-width:6;stroke-linecap:round}
.dpcs-svg .sw-sym circle{fill:var(--c-text)}
.dpcs-pics{display:flex;gap:10px;flex-wrap:wrap;margin:6px 0}
.dpcs-pic{flex:1;min-width:170px;max-width:260px;padding:8px;border:1px solid var(--c-line);border-radius:8px;background:var(--c-card);color:var(--c-text);font:600 13.5px Barlow,sans-serif;cursor:pointer;text-align:center}
.dpcs-pic svg{width:100%;height:auto;display:block;margin-bottom:6px}.dpcs-pic[aria-pressed=true]{border-color:var(--c-acc);box-shadow:inset 0 -3px 0 var(--c-acc)}
.dpcs-pic .frame{fill:none;stroke:var(--c-mute);stroke-width:2}.dpcs-pic .bed{fill:rgba(127,140,155,.18);stroke:var(--c-mute)}.dpcs-pic .bed.moving{fill:rgba(217,162,27,.2);stroke:#d9a21b}.dpcs-pic .gantry{fill:var(--c-card2);stroke:var(--c-mute)}.dpcs-pic .tool{fill:#2b8fe0}.dpcs-pic .arw{stroke:#2fb36a;stroke-width:3;fill:none}.dpcs-pic .ar{fill:#2fb36a}
@media (prefers-reduced-motion:no-preference){.dpcs-svg .bed.moving{animation:dpcsBed 3s ease-in-out infinite alternate}}
@keyframes dpcsBed{from{transform:translateY(-10px)}to{transform:translateY(12px)}}
@media (max-width:900px){#dpcs-modal .dpcs-main{grid-template-columns:1fr}#dpcs-modal .dpcs-yaml{display:none}#dpcs-modal.show-yaml .dpcs-yaml{display:flex;position:absolute;inset:56px 0 58px 0;z-index:2}#dpcs-modal .dpcs-q{padding:16px}}
@media (min-width:901px){#dpcs-modal .yaml-toggle{display:none}}
`;
  document.head.appendChild(css);

  // ------------------------------------------------------------ flow state
  const st = {
    choice: null,      // "file" | "build" | "board"
    config: null,      // {text, name, source}
    result: null,      // validator result
    flashed: false,
    dialogPort: null,  // SerialPort used by ESP Web Tools
    erase: false,
    session: null, port: null,
  };

  function render() {
    mount.innerHTML = `
<ol class="dpcs-steps">
  <li class="dpcs-step now" id="dpcs-s1">
    <h4>Machine configuration</h4>
    <p>The firmware needs a <code>config.yaml</code> that matches your board and machine. Flashing replaces the one on the board, so choose it first: it is checked now and written back after flashing.</p>
    <div class="dpcs-choices" role="group" aria-label="Where the configuration comes from">
      <button type="button" class="dpcs-choice" data-choice="file" aria-pressed="false"><b>I have a config.yaml</b><span>Pick the file. It is checked at once against the firmware's own list of settings.</span></button>
      <button type="button" class="dpcs-choice" data-choice="build" aria-pressed="false"><b>Build my machine</b><span>Answer a few questions with pictures and get a commented config.yaml.</span></button>
      <button type="button" class="dpcs-choice" data-choice="board" aria-pressed="false"${hasSerial ? "" : " disabled"}><b>Keep the config on the board</b><span>Firmware update: read the board's current config.yaml over USB and check it first.</span></button>
    </div>
    <input type="file" id="dpcs-file" accept=".yaml,.yml,.txt" hidden>
    <div id="dpcs-c1"></div>
    <div id="dpcs-ready"></div>
  </li>
  <li class="dpcs-step" id="dpcs-s2">
    <h4>Flash the firmware</h4>
    <p>Connect the controller by USB and flash. In the flasher, <b>Erase device</b> also wipes the board's files, including <code>config.yaml</code>: that's fine, your configuration is written back in the next step.</p>
    <div class="dpcs-row"><button type="button" class="dpcs-btn" id="dpcs-flash">⚡ Flash via browser</button><a class="dpcs-btn ghost" href="${esc(mount.dataset.bin || "")}" id="dpcs-bin">⇓ Download .bin</a></div>
    <div id="dpcs-c2"></div>
  </li>
  <li class="dpcs-step" id="dpcs-s3">
    <h4>Write the configuration and check it</h4>
    <p>After flashing: the page reconnects over USB, writes <code>config.yaml</code> the same way Board tools → Files does, restarts the board and reads its boot log.</p>
    <div class="dpcs-row"><button type="button" class="dpcs-btn" id="dpcs-write" disabled>Write config.yaml to the board</button></div>
    <div id="dpcs-c3"></div>
  </li>
  <li class="dpcs-step" id="dpcs-s4">
    <h4>First run on the machine</h4>
    <p>Most problems only show up on the real machine: directions, steps/mm, switches and work zero. Go through these once, with a hand near the power switch.</p>
    <div id="dpcs-c4"><button type="button" class="dpcs-btn ghost" id="dpcs-checks-open">Show the first-run checks</button></div>
  </li>
</ol>`;
    $$(".dpcs-choice", mount).forEach((b) => (b.onclick = () => choose(b.dataset.choice)));
    $("#dpcs-file").onchange = onFile;
    $("#dpcs-flash").onclick = onFlash;
    $("#dpcs-write").onclick = () => writeBack();
    $("#dpcs-checks-open").onclick = () => renderChecks();
    if (!mount.dataset.bin) $("#dpcs-bin").remove();
  }

  function stepState() {
    $("#dpcs-s1").classList.toggle("done", !!(st.config && st.result && st.result.ok));
    $("#dpcs-s1").classList.toggle("now", !(st.config && st.result && st.result.ok));
    $("#dpcs-s2").classList.toggle("now", !!(st.config && st.result && st.result.ok) && !st.flashed);
    $("#dpcs-s2").classList.toggle("done", st.flashed);
    $("#dpcs-s3").classList.toggle("now", st.flashed);
    $("#dpcs-write").disabled = !(st.config && hasSerial);
  }

  function choose(c) {
    st.choice = c;
    $$(".dpcs-choice", mount).forEach((b) => b.setAttribute("aria-pressed", b.dataset.choice === c ? "true" : "false"));
    const box = $("#dpcs-c1");
    if (c === "file") { box.innerHTML = ""; $("#dpcs-file").value = ""; $("#dpcs-file").click(); }
    else if (c === "build") { box.innerHTML = ""; openBuilder(); }
    else if (c === "board") readFromBoard();
  }

  async function onFile(ev) {
    const f = ev.target.files && ev.target.files[0];
    if (!f) return;
    const text = await f.text();
    useConfig(text, f.name, "your file " + f.name);
  }

  async function useConfig(text, name, source) {
    st.config = { text, name: name || "config.yaml", source };
    $("#dpcs-ready").innerHTML = '<div class="dpcs-note">Checking ' + esc(source) + "…</div>";
    let r;
    try { r = await check(text); } catch (e) {
      $("#dpcs-ready").innerHTML = '<div class="dpcs-note err">Could not load the firmware\'s list of settings (' + esc(e.message || e) + "). Reload the page and try again.</div>";
      return;
    }
    st.result = r;
    showResult();
    stepState();
  }

  function probList(r) {
    if (!r.problems.length) return "";
    return '<ul class="dpcs-probs">' + r.problems.map((p) => '<li class="' + p.level + '"><span class="lv">' + (p.level === "error" ? "ERROR" : p.level === "warn" ? "WARNING" : "NOTE") + (p.line ? '<br><a data-line="' + p.line + '">line ' + p.line + "</a>" : "") + "</span><span>" + esc(p.msg) + "</span></li>").join("") + "</ul>";
  }

  function showResult() {
    const r = st.result, box = $("#dpcs-ready");
    const pill = r.ok ? '<span class="dpcs-pill ok">ready</span>' : '<span class="dpcs-pill err">' + r.errors + " error" + (r.errors > 1 ? "s" : "") + "</span>";
    const w = r.warnings ? ' <span class="dpcs-pill warn">' + r.warnings + " warning" + (r.warnings > 1 ? "s" : "") + "</span>" : "";
    box.innerHTML = '<div class="dpcs-panel"><div class="dpcs-sum">' + pill + w + "<span><b>" + esc(st.config.name) + "</b> from " + esc(st.config.source) + "</span></div>" +
      (r.ok ? '<p style="margin:8px 0 0">Every key is one the firmware knows. ' + (r.warnings ? "Look at the warnings below before you flash." : "Nothing to fix.") + "</p>"
        : '<div class="dpcs-note err">The firmware would report <code>[MSG:ERR: …]</code> at boot and mark this configuration invalid. Fix the errors first.</div>') +
      probList(r) +
      '<div class="dpcs-row"><button type="button" class="dpcs-btn ghost small" id="dpcs-editbtn">Edit</button><button type="button" class="dpcs-btn ghost small" id="dpcs-dl">Download a copy</button>' +
      (st.choice === "build" ? '<button type="button" class="dpcs-btn ghost small" id="dpcs-rebuild">Back to the builder</button>' : "") + "</div>" +
      '<div id="dpcs-editor" class="dpcs-hidden"><textarea class="dpcs-edit" spellcheck="false" aria-label="config.yaml"></textarea><div class="dpcs-row"><button type="button" class="dpcs-btn small" id="dpcs-recheck">Check again</button></div></div>' +
      '<p style="margin:8px 0 0;font-size:12px">Checked against the firmware\'s settings list (' + esc(schemaSrc) + ").</p></div>";
    $("#dpcs-editbtn").onclick = () => openEditor();
    $("#dpcs-dl").onclick = () => saveFile(st.config.name || "config.yaml", st.config.text);
    if ($("#dpcs-rebuild")) $("#dpcs-rebuild").onclick = () => openBuilder();
    $$(".dpcs-probs a[data-line]", box).forEach((a) => (a.onclick = () => openEditor(+a.dataset.line)));
  }

  function openEditor(line) {
    const ed = $("#dpcs-editor");
    ed.classList.remove("dpcs-hidden");
    const ta = $("textarea", ed);
    if (!ta.value) ta.value = st.config.text;
    $("#dpcs-recheck").onclick = () => useConfig(ta.value, st.config.name, st.config.source + " (edited)").then(() => { const e2 = $("#dpcs-editor"); if (e2) { e2.classList.remove("dpcs-hidden"); $("textarea", e2).value = ta.value; } });
    if (line) {
      const lines = ta.value.split("\n");
      let pos = 0;
      for (let i = 0; i < line - 1 && i < lines.length; i++) pos += lines[i].length + 1;
      ta.focus();
      ta.setSelectionRange(pos, pos + (lines[line - 1] || "").length);
      ta.scrollTop = Math.max(0, (line - 4) * 18);
    }
  }

  function saveFile(name, text) {
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([text], { type: "text/yaml" }));
    a.download = name;
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 5000);
  }

  // ------------------------------------------------------------ USB helpers
  function wrap(sp) {
    const reader = sp.readable.getReader();
    const writer = sp.writable.getWriter();
    return {
      sp,
      read: async () => { const { value, done } = await reader.read(); return done ? null : value; },
      write: (b) => writer.write(b),
      close: async () => {
        try { await reader.cancel(); } catch (e) {}
        try { reader.releaseLock(); writer.releaseLock(); } catch (e) {}
        try { await sp.close(); } catch (e) {}
      },
    };
  }
  async function openPort(sp) {
    await sp.open({ baudRate: 115200, bufferSize: 65536 });
    return wrap(sp);
  }
  // A port we may use without asking again: the flasher's, or a granted one with the same USB ids.
  async function findPort(prefer) {
    const ports = await navigator.serial.getPorts();
    if (prefer && ports.includes(prefer)) return prefer;
    const info = prefer && prefer.getInfo ? prefer.getInfo() : null;
    if (info) {
      const same = ports.filter((p) => { const i = p.getInfo(); return i.usbVendorId === info.usbVendorId && i.usbProductId === info.usbProductId; });
      if (same.length === 1) return same[0];
    }
    if (ports.length === 1) return ports[0];
    return null;
  }

  function logger(el) {
    return (line, dir) => {
      const s = document.createElement("span");
      if (dir === "out") s.className = "o";
      else if (/\[MSG:ERR|^error:|Ignored key|Configuration is invalid/i.test(line)) s.className = "e";
      s.textContent = (dir === "out" ? "> " : "") + line + "\n";
      el.appendChild(s);
      while (el.childNodes.length > 600) el.removeChild(el.firstChild);
      el.scrollTop = el.scrollHeight;
    };
  }

  async function startSession(sp, logEl) {
    const port = await openPort(sp);
    const lines = [];
    const log = logEl ? logger(logEl) : () => {};
    const session = window.DPCBoardTools.Session(port, { onLine: (l, dir) => {
      if (dir === "in") { lines.push(l); if (lines.length > 3000) lines.splice(0, 1000); }
      if (st.quietStatus && ((dir === "in" && /^<.*>$/.test(l)) || (dir === "out" && l === "?"))) return;
      log(l, dir);
    } });
    return { port, session, lines };
  }

  async function waitReady(session, ms) {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      const r = await session.cmd("$I", 2500);
      if (r.ok) return r;
      if (session.closed) return null;
      await sleep(700);
    }
    return null;
  }

  // ------------------------------------------------------------ keep the board's config
  async function readFromBoard() {
    const box = $("#dpcs-c1");
    box.innerHTML = '<div class="dpcs-panel"><p style="margin:0 0 6px">Connect the controller by USB and pick its port. The page reads the active config file and checks it.</p><div class="dpcs-row"><button type="button" class="dpcs-btn" id="dpcs-rd">Connect and read</button></div><div class="dpcs-log dpcs-hidden" id="dpcs-rdlog"></div><div id="dpcs-rdmsg"></div></div>';
    $("#dpcs-rd").onclick = async () => {
      const msg = $("#dpcs-rdmsg"), logEl = $("#dpcs-rdlog");
      let sp;
      try { sp = await navigator.serial.requestPort(); } catch (e) { return; }
      logEl.classList.remove("dpcs-hidden");
      let s;
      try {
        s = await startSession(sp, logEl);
        msg.innerHTML = '<div class="dpcs-note">Talking to the board…</div>';
        if (!(await waitReady(s.session, 8000))) throw new Error("The board does not answer. Is FluidNC running on it (not the bootloader)?");
        let name = (await s.session.getSetting("Config/Filename")) || "config.yaml";
        name = name.replace(/^\/?(localfs\/)?/, "");
        const list = await s.session.listFiles("localfs", "/");
        const f = list.files.find((x) => x.name === name);
        if (!f) throw new Error("The board has no " + name + " in its flash.");
        const bytes = await s.session.download("localfs", name, f.size);
        const text = new TextDecoder().decode(bytes);
        msg.innerHTML = '<div class="dpcs-note ok">Read ' + esc(name) + " (" + bytes.length + " bytes) from the board. The USB port is closed again so the flasher can use it.</div>";
        await s.port.close();
        st.dialogPort = sp;
        useConfig(text, "config.yaml", "the board (" + name + ")");
      } catch (e) {
        msg.innerHTML = '<div class="dpcs-note err">' + esc(e.message || e) + "</div>";
        if (s) try { await s.port.close(); } catch (x) {}
      }
    };
  }

  // ------------------------------------------------------------ flash (ESP Web Tools)
  function onFlash() {
    const box = $("#dpcs-c2");
    if (!st.config || !st.result) {
      box.innerHTML = '<div class="dpcs-note warn">No configuration chosen. The board will start with the firmware\'s default config.yaml, which does not match your machine. <a href="#" id="dpcs-flash-anyway">Flash anyway</a></div>';
      $("#dpcs-flash-anyway").onclick = (e) => { e.preventDefault(); box.innerHTML = ""; startFlash(); };
      return;
    }
    if (!st.result.ok) {
      box.innerHTML = '<div class="dpcs-note err">The configuration still has errors (step 1). Fix them first, or <a href="#" id="dpcs-flash-anyway">flash anyway</a> and fix it later.</div>';
      $("#dpcs-flash-anyway").onclick = (e) => { e.preventDefault(); box.innerHTML = ""; startFlash(); };
      return;
    }
    box.innerHTML = "";
    startFlash();
  }
  function startFlash() {
    const b = document.createElement("button");
    b.setAttribute("data-manifest", mount.dataset.manifest);
    b.setAttribute("data-name", mount.dataset.name || "dpCREATOR controller firmware");
    if (typeof window.dpcFlash === "function") window.dpcFlash(b);
    watchDialog();
  }
  let watching = false;
  function watchDialog() {
    if (watching) return;
    watching = true;
    let dlg = null, finished = false, noted = false;
    const t = setInterval(() => {
      const d = document.querySelector("ewt-install-dialog");
      if (d && d !== dlg) { dlg = d; finished = false; noted = false; }
      if (dlg) {
        if (dlg.port) st.dialogPort = dlg.port;
        // "Erase device" question: add our note under the checkbox
        if (dlg._state === "ASK_ERASE" && dlg.shadowRoot && !noted) {
          const lbl = dlg.shadowRoot.querySelector("label.formfield");
          if (lbl && !dlg.shadowRoot.querySelector(".dpcs-erase")) {
            const n = document.createElement("div");
            n.className = "dpcs-erase";
            n.style.cssText = "margin:10px 0 0;padding:8px 10px;border-left:3px solid #d9a21b;background:rgba(217,162,27,.12);font-size:14px;line-height:1.4";
            n.textContent = st.config ? "Erasing also wipes config.yaml on the board. That's fine: the page writes your configuration back right after flashing." : "Erasing also wipes config.yaml and every other file on the board. Choose a configuration in step 1 first, so the page can write it back.";
            lbl.parentNode.appendChild(n);
            noted = true;
          }
        }
        if (dlg._installErase) st.erase = true;
        const s = dlg._installState;
        if (s && s.state === "finished" && !finished) {
          finished = true;
          st.flashed = true;
          stepState();
          $("#dpcs-c2").innerHTML = '<div class="dpcs-note ok">Firmware flashed' + (st.erase ? " (device erased)" : "") + ". Close the flasher window" + (st.config ? ": the configuration is written next." : ".") + "</div>";
        }
        if (!document.body.contains(dlg)) {
          const wasFinished = finished;
          dlg = null;
          clearInterval(t);
          watching = false;
          if (wasFinished && st.config) setTimeout(() => writeBack(true), 700);
        }
      }
    }, 400);
  }

  // ------------------------------------------------------------ write back + boot check
  async function writeBack(auto) {
    const box = $("#dpcs-c3");
    if (!st.config) { box.innerHTML = '<div class="dpcs-note warn">Choose a configuration in step 1 first.</div>'; return; }
    if (st.result && !st.result.ok) box.innerHTML = '<div class="dpcs-note warn">The configuration has errors (step 1). It is written anyway so you can see the board\'s own messages.</div>';
    else box.innerHTML = "";
    box.insertAdjacentHTML("beforeend", '<div class="dpcs-bar"><div id="dpcs-wbar"></div></div><div id="dpcs-wmsg" class="dpcs-note">Opening the USB port…</div><div class="dpcs-log" id="dpcs-wlog"></div><div id="dpcs-verdict"></div>');
    const msg = (t, cls) => { const m = $("#dpcs-wmsg"); m.className = "dpcs-note" + (cls ? " " + cls : ""); m.innerHTML = t; };
    const bar = (p) => ($("#dpcs-wbar").style.width = Math.round(p * 100) + "%");
    $("#dpcs-write").disabled = true;
    try {
      await closeSession();
      let sp = await findPort(st.dialogPort);
      if (!sp) {
        if (auto) { msg('Click <b>Write config.yaml to the board</b> and pick the controller\'s port.', "warn"); $("#dpcs-write").disabled = false; return; }
        sp = await navigator.serial.requestPort();
      }
      let s = null;
      for (let i = 0; i < 6 && !s; i++) {
        try { s = await startSession(sp, $("#dpcs-wlog")); } catch (e) { await sleep(800); sp = (await findPort(sp)) || sp; }
      }
      if (!s) throw new Error("Could not open the USB port. Close other programs that use it, then click Write again.");
      msg("Waiting for the board to start…");
      bar(0.1);
      if (!(await waitReady(s.session, 20000))) throw new Error("The board does not answer over USB. Unplug and replug the USB cable, then click Write again.");
      msg("Writing config.yaml…");
      const bytes = new TextEncoder().encode(st.config.text.endsWith("\n") ? st.config.text : st.config.text + "\n");
      await s.session.upload("localfs", "config.yaml", bytes, (p) => bar(0.1 + p * 0.6));
      const fn = await s.session.getSetting("Config/Filename");
      if (fn && fn.replace(/^\/?(localfs\/)?/, "") !== "config.yaml") await s.session.setSetting("Config/Filename", "config.yaml");
      msg("Written (" + bytes.length + " bytes). Restarting the board…");
      bar(0.75);
      let before = s.lines.length;
      s.session.send("$Bye");
      // Native USB disconnects on restart; a USB-UART bridge stays open and shows the boot log.
      await sleep(2500);
      if (s.session.closed) {
        try { await s.port.close(); } catch (e) {}
        s = null;
        for (let i = 0; i < 15 && !s; i++) {
          await sleep(1000);
          const p2 = await findPort(sp);
          if (!p2) continue;
          try { s = await startSession(p2, $("#dpcs-wlog")); sp = p2; before = 0; } catch (e) { s = null; }
        }
        if (!s) throw new Error("The board restarted but the USB port did not come back. Click Write again, or check the boot log in Board tools → Terminal ($SS).");
      } else await sleep(2500);
      msg("Reading the boot log…");
      if (!(await waitReady(s.session, 15000))) throw new Error("The board does not answer after the restart.");
      const ss = await s.session.cmd("$SS", 6000);
      const bootLines = s.lines.slice(before).concat(ss.lines || []);
      bar(1);
      st.session = s.session; st.port = s.port; st.sp = sp; st.lines = s.lines;
      verdict(bootLines);
    } catch (e) {
      if (e && e.name === "NotFoundError") { msg("No port chosen.", "warn"); }
      else msg(esc(e.message || e), "err");
    } finally {
      $("#dpcs-write").disabled = false;
    }
  }
  async function closeSession() {
    if (st.port) { try { await st.port.close(); } catch (e) {} }
    st.port = null; st.session = null;
  }

  function verdict(lines) {
    const errs = lines.filter((l) => /\[MSG:ERR|Ignored key|Configuration is invalid/i.test(l));
    const uniq = errs.filter((l, i) => errs.indexOf(l) === i);
    const cfg = lines.find((l) => /Configuration file:?\s*(\S+)/i.test(l));
    const v = $("#dpcs-verdict");
    $("#dpcs-wmsg").classList.add("dpcs-hidden");
    if (uniq.length) {
      v.innerHTML = '<div class="dpcs-note err"><b>The board rejected part of the configuration.</b> Any of these lines makes the whole config invalid:<ul class="dpcs-probs">' + uniq.map((l) => "<li class=\"error\"><span class=\"lv\">ERR</span><span><code>" + esc(l) + "</code></span></li>").join("") + "</ul>Fix them in step 1 (Edit), then click Write again.</div>";
      $("#dpcs-s3").classList.remove("done");
    } else {
      v.innerHTML = '<div class="dpcs-note ok"><b>Configuration loaded.</b> ' + (cfg ? esc(cfg.replace(/^\[MSG:INFO: ?/, "").replace(/\]$/, "")) + ". " : "") + "No <code>ERR</code> lines in the boot log. Now the first-run checks below.</div>";
      $("#dpcs-s3").classList.add("done");
      $("#dpcs-s3").classList.remove("now");
      $("#dpcs-s4").classList.add("now");
      renderChecks();
    }
  }

  // ------------------------------------------------------------ first-run checks (brief 2.11)
  function cfgAxes() {
    if (!st.config) return ["x", "y", "z"];
    const t = S.parseConfig(st.config.text);
    const a = S.child(t.root, "axes");
    return a ? a.children.map((c) => c.key).filter((k) => /^[xyzabc]$/.test(k)) : ["x", "y", "z"];
  }
  function cfgVal(path) {
    if (!st.config) return null;
    const n = S.get(S.parseConfig(st.config.text).root, path);
    return n ? n.value : null;
  }
  function renderChecks() {
    const box = $("#dpcs-c4");
    const live = !!st.session;
    const axes = cfgAxes();
    const laser = st.config && /^Laser:/m.test(st.config.text);
    const laserTool = (cfgVal("Laser.tools") || "").split(/\s+/)[0];
    const btn = (id, label, ghost) => live ? '<button type="button" class="dpcs-btn small' + (ghost ? " ghost" : "") + '" id="' + id + '">' + label + "</button>" : "";
    const dirWord = { x: "the tool moves RIGHT", y: "the tool moves AWAY from you (a moving bed comes TOWARD you)", z: "the tool moves UP", a: "the axis turns the positive way" };
    box.innerHTML = (live ? '<div class="dpcs-note ok">Connected over USB: the buttons below send the commands for you. You can also type them in Board tools → Terminal.</div>'
      : '<div class="dpcs-note">Type the commands in Board tools → Terminal (or the WebUI console). Connected over USB after step 3, the buttons send them for you.</div>') +
      '<ol class="dpcs-checks">' +
      '<li id="dpcs-k1"><b>Boot log: no ERR lines</b><p>Any <code>Ignored key</code> or pin error makes the whole config invalid. Step 3 checks this; by hand: <code>$SS</code>.</p>' + btn("dpcs-k1b", "Read the boot log again", true) + '<div class="k-out"></div></li>' +
      '<li id="dpcs-k2"><b>Switches at rest: no Pn: letters</b><p>Send <code>?</code> with nothing pressed. A letter with nothing pressed = wrong block, open wire, NO switch configured as NC, or the switch not on pins 8/7.</p>' + btn("dpcs-k2b", "Check inputs", true) + '<div class="k-out"></div></li>' +
      '<li id="dpcs-k3"><b>Directions: tool relative to the work</b><p>Unlock with <code>$X</code>, jog each axis +10 mm and watch the <b>tool against the workpiece</b>, not the bed. Wrong way: flip that axis (the page toggles <code>:low</code> on its direction_pin and writes the config again).</p><div class="k-axes">' +
      axes.map((a) => '<div class="dpcs-row" data-ax="' + a + '"><b style="min-width:22px">' + a.toUpperCase() + "</b>" + (live ? '<button type="button" class="dpcs-btn small ghost" data-jog="' + a + '">Jog ' + a.toUpperCase() + "+10</button>" : "<code>$J=G91 G21 " + a.toUpperCase() + "10 F300</code>") + '<span style="color:var(--c-mute);font-size:13px">expect: ' + (dirWord[a] || "+") + "</span>" + (live ? '<button type="button" class="dpcs-btn small ghost" data-flip="' + a + '">Wrong way: flip ' + a.toUpperCase() + "</button>" : "") + "</div>").join("") + '</div><div class="k-out"></div></li>' +
      '<li id="dpcs-k4"><b>Steps/mm over 100 mm</b><p>Mark the position, jog 100 mm (Z: 10-20 mm), measure the real distance. New value = current × commanded ÷ measured. Measure over 100 mm, not 10.</p>' +
      '<div class="dpcs-row"><select id="dpcs-cal-ax">' + axes.map((a) => "<option>" + a + "</option>").join("") + '</select><label>commanded <input type="number" id="dpcs-cal-cmd" value="100" style="width:80px"> mm</label><label>measured <input type="number" id="dpcs-cal-meas" step="0.01" style="width:90px"> mm</label>' + (live ? '<button type="button" class="dpcs-btn small ghost" id="dpcs-cal-jog">Jog it</button>' : "") + '<button type="button" class="dpcs-btn small" id="dpcs-cal-go">Calculate</button></div><div class="k-out"></div></li>' +
      '<li id="dpcs-k5"><b>Press each switch: the right letter lights up</b><p>Live view of the inputs (<code>?</code> → <code>Pn:</code>).</p>' + btn("dpcs-k5b", "Start live view", true) + '<div class="dpcs-pn" id="dpcs-pn">' + ["X", "Y", "Z", "A", "P", "D", "R", "H", "S"].map((c) => '<span data-pn="' + c + '" title="' + ({ P: "probe", D: "door", R: "reset", H: "feed hold", S: "cycle start" }[c] || c + " limit") + '">' + c + "</span>").join("") + '</div><div class="k-out"></div></li>' +
      '<li id="dpcs-k6"><b>First homing</b><p>Hand on the power switch. <code>$H</code> homes Z first, then X/Y. Each axis must move <b>toward</b> its switch; if one goes away from it, stop and check positive_direction.</p>' + btn("dpcs-k6b", "Home ($H)") + '<div class="k-out"></div></li>' +
      '<li id="dpcs-k7"><b>Work zero on the stock</b><p>Jog to the front-left corner of the stock for X0 Y0 and to its top for Z0, then zero each axis there. The WebUI shows Machine (from the switches) and Work (from your zero) side by side; work zero must sit inside the machine travel.</p><div class="dpcs-row">' + btn("dpcs-k7xy", "Zero X and Y here", true) + btn("dpcs-k7z", "Zero Z here", true) + "</div>" + (live ? "" : "<p><code>G10 L20 P1 X0 Y0</code> then <code>G10 L20 P1 Z0</code></p>") + '<div class="k-out"></div></li>' +
      (laser ? '<li id="dpcs-k9"><b>Laser polarity, before connecting the laser</b><p>A reversed output fires the laser at full power while idle. With a meter on the output: <code>M6 T' + esc(laserTool || "0") + "</code>, <code>M3 S0</code> must read off; <code>M3 S" + esc(((cfgVal("Laser.speed_map") || "").match(/(\d+)=100/) || [])[1] || "255") + "</code> must read on; <code>M5</code>. Reversed: add or remove <code>:low</code> on Laser/output_pin.</p>" +
        (live ? '<div class="dpcs-row"><button type="button" class="dpcs-btn small ghost" data-cmds="M6 T' + esc(laserTool || "0") + '|M3 S0">Output OFF test</button><button type="button" class="dpcs-btn small ghost" data-cmds="M3 S' + esc(((cfgVal("Laser.speed_map") || "").match(/(\d+)=100/) || [])[1] || "255") + '">Output ON test</button><button type="button" class="dpcs-btn small" data-cmds="M5">M5 (off)</button></div>' : "") + '<div class="k-out"></div></li>' : "") +
      '<li id="dpcs-k8"><b>Load the job and run</b><p>In the WebUI, open the job and check that the travel warning ("Goes past the travel … by N mm") is gone. If it shows, the zero is wrong: re-zero on the stock, especially after any direction or homing change.</p></li>' +
      "</ol>";
    if (!live) return;
    const out = (li, html, cls) => { const o = $("#" + li + " .k-out"); o.innerHTML = '<div class="dpcs-note ' + (cls || "") + '">' + html + "</div>"; };
    const mark = (li, ok) => { const el = $("#" + li); el.classList.toggle("pass", ok === true); el.classList.toggle("fail", ok === false); };
    const send = async (c, ms) => st.session.cmd(c, ms || 8000);
    $("#dpcs-k1b").onclick = async () => {
      const r = await send("$SS", 6000);
      const errs = (r.lines || []).filter((l) => /\[MSG:ERR|Ignored key|Configuration is invalid/i.test(l));
      mark("dpcs-k1", !errs.length);
      out("dpcs-k1", errs.length ? errs.map((l) => "<code>" + esc(l) + "</code>").join("<br>") : "No ERR lines.", errs.length ? "err" : "ok");
    };
    // realtime '?' answers with a status report line (<Idle|MPos:..|Pn:XZ>); cmd() skips those, the session's line list keeps them
    async function pins() {
      const before = st.lines.length;
      st.quietStatus = true;
      await st.session.realtime(0x3f);
      let last = null;
      for (let i = 0; i < 10 && !last; i++) { await sleep(60); last = st.lines.slice(before).filter((l) => /^<.*>$/.test(l)).pop() || null; }
      if (!last) return null;
      const pn = (last.match(/\|Pn:([A-Z]+)/) || [])[1] || "";
      const state = (last.match(/^<([A-Za-z]+)/) || [])[1] || "";
      return { pn, state, raw: last };
    }
    $("#dpcs-k2b").onclick = async () => {
      const r = await pins();
      if (!r) return out("dpcs-k2", "No status report came back. Try again.", "warn");
      mark("dpcs-k2", !r.pn);
      out("dpcs-k2", r.pn ? "Active with nothing pressed: <b>" + esc(r.pn.split("").join(", ")) + "</b>. Check that switch: block, wiring between pins 8 and 7, NC vs NO." : "No inputs active. State: " + esc(r.state) + ".", r.pn ? "err" : "ok");
    };
    $$("[data-jog]", box).forEach((b) => (b.onclick = async () => {
      const a = b.dataset.jog.toUpperCase();
      await send("$X", 3000);
      const r = await send("$J=G91 G21 " + a + "10 F300", 6000);
      out("dpcs-k3", r.ok ? "Jogged " + a + " +10 mm. Did " + (dirWord[b.dataset.jog] || "it move the + way") + "? If not, click Wrong way." : "The board refused the jog: " + esc(r.error || "") + " " + esc((r.lines || []).join(" ")), r.ok ? "" : "err");
    }));
    $$("[data-flip]", box).forEach((b) => (b.onclick = async () => {
      const a = b.dataset.flip;
      const path = "axes." + a + ".motor0.standard_stepper.direction_pin";
      const cur = cfgVal(path);
      if (!cur) return out("dpcs-k3", "No direction_pin for " + a.toUpperCase() + " in the config.", "err");
      const parts = cur.split(":");
      const next = parts.includes("low") ? parts.filter((p) => p !== "low").join(":") : cur + ":low";
      await rewrite(path, next, "dpcs-k3", a.toUpperCase() + " direction flipped (" + cur + " → " + next + "). Homing direction may need the same flip: re-check step 6.");
    }));
    $("#dpcs-cal-jog").onclick = async () => {
      const a = $("#dpcs-cal-ax").value.toUpperCase(), d = +$("#dpcs-cal-cmd").value || 100;
      await send("$X", 3000);
      const r = await send("$J=G91 G21 " + a + d + " F" + (a === "Z" ? 200 : 600), 8000);
      out("dpcs-k4", r.ok ? "Jogging " + a + " by " + d + " mm. Measure the real distance and enter it." : "Refused: " + esc(r.error || ""), r.ok ? "" : "err");
    };
    $("#dpcs-cal-go").onclick = async () => {
      const a = $("#dpcs-cal-ax").value, cmd = +$("#dpcs-cal-cmd").value, meas = +$("#dpcs-cal-meas").value;
      const cur = parseFloat(cfgVal("axes." + a + ".steps_per_mm"));
      if (!cur || !cmd || !meas) return out("dpcs-k4", "Enter the commanded and the measured distance.", "warn");
      const nv = Math.round(((cur * cmd) / meas) * 1000) / 1000;
      out("dpcs-k4", "New " + a.toUpperCase() + " steps/mm: <b>" + nv + "</b> (was " + cur + "). " +
        '<button type="button" class="dpcs-btn small ghost" id="dpcs-cal-try">Try it now (until restart)</button> <button type="button" class="dpcs-btn small" id="dpcs-cal-save">Save to config.yaml</button>');
      $("#dpcs-cal-try").onclick = async () => { const r = await send("$/axes/" + a + "/steps_per_mm=" + nv, 4000); out("dpcs-k4", r.ok ? "Set to " + nv + " until the next restart. Jog and measure again." : "Refused: " + esc(r.error || ""), r.ok ? "ok" : "err"); };
      $("#dpcs-cal-save").onclick = () => rewrite("axes." + a + ".steps_per_mm", String(nv), "dpcs-k4", a.toUpperCase() + " steps/mm saved as " + nv + ". Check max_rate in step 1: at more steps/mm the driver may allow less speed.");
    };
    let liveT = null;
    $("#dpcs-k5b").onclick = async () => {
      if (liveT) { clearInterval(liveT); liveT = null; $("#dpcs-k5b").textContent = "Start live view"; return; }
      $("#dpcs-k5b").textContent = "Stop live view";
      const seen = new Set();
      liveT = setInterval(async () => {
        if (!st.session) { clearInterval(liveT); return; }
        const r = await pins();
        if (!r) return;
        $$("#dpcs-pn span").forEach((s) => s.classList.toggle("on", r.pn.includes(s.dataset.pn)));
        r.pn.split("").forEach((c) => seen.add(c));
        out("dpcs-k5", "Seen so far: " + (seen.size ? [...seen].join(", ") : "none"));
      }, 400);
    };
    $("#dpcs-k6b").onclick = async () => {
      out("dpcs-k6", "Homing… stop with the power switch if an axis runs away from its switch.");
      const r = await send("$H", 90000);
      mark("dpcs-k6", r.ok);
      out("dpcs-k6", r.ok ? "Homed. The machine position is now 0 at each switch." : "Homing failed: " + esc(r.error || "") + " " + esc((r.lines || []).filter((l) => /ALARM|ERR/.test(l)).join(" ")), r.ok ? "ok" : "err");
    };
    $("#dpcs-k7xy").onclick = async () => { const r = await send("G10 L20 P1 X0 Y0", 4000); out("dpcs-k7", r.ok ? "X0 Y0 set here (G54)." : "Refused: " + esc(r.error || ""), r.ok ? "ok" : "err"); };
    $("#dpcs-k7z").onclick = async () => { const r = await send("G10 L20 P1 Z0", 4000); out("dpcs-k7", r.ok ? "Z0 set here (G54)." : "Refused: " + esc(r.error || ""), r.ok ? "ok" : "err"); };
    $$("[data-cmds]", box).forEach((b) => (b.onclick = async () => {
      const res = [];
      for (const c of b.dataset.cmds.split("|")) { const r = await send(c, 4000); res.push(c + ": " + (r.ok ? "ok" : r.error)); }
      out("dpcs-k9", esc(res.join(" · ")) + ". Read the meter now.");
    }));
  }

  // change one value, write the file back, restart and re-check
  async function rewrite(path, value, li, okText) {
    const nt = S.setValue(st.config.text, path, value);
    if (!nt) return;
    st.config.text = nt;
    st.result = await check(nt);
    showResult();
    const o = $("#" + li + " .k-out");
    o.innerHTML = '<div class="dpcs-note">Writing the change and restarting…</div>';
    await writeBack();
    o.innerHTML = '<div class="dpcs-note ok">' + esc(okText) + "</div>";
  }

  // ------------------------------------------------------------ builder
  const DEF_AX = {
    x: { travel: 300, wantRate: 3000, switchAt: "min" },
    y: { travel: 200, wantRate: 3000, switchAt: "min" },
    z: { travel: 60, wantRate: 1000, switchAt: "max" },
    a: { travel: 360, wantRate: 3000, switchAt: "none", rotary: true },
  };
  function defaults(boardId) {
    const b = S.BOARDS[boardId];
    const usable = ["X", "Y", "Z", "A"].filter((k) => !b.blocks[k].unusable);
    const axes = ["x", "y", "z"].map((ax) => {
      let blk = ax.toUpperCase();
      if (!usable.includes(blk)) blk = usable.find((k) => !["X", "Y", "Z", "A"].slice(0, 3).includes(k) || k === "A") || usable[0];
      return Object.assign({ axis: ax, block: blk, limitBlock: blk, moves: "tool", accel: 100, calc: { motorSteps: 200, micro: 8, drive: "screw", lead: 8 }, spmOverride: "" }, DEF_AX[ax]);
    });
    return {
      board: boardId, machineName: "", type: "router", nAxes: 3, driver: "tb6600", holdIdle: false, switchType: "nc",
      axes, probe: false, controls: {}, spindles: [{ kind: b.outputs.relay && b.outputs.relay.pin !== "" ? "relay" : "pwm", output: b.outputs.relay && b.outputs.relay.pin !== "" ? "relay" : "pwm", spinup: 3000, spindown: 3000, maxRpm: 24000 }],
      pendant: !!b.pendant, network: b.eth && b.eth.cs ? "ethernet" : "wifi", sd: true, sdHz: 4000000, custom: {},
    };
  }
  let ans = null, screen = 0, lastYaml = "";
  function SCREENS() {
    const list = ["board"];
    if (ans.board === "generic") list.push("pins");
    list.push("machine", "blocks", "moves", "drivers", "steps", "switches", "controls", "spindles", "extras", "review");
    return list;
  }
  function getP(o, path) { return path.split(".").reduce((x, k) => (x == null ? x : x[k]), o); }
  function setP(o, path, v) {
    const ks = path.split(".");
    let x = o;
    for (let i = 0; i < ks.length - 1; i++) { if (x[ks[i]] == null) x[ks[i]] = /^\d+$/.test(ks[i + 1]) ? [] : {}; x = x[ks[i]]; }
    x[ks[ks.length - 1]] = v;
  }
  function genAnswers() {
    const a = JSON.parse(JSON.stringify(ans));
    a.axes = a.axes.slice(0, a.nAxes);
    a.axes.forEach((x) => {
      const calc = S.stepsPerMm(x.calc);
      x.spm = +x.spmOverride || Math.round(calc * 1000) / 1000;
      x.spmNote = +x.spmOverride ? "measured / set by hand (calculated: " + Math.round(calc * 1000) / 1000 + ")" : null;
      if (!+x.spmOverride) delete x.spmNote;
    });
    a.spindles = (a.spindles || []).filter(Boolean);
    a.mustHome = a.axes.every((x) => x.switchAt !== "none" || x.rotary);
    return a;
  }
  function yamlNow() { try { return S.generate(genAnswers()); } catch (e) { return "# " + e.message; } }

  function openBuilder() {
    if (!ans) ans = defaults("r3");
    let m = $("#dpcs-modal");
    if (!m) {
      m = document.createElement("div");
      m.id = "dpcs-modal";
      m.setAttribute("role", "dialog");
      m.setAttribute("aria-modal", "true");
      m.setAttribute("aria-label", "Build my machine");
      m.innerHTML = '<header class="dpcs-top"><h3>Build my machine</h3><div class="dpcs-prog" aria-hidden="true"></div><button type="button" class="dpcs-btn ghost small yaml-toggle">config.yaml</button><button type="button" class="dpcs-btn ghost small" data-close>Close</button></header>' +
        '<div class="dpcs-main"><section class="dpcs-q" aria-live="polite"></section><aside class="dpcs-yaml" aria-label="config.yaml preview"><div class="hd"><b>config.yaml</b><span class="v"></span></div><pre></pre></aside></div>' +
        '<footer><button type="button" class="dpcs-btn ghost" data-back>Back</button><span class="sp"></span><button type="button" class="dpcs-btn" data-next>Next</button></footer>';
      document.body.appendChild(m);
      m.addEventListener("click", onBuilderClick);
      m.addEventListener("change", onBuilderInput);
      m.addEventListener("input", (e) => { if (e.target.matches("input[type=text],input[type=number]")) onBuilderInput(e, true); });
      m.addEventListener("keydown", (e) => {
        if (e.key === "Escape") closeBuilder();
        if ((e.key === "Enter" || e.key === " ") && e.target.matches(".tgt")) { e.preventDefault(); e.target.dispatchEvent(new MouseEvent("click", { bubbles: true })); }
      });
    }
    document.documentElement.style.overflow = "hidden";
    m.hidden = false;
    renderScreen();
    $("[data-next]", m).focus();
  }
  function closeBuilder() {
    const m = $("#dpcs-modal");
    if (m) m.hidden = true;
    document.documentElement.style.overflow = "";
  }
  function onBuilderClick(e) {
    const m = $("#dpcs-modal");
    const t = e.target.closest("[data-set],[data-close],[data-back],[data-next],[data-go],.yaml-toggle,[data-use],[data-dl],[data-add],[data-del]");
    if (!t) return;
    if (t.matches("[data-close]")) return closeBuilder();
    if (t.matches(".yaml-toggle")) return m.classList.toggle("show-yaml");
    if (t.matches("[data-back]")) { screen = Math.max(0, screen - 1); return renderScreen(); }
    if (t.matches("[data-next]")) { screen = Math.min(SCREENS().length - 1, screen + 1); return renderScreen(); }
    if (t.matches("[data-go]")) { screen = +t.dataset.go; return renderScreen(); }
    if (t.matches("[data-use]")) { const y = yamlNow(); closeBuilder(); useConfig(y, "config.yaml", "the builder"); return; }
    if (t.matches("[data-dl]")) return saveFile("config.yaml", yamlNow());
    if (t.matches("[data-add]")) { addSpindle(t.dataset.add); return renderScreen(); }
    if (t.matches("[data-del]")) { ans.spindles.splice(+t.dataset.del, 1); return renderScreen(); }
    if (t.matches("[data-set]")) {
      let v = t.dataset.val;
      if (v === "true") v = true; else if (v === "false") v = false; else if (/^-?\d+(\.\d+)?$/.test(v)) v = +v;
      if (t.dataset.set === "board") { const name = ans.machineName; ans = defaults(v); ans.machineName = name; }
      else setP(ans, t.dataset.set, v);
      afterChange(t.dataset.set);
      renderScreen();
    }
  }
  function onBuilderInput(e, typing) {
    const el = e.target;
    if (!el.dataset || !el.dataset.k) return;
    let v = el.type === "checkbox" ? el.checked : el.value;
    if (el.type === "number" || el.dataset.num) v = el.value === "" ? "" : +el.value;
    setP(ans, el.dataset.k, v);
    afterChange(el.dataset.k);
    if (typing) updateYaml(); else renderScreen();
  }
  function afterChange(path) {
    if (/^axes\.\d+\.block$/.test(path)) {
      const i = +path.split(".")[1];
      if (!ans.axes[i].limitApart) ans.axes[i].limitBlock = ans.axes[i].block;
    }
    if (/^axes\.\d+\.limitApart$/.test(path)) { const i = +path.split(".")[1]; if (!ans.axes[i].limitApart) ans.axes[i].limitBlock = ans.axes[i].block; }
    if (path === "nAxes" && ans.nAxes === 4 && ans.axes.length < 4) {
      const b = S.BOARDS[ans.board];
      const used = ans.axes.map((x) => x.block);
      const free = ["X", "Y", "Z", "A"].find((k) => !used.includes(k) && !b.blocks[k].unusable) || "A";
      ans.axes.push(Object.assign({ axis: "a", block: free, limitBlock: free, moves: "tool", accel: 100, calc: { motorSteps: 200, micro: 8, drive: "rotary", ratio: 1 }, spmOverride: "" }, DEF_AX.a));
    }
    if (path === "type") {
      const b = S.BOARDS[ans.board];
      const relay = b.outputs.relay && b.outputs.relay.pin !== "" ? "relay" : null;
      const pwmOut = b.outputs.pwm ? "pwm" : null;
      const t = ans.type;
      ans.spindles = [];
      if (t === "router" || t === "router+laser") ans.spindles.push({ kind: relay ? "relay" : "pwm", output: relay || pwmOut, spinup: 3000, spindown: 3000, maxRpm: 24000 });
      if (t === "plasma") ans.spindles.push({ kind: "relay", output: relay || pwmOut, spinup: 0, spindown: 0, maxRpm: 1 });
      if (t === "laser" || t === "router+laser") ans.spindles.push({ kind: "laser", output: pwmOut, low: b.outputs.pwm && b.outputs.pwm.lowDefault, tools: t === "router+laser" ? "2" : "", offset: [0, 0, 0], maxS: 255, pwmHz: 5000 });
    }
  }
  function addSpindle(kind) {
    const b = S.BOARDS[ans.board];
    const used = ans.spindles.map((s) => s.output);
    const want = kind === "relay" ? ["relay", "pwm", "ttl"] : kind === "vfd" ? [null] : ["pwm", "ttl", "relay"];
    const out = want.find((o) => o === null || (b.outputs[o] && !used.includes(o))) || want[0];
    const s = { kind, output: out, spinup: kind === "vfd" ? 5000 : kind === "laser" ? 0 : 3000, spindown: kind === "vfd" ? 5000 : kind === "laser" ? 0 : 3000, maxRpm: 24000 };
    if (kind === "laser") Object.assign(s, { tools: ans.spindles.some((x) => x.kind !== "laser") ? "2" : "", offset: [0, 0, 0], maxS: 255, pwmHz: 5000, low: b.outputs[out] && b.outputs[out].lowDefault });
    if (kind === "vfd") Object.assign(s, { vfd: "Huanyang", modbusId: 1, baud: 9600 });
    ans.spindles.push(s);
  }

  function updateYaml() {
    const m = $("#dpcs-modal");
    const y = yamlNow();
    const pre = $(".dpcs-yaml pre", m);
    const old = lastYaml.split("\n");
    pre.innerHTML = y.split("\n").map((l, i) => {
      const c = l.indexOf(" #") >= 0 && !/^\s*#/.test(l) ? l.indexOf(" #") : (/^\s*#/.test(l) ? 0 : -1);
      const main = c >= 0 ? l.slice(0, c) : l, com = c >= 0 ? l.slice(c) : "";
      const km = /^(\s*)([A-Za-z0-9_]+):(.*)$/.exec(main);
      const body = km ? esc(km[1]) + '<span class="k">' + esc(km[2]) + "</span>:" + esc(km[3]) : esc(main);
      return '<span class="' + (lastYaml && old[i] !== l ? "chg" : "") + '">' + body + (com ? '<span class="c">' + esc(com) + "</span>" : "") + "</span>";
    }).join("\n");
    lastYaml = y;
    const first = $(".chg", pre);
    if (first) { const top = first.offsetTop - pre.clientHeight / 3; if (top > pre.scrollTop + pre.clientHeight - 40 || top < pre.scrollTop) pre.scrollTop = Math.max(0, top); }
    schema().then((sc) => {
      const b = S.BOARDS[ans.board];
      const r = S.validate(y, sc, { chip: b.chip, octal: b.octal, driverHz: S.DRIVERS[ans.driver].hz });
      const v = $(".dpcs-yaml .v", m);
      v.innerHTML = r.ok ? '<span class="dpcs-pill ok">valid</span>' + (r.warnings ? ' <span class="dpcs-pill warn">' + r.warnings + " warning" + (r.warnings > 1 ? "s" : "") + "</span>" : "") : '<span class="dpcs-pill err">' + r.errors + " error" + (r.errors > 1 ? "s" : "") + "</span>";
      const rv = $("#dpcs-review-probs");
      if (rv) rv.innerHTML = r.problems.length ? probList(r) : '<div class="dpcs-note ok">No problems: every key is one the firmware knows, no pin is used twice, every speed is within the driver\'s limit.</div>';
    }).catch(() => {});
  }

  const opt = (path, val, cur, title, sub, dis) => '<button type="button" class="opt" data-set="' + path + '" data-val="' + esc(val) + '" aria-pressed="' + (String(cur) === String(val) ? "true" : "false") + '"' + (dis ? " disabled" : "") + "><b>" + title + "</b>" + (sub ? "<span>" + sub + "</span>" : "") + "</button>";
  const seg = (path, cur, items) => '<div class="seg" role="group">' + items.map(([v, l, dis]) => '<button type="button" data-set="' + path + '" data-val="' + esc(v) + '" aria-pressed="' + (String(cur) === String(v) ? "true" : "false") + '"' + (dis ? " disabled" : "") + ">" + l + "</button>").join("") + "</div>";
  const num = (path, val, label, extra) => '<label class="f">' + label + '<input type="number" data-k="' + path + '" value="' + esc(val == null ? "" : val) + '"' + (extra || "") + "></label>";
  const txt = (path, val, label, extra) => '<label class="f">' + label + '<input type="text" data-k="' + path + '" value="' + esc(val == null ? "" : val) + '"' + (extra || "") + "></label>";
  const chk = (path, val, title, sub, dis) => '<label class="chk"><input type="checkbox" data-k="' + path + '"' + (val ? " checked" : "") + (dis ? " disabled" : "") + "><span><b>" + title + "</b>" + (sub ? "<span>" + sub + "</span>" : "") + "</span></label>";
  const AX = (i) => ans.axes[i].axis.toUpperCase();

  const RENDER = {
    board() {
      return "<h2>Which controller?</h2><p class=\"lead\">Each board is a table of its terminal blocks and the pins behind them. The rest of the questions follow from it.</p><div class=\"opts\">" +
        opt("board", "r3", ans.board, "dpCREATOR R3", "ESP32-S3 N16R8, W5500 Ethernet, separate probe input") +
        opt("board", "r2", ans.board, "dpCREATOR R2", "Same pins as the R3; the probe shares the Z limit input") +
        opt("board", "legacy", ans.board, "Legacy v4.0.2 / XYZ50 / XYZ77", "With the ESP32-S3 module swap (pin table from the bring-up)") +
        opt("board", "generic", ans.board, "Other FluidNC board", "Enter the pins yourself; they are checked as you go") +
        "</div>" + G.board(S.BOARDS[ans.board] === S.BOARDS.generic ? Object.assign({}, S.BOARDS.generic, { label: "Your board" }) : S.BOARDS[ans.board], {});
    },
    pins() {
      const c = ans.custom;
      const row = (blk) => '<div class="axcard"><h6>' + blk + ' block</h6><div class="grid">' + txt("custom.blocks." + blk + ".step", (c.blocks || {})[blk] && c.blocks[blk].step, "STEP pin", ' placeholder="gpio.4"') + txt("custom.blocks." + blk + ".dir", (c.blocks || {})[blk] && c.blocks[blk].dir, "DIR pin", ' placeholder="gpio.5"') + txt("custom.blocks." + blk + ".limit", (c.blocks || {})[blk] && c.blocks[blk].limit, "Limit input", ' placeholder="gpio.36"') + "</div></div>";
      return "<h2>Your board's pins</h2><p class=\"lead\">Type the GPIO behind each terminal, like <code>gpio.4</code>. Leave a field empty if the board doesn't have it. The preview on the right checks each pin: used twice, flash or USB pins, input-only pins.</p>" +
        '<div class="axcard"><h6>Chip</h6>' + seg("customChip", ans.customChip || "esp32s3", [["esp32s3", "ESP32-S3"], ["esp32", "ESP32 (classic)"]]) + "</div>" +
        ["X", "Y", "Z", "A"].map(row).join("") +
        '<div class="axcard"><h6>Other outputs and inputs</h6><div class="grid">' + txt("custom.enable", c.enable, "Shared stepper enable") + txt("custom.probe", c.probe, "Probe input") + txt("custom.outputs.pwm", (c.outputs || {}).pwm, "PWM output") + txt("custom.outputs.relay", (c.outputs || {}).relay, "Relay output") +
        txt("custom.spi.miso", (c.spi || {}).miso, "SPI MISO") + txt("custom.spi.mosi", (c.spi || {}).mosi, "SPI MOSI") + txt("custom.spi.sck", (c.spi || {}).sck, "SPI SCK") + txt("custom.sd.cs", (c.sd || {}).cs, "SD card CS") + txt("custom.eth.cs", (c.eth || {}).cs, "W5500 CS") +
        txt("custom.controls.safety_door_pin", (c.controls || {}).safety_door_pin, "Door input") + txt("custom.controls.reset_pin", (c.controls || {}).reset_pin, "Reset input") + txt("custom.controls.feed_hold_pin", (c.controls || {}).feed_hold_pin, "Feed hold input") + txt("custom.controls.cycle_start_pin", (c.controls || {}).cycle_start_pin, "Cycle start input") + "</div></div>";
    },
    machine() {
      return "<h2>What kind of machine?</h2><p class=\"lead\">This picks the spindle and laser setup; you can change it later.</p><div class=\"opts\">" +
        opt("type", "router", ans.type, "CNC router", "Spindle or router motor") + opt("type", "laser", ans.type, "Laser", "Diode or CO2 laser on PWM") + opt("type", "router+laser", ans.type, "Router + laser", "Laser next to the spindle, M6 T2 selects it") +
        opt("type", "plotter", ans.type, "Plotter / drag knife", "No spindle") + opt("type", "plasma", ans.type, "Plasma", "Torch on a relay output") + "</div>" +
        "<h5>Axes</h5>" + seg("nAxes", ans.nAxes, [[3, "3 axes: X Y Z"], [4, "4 axes: X Y Z A"]]) +
        '<div class="grid" style="max-width:420px">' + txt("machineName", ans.machineName, "Machine name (for the config name)", ' placeholder="3018"') + "</div>" +
        '<p class="lead" style="margin-top:12px">Cartesian kinematics (CoreXY comes later).</p>';
    },
    blocks() {
      const b = S.BOARDS[ans.board];
      const sel = {}, lims = {};
      ans.axes.slice(0, ans.nAxes).forEach((x) => { sel[x.block] = (sel[x.block] ? sel[x.block] + "+" : "") + x.axis.toUpperCase(); if (x.limitBlock !== x.block) lims[x.limitBlock] = x.axis.toUpperCase(); });
      const bad = Object.entries(b.blocks).filter(([, v]) => v.unusable);
      return "<h2>Which block drives which axis?</h2><p class=\"lead\">Follow the real wiring: blocks can be swapped freely, e.g. Z on the A block. The limit switch of an axis can sit on a different block than its driver (pins 8/7 of any block).</p>" +
        G.board(b.editable ? Object.assign({}, b, { label: "Your board" }) : b, { sel, limits: lims }) +
        (bad.length ? '<div class="dpcs-note warn">' + bad.map(([k, v]) => "<b>" + k + " block:</b> " + esc(v.unusable)).join("<br>") + "</div>" : "") +
        ans.axes.slice(0, ans.nAxes).map((x, i) => '<div class="axcard"><h6>' + AX(i) + " axis</h6>" +
          '<div class="dpcs-row">driver on ' + seg("axes." + i + ".block", x.block, ["X", "Y", "Z", "A"].map((k) => [k, k + " block", !!b.blocks[k].unusable])) + "</div>" +
          chk("axes." + i + ".limitApart", x.limitApart, "The limit switch is on a different block", "") +
          (x.limitApart ? '<div class="dpcs-row">switch on ' + seg("axes." + i + ".limitBlock", x.limitBlock, ["X", "Y", "Z", "A"].map((k) => [k, k + " block"])) + "</div>" : "") + "</div>").join("");
    },
    moves() {
      return "<h2>What moves: the tool or the bed?</h2><p class=\"lead\"><b>Directions are always the tool relative to the workpiece.</b> +X = tool to the right, +Y = tool away from you, +Z = tool up. On a 3018 the <b>bed</b> moves on Y, so for +Y the bed comes <b>toward</b> you. Judging Y by the bed instead of the tool mirrors every job.</p>" +
        G.topView({ bed: ans.axes[1] && ans.axes[1].moves === "bed" }) +
        ans.axes.slice(0, Math.min(2, ans.nAxes)).map((x, i) => '<div class="axcard"><h6>' + AX(i) + " axis</h6>" + G.movesPick("axes." + i + ".moves", x.moves, x.axis === "x") + "</div>").join("") +
        '<p class="lead">Z: the tool always moves (up = +Z). You check each direction on the machine after flashing (first-run step 3).</p>';
    },
    drivers() {
      const d = S.DRIVERS[ans.driver];
      return "<h2>Stepper drivers</h2><p class=\"lead\">The driver type sets the pulse timing and the highest step rate.</p><div class=\"opts\">" +
        Object.entries(S.DRIVERS).map(([k, v]) => opt("driver", k, ans.driver, v.label, "pulse " + v.pulse + " µs, DIR delay " + v.dirDelay + " µs, up to ~" + Math.round(v.hz / 1000) + " kHz")).join("") + "</div>" +
        (ans.driver === "tb6600" ? '<div class="dpcs-note warn">TB6600: with 2 µs pulses it ignores steps (the motors don\'t move). <code>pulse_us: 5</code> and <code>dir_delay_us: 5</code> are set for it.</div>' : "") +
        "<h5>Wiring (common anode)</h5><p class=\"lead\">The block outputs are open-collector (ULN2803). PUL+, DIR+ and ENA+ go to +5 V (pins 1/3/5); PUL- to pin 4, DIR- to pin 6, ENA- to pin 2. ENA may stay unconnected: the driver is then always enabled.</p>" + G.wiring(d.label) +
        chk("holdIdle", ans.holdIdle, "Keep the motors powered when idle", "Holds position (Z won't drop), motors stay warm. Off: they switch off 250 ms after a move.");
    },
    steps() {
      const d = S.DRIVERS[ans.driver];
      return "<h2>Steps per mm</h2><p class=\"lead\"><b>steps/mm = motor steps per turn × microstepping ÷ mm per turn.</b> Microstepping is set with the driver's DIP switches. Lead is how far the nut moves per turn (T8x8 = 8, T8x4 = 4, T8x2 = 2), not the thread pitch.</p>" + G.leadscrew() +
        '<table class="t"><tr><th>Lead</th><th>1/4</th><th>1/8</th><th>1/16</th></tr><tr><td>2 mm</td><td>400</td><td>800</td><td>1600</td></tr><tr><td>4 mm</td><td>200</td><td>400</td><td>800</td></tr><tr><td>8 mm</td><td>100</td><td>200</td><td>400</td></tr></table>' +
        ans.axes.slice(0, ans.nAxes).map((x, i) => {
          const c = x.calc, calc = S.stepsPerMm(c), spm = +x.spmOverride || calc;
          const cap = S.maxRateFor(spm, d.hz, d.pulse, d.dirDelay);
          let hint = "";
          if (cap < (+x.wantRate || 0)) {
            const per = spm / (c.micro || 1);   // steps/mm per microstep setting (works for a measured value too)
            const better = [1, 2, 4, 8, 16, 32].filter((mm) => mm < c.micro).reverse().find((mm) => S.maxRateFor(per * mm, d.hz, d.pulse, d.dirDelay) >= x.wantRate);
            hint = '<div class="dpcs-note warn">At ' + Math.round(spm * 1000) / 1000 + " steps/mm the " + esc(d.label) + " allows only <b>" + cap + " mm/min</b>" + (better ? ". Microstepping 1/" + better + " would allow " + x.wantRate + "." : ".") + " The config uses " + cap + ".</div>";
          }
          return '<div class="axcard"><h6>' + AX(i) + "</h6>" +
            '<div class="grid">' +
            '<label class="f">Motor steps/turn<select data-k="axes.' + i + '.calc.motorSteps" data-num="1"><option' + (c.motorSteps == 200 ? " selected" : "") + ">200</option><option" + (c.motorSteps == 400 ? " selected" : "") + ">400</option></select></label>" +
            '<label class="f">Microstepping<select data-k="axes.' + i + '.calc.micro" data-num="1">' + [1, 2, 4, 8, 16, 32].map((m) => "<option value=\"" + m + "\"" + (c.micro == m ? " selected" : "") + ">1/" + m + "</option>").join("") + "</select></label>" +
            '<label class="f">Drive<select data-k="axes.' + i + '.calc.drive">' + [["screw", "Leadscrew / ballscrew"], ["belt", "Belt"], ["rack", "Rack and pinion"], ["rotary", "Rotary (degrees)"]].map(([v, l]) => '<option value="' + v + '"' + (c.drive === v ? " selected" : "") + ">" + l + "</option>").join("") + "</select></label>" +
            (c.drive === "belt" ? num("axes." + i + ".calc.teeth", c.teeth || 20, "Pulley teeth") + num("axes." + i + ".calc.pitch", c.pitch || 2, "Belt pitch, mm") : c.drive === "rack" ? num("axes." + i + ".calc.pinionDia", c.pinionDia || 20, "Pinion pitch diameter, mm") : c.drive === "rotary" ? num("axes." + i + ".calc.ratio", c.ratio || 1, "Gear ratio") : num("axes." + i + ".calc.lead", c.lead, "Lead, mm per turn", ' step="any"')) +
            "</div>" +
            '<div class="dpcs-row">calculated <span class="res">' + Math.round(calc * 1000) / 1000 + "</span> steps/" + (c.drive === "rotary" ? "degree" : "mm") + "</div>" +
            '<div class="grid">' + num("axes." + i + ".spmOverride", x.spmOverride, "Measured steps/mm (optional)", ' step="any" placeholder="leave empty"') + num("axes." + i + ".wantRate", x.wantRate, "Wanted max speed, mm/min") + num("axes." + i + ".accel", x.accel, "Acceleration, mm/s²") + "</div>" + hint + "</div>";
        }).join("") +
        '<div class="dpcs-note">After flashing you calibrate over 100 mm (first-run step 4): new = current × commanded ÷ measured. On the 3018 the first guess was 200; the measured value was 1600.</div>';
    },
    switches() {
      const xi = ans.axes.findIndex((x) => x.axis === "x"), yi = ans.axes.findIndex((x) => x.axis === "y"), zi = ans.axes.findIndex((x) => x.axis === "z");
      const bedY = yi >= 0 && ans.axes[yi].moves === "bed";
      const rng = ans.axes.slice(0, ans.nAxes).map((x) => ({ axis: x.axis, from: x.switchAt === "none" ? null : 0, to: x.switchAt === "max" ? -(+x.travel || 0) : +x.travel || 0 }));
      return "<h2>Limit switches and homing</h2><p class=\"lead\">Click where each switch is, along the <b>tool's</b> travel." + (bedY ? " Y moves the bed: a switch that trips with the bed at the back means the tool is at the <b>front</b> = Y minimum." : "") + "</p>" +
        G.topView({ bed: bedY, sw: { x: xi >= 0 ? ans.axes[xi].switchAt : "", y: yi >= 0 ? ans.axes[yi].switchAt : "" }, pick: { x: xi >= 0 ? "axes." + xi + ".switchAt" : null, y: yi >= 0 ? "axes." + yi + ".switchAt" : null } }) +
        '<div class="dpcs-row">' + (xi >= 0 ? '<button type="button" class="dpcs-btn ghost small" data-set="axes.' + xi + '.switchAt" data-val="none">No X switch</button>' : "") + (yi >= 0 ? '<button type="button" class="dpcs-btn ghost small" data-set="axes.' + yi + '.switchAt" data-val="none">No Y switch</button>' : "") + "</div>" +
        (zi >= 0 ? G.sideZ({ sw: ans.axes[zi].switchAt, pick: "axes." + zi + ".switchAt" }) + '<div class="dpcs-row"><button type="button" class="dpcs-btn ghost small" data-set="axes.' + zi + '.switchAt" data-val="none">No Z switch</button></div>' : "") +
        "<h5>Switch type</h5>" + seg("switchType", ans.switchType, [["nc", "NC, normally closed (recommended)"], ["no", "NO, normally open"]]) +
        "<h5>Wiring</h5>" + G.limitWiring() +
        "<h5>Travel</h5><div class=\"grid\">" + ans.axes.slice(0, ans.nAxes).map((x, i) => num("axes." + i + ".travel", x.travel, x.axis.toUpperCase() + " travel, mm")).join("") + "</div>" +
        "<h5>Machine coordinates after homing</h5><p class=\"lead\">The switch is machine 0. Work zero (your stock) must be inside these ranges.</p>" + G.ranges(rng) +
        '<div class="dpcs-note">With switches, the config turns on <code>hard_limits</code> (stop at once if a switch trips during a job) and <code>soft_limits</code> (refuse moves outside the travel). Without hard_limits the machine runs through the switches during jobs.</div>';
    },
    controls() {
      const b = S.BOARDS[ans.board];
      const names = { safety_door_pin: ["Safety door switch", "Stops the job when the door opens"], reset_pin: ["Reset / e-stop button", "Soft reset, like Ctrl-X"], feed_hold_pin: ["Feed hold button", "Pauses the job"], cycle_start_pin: ["Cycle start button", "Resumes the job"] };
      const zSw = ans.axes.find((x) => x.axis === "z" && x.switchAt !== "none");
      const probeClash = b.probe && b.probe.sharedWithZ && zSw;
      return "<h2>Buttons, door and probe</h2><p class=\"lead\">Tick only what is connected. Anything not present becomes <code>NO_PIN</code>: an unconnected door input reads \"open\" and gives <code>error:13 Safety door open</code> and <code>ALARM:11</code> at every start.</p>" +
        b.controls.map((c) => { const pin = c.pin || (b.editable ? ((ans.custom.controls || {})[c.key] || "") : null); const n = names[c.key]; return chk("controls." + c.key, ans.controls[c.key], n[0], pin ? n[1] + " · " + esc(pin) : b.editable ? n[1] + " (enter the pin on the pins page)" : "No input for this on the " + esc(b.short) + ".", !pin && !b.editable); }).join("") +
        "<h5>Probe / touch plate</h5>" + chk("probe", ans.probe, "Touch plate or probe", (b.probe && b.probe.pin ? esc(b.probe.where || b.probe.pin) + " · " : "") + "test with ? after flashing: Pn:P appears when it touches", probeClash) +
        (probeClash ? '<div class="dpcs-note warn">On the R2 the probe shares the Z limit input. With a Z switch configured, the probe can\'t be used. No Z switch: remove it on the switches page.</div>' : "");
    },
    spindles() {
      const b = S.BOARDS[ans.board];
      const outs = Object.entries(b.outputs || {}).filter(([, v]) => v.pin !== undefined);
      const names = { relay: "Relay (on/off)", pwm: "PWM spindle", "10v": "0-10 V spindle", vfd: "RS485 VFD", laser: "Laser" };
      const usedBy = {};
      ans.spindles.forEach((s, i) => { if (s.output) (usedBy[s.output] = usedBy[s.output] || []).push(i); });
      return "<h2>Spindle and laser</h2><p class=\"lead\">Several at once is fine, each on its own output: two spindles can't share an output pin. With a router and a laser, <code>M6 T2</code> selects the laser (dpCREATOR firmware) and any other tool the spindle.</p>" +
        ans.spindles.map((s, i) => '<div class="axcard"><h6>' + names[s.kind] + ' <button type="button" class="dpcs-btn ghost small" data-del="' + i + '" style="float:right">Remove</button></h6>' +
          (s.kind !== "vfd" ? '<div class="dpcs-row">output ' + seg("spindles." + i + ".output", s.output, outs.map(([k, v]) => [k, esc(v.label || k)])) + "</div>" + ((usedBy[s.output] || []).length > 1 ? '<div class="dpcs-note err">This output is used by two spindles. Pick another output.</div>' : "") : '<div class="grid"><label class="f">VFD model<select data-k="spindles.' + i + '.vfd">' + ["Huanyang", "H2A", "H100", "YL620", "NowForever", "SiemensV20", "DanfossVLT2800", "DeltaMS300", "FolinnBD600", "MollomG70"].map((v) => "<option" + (s.vfd === v ? " selected" : "") + ">" + v + "</option>").join("") + "</select></label>" + num("spindles." + i + ".modbusId", s.modbusId, "Modbus ID") + num("spindles." + i + ".baud", s.baud, "Baud") + "</div>" + (b.rs485 ? "" : '<div class="dpcs-note warn">This board has no RS485 port.</div>')) +
          (s.kind === "laser" ? chk("spindles." + i + ".low", s.low, "Output is active-low (:low)", "Check with a meter before connecting the laser (first-run step). A reversed laser fires at full power while idle.") + '<div class="grid">' + num("spindles." + i + ".maxS", s.maxS, "S value for full power") + num("spindles." + i + ".pwmHz", s.pwmHz, "PWM frequency, Hz") + (ans.spindles.some((x) => x.kind !== "laser") ? txt("spindles." + i + ".tools", s.tools, "Selected by tool number(s)") : "") + "</div>" +
            (ans.spindles.some((x) => x.kind !== "laser") ? '<p class="lead" style="margin:8px 0 4px">Laser spot relative to the router bit (offset_mm). Measure: engrave a dot with the laser, plunge a V-bit at the same work zero, measure the distance in X and Y.</p><div class="grid">' + [0, 1, 2].map((k) => num("spindles." + i + ".offset." + k, (s.offset || [0, 0, 0])[k], "XYZ"[k] + " offset, mm", ' step="any"')).join("") + "</div>" : "")
            : s.kind === "vfd" || s.kind === "relay" || s.kind === "pwm" || s.kind === "10v" ? '<div class="grid">' + num("spindles." + i + ".spinup", s.spinup, "Spin-up wait, ms") + num("spindles." + i + ".spindown", s.spindown, "Spin-down wait, ms") + num("spindles." + i + ".maxRpm", s.maxRpm, "Max RPM") + "</div>" : "") + "</div>").join("") +
        '<div class="dpcs-row">add: ' + ["relay", "pwm", "10v", "vfd", "laser"].map((k) => '<button type="button" class="dpcs-btn ghost small" data-add="' + k + '"' + (k === "vfd" && !b.rs485 ? " disabled" : "") + ">" + names[k] + "</button>").join("") + "</div>";
    },
    extras() {
      const b = S.BOARDS[ans.board];
      return "<h2>Pendant, network and SD card</h2>" +
        (b.pendant ? chk("pendant", ans.pendant, "SmartTabUI pendant", "On UART1 (" + esc(b.pendant.tx + " / " + b.pendant.rx) + ", 1 000 000 baud)") : '<p class="lead">This board has no pendant port.</p>') +
        "<h5>Network</h5>" + seg("network", ans.network, [["ethernet", "Wired Ethernet (W5500)", !(b.eth && (b.eth.cs || b.editable))], ["wifi", "Wi-Fi"]]) +
        (ans.network === "ethernet" && b.eth && !b.eth.intWired && b.eth.intLater ? '<div class="dpcs-note"><code>int_pin: NO_PIN</code>: the W5500 is polled. Set it to ' + esc(b.eth.intLater) + " only once the INT wire is really connected: a wrong int_pin brings the link up but no packets pass.</div>" : "") +
        "<h5>SD card</h5>" + chk("sd", ans.sd, "SD card slot", "Shares the SPI bus with the W5500") +
        (ans.sd ? seg("sdHz", ans.sdHz, [[4000000, "4 MHz (default)"], [1000000, "1 MHz (long wires)"]]) + '<div class="dpcs-note">If Ethernet stops working when the SD card is read, the card is holding MISO: about 2 kΩ in series in the card\'s MISO fixes it.</div>' : "");
    },
    review() {
      return "<h2>Review</h2><p class=\"lead\">Every line has a comment saying where it came from. The check below uses the firmware's own list of settings.</p>" +
        '<div id="dpcs-review-probs"><div class="dpcs-note">Checking…</div></div>' +
        '<div class="dpcs-row"><button type="button" class="dpcs-btn" data-use>Use this config</button><button type="button" class="dpcs-btn ghost" data-dl>Download config.yaml</button></div>' +
        '<p class="lead">Without the flasher: upload it in the WebUI under Files as <code>config.yaml</code>, then send <code>$bye</code>.</p>' +
        "<h5>Jump back</h5><div class=\"dpcs-row\">" + SCREENS().map((s, i) => '<button type="button" class="dpcs-btn ghost small" data-go="' + i + '">' + ({ board: "Board", pins: "Pins", machine: "Machine", blocks: "Blocks", moves: "What moves", drivers: "Drivers", steps: "Steps/mm", switches: "Switches", controls: "Buttons", spindles: "Spindle", extras: "Network", review: "Review" }[s]) + "</button>").join("") + "</div>";
    },
  };

  function renderScreen() {
    const m = $("#dpcs-modal");
    const list = SCREENS();
    screen = Math.min(screen, list.length - 1);
    if (ans.board === "generic") { S.BOARDS.generic.chip = ans.customChip || "esp32s3"; S.BOARDS.generic.octal = (ans.customChip || "esp32s3") === "esp32s3"; }
    const q = $(".dpcs-q", m);
    const scroll = q.scrollTop, same = q.dataset.s === list[screen];
    q.innerHTML = RENDER[list[screen]]();
    q.dataset.s = list[screen];
    if (same) q.scrollTop = scroll; else q.scrollTop = 0;
    $(".dpcs-prog", m).innerHTML = list.map((_, i) => "<i" + (i <= screen ? ' class="on"' : "") + "></i>").join("");
    $("[data-back]", m).disabled = screen === 0;
    $("[data-next]", m).classList.toggle("dpcs-hidden", screen === list.length - 1);
    $("footer .sp", m).textContent = "Step " + (screen + 1) + " of " + list.length;
    updateYaml();
  }

  // ------------------------------------------------------------ go
  render();
  stepState();
  schema().catch(() => {});
  window.DPCSetupUI = { openBuilder, useConfig, state: st };
})();
