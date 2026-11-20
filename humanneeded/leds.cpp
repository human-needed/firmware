// SPDX-License-Identifier: FSL-1.1-ALv2
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "core.h"
#include "leds.h"
#include "settings.h"

struct RGB { float r, g, b; };

#if LED_16BIT
// WS2816: 6 bytes per LED (G, R, B, each 16 bits, high byte first) on the same 800 kHz wire.
// Driven as twice as many 3-byte pixels whose bytes are written raw (see show()).
static Adafruit_NeoPixel strip(LED_COUNT * 2, LED_PIN, NEO_RGB + NEO_KHZ800);
#else
static Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
#endif
static RGB shown[LED_COUNT];  // what is on the LEDs now (linear light, 0-255 per channel, before the cap)
[[maybe_unused]] static RGB ditherErr[LED_COUNT];  // rounding error carried to the next frame (temporal dithering)
static float capK = 1;            // current-cap scale, eased so a sound starting doesn't jump the light
static uint32_t lastShowMs = 0;
static RGB from[LED_COUNT];   // snapshot at the last state change, for cross-fading
static int lastState = -1;
static uint32_t lastSince = 0;
static uint32_t changeMs = 0;
static uint32_t helloStart = 0;
static bool helloActive = false;
static uint32_t selftestStart = 0;
static bool selftestActive = false;

void ledsBegin() {
  strip.begin();
  strip.clear();
  strip.show();
}

void ledsHello() {
  helloStart = millis();
  helloActive = true;
}

void ledsSelftest() {
  selftestStart = millis();
  selftestActive = true;
}

bool ledsSelftestActive() { return selftestActive; }

// Smoothstep: eases in and out, so fades and edges have no visible corner.
static float ease(float x) {
  x = constrain(x, 0.0f, 1.0f);
  return x * x * (3 - 2 * x);
}

// 0..1 wave over one animation cycle: 1 at the start and end, 0 halfway. Every cycle that varies
// brightness starts and ends at full, so the fades run to and from full.
static float wave(uint32_t t) {
  return 0.5f + 0.5f * cosf(2 * PI * (t % ANIM_MS) / ANIM_MS);
}

// 1 between on and off (ms), 0 outside, with eased edges `edge` ms long instead of a hard step.
static float square(uint32_t p, uint32_t on, uint32_t off, uint32_t edge) {
  if (p < on || p >= off + edge) return 0;
  if (p < on + edge) return ease((p - on) / (float)edge);
  if (p < off) return 1;
  return 1 - ease((p - off) / (float)edge);
}

// Light level (0..1) for pixel i, t ms after the state began. Every animation repeats exactly
// every ANIM_MS, and durations are whole seconds, so a state always ends on a complete cycle.
static float lightLevel(uint8_t light, uint32_t t, int i) {
  if (light == LIGHT_SPIN && LED_COUNT < 3) light = LIGHT_BREATHE;  // no spin on a single pixel
  switch (light) {
    case LIGHT_OFF:     return 0;
    case LIGHT_BREATHE: return 0.35f + 0.65f * wave(t);  // floor keeps the hue true
    case LIGHT_BLINK:   return 1 - square(t % ANIM_MS, 220, 720, 60);  // off half a second, soft edges
    case LIGHT_HEARTBEAT: {
      // Two quick beats, then a rest. The first beat spans the join between cycles.
      uint32_t p = (t + 66) % ANIM_MS;
      return 0.15f + 0.85f * max(square(p, 0, 99, 33), square(p, 210, 309, 33));
    }
    case LIGHT_FLICKER: {
      // Candle-like: a few out-of-step waves, offset per pixel (whole numbers of them per cycle).
      uint32_t p = t % ANIM_MS;
      float a = 2 * PI * p / ANIM_MS;
      float f = sinf(2 * a + i) * sinf(a + 2 * i) + 0.5f * sinf(5 * a + 3 * i);
      // Eased in from full at the start of the cycle and back to full at the end.
      float depth = 1 - constrain(0.7f + 0.25f * f, 0.3f, 1.0f);
      return 1 - depth * ease(min(p, ANIM_MS - p) / 120.0f);
    }
    case LIGHT_SPIN: {
      // A comet tail chasing around the ring once per cycle.
      float head = (t % ANIM_MS) * LED_COUNT / (float)ANIM_MS;
      float behind = fmodf(head - i + LED_COUNT, LED_COUNT);
      return 0.1f + 0.9f * max(0.0f, 1 - behind / (LED_COUNT * 0.5f));
    }
    default: return 1;  // solid
  }
}

// Hex colours are sRGB (made for screens); LEDs are linear. Converting each channel
// keeps colours saturated instead of washing out towards white.
static float toLinear(uint8_t channel) {
  return 255 * powf(channel / 255.0f, 2.2f);
}

static RGB scaled(uint32_t color, float level) {
  level = level * level;  // rough gamma so fades look even to the eye
  return {toLinear(color >> 16) * level, toLinear(color >> 8) * level, toLinear(color) * level};
}

// The state's colour in linear light at full level, with its own brightness. Once per frame.
// The state's own brightness dims linearly, after gamma: dimming before it leaves
// too few steps at low levels and the hue drifts (idle turned pink).
static RGB stateBase(State state) {
  const StateStyle &st = settings.state[state];
  float k = st.brightness / 100.0f;
  return {toLinear(st.color >> 16) * k, toLinear(st.color >> 8) * k, toLinear(st.color) * k};
}

static RGB target(const RGB &base, uint8_t light, uint32_t t, int i) {
  float level = settings.brightness / 100.0f * lightLevel(light, t, i);
  level *= level;  // rough gamma so fades look even to the eye
  return {base.r * level, base.g * level, base.b * level};
}

static RGB helloColor(uint32_t elapsed, int i) {
  // Rainbow sweep that fades out (a full-ring spin once the ring is fitted).
  uint16_t hue = i * 65536L / LED_COUNT + elapsed * 65536L / 750;
  uint32_t c = Adafruit_NeoPixel::ColorHSV(hue);
  return scaled(c, settings.brightness / 100.0f * (1 - (float)elapsed / HELLO_MS));
}

static float fadeChannel(float a, float b, float mix) {
  float v = sqrtf(a) + (sqrtf(b) - sqrtf(a)) * mix;
  return v * v;
}

// Writes the frame, scaled down if it would draw more than the current budget.
static float lastMa = 0;
float ledsLastMa() { return lastMa; }

// One channel to 8 bits. A level between two steps alternates between them from frame to frame
// (the error is carried over), so slow fades at low brightness don't move in visible steps.
// Below one step there is no dithering: it would flicker.
[[maybe_unused]] static uint8_t dither(float v, float &err) {
  if (v < 1) { err = 0; return v >= 0.5f ? 1 : 0; }
  float want = v + err;
  int out = constrain((int)(want + 0.5f), 0, 255);
  err = constrain(want - out, -0.5f, 0.5f);
  return out;
}

#if LED_16BIT
// One channel (linear, 0-255 float) to 16 bits. 257 x finer than 8 bits: no dithering needed.
static uint16_t to16(float v) {
  if (v <= 0) return 0;
#if defined(LED_OUT_GAMMA)
  if (LED_OUT_GAMMA != 1.0f) v = 255 * powf(v / 255, 1 / LED_OUT_GAMMA);
#endif
  float o = v * 257 + 0.5f;
  return o >= 65535 ? 65535 : (uint16_t)o;
}

static void put16(uint8_t *p, uint16_t v) {
  p[0] = v >> 8;
  p[1] = v & 0xFF;
}
#endif

static void show(bool sound) {
  uint32_t now = millis();
  float dt = (float)(now - lastShowMs);
  lastShowMs = now;
  float sum = 0;
  for (int i = 0; i < LED_COUNT; i++) sum += shown[i].r + shown[i].g + shown[i].b;
  float ma = sum / 255 * LED_MA_PER_CHANNEL;
  float budget = sound ? LED_BUDGET_SOUND_MA : LED_BUDGET_MA;
  float k = ma > budget ? budget / ma : 1;
  // Ease towards the cap: down within ~40 ms, back up over ~250 ms, so a sound starting or
  // stopping dims and restores the ring smoothly instead of in one step.
  capK += (k - capK) * min(1.0f, dt / (k < capK ? CAP_DOWN_MS : CAP_UP_MS));
  float kk = min(capK, 1.0f);
  if (ma * kk > budget * 1.25f) kk = budget * 1.25f / ma;  // hard limit, even while easing
  lastMa = ma * kk;
#if LED_16BIT
  uint8_t *px = strip.getPixels();  // raw bytes, sent as they are (no setBrightness)
  for (int i = 0; i < LED_COUNT; i++) {
    put16(px + 6 * i, to16(shown[i].g * kk));
    put16(px + 6 * i + 2, to16(shown[i].r * kk));
    put16(px + 6 * i + 4, to16(shown[i].b * kk));
  }
#else
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, dither(shown[i].r * kk, ditherErr[i].r), dither(shown[i].g * kk, ditherErr[i].g),
                        dither(shown[i].b * kk, ditherErr[i].b));
  }
#endif
  strip.show();
}

void ledsRender(State state, uint32_t since, bool dark, bool sound) {
  uint32_t t = millis();

  if (dark) {  // USB suspend: fully dark; fade back in on resume
    memset(shown, 0, sizeof(shown));
    lastState = -1;
    helloActive = selftestActive = false;
    show(sound);
    return;
  }

  if (selftestActive) {  // factory test: each LED red, green, blue in turn, full level
    uint32_t step = (t - selftestStart) / SELFTEST_STEP_MS;
    if (step >= 3UL * LED_COUNT) {
      selftestActive = false;
      lastState = -1;
    } else {
      memset(shown, 0, sizeof(shown));
      RGB &p = shown[step / 3];
      (step % 3 == 0 ? p.r : step % 3 == 1 ? p.g : p.b) = 255;
      show(sound);
      return;
    }
  }

  if (helloActive && t - helloStart >= HELLO_MS) {
    helloActive = false;
    lastState = -1;  // fade in from the end of the hello
  }
  // A new state, or the same one starting again (a new agent needs you, a second preview).
  if (!helloActive && (state != lastState || since != lastSince)) {
    memcpy(from, shown, sizeof(from));
    changeMs = t;
    lastState = state;
    lastSince = since;
  }

  // Fade (LIGHT_FADE_MS) from the last frame shown to this light's first frame, eased and mixed
  // on a perceptual scale (square root of light) so it neither jumps at the start nor crawls at
  // the end. The effect starts after it and runs for the state's duration: whole cycles.
  RGB base = stateBase(state);
  uint8_t light = settings.state[state].light;
  float mix = ease((t - changeMs) / (float)LIGHT_FADE_MS);
  uint32_t elapsed = t - (since ? since : changeMs);
  uint32_t effect = elapsed > LIGHT_FADE_MS ? elapsed - LIGHT_FADE_MS : 0;
  for (int i = 0; i < LED_COUNT; i++) {
    if (helloActive) {
      shown[i] = helloColor(t - helloStart, i);
    } else {
      RGB to = target(base, light, effect, i);
      shown[i] = mix >= 1 ? to : RGB{fadeChannel(from[i].r, to.r, mix), fadeChannel(from[i].g, to.g, mix),
                                     fadeChannel(from[i].b, to.b, mix)};
    }
  }
  show(sound);
}
