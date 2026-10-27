#!/usr/bin/env python3
"""A simulated Modbus RTU I/O module (slave) on a pseudo terminal, for the
host build (pio run -e linux_x86_64).

    python3 tools/modbus_sim.py [--id 3] [--dir <run dir>] [--uart 1]

It writes the pty's name to <dir>/native_uart<uart>, so the host build's
uartN (RS485, half duplex) talks to it. State lives in <dir>/modbus_sim.json:

    {"inputs": [0,1,0,0,0,0,0,0], "input_registers": [512, 0, ...],
     "coils": [...], "holding_registers": [...]}

Edit "inputs" / "input_registers" while it runs to change what FluidNC reads;
"coils" / "holding_registers" show what FluidNC wrote. Each request and
answer is printed. Functions: 1, 2, 3, 4, 5, 6, 15, 16. Ctrl-C to stop.
"""
import argparse
import json
import os
import pty
import select
import time
import tty


def crc16(data):
    c = 0xFFFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c


def with_crc(data):
    c = crc16(data)
    return bytes(data) + bytes([c & 0xFF, c >> 8])


class Sim:
    def __init__(self, path, n=64):
        self.path = path
        self.state = {"inputs": [0] * n, "coils": [0] * n, "input_registers": [0] * n, "holding_registers": [0] * n}
        self.save()

    def load(self):
        try:
            with open(self.path) as f:
                d = json.load(f)
            for k in self.state:
                if k in d:
                    v = list(d[k]) + [0] * (len(self.state[k]) - len(d[k]))
                    self.state[k] = v[: len(self.state[k])]
        except (OSError, ValueError):
            pass

    def save(self):
        tmp = self.path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(self.state, f)
        os.replace(tmp, self.path)

    def handle(self, req):
        self.load()
        uid, fn = req[0], req[1]
        s = self.state
        if fn in (1, 2):
            start, count = (req[2] << 8) | req[3], (req[4] << 8) | req[5]
            bits = (s["coils"] if fn == 1 else s["inputs"])[start : start + count]
            nbytes = (count + 7) // 8
            out = [0] * nbytes
            for i, b in enumerate(bits):
                if b:
                    out[i // 8] |= 1 << (i % 8)
            return with_crc([uid, fn, nbytes] + out)
        if fn in (3, 4):
            start, count = (req[2] << 8) | req[3], (req[4] << 8) | req[5]
            regs = (s["holding_registers"] if fn == 3 else s["input_registers"])[start : start + count]
            data = []
            for r in regs:
                data += [(r >> 8) & 0xFF, r & 0xFF]
            return with_crc([uid, fn, len(data)] + data)
        if fn == 5:
            addr = (req[2] << 8) | req[3]
            s["coils"][addr] = 1 if req[4] == 0xFF else 0
            self.save()
            return with_crc(req[:6])
        if fn == 6:
            addr = (req[2] << 8) | req[3]
            s["holding_registers"][addr] = (req[4] << 8) | req[5]
            self.save()
            return with_crc(req[:6])
        if fn == 15:
            start, count = (req[2] << 8) | req[3], (req[4] << 8) | req[5]
            for i in range(count):
                s["coils"][start + i] = (req[7 + i // 8] >> (i % 8)) & 1
            self.save()
            return with_crc(req[:6])
        if fn == 16:
            start, count = (req[2] << 8) | req[3], (req[4] << 8) | req[5]
            for i in range(count):
                s["holding_registers"][start + i] = (req[7 + 2 * i] << 8) | req[8 + 2 * i]
            self.save()
            return with_crc(req[:6])
        return with_crc([uid, fn | 0x80, 1])  # illegal function


def request_length(buf):
    if len(buf) < 2:
        return None
    fn = buf[1]
    if fn in (1, 2, 3, 4, 5, 6):
        return 8
    if fn in (15, 16):
        return 9 + buf[6] if len(buf) >= 7 else None
    return 8


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--id", type=int, default=3)
    ap.add_argument("--dir", default=".")
    ap.add_argument("--uart", type=int, default=1)
    ap.add_argument("--silent", action="store_true", help="do not answer (a module that is off)")
    a = ap.parse_args()

    master, slave = pty.openpty()
    tty.setraw(master)
    name = os.ttyname(slave)
    with open(os.path.join(a.dir, "native_uart%d" % a.uart), "w") as f:
        f.write(name + "\n")
    sim = Sim(os.path.join(a.dir, "modbus_sim.json"))
    print("Modbus slave id %d on %s" % (a.id, name), flush=True)

    buf = b""
    while True:
        r, _, _ = select.select([master], [], [], 0.5)
        if not r:
            buf = b""  # a gap ends a frame
            continue
        buf += os.read(master, 256)
        while True:
            n = request_length(buf)
            if n is None or len(buf) < n:
                break
            req, buf = buf[:n], buf[n:]
            if crc16(req[:-2]) != (req[-2] | (req[-1] << 8)):
                print("bad CRC", req.hex(" "), flush=True)
                buf = b""
                break
            if req[0] != a.id:
                continue
            ans = None if a.silent else sim.handle(req)
            print("rx", req.hex(" "), "-> tx", ans.hex(" ") if ans else "(silent)", flush=True)
            if ans:
                time.sleep(0.002)
                os.write(master, ans)


if __name__ == "__main__":
    main()
