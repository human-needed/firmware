// HumanNeeded firmware: a USB desk puck that shows what your AI coding agents need.
// Protocol, states and settings are defined in SPEC.md.

#include <USB.h>
#include <pico/unique_id.h>
#include "config.h"
#include "state.h"
#include "settings.h"
#include "sessions.h"
#include "leds.h"
#include "audio.h"

static char serialNumber[2 + 2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];  // "HN" + chip id
static char line[200];
static size_t lineLen = 0;
static int testState = -1;
static uint32_t testUntil = 0;

static State displayState() {
  if (testState >= 0 && (int32_t)(testUntil - millis()) > 0) return (State)testState;
  testState = -1;
  return sessionsTop();
}

// ---- command handling: one reply line per command ----

static void ok() { Serial.println("OK"); }
static void err(const char *msg) { Serial.print("ERR "); Serial.println(msg); }

static void handleStatus() {
  Serial.print("{\"state\":\"");
  Serial.print(STATE_NAMES[displayState()]);
  Serial.print("\",\"muted\":");
  Serial.print(audioMuted() ? "true" : "false");
  Serial.print(",\"sounds\":");
  Serial.print(audioPlayCount());
  Serial.print(",\"last_sound\":\"");
  Serial.print(SOUND_NAMES[audioLastSound()]);
  Serial.print("\",\"sessions\":[");
  Serial.print(sessionsJson());
  Serial.println("]}");
}

static void handleCommand(char *cmdLine) {
  char *cmd = strtok(cmdLine, " \t");
  char *a1 = strtok(nullptr, " \t");
  char *a2 = strtok(nullptr, " \t");
  char *a3 = strtok(nullptr, " \t");
  if (!cmd) return;

  if (!strcasecmp(cmd, "HELLO")) {
    Serial.print("{\"name\":\"" USB_PRODUCT_NAME "\",\"fw\":\"" FW_VERSION "\",\"board\":\"" BOARD_ID "\",\"serial\":\"");
    Serial.print(serialNumber);
    Serial.println("\"}");

  } else if (!strcasecmp(cmd, "EV")) {
    int state = a1 && !strcasecmp(a1, "end") ? EV_END : (a1 ? parseState(a1) : -1);
    if (state == -1 || !a2) return err("usage: EV <working|needs|done|idle|end> <session_id> [agent]");
    sessionsEvent(state, a2, a3);
    ok();

  } else if (!strcasecmp(cmd, "SET")) {
    if (!a1 || !a2) return err("usage: SET <key> <value>");
    if (!settingsSet(a1, a2)) return err("bad key or value");
    ok();

  } else if (!strcasecmp(cmd, "GET")) {
    String json;
    if (!a1) return err("usage: GET <key|all>");
    if (!settingsGetJson(a1, json)) return err("unknown key");
    Serial.println(json);

  } else if (!strcasecmp(cmd, "RESET")) {  // settings back to defaults
    settingsDefaults();
    settingsSave();
    ok();

  } else if (!strcasecmp(cmd, "MUTE")) {
    long minutes = a1 ? atol(a1) : -1;
    if (minutes < 0) return err("usage: MUTE <minutes>");
    audioMute(minutes);
    ok();

  } else if (!strcasecmp(cmd, "TEST")) {
    if (a1 && !strcasecmp(a1, "hello")) {
      ledsHello();
      audioPlay(SOUND_ARPEGGIO, 100, 1);
      return ok();
    }
    int state = a1 ? parseState(a1) : -1;
    if (state < 0) return err("usage: TEST <idle|working|done|needs|hello>");
    // Preview exactly as configured: the state's light for its duration (or 5 s), and its sound.
    const StateStyle &st = settings.state[state];
    testState = state;
    testUntil = millis() + (st.duration ? st.duration * 1000UL : TEST_PREVIEW_MS);
    if (state != ST_IDLE) audioPlay(st.sound, st.volume, st.repeat);
    ok();

  } else if (!strcasecmp(cmd, "CLEAR")) {
    sessionsClear();
    testState = -1;
    ok();

  } else if (!strcasecmp(cmd, "BOOTSEL")) {
    ok();
    Serial.flush();
    delay(100);
    rp2040.rebootToBootloader();

  } else if (!strcasecmp(cmd, "REBOOT")) {
    ok();
    Serial.flush();
    delay(100);
    rp2040.reboot();

  // ---- extras beyond SPEC.md, for debugging and tests ----
  } else if (!strcasecmp(cmd, "STATUS")) {
    handleStatus();

  } else if (!strcasecmp(cmd, "SKIP")) {  // move the session clock forward N minutes
    long minutes = a1 ? atol(a1) : 0;
    if (minutes <= 0) return err("usage: SKIP <minutes>");
    skipMinutes(minutes);
    ok();

  } else {
    err("unknown command");
  }
}

static void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[lineLen] = 0;
      handleCommand(line);
      lineLen = 0;
    } else if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = c;
    }
  }
}

// ---- main ----

void setup() {
  // USB identity: product "HumanNeeded", serial "HN<chip id>".
  strcpy(serialNumber, "HN");
  pico_get_unique_board_id_string(serialNumber + 2, sizeof(serialNumber) - 2);
  USB.disconnect();
  USB.setProduct(USB_PRODUCT_NAME);
  USB.setSerialNumber(serialNumber);
  USB.connect();

  Serial.begin(115200);
  settingsLoad();
  ledsBegin();
  audioBegin();

  ledsHello();
  audioPlay(SOUND_ARPEGGIO, 100, 1);
}

void loop() {
  static uint32_t lastFrame = 0, lastTick = 0;

  readSerial();
  audioTick();

  if (millis() - lastTick >= 250) {
    lastTick = millis();
    sessionsTick();
  }
  if (millis() - lastFrame >= 16) {  // ~60 fps
    lastFrame = millis();
    ledsRender(displayState());
  }
}
