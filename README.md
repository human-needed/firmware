# HumanNeeded firmware

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
arduino-cli compile -u --fqbn "rp2040:rp2040:generic:flash=2097152_0,freq=125" -p COM4 humanneeded
```

`freq=125` is required: at the core's default 200 MHz the board can't run from its flash and
never comes up on USB.

Use the COM port the puck shows up as (`arduino-cli board list`; its USB product is `HumanNeeded`).
If the upload can't find the board, hold **BOOT** while plugging it in: a drive called `RPI-RP2`
appears; upload with `-p UF2_Board`, or copy a `.uf2` onto the drive.

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

## Serial protocol

USB CDC, 115200 baud, lines ending in `\n`. Each command gets one reply: `OK`, `ERR <msg>`, or one line of JSON.

| Command | Does |
|---|---|
| `HELLO` | `{"name":"HumanNeeded","fw":"0.3.0","board":"hn-v1","serial":"HN..."}` |
| `EV <working\|needs\|done\|idle\|end> <session_id> [agent]` | report a session's state |
| `SET <key> <value>` / `GET <key\|all>` / `RESET` | settings, saved in flash |
| `SCHEMA` | every setting with its type, options and help text, as one line of JSON (from 0.3.0) |
| `MUTE <minutes>` | silence sound (0 = unmute) |
| `TEST <state\|hello>` | preview a state as configured |
| `CLEAR` | forget all sessions |
| `BOOTSEL` / `REBOOT` | restart into the UF2 bootloader / restart |
| `STATUS`, `SKIP <minutes>` | debugging: state and session table; move the session clock forward |

The puck shows the highest-priority live session: needs > done > working > idle.

Settings (the full list, with labels, is what `SCHEMA` returns): global `brightness` and `volume` (0-100), plus per state (`needs`, `done`, `working`, `idle`)
`<state>.color` `#RRGGBB`, `.light` `off|solid|breathe|pulse|blink|heartbeat|flicker|spin`,
`.brightness` 0-100, `.duration` seconds 0-3600 (0 = as long as the state lasts),
`.sound` `none|ding|chime|rising|arpeggio|bell|beeps|bloop|bright|clock|doorbell|fanfare|ripple|call|lullaby|harp`, `.volume` 0-100, `.repeat` 1-5.

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
- Firmware older than 0.3.0 answers `ERR unknown command`; the page then uses its built-in copy of
  the 0.2.0 layout.

## Release

Bump `FW_VERSION` in `humanneeded/config.h`, then from `main`:

```powershell
powershell -ExecutionPolicy Bypass -File release.ps1
```

It builds a UF2 for every board (the HumanNeeded board and prototype 0), publishes them as one GitHub
release, and updates `latest.json` in the website repo,
which is what the settings page checks. Push the website repo to offer the update.
