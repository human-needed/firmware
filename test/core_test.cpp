// PC unit tests for the protocol logic (core.cpp, settings.cpp). No hardware needed.
//   g++ -std=gnu++17 -I../humanneeded core_test.cpp ../humanneeded/core.cpp ../humanneeded/settings.cpp -o core_test && ./core_test
#include <stdio.h>
#include <string>
#include "core.h"
#include "settings.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static LineEvent feedStr(LineReader &r, const std::string &s, uint32_t now) {
  LineEvent last = LINE_NONE;
  for (char c : s) { LineEvent e = r.feed(c, now); if (e != LINE_NONE) last = e; }
  return last;
}

static const char *snapErr(const char *args, Snap &s) {
  static char buf[2048];
  snprintf(buf, sizeof(buf), "%s", args);
  return parseSnap(buf, s);
}

static void testLines() {
  LineReader r;
  CHECK(feedStr(r, "HELLO\r\n", 0) == LINE_READY && !strcmp(r.buf, "HELLO"));
  CHECK(feedStr(r, std::string(1024, 'a') + "\n", 0) == LINE_READY && strlen(r.buf) == 1024);
  CHECK(feedStr(r, std::string(1025, 'a') + "\n", 0) == LINE_TOO_LONG);
  CHECK(feedStr(r, std::string(5000, 'a') + "\n", 0) == LINE_TOO_LONG);
  CHECK(feedStr(r, "STATUS\n", 0) == LINE_READY && !strcmp(r.buf, "STATUS"));
  // half line dropped after 500 ms of silence, kept before
  feedStr(r, "HEL", 1000);
  CHECK(!r.tick(1499));
  CHECK(r.tick(1500));
  CHECK(feedStr(r, "LO\n", 1600) == LINE_READY && !strcmp(r.buf, "LO"));
  // a dropped over-long half line doesn't spoil the next one
  feedStr(r, std::string(2000, 'x'), 2000);
  CHECK(r.tick(2600));
  CHECK(feedStr(r, "HELLO\n", 2700) == LINE_READY && !strcmp(r.buf, "HELLO"));
}

static void testSnapParse() {
  Snap s;
  CHECK(snapErr("30", s) == nullptr && s.lease == 30 && s.count == 0);
  CHECK(snapErr("  30   0a1b2c3d:needs  ffffffff:working ", s) == nullptr && s.count == 2 &&
        !strcmp(s.e[0].id, "0a1b2c3d") && s.e[0].state == ST_NEEDS && s.e[1].state == ST_WORKING);
  CHECK(snapErr("", s) != nullptr);
  CHECK(snapErr("4", s) != nullptr);
  CHECK(snapErr("5", s) == nullptr);
  CHECK(snapErr("600", s) == nullptr);
  CHECK(snapErr("601", s) != nullptr);
  CHECK(snapErr("30x", s) != nullptr);
  CHECK(snapErr("-30", s) != nullptr);
  CHECK(snapErr("30 0A1B2C3D:needs", s) != nullptr);   // uppercase id
  CHECK(snapErr("30 0a1b2c3:needs", s) != nullptr);    // 7 hex
  CHECK(snapErr("30 0a1b2c3d0:needs", s) != nullptr);  // 9 hex
  CHECK(snapErr("30 0a1b2c3g:needs", s) != nullptr);
  CHECK(snapErr("30 0a1b2c3d:done", s) != nullptr);
  CHECK(snapErr("30 0a1b2c3d:idle", s) != nullptr);
  CHECK(snapErr("30 0a1b2c3d:Needs", s) != nullptr);
  CHECK(snapErr("30 0a1b2c3d:", s) != nullptr);
  CHECK(snapErr("30 0a1b2c3d", s) != nullptr);
  CHECK(snapErr("30 0a1b2c3d:needs 0a1b2c3d:working", s) != nullptr);  // duplicate
  std::string many = "30";
  for (int i = 0; i < 32; i++) { char e[20]; snprintf(e, sizeof(e), " %08x:working", i); many += e; }
  CHECK(snapErr(many.c_str(), s) == nullptr && s.count == 32);
  CHECK(snapErr((many + " deadbeef:needs").c_str(), s) != nullptr);
}

static Snap mk(uint16_t lease, std::initializer_list<std::pair<const char *, int>> es) {
  Snap s = {};
  s.lease = lease;
  for (auto &e : es) { Entry &x = s.e[s.count++]; memcpy(x.id, e.first, 9); x.state = e.second; }
  return s;
}

static void testEngine() {
  uint16_t dur[ST_COUNT] = {0, 0, 3, 0};  // idle, working, done, needs
  Engine g;
  g.dur = dur;
  CHECK(g.shown(0) == ST_IDLE && g.leaseLeftS(0) == 0);

  CHECK(g.applySnap(mk(30, {{"00000001", ST_WORKING}}), 1000) == 0);
  CHECK(g.shown(1000) == ST_WORKING && g.leaseLeftS(1000) == 30);
  CHECK(g.applySnap(mk(30, {{"00000001", ST_NEEDS}}), 2000) & EV_NEEDS_SOUND);
  CHECK(g.shown(2000) == ST_NEEDS);
  CHECK(!(g.applySnap(mk(30, {{"00000001", ST_NEEDS}}), 3000) & EV_NEEDS_SOUND));  // already needs
  CHECK(g.applySnap(mk(30, {{"00000001", ST_NEEDS}, {"00000002", ST_NEEDS}}), 4000) & EV_NEEDS_SOUND);
  CHECK(!(g.applySnap(mk(30, {{"00000002", ST_NEEDS}}), 5000) & EV_NEEDS_SOUND));
  // working -> needs again counts as entering
  g.applySnap(mk(30, {{"00000002", ST_WORKING}}), 6000);
  CHECK(g.applySnap(mk(30, {{"00000002", ST_NEEDS}}), 7000) & EV_NEEDS_SOUND);

  // lease runs out: cleared, idle; a lapsed lease counts as empty for the sound
  CHECK(!(g.tick(36999) & EV_CLEARED) && g.shown(36999) == ST_NEEDS);
  CHECK((g.tick(37000) & EV_CLEARED) && g.shown(37000) == ST_IDLE && g.table.count == 0);
  CHECK(g.applySnap(mk(30, {{"00000002", ST_NEEDS}}), 38000) & EV_NEEDS_SOUND);
  g.clear();  // USB unmount
  CHECK(g.shown(38000) == ST_IDLE);
  CHECK(g.applySnap(mk(30, {{"00000002", ST_NEEDS}}), 39000) & EV_NEEDS_SOUND);

  // FLASH done shows over needs for done.duration, then needs again
  g.flashDone(40000, 3);
  CHECK(g.shown(40000) == ST_DONE);
  g.tick(42999);
  CHECK(g.shown(42999) == ST_DONE);
  g.tick(43000);
  CHECK(g.shown(43000) == ST_NEEDS);
  g.flashDone(44000, 0);  // never less than 1 s
  g.tick(44999);
  CHECK(g.shown(44999) == ST_DONE);
  g.tick(45000);
  CHECK(g.shown(45000) == ST_NEEDS);

  // per-state duration: needs shows 10 s, then working underneath
  dur[ST_NEEDS] = 10;
  Engine h;
  h.dur = dur;
  h.applySnap(mk(600, {{"0000000a", ST_WORKING}}), 0);
  h.applySnap(mk(600, {{"0000000a", ST_WORKING}, {"0000000b", ST_NEEDS}}), 1000);
  CHECK(h.shown(10999) == ST_NEEDS);
  CHECK(h.shown(11000) == ST_WORKING);
  // a new id entering needs starts it again
  h.applySnap(mk(600, {{"0000000a", ST_WORKING}, {"0000000b", ST_NEEDS}, {"0000000c", ST_NEEDS}}), 12000);
  CHECK(h.shown(21999) == ST_NEEDS && h.shown(22000) == ST_WORKING);
  dur[ST_NEEDS] = 0;

  // TEST: shown over everything, max 10 s
  Engine t;
  t.dur = dur;
  t.startTest(ST_NEEDS, 0);  // needs duration 0 -> capped at 10 s
  CHECK(t.shown(0) == ST_NEEDS);
  t.tick(9999);
  CHECK(t.shown(9999) == ST_NEEDS);
  t.tick(10000);
  CHECK(t.shown(10000) == ST_IDLE);
  t.startTest(ST_DONE, 20000);  // done duration 3 s
  t.tick(22999);
  CHECK(t.shown(22999) == ST_DONE);
  t.tick(23000);
  CHECK(t.shown(23000) == ST_IDLE);
  // a SNAP that doesn't change what's shown keeps the preview
  t.startTest(ST_NEEDS, 30000);
  CHECK(!(t.applySnap(mk(30, {}), 31000) & EV_TEST_ENDED) && t.shown(31000) == ST_NEEDS);
  // one that does ends it at once
  CHECK((t.applySnap(mk(30, {{"00000001", ST_WORKING}}), 32000) & EV_TEST_ENDED) && t.shown(32000) == ST_WORKING);
  // FLASH ends it too
  t.startTest(ST_IDLE, 33000);
  CHECK((t.flashDone(34000, 3) & EV_TEST_ENDED) && t.shown(34000) == ST_DONE);
  // and so does the lease running out under it
  t.tick(40000);
  t.applySnap(mk(5, {{"00000001", ST_WORKING}}), 50000);
  t.startTest(ST_NEEDS, 51000);
  CHECK((t.tick(55000) & EV_TEST_ENDED) && t.shown(55000) == ST_IDLE);
}

static void testSettings() {
  uint32_t n;
  CHECK(parseUint("0", 0, 10, n) && n == 0);
  CHECK(!parseUint("", 0, 10, n) && !parseUint("+1", 0, 10, n) && !parseUint(" 1", 0, 10, n) && !parseUint("11", 0, 10, n));
  CHECK(!parseUint("99999999999", 0, 0xFFFFFFFF, n));
  CHECK(parseColor("#A0b1C2", n) && n == 0xA0B1C2);
  CHECK(!parseColor("A0B1C2", n) && !parseColor("#A0B1C", n) && !parseColor("#A0B1CG", n) && !parseColor("#+0B1C2", n));

  settingsDefaults(settings);
  settingsUpdated();
  CHECK(settingsSet("needs.color", "#112233") && settings.state[ST_NEEDS].color == 0x112233);
  CHECK(settingsSet("BRIGHTNESS", "100") && settings.brightness == 100);
  CHECK(!settingsSet("brightness", "101"));
  CHECK(!settingsSet("done.duration", "0"));
  CHECK(settingsSet("done.duration", "1") && settingsDurations[ST_DONE] == 1);
  CHECK(settingsSet("needs.duration", "0"));
  CHECK(!settingsSet("needs.duration", "3601"));
  CHECK(!settingsSet("idle.sound", "ding") && !settingsSet("idle.volume", "5") && !settingsSet("idle.repeat", "2"));
  CHECK(settingsSet("needs.sound", "harp") && settings.state[ST_NEEDS].sound == SOUND_HARP);
  CHECK(!settingsSet("needs.sound", "nope") && !settingsSet("bogus", "1") && !settingsSet("bogus.color", "#000000"));
  CHECK(!settingsSet("needs.repeat", "0") && !settingsSet("needs.repeat", "6"));
  CHECK(!settingsSet("averyveryverylongstate.color", "#000000"));

  static char mem[8192];
  Buf b(mem, sizeof(mem));
  CHECK(settingsGetJson("needs.color", b) && !strcmp(b.p, "{\"needs.color\":\"#112233\"}"));
  b.clear();
  CHECK(!settingsGetJson("nope", b));
  b.clear();
  CHECK(settingsGetJson("all", b) && strstr(b.p, "\"done.duration\":1") && !strstr(b.p, "idle.sound"));
  b.clear();
  settingsSchema(b, true);
  CHECK(!b.overflow && b.len > 1000 && strstr(b.p, "\"done_duration\""));
  printf("SCHEMA is %u bytes\n", (unsigned)b.len);

  // stored format: round trip, CRC, clamp, 0.3.0 migration
  uint8_t raw[SETTINGS_STORE_SIZE] = {};
  settingsEncode(settings, raw);
  Settings back;
  CHECK(settingsDecode(raw, sizeof(raw), back) && !memcmp(&back, &settings, sizeof(back)));
  raw[10] ^= 1;
  CHECK(!settingsDecode(raw, sizeof(raw), back));
  uint8_t blank[SETTINGS_STORE_SIZE];
  memset(blank, 0xFF, sizeof(blank));
  CHECK(!settingsDecode(blank, sizeof(blank), back));

  Settings bad = settings;
  bad.brightness = 250;
  bad.state[ST_NEEDS].light = 99;
  bad.state[ST_NEEDS].sound = 200;
  bad.state[ST_DONE].duration = 0;
  bad.state[ST_IDLE].sound = SOUND_DING;
  bad.state[ST_WORKING].repeat = 0;
  bad.state[ST_WORKING].color = 0xFF000000;
  CHECK(settingsClamp(bad));
  CHECK(bad.brightness == 100 && bad.state[ST_NEEDS].light < LIGHT_COUNT && bad.state[ST_NEEDS].sound < SOUND_COUNT &&
        bad.state[ST_DONE].duration == 1 && bad.state[ST_IDLE].sound == SOUND_NONE && bad.state[ST_WORKING].repeat == 1 &&
        bad.state[ST_WORKING].color <= 0xFFFFFF);
  CHECK(!settingsClamp(bad));

  struct { uint32_t magic; uint8_t b, v; StateStyle st[ST_COUNT]; } v3;
  memset(&v3, 0, sizeof(v3));
  v3.magic = 0x484E0003;
  v3.b = 42;
  v3.v = 33;
  v3.st[ST_DONE] = {0x22E04A, LIGHT_PULSE, 100, 0, SOUND_BRIGHT, 100, 1};
  uint8_t raw3[SETTINGS_STORE_SIZE] = {};
  memcpy(raw3, &v3, sizeof(v3));
  CHECK(settingsDecode(raw3, sizeof(raw3), back) && back.brightness == 42 && back.volume == 33 &&
        back.state[ST_DONE].duration == 1 && back.state[ST_DONE].sound == SOUND_BRIGHT);
}

int main() {
  testLines();
  testSnapParse();
  testEngine();
  testSettings();
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
