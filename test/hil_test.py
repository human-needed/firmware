#!/usr/bin/env python3
"""Hardware-in-loop test: drives a real puck through every hn-usb/2 command and the lease rules.

    pip install pyserial
    python hil_test.py                 # finds the puck by USB VID and product name
    python hil_test.py --port COM13
    python hil_test.py --loud          # keep the puck's volume (default: volume 0 while testing)
    python hil_test.py --bootsel build/humanneeded.ino.uf2
                                       # also tests BOOTSEL: reboots into the bootloader and copies
                                       # this UF2 back onto the RPI-RP2 drive

Settings are read at the start and written back at the end. Takes about 1.5 minutes.
Exit code 0 when everything passed.
"""
import argparse
import json
import os
import re
import shutil
import string
import sys
import time

import serial
import serial.tools.list_ports

VID = 0x2E8A
REPLY_LIMIT = 0.5  # seconds, contract section 1

results = []       # (name, ok, detail)
max_latency = [0.0, ""]


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))
    print(("PASS " if ok else "FAIL ") + name + (f"  ({detail})" if detail and not ok else ""))
    return ok


def find_port(serial_no=None, timeout=10):
    end = time.time() + timeout
    while time.time() < end:
        for p in serial.tools.list_ports.comports():
            if p.vid == VID and (p.product == "HumanNeeded" or (p.serial_number or "").startswith("HN")):
                if serial_no is None or p.serial_number == serial_no:
                    return p.device
        time.sleep(0.2)
    return None


class Puck:
    def __init__(self, port):
        self.port = port
        self.events = []  # "!..." lines seen
        self.s = None
        self.open()

    def open(self, timeout=10):
        end = time.time() + timeout
        while True:
            try:
                self.s = serial.Serial(self.port, 115200, timeout=0.05)
                break
            except serial.SerialException:
                if time.time() > end:
                    raise
                time.sleep(0.3)  # another program may have it open for a moment
        time.sleep(0.2)
        self.drain()

    def close(self):
        if self.s:
            self.s.close()
            self.s = None

    def drain(self, wait=0.0):
        """Reads whatever is pending; keeps '!' events."""
        end = time.time() + wait
        buf = b""
        while True:
            chunk = self.s.read(4096)
            buf += chunk
            if not chunk and time.time() >= end:
                break
        for line in buf.decode(errors="replace").splitlines():
            if line.startswith("!"):
                self.events.append(line.strip())
        return buf

    def raw(self, data):
        self.s.write(data)

    def readline(self, timeout=2.0):
        end = time.time() + timeout
        buf = b""
        while time.time() < end:
            buf += self.s.read(1)
            if buf.endswith(b"\n"):
                return buf.decode(errors="replace").rstrip("\r\n")
        return None

    def cmd(self, line, timeout=2.0, track=True):
        """Sends one command, returns its one reply line (skipping '!' events) and the latency."""
        self.s.write(line.encode() + b"\n")
        t0 = time.time()
        while True:
            reply = self.readline(timeout - (time.time() - t0))
            if reply is None:
                return None
            if reply.startswith("!"):
                self.events.append(reply)
                continue
            dt = time.time() - t0
            if track and dt > max_latency[0]:
                max_latency[0], max_latency[1] = dt, line.split(" ")[0]
            return reply

    def json(self, line):
        r = self.cmd(line)
        try:
            return json.loads(r)
        except Exception:
            return {"_raw": r}

    def no_more_replies(self, wait=0.3):
        extra = self.drain(wait).decode(errors="replace")
        lines = [l for l in extra.splitlines() if l.strip() and not l.startswith("!")]
        return lines


def status(p):
    return p.json("STATUS")


def sounds(p):
    return status(p).get("sounds")


# ---------------------------------------------------------------------------------------------

def test_identity(p):
    h = p.json("HELLO")
    check("HELLO name/fw/proto", h.get("name") == "HumanNeeded" and h.get("fw") == "2.0.0" and h.get("proto") == 2, h)
    check("HELLO board", h.get("board") in ("hn-v1", "hn-v2", "kb2040-proto0"), h.get("board"))
    check("HELLO serial = HN + 16 hex", re.fullmatch(r"HN[0-9A-F]{16}", h.get("serial", "")) is not None, h.get("serial"))
    usb = [x for x in serial.tools.list_ports.comports() if x.device == p.port]
    if usb:
        check("USB VID 0x2E8A, serial matches HELLO", usb[0].vid == VID and usb[0].serial_number == h.get("serial"),
              f"{usb[0].vid:#06x} {usb[0].serial_number}")
    return h


def test_lines(p):
    check("unknown command -> ERR", p.cmd("FOO") == "ERR unknown command")
    for gone in ("EV working abc", "CLEAR", "SKIP 5"):
        check(f"removed: {gone.split()[0]}", p.cmd(gone) == "ERR unknown command")
    p.raw(b"\n\r\n   \n")
    check("empty lines get no reply", p.no_more_replies() == [])
    check("lower-case command accepted", p.json("hello").get("proto") == 2)
    check("1025-byte line -> ERR line too long", p.cmd("X" * 1025) == "ERR line too long")
    check("one reply only for a 3000-byte line", p.cmd("X" * 3000) == "ERR line too long" and p.no_more_replies() == [])
    check("1024-byte line accepted as a line", p.cmd("X" * 1024) == "ERR unknown command")
    check("still answering after long lines", p.json("HELLO").get("proto") == 2)
    p.raw(b"HEL")
    time.sleep(0.8)
    check("half line dropped after 500 ms", p.json("HELLO").get("proto") == 2)
    p.raw(b"HEL")
    time.sleep(0.2)
    check("half line kept within 500 ms", p.json("LO").get("proto") == 2)
    check("\\r ignored", (p.raw(b"HELLO\r\n") or True) and json.loads(p.readline()).get("proto") == 2)
    check("args on no-arg command -> ERR", (p.cmd("HELLO x") or "").startswith("ERR"))


def test_snap(p):
    p.cmd("SNAP 30")
    bad = ["SNAP", "SNAP 4", "SNAP 601", "SNAP 30x", "SNAP 30 0A1B2C3D:needs", "SNAP 30 0a1b2c3:needs",
           "SNAP 30 0a1b2c3d:done", "SNAP 30 0a1b2c3d:idle", "SNAP 30 0a1b2c3d", "SNAP 30 0a1b2c3d:needs 0a1b2c3d:working",
           "SNAP 30 " + " ".join(f"{i:08x}:working" for i in range(33))]
    p.cmd("SNAP 30 aaaaaaaa:working")
    for b in bad:
        r = p.cmd(b)
        if not check(f"rejects: {b[:40]}", r and r.startswith("ERR"), r):
            pass
    st = status(p)
    check("bad SNAP changes nothing", st.get("entries") == [{"id": "aaaaaaaa", "state": "working"}], st)
    check("SNAP 32 entries OK", p.cmd("SNAP 30 " + " ".join(f"{i:08x}:working" for i in range(32))) == "OK")
    check("STATUS lists 32 entries", len(status(p).get("entries", [])) == 32)

    p.cmd("SNAP 30")
    st = status(p)
    check("STATUS shape", set(["proto", "lease_left", "shown", "muted", "entries"]) <= set(st) and st["proto"] == 2, st)
    check("SNAP 30 = nothing active", st.get("entries") == [] and st.get("shown") == "idle" and 29 <= st.get("lease_left", 0) <= 30, st)

    p.cmd("SNAP 30 00000001:working")
    check("working shown", status(p).get("shown") == "working")
    n0 = sounds(p)
    p.cmd("SNAP 30 00000001:working 00000002:needs")
    st = status(p)
    check("needs shown over working", st.get("shown") == "needs", st)
    check("needs sound when an id enters needs", st.get("sounds") == n0 + 1, (n0, st.get("sounds")))
    p.cmd("SNAP 30 00000001:working 00000002:needs")
    check("no sound for an id already in needs", sounds(p) == n0 + 1)
    p.cmd("SNAP 30 00000001:needs 00000002:needs")
    check("sound for a second id entering needs", sounds(p) == n0 + 2)
    p.cmd("SNAP 30 00000001:working")
    p.cmd("SNAP 30 00000001:needs")
    check("sound when an id comes back to needs", sounds(p) == n0 + 3)
    p.cmd("SNAP 30")
    check("idle after empty snapshot", status(p).get("shown") == "idle")

    # mute
    p.cmd("MUTE on")
    n0 = sounds(p)
    p.cmd("SNAP 30 00000005:needs")
    check("muted: no needs sound", sounds(p) == n0)
    p.cmd("SNAP 30")
    p.cmd("MUTE 0")
    p.cmd("SNAP 30 00000005:needs")
    check("unmuted: needs sound again", sounds(p) == n0 + 1)
    p.cmd("SNAP 30")


def test_lease(p):
    p.cmd("SNAP 5 0000000a:needs 0000000b:working")
    st = status(p)
    check("lease_left counts down from 5", 4 <= st.get("lease_left", 0) <= 5, st)
    n0 = st.get("sounds")
    time.sleep(3)
    check("still active before the lease ends", status(p).get("shown") == "needs")
    time.sleep(2.6)
    st = status(p)
    check("lease ran out: table cleared, idle", st.get("entries") == [] and st.get("shown") == "idle" and st.get("lease_left") == 0, st)
    check("lease ran out: silent", st.get("sounds") == n0)
    p.cmd("SNAP 30 0000000a:needs")
    check("lapsed lease counts as empty: needs sounds again", sounds(p) == n0 + 1)
    # renewing keeps it alive
    p.cmd("SNAP 5 0000000a:needs")
    for _ in range(3):
        time.sleep(2)
        p.cmd("SNAP 5 0000000a:needs")
    check("lease renewed by each snapshot", status(p).get("shown") == "needs")
    p.cmd("SNAP 30")


def test_flash(p, done_sound):
    check("FLASH needs an argument", (p.cmd("FLASH") or "").startswith("ERR"))
    check("FLASH only takes done", (p.cmd("FLASH needs") or "").startswith("ERR"))
    p.cmd("SET done.duration 2")
    p.cmd("SNAP 30 0000000c:needs")
    n0 = sounds(p)
    check("FLASH done -> OK", p.cmd("FLASH done") == "OK")
    st = status(p)
    check("FLASH done shows over needs", st.get("shown") == "done", st)
    check("FLASH done plays the done sound", st.get("sounds") == n0 + 1 and (done_sound == "none" or st.get("last_sound") == done_sound), st)
    check("FLASH is not stored in the table", st.get("entries") == [{"id": "0000000c", "state": "needs"}], st)
    time.sleep(2.3)
    check("done light ends after done.duration", status(p).get("shown") == "needs")
    p.cmd("SNAP 30")


def test_test(p):
    check("TEST bogus -> ERR", (p.cmd("TEST bogus") or "").startswith("ERR"))
    check("TEST hello -> OK", p.cmd("TEST hello") == "OK")
    time.sleep(1.6)
    p.cmd("SNAP 30")
    check("TEST needs -> OK", p.cmd("TEST needs") == "OK")
    check("TEST shows the preview", status(p).get("shown") == "needs")
    p.cmd("SNAP 30")
    check("SNAP that changes nothing keeps the preview", status(p).get("shown") == "needs")
    p.cmd("SNAP 30 0000000d:working")
    check("SNAP that changes what's shown ends the preview at once", status(p).get("shown") == "working")
    p.cmd("SNAP 30")
    p.cmd("TEST working")
    p.cmd("FLASH done")
    check("FLASH done ends the preview", status(p).get("shown") == "done")
    time.sleep(2.3)
    p.cmd("SET working.duration 0")
    p.cmd("TEST working")
    time.sleep(9.5)
    check("TEST still showing at 9.5 s", status(p).get("shown") == "working")
    time.sleep(1.0)
    check("TEST capped at 10 s", status(p).get("shown") == "idle")
    for s in ("done", "idle"):
        check(f"TEST {s} -> OK", p.cmd(f"TEST {s}") == "OK")
    time.sleep(0.1)
    p.cmd("SNAP 30 0000000e:working")  # ends the idle preview
    p.cmd("SNAP 30")


def test_mute(p):
    for arg, want in (("0", "off"), ("5", "5"), ("1440", "1440"), ("on", "on"), ("0", "off")):
        check(f"MUTE {arg}", p.cmd(f"MUTE {arg}") == "OK" and status(p).get("muted") == want, status(p).get("muted"))
    for arg in ("1441", "-1", "abc", "", "5 6"):
        check(f"MUTE {arg!r} -> ERR", (p.cmd(f"MUTE {arg}".strip()) or "").startswith("ERR"))


def test_settings(p):
    check("SET needs.color", p.cmd("SET needs.color #123456") == "OK")
    check("GET needs.color", p.json("GET needs.color") == {"needs.color": "#123456"})
    for bad in ("SET needs.color 123456", "SET needs.color #12345G", "SET brightness 101", "SET brightness -1",
                "SET done.duration 0", "SET needs.duration 3601", "SET idle.sound ding", "SET needs.sound nope",
                "SET needs.repeat 6", "SET nope 1", "SET brightness", "SET"):
        check(f"rejects: {bad}", (p.cmd(bad) or "").startswith("ERR"))
    check("GET unknown -> ERR", (p.cmd("GET nope") or "").startswith("ERR"))
    a = p.json("GET all")
    check("GET all has every key, no idle sound", "needs.repeat" in a and "brightness" in a and "idle.sound" not in a and a.get("done.duration", 0) >= 1, a)

    sc_t0 = time.time()
    raw = p.cmd("SCHEMA")
    dt = time.time() - sc_t0
    try:
        sc = json.loads(raw)
    except Exception:
        sc = {}
    check("SCHEMA is one valid JSON line", bool(sc.get("groups")), (raw or "")[:80])
    check(f"SCHEMA within 500 ms", dt < REPLY_LIMIT, f"{dt*1000:.0f} ms")
    fields = {f["key"]: f for g in sc.get("groups", []) for f in g["fields"]}
    check("SCHEMA: done.duration has no 0", fields.get("done.duration", {}).get("options") == "done_duration" and
          all(v != "0" for v, _ in sc.get("lists", {}).get("done_duration", [["0", ""]])))
    check("SCHEMA: idle has no sound", not any(k.startswith("idle.") and k.split(".")[1] in ("sound", "volume", "repeat") for k in fields))

    # deferred flash save: 2 s after the last change, only if changed
    saves0 = status(p).get("saves")
    p.cmd("SET needs.brightness 90")
    p.cmd("SET needs.brightness 91")
    time.sleep(1.0)
    check("no flash write right after SET", status(p).get("saves") == saves0)
    p.cmd("SET needs.brightness 92")  # restarts the 2 s
    time.sleep(1.5)
    check("no write while changes keep coming", status(p).get("saves") == saves0)
    time.sleep(1.2)
    check("one write 2 s after the last change", status(p).get("saves") == saves0 + 1)
    p.cmd("SET needs.brightness 92")
    time.sleep(2.5)
    check("no write when nothing changed", status(p).get("saves") == saves0 + 1)
    p.cmd("SET needs.brightness 50")
    p.cmd("SET needs.brightness 92")
    time.sleep(2.5)
    check("no write when changed back", status(p).get("saves") == saves0 + 1)


def reconnect(p, serial_no, timeout=10):
    p.close()
    time.sleep(1.0)
    port = find_port(serial_no, timeout)
    if not port:
        return False
    p.port = port
    p.events = []
    p.open()
    return True


def test_reboot(p, serial_no):
    p.cmd("SET needs.color #654321")
    p.cmd("SNAP 30 0000000f:needs")
    check("REBOOT -> OK", p.cmd("REBOOT") == "OK")
    ok = check("puck back after REBOOT", reconnect(p, serial_no))
    if not ok:
        return False
    p.drain(0.5)
    check("!HELLO after connect", "!HELLO" in p.events, p.events)
    check("setting kept over REBOOT (saved before restart)", p.json("GET needs.color") == {"needs.color": "#654321"})
    st = status(p)
    check("power-up: table empty, idle, silent", st.get("entries") == [] and st.get("shown") == "idle" and st.get("sounds") == 0, st)
    check("no second !HELLO on the same connection", p.no_more_replies(0.5) == [] and p.events.count("!HELLO") == 1)
    return True


def test_reset(p):
    check("RESET -> OK", p.cmd("RESET") == "OK")
    a = p.json("GET all")
    check("RESET restores defaults", a.get("needs.color") == "#FFA62B" and a.get("brightness") == 60 and a.get("done.duration") == 3, a)


def test_selftest(p, serial_no):
    t = p.json("SELFTEST")
    check("SELFTEST JSON", t.get("selftest") == "started" and t.get("serial") == serial_no and t.get("leds") == 16 and
          t.get("button") in ("pressed", "released"), t)
    time.sleep(t.get("ms", 6000) / 1000 + 0.5)
    check("answers after SELFTEST", p.json("HELLO").get("proto") == 2)


def test_1200_baud(p, serial_no):
    p.close()
    try:
        s = serial.Serial(p.port, 1200)
        s.dtr = False
        time.sleep(0.2)
        s.close()
    except serial.SerialException as e:
        check("1200-baud open", False, str(e))
    time.sleep(2.5)
    drives = uf2_drives()
    port = find_port(serial_no, 3)
    check("1200-baud touch does not enter the bootloader", port is not None and not drives, (port, drives))
    if port:
        p.port = port
        p.open()


def uf2_drives():
    found = []
    if os.name == "nt":
        for letter in string.ascii_uppercase:
            if os.path.exists(f"{letter}:\\INFO_UF2.TXT"):
                found.append(f"{letter}:\\")
    return found


def test_bootsel(p, serial_no, uf2):
    check("BOOTSEL -> OK", p.cmd("BOOTSEL") == "OK")
    p.close()
    end = time.time() + 15
    drives = []
    while time.time() < end and not drives:
        time.sleep(0.5)
        drives = uf2_drives()
    if not check("bootloader drive appears", drives, "no RPI-RP2 drive"):
        return
    shutil.copy(uf2, drives[0])
    port = find_port(serial_no, 20)
    check("puck back after copying the UF2", port is not None)
    if port:
        p.port = port
        p.open()
        check("HELLO after update", p.json("HELLO").get("fw") == "2.0.0")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--loud", action="store_true", help="keep the volume (default: 0 while testing)")
    ap.add_argument("--bootsel", metavar="UF2", help="also test BOOTSEL and copy this UF2 back")
    args = ap.parse_args()

    port = args.port or find_port(timeout=3)
    if not port:
        sys.exit("no puck found (USB VID 2E8A, product HumanNeeded)")
    print(f"puck on {port}")
    p = Puck(port)
    hello = test_identity(p)
    serial_no = hello.get("serial")
    saved = p.json("GET all")
    if not args.loud:
        p.cmd("SET volume 0")  # sounds still count in STATUS
    p.cmd("MUTE 0")
    p.cmd("SET done.duration 3")
    done_sound = p.json("GET done.sound").get("done.sound")
    try:
        test_lines(p)
        test_snap(p)
        test_lease(p)
        test_flash(p, done_sound)
        test_test(p)
        test_mute(p)
        test_settings(p)
        test_selftest(p, serial_no)
        if test_reboot(p, serial_no):
            test_1200_baud(p, serial_no)
        test_reset(p)
        if args.bootsel:
            test_bootsel(p, serial_no, args.bootsel)
    finally:
        if p.s:
            for k, v in saved.items():
                if not k.startswith("_"):
                    p.cmd(f"SET {k} {v}")
            p.cmd("SNAP 30")
            p.close()
    check(f"every reply within 500 ms", max_latency[0] < REPLY_LIMIT, f"max {max_latency[0]*1000:.0f} ms on {max_latency[1]}")
    print(f"slowest reply: {max_latency[0]*1000:.0f} ms ({max_latency[1]})")
    failed = [r for r in results if not r[1]]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed")
    for name, _, detail in failed:
        print(f"  FAIL {name}  {detail}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
