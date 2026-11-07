#include "leds.h"

#include <math.h>

#include "app_state.h"
#include "config.h"
#include "voice_frontend.h"

static const int N = LED_RING_COUNT + LED_BAR_COUNT;
static uint8_t px[N][3];
static rmt_data_t bits[N * 24];
static bool ready = false;
static uint32_t flashUntil = 0, volumeUntil = 0;
static uint8_t flashRgb[3];
static uint8_t shownVolume = 0;
static bool pending = false;

static void show() {
  if (!ready) return;
  // WS2812: ordem GRB; RMT a 10 MHz → 0 = 0,4/0,85 µs, 1 = 0,8/0,45 µs.
  int k = 0;
  for (int i = 0; i < N; i++) {
    uint8_t grb[3] = {px[i][1], px[i][0], px[i][2]};
    for (int c = 0; c < 3; c++) {
      for (int b = 7; b >= 0; b--) {
        bool one = grb[c] & (1 << b);
        bits[k].level0 = 1;
        bits[k].duration0 = one ? 8 : 4;
        bits[k].level1 = 0;
        bits[k].duration1 = one ? 4 : 8;
        k++;
      }
    }
  }
  rmtWrite(PIN_LED_RGB, bits, N * 24, RMT_WAIT_FOR_EVER);
}

static void fill(int from, int count, uint8_t r, uint8_t g, uint8_t b) {
  for (int i = from; i < from + count && i < N; i++) {
    px[i][0] = r;
    px[i][1] = g;
    px[i][2] = b;
  }
}

static void ring(uint8_t r, uint8_t g, uint8_t b) { fill(0, LED_RING_COUNT, r, g, b); }
static void bar(uint8_t r, uint8_t g, uint8_t b) { fill(LED_RING_COUNT, LED_BAR_COUNT, r, g, b); }

void ledsBegin() {
  pinMode(PIN_LED_STATUS, OUTPUT);
  digitalWrite(PIN_LED_STATUS, LOW);
  ready = rmtInit(PIN_LED_RGB, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
  memset(px, 0, sizeof(px));
  show();
}

void ledsFlash(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
  flashRgb[0] = r;
  flashRgb[1] = g;
  flashRgb[2] = b;
  flashUntil = millis() + ms;
}

void ledsVolume(uint8_t volume) {
  shownVolume = volume;
  volumeUntil = millis() + 1500;
}

void ledsSetPendingMessage(bool on) { pending = on; }

void ledsUpdate() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 33) return;
  last = now;
  digitalWrite(PIN_LED_STATUS, (app.wifiUp && app.wsReady) ? HIGH : LOW);

  const uint8_t OR = 255, OG = 110, OB = 20;  // laranja da marca
  float t = now / 1000.0f;
  memset(px, 0, sizeof(px));

  if (now < volumeUntil) {
    int lit = (shownVolume * LED_RING_COUNT + 50) / 100;
    for (int i = 0; i < LED_RING_COUNT; i++) {
      if (i < lit) fill(i, 1, 120, 120, 120);
    }
  } else if (now < flashUntil) {
    ring(flashRgb[0], flashRgb[1], flashRgb[2]);
  } else {
    switch (app.mode) {
      case Mode::Listening: {
        // a respirar, mais forte quando a pessoa fala
        float lvl = constrain((frontendLevelDb() + 60.0f) / 50.0f, 0.15f, 1.0f);
        ring(OR * lvl, OG * lvl, OB * lvl);
        break;
      }
      case Mode::Thinking: {
        int head = (int)(t * 18) % LED_RING_COUNT;
        for (int i = 0; i < 4; i++) {
          float f = 1.0f - i * 0.28f;
          fill((head - i + LED_RING_COUNT) % LED_RING_COUNT, 1, OR * f, OG * f, OB * f);
        }
        break;
      }
      case Mode::Speaking: {
        float f = 0.45f + 0.55f * (0.5f + 0.5f * sinf(t * 7.0f));
        ring(OR * f, OG * f, OB * f);
        break;
      }
      case Mode::FollowUp: {
        float f = 0.25f + 0.15f * sinf(t * 3.0f);
        ring(OR * f, OG * f, OB * f);
        break;
      }
      case Mode::PlayingMessage: {
        float f = 0.4f + 0.6f * (0.5f + 0.5f * sinf(t * 4.0f));
        ring(255 * f, 160 * f, 0);
        break;
      }
      case Mode::Muted:
        ring(90, 0, 0);
        break;
      case Mode::Setup:
        if ((now / 500) % 2) ring(0, 60, 200);
        break;
      case Mode::Error:
        if ((now / 300) % 2) ring(200, 0, 0);
        break;
      default:
        if (pending && (now / 700) % 2) ring(255, 160, 0);
        break;
    }
  }
  // A barra frontal acompanha o anel, mais suave; em espera fica com um brilho mínimo de presença.
  uint8_t br = px[0][0], bg = px[0][1], bb = px[0][2];
  if (br == 0 && bg == 0 && bb == 0 && app.mode == Mode::Idle) {
    bar(12, 5, 1);
  } else {
    bar(br / 2, bg / 2, bb / 2);
  }
  show();
}
