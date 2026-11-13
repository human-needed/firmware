#include <stdarg.h>
#include <stdio.h>
#include "core.h"

// ---- Buf ----

void Buf::add(const char *s) {
  while (*s) addc(*s++);
}

void Buf::addc(char c) {
  if (len + 1 >= cap) { overflow = true; return; }
  p[len++] = c;
  p[len] = 0;
}

void Buf::addf(const char *fmt, ...) {
  if (len + 1 >= cap) { overflow = true; return; }
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(p + len, cap - len, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if ((size_t)n >= cap - len) { overflow = true; len = cap - 1; p[len] = 0; return; }
  len += n;
}

void Buf::str(const char *s) {
  addc('"');
  for (; *s; s++) {
    unsigned char c = *s;
    if (c == '"' || c == '\\') { addc('\\'); addc(c); }
    else if (c < 0x20) addf("\\u%04x", c);
    else addc(c);
  }
  addc('"');
}

// ---- LineReader ----

LineEvent LineReader::feed(char c, uint32_t now) {
  lastByteMs = now;
  if (c == '\r') { started = true; return LINE_NONE; }
  if (c == '\n') {
    bool tooLong = overflow;
    buf[len] = 0;
    len = 0;
    overflow = false;
    started = false;
    if (tooLong) { buf[0] = 0; return LINE_TOO_LONG; }
    return LINE_READY;
  }
  started = true;
  if (len < LINE_MAX_BYTES) buf[len++] = c;
  else overflow = true;
  return LINE_NONE;
}

bool LineReader::tick(uint32_t now) {
  if (!started || now - lastByteMs < LINE_IDLE_MS) return false;
  len = 0;
  overflow = false;
  started = false;
  return true;
}

// ---- Parsing ----

bool parseUint(const char *s, uint32_t lo, uint32_t hi, uint32_t &out) {
  if (!s || !*s || strlen(s) > 9) return false;
  uint32_t v = 0;
  for (const char *q = s; *q; q++) {
    if (*q < '0' || *q > '9') return false;
    v = v * 10 + (*q - '0');
  }
  if (v < lo || v > hi) return false;
  out = v;
  return true;
}

static int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseColor(const char *s, uint32_t &out) {
  if (!s || s[0] != '#' || strlen(s) != 7) return false;
  uint32_t v = 0;
  for (int i = 1; i < 7; i++) {
    int d = hexDigit(s[i]);
    if (d < 0) return false;
    v = (v << 4) | d;
  }
  out = v;
  return true;
}

uint32_t crc32(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFu;
  while (len--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

// Next space-separated token, or nullptr. Modifies the string.
static char *nextToken(char *&s) {
  while (*s == ' ' || *s == '\t') s++;
  if (!*s) return nullptr;
  char *start = s;
  while (*s && *s != ' ' && *s != '\t') s++;
  if (*s) *s++ = 0;
  return start;
}

const char *parseSnap(char *args, Snap &out) {
  char *s = args ? args : (char *)"";
  char *tok = nextToken(s);
  uint32_t lease;
  if (!tok) return "usage: SNAP <lease_s> [<id>:<state> ...]";
  if (!parseUint(tok, LEASE_MIN_S, LEASE_MAX_S, lease)) return "bad lease (5-600)";
  out.lease = lease;
  out.count = 0;
  while ((tok = nextToken(s))) {
    if (out.count >= SNAP_MAX_ENTRIES) return "too many entries (max 32)";
    if (strlen(tok) < 10 || tok[8] != ':') return "bad entry, want <8 hex>:<working|needs>";
    for (int i = 0; i < 8; i++) {
      char c = tok[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return "bad id, want 8 lowercase hex";
    }
    const char *st = tok + 9;
    uint8_t state;
    if (!strcmp(st, "working")) state = ST_WORKING;
    else if (!strcmp(st, "needs")) state = ST_NEEDS;
    else return "bad state, want working or needs";
    for (int i = 0; i < out.count; i++) {
      if (!memcmp(out.e[i].id, tok, 8)) return "duplicate id";
    }
    Entry &e = out.e[out.count++];
    memcpy(e.id, tok, 8);
    e.id[8] = 0;
    e.state = state;
  }
  return nullptr;
}

// ---- Engine ----

bool Engine::within(uint8_t st, uint32_t since, uint32_t now) const {
  uint32_t d = dur ? dur[st] : 0;
  return d == 0 || now - since < d * 1000UL;
}

State Engine::base(uint32_t now) const {
  if (doneOn) return ST_DONE;
  if (needsOn && within(ST_NEEDS, needsSince, now)) return ST_NEEDS;
  if (workingOn && within(ST_WORKING, workingSince, now)) return ST_WORKING;
  return ST_IDLE;
}

State Engine::shown(uint32_t now) const {
  return testState >= 0 ? (State)testState : base(now);
}

uint32_t Engine::leaseLeftS(uint32_t now) const {
  if (!leased || timeReached(now, leaseEnd)) return 0;
  return (leaseEnd - now + 999) / 1000;
}

int Engine::checkTest(uint32_t now) {
  if (testState >= 0 && base(now) != testBase) {
    testState = -1;
    return EV_TEST_ENDED;
  }
  return 0;
}

int Engine::applySnap(const Snap &s, uint32_t now) {
  bool newNeeds = false;
  for (int i = 0; i < s.count; i++) {
    if (s.e[i].state != ST_NEEDS) continue;
    bool was = false;
    for (int j = 0; leased && j < table.count; j++) {
      if (table.e[j].state == ST_NEEDS && !memcmp(table.e[j].id, s.e[i].id, 8)) { was = true; break; }
    }
    if (!was) newNeeds = true;
  }
  bool wasNeeds = needsOn, wasWorking = workingOn;
  table = s;
  leased = true;
  leaseEnd = now + s.lease * 1000UL;
  needsOn = workingOn = false;
  for (int i = 0; i < s.count; i++) {
    if (s.e[i].state == ST_NEEDS) needsOn = true;
    else workingOn = true;
  }
  // A state's duration runs from when it starts; a new id in needs starts needs again.
  if (needsOn && (!wasNeeds || newNeeds)) needsSince = now;
  if (workingOn && !wasWorking) workingSince = now;
  return (newNeeds ? EV_NEEDS_SOUND : 0) | checkTest(now);
}

int Engine::flashDone(uint32_t now, uint16_t doneSeconds) {
  if (doneSeconds < 1) doneSeconds = 1;
  doneOn = true;
  doneSince = now;
  doneEnd = now + doneSeconds * 1000UL;
  return checkTest(now);
}

void Engine::startTest(int state, uint32_t now) {
  uint32_t d = dur ? dur[state] : 0;
  uint32_t ms = (d == 0 || d * 1000UL > TEST_MAX_MS) ? TEST_MAX_MS : d * 1000UL;
  testState = state;
  testEnd = now + ms;
  testBase = base(now);
}

void Engine::clear() {
  table.count = 0;
  leased = false;
  needsOn = workingOn = false;
}

int Engine::tick(uint32_t now) {
  int ev = 0;
  if (leased && timeReached(now, leaseEnd)) {
    clear();
    ev |= EV_CLEARED;
  }
  if (doneOn && timeReached(now, doneEnd)) doneOn = false;
  if (testState >= 0 && timeReached(now, testEnd)) testState = -1;
  return ev | checkTest(now);
}
