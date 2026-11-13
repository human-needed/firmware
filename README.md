# HumanNeeded firmware

> **Firmware 2.0 speaks `hn-usb/2`**: the HumanNeeded app owns the puck and sends full-state
> snapshots under a lease. 0.x was the prototype protocol; there is no backward compatibility.

Firmware for the [HumanNeeded](https://human-needed.com) puck: a USB desk light that glows and chimes
when your AI coding agent is working, done, or needs you. RP2040, Arduino sketch on the
[earlephilhower arduino-pico](https://github.com/earlephilhower/arduino-pico) core.

**Just want to update your puck?** Use **Update firmware** on [the settings page](https://human-needed.com/settings/).
Builds are in [Releases](https://github.com/human-needed/firmware/releases).

## Build and flash (Windows)

One-time toolchain setup:

```powershell
winget install --id ArduinoSA.CLI -e
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
arduino-cli core update-index
arduino-cli core install rp2040:rp2040
arduino-cli lib install "Adafruit NeoPixel"
```

Build and upload for the HumanNeeded board (pins are in `humanneeded/config.h`):

```powershell
arduino-cli compile --fqbn "rp2040:rp2040:generic:flash=2097152_0,freq=125" --output-dir build humanneeded
```

`freq=125` is required: at the core's default 200 MHz the board can't run from its flash and
never comes up on USB.

Firmware 2.0 has no 1200-baud reset, so `arduino-cli upload -p COMx` can't restart it. Send
`BOOTSEL` on the puck's port (or hold the button / BOOT pad while plugging it in): a drive called
`RPI-RP2` appears; copy `build/humanneeded.ino.uf2` onto it.

**Prototype 0** (development only): a KB2040 wired to a 16-LED WS2812 ring and a MAX98357A I2S
amp. Build it with `--fqbn rp2040:rp2040:adafruit_kb2040 --build-property "compiler.cpp.extra_flags=-DBOARD_PROTO0"`.

| KB2040 | Goes to |
|---|---|
| D2 (GPIO2) | ring DIN |
| D4 (GPIO4) | MAX98357A LRC |
| D5 (GPIO5) | MAX98357A BCLK |
| D6 (GPIO6) | MAX98357A DIN |
| RAW (USB 5 V), GND | ring 5V/GND, MAX98357A VIN/GND |

Its `BOARD_ID` is `kb2040-proto0`. The KB2040-alone and RP2040-Zero test boards are no longer
supported from 0.3.0.
Keep the overall brightness moderate: 16 LEDs at full white draw about 1 A, more than a USB port gives.

## Serial protocol (`hn-usb/2`)

USB CDC, lines ending in `
` (`` ignored), at most 1024 bytes (longer: `ERR line too long`; a half
line with no byte for 500 ms is dropped). Each command gets one reply within 500 ms: `OK`,
`ERR <msg>`, or one line of JSON. Lines starting with `!` are events from the puck: `!HELLO` once
after each USB connect (when the host opens the port), so the app re-sends its state.

| Command | Does |
|---|---|
| `HELLO` | `{"name":"HumanNeeded","fw":"2.0.0","board":"hn-v1","serial":"HN...","proto":2}` |
| `SNAP <lease_s> [<id>:<state> ...]` | the whole session table: lease 5-600 s, id 8 lowercase hex, state `working` or `needs`, max 32 |
| `FLASH done` | one-off done light and sound |
| `SET <key> <value>` / `GET <key\|all>` / `RESET` | settings; saved to flash 2 s after the last change, only if changed |
| `SCHEMA` | every setting with its type, options and help text, as one line of JSON |
| `MUTE <0-1440\|on>` | silence sound: 0 = unmute, minutes, or until turned off |
| `TEST <needs\|done\|working\|idle\|hello>` | preview a state as configured, max 10 s, ended by any real change |
| `STATUS` | `{"proto":2,"lease_left":s,"shown":..,"muted":..,"entries":[..],"sounds":n,"last_sound":..,"saves":n}` |
| `SELFTEST` | factory test: replies serial and button state, then each LED R/G/B and a speaker sweep |
| `BOOTSEL` / `REBOOT` | restart into the UF2 bootloader / restart (settings kept) |

The ring shows, highest first: `TEST` preview, `FLASH done`, needs, working, idle. When the lease
runs out (or on USB unmount, bus reset, power-up) the table is cleared. USB suspend turns the ring
fully dark. The needs sound plays when an id enters needs. Power-up is silent. No 1200-baud reset:
the bootloader is reached with `BOOTSEL` or by holding the button while plugging in.

The full contract lives in the project's PM hub (`pm/contracts/usb-protocol.md`).

## Tests

- `test/core_test.cpp`: unit tests of the parser, lease logic and settings, on the PC:
  `g++ -std=gnu++17 -Ihumanneeded test/core_test.cpp humanneeded/core.cpp humanneeded/settings.cpp -o core_test && ./core_test`
- `test/hil_test.py`: drives a real puck through every command and the lease rules
  (`pip install pyserial`, then `python test/hil_test.py`; `--bootsel <uf2>` also tests an update).

Settings (the full list, with labels, is what `SCHEMA` returns): global `brightness` and `volume` (0-100), plus per state (`needs`, `done`, `working`, `idle`)
`<state>.color` `#RRGGBB`, `.light` `off|solid|breathe|pulse|blink|heartbeat|flicker|spin`,
`.brightness` 0-100, `.duration` seconds 0-3600 (0 = as long as the state lasts; `done` 1-3600),
`.sound` `none|ding|chime|rising|arpeggio|bell|beeps|bloop|bright|clock|doorbell|fanfare|ripple|call|lullaby|harp`, `.volume` 0-100, `.repeat` 1-5
(idle has no sound settings). Stored with a CRC and clamped on load; 0.3.0 settings are migrated.

### SCHEMA

The settings page builds its form from this reply, so it only ever offers what the puck's firmware
has. To add a setting or an option, add it in the firmware (`settings.cpp`, `settings.h`); the page
needs no change.

```json
{"schema":1,
 "lists":{"sound":[["none","No sound"],["ding","Ding"],...], "light":[...], ...},
 "groups":[
  {"id":"overall","title":"Overall","desc":"...","fields":[
    {"key":"brightness","label":"Brightness","type":"range","min":0,"max":100,"unit":"%","tip":"..."}]},
  {"id":"needs","title":"Needs you","desc":"...","preview":"needs","swatch":"needs.color","fields":[
    {"key":"needs.color","label":"Colour","type":"color"},
    {"key":"needs.sound","label":"Sound","type":"select","options":"sound"}, ...]}]}
```

- `groups`: one card each, in order. `preview` is sent as `TEST <preview>`; `swatch` names the
  colour setting that marks the card.
- Field `type`: `range` (`min`, `max`, optional `step`, `unit` shown after the value), `select`
  (`options` = a list name in `lists`, or an inline `[[value,label],...]`; `unit` labels a value that
  isn't in the list, e.g. "45 seconds"), `color` (`#RRGGBB`), `toggle` (sent as `1`/`0`), `number`,
  `text` (`maxlength`). The page shows any other type as a text box.
- `tip`: help text behind a "?" button. Every value is set with `SET <key> <value>` and read from
  `GET all`.
- `done.duration` uses the `done_duration` list (no "as long as it lasts").

## Release

Bump `FW_VERSION` in `humanneeded/config.h`, then from `main`:

```powershell
powershell -ExecutionPolicy Bypass -File release.ps1
```

It builds a UF2 for every board (the HumanNeeded board and prototype 0), publishes them as one GitHub
release, and updates `latest.json` in the website repo,
which is what the settings page checks. Push the website repo to offer the update.
