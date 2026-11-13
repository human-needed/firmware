#pragma once
#include <stdint.h>
#include <string.h>
#include <strings.h>

// Display states, ordered by priority: a higher value wins.
// done is only ever shown by FLASH done (or TEST done); SNAP entries are working or needs.
enum State : uint8_t { ST_IDLE = 0, ST_WORKING, ST_DONE, ST_NEEDS, ST_COUNT };

static const char *const STATE_NAMES[ST_COUNT] = {"idle", "working", "done", "needs"};

// Returns the State for a name (any case), or -1 if unknown.
inline int parseState(const char *name) {
  for (int i = 0; i < ST_COUNT; i++) {
    if (strcasecmp(name, STATE_NAMES[i]) == 0) return i;
  }
  return -1;
}

// Time comparisons that survive the 49-day millis() wrap.
inline bool timeReached(uint32_t now, uint32_t when) { return (int32_t)(now - when) >= 0; }
