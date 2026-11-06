#include "config.h"
#include "audio.h"
#include "settings.h"

static uint32_t mutedUntil = 0;
static uint32_t playCount = 0;
static uint8_t lastSound = SOUND_NONE;

void audioMute(uint32_t minutes) {
  mutedUntil = minutes ? millis() + minutes * 60000UL : 0;
}

bool audioMuted() {
  return mutedUntil && (int32_t)(mutedUntil - millis()) > 0;
}

uint32_t audioPlayCount() { return playCount; }
uint8_t audioLastSound() { return lastSound; }

// Returns false if nothing should play.
static bool startPlay(uint8_t sound) {
  if (sound == SOUND_NONE || sound >= SOUND_COUNT || audioMuted()) return false;
  playCount++;
  lastSound = sound;
  return true;
}

#if ENABLE_AUDIO
#ifdef AUDIO_PWM_PIN
#include <PWMAudio.h>
#else
#include <I2S.h>
#endif

// A sound is up to three decaying sine notes.
struct Note { uint16_t hz, startMs, lenMs; };
static const Note SOUNDS[SOUND_COUNT][3] = {
  {},                                                    // none
  {{1568, 0, 500}},                                      // ding: one bright note
  {{1319, 0, 350}, {1047, 120, 700}},                    // chime: soft, falling
  {{784, 0, 300}, {1175, 180, 700}},                     // rising: two notes up
  {{1047, 0, 250}, {1319, 110, 250}, {1568, 220, 600}},  // arpeggio: three notes up
  {{880, 0, 1200}},                                      // bell: one low, long note
  {{1760, 0, 90}, {1760, 160, 90}, {1760, 320, 90}},     // beeps: three short beeps
  {{523, 0, 120}, {1047, 70, 250}},                      // bloop: quick low-high
};
static const uint16_t REPEAT_GAP_MS = 350;

#ifdef AUDIO_PWM_PIN
static PWMAudio out(AUDIO_PWM_PIN);  // mono
#else
static I2S out(OUTPUT);
#endif
static int16_t sine[256];
static const Note *playing = nullptr;
static uint32_t sampleIdx = 0, soundLen = 0;
static uint8_t repeatsLeft = 0;
static uint32_t phase[3];
static int gain = 0;  // 0-256

void audioBegin() {
  for (int i = 0; i < 256; i++) sine[i] = 32767 * sinf(i * 2 * PI / 256);
#ifdef AMP_SD_PIN
  pinMode(AMP_SD_PIN, OUTPUT);
  digitalWrite(AMP_SD_PIN, LOW);  // amp on
#endif
#ifdef AUDIO_PWM_PIN
  out.begin(AUDIO_RATE, AUDIO_PWM_HZ);
#else
#if defined(I2S_LRCLK_PIN) && I2S_LRCLK_PIN == I2S_BCLK_PIN - 1
  out.swapClocks();             // LRCLK sits just below BCLK...
  out.setBCLK(I2S_LRCLK_PIN);  // ...and setBCLK takes the lower of the two pins
#else
  out.setBCLK(I2S_BCLK_PIN);   // LRCLK is BCLK + 1
#endif
  out.setDATA(I2S_DATA_PIN);
  out.setBitsPerSample(16);
  out.begin(AUDIO_RATE);
#endif
}

void audioPlay(uint8_t sound, uint8_t volume, uint8_t repeat) {
  if (!startPlay(sound)) return;
  playing = SOUNDS[sound];
  soundLen = 0;
  for (int n = 0; n < 3; n++) {
    if (playing[n].hz) soundLen = max(soundLen, (uint32_t)playing[n].startMs + playing[n].lenMs);
  }
  soundLen = (soundLen + REPEAT_GAP_MS) * AUDIO_RATE / 1000;  // in samples, with the gap
  repeatsLeft = constrain(repeat, 1, 5);
  sampleIdx = 0;
  memset(phase, 0, sizeof(phase));
  gain = 256L * volume * settings.volume / 10000;
}

static int16_t nextSample() {
  if (sampleIdx >= soundLen) {  // one play finished: repeat or stop
    if (--repeatsLeft == 0) { playing = nullptr; return 0; }
    sampleIdx = 0;
    memset(phase, 0, sizeof(phase));
  }
  int32_t mix = 0;
  for (int n = 0; n < 3; n++) {
    const Note &note = playing[n];
    if (!note.hz) continue;
    int32_t k = sampleIdx - note.startMs * AUDIO_RATE / 1000;
    int32_t len = note.lenMs * AUDIO_RATE / 1000;
    if (k < 0 || k >= len) continue;
    int32_t env = 256 - 256 * k / len;  // linear decay
    env = env * env / 256;              // ...shaped to sound like a bell
    phase[n] += (uint32_t)note.hz * (0xFFFFFFFFu / AUDIO_RATE);
    mix += sine[phase[n] >> 24] * env / 256 / 3;
  }
  sampleIdx++;
  return mix * gain / 256;
}

void audioTick() {
  while (playing && out.availableForWrite() >= 4) {  // one stereo I2S frame or one PWM word
    int16_t s = nextSample();
#ifdef AUDIO_PWM_PIN
    out.write(s);
#else
    out.write16(s, s);
#endif
  }
}

#else  // no audio hardware yet

void audioBegin() {}
void audioTick() {}

void audioPlay(uint8_t sound, uint8_t volume, uint8_t repeat) {
  (void)volume;
  (void)repeat;
  startPlay(sound);
}

#endif
