/* dpCREATOR WebUI installer: puts the licensed WebUI on a board over USB.
 *
 * Runs on the dpcreator.eu Flash Firmware page (Chrome or Edge on a computer:
 * Web Serial). Steps:
 *   1. open the board's USB serial port, ask $Board/ID and the machine state
 *   2. POST the chip ID to /webui.php?f=install -> the page licensed to it
 *   3. $Xmodem/Receive=/localfs/webui.tmp and send it (XModem-1K, CRC-16)
 *   4. check the size the board reports, keep the stock page as legacy.html.gz
 *      (Classic UI), and rename webui.tmp to index.html.gz
 * Nothing is deleted before the new page is safely on the board.
 */
(function (root) {
  "use strict";

  const SOH = 0x01, STX = 0x02, EOT = 0x04, ACK = 0x06, NAK = 0x15, CAN = 0x18, CTRLZ = 0x1a;
  const TMP = "/localfs/webui.tmp", INDEX = "/localfs/index.html.gz", LEGACY = "/localfs/legacy.html.gz";

  function crc16(buf, off, len) {
    let crc = 0;
    for (let i = off; i < off + len; i++) {
      crc ^= buf[i] << 8;
      for (let j = 0; j < 8; j++) crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
    }
    return crc;
  }

  /** Byte stream from the board with line and raw readers. `port` has
   *  read() -> Promise<Uint8Array|null> and write(Uint8Array) -> Promise. */
  function Link(port, log) {
    const q = [];          // received bytes not yet consumed
    let waiters = [];
    let closed = false;
    let lineBuf = "";
    const lines = [];       // complete text lines (only in text mode)
    let raw = false;

    (async () => {
      try {
        for (;;) {
          const chunk = await port.read();
          if (!chunk) break;
          if (raw) {
            for (const b of chunk) q.push(b);
          } else {
            for (const b of chunk) {
              if (b === 10) { lines.push(lineBuf.replace(/\r$/, "")); log && log("< " + lines[lines.length - 1]); lineBuf = ""; }
              else lineBuf += String.fromCharCode(b);
            }
          }
          const w = waiters; waiters = []; w.forEach((f) => f());
        }
      } catch (e) { /* port closed */ }
      closed = true;
      const w = waiters; waiters = []; w.forEach((f) => f());
    })();

    function wait(ms) {
      return new Promise((res) => {
        const t = setTimeout(() => { waiters = waiters.filter((x) => x !== done); res(false); }, ms);
        function done() { clearTimeout(t); res(true); }
        waiters.push(done);
      });
    }

    return {
      write: (bytes) => port.write(bytes),
      async send(text) { log && log("> " + text); await port.write(new TextEncoder().encode(text + "\n")); },
      setRaw(on) {
        raw = on;
        if (on) { q.length = 0; for (const c of lineBuf) q.push(c.charCodeAt(0)); lineBuf = ""; return; }
        // back to text: bytes that arrived after the last raw read are text
        for (const b of q.splice(0)) {
          if (b === 10) { lines.push(lineBuf.replace(/\r$/, "")); log && log("< " + lines[lines.length - 1]); lineBuf = ""; }
          else lineBuf += String.fromCharCode(b);
        }
      },
      /** next raw byte, or -1 after ms */
      async byte(ms) {
        const end = Date.now() + ms;
        while (!q.length) {
          if (closed) return -1;
          const left = end - Date.now();
          if (left <= 0 || !(await wait(left))) { if (!q.length) return -1; }
        }
        return q.shift();
      },
      /** first line matching re (lines before it are dropped), or null after ms */
      async line(re, ms) {
        const end = Date.now() + ms;
        for (;;) {
          while (lines.length) { const l = lines.shift(); const m = re.exec(l); if (m) return m; }
          if (closed) return null;
          const left = end - Date.now();
          if (left <= 0 || !(await wait(left))) { if (!lines.length) return null; }
        }
      },
      drain() { lines.length = 0; q.length = 0; },
      get closed() { return closed; },
    };
  }

  /** Send one command and wait for ok / error:N. Returns {ok, lines, error}. */
  async function command(link, cmd, ms = 4000) {
    link.drain();
    await link.send(cmd);
    const got = [];
    const end = Date.now() + ms;
    for (;;) {
      const m = await link.line(/^.*$/, Math.max(1, end - Date.now()));
      if (!m) return { ok: false, lines: got, error: "timeout" };
      const l = m[0];
      if (l === "ok") return { ok: true, lines: got };
      if (/^error:/.test(l)) return { ok: false, lines: got, error: l };
      if (l.startsWith("<")) continue;   // status report
      got.push(l);
    }
  }

  /** XModem-1K/CRC sender for FluidNC's xmodemReceive. */
  async function xmodemSend(link, data, progress) {
    // The board waits 1 s, then sends 'C' every 2 s (16 tries). Log lines can
    // also contain a 'C', so accept it only at the start of a line.
    link.setRaw(true);
    let prev = 10, start = false;
    const end = Date.now() + 40000;
    while (Date.now() < end) {
      const c = await link.byte(end - Date.now());
      if (c < 0) break;
      if (c === 0x43 && (prev === 10 || prev === 13 || prev === 0x43)) { start = true; break; }
      if (c === CAN) throw new Error("The board refused the file (cannot open it).");
      prev = c;
    }
    if (!start) throw new Error("The board did not start receiving.");

    const blocks = Math.ceil(data.length / 1024);
    const pkt = new Uint8Array(1029);
    for (let n = 0; n < blocks; n++) {
      pkt[0] = STX;
      pkt[1] = (n + 1) & 0xff;
      pkt[2] = 0xff - pkt[1];
      const chunk = data.subarray(n * 1024, n * 1024 + 1024);
      pkt.fill(CTRLZ, 3, 1027);
      pkt.set(chunk, 3);
      const crc = crc16(pkt, 3, 1024);
      pkt[1027] = crc >> 8;
      pkt[1028] = crc & 0xff;
      let sent = false;
      for (let tries = 0; tries < 10 && !sent; tries++) {
        await link.write(pkt);
        for (;;) {
          const r = await link.byte(5000);
          if (r === ACK) { sent = true; break; }
          if (r === NAK || r < 0) break;            // send again
          if (r === CAN) {
            const r2 = await link.byte(1000);
            if (r2 === CAN) throw new Error("The board cancelled the transfer (not enough space?).");
          }
          // anything else (a stray 'C', log text) is ignored
        }
      }
      if (!sent) throw new Error("Transfer failed at block " + (n + 1) + " of " + blocks + ".");
      progress && progress((n + 1) / blocks);
    }
    for (let tries = 0; tries < 10; tries++) {
      await link.write(new Uint8Array([EOT]));
      const r = await link.byte(5000);
      if (r === ACK) { link.setRaw(false); return; }
      if (r === CAN) throw new Error("The board cancelled the transfer (not enough space?).");
    }
    throw new Error("The board did not confirm the end of the transfer.");
  }

  /** The whole install. `api` = {fetchFile(boardId) -> {bytes, sha256, version}}, `ui` = callbacks. */
  async function install(port, api, ui) {
    const log = ui.log;
    const link = Link(port, log);
    // The board may print its startup log when the port opens.
    await new Promise((r) => setTimeout(r, 600));
    link.drain();

    ui.step("Reading the board");
    // Wait for the ID line itself (an "ok" left over from earlier input must
    // not count as the answer); ask twice in case the board was still starting.
    let idm = null;
    for (let i = 0; i < 2 && !idm; i++) {
      link.drain();
      await link.send("$Board/ID");
      idm = await link.line(/\[MSG:BoardID:([0-9A-F]{12}):api=(\d+)\]/, 3000);
    }
    // Newer firmware adds [MSG:BoardMem:psram=<KB>:ok|bad:<why>] before the ok.
    let mem = null;
    if (idm) {
      const m = await link.line(/\[MSG:BoardMem:psram=(\d+):(ok|bad)(?::([^\]]*))?\]|^ok$/, 1000);
      if (m && m[2]) { mem = m; await link.line(/^ok$/, 1000); }
    }
    if (!idm) {
      throw new Error("This board's firmware does not report its ID. Update the controller firmware first " +
        "(Controller firmware tab), then install the WebUI.");
    }
    if (mem && mem[2] === "bad") {
      // Not installed, and no licence slot used: the board would run out of
      // memory during jobs.
      throw new Error("This board's ESP32-S3 module is not supported: " + (mem[3] || "no PSRAM found.") +
        " The WebUI was not installed and none of your board licences was used.");
    }
    const board = idm[1];
    ui.board(board);

    link.drain();
    await link.write(new Uint8Array([0x3f]));   // '?': status report
    const st = await link.line(/^<([A-Za-z]+)/, 3000);
    if (st && !/^(Idle|Alarm)$/.test(st[1])) throw new Error("The machine is " + st[1] + ". Stop the job and try again when it is Idle.");

    ui.step("Getting the WebUI licensed to this board");
    const file = await api.fetchFile(board);
    const bytes = file.bytes;
    if (bytes[bytes.length - 1] === CTRLZ) throw new Error("Unexpected file from the shop, please try again.");

    // free space check is done by the board while receiving
    ui.step("Sending the WebUI to the board");
    const list0 = await command(link, "$LocalFS/List", 5000);
    const names = (list0.lines || []).map((l) => (/\[FILE:\s*([^|\]]+)\|SIZE:(\d+)/.exec(l) || [])[1]).filter(Boolean).map((s) => s.trim());
    if (names.includes("webui.tmp")) await command(link, "$LocalFS/Delete=" + TMP, 4000);

    link.drain();
    await link.send("$Xmodem/Receive=" + TMP);
    await xmodemSend(link, bytes, ui.progress);
    const done = await link.line(/Received (\d+) bytes|Reception failed.*|^error:.*/, 8000);
    if (!done || !done[1]) throw new Error("The board did not store the file" + (done ? ": " + done[0] : "."));
    if (+done[1] !== bytes.length) {
      await command(link, "$LocalFS/Delete=" + TMP, 4000);
      throw new Error("The board stored " + done[1] + " of " + bytes.length + " bytes. Nothing was changed; please try again.");
    }
    await link.line(/^ok$/, 2000);

    ui.step("Switching the board to the new WebUI");
    // Keep the stock FluidNC page as the Classic UI once; then replace index.
    if (names.includes("index.html.gz")) {
      if (!names.includes("legacy.html.gz")) {
        const r = await command(link, "$LocalFS/Rename=" + INDEX + ">" + LEGACY, 5000);
        if (!r.ok) throw new Error("Could not keep the Classic UI (" + (r.error || "") + "). The new WebUI is on the board as webui.tmp.");
      } else {
        const r = await command(link, "$LocalFS/Delete=" + INDEX, 5000);
        if (!r.ok) throw new Error("Could not remove the old WebUI (" + (r.error || "") + ").");
      }
    }
    const r = await command(link, "$LocalFS/Rename=" + TMP + ">" + INDEX, 5000);
    if (!r.ok) throw new Error("Could not activate the new WebUI (" + (r.error || "") + "). Try the install again.");

    const list1 = await command(link, "$LocalFS/List", 5000);
    const size = (list1.lines || []).map((l) => /\[FILE:\s*index\.html\.gz\|SIZE:(\d+)/.exec(l)).find(Boolean);
    if (!size || +size[1] !== bytes.length) throw new Error("The new WebUI could not be checked on the board. Try the install again.");
    return { board, version: file.version, size: bytes.length };
  }

  /** Web Serial port adapter. */
  async function openSerial() {
    const sp = await navigator.serial.requestPort();
    await sp.open({ baudRate: 115200, bufferSize: 65536 });
    const reader = sp.readable.getReader();
    const writer = sp.writable.getWriter();
    return {
      read: async () => { const { value, done } = await reader.read(); return done ? null : value; },
      write: (b) => writer.write(b),
      close: async () => {
        try { await reader.cancel(); } catch (e) {}
        try { reader.releaseLock(); writer.releaseLock(); } catch (e) {}
        try { await sp.close(); } catch (e) {}
      },
    };
  }

  /** Shop API (same origin, logged-in session). */
  const shop = {
    access: () => fetch("/webui.php?f=access", { credentials: "same-origin" }).then((r) => r.json()),
    version: () => fetch("/webui.php?f=version").then((r) => r.json()),
    async fetchFile(board) {
      const r = await fetch("/webui.php?f=install", {
        method: "POST", credentials: "same-origin",
        headers: { "Content-Type": "application/json", "X-DPC-Installer": "1" },
        body: JSON.stringify({ board }),
      });
      if (!r.ok) {
        let j = {};
        try { j = await r.json(); } catch (e) {}
        const why = {
          login_required: "Log in to your dpcreator.eu account first.",
          licence_required: "Your account has no WebUI licence yet.",
          board_limit: "Your licence already covers " + (j.max_boards || 2) + " boards. Remove one below to use this board.",
          too_many_installs: "Too many installs in the last hour. Please try again later.",
          not_available: "The WebUI is not available right now. Please try again in a few minutes.",
        }[j.error] || "The shop could not prepare the WebUI (" + (j.error || r.status) + ").";
        const e = new Error(why); e.code = j.error; e.state = j; throw e;
      }
      const bytes = new Uint8Array(await r.arrayBuffer());
      const want = r.headers.get("X-DPC-Sha256");
      if (want && root.crypto && root.crypto.subtle) {
        const h = Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", bytes))).map((b) => b.toString(16).padStart(2, "0")).join("");
        if (h !== want) throw new Error("The download was damaged. Please try again.");
      }
      return { bytes, version: r.headers.get("X-DPC-Version") || "" };
    },
    release: (board) => fetch("/webui.php?f=release", {
      method: "POST", credentials: "same-origin",
      headers: { "Content-Type": "application/json", "X-DPC-Installer": "1" },
      body: JSON.stringify({ board }),
    }).then((r) => r.json()),
  };

  const api = { install, xmodemSend, Link, command, crc16, openSerial, shop };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.DPCWebUiInstaller = api;
})(typeof window !== "undefined" ? window : globalThis);
