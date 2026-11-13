#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "leds.h"
#include "settings.h"

struct RGB { float r, g, b; };

static Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
static RGB shown[LED_COUNT];  // what is on the LEDs now
static RGB from[LED_COUNT];   // snapshot at the last state change, for cross-fading
static int lastState = -1;
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

// 0..1 wave with the given period: 1 at t=0 (so a new state starts bright), 0 at half period.
static float wave(uint32_t t, uint32_t periodMs) {
  return 0.5f + 0.5f * cosf(2 * PI * (t % periodMs) / periodMs);
}

// Light level (0..1) for pixel i, t ms after the state began.
static float lightLevel(uint8_t light, uint32_t t, int i) {
  if (light == LIGHT_SPIN && LED_COUNT < 3) light = LIGHT_BREATHE;  // no spin on a single pixel
  switch (light) {
    case LIGHT_OFF:     return 0;
    case LIGHT_BREATHE: return 0.35f + 0.65f * wave(t, 4000);  // floor keeps the hue true
    case LIGHT_PULSE:   return 0.2f + 0.8f * wave(t, 1400);
    case LIGHT_BLINK:   return (t % 1000) < 500 ? 1 : 0;
    case LIGHT_HEARTBEAT: {
      // Two quick beats, then a rest, every 1.2 s.
      uint32_t p = t % 1200;
      return (p < 120 || (p >= 250 && p < 370)) ? 1 : 0.15f;
    }
    case LIGHT_FLICKER: {
      // Candle-like: a few out-of-step waves, offset per pixel.
      float f = sinf(t * 0.013f + i) * sinf(t * 0.0071f + 2 * i) + 0.5f * sinf(t * 0.031f + 3 * i);
      return constrain(0.7f + 0.25f * f, 0.3f, 1.0f);
    }
    case LIGHT_SPIN: {
      // A comet tail chasing around the ring once every 1.5 s.
      float head = (t % 1500) * LED_COUNT / 1500.0f;
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

static RGB target(State state, uint32_t t, int i) {
  const StateStyle &st = settings.state[state];
  RGB c = scaled(st.color, settings.brightness / 100.0f * lightLevel(st.light, t, i));
  // The state's own brightness dims linearly, after gamma: dimming before it leaves
  // too few steps at low levels and the hue drifts (idle turned pink).
  float k = st.brightness / 100.0f;
  return {c.r * k, c.g * k, c.b * k};
}

static RGB helloColor(uint32_t elapsed, int i) {
  // Rainbow sweep that fades out (a full-ring spin once the ring is fitted).
  uint16_t hue = i * 65536L / LED_COUNT + elapsed * 65536L / 750;
  uint32_t c = Adafruit_NeoPixel::ColorHSV(hue);
  return scaled(c, settings.brightness / 100.0f * (1 - (float)elapsed / HELLO_MS));
}

// Writes the frame, scaled down if it would draw more than the current budget.
static float lastMa = 0;
float ledsLastMa() { return lastMa; }

static void show(bool sound) {
  float sum = 0;
  for (int i = 0; i < LED_COUNT; i++) sum += shown[i].r + shown[i].g + shown[i].b;
  float ma = sum / 255 * LED_MA_PER_CHANNEL;
  float budget = sound ? LED_BUDGET_SOUND_MA : LED_BUDGET_MA;
  float k = ma > budget ? budget / ma : 1;
  lastMa = ma * k;
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, (uint8_t)(shown[i].r * k), (uint8_t)(shown[i].g * k), (uint8_t)(shown[i].b * k));
  }
  strip.show();
}

void ledsRender(State state, bool dark, bool sound) {
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
  if (!helloActive && state != lastState) {
    memcpy(from, shown, sizeof(from));
    changeMs = t;
    lastState = state;
  }

  float mix = min(1.0f, (t - changeMs) / (float)FADE_MS);
  for (int i = 0; i < LED_COUNT; i++) {
    if (helloActive) {
      shown[i] = helloColor(t - helloStart, i);
    } else {
      RGB to = target(state, t - changeMs, i);  // animations run from the state change
      shown[i] = {from[i].r + (to.r - from[i].r) * mix,
                  from[i].g + (to.g - from[i].g) * mix,
                  from[i].b + (to.b - from[i].b) * mix};
    }
  }
  show(sound);
}
