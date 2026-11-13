#pragma once
// Protocol logic with no hardware in it (hn-usb/2, pm/contracts/usb-protocol.md).
// Builds both in the sketch and on the PC: test/core_test.cpp unit-tests it.
#include <stddef.h>
#include <stdio.h>
#include "state.h"

#define LINE_MAX_BYTES   1024  // longest accepted line, without the "\n"
#define LINE_IDLE_MS     500   // a partial line with no byte for this long is dropped
#define SNAP_MAX_ENTRIES 32
#define LEASE_MIN_S      5
#define LEASE_MAX_S      600
#define TEST_MAX_MS      10000

// ---- Reply buffer: everything is built here, then sent in chunks. ----
struct Buf {
  char *p;
  size_t cap, len;
  bool overflow;
  Buf(char *mem, size_t size) : p(mem), cap(size), len(0), overflow(false) { p[0] = 0; }
  void clear() { len = 0; overflow = false; p[0] = 0; }
  void add(const char *s);
  void addc(char c);
  void addf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  void str(const char *s);  // a JSON string, quoted and escaped
};

// ---- Line assembly ----
enum LineEvent { LINE_NONE, LINE_READY, LINE_TOO_LONG };

struct LineReader {
  char buf[LINE_MAX_BYTES + 1];
  size_t len = 0;
  bool overflow = false;   // the current line went past LINE_MAX_BYTES
  bool started = false;    // bytes of a line have arrived
  uint32_t lastByteMs = 0;

  // Feeds one byte. LINE_READY: buf holds the line (NUL terminated). LINE_TOO_LONG: reply an error.
  LineEvent feed(char c, uint32_t now);
  // Drops a partial line that has had no byte for LINE_IDLE_MS. Returns true if it dropped one.
  bool tick(uint32_t now);
};

// ---- Snapshot ----
struct Entry {
  char id[9];     // 8 lowercase hex + NUL
  uint8_t state;  // ST_WORKING or ST_NEEDS
};

struct Snap {
  uint16_t lease;  // seconds
  uint8_t count;
  Entry e[SNAP_MAX_ENTRIES];
};

// Parses the arguments after "SNAP" (the string is modified). Returns nullptr when valid,
// else the error text for "ERR <text>". Nothing is applied here.
const char *parseSnap(char *args, Snap &out);

// ---- What the ring shows ----
// Event bits returned by Engine calls.
enum {
  EV_NEEDS_SOUND = 1,  // play the needs sound (an id entered needs)
  EV_TEST_ENDED = 2,   // a TEST preview was cut short by a real change; stop its sound
  EV_CLEARED = 4,      // the lease ran out and the table was cleared
};

struct Engine {
  Snap table = {};
  bool leased = false;
  uint32_t leaseEnd = 0;
  bool needsOn = false, workingOn = false;  // any entry in that state
  uint32_t needsSince = 0, workingSince = 0;
  bool doneOn = false;                      // FLASH done light
  uint32_t doneSince = 0, doneEnd = 0;
  int testState = -1;                       // TEST preview, or -1
  uint32_t testEnd = 0;
  int testBase = -1;                        // what was shown under the preview when it began
  const uint16_t *dur = nullptr;            // per-state duration in seconds (settings), 0 = no limit

  // Applies a validated snapshot.
  int applySnap(const Snap &s, uint32_t now);
  // FLASH done: the done light for doneSeconds (at least 1).
  int flashDone(uint32_t now, uint16_t doneSeconds);
  // TEST <state>: preview for the state's duration, capped at TEST_MAX_MS.
  void startTest(int state, uint32_t now);
  // Lease, FLASH and TEST timeouts. Call often.
  int tick(uint32_t now);
  // Table cleared: lease end, USB unmount, bus reset, power-up.
  void clear();

  State base(uint32_t now) const;   // shown state without the TEST preview
  State shown(uint32_t now) const;  // what the ring shows
  uint32_t leaseLeftS(uint32_t now) const;

 private:
  bool within(uint8_t st, uint32_t since, uint32_t now) const;
  int checkTest(uint32_t now);
};

// ---- Strict value parsing ----
bool parseUint(const char *s, uint32_t lo, uint32_t hi, uint32_t &out);  // digits only
bool parseColor(const char *s, uint32_t &out);                          // "#RRGGBB"
uint32_t crc32(const void *data, size_t len);
