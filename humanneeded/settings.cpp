#include <EEPROM.h>
#include "settings.h"

#define SETTINGS_MAGIC 0x484E0003  // "HN" + layout version 3

Settings settings;

static void setStyle(State s, uint32_t color, Light light, uint8_t brightness,
                     uint16_t duration, Sound sound) {
  settings.state[s] = {color, light, brightness, duration, sound, 100, 1};
}

void settingsDefaults() {
  settings.magic = SETTINGS_MAGIC;
  settings.brightness = 60;
  settings.volume = 70;
  //                       colour    light            bright  secs  sound
  setStyle(ST_NEEDS,   0xFFA62B, LIGHT_PULSE,     100,    0,    SOUND_RISING);
  setStyle(ST_DONE,    0x22E04A, LIGHT_PULSE,     100,    3,    SOUND_CHIME);
  setStyle(ST_WORKING, 0x3366FF, LIGHT_BREATHE,   100,    0,    SOUND_NONE);
  setStyle(ST_IDLE,    0xFFD2A0, LIGHT_SOLID,     20,     0,    SOUND_NONE);
}

void settingsLoad() {
  EEPROM.begin(256);
  EEPROM.get(0, settings);
  if (settings.magic != SETTINGS_MAGIC) {
    settingsDefaults();
    settingsSave();
  }
}

void settingsSave() {
  EEPROM.put(0, settings);
  EEPROM.commit();
}

// ---- parsing helpers ----

static int findName(const char *const *names, int count, const char *value) {
  for (int i = 0; i < count; i++) {
    if (strcasecmp(value, names[i]) == 0) return i;
  }
  return -1;
}

static bool parseInt(const char *value, long lo, long hi, long &out) {
  char *end;
  out = strtol(value, &end, 10);
  return *value && *end == 0 && out >= lo && out <= hi;
}

static bool parseColor(const char *value, uint32_t &out) {
  if (value[0] == '#') value++;
  if (strlen(value) != 6) return false;
  char *end;
  out = strtoul(value, &end, 16);
  return *end == 0;
}

// Sets one per-state setting, e.g. name "color" for key "needs.color".
static bool setStateKey(StateStyle &st, const char *name, const char *value) {
  long n;
  int i;
  uint32_t c;
  if (!strcasecmp(name, "color") && parseColor(value, c))                         st.color = c;
  else if (!strcasecmp(name, "light") && (i = findName(LIGHT_NAMES, LIGHT_COUNT, value)) >= 0) st.light = i;
  else if (!strcasecmp(name, "brightness") && parseInt(value, 0, 100, n))         st.brightness = n;
  else if (!strcasecmp(name, "duration") && parseInt(value, 0, 3600, n))          st.duration = n;
  else if (!strcasecmp(name, "sound") && (i = findName(SOUND_NAMES, SOUND_COUNT, value)) >= 0) st.sound = i;
  else if (!strcasecmp(name, "volume") && parseInt(value, 0, 100, n))             st.volume = n;
  else if (!strcasecmp(name, "repeat") && parseInt(value, 1, 5, n))               st.repeat = n;
  else return false;
  return true;
}

bool settingsSet(const char *key, const char *value) {
  long n;
  const char *dot = strchr(key, '.');

  if (!strcasecmp(key, "brightness") && parseInt(value, 0, 100, n)) {
    settings.brightness = n;
  } else if (!strcasecmp(key, "volume") && parseInt(value, 0, 100, n)) {
    settings.volume = n;
  } else if (dot) {
    char stateName[12];
    strlcpy(stateName, key, min((size_t)(dot - key + 1), sizeof(stateName)));
    int s = parseState(stateName);
    if (s < 0 || !setStateKey(settings.state[s], dot + 1, value)) return false;
  } else {
    return false;
  }
  settingsSave();
  return true;
}

// ---- JSON output ----

static void addPair(String &out, const String &key, const String &value, bool quote) {
  if (out.length() > 1) out += ',';
  out += '"' + key + "\":";
  out += quote ? '"' + value + '"' : value;
}

static String colorText(uint32_t c) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%06lX", (unsigned long)c);
  return buf;
}

bool settingsGetJson(const char *key, String &out) {
  bool all = !strcasecmp(key, "all");
  int found = 0;
  out = "{";

  auto add = [&](const String &k, const String &v, bool quote) {
    if (all || k.equalsIgnoreCase(key)) { addPair(out, k, v, quote); found++; }
  };
  add("brightness", String(settings.brightness), false);
  add("volume", String(settings.volume), false);
  for (int s = 0; s < ST_COUNT; s++) {
    const StateStyle &st = settings.state[s];
    String p = String(STATE_NAMES[s]) + '.';
    add(p + "color", colorText(st.color), true);
    add(p + "light", LIGHT_NAMES[st.light], true);
    add(p + "brightness", String(st.brightness), false);
    add(p + "duration", String(st.duration), false);
    add(p + "sound", SOUND_NAMES[st.sound], true);
    add(p + "volume", String(st.volume), false);
    add(p + "repeat", String(st.repeat), false);
  }

  out += '}';
  return found > 0;
}
