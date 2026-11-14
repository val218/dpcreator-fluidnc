#!/usr/bin/env python3
"""Motion check for the host build: the S-curve ($Stepping/SCurve) against
straight ramps, from the segment trace ($Stepping/Trace).

    python3 tools/motion_check.py [.pio/build/linux_x86_64/program]

For Off, Light and Full it runs, from the host "SD card":
  - a test program (long moves, a short one, an arc, rapids, a 3-axis move),
  - 150 random moves with mixed feeds,
  - the random moves again, streamed line by line with a feed hold in the middle,
and checks that every run ends exactly where it should, that the run times
match (the S-curve keeps the planner's times), that the S-curve really shaped
the ramps, and that no segment changes speed faster than the acceleration
allows (a velocity jump would lose steps on a real machine).
"""
import math
import os
import random
import re
import subprocess
import sys
import tempfile
import threading
import time

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".pio/build/linux_x86_64/program")
ACCEL = 200.0  # mm/s^2, X and Y in the config below
PROBLEMS = []

CONFIG = """name: motion check
board: host
axes:
  x:
    steps_per_mm: 80
    max_rate_mm_per_min: 6000
    acceleration_mm_per_sec2: 200
    max_travel_mm: 1000
    motor0:
      null_motor:
  y:
    steps_per_mm: 80
    max_rate_mm_per_min: 6000
    acceleration_mm_per_sec2: 200
    max_travel_mm: 1000
    motor0:
      null_motor:
  z:
    steps_per_mm: 400
    max_rate_mm_per_min: 1000
    acceleration_mm_per_sec2: 50
    max_travel_mm: 100
    motor0:
      null_motor:
"""

PROG = """G21 G90
G1 X60 F3000
G1 X60.5 F3000
G1 Y3 F2000
G2 X80.5 Y3 I10 J0 F2500
G1 X0 Y0 F4000
G0 X40 Y20
G0 Z-5
G1 X41 Y21 Z-4 F1500
G0 X0 Y0 Z0
"""


def random_prog():
    random.seed(1)
    lines, x, y = ["G21 G90"], 0.0, 0.0
    for _ in range(150):
        x = max(0, min(50, x + random.uniform(-3, 3)))
        y = max(0, min(50, y + random.uniform(-3, 3)))
        lines.append(f"G1 X{x:.3f} Y{y:.3f} F{random.choice([1000, 3000, 5000])}")
    lines.append("G0 X0 Y0")
    return "\n".join(lines) + "\n"


class Board:
    def __init__(self, cwd):
        self.p = subprocess.Popen([BIN, "-"], cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
        self.lines = []
        threading.Thread(target=self._read, daemon=True).start()
        time.sleep(1.5)

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

    def send(self, text):
        self.p.stdin.write(text.encode())
        self.p.stdin.flush()

    def status(self):
        n = len(self.lines)
        self.send("?")
        for _ in range(100):
            time.sleep(0.02)
            for line in self.lines[n:]:
                if line.startswith("<"):
                    return line
        return ""

    def wait_idle(self, timeout=120):
        t0 = time.time()
        while time.time() - t0 < timeout:
            st = self.status()
            if st.startswith("<Idle"):
                return time.time() - t0, st
            time.sleep(0.05)
        return None, self.status()

    def trace(self):
        n = len(self.lines)
        self.send("$Stepping/Trace\n")
        time.sleep(2)
        out = []
        for line in self.lines[n:]:
            m = re.match(r"\[TRACE:([\d.]+),([\d.]+),(\w+)\]", line)
            if m:
                out.append((float(m.group(1)), float(m.group(2)), m.group(3)))
        return out

    def close(self):
        self.p.kill()


def mpos(st):
    m = re.search(r"MPos:([-\d.,]+)", st)
    return [float(v) for v in m.group(1).split(",")] if m else None


def jumps(pts, limit):
    out, prev = [], (0.0, 0.0)
    for t, v, kind in pts:
        dt = (t - prev[0]) / 1000.0
        if dt > 0:
            a = (v - prev[1]) / 60.0 / dt
            if abs(a) > limit:
                out.append((round(a), round(prev[1]), round(v), kind, t))
        prev = (t, v)
    return out


def check(ok, msg):
    print(("PASS " if ok else "FAIL ") + msg)
    if not ok:
        PROBLEMS.append(msg)


def run(work, mode, how):
    b = Board(work)
    b.send(f"$Stepping/SCurve={mode}\n$Stepping/Trace=On\n")
    time.sleep(0.3)
    t0 = time.time()
    if how == "stream":
        lines = open(os.path.join(work, "native_sd", "random.nc")).read().splitlines()
        sent, base, held = 0, len(b.lines), False
        while sent < len(lines):
            acked = sum(1 for line in b.lines[base:] if line == "ok" or line.startswith("error"))
            if sent - acked < 4:
                b.send(lines[sent] + "\n")
                sent += 1
            else:
                time.sleep(0.002)
            if not held and time.time() - t0 > 3:
                b.send("!")
                time.sleep(0.8)
                b.send("~")
                held = True
    else:
        b.send(f"$SD/Run=/{how}\n")
        time.sleep(0.3)
    t, st = b.wait_idle()
    pts = b.trace()
    errs = [line for line in b.lines if "ERR" in line or "ALARM" in line or line.startswith("error")]
    b.close()
    return t, st, pts, errs


def main():
    if not os.path.exists(BIN):
        sys.exit(f"no program at {BIN}")
    work = tempfile.mkdtemp(prefix="motion_check_")
    os.makedirs(os.path.join(work, "native_localfs"))
    os.makedirs(os.path.join(work, "native_sd"))
    open(os.path.join(work, "native_localfs", "config.yaml"), "w").write(CONFIG)
    open(os.path.join(work, "native_sd", "prog.nc"), "w").write(PROG)
    open(os.path.join(work, "native_sd", "random.nc"), "w").write(random_prog())

    times = {}
    for how in ("prog.nc", "random.nc", "stream"):
        for mode, peak in (("Off", 1.0), ("Light", 1.25), ("Full", 1.5)):
            t, st, pts, errs = run(work, mode, how)
            pos = mpos(st)
            shaped = sum(1 for p in pts if p[2].endswith("S"))
            # path acceleration of a diagonal X/Y move is up to sqrt(2) x the axis
            # value; the trace samples 10 ms segments, so allow 20 % on top
            limit = ACCEL * math.sqrt(2) * peak * 1.2
            j = jumps(pts, limit)
            label = f"{mode:5} {how:9}"
            check(pos == [0.0, 0.0, 0.0], f"{label} ends at 0,0,0 ({pos})")
            check(not errs, f"{label} no errors ({errs[:2]})")
            check(not j, f"{label} no speed change faster than {round(limit)} mm/s^2 in {len(pts)} segments ({j[:3]})")
            if mode != "Off":
                check(shaped > len(pts) * 0.05, f"{label} S-shaped segments: {shaped} of {len(pts)}")
            if how != "stream":
                times.setdefault(how, {})[mode] = t
        if how in times:
            ts = times[how]
            spread = max(ts.values()) - min(ts.values())
            check(spread < 0.05 * max(ts.values()) + 0.3, f"{how}: same run time with every mode ({ {k: round(v, 2) for k, v in ts.items()} })")

    print(f"\n{len(PROBLEMS)} problem(s)" if PROBLEMS else "\nall passed")
    sys.exit(1 if PROBLEMS else 0)


if __name__ == "__main__":
    main()
