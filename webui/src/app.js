"use strict";
// UI glue: DRO, jogging, machine buttons, overrides, files, console, job progress.
(() => {
  const AXES = ["X", "Y", "Z", "A", "B", "C"];
  const S = {
    state: "Unknown", mpos: [0, 0, 0], wco: [0, 0, 0], nAxes: 3, pins: "", acc: "",
    alarm: null, ov: [100, 100, 100], job: null, fs: "sd", dir: "/", sel: null,
  };
  const idle = () => S.state === "Idle";
  const canJog = () => S.state === "Idle" || S.state === "Jog";

  // ------------------------------------------------------------- console
  const logEl = $("#log");
  const showStatus = $("#show-status");
  function log(text, cls = "") {
    const atBottom = logEl.scrollHeight - logEl.scrollTop - logEl.clientHeight < 30;
    const line = el("span", cls ? { class: cls } : {}, text + "\n");
    logEl.append(line);
    while (logEl.childNodes.length > 1500) logEl.firstChild.remove();
    if (atBottom) logEl.scrollTop = logEl.scrollHeight;
  }
  Conn.on("tx", (t) => log("> " + t, "tx"));
  $("#clear-console").onclick = () => (logEl.textContent = "");

  const hist = Prefs.get("history", []);
  let histPos = hist.length;
  $("#cmd-form").onsubmit = (e) => {
    e.preventDefault();
    const input = $("#cmd");
    const t = input.value.trim();
    if (!t) return;
    send(t);
    if (hist[hist.length - 1] !== t) hist.push(t);
    while (hist.length > 100) hist.shift();
    Prefs.set("history", hist);
    histPos = hist.length;
    input.value = "";
  };
  $("#cmd").addEventListener("keydown", (e) => {
    if (e.key === "ArrowUp" && histPos > 0) { e.target.value = hist[--histPos]; e.preventDefault(); }
    if (e.key === "ArrowDown") { histPos = Math.min(hist.length, histPos + 1); e.target.value = hist[histPos] || ""; e.preventDefault(); }
  });

  function send(line) {
    if (!Conn.sendLine(line)) toast("Not connected", true);
  }

  // ------------------------------------------------------------- incoming lines
  let ipWaiter = null;  // [ESP111] answer for the PC-drive script
  Conn.on("line", (line) => {
    if (ipWaiter && /^\d{1,3}(\.\d{1,3}){3}$/.test(line.trim())) {
      ipWaiter(line.trim());
      ipWaiter = null;
      return;
    }
    if (line[0] === "<") {
      if (showStatus.checked) log(line, "st");
      return;
    }
    if (line === "ok") return;
    let m;
    if ((m = /^error:(\d+)/.exec(line))) {
      const desc = ERRORS[m[1]] || "";
      log(line + (desc ? "  (" + desc + ")" : ""), "err");
      toast("Error " + m[1] + (desc ? ": " + desc : ""), true);
      return;
    }
    if ((m = /^ALARM:(\d+)/i.exec(line))) {
      S.alarm = Number(m[1]);
      log(line + "  (" + (ALARMS[m[1]] || "alarm") + ")", "err");
      renderAlarm();
      return;
    }
    if ((m = /^\[GC:(.*)\]/.exec(line))) {
      const wcs = /G5[4-9](\.\d)?/.exec(m[1]);
      if (wcs) $("#wcs").value = wcs[0].slice(0, 3);
      log(line, "msg");
      return;
    }
    if ((m = /^\[MSG:Crash:(.*)\]$/.exec(line))) {
      // Last restart was a crash: show it with the backtrace to copy
      const info = m[1];
      if (info !== "none") {
        $("#crash-text").textContent = info === "unknown" ? "(no backtrace saved)" : info;
        $("#crash").hidden = false;
      }
      log(line, info === "none" ? "msg" : "err");
      return;
    }
    if ((m = /^\[VER:.*FluidNC (.*)\]?/.exec(line))) {
      checkUiVersion(m[1]);
      log(line, "msg");
      return;
    }
    if ((m = /^\[MSG:Rewind(Err)?:(.*)\]$/.exec(line))) {
      Rewind.onMsg(m[1] || "", m[2]);
      log(line, m[1] ? "err" : "msg");
      return;
    }
    if ((m = /^\[MSG:PathRetrace:/.exec(line))) return;  // old plugin
    if ((m = /^\[MSG:ZAdjust(Err|Info|Limit)?:(.*)\]$/.exec(line))) {
      ZAdj.onMsg(m[1] || "", m[2]);
      log(line, m[1] === "Err" ? "err" : "msg");
      return;
    }
    if ((m = /^\[MSG:Recovery(Err|Info|Ready)?:(.*)\]$/.exec(line))) {
      Recovery.onMsg(m[1] || "", m[2]);
      log(line, m[1] === "Err" ? "err" : "msg");
      return;
    }
    if ((m = /^\[MSG:Prepared:(\w+):(.*)\]$/.exec(line))) {
      setPrepared(m[1], m[2]);
      log(line, "msg");
      return;
    }
    if ((m = /^\[MSG:JobWait:(.*)\]$/.exec(line))) {
      // The board holds a job until the pendant shows its preview.
      toast("Job: " + m[1], m[1].includes("cancel"));
      log(line, "msg");
      return;
    }
    if ((m = /^\[MSG:Pendant:(\w+)\]$/.exec(line))) {
      setPendant(m[1]);
      return;
    }
    if ((m = /^\[MSG:Viz(Auto(?:Ready|Busy|Err|Queued)|Ready|Busy|Err|Deleted|Status):?(.*)\]$/.exec(line))) {
      onVizMsg(m[1], m[2]);
      log(line, m[1].endsWith("Err") ? "err" : "msg");
      return;
    }
    if (/^Grbl |^\[VER:/.test(line) && line.startsWith("Grbl")) {
      log(line, "msg");
      setTimeout(Conn.resync, 300);
      return;
    }
    log(line, line[0] === "[" ? "msg" : "");
  });

  // ------------------------------------------------------------- connection state
  Conn.on("state", (s) => {
    const c = $("#conn");
    c.textContent = s === "open" ? "online" : s === "connecting" ? "connecting…" : "offline";
    c.className = "conn conn-" + s;
    if (s !== "open") { setStateBadge("Unknown"); $("#pendant").hidden = true; }
  });

  // TabUI pendant on uart_channel1 ($Pendant/Status, plus a message on change)
  function setPendant(st) {
    const p = $("#pendant");
    p.hidden = st === "none";
    p.className = "pendant pendant-" + st;
    p.textContent = st === "connected" ? "Pendant" : "Pendant offline";
    p.title = st === "connected" ? "TabUI pendant connected (UART1)" : "No TabUI pendant on UART1";
  }

  var recovHook = null;  // var: hoisted, set by the recovery panel
  var rwHook = null;     // var: hoisted, set by the rewind bar
  var rwLineNow = 0;     // line being retraced (3D marker)
  function setStateBadge(state) {
    S.state = state;
    zadjEnable();
    if (recovHook) recovHook();
    if (rwHook) rwHook();
    const b = $("#state");
    b.textContent = state === "Unknown" ? "--" : state;
    b.className = "state state-" + state.toLowerCase();
  }
  function renderAlarm() {
    const a = $("#alarm");
    if (S.state === "Alarm") {
      a.hidden = false;
      a.textContent = S.alarm ? `Alarm ${S.alarm}: ${ALARMS[S.alarm] || ""}` : "Alarm - unlock ($X) or home";
    } else {
      a.hidden = true;
      S.alarm = null;
    }
  }

  // ------------------------------------------------------------- DRO
  const dro = $("#dro");
  function buildDro(n) {
    dro.textContent = "";
    for (let i = 0; i < n; i++) {
      const ax = AXES[i];
      dro.append(
        el("div", { class: "axis", id: "ax-" + ax },
          el("span", { class: "name" }, ax),
          el("span", { class: "wpos" }, "0.000"),
          el("span", { class: "mpos" }, "0.000"),
          el("button", { class: "btn z0", title: `Set ${ax} work zero here`, onclick: () => zeroAxis(ax) }, ax + "0"))
      );
    }
  }
  buildDro(3);

  function zeroAxis(ax) {
    if (!idle()) return toast("Machine must be idle", true);
    send(`G10 L20 P0 ${ax}0`);
  }

  Conn.on("status", (st) => {
    S.state = st.state;
    setStateBadge(st.state);
    renderAlarm();
    if (st.wco) S.wco = st.wco;
    let m = st.mpos, w = st.wpos;
    if (m) w = m.map((v, i) => v - (S.wco[i] || 0));
    else if (w) m = w.map((v, i) => v + (S.wco[i] || 0));
    if (!m) return;
    S.mpos = m;
    if (m.length !== S.nAxes) { S.nAxes = m.length; buildDro(m.length); }
    for (let i = 0; i < m.length; i++) {
      const row = $("#ax-" + AXES[i]);
      row.children[1].textContent = fmt(w[i]);
      row.children[2].textContent = fmt(m[i]);
      row.classList.toggle("limit", st.pins.includes(AXES[i]));
    }
    Viewer.setTool(w[0], w[1], w[2]);
    Monitor.status(st, m);
    $("#pins").textContent = st.pins ? "Inputs active: " + st.pins.split("").join(" ") : "";

    if (st.ov) {
      S.ov = st.ov;
      $("#ov-f").textContent = st.ov[0] + "%";
      $("#ov-r").textContent = st.ov[1] + "%";
      $("#ov-s").textContent = st.ov[2] + "%";
    }
    if (st.feed !== undefined) $("#feedspeed").textContent = `F ${Math.round(st.feed)} · S ${Math.round(st.speed || 0)}`;
    $("#btn-flood").classList.toggle("on", st.acc.includes("F"));
    $("#btn-mist").classList.toggle("on", st.acc.includes("M"));
    $$('[data-cmd="spindle-cw"]').forEach((b) => b.classList.toggle("on", st.acc.includes("S")));
    jobStatus(st, w);
  });

  // ------------------------------------------------------------- job progress
  function jobStatus(st, wpos) {
    const running = st.sdPct !== undefined && !Number.isNaN(st.sdPct);
    const jobEl = $("#job");
    if (running) {
      if (!S.job || S.job.file !== st.sdFile) S.job = { file: st.sdFile, start: Date.now() };
      ensurePreview(st.sdFile);
      const filePct = Math.max(0, Math.min(100, st.sdPct));
      // Accurate %: where the tool is on the parsed path, weighted by estimated
      // time. Falls back to FluidNC's file-read % (runs ahead of the tool by
      // the planner buffer, and counts bytes, not machining time).
      const [jfs, jpath] = jobFsPath(st.sdFile);
      const onScreen = Viewer.job && $("#viewer-file").dataset.path === cacheKey(jfs, jpath);
      const p = onScreen ? Viewer.setProgress(filePct / 100, wpos, rwLineNow > 0 ? rwLineNow : st.line) : null;
      const pct = p === null ? filePct : p * 100;
      const elapsed = (Date.now() - S.job.start) / 1000;
      // Page opened mid-job: measure the rate from the first % we saw.
      if (S.job.p0 === undefined) S.job.p0 = pct;
      const done = pct - S.job.p0;
      const eta = done > 0.3 && elapsed > 5 ? (elapsed * (100 - pct)) / done : NaN;
      jobEl.hidden = false;
      jobEl.textContent = `${st.sdFile.split("/").pop()} · ${pct.toFixed(1)}% · ${fmtTime(elapsed)} · ETA ${fmtTime(eta)}`;
      jobEl.title = `Tool position on the path: ${p === null ? "n/a" : (p * 100).toFixed(1) + "%"} · file read by FluidNC: ${filePct.toFixed(1)}%`;
      $("#progress-bar").style.width = pct + "%";
    } else if (S.job) {
      // job finished or stopped
      if (idle()) {
        log(`Job ended: ${S.job.file} (${fmtTime((Date.now() - S.job.start) / 1000)})`, "msg");
        if ($("#viewer-file").dataset.partial) ensurePreview(S.job.file);
        S.job = null;
        zadjEnable();
        jobEl.hidden = true;
      }
    }
  }

  // ------------------------------------------------------------- machine buttons
  const actions = {
    home: () => send("$H"),
    unlock: () => send("$X"),
    reset: () => Conn.realtime(0x18),
    hold: () => Conn.realtime(0x21),     // !
    resume: () => Conn.realtime(0x7e),   // ~
    stop: () => { Conn.realtime(0x21); setTimeout(() => Conn.realtime(0x18), 250); },
    "jog-cancel": () => Conn.realtime(0x85),
    "spindle-cw": () => send("M3 S" + (Number($("#rpm").value) || 0)),
    "spindle-off": () => send("M5"),
    flood: () => Conn.realtime(0xa0),
    mist: () => Conn.realtime(0xa1),
    "zero-all": () => {
      if (!idle()) return toast("Machine must be idle", true);
      send("G10 L20 P0 " + AXES.slice(0, S.nAxes).map((a) => a + "0").join(" "));
    },
    "goto-xy0": () => { if (!idle()) return toast("Machine must be idle", true); send("G90 G0 X0 Y0"); },
    "goto-z0": () => { if (!idle()) return toast("Machine must be idle", true); send("G90 G0 Z0"); },
  };
  $$("[data-cmd]").forEach((b) => b.addEventListener("click", () => actions[b.dataset.cmd]()));
  $$("[data-ovr]").forEach((b) => b.addEventListener("click", () => Conn.realtime(parseInt(b.dataset.ovr, 16))));
  $("#wcs").addEventListener("change", (e) => { send(e.target.value); send("$G"); });

  // ------------------------------------------------------------- jogging
  let step = Prefs.get("step", "1");
  const feedInput = $("#jogfeed");
  feedInput.value = Prefs.get("jogfeed", 1500);
  feedInput.onchange = () => Prefs.set("jogfeed", Number(feedInput.value));
  function setStep(v) {
    step = v;
    Prefs.set("step", v);
    $$("#step button").forEach((b) => b.classList.toggle("active", b.dataset.step === v));
  }
  setStep(step);
  $$("#step button").forEach((b) => (b.onclick = () => setStep(b.dataset.step)));

  // "X1 Y-1" -> {X:1, Y:-1}
  const dirs = (spec) => Object.fromEntries(spec.split(" ").map((t) => [t[0], Number(t.slice(1))]));

  function jog(spec, distance) {
    if (!canJog()) { toast(`Can't jog while ${S.state}`, true); return false; }
    const f = Math.max(10, Number(feedInput.value) || 1000);
    const words = Object.entries(dirs(spec)).map(([a, d]) => a + (d * distance).toFixed(4)).join(" ");
    Conn.sendLine(`$J=G91 G21 ${words} F${f}`, true);
    return true;
  }
  let continuous = false;
  function jogStart(spec) {
    if (step === "cont") {
      continuous = jog(spec, 10000) ; // long move; cancelled on release
    } else {
      jog(spec, Number(step));
    }
  }
  function jogStop() {
    if (continuous) { Conn.realtime(0x85); continuous = false; }
  }
  $$("[data-jog]").forEach((b) => {
    b.addEventListener("pointerdown", (e) => { e.preventDefault(); b.setPointerCapture(e.pointerId); jogStart(b.dataset.jog); });
    b.addEventListener("pointerup", jogStop);
    b.addEventListener("pointercancel", jogStop);
    b.addEventListener("lostpointercapture", jogStop);
  });
  // safety: stop continuous jog whenever focus is lost
  window.addEventListener("blur", jogStop);
  document.addEventListener("visibilitychange", () => document.hidden && jogStop());
  Conn.on("state", (s) => s !== "open" && (continuous = false));

  const kb = $("#kbjog");
  kb.checked = false;
  const keyMap = { ArrowLeft: "X-1", ArrowRight: "X1", ArrowUp: "Y1", ArrowDown: "Y-1", PageUp: "Z1", PageDown: "Z-1" };
  document.addEventListener("keydown", (e) => {
    if (!kb.checked || !keyMap[e.key] || /INPUT|TEXTAREA|SELECT/.test(document.activeElement.tagName)) return;
    e.preventDefault();
    if (e.repeat) return;
    jogStart(keyMap[e.key]);
  });
  document.addEventListener("keyup", (e) => { if (keyMap[e.key]) jogStop(); });

  // ------------------------------------------------------------- viewer
  Viewer.init($("#viewer"));
  $$("#views button").forEach((b) => (b.onclick = () => {
    $$("#views button").forEach((x) => x.classList.toggle("active", x === b));
    Viewer.view(b.dataset.v);
  }));
  $("#fit").onclick = () => Viewer.fit();

  // Parsed files are kept (last 3) so re-opening or running a file is instant.
  const parsedCache = new Map();
  const cacheKey = (fs, path) => fs + ":" + path;
  const forget = (fs, path) => parsedCache.delete(cacheKey(fs, path));
  const moving = () => ["Run", "Jog", "Home"].includes(S.state);

  function showJob(job, key, partial) {
    const label = $("#viewer-file");
    Viewer.load(job);
    const name = key.slice(key.indexOf(":") + 1).split("/").pop();
    label.textContent = name + (partial ? " · preview" : "");
    label.dataset.path = key;
    label.dataset.partial = partial ? "1" : "";
    $("#load-full").hidden = !partial;
    const size = job.max.map((v, i) => v - job.min[i]);
    $("#viewer-info").textContent = partial
      ? `${size.map((v) => v.toFixed(1)).join(" × ")} mm · simplified path from the .viz · ` +
        "the full 3D path loads when the machine stops"
      : `${size.map((v) => v.toFixed(1)).join(" × ")} mm · Z ${job.min[2].toFixed(2)}…${job.max[2].toFixed(2)}` +
        ` · cut ${(job.feedLen / 1000).toFixed(2)} m · est. ${fmtTime(job.estMinutes * 60)}` +
        (job.tools ? ` · ${job.tools} tool change(s)` : "");
  }

  async function previewFile(fs, path) {
    const key = cacheKey(fs, path);
    const label = $("#viewer-file");
    let job = parsedCache.get(key);
    if (!job) {
      label.textContent = "Loading " + path + "…";
      const text = await Files.getText(fs, path);
      label.textContent = "Parsing " + path + "…";
      job = await Gcode.parse(text, {
        onProgress: (f) => (label.textContent = `Parsing ${path}… ${Math.round(f * 100)}%`),
      });
      parsedCache.set(key, job);
      while (parsedCache.size > 3) parsedCache.delete(parsedCache.keys().next().value);
    }
    showJob(job, key, false);
    $("#progress-bar").style.width = "0%";
  }

  // "/sd/a.nc" -> ["sd", "/a.nc"]; "/littlefs/a.nc" -> ["flash", "/a.nc"]
  function jobFsPath(file) {
    const m = /^\/(sd|littlefs|spiffs|localfs)(\/.*)$/i.exec(file);
    if (!m) return ["sd", file.startsWith("/") ? file : "/" + file];
    return [m[1].toLowerCase() === "sd" ? "sd" : "flash", m[2]];
  }

  // Make sure the running job's path is on screen. Called on every status
  // report while a job runs; does at most one fetch at a time. While the
  // machine moves it only fetches the small pendant .viz (FluidNC avoids
  // serving big files mid-cut, and the job reads from the same card); the
  // full 3D path is loaded as soon as the machine stops (hold, end of job).
  let previewBusy = false, previewRetryAt = 0;
  async function ensurePreview(file) {
    if (!file || previewBusy || Date.now() < previewRetryAt) return;
    const [fs, path] = jobFsPath(file);
    const key = cacheKey(fs, path);
    const cur = $("#viewer-file").dataset;
    const shown = Viewer.job && cur.path === key;
    if (shown && !cur.partial) return;
    if (shown && moving()) return;  // 2D outline already up; wait for a stop
    previewBusy = true;
    try {
      if (parsedCache.has(key) || !moving()) {
        await previewFile(fs, path);
      } else {
        let job = null;
        if (fs === "sd") job = Gcode.fromViz(await Files.getText("sd", path + ".viz").catch(() => ""));
        if (job && job.feed.length) {
          showJob(job, key, true);
        } else {
          Viewer.clear();
          $("#viewer-file").textContent = key.split("/").pop();
          $("#viewer-file").dataset.path = key;
          $("#viewer-file").dataset.partial = "1";
          $("#load-full").hidden = false;
          $("#viewer-info").textContent = "No pendant .viz for this file - the path loads when the machine stops, or press Load path";
          previewRetryAt = Date.now() + 4000;  // FluidNC is building it; look again shortly
        }
      }
    } catch (e) {
      $("#viewer-info").textContent = "Could not load the job's path: " + e.message + " (retrying)";
      previewRetryAt = Date.now() + 10000;
    } finally {
      previewBusy = false;
    }
  }
  $("#load-full").onclick = () => {
    const key = $("#viewer-file").dataset.path;
    if (!key) return;
    if (moving() && !confirm("Load the full path now? This reads the whole file from the SD card while the job is running from it; on large files it can slow the job. Otherwise it loads by itself when the machine stops.")) return;
    const fs = key.slice(0, key.indexOf(":")), path = key.slice(key.indexOf(":") + 1);
    previewBusy = true;
    previewFile(fs, path).catch((e) => toast(e.message, true)).finally(() => (previewBusy = false));
  };

  // ------------------------------------------------------------- TabUI pendant .viz
  // The pendant shows a preview from "<file>.viz" next to the G-code on the SD card.
  // FluidNC's VizGenerator builds it on request ($Viz/Generate=/sd/<file>), a few
  // milliseconds at a time, so the controller keeps answering meanwhile.
  const GCODE_RE = /\.(nc|gcode|gc|ngc|tap|cnc|g)$/i;
  // The firmware queues the build itself whenever G-code lands on /sd (this UI,
  // a mapped network drive, the classic UI). Its messages say VizAuto* so the
  // pendant, which loads any "VizReady" it sees, is not disturbed.
  function onVizMsg(kind, rest) {
    const auto = kind.startsWith("Auto");
    const k = auto ? kind.slice(4) : kind;
    const name = rest.split(":")[0].replace(/^.*\//, "").replace(/\.viz$/, "");
    const ncPath = rest.split(":")[0].replace(/\.viz$/, "");
    if (k === "Queued" || k === "Busy") setVizState(ncPath, "building");
    else if (k === "Ready") setVizState(ncPath, "ok");
    else if (k === "Err") { const m = /(\/sd\/[^:()]*?)(\.viz)?(?: \(|:|$)/i.exec(rest); if (m) setVizState(m[1].trim(), "missing"); }
    if (k === "Queued") {
      $("#viz-status").textContent = moving() || S.job
        ? `Pendant .viz ${name}: waiting until the machine stops / the job ends`
        : `Pendant .viz ${name}: starting…`;
      return;
    }
    if (k === "Busy") {
      const pct = /:(\d+)$/.exec(rest);
      $("#viz-status").textContent = `Building pendant .viz ${name}` + (pct ? `: ${pct[1]}%` : "…");
      return;
    }
    if (k !== "Ready" && k !== "Err") return;
    $("#viz-status").textContent = "";
    if (k === "Ready") {
      // The running job's preview was just built (FluidNC builds it when a job
      // starts without one): show it instead of the "no .viz" placeholder.
      const vp = rest.split(":")[0].replace(/\.viz$/, "");
      const cur = $("#viewer-file").dataset;
      if (cur.partial && !Viewer.job && cur.path === "sd:" + vp.replace(/^\/sd/i, "")) delete cur.path;
    }
    if (!auto) return;  // pendant's own requests: leave them to the pendant
    if (k === "Ready") {
      const pts = rest.split(":")[1];
      toast(`Pendant .viz ready: ${name}` + (pts ? ` (${pts} points)` : ""));
    } else {
      toast("Pendant .viz failed: " + rest, true);
    }
  }
  // ------------------------------------------------------------- PC drive script
  // Downloads tools/windows/dpcreator-sd-drive.cmd with this board's IP filled
  // in: double-click on the PC maps the SD card as drive S: (WebDAV over LAN).
  const SD_DRIVE_CMD = __SD_DRIVE_CMD__;
  function boardIp() {
    const host = HOST.replace(/:\d+$/, "");
    if (/^\d{1,3}(\.\d{1,3}){3}$/.test(host)) return Promise.resolve(host);
    // Opened by name (e.g. fluidnc.local): ask FluidNC for its IP.
    return new Promise((resolve) => {
      ipWaiter = resolve;
      Conn.sendLine("[ESP111]", true);
      setTimeout(() => { if (ipWaiter === resolve) { ipWaiter = null; resolve(host); } }, 2500);
    });
  }
  $("#pc-drive").onclick = async () => {
    const ip = await boardIp();
    const text = SD_DRIVE_CMD
      .replace(/set "DEFAULT_IP=[^"]*"/, `set "DEFAULT_IP=${ip}"`)
      .replace(/set "ASK_IP=1"/, 'set "ASK_IP=0"');
    const a = el("a", { href: URL.createObjectURL(new Blob([text], { type: "application/octet-stream" })),
                         download: `dpcreator-sd-drive-${ip}.cmd` });
    document.body.append(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
    toast(`Downloaded the drive script for ${ip}: double-click it on your PC (not as administrator)`);
  };

  $("#make-viz").onclick = () => {
    const it = S.sel;
    if (!it) return;
    const big = it.size > 2 * 1024 * 1024;
    const msg =
      `Build the TabUI pendant preview for ${it.name} (${fmtSize(it.size)})?\n\n` +
      "The controller reads the whole file in the background" +
      (big ? " - for a file this size that can take a minute or more." : ".") +
      "\nIt starts only while the machine is not moving and no job is running.";
    if (!confirm(msg)) return;
    send("$Viz/Refresh=/sd" + it.path);
  };

  // ------------------------------------------------------------- files
  const listEl = $("#file-list");
  $$("#fs button").forEach((b) => (b.onclick = () => {
    $$("#fs button").forEach((x) => x.classList.toggle("active", x === b));
    S.fs = b.dataset.fs; S.dir = "/"; refresh();
  }));
  $("#refresh").onclick = () => refresh();

  async function refresh() {
    selectFile(null);
    $("#file-path").textContent = (S.fs === "sd" ? "SD" : "Flash") + ":" + S.dir;
    listEl.textContent = "";
    listEl.append(el("li", { class: "muted" }, "Loading…"));
    try {
      const items = await Files.list(S.fs, S.dir);
      listEl.textContent = "";
      if (S.dir !== "/") {
        listEl.append(el("li", { class: "dir", onclick: () => { S.dir = S.dir.replace(/\/[^/]+\/?$/, "") || "/"; refresh(); } },
          el("span", { class: "fname" }, "..")));
      }
      // Which G-code files already have their pendant preview (<file>.viz).
      const vizNames = new Set(items.filter((i) => !i.dir && /\.viz$/i.test(i.name)).map((i) => i.name.slice(0, -4)));
      for (const it of items) {
        if (!it.dir && /\.viz(\.tmp)?$/i.test(it.name)) continue; // pendant preview sidecars
        const gcode = !it.dir && S.fs === "sd" && GCODE_RE.test(it.name);
        if (gcode) it.viz = vizNames.has(it.name) ? "ok" : "missing";
        const li = el("li", { class: it.dir ? "dir" : "" },
          el("span", { class: "fname" }, it.name),
          gcode ? el("span", { class: "vizdot" }) : "",
          el("span", { class: "fsize" }, it.dir ? "" : fmtSize(it.size)));
        if (gcode) { li.dataset.path = it.path; li._item = it; paintViz(li, it.viz); }
        li.onclick = () => {
          if (it.dir) { S.dir = it.path; refresh(); return; }
          $$("li", listEl).forEach((x) => x.classList.toggle("sel", x === li));
          selectFile(it);
        };
        li.ondblclick = () => !it.dir && previewSelected();
        listEl.append(li);
      }
      if (!items.length) listEl.append(el("li", { class: "muted" }, "Empty"));
    } catch (e) {
      listEl.textContent = "";
      listEl.append(el("li", { class: "muted" }, e.message + (S.fs === "sd" ? " - is an SD card inserted?" : "")));
    }
  }

  function selectFile(it) {
    S.sel = it;
    $("#file-run").hidden = !it;
    if (!it) return;
    $("#sel-name").textContent = it.name;
    $("#sel-size").textContent = fmtSize(it.size);
    const isText = /\.(nc|gcode|gc|ngc|tap|txt|yaml|yml|json|cnc|g|macro)$/i.test(it.name);
    $("#edit").disabled = !isText || it.size > 256 * 1024;
    $("#preview").disabled = !GCODE_RE.test(it.name);
    $("#make-viz").hidden = S.fs !== "sd" || !GCODE_RE.test(it.name);
    $("#run").textContent = S.fs === "sd" && GCODE_RE.test(it.name) ? "Prepare" : "▶ Run";
    paintVizButton();
  }

  // Pendant preview state of a file: ok (green), missing (red), building (amber).
  const VIZ_TITLES = {
    ok: "Pendant preview (.viz) is ready",
    missing: "No pendant preview (.viz) yet - it is built automatically when the pendant or a job needs it; click to build it now",
    building: "Building the pendant preview (.viz)…",
  };
  function paintViz(li, st) {
    const dot = li.querySelector(".vizdot");
    if (!dot) return;
    dot.className = "vizdot viz-" + st;
    dot.title = VIZ_TITLES[st];
  }
  function paintVizButton() {
    const b = $("#make-viz"), st = S.sel && S.sel.viz;
    b.classList.remove("viz-ok", "viz-missing", "viz-building");
    if (st) b.classList.add("viz-" + st);
    b.title = st ? VIZ_TITLES[st] + (st === "ok" ? " - click to rebuild it" : "") : "Build the TabUI pendant preview (.viz)";
  }
  // "/sd/dir/a.nc" (from a Viz message) -> update its dot and the button
  function setVizState(sdPath, st) {
    const path = sdPath.replace(/^\/sd/i, "");
    if (S.fs !== "sd") return;
    for (const li of $$("li", listEl)) {
      if (li.dataset.path === path && li._item) { li._item.viz = st; paintViz(li, st); }
    }
    if (S.sel && S.sel.path === path) paintVizButton();
  }

  function previewSelected() {
    if (!S.sel) return;
    previewFile(S.fs, S.sel.path).catch((e) => toast(e.message, true));
    showPane("viewer");
  }
  $("#preview").onclick = previewSelected;

  $("#crash-hide").onclick = () => ($("#crash").hidden = true);
  $("#crash-copy").onclick = async () => {
    const t = "dpCREATOR FluidNC crash: " + $("#crash-text").textContent;
    try { await navigator.clipboard.writeText(t); toast("Crash details copied - paste them to the developer"); }
    catch { prompt("Copy the crash details:", t); }
  };

  // ------------------------------------------------------------- page vs firmware
  // The page lives in the filesystem image (littlefs.bin / merged-flash.bin);
  // a firmware-only update (firmware.bin, OTA) keeps the old page, which then
  // lacks the controls for the new firmware features. Both come from the same
  // commit in a release build: compare the commit ids.
  function checkUiVersion(fw) {
    const ui = $("#ui-ver").textContent.trim();
    const m = /-([0-9a-f]{7,})(-dirty)?\)/.exec(fw);
    $("#fw-ver").textContent = m ? m[1] : "";
    if (!m || !/^[0-9a-f]{7,}$/.test(ui) || m[1].startsWith(ui) || ui.startsWith(m[1])) {
      $("#uiold").hidden = true;
      return;
    }
    $("#uiold-text").textContent = `Page ${ui}, firmware ${m[1]}: newer controls are missing. ` +
      "Flash merged-flash.bin (or littlefs.bin) from the release - firmware.bin alone keeps the old page - then reload with Ctrl+F5.";
    $("#uiold").hidden = false;
  }
  $("#uiold-hide").onclick = () => ($("#uiold").hidden = true);

  // ------------------------------------------------------------- handwheel rewind
  // In Hold the board moves the tool back along the path it cut ($Rewind/Move),
  // changes the height there ($Rewind/Z) and Continue re-cuts to the hold point
  // and resumes the job (Rewind.h in the firmware). Buttons repeat while held.
  const Rewind = (() => {
    const bar = $("#rewind");
    let st = "off", back = 0, z = 0, line = 0, max = 0, starting = false;
    function show() {
      const hold = /^Hold/.test(S.state) && !!S.job;
      const on = st !== "off";
      bar.hidden = !(hold || on);
      $("#rw-start").hidden = on;
      $("#rw-start").disabled = starting;
      $("#rw-ctl").hidden = !on;
      $("#rw-pos").textContent = back.toFixed(2) + " mm";
      $("#rw-z").textContent = (z > 0 ? "+" : z < 0 ? "−" : "±") + Math.abs(z).toFixed(3);
      const busy = st === "continuing" || st === "returning";
      $$("[data-rw], [data-rwz], #rw-cancel, #rw-go", bar).forEach((b) => (b.disabled = busy));
      const info = $("#rw-info");
      if (!info.classList.contains("err")) {
        info.textContent = st === "continuing" ? "re-cutting to the hold point, then the job continues…"
          : st === "returning" ? "back to the hold point at the old height…"
          : on ? `line ${line} · up to ${max.toFixed(1)} mm back` : "move back along the cut path, adjust Z, continue";
      }
      rwLineNow = on ? line : 0;
    }
    function onMsg(kind, body) {
      const info = $("#rw-info");
      if (kind === "Err") {
        starting = false;
        info.textContent = body;
        info.classList.add("err");
        setTimeout(() => info.classList.remove("err"), 5000);
        toast("Rewind: " + body, true);
        return show();
      }
      const p = body.split(":");
      const was = st;
      st = p[0]; back = +p[1] || 0; z = +p[2] || 0; line = +p[3] || 0; max = +p[4] || 0;
      starting = false;
      if (was !== "off" && st === "off") toast(was === "continuing" ? "Rewind: job continues" : "Rewind: back at the hold point");
      show();
    }
    // press and hold = repeat
    function repeat(btn, fn) {
      let t = null;
      const stop = () => { clearInterval(t); t = null; };
      btn.addEventListener("pointerdown", (e) => {
        if (btn.disabled) return;
        e.preventDefault();
        fn();
        stop();
        t = setInterval(fn, 250);
      });
      ["pointerup", "pointerleave", "pointercancel"].forEach((ev) => btn.addEventListener(ev, stop));
    }
    $$("[data-rw]", bar).forEach((b) => repeat(b, () => send("$Rewind/Move=" + b.dataset.rw)));
    $$("[data-rwz]", bar).forEach((b) => repeat(b, () => send("$Rewind/Z=" + b.dataset.rwz)));
    $("#rw-start").onclick = () => { starting = true; send("$Rewind/Start"); show(); };
    $("#rw-cancel").onclick = () => send("$Rewind/Cancel");
    $("#rw-go").onclick = () => {
      if (confirm(`Continue: re-cut ${back.toFixed(2)} mm to where the job stopped${z ? ` at Z ${z > 0 ? "+" : ""}${z.toFixed(3)}` : ""}, then resume the job?`)) send("$Rewind/Continue");
    };
    rwHook = show;
    return { onMsg };
  })();

  // ------------------------------------------------------------- live Z adjust
  // A temporary Z offset for the running job, kept by the board (like a wear
  // offset): + raises the tool. It reaches the tool after the moves already
  // planned ("pending" -> "active") and ends with the job; then the board
  // offers to move the work zero by it ($Job/ZAdjust/Apply).
  function zadjEnable() {  // hoisted: called on every state change
    const on = /^(Run|Hold)/.test(S.state) || (!!S.job && !/^(Alarm|Unknown)/.test(S.state));
    $$("[data-zadj], .zadj-reset").forEach((x) => (x.disabled = !on));
    const bar = $("#jobzadj");
    if (bar) bar.hidden = !on;  // big bar above the 3D view while a job runs
  }
  const ZAdj = (() => {
    let val = 0;
    function paint(state, line) {
      $$(".zadj-val").forEach((b) => {
        b.textContent = (val > 0 ? "+" : val < 0 ? "−" : "±") + Math.abs(val).toFixed(3);
        b.classList.toggle("up", val > 0);
        b.classList.toggle("down", val < 0);
      });
      const t = state === "pending" ? (line > 0 ? `applies at line ${line}` : /^Hold/.test(S.state) ? "applies on Resume" : "moving to it…")
        : state === "active" ? (val > 0 ? "active · tool higher (shallower)" : "active · tool lower (deeper)")
        : "+ = shallower";
      $$(".zadj-state").forEach((n) => {
        n.classList.toggle("pending", state === "pending");
        n.classList.toggle("active", state === "active");
        n.textContent = t;
      });
      enable();
    }
    const enable = zadjEnable;
    function onMsg(kind, body) {
      waitAns = 0;
      if (kind === "Err") return toast("Z adjust: " + body, true);
      if (kind === "Info") return toast(body);
      if (kind === "Limit") return;
      const [v, state, line] = body.split(":");
      const n = parseFloat(v) || 0;
      if (state === "ended") {
        val = 0;
        paint("", 0);
        $("#zadj-end-v").textContent = (n > 0 ? "+" : "") + n.toFixed(3);
        $("#zadj-end").hidden = false;
        return;
      }
      $("#zadj-end").hidden = true;
      val = n;
      paint(state === "none" ? "" : state, +line || 0);
    }
    // No answer at all = the controller did not run it (old firmware, or the
    // channel was blocked): say so instead of silently doing nothing.
    let waitAns = 0;
    function zsend(v) {
      send("$Job/ZAdjust=" + v);
      const t = (waitAns = Date.now());
      setTimeout(() => {
        if (waitAns === t) toast("Z adjust: no answer from the controller - update the firmware (merged-flash.bin)", true);
      }, 2500);
    }
    $$("[data-zadj]").forEach((b) => (b.onclick = () => zsend(b.dataset.zadj)));
    $$(".zadj-reset").forEach((b) => (b.onclick = () => zsend("0")));
    $("#zadj-apply").onclick = () => {
      if (!idle()) return toast(`The machine must be Idle (now ${S.state})`, true);
      send("$Job/ZAdjust/Apply");
    };
    $("#zadj-no").onclick = () => { send("$Job/ZAdjust=0"); $("#zadj-end").hidden = true; };
    paint("", 0);
    return { onMsg, enable };
  })();

  // ------------------------------------------------------------- job recovery
  // The board records the line a file job was at and keeps an interrupted job
  // (alarm, E-stop, reset, Stop, power loss). It works out safe restart points
  // ($Job/Points) and continues from a chosen line ($Job/Resume=<line>[:dry]),
  // setting up coordinates, units, feed, spindle and coolant first. The pendant
  // shows the same choices; this panel only displays and sends them.
  const Recovery = (() => {
    const box = $("#recovery");
    let rec = { st: "none", stop: 0, retract: 0, tool: 0, why: "", file: "" };
    let choice = -1, custom = 0, asked = "", later = "", text = null, textFor = "", busy = false;
    const key = () => rec.file + "@" + rec.stop;

    // Ready to continue? The board says alarm / unhomed / busy / ok; Alarm in
    // the status report counts too (older firmware, or before the message).
    let ready = "ok";
    function readiness() { return /^Alarm/.test(S.state) ? "alarm" : ready; }
    function renderReady() {
      const r = readiness(), box2 = $("#rec-ready");
      const show = rec.st === "interrupted" && r !== "ok";
      box2.hidden = !show;
      if (show) {
        $("#rec-ready-text").textContent =
          r === "alarm" ? "The machine is in alarm: Unlock it (Home if the position was lost), then Dry run / Resume."
          : r === "unhomed" ? "The position was lost: Home the machine, then Dry run / Resume."
          : "Waiting for the machine to be Idle…";
        $("#rec-unlock").classList.toggle("go", r === "alarm");
        $("#rec-home").classList.toggle("go", r === "unhomed");
        $("#rec-unlock").disabled = r !== "alarm";
        $("#rec-home").disabled = !(r === "alarm" || r === "unhomed" || S.state === "Idle");
      }
      const ok = rec.st === "interrupted" && !busy && pick() > 0 && r === "ok";
      $("#rec-go").disabled = $("#rec-dry").disabled = !ok;
    }
    recovHook = () => { if (!box.hidden) renderReady(); };
    $("#rec-unlock").onclick = () => send("$X");
    $("#rec-home").onclick = () => {
      if (confirm("Home the machine now?\n\nIt moves to the home switches (Z first). Make sure the tool can lift out of the part.")) send("$H");
    };

    function onMsg(kind, body) {
      if (kind === "Ready") { ready = body; return render(); }
      if (kind === "Err") { busy = false; msg(body, true); toast("Recovery: " + body, true); return render(); }
      if (kind === "Info") { msg(body); toast(body); return; }
      // <state>:<stop>:<retract>:<tool>:<reason>:<file>
      const p = body.split(":");
      const was = key();
      rec = { st: p[0], stop: +p[1] || 0, retract: +p[2] || 0, tool: +p[3] || 0, why: p[4] || "", file: p.slice(5).join(":") };
      if (rec.st === "none") { busy = false; box.hidden = true; return; }
      if (key() !== was) { choice = -1; custom = rec.stop; msg(""); }
      if (rec.st === "interrupted") {
        busy = false;
        if (!rec.retract && !rec.tool && asked !== key()) { asked = key(); send("$Job/Points"); }
        if (choice < 0 || (choice === 2 && (rec.retract || rec.tool))) choice = rec.retract ? 0 : rec.tool ? 1 : 2;
      }
      render();
      loadText();
    }

    const lineOf = (c) => [rec.retract, rec.tool, rec.stop, custom][c] || 0;
    const pick = () => lineOf(choice);

    function msg(t, err = false) {
      const m = $("#rec-msg");
      delete m.dataset.work;
      m.textContent = t;
      m.className = "rec-msg" + (err ? " err" : "");
    }

    function render() {
      box.hidden = false;
      const collapsed = later === key();
      box.classList.toggle("collapsed", collapsed);
      $("#rec-show").hidden = !collapsed;
      $("#rec-file").textContent = rec.file.replace(/^\/sd\//i, "");
      $("#rec-why").textContent = `stopped at line ${rec.stop}` + (rec.why ? ` (${rec.why})` : "");
      const work = rec.st === "scanning" ? "finding safe restart points…" : rec.st === "resuming" ? "reading up to the line, then starting…" : "";
      const m = $("#rec-msg");
      if (work) { msg(work); m.dataset.work = "1"; }
      else if (m.dataset.work) { msg(""); delete m.dataset.work; }
      const opts = [
        ["Last retract", "tool was up, safest", rec.retract],
        ["Start of this tool", "after the last tool change", rec.tool],
        ["Where it stopped", "may restart mid-cut", rec.stop],
      ];
      const ch = $("#rec-choices");
      ch.textContent = "";
      opts.forEach(([name, hint, ln], i) => {
        const b = el("label", { class: "rec-choice" + (choice === i ? " on" : "") + (ln ? "" : " off") });
        b.append(el("b", {}, name), " ", ln ? "line " + ln : rec.st === "scanning" ? "…" : "none", " ", el("small", {}, hint));
        if (ln) b.onclick = () => { choice = i; render(); showLine(); };
        ch.append(b);
      });
      const c = el("label", { class: "rec-choice" + (choice === 3 ? " on" : "") });
      const inp = el("input", { type: "number", min: 1, max: rec.stop || undefined, value: custom || rec.stop });
      inp.oninput = () => { custom = Math.max(1, Math.min(rec.stop || 1e9, parseInt(inp.value, 10) || 1)); choice = 3; showLine(); markChoices(); };
      inp.onfocus = () => { choice = 3; markChoices(); };
      c.append(el("b", {}, "Line"), " ", inp);
      ch.append(c);
      $("#rec-go").textContent = pick() ? `▶ Resume from line ${pick()}` : "▶ Resume";
      renderReady();
      showLine();
    }
    function markChoices() {
      $$(".rec-choice", $("#rec-choices")).forEach((b, i) => b.classList.toggle("on", i === choice));
      $("#rec-go").textContent = `▶ Resume from line ${pick()}`;
    }

    // G-code around the chosen line, and the path up to it in the 3D view
    async function loadText() {
      if (rec.st !== "interrupted" || textFor === rec.file) return;
      textFor = rec.file;
      text = null;
      const [fs, path] = jobFsPath(rec.file);
      try {
        const t = await Files.getText(fs, path);
        const lines = t.split("\n"), offs = new Array(lines.length + 1);
        let o = 0;
        for (let i = 0; i < lines.length; i++) { offs[i] = o; o += lines[i].length + 1; }
        offs[lines.length] = o;
        text = { lines, offs, bytes: o };
        showLine();
        await previewFile(fs, path).catch(() => {});
        showLine();
      } catch { textFor = ""; }
    }
    function showLine() {
      const pre = $("#rec-text"), ln = pick();
      if (!text || !ln) { pre.textContent = text ? "" : "(loading the file…)"; return; }
      pre.textContent = "";
      for (let i = Math.max(1, ln - 3); i <= Math.min(text.lines.length, ln + 3); i++) {
        const row = el("span", i === ln ? { class: "here" } : {});
        row.append(el("span", { class: "ln" }, String(i).padStart(6) + "  "), text.lines[i - 1].replace(/\r$/, ""));
        pre.append(row, i === ln ? "" : "\n");
      }
      const [fs, path] = jobFsPath(rec.file);
      if ($("#viewer-file").dataset.path === cacheKey(fs, path) && !S.job)
        Viewer.setProgress(text.offs[Math.min(ln - 1, text.lines.length)] / text.bytes, null, ln);
    }

    function go(dry) {
      const ln = pick();
      if (!ln) return;
      if (!idle()) return toast(`The machine must be Idle (now ${S.state})`, true);
      if (!dry && !confirm(`Resume ${rec.file.replace(/^.*\//, "")} from line ${ln}?\n\nThe tool lifts to safe height, moves over the start, starts the spindle and lowers to the cut.`)) return;
      busy = !dry;
      send(`$Job/Resume=${ln}` + (dry ? ":dry" : ""));
      render();
    }
    $("#rec-go").onclick = () => go(false);
    $("#rec-dry").onclick = () => go(true);
    $("#rec-later").onclick = () => { later = key(); render(); };
    $("#rec-show").onclick = () => { later = ""; render(); };
    $("#rec-discard").onclick = () => {
      if (confirm("Forget the interrupted job? It can't be resumed afterwards.")) send("$Job/Discard");
    };
    return { onMsg };
  })();

  // ------------------------------------------------------------- prepared job
  // "Prepare" puts the file on the pendant's screen (and in this viewer); the
  // job is then started - or cancelled - from either side. The board keeps
  // the state and reports it as [MSG:Prepared:<state>:<file>].
  const PREP_STATES = {
    loading: "loading on the pendant…",
    ready: "shown on the pendant",
    nopendant: "no pendant connected",
  };
  let prepFile = "";
  function setPrepared(st, file) {
    const box = $("#prepared");
    if (st === "running") { prepFile = ""; box.hidden = true; return; }  // started: keep the preview
    if (st === "none" || !file) {
      // Cancelled: the prepared file's preview goes too (unless a job runs).
      const was = prepFile;
      prepFile = "";
      box.hidden = true;
      if (was && !S.job && $("#viewer-file").dataset.path === "sd:" + was.replace(/^\/sd/i, "")) {
        Viewer.clear();
        $("#viewer-file").textContent = "No file loaded";
        $("#viewer-file").dataset.path = "";
        $("#viewer-info").textContent = "";
      }
      return;
    }
    prepFile = file;
    box.hidden = false;
    $("#prep-name").textContent = file.replace(/^\/sd\//i, "");
    const s = $("#prep-state");
    s.textContent = PREP_STATES[st] || st;
    s.className = "prep-state " + st;
    // show the same file here
    const path = file.replace(/^\/sd/i, "");
    if ($("#viewer-file").dataset.path !== "sd:" + path) previewFile("sd", path).catch(() => {});
  }
  $("#prep-run").onclick = () => {
    if (!prepFile) return;
    if (!idle()) return toast(`Can't start a job while ${S.state}`, true);
    const path = prepFile.replace(/^\/sd/i, "");
    if (!confirm(`Run ${path.replace(/^.*\//, "")}?`)) return;
    send(Files.runCommand("sd", path));
  };
  $("#prep-cancel").onclick = () => send("$Job/Unprepare");

  $("#run").onclick = async () => {
    const it = S.sel;
    if (!it) return;
    if (!idle()) return toast(`Can't start a job while ${S.state}`, true);
    if (S.fs === "sd" && GCODE_RE.test(it.name)) {
      // Prepare: pendant + this viewer show it; Run / Cancel from the bar
      send("$Job/Prepare=" + it.path);
      previewFile("sd", it.path).catch(() => {});
      showPane("viewer");
      return;
    }
    if (!confirm(`Run ${it.name}?`)) return;
    if (!(Viewer.job && $("#viewer-file").dataset.path === S.fs + ":" + it.path && !$("#viewer-file").dataset.partial)) {
      previewFile(S.fs, it.path).catch(() => {});
    }
    send(Files.runCommand(S.fs, it.path));
  };

  $("#del").onclick = async () => {
    const it = S.sel;
    if (!it || !confirm(`Delete ${it.name}?`)) return;
    try {
      await Files.remove(S.fs, it.path);
      forget(S.fs, it.path);
      if (S.fs === "sd" && GCODE_RE.test(it.name)) await Files.remove("sd", it.path + ".viz").catch(() => {});
      toast("Deleted " + it.name);
      refresh();
    }
    catch (e) { toast(e.message, true); }
  };

  $("#upload").onchange = async (e) => {
    const files = Array.from(e.target.files || []);
    e.target.value = "";
    const bar = $("#upload-progress");
    for (const f of files) {
      bar.hidden = false;
      try {
        await Files.upload(S.fs, S.dir, f, (p) => (bar.firstElementChild.style.width = (p * 100).toFixed(0) + "%"));
        forget(S.fs, (S.dir.endsWith("/") ? S.dir : S.dir + "/") + f.name);
        toast("Uploaded " + f.name);
      } catch (err) { toast(err.message, true); }
    }
    bar.hidden = true;
    bar.firstElementChild.style.width = "0";
    refresh();
  };

  // simple text editor (config.yaml, small G-code, macros)
  const dlg = $("#editor");
  $("#edit").onclick = async () => {
    const it = S.sel;
    if (!it) return;
    try {
      $("#editor-text").value = await Files.getText(S.fs, it.path);
      $("#editor-title").textContent = (S.fs === "sd" ? "SD" : "Flash") + ":" + it.path;
      dlg.showModal();
    } catch (e) { toast(e.message, true); }
  };
  dlg.addEventListener("close", async () => {
    if (dlg.returnValue !== "save" || !S.sel) return;
    try {
      await Files.putText(S.fs, S.sel.path, $("#editor-text").value);
      forget(S.fs, S.sel.path);
      toast("Saved " + S.sel.name + (S.sel.name === "config.yaml" ? " - restart ($bye) to apply" : ""));
      refresh();
    } catch (e) { toast(e.message, true); }
  });

  // ------------------------------------------------------------- axis monitor
  Monitor.init($("#monitor"));
  function setMonitor(open) {
    $("#monitor").classList.toggle("collapsed", !open);
    $("#mon-toggle").textContent = "Axis monitor " + (open ? "▾" : "▸");
    document.documentElement.style.setProperty("--mon-h", open ? "74px" : "40px");
    Monitor.setVisible(open);
    Prefs.set("monitor", open);
    requestAnimationFrame(() => Viewer.resize());
  }
  $("#mon-toggle").onclick = () => setMonitor($("#monitor").classList.contains("collapsed"));
  setMonitor(Prefs.get("monitor", true));

  // ------------------------------------------------------------- phone tabs
  function showPane(name) {
    $$("#tabs button").forEach((b) => b.classList.toggle("active", b.dataset.view === name));
    $$(".layout > .panel").forEach((p) => p.classList.toggle("shown", p.dataset.pane === name));
    if (name === "viewer") requestAnimationFrame(() => Viewer.resize());
  }
  $$("#tabs button").forEach((b) => (b.onclick = () => showPane(b.dataset.view)));
  showPane("control");

  // ------------------------------------------------------------- start
  Conn.connect();
  refresh();
})();
