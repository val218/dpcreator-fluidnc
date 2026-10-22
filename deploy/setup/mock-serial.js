/* A pretend FluidNC board on navigator.serial, for testing the setup flow without hardware.
 * Files persist across reconnects in window.__mockFS. $Bye drops the port like native USB does;
 * a new port object shows up in getPorts() 1.5 s later. */
(function () {
  const enc = new TextEncoder(), dec = new TextDecoder();
  const SOH = 1, STX = 2, EOT = 4, ACK = 6, NAK = 0x15, CAN = 0x18;
  window.__mockFS = window.__mockFS || { "config.yaml": enc.encode("board: default\nname: factory\n") };
  window.__mockLog = [];
  let ports = [];
  function crc16(buf, off, len) { let c = 0; for (let i = off; i < off + len; i++) { c ^= buf[i] << 8; for (let j = 0; j < 8; j++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xffff : (c << 1) & 0xffff; } return c; }
  function MockPort() {
    let ctrl = null, mode = "text", rx = [], line = "", xm = null, opened = false, gone = false;
    const self = this;
    const send = (s) => { if (ctrl && !gone) ctrl.enqueue(typeof s === "string" ? enc.encode(s) : s); };
    const say = (...ls) => ls.forEach((l) => send(l + "\r\n"));
    this.getInfo = () => ({ usbVendorId: 0x303a, usbProductId: 0x1001 });
    this.open = async () => {
      if (gone) throw new Error("device lost");
      if (opened) throw new Error("already open");
      opened = true;
      self.readable = new ReadableStream({ start(c) { ctrl = c; } });
      self.writable = new WritableStream({ write(chunk) { for (const b of chunk) onByte(b); } });
    };
    this.close = async () => { opened = false; try { ctrl && ctrl.close(); } catch (e) {} ctrl = null; };
    this.setSignals = async () => {};
    function onByte(b) {
      if (mode === "xrecv") return xRecv(b);
      if (mode === "xsend") return xSend(b);
      if (b === 0x3f && !line) { say("<Idle|MPos:0.000,0.000,0.000|FS:0,0" + (window.__mockPn ? "|Pn:" + window.__mockPn : "") + ">"); return; }
      if (b === 10) { const l = line.replace(/\r$/, ""); line = ""; cmd(l); return; }
      line += String.fromCharCode(b);
    }
    function cmd(l) {
      window.__mockLog.push(l);
      let m;
      if (l === "$I") return say("[VER:4.1.1 dpCREATOR FluidNC (mock)]", "[OPT:PHSE]", "ok");
      if (l === "$Board/ID") return say("[MSG:BoardID:A1B2C3D4E5F6]", "ok");
      if (l === "$Config/Filename") return say("$Config/Filename=config.yaml", "ok");
      if (/^\$Config\/Filename=/.test(l)) return say("ok");
      if ((m = /^\$LocalFS\/ListJSON=(.*)$/.exec(l))) return say(JSON.stringify({ files: Object.entries(window.__mockFS).map(([n, v]) => ({ name: n, size: v.length })), path: "/", total: "1.5MB", used: "200KB" }), "ok");
      if ((m = /^\$Xmodem\/Receive=\/localfs\/(.*)$/.exec(l))) { mode = "xrecv"; xm = { name: m[1], data: [], buf: [] }; setTimeout(() => send(new Uint8Array([0x43])), 300); return; }
      if ((m = /^\$Xmodem\/Send=\/localfs\/(.*)$/.exec(l))) {
        const d = window.__mockFS[m[1]];
        if (!d) return say("[MSG:ERR: Cannot open " + m[1] + "]", "error:60");
        say("[MSG:INFO: Sending /localfs/" + m[1] + " via XModem]");
        mode = "xsend"; xm = { data: d, n: 0, wait: "C" }; return;
      }
      if (l === "$Bye") {
        say("[MSG:INFO: Restarting]");
        setTimeout(() => {
          gone = true;
          try { ctrl && ctrl.error(new Error("The device has been lost.")); } catch (e) {}
          ports = ports.filter((p) => p !== self);
          setTimeout(() => { ports.push(new MockPort()); }, 1500);
        }, 200);
        return;
      }
      if (l === "$SS") {
        const cfg = dec.decode(window.__mockFS["config.yaml"] || new Uint8Array());
        const out = ["[MSG:INFO: FluidNC v4.1.1 (mock)]", "[MSG:INFO: Compiled with ESP32 SDK:v4.4.7]", "[MSG:INFO: Local filesystem type is littlefs]", "[MSG:INFO: Configuration file:config.yaml]"];
        const bad = [...cfg.matchAll(/^\s*(delay_ms|bogus_key):/gm)].map((x) => "[MSG:ERR: Ignored key " + x[1] + "]");
        out.push(...bad);
        if (bad.length) out.push("[MSG:ERR: Configuration is invalid. Check boot messages for ERR's.]");
        else out.push("[MSG:INFO: Machine " + ((/^name:\s*(.*)$/m.exec(cfg) || [])[1] || "?") + "]", "[MSG:INFO: Board " + ((/^board:\s*(.*)$/m.exec(cfg) || [])[1] || "?") + "]");
        return say(...out, "ok");
      }
      if (l === "$H") return setTimeout(() => say("ok"), 800);
      if (/^\$X|^\$J=|^G10|^M[356]|^\$\/axes/.test(l)) return say("ok");
      say("ok");
    }
    function xRecv(b) {
      xm.buf.push(b);
      if (xm.buf[0] === EOT) {
        send(new Uint8Array([ACK]));
        let all = [].concat(...xm.data);
        let n = all.length; while (n > 0 && all[n - 1] === 0x1a) n--;
        const name = xm.name;
        window.__mockFS[name] = new Uint8Array(all.slice(0, n));
        mode = "text"; xm = null;
        say("[MSG:INFO: Received " + n + " bytes to file /localfs/" + name + "]", "ok");
        return;
      }
      if (xm.buf[0] === STX && xm.buf.length === 1029) {
        const p = new Uint8Array(xm.buf);
        const ok = p[1] === ((~p[2]) & 0xff) && crc16(p, 3, 1024) === ((p[1027] << 8) | p[1028]);
        if (ok) xm.data.push(Array.from(p.slice(3, 1027)));
        xm.buf = [];
        send(new Uint8Array([ok ? ACK : NAK]));
      }
      if (xm && xm.buf.length && xm.buf[0] !== STX && xm.buf[0] !== EOT) xm.buf = [];
    }
    function xSend(b) {
      if (xm.wait === "C" && b === 0x43) { xm.wait = "ack"; pkt(); return; }
      if (xm.wait === "ack" && b === ACK) {
        xm.n++;
        if (xm.n * 1024 >= xm.data.length) { xm.wait = "eot"; send(new Uint8Array([EOT])); return; }
        pkt(); return;
      }
      if (xm.wait === "eot" && b === ACK) { mode = "text"; const n = xm.data.length; xm = null; say("[MSG:INFO: Sent " + n + " bytes]", "ok"); }
    }
    function pkt() {
      const p = new Uint8Array(1029); p[0] = STX; p[1] = (xm.n + 1) & 0xff; p[2] = ~(xm.n + 1) & 0xff; p.fill(0x1a, 3, 1027);
      p.set(xm.data.subarray(xm.n * 1024, Math.min(xm.data.length, (xm.n + 1) * 1024)), 3);
      const c = crc16(p, 3, 1024); p[1027] = c >> 8; p[1028] = c & 0xff; send(p);
    }
  }
  ports.push(new MockPort());
  Object.defineProperty(navigator, "serial", { configurable: true, value: { requestPort: async () => ports[0], getPorts: async () => ports.slice(), addEventListener() {} } });
})();
