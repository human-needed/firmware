#pragma once
// All board pins and build-time switches live here.

#define FW_VERSION "2.0.0"
#define PROTO_VERSION 2           // hn-usb/2, pm/contracts/usb-protocol.md
#define USB_PRODUCT_NAME "HumanNeeded"
#define USB_VID_HN 0x2E8A         // Raspberry Pi
// USB PID: 0 keeps the core's default for the board until Raspberry Pi assigns ours (D-035).
// Set it here and nowhere else.
#define USB_PID_HN 0

// ---- Board ----
// The puck is the HN board. BOARD_PROTO0 is the hand-wired prototype, kept for development.
// A plain build is the HN board; release.ps1 builds the prototype with -DBOARD_PROTO0.
// (The KB2040-alone and RP2040-Zero test boards are no longer supported, from 0.3.0.)
#if !defined(BOARD_PROTO0)
#define BOARD_HN_V1 1
#endif

#if defined(BOARD_HN_V1)
  #define BOARD_ID      "hn-v1"
  #define LED_PIN       28     // ring data, buffered to 5 V; chain runs clockwise from LED16
  #define LED_COUNT     16
  #define AUDIO_PWM_PIN 4      // PWM audio -> RC filter -> 8002A amp (no I2S on this board)
  #define AUDIO_PWM_HZ  200000 // PWM carrier; the board's RC filter is set for it
  #define AUDIO_PWM_OSR 8      // PWM periods per audio sample (see audioTick)
  #define AUDIO_RATE    25000  // AUDIO_PWM_HZ / AUDIO_PWM_OSR, so samples and PWM periods line up
  #define AMP_SD_PIN    5      // amp shutdown, high = off (pulled low on the board)
  #define ENABLE_AUDIO  1
  #define AUDIO_NORMALIZE 1    // amp gain is only 2: use the full PWM swing for every sound
#elif defined(BOARD_PROTO0)
  // Prototype 0: KB2040 + 16-LED ring + MAX98357A amp. KB2040 pins D2-D6 are GPIO2-GPIO6.
  #define BOARD_ID      "kb2040-proto0"
  #define LED_PIN       2      // D2: ring data in
  #define LED_COUNT     16
  #define I2S_LRCLK_PIN 4      // D4: MAX98357A LRC
  #define I2S_BCLK_PIN  5      // D5: MAX98357A BCLK
  #define I2S_DATA_PIN  6      // D6: MAX98357A DIN
  #define ENABLE_AUDIO  1
#endif

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
#define FADE_MS             600     // cross-fade between states
#define SAVE_DELAY_MS       2000    // settings go to flash this long after the last change
#define WATCHDOG_MS         3000    // longer than the worst flash erase + write
#define REPLY_TIMEOUT_MS    400     // give up sending a reply the host doesn't read
#define HELLO_DELAY_MS      150     // "!HELLO" this long after the host opens the port
#define SELFTEST_STEP_MS    120     // SELFTEST: time per LED colour

// ---- LED current cap ----
// Estimated draw of one LED channel at full level. Not measured on the XL-2020 yet (P9):
// measure full white on a rev A board and correct this.
#ifndef LED_MA_PER_CHANNEL
#define LED_MA_PER_CHANNEL  12.0f
#endif
#define LED_BUDGET_MA       300.0f  // LEDs, normally
#define LED_BUDGET_SOUND_MA 180.0f  // LEDs while the amp is on (amp peak ~280 mA, USB 500 mA)
