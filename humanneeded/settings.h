#pragma once
#include "state.h"

// Light modes and sounds, by the names used in the serial protocol.
enum Light : uint8_t {
  LIGHT_OFF, LIGHT_SOLID, LIGHT_BREATHE, LIGHT_PULSE, LIGHT_BLINK,
  LIGHT_HEARTBEAT, LIGHT_FLICKER, LIGHT_SPIN, LIGHT_COUNT
};
static const char *const LIGHT_NAMES[LIGHT_COUNT] = {
  "off", "solid", "breathe", "pulse", "blink", "heartbeat", "flicker", "spin"
};
// What the settings page shows for each (sent with SCHEMA).
static const char *const LIGHT_LABELS[LIGHT_COUNT] = {
  "Off", "Solid", "Breathe (slow)", "Pulse", "Blink", "Heartbeat", "Candle flicker", "Spin (LED ring)"
};

enum Sound : uint8_t {
  SOUND_NONE, SOUND_DING, SOUND_CHIME, SOUND_RISING, SOUND_ARPEGGIO,
  SOUND_BELL, SOUND_BEEPS, SOUND_BLOOP,
  // Melodies. Saved settings store the number, so only ever add at the end.
  SOUND_BRIGHT, SOUND_CLOCK, SOUND_DOORBELL, SOUND_FANFARE, SOUND_RIPPLE, SOUND_CALL,
  SOUND_LULLABY, SOUND_HARP, SOUND_COUNT
};
static const char *const SOUND_NAMES[SOUND_COUNT] = {
  "none", "ding", "chime", "rising", "arpeggio", "bell", "beeps", "bloop",
  "bright", "clock", "doorbell", "fanfare", "ripple", "call", "lullaby", "harp"
};
static const char *const SOUND_LABELS[SOUND_COUNT] = {
  "No sound", "Ding", "Chime (falling)", "Rising", "Arpeggio", "Bell", "Beeps", "Bloop",
  "Bright (melody)", "Clock (melody)", "Doorbell (melody)", "Fanfare (melody)", "Ripple (melody)",
  "Call (melody)", "Lullaby (melody)", "Harp (melody)"
};

// The same settings for every state; keys are "<state>.<name>", e.g. "needs.color".
struct StateStyle {
  uint32_t color;       // 0xRRGGBB
  uint8_t light;        // Light
  uint8_t brightness;   // 0-100, scaled by the global brightness
  uint16_t duration;    // seconds the light shows this state (0 = as long as the state lasts)
  uint8_t sound;        // Sound, played when a session enters this state
  uint8_t volume;       // 0-100, scaled by the global volume
  uint8_t repeat;       // play the sound 1-5 times
};

struct Settings {
  uint32_t magic;       // marks valid data in flash; bump when layout changes
  uint8_t brightness;   // global 0-100
  uint8_t volume;       // global 0-100
  StateStyle state[ST_COUNT];
};

extern Settings settings;

void settingsLoad();
void settingsDefaults();
void settingsSave();

// Sets one key from its text value and saves. Returns false on bad key/value.
bool settingsSet(const char *key, const char *value);

// Appends the JSON for one key, or for every key when key is "all".
// Returns false for an unknown key.
bool settingsGetJson(const char *key, String &out);

// Prints the settings page's layout as one JSON line: every setting, its type, range or
// options, label and help text. The page builds its form from this, so it always matches
// the firmware on the puck. Format in README.md (SCHEMA).
void settingsPrintSchema(Print &out);
