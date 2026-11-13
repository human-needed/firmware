#!/usr/bin/env python3
"""Hardware checks that need a debug build (-DHN_DEBUG): amp switching, LED current cap, watchdog,
USB suspend. The debug build adds STATUS "dbg" and a HANG command.

    arduino-cli compile --fqbn "rp2040:rp2040:generic:flash=2097152_0,freq=125" \
        --build-property "compiler.cpp.extra_flags=-DHN_DEBUG" --output-dir build-dbg humanneeded
    (BOOTSEL, copy build-dbg/humanneeded.ino.uf2 onto RPI-RP2)
    python hil_debug.py                # amp, current cap, watchdog
    python hil_debug.py --after-sleep  # run after putting the PC to sleep and waking it

Settings are written back at the end. Put the release build back on the puck afterwards.
"""
import argparse
import sys
import time

from hil_test import Puck, check, find_port, reconnect, results

WDT_RESET = 4  # rp2040.getResetReason()


def dbg(p):
    return p.json("STATUS").get("dbg", {})


def test_amp(p):
    p.cmd("SNAP 30")
    time.sleep(0.5)
    check("amp off at rest", dbg(p).get("amp") == 0, dbg(p))
    p.cmd("SET needs.sound ding")
    p.cmd("SET needs.repeat 1")
    p.cmd("SNAP 30 00000001:needs")
    t0 = time.time()
    on = dbg(p).get("amp")
    check("amp on as the sound starts", on == 1, on)
    while dbg(p).get("amp") == 1 and time.time() - t0 < 5:
        time.sleep(0.02)
    dt = time.time() - t0
    check("amp off after the sound (ding 0.5 s + gap + tail)", 0.5 < dt < 1.5, f"{dt:.2f} s")
    p.cmd("SNAP 30")
    p.cmd("MUTE on")
    p.cmd("SNAP 30 00000002:needs")
    check("amp stays off when muted", dbg(p).get("amp") == 0)
    p.cmd("MUTE 0")
    p.cmd("SNAP 30")


def test_current_cap(p):
    for k, v in (("brightness", 100), ("needs.color", "#FFFFFF"), ("needs.light", "solid"),
                 ("needs.brightness", 100), ("needs.sound", "lullaby"), ("needs.repeat", 2)):
        p.cmd(f"SET {k} {v}")
    p.cmd("SNAP 30 00000003:needs")
    time.sleep(0.8)  # after the fade
    d = dbg(p)
    check("full white while sound plays: LEDs capped at 180 mA", d.get("amp") == 1 and 150 <= d.get("led_ma", 0) <= 180, d)
    while dbg(p).get("amp") == 1:
        time.sleep(0.1)
    time.sleep(0.2)
    d = dbg(p)
    check("full white, no sound: LEDs capped at 300 mA (uncapped estimate 576)", 280 <= d.get("led_ma", 0) <= 300, d)
    p.cmd("SNAP 30")


def test_watchdog(p, serial_no):
    p.cmd("SNAP 30 00000004:working")
    check("HANG -> OK", p.cmd("HANG") == "OK")
    t0 = time.time()
    p.close()
    while find_port(serial_no, timeout=0.1) and time.time() - t0 < 10:  # wait for the reset
        time.sleep(0.1)
    gone = time.time() - t0
    check("puck resets 2-5 s after the hang (watchdog 3 s)", 2 <= gone <= 5, f"{gone:.1f} s")
    ok = check("puck back after a hang", reconnect(p, serial_no, timeout=15))
    if not ok:
        return
    st = p.json("STATUS")
    check("restart was the watchdog", st.get("dbg", {}).get("reset") == WDT_RESET, st.get("dbg"))
    check("after watchdog: table cleared", st.get("entries") == [] and st.get("shown") == "idle", st)


def test_after_sleep(p):
    d = dbg(p)
    check("puck saw a USB suspend", d.get("suspends", 0) >= 1, d)
    check("LEDs fully dark while suspended", d.get("suspend_ma", -1) == 0, d)
    check("lights again after resume", d.get("led_ma", 0) > 0, d)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--after-sleep", action="store_true")
    args = ap.parse_args()
    port = args.port or find_port(timeout=3)
    if not port:
        sys.exit("no puck found")
    p = Puck(port)
    hello = p.json("HELLO")
    if "dbg" not in p.json("STATUS"):
        sys.exit("not a debug build (-DHN_DEBUG)")
    if args.after_sleep:
        test_after_sleep(p)
    else:
        saved = p.json("GET all")
        p.cmd("SET volume 0")
        try:
            test_amp(p)
            test_current_cap(p)
        finally:
            for k, v in saved.items():
                p.cmd(f"SET {k} {v}")
            p.cmd("SNAP 30")
        time.sleep(2.5)  # let the restored settings reach flash before the hang
        test_watchdog(p, hello.get("serial"))
        p.cmd("SNAP 30 00000005:working")  # something lit, for the sleep check
    p.close()
    failed = [r for r in results if not r[1]]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
