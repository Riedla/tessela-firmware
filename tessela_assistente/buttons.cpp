#include "buttons.h"

#include "config.h"

struct Btn {
  uint8_t pin;
  bool stable = false;   // premido?
  bool lastRaw = false;
  uint32_t changedAt = 0;
  uint32_t pressedAt = 0;
  bool longFired = false;
};

static Btn btns[3] = {{PIN_BTN_VOLUP}, {PIN_BTN_VOLDN}, {PIN_BTN_MUTE}};

void buttonsBegin() {
  for (auto &b : btns) pinMode(b.pin, INPUT_PULLUP);
}

ButtonEvent buttonsPoll() {
  uint32_t now = millis();
  for (int i = 0; i < 3; i++) {
    Btn &b = btns[i];
    bool raw = digitalRead(b.pin) == LOW;
    if (raw != b.lastRaw) {
      b.lastRaw = raw;
      b.changedAt = now;
    }
    if (now - b.changedAt < BUTTON_DEBOUNCE_MS || raw == b.stable) {
      // segurar o MUTE: evento longo uma vez
      if (i == 2 && b.stable && !b.longFired && now - b.pressedAt >= BUTTON_LONG_MS) {
        b.longFired = true;
        return ButtonEvent::MuteLong;
      }
      continue;
    }
    b.stable = raw;
    if (raw) {
      b.pressedAt = now;
      b.longFired = false;
      if ((i == 0 && btns[1].stable) || (i == 1 && btns[0].stable)) return ButtonEvent::Battery;
      if (i == 0) return ButtonEvent::VolUp;
      if (i == 1) return ButtonEvent::VolDown;
    } else if (i == 2 && !b.longFired) {
      return ButtonEvent::MuteShort;  // o MUTE actua ao largar (para distinguir do longo)
    }
  }
  return ButtonEvent::None;
}
