#include "config.h"
#include "settings.h"

#define SETTINGS_MAGIC    0x484E0004  // "HN" + layout version 4: Settings + CRC32
#define SETTINGS_MAGIC_V3 0x484E0003  // 0.3.0: magic + Settings, no CRC

Settings settings;
uint16_t settingsDurations[ST_COUNT];

static void setStyle(Settings &s, State st, uint32_t color, Light light, uint8_t brightness,
                     uint16_t duration, Sound sound) {
  s.state[st] = {color, light, brightness, duration, sound, 100, 1};
}

void settingsDefaults(Settings &s) {
  memset(&s, 0, sizeof(s));
  s.brightness = 60;
  s.volume = 70;
  //                         colour    light            bright  secs  sound
  setStyle(s, ST_NEEDS,   0xFFA62B, LIGHT_PULSE,     100,    0,    SOUND_CALL);
  setStyle(s, ST_DONE,    0x22E04A, LIGHT_PULSE,     100,    3,    SOUND_BRIGHT);
  setStyle(s, ST_WORKING, 0x3366FF, LED_COUNT >= 3 ? LIGHT_SPIN : LIGHT_BREATHE, 100, 0, SOUND_NONE);
  setStyle(s, ST_IDLE,    0xFFD2A0, LIGHT_SOLID,     20,     0,    SOUND_NONE);
}

template <typename T>
static void clampField(T &v, T lo, T hi, bool &changed) {
  T n = v < lo ? lo : v > hi ? hi : v;
  if (n != v) { v = n; changed = true; }
}

bool settingsClamp(Settings &s) {
  bool changed = false;
  clampField<uint8_t>(s.brightness, 0, 100, changed);
  clampField<uint8_t>(s.volume, 0, 100, changed);
  for (int i = 0; i < ST_COUNT; i++) {
    StateStyle &st = s.state[i];
    clampField<uint32_t>(st.color, 0, 0xFFFFFF, changed);
    clampField<uint8_t>(st.light, 0, LIGHT_COUNT - 1, changed);
    clampField<uint8_t>(st.brightness, 0, 100, changed);
    clampField<uint16_t>(st.duration, i == ST_DONE ? 1 : 0, 3600, changed);
    clampField<uint8_t>(st.sound, 0, i == ST_IDLE ? SOUND_NONE : SOUND_COUNT - 1, changed);
    clampField<uint8_t>(st.volume, 0, 100, changed);
    clampField<uint8_t>(st.repeat, 1, 5, changed);
  }
  return changed;
}

void settingsUpdated() {
  for (int i = 0; i < ST_COUNT; i++) settingsDurations[i] = settings.state[i].duration;
}

// ---- SET ----

static int findName(const char *const *names, int count, const char *value) {
  for (int i = 0; i < count; i++) {
    if (strcasecmp(value, names[i]) == 0) return i;
  }
  return -1;
}

static bool hasSound(int state) { return state != ST_IDLE; }

// Sets one per-state setting, e.g. name "color" for key "needs.color".
static bool setStateKey(int state, const char *name, const char *value) {
  StateStyle &st = settings.state[state];
  uint32_t n;
  int i;
  bool snd = hasSound(state);
  if (!strcasecmp(name, "color") && parseColor(value, n))                                 st.color = n;
  else if (!strcasecmp(name, "light") && (i = findName(LIGHT_NAMES, LIGHT_COUNT, value)) >= 0) st.light = i;
  else if (!strcasecmp(name, "brightness") && parseUint(value, 0, 100, n))                st.brightness = n;
  else if (!strcasecmp(name, "duration") && parseUint(value, state == ST_DONE ? 1 : 0, 3600, n)) st.duration = n;
  else if (snd && !strcasecmp(name, "sound") && (i = findName(SOUND_NAMES, SOUND_COUNT, value)) >= 0) st.sound = i;
  else if (snd && !strcasecmp(name, "volume") && parseUint(value, 0, 100, n))             st.volume = n;
  else if (snd && !strcasecmp(name, "repeat") && parseUint(value, 1, 5, n))               st.repeat = n;
  else return false;
  return true;
}

bool settingsSet(const char *key, const char *value) {
  uint32_t n;
  const char *dot = strchr(key, '.');
  if (!strcasecmp(key, "brightness") && parseUint(value, 0, 100, n)) {
    settings.brightness = n;
  } else if (!strcasecmp(key, "volume") && parseUint(value, 0, 100, n)) {
    settings.volume = n;
  } else if (dot && dot - key < 12) {
    char stateName[12];
    memcpy(stateName, key, dot - key);
    stateName[dot - key] = 0;
    int s = parseState(stateName);
    if (s < 0 || !setStateKey(s, dot + 1, value)) return false;
  } else {
    return false;
  }
  settingsUpdated();
  return true;
}

// ---- GET ----

bool settingsGetJson(const char *key, Buf &out) {
  bool all = !strcasecmp(key, "all");
  int found = 0;
  out.addc('{');
  auto pair = [&](const char *k, bool quote, const char *fmt, unsigned long v, const char *text) {
    if (!all && strcasecmp(k, key)) return;
    if (found++) out.addc(',');
    out.str(k);
    out.addc(':');
    if (text) out.str(text);
    else if (quote) { char c[8]; snprintf(c, sizeof(c), fmt, v); out.str(c); }
    else out.addf(fmt, v);
  };
  pair("brightness", false, "%lu", settings.brightness, nullptr);
  pair("volume", false, "%lu", settings.volume, nullptr);
  for (int s = 0; s < ST_COUNT; s++) {
    const StateStyle &st = settings.state[s];
    char k[24];
    auto key2 = [&](const char *name) { snprintf(k, sizeof(k), "%s.%s", STATE_NAMES[s], name); return k; };
    pair(key2("color"), true, "#%06lX", st.color, nullptr);
    pair(key2("light"), false, nullptr, 0, LIGHT_NAMES[st.light]);
    pair(key2("brightness"), false, "%lu", st.brightness, nullptr);
    pair(key2("duration"), false, "%lu", st.duration, nullptr);
    if (!hasSound(s)) continue;
    pair(key2("sound"), false, nullptr, 0, SOUND_NAMES[st.sound]);
    pair(key2("volume"), false, "%lu", st.volume, nullptr);
    pair(key2("repeat"), false, "%lu", st.repeat, nullptr);
  }
  out.addc('}');
  return found > 0;
}

// ---- SCHEMA ----

// Option list: [[value,label],...]
static void jsonOptions(Buf &out, const char *const *values, const char *const *labels, int count) {
  out.addc('[');
  for (int i = 0; i < count; i++) {
    if (i) out.addc(',');
    out.addc('[');
    out.str(values[i]);
    out.addc(',');
    out.str(labels[i]);
    out.addc(']');
  }
  out.addc(']');
}

struct Field {
  const char *name, *label, *type;
  const char *options;  // select: name of a list in "lists"
  const char *unit;     // after a range's value; for a select, names a value missing from its list
  int16_t min, max;     // range
  const char *tip;
  bool idle;            // also shown for the idle state
  bool sound;           // only with a speaker
};

static const char TIP_BRIGHTNESS[] = "Master brightness for the whole puck. Each state's own brightness is a share of this one: a state at 50% with Overall at 60% glows at half of 60%.";
static const char TIP_VOLUME[] = "Master volume for the whole puck. Each state's own volume is a share of this one: a sound at 50% with Overall at 70% plays at half of 70%. To silence everything for a while, mute the puck from the app.";

static const Field GLOBAL_FIELDS[] = {
  {"brightness", "Brightness", "range", nullptr, "%", 0, 100, TIP_BRIGHTNESS, true, false},
  {"volume", "Volume", "range", nullptr, "%", 0, 100, TIP_VOLUME, true, true},
};

static const Field STATE_FIELDS[] = {
  {"color", "Colour", "color", nullptr, nullptr, 0, 0, nullptr, true, false},
  {"light", "Light", "select", "light", nullptr, 0, 0, nullptr, true, false},
  {"brightness", "Brightness", "range", nullptr, "%", 0, 100, "This state's brightness as a share of the Overall brightness.", true, false},
  {"duration", "Show light for", "select", "duration", "seconds", 0, 0,
   "How long the light shows this state after it starts. When the time is up the puck goes back to whatever else is happening (usually idle), even if the agent hasn't moved on.", false, false},
  {"sound", "Sound", "select", "sound", nullptr, 0, 0, nullptr, false, true},
  {"volume", "Volume", "range", nullptr, "%", 0, 100, "This state's volume as a share of the Overall volume.", false, true},
  {"repeat", "Play sound", "select", "repeat", nullptr, 0, 0,
   "How many times the sound plays when the state starts. Each sound is short, so this sets how long and how insistent the alert is.", false, true},
};

struct Group { State state; const char *title, *desc; };
static const Group STATE_GROUPS[] = {  // in the order the form shows them
  {ST_NEEDS, "Needs you", "An agent is waiting for your approval or an answer."},
  {ST_DONE, "Done", "An agent finished its task."},
  {ST_WORKING, "Working", "An agent is busy on your request."},
  {ST_IDLE, "Idle", "No agent is working or waiting. Sounds and duration don't apply here."},
};

static void jsonField(Buf &out, const char *key, const Field &f, const char *options) {
  out.add("{\"key\":");
  out.str(key);
  out.add(",\"label\":");
  out.str(f.label);
  out.add(",\"type\":");
  out.str(f.type);
  if (options) { out.add(",\"options\":"); out.str(options); }
  if (f.unit) { out.add(",\"unit\":"); out.str(f.unit); }
  if (!strcmp(f.type, "range")) out.addf(",\"min\":%d,\"max\":%d", f.min, f.max);
  if (f.tip) { out.add(",\"tip\":"); out.str(f.tip); }
  out.addc('}');
}

void settingsSchema(Buf &out, bool audio) {
  static const char *const DURATION_VALUES[] = {"0", "3", "5", "10", "30", "60", "300", "600"};
  static const char *const DURATION_LABELS[] = {"As long as the state lasts", "3 seconds", "5 seconds",
                                                "10 seconds", "30 seconds", "1 minute", "5 minutes", "10 minutes"};
  static const char *const REPEAT_VALUES[] = {"1", "2", "3", "4", "5"};
  static const char *const REPEAT_LABELS[] = {"Once", "2 times", "3 times", "4 times", "5 times"};

  out.add("{\"schema\":1,\"lists\":{\"light\":");
  jsonOptions(out, LIGHT_NAMES, LIGHT_LABELS, LIGHT_COUNT);
  out.add(",\"sound\":");
  jsonOptions(out, SOUND_NAMES, SOUND_LABELS, SOUND_COUNT);
  out.add(",\"duration\":");
  jsonOptions(out, DURATION_VALUES, DURATION_LABELS, 8);
  out.add(",\"done_duration\":");  // done always ends: no "as long as it lasts"
  jsonOptions(out, DURATION_VALUES + 1, DURATION_LABELS + 1, 7);
  out.add(",\"repeat\":");
  jsonOptions(out, REPEAT_VALUES, REPEAT_LABELS, 5);

  out.add("},\"groups\":[{\"id\":\"overall\",\"title\":\"Overall\",\"desc\":\"Master controls for the whole puck.\",\"fields\":[");
  bool first = true;
  for (const Field &f : GLOBAL_FIELDS) {
    if (!audio && f.sound) continue;
    if (!first) out.addc(',');
    first = false;
    jsonField(out, f.name, f, f.options);
  }
  out.add("]}");

  for (const Group &g : STATE_GROUPS) {
    const char *id = STATE_NAMES[g.state];
    char key[24];
    out.add(",{\"id\":");
    out.str(id);
    out.add(",\"title\":");
    out.str(g.title);
    out.add(",\"desc\":");
    out.str(g.desc);
    out.add(",\"preview\":");  // sent as TEST <preview>
    out.str(id);
    snprintf(key, sizeof(key), "%s.color", id);
    out.add(",\"swatch\":");   // the key whose colour marks the card
    out.str(key);
    out.add(",\"fields\":[");
    first = true;
    for (const Field &f : STATE_FIELDS) {
      if (g.state == ST_IDLE && !f.idle) continue;
      if (!audio && f.sound) continue;
      if (!first) out.addc(',');
      first = false;
      snprintf(key, sizeof(key), "%s.%s", id, f.name);
      const char *opts = f.options;
      if (g.state == ST_DONE && !strcmp(f.name, "duration")) opts = "done_duration";
      jsonField(out, key, f, opts);
    }
    out.add("]}");
  }
  out.add("]}");
}

// ---- Stored format ----

// 0.3.0 layout (magic first, no CRC). Same StateStyle.
struct SettingsV3 {
  uint32_t magic;
  uint8_t brightness, volume;
  StateStyle state[ST_COUNT];
};

size_t settingsEncode(const Settings &s, uint8_t *raw) {
  uint32_t magic = SETTINGS_MAGIC;
  uint32_t crc = crc32(&s, sizeof(s));
  memcpy(raw, &magic, 4);
  memcpy(raw + 4, &s, sizeof(s));
  memcpy(raw + 4 + sizeof(s), &crc, 4);
  return 8 + sizeof(s);
}

bool settingsDecode(const uint8_t *raw, size_t n, Settings &out) {
  static_assert(8 + sizeof(Settings) <= SETTINGS_STORE_SIZE, "stored settings too big");
  uint32_t magic;
  if (n < 4) return false;
  memcpy(&magic, raw, 4);
  if (magic == SETTINGS_MAGIC && n >= 8 + sizeof(Settings)) {
    uint32_t crc;
    memcpy(&out, raw + 4, sizeof(Settings));
    memcpy(&crc, raw + 4 + sizeof(Settings), 4);
    if (crc != crc32(&out, sizeof(Settings))) return false;
  } else if (magic == SETTINGS_MAGIC_V3 && n >= sizeof(SettingsV3)) {
    SettingsV3 v3;
    memcpy(&v3, raw, sizeof(v3));
    memset(&out, 0, sizeof(out));
    out.brightness = v3.brightness;
    out.volume = v3.volume;
    memcpy(out.state, v3.state, sizeof(out.state));
  } else {
    return false;
  }
  settingsClamp(out);
  return true;
}
