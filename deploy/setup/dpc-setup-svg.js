/* dpCREATOR machine setup: pictures (inline SVG). Click targets carry
 * data-set="path" data-val="value" and are handled by the builder.
 * Copyright (c) 2026 dpCREATOR (Val). All rights reserved.
 */
(function (root) {
  "use strict";
  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

  // Board with its four axis terminal blocks (pins 1-8). opts: {sel: {X:"x",...}, limits: {...}, pick: "axes.0.block" | null}
  function board(b, opts) {
    opts = opts || {};
    const blocks = ["X", "Y", "Z", "A"];
    let s = '<svg class="dpcs-svg" viewBox="0 0 520 275" role="img" aria-label="' + esc(b.label) + ' with its X, Y, Z and A terminal blocks">';
    s += '<rect x="10" y="10" width="500" height="255" rx="10" class="pcb"/>';
    s += '<text x="26" y="34" class="pcb-t">' + esc(b.label) + "</text>";
    s += '<rect x="360" y="110" width="120" height="90" rx="6" class="chip"/><text x="420" y="160" text-anchor="middle" class="chip-t">ESP32-S3</text>';
    blocks.forEach((k, i) => {
      const x = 30 + i * 80, y = 78;
      const blk = b.blocks[k] || {};
      const bad = !!blk.unusable;
      const who = (opts.sel || {})[k];
      const lim = (opts.limits || {})[k];
      const pick = opts.pick && !bad;
      s += "<g" + (pick ? ' class="tgt" tabindex="0" role="button" data-set="' + esc(opts.pick) + '" data-val="' + k + '" aria-label="' + k + ' block"' : "") + (bad ? ' class="bad"' : "") + ">";
      s += '<rect x="' + x + '" y="' + y + '" width="64" height="150" rx="5" class="blk' + (who ? " on" : "") + (bad ? " off" : "") + '"/>';
      for (let p = 0; p < 8; p++) {
        s += '<rect x="' + (x + 8) + '" y="' + (y + 10 + p * 17) + '" width="14" height="12" rx="2" class="pin"/>';
        s += '<text x="' + (x + 28) + '" y="' + (y + 20 + p * 17) + '" class="pin-t">' + (p + 1) + " " + ["+5V", "ENA", "+5V", "STEP", "+5V", "DIR", "GND*", "LIM"][p] + "</text>";
      }
      s += '<text x="' + (x + 32) + '" y="' + (y - 8) + '" text-anchor="middle" class="blk-t">' + k + " block</text>";
      if (who) s += '<text x="' + (x + 32) + '" y="' + (y + 172) + '" text-anchor="middle" class="ax-t">' + esc(who) + "</text>";
      if (lim) s += '<text x="' + (x + 32) + '" y="' + (y + 186) + '" text-anchor="middle" class="lim-t">limit: ' + esc(lim) + "</text>";
      if (bad) s += '<line x1="' + x + '" y1="' + y + '" x2="' + (x + 64) + '" y2="' + (y + 150) + '" class="x"/><line x1="' + (x + 64) + '" y1="' + y + '" x2="' + x + '" y2="' + (y + 150) + '" class="x"/>';
      s += "</g>";
    });
    s += '<text x="360" y="228" class="pin-t">* pin 7: isolated GND</text><text x="360" y="242" class="pin-t">for the limit switch</text>';
    return s + "</svg>";
  }

  // Top view. bed: Y moves the bed. sw: {x: "min"|"max"|"none", y: ...}. pick: path prefixes {x: "axes.0.switchAt", y: ...}
  function topView(o) {
    o = o || {};
    const bed = o.bed;
    let s = '<svg class="dpcs-svg" viewBox="0 0 520 330" role="img" aria-label="Machine seen from above">';
    s += '<defs><marker id="dpcs-ar" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0L10,5L0,10z" class="ar"/></marker></defs>';
    s += '<rect x="60" y="40" width="400" height="230" rx="6" class="frame"/>';
    s += '<rect x="95" y="' + (bed ? 95 : 70) + '" width="330" height="' + (bed ? 150 : 170) + '" rx="4" class="bed' + (bed ? " moving" : "") + '"/>';
    s += '<text x="' + (bed ? 330 : 260) + '" y="' + (bed ? 112 : 232) + '" text-anchor="middle" class="lbl">' + (bed ? "bed (moves front-back)" : "table") + "</text>";
    s += '<rect x="70" y="140" width="380" height="22" rx="3" class="gantry"/>';
    s += '<circle cx="260" cy="151" r="15" class="tool"/><text x="260" y="156" text-anchor="middle" class="tool-t">tool</text>';
    // +X / +Y arrows (tool motion)
    s += '<line x1="285" y1="151" x2="345" y2="151" class="arw" marker-end="url(#dpcs-ar)"/><text x="350" y="146" class="arw-t">+X tool right</text>';
    s += '<line x1="260" y1="126" x2="260" y2="88" class="arw" marker-end="url(#dpcs-ar)"/><text x="268" y="96" class="arw-t">+Y tool away</text>';
    if (bed) s += '<line x1="140" y1="196" x2="140" y2="246" class="arw bedarw" marker-end="url(#dpcs-ar)"/><text x="150" y="226" class="arw-t">+Y: bed comes toward you</text>';
    s += '<text x="260" y="318" text-anchor="middle" class="lbl">front (you stand here)</text>';
    const spot = (cx, cy, path, val, on, label) =>
      '<g class="tgt' + (on ? " on" : "") + '" tabindex="0" role="button" aria-pressed="' + (on ? "true" : "false") + '" data-set="' + path + '" data-val="' + val + '" aria-label="' + esc(label) + '"><circle cx="' + cx + '" cy="' + cy + '" r="14" class="sw"/><text x="' + cx + '" y="' + (cy + 4) + '" text-anchor="middle" class="sw-t">' + (on ? "✓" : "") + "</text></g>";
    if (o.pick && o.pick.x) {
      s += spot(48, 151, o.pick.x, "min", o.sw.x === "min", "X switch at the left");
      s += spot(472, 151, o.pick.x, "max", o.sw.x === "max", "X switch at the right");
      s += '<text x="48" y="182" text-anchor="middle" class="lbl">X left</text><text x="472" y="182" text-anchor="middle" class="lbl">X right</text>';
    }
    if (o.pick && o.pick.y) {
      s += spot(410, 28, o.pick.y, "max", o.sw.y === "max", "Y switch: tool at the back");
      s += spot(410, 284, o.pick.y, "min", o.sw.y === "min", "Y switch: tool at the front");
      s += '<text x="432" y="32" class="lbl">tool at the back</text><text x="432" y="288" class="lbl">tool at the front</text>';
    }
    return s + "</svg>";
  }

  // Side view for Z with switch top / bottom
  function sideZ(o) {
    o = o || {};
    let s = '<svg class="dpcs-svg dpcs-svg-z" viewBox="0 0 220 300" role="img" aria-label="Z axis seen from the side">';
    s += '<defs><marker id="dpcs-arz" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0L10,5L0,10z" class="ar"/></marker></defs>';
    s += '<rect x="80" y="30" width="16" height="220" class="frame"/>';
    s += '<rect x="96" y="110" width="50" height="70" rx="6" class="gantry"/><rect x="113" y="180" width="16" height="40" class="tool"/>';
    s += '<rect x="30" y="262" width="170" height="14" class="bed"/><text x="115" y="294" text-anchor="middle" class="lbl">workpiece</text>';
    s += '<line x1="170" y1="160" x2="170" y2="110" class="arw" marker-end="url(#dpcs-arz)"/><text x="160" y="100" class="arw-t">+Z up</text>';
    const spot = (cy, val, label) => '<g class="tgt' + (o.sw === val ? " on" : "") + '" tabindex="0" role="button" aria-pressed="' + (o.sw === val ? "true" : "false") + '" data-set="' + o.pick + '" data-val="' + val + '" aria-label="' + label + '"><circle cx="58" cy="' + cy + '" r="14" class="sw"/><text x="58" y="' + (cy + 4) + '" text-anchor="middle" class="sw-t">' + (o.sw === val ? "✓" : "") + "</text></g>";
    if (o.pick) {
      s += spot(40, "max", "Z switch at the top") + '<text x="20" y="18" class="lbl">switch at the top</text>';
      s += spot(240, "min", "Z switch at the bottom") + '<text x="6" y="230" class="lbl">bottom</text>';
    }
    return s + "</svg>";
  }

  // What moves: gantry vs moving bed, small
  function movesPick(path, val, horizontal) {
    const card = (v, title, bed) => {
      let s = '<button type="button" class="dpcs-pic' + (val === v ? " on" : "") + '" data-set="' + path + '" data-val="' + v + '" aria-pressed="' + (val === v ? "true" : "false") + '">';
      s += '<svg viewBox="0 0 200 120" aria-hidden="true"><rect x="10" y="10" width="180" height="100" rx="4" class="frame"/>';
      s += '<rect x="30" y="' + (bed ? 40 : 30) + '" width="140" height="' + (bed ? 55 : 70) + '" rx="3" class="bed' + (bed ? " moving" : "") + '"/>';
      s += '<rect x="16" y="56" width="168" height="10" class="gantry"/><circle cx="100" cy="61" r="8" class="tool"/>';
      if (horizontal) {
        if (bed) s += '<path d="M60,84 h-26" class="arw" marker-end="url(#dpcs-ar2)"/><path d="M140,84 h26" class="arw" marker-end="url(#dpcs-ar2)"/>';
        else s += '<path d="M92,61 h-34" class="arw" marker-end="url(#dpcs-ar2)"/><path d="M108,61 h34" class="arw" marker-end="url(#dpcs-ar2)"/>';
      } else if (bed) s += '<path d="M60,74 v26" class="arw" marker-end="url(#dpcs-ar2)"/><path d="M60,52 v-14" class="arw" marker-end="url(#dpcs-ar2)"/>';
      else s += '<path d="M150,61 v-24" class="arw" marker-end="url(#dpcs-ar2)"/><path d="M150,61 v24" class="arw" marker-end="url(#dpcs-ar2)"/>';
      s += '<defs><marker id="dpcs-ar2" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0L10,5L0,10z" class="ar"/></marker></defs></svg>';
      return s + "<span>" + title + "</span></button>";
    };
    return '<div class="dpcs-pics">' + card("tool", horizontal ? "The tool moves left-right" : "The tool (gantry) moves", false) + card("bed", horizontal ? "The bed moves left-right" : "The bed moves under the tool", true) + "</div>";
  }

  // Common-anode wiring: block pins -> TB6600-style driver
  function wiring(drvLabel) {
    let s = '<svg class="dpcs-svg" viewBox="0 0 520 220" role="img" aria-label="Wiring from a terminal block to the driver, common anode">';
    s += '<rect x="20" y="20" width="110" height="180" rx="6" class="blk"/><text x="75" y="14" text-anchor="middle" class="blk-t">axis block</text>';
    const L = ["1 +5V", "2 ENA", "3 +5V", "4 STEP", "5 +5V", "6 DIR"];
    L.forEach((t, i) => { s += '<text x="34" y="' + (48 + i * 26) + '" class="pin-t big">' + t + "</text>"; });
    s += '<rect x="370" y="20" width="130" height="180" rx="6" class="drv"/><text x="435" y="14" text-anchor="middle" class="blk-t">' + esc(drvLabel) + "</text>";
    const R = ["ENA+", "ENA-", "PUL+", "PUL-", "DIR+", "DIR-"];
    R.forEach((t, i) => { s += '<text x="386" y="' + (48 + i * 26) + '" class="pin-t big">' + t + "</text>"; });
    // pin rows: block 1(+5V)->ENA+, 2(ENA)->ENA-, 3->PUL+, 4 STEP->PUL-, 5->DIR+, 6 DIR->DIR-
    const pairs = [[0, 0, "pos"], [1, 1, "sig"], [2, 2, "pos"], [3, 3, "sig"], [4, 4, "pos"], [5, 5, "sig"]];
    pairs.forEach(([a, b, k]) => { s += '<path d="M128,' + (44 + a * 26) + " C250," + (44 + a * 26) + " 250," + (44 + b * 26) + " 372," + (44 + b * 26) + '" class="wire ' + k + '"/>'; });
    s += '<text x="250" y="212" text-anchor="middle" class="lbl">red: +5 V to every "+" input · blue: STEP, DIR and ENA to the "-" inputs</text>';
    return s + "</svg>";
  }

  // NC switch between pins 8 and 7
  function limitWiring() {
    let s = '<svg class="dpcs-svg" viewBox="0 0 520 170" role="img" aria-label="Limit switch wired between pins 8 and 7 of a block, not to board ground">';
    s += '<rect x="20" y="20" width="120" height="120" rx="6" class="blk"/><text x="80" y="14" text-anchor="middle" class="blk-t">axis block</text>';
    s += '<text x="34" y="62" class="pin-t big">7 GND (isolated)</text><text x="34" y="100" class="pin-t big">8 LIM</text>';
    s += '<path d="M138,58 H290 V70" class="wire sig"/><path d="M138,96 H250 V110 H330 V70" class="wire sig"/>';
    s += '<g class="sw-sym"><line x1="290" y1="70" x2="330" y2="70" class="wire pos"/><circle cx="290" cy="70" r="3"/><circle cx="330" cy="70" r="3"/></g>';
    s += '<text x="310" y="54" text-anchor="middle" class="lbl">NC switch (closed at rest)</text>';
    s += '<path d="M400,58 H470" class="wire bad"/><text x="405" y="92" class="lbl">board GND: never</text><line x1="420" y1="70" x2="450" y2="40" class="x"/><line x1="420" y1="40" x2="450" y2="70" class="x"/>';
    s += '<text x="260" y="160" text-anchor="middle" class="lbl">The inputs are opto-isolated: pin 7 is their own ground. A switch to board GND never works.</text>';
    return s + "</svg>";
  }

  // Leadscrew lead vs pitch
  function leadscrew() {
    let s = '<svg class="dpcs-svg dpcs-svg-sm" viewBox="0 0 420 110" role="img" aria-label="Lead is how far the nut moves in one turn">';
    s += '<rect x="20" y="45" width="380" height="20" rx="3" class="frame"/>';
    for (let i = 0; i < 19; i++) s += '<line x1="' + (30 + i * 20) + '" y1="45" x2="' + (40 + i * 20) + '" y2="65" class="thread"/>';
    s += '<rect x="170" y="35" width="50" height="40" rx="4" class="gantry"/>';
    s += '<path d="M170,92 H250" class="arw" marker-end="url(#dpcs-ar3)"/><defs><marker id="dpcs-ar3" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0L10,5L0,10z" class="ar"/></marker></defs>';
    s += '<text x="260" y="96" class="lbl">lead = mm the nut moves per turn (T8x8: 8, T8x2: 2)</text>';
    return s + "</svg>";
  }

  // Ranges after homing
  function ranges(list) {
    let s = '<svg class="dpcs-svg dpcs-svg-sm" viewBox="0 0 420 ' + (30 + list.length * 34) + '" role="img" aria-label="Machine coordinate range of each axis after homing">';
    list.forEach((r, i) => {
      const y = 22 + i * 34;
      s += '<text x="10" y="' + (y + 5) + '" class="blk-t">' + r.axis.toUpperCase() + "</text>";
      s += '<line x1="40" y1="' + y + '" x2="400" y2="' + y + '" class="track"/>';
      if (r.from === null) { s += '<text x="220" y="' + (y + 5) + '" text-anchor="middle" class="lbl">no homing switch</text>'; return; }
      const zeroX = r.to < 0 ? 400 : 40, endX = r.to < 0 ? 40 : 400;
      s += '<circle cx="' + zeroX + '" cy="' + y + '" r="5" class="tool"/><text x="' + zeroX + '" y="' + (y - 8) + '" text-anchor="middle" class="lbl">0 (switch)</text>';
      s += '<text x="' + endX + '" y="' + (y - 8) + '" text-anchor="middle" class="lbl">' + (r.to > 0 ? "+" : "") + r.to + "</text>";
    });
    return s + "</svg>";
  }

  root.DPCSetupSVG = { board, topView, sideZ, movesPick, wiring, limitWiring, leadscrew, ranges };
})(typeof window !== "undefined" ? window : globalThis);
