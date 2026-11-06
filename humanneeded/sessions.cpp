#include "config.h"
#include "sessions.h"
#include "settings.h"
#include "audio.h"

struct Session {
  bool used;
  char id[48];
  char agent[16];
  uint8_t state;
  uint32_t lastEventMs;   // any event for this session (LRU + working stale guard)
  uint32_t stateSinceMs;  // when the current state began (light duration)
};

static Session table[MAX_SESSIONS];
static uint32_t skewMs = 0;

uint32_t nowMs() { return millis() + skewMs; }
void skipMinutes(uint32_t minutes) { skewMs += minutes * 60000UL; }

static Session *find(const char *id) {
  for (auto &s : table) {
    if (s.used && strcmp(s.id, id) == 0) return &s;
  }
  return nullptr;
}

// A free slot, or the least recently used one when the table is full.
static Session *allocate() {
  Session *oldest = &table[0];
  for (auto &s : table) {
    if (!s.used) return &s;
    if (nowMs() - s.lastEventMs > nowMs() - oldest->lastEventMs) oldest = &s;
  }
  return oldest;
}

// A session lights the puck while its state's duration hasn't run out (0 = no limit).
static bool lit(const Session &s) {
  uint32_t secs = settings.state[s.state].duration;
  return secs == 0 || nowMs() - s.stateSinceMs < secs * 1000UL;
}

State sessionsTop() {
  State top = ST_IDLE;
  for (auto &s : table) {
    if (s.used && s.state > top && lit(s)) top = (State)s.state;
  }
  return top;
}

static void setState(Session &s, State state) {
  s.state = state;
  s.stateSinceMs = nowMs();
}

void sessionsEvent(int state, const char *id, const char *agent) {
  Session *s = find(id);

  if (state == EV_END) {
    if (s) s->used = false;
    return;
  }

  if (!s) {
    s = allocate();
    memset(s, 0, sizeof(*s));
    s->used = true;
    strlcpy(s->id, id, sizeof(s->id));
    s->state = ST_IDLE;
  }
  if (agent && *agent) strlcpy(s->agent, agent, sizeof(s->agent));
  s->lastEventMs = nowMs();

  if (s->state == state) return;  // repeated event: refresh only, no new sound
  setState(*s, (State)state);

  // Sound when a session enters a state, unless a higher state is already showing.
  if (state != ST_IDLE && sessionsTop() == state) {
    const StateStyle &st = settings.state[state];
    audioPlay(st.sound, st.volume, st.repeat);
  }
}

void sessionsTick() {
  for (auto &s : table) {
    if (s.used && s.state == ST_WORKING && nowMs() - s.lastEventMs > WORKING_STALE_MS) {
      setState(s, ST_IDLE);
    }
  }
}

void sessionsClear() {
  for (auto &s : table) s.used = false;
}

int sessionsCount() {
  int n = 0;
  for (auto &s : table) n += s.used;
  return n;
}

String sessionsJson() {
  String out;
  for (auto &s : table) {
    if (!s.used) continue;
    if (out.length()) out += ',';
    out += "{\"id\":\"" + String(s.id) + "\",\"agent\":\"" + String(s.agent) +
           "\",\"state\":\"" + STATE_NAMES[s.state] +
           "\",\"lit\":" + (lit(s) ? "true" : "false") +
           ",\"age_s\":" + String((nowMs() - s.stateSinceMs) / 1000) + "}";
  }
  return out;
}
