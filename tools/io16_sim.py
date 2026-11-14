#!/usr/bin/env python3
"""A simulated dpCREATOR IO16 expander on a pseudo terminal, for the host build
(pio run -e linux_x86_64).

    python3 tools/io16_sim.py [--dir <run dir>] [--uart 1] [--inputs 00A0] [--id <ID>] [--quiet]

It writes the pty's name to <dir>/native_uart<uart>, so the host build's uartN
(the uart_num of a uart_channel) talks to it. Speaks the expander protocol of
the real firmware (val218/dpcreator-io16, src/proto.c):

  [EXP:ID]         -> "(EXP,dpCREATOR IO16 v1.0.0)"
  [EXP:io.N=...]   -> ACK (U+00B2)
  [EXP:MON=1/0]    monitor stream on/off (no ACK); [EXP:STATE] one frame (no ACK)
  [MSG:RST]        drop the configuration
  <...> lines      heartbeat (status reports); outputs drop after 1 s without
  U+0100+N/0140+N  output N off / on;  U+10000+(N<<10)+duty  PWM

Monitor frame: 5 x U+A000 + (index<<8) + byte (IN1-8, IN9-16, OUT1-8,
OUT9-16, flags). Inputs come from <dir>/io16_inputs (hex, re-read every
100 ms; --inputs sets the start value); an input change on a configured
input is sent to FluidNC as U+0140+N / U+0100+N. Ctrl-C to stop.
"""
import argparse
import os
import pty
import select
import time
import tty

ID = "dpCREATOR IO16 v1.0.0"
HB_TIMEOUT = 1.0
MON_MIN = 0.020
MON_PERIOD = 1.0


def cp(c):
    return chr(c).encode("utf-8")


class IO16:
    def __init__(self, fd, inputs_path, quiet, ident=ID):
        self.fd = fd
        self.id = ident
        self.inputs_path = inputs_path
        self.quiet = quiet
        self.reset()
        self.inputs = self.read_inputs(0)
        self.mon_last = None
        self.mon_t = 0.0
        self.line = b""
        self.utf = b""

    def reset(self):
        self.modes = {}  # n -> "in"/"out"/"pwm"
        self.low = set()
        self.out = 0
        self.configured = False
        self.hb_t = None
        self.cfg_error = False
        self.mon = False
        self.mon_once = False
        self.reported = 0

    def log(self, *a):
        if not self.quiet:
            print("io16:", *a, flush=True)

    def send(self, b):
        os.write(self.fd, b)

    def read_inputs(self, default):
        try:
            with open(self.inputs_path) as f:
                return int(f.read().strip() or "0", 16) & 0xFFFF
        except (OSError, ValueError):
            return default

    def link(self, now):
        return self.configured and self.hb_t is not None and now - self.hb_t < HB_TIMEOUT

    def flags(self, now):
        return (1 if self.link(now) else 0) | (2 if self.configured else 0) | (4 if self.link(now) else 0) | (8 if self.cfg_error else 0)

    # ---- receive
    def rx(self, data):
        for b in data:
            if self.utf or b >= 0xC0:
                self.utf += bytes([b])
                need = 2 if self.utf[0] < 0xE0 else 3 if self.utf[0] < 0xF0 else 4
                if len(self.utf) == need:
                    c = ord(self.utf.decode("utf-8", "replace"))
                    self.utf = b""
                    if self.line[:1] in (b"<", b"[") and not self.line.endswith((b">", b"]")):
                        continue  # a non-ASCII letter inside a text line
                    self.codepoint(c)
                continue
            if b in (10, 13):
                if self.line:
                    self.process(self.line.decode("latin-1"))
                self.line = b""
            elif b >= 0x20 and len(self.line) < 300:
                self.line += bytes([b])

    def codepoint(self, c):
        if 0x100 <= c < 0x180:
            n, on = (c - 0x100) & 0x3F, c >= 0x140
            if 16 <= n < 32 and self.modes.get(n) == "out":
                bit = 1 << (n - 16)
                phys = on != (n in self.low)
                self.out = (self.out | bit) if phys else (self.out & ~bit)
                self.log("OUT%d %s" % (n - 15, "on" if phys else "off"))
        elif 0x10000 <= c < 0x10000 + (64 << 10):
            n, duty = (c - 0x10000) >> 10, (c - 0x10000) & 0x3FF
            if 16 <= n < 32 and self.modes.get(n) == "pwm":
                bit = 1 << (n - 16)
                self.out = (self.out | bit) if duty > 0 else (self.out & ~bit)
                self.log("OUT%d pwm %d" % (n - 15, duty))

    def process(self, s):
        if s.startswith("<"):
            self.hb_t = time.monotonic()
            return
        if s.startswith("[MSG:RST]"):
            self.log("RST")
            self.reset()
            return
        if not s.startswith("[EXP:"):
            return
        p = s[5:]
        self.log("<-", s)
        if p.startswith("ID]"):
            self.send(("(EXP,%s)" % self.id).encode())
        elif p.startswith("io."):
            self.config(p[3:].rstrip("]"))
        elif p.startswith("MON=1]"):
            self.mon = True
            self.mon_last = None
        elif p.startswith("MON=0]"):
            self.mon = False
        elif p.startswith("STATE]"):
            self.mon_once = True
        else:
            self.send(cp(0xB3))

    def config(self, spec):
        try:
            num, rest = spec.split("=", 1)
            n = int(num)
        except ValueError:
            self.send(cp(0xB3))
            return
        opts = rest.split(",")
        mode = opts[0]
        ok = (mode == "in" and n < 16) or (mode in ("out", "pwm") and 16 <= n < 32)
        if not ok:
            self.cfg_error = True
            self.send(cp(0xB3))
            return
        self.modes[n] = mode
        if "low" in opts:
            self.low.add(n)
        self.configured = True
        self.send(cp(0xB2))
        if mode == "in":
            self.send_input(n)

    def send_input(self, n):
        active = bool(self.inputs >> n & 1) != (n in self.low)
        self.send(cp((0x140 if active else 0x100) + n))
        if active:
            self.reported |= 1 << n
        else:
            self.reported &= ~(1 << n)

    # ---- periodic
    def poll(self, now):
        self.inputs = self.read_inputs(self.inputs)
        for n, m in self.modes.items():
            if m == "in":
                active = bool(self.inputs >> n & 1) != (n in self.low)
                if active != bool(self.reported >> n & 1):
                    self.send_input(n)
        if self.out and not self.link(now):
            self.log("no heartbeat: outputs off")
            self.out = 0
        if not (self.mon or self.mon_once):
            return
        f = [self.inputs & 0xFF, self.inputs >> 8, self.out & 0xFF, self.out >> 8, self.flags(now)]
        age = now - self.mon_t
        if self.mon_once or self.mon_last is None or (f != self.mon_last and age >= MON_MIN) or age >= MON_PERIOD:
            self.send(b"".join(cp(0xA000 + (i << 8) + v) for i, v in enumerate(f)))
            self.mon_last, self.mon_t, self.mon_once = f, now, False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default=".")
    ap.add_argument("--uart", type=int, default=1)
    ap.add_argument("--inputs", default=None, help="start value of the inputs, hex (bit 0 = IN1)")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--id", default=ID, help="ID reply (anything not starting with 'dpCREATOR IO' gets no monitor)")
    a = ap.parse_args()

    master, slave = pty.openpty()
    tty.setraw(slave)
    name = os.ttyname(slave)
    inputs_path = os.path.join(a.dir, "io16_inputs")
    if a.inputs is not None:
        with open(inputs_path, "w") as f:
            f.write(a.inputs + "\n")
    with open(os.path.join(a.dir, "native_uart%d" % a.uart), "w") as f:
        f.write(name + "\n")
    print("io16_sim on", name, flush=True)

    sim = IO16(master, inputs_path, a.quiet, a.id)
    while True:
        r, _, _ = select.select([master], [], [], 0.01)
        if r:
            try:
                data = os.read(master, 4096)
            except OSError:
                data = b""
            if data:
                sim.rx(data)
        sim.poll(time.monotonic())


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
