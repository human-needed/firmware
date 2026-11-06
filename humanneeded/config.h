#pragma once
// All board pins and build-time switches live here.

#define FW_VERSION "0.2.0"
#define USB_PRODUCT_NAME "HumanNeeded"

// ---- Board selection: set exactly one to 1 ----
#define BOARD_KB2040       1   // Adafruit KB2040 (test board)
#define BOARD_RP2040_ZERO  0   // Waveshare RP2040-Zero (prototype board)
#define BOARD_HN_V1        0   // HumanNeeded main board HN-08 rev A (pcb/)

#if BOARD_KB2040
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
  #define AMP_SD_PIN    5      // amp shutdown, high = off (pulled low on the board)
  #define ENABLE_AUDIO  1
#else
  #error "Select a board in config.h"
#endif
// When the 16-LED ring arrives: point LED_PIN at its data pin and set LED_COUNT 16.

// ---- Audio: MAX98357A over I2S, or PWM if the board sets AUDIO_PWM_PIN. ----
// Off on the test boards until the amp + speaker arrive.
#ifndef ENABLE_AUDIO
#define ENABLE_AUDIO     0
#endif
#define I2S_BCLK_PIN     26    // LRCLK is always BCLK + 1 (27)
#define I2S_DATA_PIN     28
#define AUDIO_RATE       22050

// ---- Behaviour constants from SPEC.md ----
#define MAX_SESSIONS        16
#define WORKING_STALE_MS    (30UL * 60 * 1000)  // working with no event -> idle
#define TEST_PREVIEW_MS     5000                // how long TEST <state> shows
#define HELLO_MS            1500                // boot spin length
#define FADE_MS             600                 // cross-fade between states
