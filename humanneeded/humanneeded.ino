// HumanNeeded firmware 2.0: a USB desk puck that shows what your AI coding agents need.
// The HumanNeeded app owns the puck and sends full-state snapshots under a lease.
// Protocol: hn-usb/2 (pm/contracts/usb-protocol.md in the project; summary in README.md).

#include <USB.h>
#include <EEPROM.h>
#include <tusb.h>
#include <pico/unique_id.h>
#include "config.h"
#include "core.h"
#include "settings.h"
#include "leds.h"
#include "audio.h"

static char serialNumber[2 + 2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];  // "HN" + chip id
static LineReader reader;
static Engine engine;
static Snap snap;                 // parse target (too big for the stack comfortably)
static char outMem[8192];         // every reply is built here, then sent in chunks
static Buf out(outMem, sizeof(outMem));
static bool testSound = false;    // the sound playing belongs to a TEST preview

// ---- 1200-baud reset off ----
// arduino-pico reboots into the bootloader when a host opens the port at 1200 baud and drops
// DTR, and has no switch for it. Its private "rebooting" flag makes it skip that check, so set
// it once at start-up. (Explicit template instantiation may name private members.)
template <auto M> struct SerialStateAccess {
  friend auto &serialState(SerialUSB &s) { return s.*M; }
};
auto &serialState(SerialUSB &s);
template struct SerialStateAccess<&SerialUSB::_ss>;

// ---- USB events (TinyUSB callbacks, run outside loop()) ----
static volatile bool usbMounted = false, usbUnmounted = false, helloPending = false;

extern "C" void tud_mount_cb(void) {
  usbMounted = true;
  helloPending = true;  // "!HELLO" once the host opens the port
}
extern "C" void tud_umount_cb(void) {  // unplug or bus reset
  usbMounted = false;
  usbUnmounted = true;
}

// ---- Settings in flash: saved SAVE_DELAY_MS after the last change, only if changed ----
static uint8_t stored[SETTINGS_STORE_SIZE];  // what flash holds now
static bool dirty = false;
static uint32_t changedMs = 0;
static uint32_t saveCount = 0;

static void markDirty() {
  dirty = true;
  changedMs = millis();
}

static void loadSettings() {
  EEPROM.begin(256);
  for (int i = 0; i < SETTINGS_STORE_SIZE; i++) stored[i] = EEPROM.read(i);
  if (!settingsDecode(stored, sizeof(stored), settings)) settingsDefaults(settings);
  settingsUpdated();
  uint8_t fresh[SETTINGS_STORE_SIZE] = {};
  size_t n = settingsEncode(settings, fresh);
  if (memcmp(fresh, stored, n)) markDirty();  // defaults, migrated or clamped: store the new form
}

// force: save now (REBOOT, BOOTSEL), else only when due and no sound is playing.
static void saveTick(bool force) {
  if (!dirty) return;
  if (!force && (millis() - changedMs < SAVE_DELAY_MS || audioPlaying())) return;
  dirty = false;
  uint8_t fresh[SETTINGS_STORE_SIZE] = {};
  size_t n = settingsEncode(settings, fresh);
  if (!memcmp(fresh, stored, n)) return;  // same as flash: no write
  for (size_t i = 0; i < n; i++) EEPROM.write(i, fresh[i]);
  EEPROM.commit();  // erases and writes one flash sector, interrupts off
  memcpy(stored, fresh, n);
  saveCount++;
}

// ---- Replies ----

// Sends in pieces the USB buffer can take, keeping the audio fed. Gives up on a host that
// doesn't read.
static void send(const char *p, size_t n) {
  uint32_t start = millis();
  while (n) {
    if (!Serial) return;  // port not open: nobody to read it
    int room = Serial.availableForWrite();
    if (room > 0) {
      size_t k = n < (size_t)room ? n : room;
      size_t w = Serial.write((const uint8_t *)p, k);
      p += w;
      n -= w;
    } else if (millis() - start > REPLY_TIMEOUT_MS) {
      return;
    }
    audioTick();
    rp2040.wdt_reset();
  }
  Serial.flush();
}

static void reply() {  // sends `out` as one line
  out.addc('\n');
  if (out.overflow) {  // can't happen with today's replies; never send half a line
    out.clear();
    out.add("ERR reply too long\n");
  }
  send(out.p, out.len);
  out.clear();
}

static void ok() { out.add("OK"); reply(); }
static void err(const char *msg) { out.add("ERR "); out.add(msg); reply(); }

// ---- Commands ----

static void playState(int state) {
  const StateStyle &st = settings.state[state];
  audioPlay(st.sound, st.volume, st.repeat);
}

static void handleEvents(int ev) {
  if ((ev & EV_TEST_ENDED) && testSound) {
    audioStop();
    testSound = false;
  }
  if (ev & EV_NEEDS_SOUND) {
    testSound = false;
    playState(ST_NEEDS);
  }
}

static void statusJson() {
  uint32_t now = millis();
  char muted[12];
  audioMuteText(muted, sizeof(muted));
  out.addf("{\"proto\":%d,\"lease_left\":%lu,\"shown\":", PROTO_VERSION, (unsigned long)engine.leaseLeftS(now));
  out.str(STATE_NAMES[engine.shown(now)]);
  out.add(",\"muted\":");
  out.str(muted);
  out.add(",\"entries\":[");
  for (int i = 0; i < engine.table.count; i++) {
    if (i) out.addc(',');
    out.add("{\"id\":");
    out.str(engine.table.e[i].id);
    out.add(",\"state\":");
    out.str(STATE_NAMES[engine.table.e[i].state]);
    out.addc('}');
  }
  // Beyond the contract, for the host test script: sounds played, the last one, flash saves.
  out.addf("],\"sounds\":%lu,\"last_sound\":", (unsigned long)audioPlayCount());
  out.str(SOUND_NAMES[audioLastSound()]);
  out.addf(",\"saves\":%lu}", (unsigned long)saveCount);
}

static void restart(bool bootloader) {
  ok();
  delay(50);          // let the reply leave
  audioSuspend(true); // amp off
  saveTick(true);
  if (bootloader) rp2040.rebootToBootloader();
  else rp2040.reboot();
}

// Splits off the next space-separated word. Modifies the string.
static char *word(char *&s) {
  while (*s == ' ' || *s == '\t') s++;
  if (!*s) return nullptr;
  char *start = s;
  while (*s && *s != ' ' && *s != '\t') s++;
  if (*s) *s++ = 0;
  return start;
}

static void handleLine(char *line) {
  char *rest = line;
  char *cmd = word(rest);
  if (!cmd) return;  // empty line: not a command, no reply

  if (!strcasecmp(cmd, "SNAP")) {  // the only command with a variable argument list
    const char *e = parseSnap(rest, snap);
    if (e) return err(e);
    handleEvents(engine.applySnap(snap, millis()));
    return ok();
  }

  char *a1 = word(rest), *a2 = a1 ? word(rest) : nullptr;
  int argc = !a1 ? 0 : !a2 ? 1 : word(rest) ? 3 : 2;  // 3 = too many for any command

  if (!strcasecmp(cmd, "HELLO")) {
    if (argc) return err("usage: HELLO");
    out.add("{\"name\":\"" USB_PRODUCT_NAME "\",\"fw\":\"" FW_VERSION "\",\"board\":\"" BOARD_ID "\",\"serial\":");
    out.str(serialNumber);
    out.addf(",\"proto\":%d}", PROTO_VERSION);
    reply();

  } else if (!strcasecmp(cmd, "FLASH")) {
    if (argc != 1 || strcmp(a1, "done")) return err("usage: FLASH done");
    handleEvents(engine.flashDone(millis(), settings.state[ST_DONE].duration));
    testSound = false;
    playState(ST_DONE);
    ok();

  } else if (!strcasecmp(cmd, "GET")) {
    if (argc != 1) return err("usage: GET <key|all>");
    if (!settingsGetJson(a1, out)) { out.clear(); return err("unknown key"); }
    reply();

  } else if (!strcasecmp(cmd, "SET")) {
    if (argc != 2) return err("usage: SET <key> <value>");
    if (!settingsSet(a1, a2)) return err("bad key or value");
    markDirty();
    ok();

  } else if (!strcasecmp(cmd, "SCHEMA")) {
    if (argc) return err("usage: SCHEMA");
    settingsSchema(out, ENABLE_AUDIO);
    reply();

  } else if (!strcasecmp(cmd, "RESET")) {
    if (argc) return err("usage: RESET");
    settingsDefaults(settings);
    settingsUpdated();
    markDirty();
    ok();

  } else if (!strcasecmp(cmd, "MUTE")) {
    uint32_t minutes;
    if (argc == 1 && !strcasecmp(a1, "on")) audioMuteOn();
    else if (argc == 1 && parseUint(a1, 0, 1440, minutes)) audioMute(minutes);
    else return err("usage: MUTE <0-1440|on>");
    testSound = false;
    ok();

  } else if (!strcasecmp(cmd, "TEST")) {
    if (argc == 1 && !strcasecmp(a1, "hello")) {
      ledsHello();  // light only, like power-up
      return ok();
    }
    int state = argc == 1 ? parseState(a1) : -1;
    if (state < 0) return err("usage: TEST <needs|done|working|idle|hello>");
    engine.startTest(state, millis());
    if (state != ST_IDLE) {
      playState(state);
      testSound = true;
    }
    ok();

  } else if (!strcasecmp(cmd, "STATUS")) {
    if (argc) return err("usage: STATUS");
    statusJson();
    reply();

  } else if (!strcasecmp(cmd, "REBOOT")) {
    if (argc) return err("usage: REBOOT");
    restart(false);

  } else if (!strcasecmp(cmd, "BOOTSEL")) {
    if (argc) return err("usage: BOOTSEL");
    restart(true);

  } else if (!strcasecmp(cmd, "SELFTEST")) {
    if (argc) return err("usage: SELFTEST");
    // Reply first (the pattern takes a few seconds), then run it: each LED R, G, B in turn,
    // and a speaker sweep. The tester watches and listens; the reply has what can be read.
    bool pressed = BOOTSEL;  // the recovery button is the BOOTSEL line
    out.add("{\"selftest\":\"started\",\"fw\":\"" FW_VERSION "\",\"board\":\"" BOARD_ID "\",\"serial\":");
    out.str(serialNumber);
    out.addf(",\"leds\":%d,\"audio\":%s,\"button\":\"%s\",\"ms\":%lu}", LED_COUNT,
             ENABLE_AUDIO ? "true" : "false", pressed ? "pressed" : "released",
             (unsigned long)(3UL * LED_COUNT * SELFTEST_STEP_MS));
    reply();
    ledsSelftest();
    testSound = false;
    audioSweep();

  } else {
    err("unknown command");
  }
}

static void readSerial() {
  while (Serial.available()) {
    int c = Serial.read();
    if (c < 0) break;
    LineEvent ev = reader.feed((char)c, millis());
    if (ev == LINE_READY) handleLine(reader.buf);
    else if (ev == LINE_TOO_LONG) err("line too long");
  }
  reader.tick(millis());  // drop a half line the host gave up on
}

// ---- main ----

void setup() {
  // USB identity: product "HumanNeeded", serial "HN<chip id>".
  strcpy(serialNumber, "HN");
  pico_get_unique_board_id_string(serialNumber + 2, sizeof(serialNumber) - 2);
  USB.disconnect();
  USB.setProduct(USB_PRODUCT_NAME);
  USB.setSerialNumber(serialNumber);
#if USB_PID_HN
  USB.setVIDPID(USB_VID_HN, USB_PID_HN);
#endif
  USB.connect();

  Serial.begin(115200);
  serialState(Serial).rebooting = 1;  // no 1200-baud bootloader entry

  loadSettings();
  engine.dur = settingsDurations;
  ledsBegin();
  audioBegin();
  ledsHello();  // power-up is light only, no sound

  rp2040.wdt_begin(WATCHDOG_MS);
}

void loop() {
  static uint32_t lastFrame = 0;
  static bool wasSuspended = false;
  rp2040.wdt_reset();

  // USB state: unplug or bus reset clears the table; suspend goes dark.
  if (usbUnmounted) {
    usbUnmounted = false;
    engine.clear();
  }
  bool suspended = tud_suspended();
  if (suspended != wasSuspended) {
    wasSuspended = suspended;
    audioSuspend(suspended);
  }
  // "!HELLO" once per USB connect, HELLO_DELAY_MS after the host opens the port: Windows
  // drivers purge what arrives while the port is being opened.
  static uint32_t openedMs = 0;
  static bool wasOpen = false;
  bool open = Serial;
  if (open && !wasOpen) openedMs = millis();
  wasOpen = open;
  if (helloPending && open && millis() - openedMs >= HELLO_DELAY_MS) {
    helloPending = false;
    send("!HELLO\n", 7);
  }

  readSerial();
  audioTick();
  handleEvents(engine.tick(millis()));
  saveTick(false);

  if (millis() - lastFrame >= 16) {  // ~60 fps
    lastFrame = millis();
    ledsRender(engine.shown(millis()), suspended, audioPlaying());
  }
}
