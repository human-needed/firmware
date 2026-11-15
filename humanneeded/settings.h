#pragma once
// Settings: values, parsing, JSON and the stored format. No hardware here (flash I/O is in
// humanneeded.ino), so it is unit-tested on the PC too.
#include "state.h"
#include "core.h"

// Light modes and sounds, by the names used in the serial protocol.
enum Light : uint8_t {
  LIGHT_OFF, LIGHT_SOLID, LIGHT_BREATHE, LIGHT_PULSE, LIGHT_BLINK,
  LIGHT_HEARTBEAT, LIGHT_FLICKER, LIGHT_SPIN, LIGHT_COUNT
};
static const char *const LIGHT_NAMES[LIGHT_COUNT] = {
  "off", "solid", "breathe", "pulse", "blink", "heartbeat", "flicker", "spin"
};
// What the settings form shows for each (sent with SCHEMA).
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
// The idle state has no sound (sound/volume/repeat are not offered for it).
// Layout is shared with the stored format: don't reorder.
struct StateStyle {
  uint32_t color;       // 0xRRGGBB
  uint8_t light;        // Light
  uint8_t brightness;   // 0-100, scaled by the global brightness
  uint16_t duration;    // seconds the light shows this state (0 = as long as it lasts; done: 1-3600)
  uint8_t sound;        // Sound, played when the state starts
  uint8_t volume;       // 0-100, scaled by the global volume
  uint8_t repeat;       // play the sound 1-5 times
};

struct Settings {
  uint8_t brightness;   // global 0-100
  uint8_t volume;       // global 0-100
  StateStyle state[ST_COUNT];
};

extern Settings settings;
extern uint16_t settingsDurations[ST_COUNT];  // settings.state[i].duration, for Engine::dur

void settingsDefaults(Settings &s);
// Puts every field in range. Returns true if anything changed.
bool settingsClamp(Settings &s);
// Call after changing `settings` directly (refreshes settingsDurations).
void settingsUpdated();

// Sets one key from its text value. Returns false on a bad key or value (nothing changes).
bool settingsSet(const char *key, const char *value);
// JSON object with one key, or every key for "all". Returns false for an unknown key.
bool settingsGetJson(const char *key, Buf &out);
// The settings form layout as one JSON object (format in README.md, SCHEMA).
void settingsSchema(Buf &out, bool audio);

// Stored format: magic, Settings, CRC32. Older layouts are migrated on load.
#define SETTINGS_STORE_SIZE 64
size_t settingsEncode(const Settings &s, uint8_t *raw);  // returns bytes used
// Reads stored bytes. Returns false if there is nothing valid (caller uses defaults).
bool settingsDecode(const uint8_t *raw, size_t n, Settings &out);
