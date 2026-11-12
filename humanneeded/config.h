#pragma once
// All board pins and build-time switches live here.

#define FW_VERSION "0.2.0"
#define USB_PRODUCT_NAME "HumanNeeded"

// ---- Board selection: set exactly one to 1 ----
#define BOARD_PROTO0       1   // Prototype 0: KB2040 + 16-LED ring + MAX98357A amp
#define BOARD_KB2040       0   // Adafruit KB2040 alone (test board, onboard NeoPixel)
#define BOARD_RP2040_ZERO  0   // Waveshare RP2040-Zero (prototype board)
#define BOARD_HN_V1        0   // HumanNeeded main board HN-08 rev A (pcb/)

#if BOARD_PROTO0
  // KB2040 pins D2-D6 are GPIO2-GPIO6.
  #define BOARD_ID      "kb2040-proto0"  // own id, so the settings page never offers it plain KB2040 firmware
  #define LED_PIN       2      // D2: ring data in
  #define LED_COUNT     16
  #define I2S_LRCLK_PIN 4      // D4: MAX98357A LRC
  #define I2S_BCLK_PIN  5      // D5: MAX98357A BCLK
  #define I2S_DATA_PIN  6      // D6: MAX98357A DIN
  #define ENABLE_AUDIO  1
#elif BOARD_KB2040
  #define BOARD_ID   "kb2040"  // names the firmware file for this board
  #define LED_PIN    17        // onboard NeoPixel
  #define LED_COUNT  1
#elif BOARD_RP2040_ZERO
  #define BOARD_ID   "rp2040-zero"
  #define LED_PIN    16        // onboard WS2812
  #define LED_COUNT  1
#elif BOARD_HN_V1
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
#else
  #error "Select a board in config.h"
#endif

// ---- Audio: MAX98357A over I2S, or PWM if the board sets AUDIO_PWM_PIN. ----
// Off on the test boards until the amp + speaker arrive.
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

// ---- Behaviour constants from SPEC.md ----
#define MAX_SESSIONS        16
#define WORKING_STALE_MS    (30UL * 60 * 1000)  // working with no event -> idle
#define TEST_PREVIEW_MS     5000                // how long TEST <state> shows
#define HELLO_MS            1500                // boot spin length
#define FADE_MS             600                 // cross-fade between states
