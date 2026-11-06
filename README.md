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

Build and upload (KB2040 by default; pick the board and pins in `humanneeded/config.h`):

```powershell
arduino-cli compile -u --fqbn rp2040:rp2040:adafruit_kb2040 -p COM4 humanneeded
```

Use the COM port the puck shows up as (`arduino-cli board list`; its USB product is `HumanNeeded`).
If the upload can't find the board, hold **BOOT** while plugging it in: a drive called `RPI-RP2`
appears; upload with `-p UF2_Board`, or copy a `.uf2` onto the drive.

Audio is off until the amp is fitted: set `ENABLE_AUDIO 1` in `config.h`.

## Serial protocol

USB CDC, 115200 baud, lines ending in `\n`. Each command gets one reply: `OK`, `ERR <msg>`, or one line of JSON.

| Command | Does |
|---|---|
| `HELLO` | `{"name":"HumanNeeded","fw":"0.2.0","board":"kb2040","serial":"HN..."}` |
| `EV <working\|needs\|done\|idle\|end> <session_id> [agent]` | report a session's state |
| `SET <key> <value>` / `GET <key\|all>` / `RESET` | settings, saved in flash |
| `MUTE <minutes>` | silence sound (0 = unmute) |
| `TEST <state\|hello>` | preview a state as configured |
| `CLEAR` | forget all sessions |
| `BOOTSEL` / `REBOOT` | restart into the UF2 bootloader / restart |
| `STATUS`, `SKIP <minutes>` | debugging: state and session table; move the session clock forward |

The puck shows the highest-priority live session: needs > done > working > idle.

Settings: global `brightness` and `volume` (0-100), plus per state (`needs`, `done`, `working`, `idle`)
`<state>.color` `#RRGGBB`, `.light` `off|solid|breathe|pulse|blink|heartbeat|flicker|spin`,
`.brightness` 0-100, `.duration` seconds 0-3600 (0 = as long as the state lasts),
`.sound` `none|ding|chime|rising|arpeggio|bell|beeps|bloop`, `.volume` 0-100, `.repeat` 1-5.

## Release

Bump `FW_VERSION` in `humanneeded/config.h`, then:

```powershell
powershell -ExecutionPolicy Bypass -File release.ps1
```

It builds the UF2, publishes it as a GitHub release, and updates `latest.json` in the website repo,
which is what the settings page checks. Push the website repo to offer the update.
