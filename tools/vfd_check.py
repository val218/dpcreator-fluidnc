#!/usr/bin/env python3
"""RS485 / Modbus command check for the host build.

    python3 tools/vfd_check.py [.pio/build/linux_x86_64/program]

Plays, on one pseudo terminal (uart1): a YL620 drive (id 1, its speed
follows the speed it was given while running) and an I/O module (id 5).
Then runs the host firmware with a YL620 spindle, twice: alone on the UART,
and sharing it with a modbus_bus: and a modbus_io module. Checks:

  - $VFD/State: answering, rpm, "not answering" once when the drive goes
    quiet, "answering again" when it comes back
  - M3 S... still syncs to the drive's speed, M5 stops it
  - $Modbus/Read (functions 1-4), Write (6, 16, coil), Raw, Scan, the
    usage line, errors (no answer, exception, bad arguments), the states
    where Write and Scan are refused
  - with the shared bus: the module's input still reads, and the scan finds both
  - no VFD and no bus: [MODBUS:ERR:no bus], [MSG:INFO: No VFD spindle]
"""
import os
import pty
import re
import select
import subprocess
import sys
import tempfile
import threading
import time
import tty

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".pio/build/linux_x86_64/program")
PROBLEMS = []

UART = """uart1:
  txd_pin: gpio.17
  rxd_pin: gpio.16
  rts_pin: gpio.4
  baud: 9600
  mode: 8N1
"""
AXES = """axes:
  x:
    steps_per_mm: 80
    max_rate_mm_per_min: 6000
    acceleration_mm_per_sec2: 200
    max_travel_mm: 300
    motor0:
      null_motor:
"""
SPINDLE = """YL620:
  uart_num: 1
  modbus_id: 1
  spinup_ms: 0
  spindown_ms: 0
  speed_map: 0=0% 24000=100%
"""
BUS = """modbus_bus:
  uart_num: 1
  poll_ms: 50
extenders:
  pinext0:
    modbus_io:
      modbus_id: 5
      inputs: 8
      input_function: 2
user_inputs:
  digital0_pin: pinext0.0
"""


def crc16(data):
    c = 0xFFFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c


def with_crc(d):
    c = crc16(d)
    return bytes(d) + bytes([c & 0xFF, c >> 8])


class Slaves:
    """YL620 at id 1, I/O module at id 5, on one pty."""

    def __init__(self, work):
        self.master, slave = pty.openpty()
        tty.setraw(self.master)
        open(os.path.join(work, "native_uart1"), "w").write(os.ttyname(slave) + "\n")
        self.silent = False
        self.hr = {0x0308: 0, 0x0309: 24000 // 6}  # YL620: min Hz*10, max rpm/6
        self.running = False
        self.inputs = 0b00000001  # module id 5, IN1 on
        self.requests = []
        threading.Thread(target=self.run, daemon=True).start()

    def answer(self, req):
        uid, fn = req[0], req[1]
        if uid == 1:
            if fn == 3:
                start, count = (req[2] << 8) | req[3], (req[4] << 8) | req[5]
                if start == 0x200B:  # output frequency, Hz*10
                    self.hr[0x200B] = self.hr.get(0x2001, 0) if self.running else 0
                if start == 0x9999:
                    return with_crc([uid, fn | 0x80, 2])  # illegal address
                data = []
                for r in range(start, start + count):
                    v = self.hr.get(r, 0)
                    data += [(v >> 8) & 0xFF, v & 0xFF]
                return with_crc([uid, fn, len(data)] + data)
            if fn == 4:
                start, count = (req[2] << 8) | req[3], (req[4] << 8) | req[5]
                data = []
                for r in range(start, start + count):
                    data += [0x12, r & 0xFF]
                return with_crc([uid, fn, len(data)] + data)
            if fn in (1, 2):
                count = (req[4] << 8) | req[5]
                nb = (count + 7) // 8
                return with_crc([uid, fn, nb] + [0xA5] * nb)
            if fn == 6:
                reg, val = (req[2] << 8) | req[3], (req[4] << 8) | req[5]
                self.hr[reg] = val
                if reg == 0x2000:
                    self.running = val in (0x12, 0x22)
                return with_crc(req[:6])
            if fn == 16:
                reg, count = (req[2] << 8) | req[3], (req[4] << 8) | req[5]
                for i in range(count):
                    self.hr[reg + i] = (req[7 + 2 * i] << 8) | req[8 + 2 * i]
                return with_crc(req[:6])
            if fn == 5:
                return with_crc(req[:6])
            return with_crc([uid, fn | 0x80, 1])
        if uid == 5:
            if fn == 2:
                return with_crc([uid, 2, 1, self.inputs])
            if fn == 3:
                return with_crc([uid, fn | 0x80, 2])  # no register 0: an exception, still "there"
        return None

    def run(self):
        buf = b""
        while True:
            r, _, _ = select.select([self.master], [], [], 0.02)
            if not r:
                buf = b""
                continue
            try:
                buf += os.read(self.master, 256)
            except OSError:
                return
            while len(buf) >= 2:
                fn = buf[1]
                n = (9 + buf[6] if len(buf) >= 7 else 999) if fn in (15, 16) else 8
                if len(buf) < n:
                    break
                req, buf = buf[:n], buf[n:]
                if crc16(req[:-2]) != (req[-2] | (req[-1] << 8)):
                    buf = b""
                    break
                self.requests.append(bytes(req))
                ans = None if self.silent else self.answer(req)
                if ans:
                    time.sleep(0.003)
                    os.write(self.master, ans)


class Board:
    def __init__(self, cwd):
        self.p = subprocess.Popen([BIN, "-"], cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
        self.lines = []
        threading.Thread(target=self._read, daemon=True).start()
        time.sleep(2.0)

    def _read(self):
        buf = b""
        while True:
            c = self.p.stdout.read(1)
            if not c:
                return
            buf += c
            if c == b"\n":
                self.lines.append(buf.decode(errors="replace").strip())
                buf = b""

    def cmd(self, text, wait=5.0):
        """Send a line, return the lines up to and including ok / error:N."""
        n = len(self.lines)
        self.p.stdin.write((text + "\n").encode())
        self.p.stdin.flush()
        t0 = time.time()
        while time.time() - t0 < wait:
            new = self.lines[n:]
            for i, line in enumerate(new):
                if line == "ok" or line.startswith("error:"):
                    return [l for l in new[: i + 1] if not l.startswith("<")]
            time.sleep(0.01)
        return [l for l in self.lines[n:] if not l.startswith("<")] + ["(timeout)"]

    def status(self):
        n = len(self.lines)
        self.p.stdin.write(b"?")
        self.p.stdin.flush()
        for _ in range(100):
            time.sleep(0.02)
            for line in self.lines[n:]:
                if line.startswith("<"):
                    return line
        return ""

    def since(self, n):
        return self.lines[n:]

    def close(self):
        self.p.kill()


def check(ok, msg):
    print(("PASS " if ok else "FAIL ") + msg)
    if not ok:
        PROBLEMS.append(msg)


def has(lines, pattern):
    return any(re.search(pattern, l) for l in lines)


def vfd_state(b):
    r = b.cmd("$VFD/State")
    for l in r:
        m = re.match(r"\[VFD:(\w+),id=(\d+),answering=(\d),rpm=(\d+),errors=(\d+),last=(.*)\]$", l)
        if m:
            return {"name": m[1], "id": int(m[2]), "answering": int(m[3]), "rpm": int(m[4]), "errors": int(m[5]), "last": m[6]}
    return {"raw": r}


def wait_for(fn, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = fn()
        if v:
            return v
        time.sleep(0.2)
    return None


def common_checks(b, s, label):
    st = wait_for(lambda: (lambda v: v if v.get("answering") == 1 else None)(vfd_state(b)), 6)
    check(bool(st), f"{label}: $VFD/State answering=1 without turning the spindle on ({st or vfd_state(b)})")
    st = vfd_state(b)
    check(st.get("name") == "YL620" and st.get("id") == 1 and st.get("last") == "", f"{label}: name, id, empty last ({st})")

    # Speed sync still works, and rpm follows
    r = b.cmd("M3 S12000", wait=8)
    check(r[-1] == "ok", f"{label}: M3 S12000 ok ({r[-3:]})")
    check(s.running and s.hr.get(0x2001) == 12000 * 10 // 60, f"{label}: drive running at 200.0 Hz (reg {s.hr.get(0x2001)})")
    time.sleep(1.5)
    st = vfd_state(b)
    check(st.get("rpm") == 12000, f"{label}: $VFD/State rpm=12000 while running ({st})")
    r = b.cmd("M3 S6000", wait=8)
    check(r[-1] == "ok" and s.hr.get(0x2001) == 1000, f"{label}: M3 S6000 changes speed ({r[-2:]}, reg {s.hr.get(0x2001)})")
    r = b.cmd("M5", wait=8)
    time.sleep(1)
    check(r[-1] == "ok" and not s.running, f"{label}: M5 stops the drive ({r[-2:]})")

    # Read
    r = b.cmd("$Modbus/Read")
    check(has(r, r"\[MSG:INFO: Usage: \$Modbus/Read=") and r[-1] == "ok", f"{label}: $Modbus/Read alone prints usage + ok ({r})")
    r = b.cmd("$Modbus/Read=1,3,0x0308,2")
    check("[MODBUS:READ:1,3,776=0,4000]" in r and r[-1] == "ok", f"{label}: read holding 0x0308 x2 ({r})")
    r = b.cmd("$MBR=1,4,10,3")
    check("[MODBUS:READ:1,4,10=4618,4619,4620]" in r, f"{label}: read input registers ({r})")
    r = b.cmd("$MBR=1,1,0,10")
    check("[MODBUS:READ:1,1,0=1,0,1,0,0,1,0,1,1,0]" in r, f"{label}: read coils as bits ({r})")
    r = b.cmd("$MBR=1,2,0,3")
    check("[MODBUS:READ:1,2,0=1,0,1]" in r, f"{label}: read discrete inputs ({r})")
    r = b.cmd("$MBR=1,3,0x9999")
    check("[MODBUS:ERR:1,exception 2]" in r and r[-1].startswith("error:"), f"{label}: exception answer ({r})")
    r = b.cmd("$MBR=9,3,0", wait=3)
    check("[MODBUS:ERR:9,no answer]" in r and r[-1].startswith("error:"), f"{label}: no answer from id 9 ({r})")
    for bad in ("1,7,0", "1,3", "1,3,0,33", "300,3,0", "1,3,zz"):
        r = b.cmd("$MBR=" + bad)
        check(r[-1] == "error:81", f"{label}: bad arguments {bad} -> error:81 ({r})")

    # Write
    r = b.cmd("$Modbus/Write=1,0x0100,1234")
    check("[MODBUS:WROTE:1,256]" in r and s.hr.get(0x100) == 1234, f"{label}: write one register, function 6 ({r})")
    r = b.cmd("$MBW=1,0x0200,1,2,65535")
    check("[MODBUS:WROTE:1,512]" in r and [s.hr.get(0x200 + i) for i in range(3)] == [1, 2, 65535], f"{label}: write 3 registers, function 16 ({r})")
    r = b.cmd("$MBW=1,coil:7,1")
    check("[MODBUS:WROTE:1,coil:7]" in r and bytes([1, 5, 0, 7, 0xFF, 0]) in [q[:6] for q in s.requests[-5:]], f"{label}: write coil, function 5 ({r})")
    r = b.cmd("$MBW=1,5,70000")
    check(r[-1] == "error:81", f"{label}: value out of range refused ({r})")

    # Raw
    r = b.cmd("$Modbus/Raw=01 03 03 08 00 02")
    check(has(r, r"^\[MODBUS:RX:01 03 04 00 00 0F A0\]$"), f"{label}: raw read ({r})")
    r = b.cmd("$MBX=01 41 00 00 00 00")
    check(has(r, r"^\[MODBUS:RX:01 C1 01\]$"), f"{label}: raw keeps an exception answer ({r})")
    r = b.cmd("$MBX=01 0")
    check(r[-1] == "error:81", f"{label}: odd hex refused ({r})")

    # Scan
    r = b.cmd("$Modbus/Scan=1-6", wait=8)
    check("[MODBUS:FOUND:1]" in r and "[MODBUS:FOUND:5]" in r and "[MODBUS:SCAN:done,2 found]" in r, f"{label}: scan finds ids 1 and 5 ({r})")
    r = b.cmd("$MBS=5-2")
    check(r[-1] == "error:81", f"{label}: bad scan range ({r})")

    # Not while moving
    b.cmd("$X")
    b.cmd("G91 G1 X50 F600")
    time.sleep(0.3)
    r = b.cmd("$MBW=1,256,1")
    check(r[-1] in ("error:8", "error:120"), f"{label}: write refused while moving ({r})")
    # A read waits for the moves before it (it runs on the protocol task), so
    # it never runs while the axis moves
    r = b.cmd("$MBR=1,3,0", wait=10)
    st = b.status()
    check(r[-1] in ("error:8", "error:120") or (r[-1] == "ok" and st.startswith("<Idle")), f"{label}: read refused or run after the move ({r}, {st[:6]})")
    r = b.cmd("$VFD/State")
    check(has(r, r"^\[VFD:") and r[-1] == "ok", f"{label}: $VFD/State works while moving ({r})")
    check(bool(wait_for(lambda: b.status().startswith("<Idle"), 10)), f"{label}: move finishes")

    # The drive goes quiet, then comes back
    n = len(b.lines)
    s.silent = True
    st = wait_for(lambda: (lambda v: v if v.get("answering") == 0 else None)(vfd_state(b)), 20)
    check(bool(st) and st.get("last") == "no response", f"{label}: answering=0, last=no response when the drive is silent ({st or vfd_state(b)})")
    time.sleep(3)
    warns = [l for l in b.since(n) if "VFD not answering" in l]
    check(warns == ["[MSG:WARN: VFD not answering (no response)]"], f"{label}: one 'not answering' warning ({warns})")
    n = len(b.lines)
    s.silent = False
    st = wait_for(lambda: (lambda v: v if v.get("answering") == 1 else None)(vfd_state(b)), 10)
    check(bool(st) and st.get("last") == "" and st.get("errors", 0) >= 3, f"{label}: answering again, errors counted ({st})")
    time.sleep(0.5)
    check(has(b.since(n), r"\[MSG:INFO: VFD answering again\]"), f"{label}: 'answering again' message")
    return


def main():
    if not os.path.exists(BIN):
        sys.exit(f"no program at {BIN}")

    for label, extra in (("VFD alone", ""), ("VFD + modbus_bus", BUS)):
        work = tempfile.mkdtemp(prefix="vfd_check_")
        os.makedirs(os.path.join(work, "native_localfs"))
        open(os.path.join(work, "native_localfs", "config.yaml"), "w").write(
            "name: vfd check\nboard: host\n" + UART + AXES + SPINDLE + extra)
        s = Slaves(work)
        b = Board(work)
        try:
            if extra:
                r = b.cmd("$Modbus/Values")
                time.sleep(0.5)
                r = b.cmd("$Modbus/Values")
                check(has(r, r"modbus_io 5") or has(r, r"5"), f"{label}: the I/O module still reads ({r})")
            common_checks(b, s, label)
            errs = [l for l in b.lines if "ALARM" in l or "Critical" in l]
            check(not errs, f"{label}: no alarm ({errs[:3]})")
        finally:
            b.close()

    # No VFD, no bus
    work = tempfile.mkdtemp(prefix="vfd_check_")
    os.makedirs(os.path.join(work, "native_localfs"))
    open(os.path.join(work, "native_localfs", "config.yaml"), "w").write("name: vfd check\nboard: host\n" + AXES)
    b = Board(work)
    try:
        r = b.cmd("$VFD/State")
        check("[MSG:INFO: No VFD spindle]" in r and r[-1] == "ok", f"no VFD: $VFD/State ({r})")
        r = b.cmd("$MBR=1,3,0")
        check("[MODBUS:ERR:no bus]" in r and r[-1].startswith("error:"), f"no bus: read ({r})")
        r = b.cmd("$MBS")
        check("[MODBUS:ERR:no bus]" in r, f"no bus: scan ({r})")
        r = b.cmd("$Modbus/Read")
        check(r[-1] == "ok", f"no bus: usage still answers ok ({r})")
    finally:
        b.close()

    print(f"\n{len(PROBLEMS)} problem(s)" if PROBLEMS else "\nall passed")
    sys.exit(1 if PROBLEMS else 0)


if __name__ == "__main__":
    main()
