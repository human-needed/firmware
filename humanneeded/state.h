#pragma once
#include <Arduino.h>

// Display states, ordered by priority: a higher value wins.
enum State : uint8_t { ST_IDLE = 0, ST_WORKING, ST_DONE, ST_NEEDS, ST_COUNT };

static const char *const STATE_NAMES[ST_COUNT] = {"idle", "working", "done", "needs"};

// Returns the State for a name, or -1 if unknown.
inline int parseState(const char *name) {
  for (int i = 0; i < ST_COUNT; i++) {
    if (strcasecmp(name, STATE_NAMES[i]) == 0) return i;
  }
  return -1;
}
