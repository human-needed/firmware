#include <EEPROM.h>
#include "config.h"
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
  setStyle(ST_NEEDS,   0xFFA62B, LIGHT_PULSE,     100,    0,    SOUND_CALL);
  setStyle(ST_DONE,    0x22E04A, LIGHT_PULSE,     100,    3,    SOUND_BRIGHT);
  setStyle(ST_WORKING, 0x3366FF, LED_COUNT >= 3 ? LIGHT_SPIN : LIGHT_BREATHE, 100, 0, SOUND_NONE);  // spin needs a ring
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

// ---- SCHEMA: the settings page's layout ----

static void jsonText(Print &out, const char *s) {
  out.print('"');
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') out.print('\\');
    out.print(*s);
  }
  out.print('"');
}

// Option list: [[value,label],...]
static void jsonOptions(Print &out, const char *const *values, const char *const *labels, int count) {
  out.print('[');
  for (int i = 0; i < count; i++) {
    if (i) out.print(',');
    out.print('[');
    jsonText(out, values[i]);
    out.print(',');
    jsonText(out, labels[i]);
    out.print(']');
  }
  out.print(']');
}

struct Field {
  const char *name, *label, *type;
  const char *options;  // select: name of a list in "lists"
  const char *unit;     // after a range's value; for a select, names a value missing from its list
  int16_t min, max;     // range
  const char *tip;
  bool idle;            // also shown for the idle state
};

static const char TIP_BRIGHTNESS[] = "Master brightness for the whole puck. Each state's own brightness is a share of this one: a state at 50% with Overall at 60% glows at half of 60%.";
static const char TIP_VOLUME[] = "Master volume for the whole puck. Each state's own volume is a share of this one: a sound at 50% with Overall at 70% plays at half of 70%. To silence everything for a while, ask your agent to mute the puck.";

static const Field GLOBAL_FIELDS[] = {
  {"brightness", "Brightness", "range", nullptr, "%", 0, 100, TIP_BRIGHTNESS, true},
  {"volume", "Volume", "range", nullptr, "%", 0, 100, TIP_VOLUME, true},
};

static const Field STATE_FIELDS[] = {
  {"color", "Colour", "color", nullptr, nullptr, 0, 0, nullptr, true},
  {"light", "Light", "select", "light", nullptr, 0, 0, nullptr, true},
  {"brightness", "Brightness", "range", nullptr, "%", 0, 100, "This state's brightness as a share of the Overall brightness.", true},
  {"duration", "Show light for", "select", "duration", "seconds", 0, 0,
   "How long the light shows this state after it starts. When the time is up the puck goes back to whatever else is happening (usually idle), even if the agent hasn't moved on.", false},
  {"sound", "Sound", "select", "sound", nullptr, 0, 0, nullptr, false},
  {"volume", "Volume", "range", nullptr, "%", 0, 100, "This state's volume as a share of the Overall volume.", false},
  {"repeat", "Play sound", "select", "repeat", nullptr, 0, 0,
   "How many times the sound plays when the state starts. Each sound is short, so this sets how long and how insistent the alert is.", false},
};

struct Group { State state; const char *title, *desc; };
static const Group STATE_GROUPS[] = {  // in the order the page shows them
  {ST_NEEDS, "Needs you", "An agent is waiting for your approval or an answer."},
  {ST_DONE, "Done", "An agent finished its task."},
  {ST_WORKING, "Working", "An agent is busy on your request."},
  {ST_IDLE, "Idle", "No agent is working or waiting. Sounds and duration don't apply here."},
};

static void jsonField(Print &out, const char *key, const Field &f) {
  out.print("{\"key\":");
  jsonText(out, key);
  out.print(",\"label\":");
  jsonText(out, f.label);
  out.print(",\"type\":");
  jsonText(out, f.type);
  if (f.options) { out.print(",\"options\":"); jsonText(out, f.options); }
  if (f.unit) { out.print(",\"unit\":"); jsonText(out, f.unit); }
  if (!strcmp(f.type, "range")) out.printf(",\"min\":%d,\"max\":%d", f.min, f.max);
  if (f.tip) { out.print(",\"tip\":"); jsonText(out, f.tip); }
  out.print('}');
}

void settingsPrintSchema(Print &out) {
  static const char *const DURATION_VALUES[] = {"0", "3", "5", "10", "30", "60", "300", "600"};
  static const char *const DURATION_LABELS[] = {"As long as the state lasts", "3 seconds", "5 seconds",
                                                "10 seconds", "30 seconds", "1 minute", "5 minutes", "10 minutes"};
  static const char *const REPEAT_VALUES[] = {"1", "2", "3", "4", "5"};
  static const char *const REPEAT_LABELS[] = {"Once", "2 times", "3 times", "4 times", "5 times"};

  out.print("{\"schema\":1,\"lists\":{\"light\":");
  jsonOptions(out, LIGHT_NAMES, LIGHT_LABELS, LIGHT_COUNT);
  out.print(",\"sound\":");
  jsonOptions(out, SOUND_NAMES, SOUND_LABELS, SOUND_COUNT);
  out.print(",\"duration\":");
  jsonOptions(out, DURATION_VALUES, DURATION_LABELS, 8);
  out.print(",\"repeat\":");
  jsonOptions(out, REPEAT_VALUES, REPEAT_LABELS, 5);

  out.print("},\"groups\":[{\"id\":\"overall\",\"title\":\"Overall\",\"desc\":\"Master controls for the whole puck.\",\"fields\":[");
  bool first = true;
  for (const Field &f : GLOBAL_FIELDS) {
    if (!ENABLE_AUDIO && !strcmp(f.name, "volume")) continue;  // no speaker
    if (!first) out.print(',');
    first = false;
    jsonField(out, f.name, f);
  }
  out.print("]}");

  for (const Group &g : STATE_GROUPS) {
    const char *id = STATE_NAMES[g.state];
    char key[24];
    out.print(",{\"id\":");
    jsonText(out, id);
    out.print(",\"title\":");
    jsonText(out, g.title);
    out.print(",\"desc\":");
    jsonText(out, g.desc);
    out.print(",\"preview\":");  // sent as TEST <preview>
    jsonText(out, id);
    snprintf(key, sizeof(key), "%s.color", id);
    out.print(",\"swatch\":");   // the key whose colour marks the card
    jsonText(out, key);
    out.print(",\"fields\":[");
    first = true;
    for (const Field &f : STATE_FIELDS) {
      if (g.state == ST_IDLE && !f.idle) continue;
      bool sound = !strcmp(f.name, "sound") || !strcmp(f.name, "volume") || !strcmp(f.name, "repeat");
      if (!ENABLE_AUDIO && sound) continue;
      if (!first) out.print(',');
      first = false;
      snprintf(key, sizeof(key), "%s.%s", id, f.name);
      jsonField(out, key, f);
    }
    out.print("]}");
  }
  out.println("]}");
}
