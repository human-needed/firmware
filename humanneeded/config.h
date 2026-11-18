#pragma once
// All board pins and build-time switches live here.

#define FW_VERSION "2.0.4"
#define PROTO_VERSION 2           // hn-usb/2, pm/contracts/usb-protocol.md
#define USB_PRODUCT_NAME "HumanNeeded"
#define USB_VID_HN 0x2E8A         // Raspberry Pi
// USB PID: 0 keeps the core's default for the board until Raspberry Pi assigns ours (D-035).
// Set it here and nowhere else.
#define USB_PID_HN 0

// ---- Board ----
// HN-08 rev A ("hn-v1", the default build) and rev B ("hn-v2", build with -DBOARD_HN_V2).
// Same pins on both. Prototype 0 (KB2040) and the older test boards are no longer supported (D-053).
#ifdef BOARD_HN_V2
// Rev B (D-057): 16 x WS2816B, 16 bits per channel (48 bits per LED, GRB), 20 mA per LED at full
// white; amp SD pulled up on the board (amp off until GPIO5 goes low).
#define BOARD_ID      "hn-v2"
#define LED_16BIT     1
#define LED_MA_PER_CHANNEL 6.7f  // 20 mA per LED / 3 (datasheet max; measure on the pilot boards)
// The firmware sends linear light (gamma is done here, in leds.cpp). If the WS2816B's built-in
// gamma turns out to map its 16-bit input through a curve, set this to undo it: the output is
// light^(1/LED_OUT_GAMMA). 1 = the chip is linear. Check on the pilot boards (see README).
#define LED_OUT_GAMMA 1.0f
#else
#define BOARD_ID      "hn-v1"
#define LED_16BIT     0
#endif
#define LED_PIN       28     // ring data, buffered to 5 V; chain runs clockwise from LED16
#define LED_COUNT     16
#define AUDIO_PWM_PIN 4      // PWM audio -> RC filter -> 8002A amp (no I2S on this board)
#define AUDIO_PWM_HZ  200000 // PWM carrier; the board's RC filter is set for it
#define AUDIO_PWM_OSR 8      // PWM periods per audio sample (see audioTick)
#define AUDIO_RATE    25000  // AUDIO_PWM_HZ / AUDIO_PWM_OSR, so samples and PWM periods line up
#define AMP_SD_PIN    5      // amp shutdown, high = off (rev A: pulled low; rev B: pulled up)
#define ENABLE_AUDIO  1
#define AUDIO_NORMALIZE 1    // amp gain is only 2: use the full PWM swing for every sound

// ---- Audio: MAX98357A over I2S, or PWM if the board sets AUDIO_PWM_PIN. ----
#ifndef ENABLE_AUDIO
#define ENABLE_AUDIO     0
#endif
// I2S clock pins must be neighbours: LRCLK is BCLK + 1, or BCLK - 1 when the board sets I2S_LRCLK_PIN so.
#ifndef I2S_BCLK_PIN
#define I2S_BCLK_PIN     26
#define I2S_DATA_PIN     28
#endif
#ifndef AUDIO_RATE
#define AUDIO_RATE       22050
#endif
#ifndef AUDIO_NORMALIZE
#define AUDIO_NORMALIZE  0    // 1: scale each sound so its peak is full scale (see audio.cpp)
#endif

// ---- Behaviour constants ----
#define HELLO_MS            1500    // hello spin length
#define FADE_MS             400     // cross-fade between states (eased)
#define ANIM_FADE_MS        120     // the same into an animated light, so its first cycle stays whole
#define ANIM_MS             1000    // every light animation is one cycle of this length
#define FRAME_MS            5       // LED frame period: 200 fps, so temporal dithering doesn't flicker
#define CAP_DOWN_MS         40.0f   // current cap eases down this fast when a sound starts...
#define CAP_UP_MS           250.0f  // ...and back up this slowly
#define SAVE_DELAY_MS       2000    // settings go to flash this long after the last change
#define WATCHDOG_MS         3000    // longer than the worst flash erase + write
#define REPLY_TIMEOUT_MS    400     // give up sending a reply the host doesn't read
#define HELLO_DELAY_MS      150     // "!HELLO" this long after the host opens the port
#define SELFTEST_STEP_MS    240     // SELFTEST: time per LED colour (11.5 s for 16 LEDs x R/G/B)

// ---- LED current cap ----
// Estimated draw of one LED channel at full level. Not measured on the XL-2020 yet (P9):
// measure full white on a rev A board and correct this.
#ifndef LED_MA_PER_CHANNEL
#define LED_MA_PER_CHANNEL  12.0f
#endif
#define LED_BUDGET_MA       300.0f  // LEDs, normally
#define LED_BUDGET_SOUND_MA 180.0f  // LEDs while the amp is on (amp peak ~280 mA, USB 500 mA)
