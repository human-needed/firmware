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

// A sound is up to MAX_NOTES decaying notes.
// The first seven are plain sine beeps. The melodies use the rich voice (soft attack, second
// and third harmonic) and long notes that overlap: on the small speaker that is what makes
// them sound like an instrument and not like a game console.
// The HN-08 speaker (XHXDZ 1510) is loudest from 1.3 to 3 kHz with a peak near 1.6 kHz, and
// 20 dB down at 1 kHz. So melodies either sit in that band, or sit lower and let their
// harmonics land in it.
#define MAX_NOTES 12
struct Note { uint16_t hz, startMs, lenMs; };
struct SoundDef { bool rich; Note notes[MAX_NOTES]; };
static const SoundDef SOUNDS[SOUND_COUNT] = {
  {},                                                           // none
  {false, {{1568, 0, 500}}},                                      // ding: one bright note
  {false, {{1319, 0, 350}, {1047, 120, 700}}},                    // chime: soft, falling
  {false, {{784, 0, 300}, {1175, 180, 700}}},                     // rising: two notes up
  {false, {{1047, 0, 250}, {1319, 110, 250}, {1568, 220, 600}}},  // arpeggio: three notes up
  {false, {{880, 0, 1200}}},                                      // bell: one low, long note
  {false, {{1760, 0, 90}, {1760, 160, 90}, {1760, 320, 90}}},     // beeps: three short beeps
  {false, {{523, 0, 120}, {1047, 70, 250}}},                      // bloop: quick low-high
  // bright: three notes up, then a strummed major chord
  {true, {{784, 0, 450}, {1047, 180, 450}, {1319, 360, 450}, {1047, 620, 1100}, {1319, 680, 1100}, {1568, 740, 1100}}},
  // clock: first phrase of the Westminster quarters (1793), stepping down to a long low note
  {true, {{1319, 0, 700}, {1175, 400, 700}, {1047, 800, 700}, {784, 1200, 1500}}},
  // doorbell: ding-dong
  {true, {{1319, 0, 900}, {1047, 450, 1400}}},
  // fanfare: three quick repeated notes, then a major chord
  {true, {{784, 0, 250}, {784, 150, 250}, {784, 300, 250}, {1047, 450, 1200}, {1319, 500, 1200}, {1568, 550, 1200}}},
  // ripple: quick run down the pentatonic scale onto an open fifth
  {true, {{2093, 0, 400}, {1760, 100, 400}, {1568, 200, 400}, {1319, 300, 400}, {1175, 400, 400}, {1047, 500, 1000}, {1568, 560, 1000}}},
  // call: a fourth up, twice, the second one held (made for "needs you")
  {true, {{1319, 0, 450}, {1760, 200, 700}, {1319, 800, 450}, {1760, 1000, 1100}}},
  // lullaby: a falling phrase that comes back up and closes on a strummed chord, 2.9 s
  {true, {{1319, 0, 450}, {1175, 200, 450}, {1047, 400, 450}, {784, 600, 450}, {880, 800, 450}, {1047, 1000, 600},
          {784, 1400, 1400}, {1047, 1460, 1400}, {1319, 1520, 1400}}},
  // harp: a fast sweep up the chord, then the chord rings
  {true, {{1047, 0, 600}, {1319, 70, 600}, {1568, 140, 600}, {2093, 210, 600}, {2637, 280, 600},
          {1047, 500, 1300}, {1319, 540, 1300}, {1568, 580, 1300}}},
};
static const uint16_t REPEAT_GAP_MS = 350;

#ifdef AUDIO_PWM_PIN
static PWMAudio out(AUDIO_PWM_PIN);  // mono
#else
static I2S out(OUTPUT);
#endif
#define SINE_BITS 10
static int16_t sine[1 << SINE_BITS];
static const SoundDef *playing = nullptr;
static uint32_t sampleIdx = 0, soundLen = 0;
static uint8_t repeatsLeft = 0;
static uint32_t phase[MAX_NOTES];
static int gain = 0;  // 0-256
#ifdef AUDIO_PWM_PIN
static int32_t pwmLevels = 1;
#endif
static int norm = 256 / 3;  // mix scale, 256 = 1.0

// Envelope of a note at time ms, 0-256 (the same curve nextSample() uses).
static int32_t noteEnv(const Note &note, int32_t ms) {
  int32_t k = ms - note.startMs;
  if (!note.hz || k < 0 || k >= note.lenMs) return 0;
  int32_t env = 256 - 256 * k / note.lenMs;
  return env * env / 256;
}

// Scale that takes the mixed notes to the output range.
// Default: a fixed 1/3, so three full notes can never clip.
// AUDIO_NORMALIZE: the sound's loudest moment lands exactly on full scale. Notes only decay,
// so that moment is at one of the note starts.
static int soundNorm(const SoundDef *sound) {
  int32_t peak = 0;
  for (int i = 0; i < MAX_NOTES; i++) {
    if (!sound->notes[i].hz) continue;
    int32_t sum = 0;
    for (int j = 0; j < MAX_NOTES; j++) sum += noteEnv(sound->notes[j], sound->notes[i].startMs);
    peak = max(peak, sum);
  }
#if AUDIO_NORMALIZE
  if (peak > 0) return 256L * 256 / peak;
#endif
  if (peak > 3 * 256) return 256L * 256 / peak;  // more than three notes at once: scale so they fit
  return 256 / 3;
}

void audioBegin() {
  for (int i = 0; i < (1 << SINE_BITS); i++) sine[i] = 32767 * sinf(i * 2 * PI / (1 << SINE_BITS));
#ifdef AMP_SD_PIN
  pinMode(AMP_SD_PIN, OUTPUT);
  digitalWrite(AMP_SD_PIN, LOW);  // amp on
#endif
#ifdef AUDIO_PWM_PIN
  // The PWM only has F_CPU / AUDIO_PWM_HZ levels (625 at 125 MHz), so it is fed one word per
  // PWM period, AUDIO_PWM_OSR times the audio rate, and audioTick() noise-shapes the samples.
  pwmLevels = rp2040.f_cpu() / AUDIO_PWM_HZ;
  out.setBuffers(8, 1024);  // 8192 words = 41 ms at 200 kHz
  out.begin((long)AUDIO_RATE * AUDIO_PWM_OSR, AUDIO_PWM_HZ);
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
  playing = &SOUNDS[sound];
  soundLen = 0;
  for (int n = 0; n < MAX_NOTES; n++) {
    const Note &note = playing->notes[n];
    if (note.hz) soundLen = max(soundLen, (uint32_t)note.startMs + note.lenMs);
  }
  soundLen = (soundLen + REPEAT_GAP_MS) * AUDIO_RATE / 1000;  // in samples, with the gap
  repeatsLeft = constrain(repeat, 1, 5);
  sampleIdx = 0;
  memset(phase, 0, sizeof(phase));
  gain = 256L * volume * settings.volume / 10000;
  norm = soundNorm(playing);
}

static int16_t nextSample() {
  if (sampleIdx >= soundLen) {  // one play finished: repeat or stop
    if (--repeatsLeft == 0) { playing = nullptr; return 0; }
    sampleIdx = 0;
    memset(phase, 0, sizeof(phase));
  }
  int32_t mix = 0;
  const int32_t attack = AUDIO_RATE / 250;  // 4 ms
  for (int n = 0; n < MAX_NOTES; n++) {
    const Note &note = playing->notes[n];
    if (!note.hz) continue;
    int32_t k = sampleIdx - note.startMs * AUDIO_RATE / 1000;
    int32_t len = note.lenMs * AUDIO_RATE / 1000;
    if (k < 0 || k >= len) continue;
    int32_t env = 256 - 256 * k / len;  // linear decay
    env = env * env / 256;              // ...shaped to sound like a bell
    phase[n] += (uint32_t)note.hz * (0xFFFFFFFFu / AUDIO_RATE);
    int32_t s = sine[phase[n] >> (32 - SINE_BITS)];
    if (playing->rich) {
      if (k < attack) env = env * k / attack;
      // Weights add up to 256, so the voice never exceeds a plain sine's peak.
      s = (s * 176 + sine[(phase[n] * 2) >> (32 - SINE_BITS)] * 53 + sine[(phase[n] * 3) >> (32 - SINE_BITS)] * 27) / 256;
    }
    mix += s * env / 256;
  }
  sampleIdx++;
  return constrain(mix * norm / 256 * gain / 256, -32767, 32767);
}

#ifdef AUDIO_PWM_PIN
// PWM output. Two things the plain "write each sample" loop gets wrong on this hardware:
// - Resolution: 625 PWM levels is about 9 bits, which sounds gritty on the quiet tail of a note.
//   Each audio sample is spread over AUDIO_PWM_OSR PWM periods (linear interpolation) and the
//   rounding error of every period is carried into the next one, which moves the error above
//   the audible band.
// - Clicks: when its buffer runs dry, PWMAudio writes a raw 0x8000 to the PWM, which is far
//   above the wrap value, so the pin jumps to 100 % duty. So this never lets it run dry: it
//   keeps writing the midpoint while nothing plays.
void audioTick() {
  static int32_t prev = 0, carry = 0;
  while (out.availableForWrite() >= 4 * AUDIO_PWM_OSR) {
    int32_t cur = playing ? nextSample() : 0;
    for (int i = 1; i <= AUDIO_PWM_OSR; i++) {
      int32_t v = prev + (cur - prev) * i / AUDIO_PWM_OSR;       // -32767..32767
      int32_t x = (v + 32768) * pwmLevels + carry;                // PWM level in 16.16 fixed point
      int32_t level = constrain(x >> 16, 0, pwmLevels - 1);
      carry = x - (level << 16);
      // PWMAudio maps a sample to a level as ((s + 32768) * levels) >> 16; this is its inverse.
      out.write((int16_t)(((level << 16) + pwmLevels - 1) / pwmLevels - 32768));
    }
    prev = cur;
  }
}
#else
void audioTick() {
  while (playing && out.availableForWrite() >= 4) {  // one stereo I2S frame
    int16_t s = nextSample();
    out.write16(s, s);
  }
}
#endif
#else  // no audio hardware yet

void audioBegin() {}
void audioTick() {}

void audioPlay(uint8_t sound, uint8_t volume, uint8_t repeat) {
  (void)volume;
  (void)repeat;
  startPlay(sound);
}

#endif
