#include "battery.h"

#include <Wire.h>
#include <esp_sleep.h>

#include "config.h"

// Registos do LC709203F (datasheet ON Semi, "LC709203F-D").
static const uint8_t REG_CELL_TEMP = 0x08;     // 0,1 K (modo I2C)
static const uint8_t REG_CELL_VOLTAGE = 0x09;  // mV
static const uint8_t REG_APA = 0x0B;           // ajuste ao tamanho da bateria
static const uint8_t REG_RSOC = 0x0D;          // %
static const uint8_t REG_PROFILE = 0x12;       // perfil da química (1 = Li-ion/LiPo 3,7 V / 4,2 V)
static const uint8_t REG_POWER_MODE = 0x15;    // 1 = a funcionar, 2 = a dormir
static const uint8_t REG_STATUS_BIT = 0x16;    // 0 = temperatura dada por I2C, 1 = termístor

static BatteryStatus st;
static BatteryEvent pendingEvent = BatteryEvent::None;
static uint32_t lastRead = 0;
static uint8_t lastBand = 255;  // 0: >20 %, 1: <=20 %, 2: <=10 %, 3: <=5 %
static bool wasFull = false;

static uint8_t crc8(const uint8_t *d, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  }
  return c;
}

static bool write16(uint8_t reg, uint16_t v) {
  uint8_t f[4] = {(uint8_t)(LC709203F_ADDR << 1), reg, (uint8_t)(v & 0xFF), (uint8_t)(v >> 8)};
  Wire.beginTransmission(LC709203F_ADDR);
  Wire.write(reg);
  Wire.write(f[2]);
  Wire.write(f[3]);
  Wire.write(crc8(f, 4));
  return Wire.endTransmission() == 0;
}

static bool read16(uint8_t reg, uint16_t &v) {
  Wire.beginTransmission(LC709203F_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)LC709203F_ADDR, (uint8_t)3) != 3) return false;
  uint8_t lo = Wire.read(), hi = Wire.read(), crc = Wire.read();
  uint8_t f[5] = {(uint8_t)(LC709203F_ADDR << 1), reg, (uint8_t)((LC709203F_ADDR << 1) | 1), lo, hi};
  if (crc8(f, 5) != crc) return false;
  v = (uint16_t)lo | ((uint16_t)hi << 8);
  return true;
}

static uint8_t band(uint8_t pct) {
  if (pct <= BATTERY_CRITICAL_PCT) return 3;
  if (pct <= BATTERY_VERY_LOW_PCT) return 2;
  if (pct <= BATTERY_LOW_PCT) return 1;
  return 0;
}

static void readNow() {
  bool powered = digitalRead(PIN_PWR_GOOD) == LOW;
  uint16_t mv = 0, pct = 0;
  bool ok = read16(REG_CELL_VOLTAGE, mv) && read16(REG_RSOC, pct);
  if (ok) {
    st.present = true;
    st.mv = mv;
    st.percent = pct > 100 ? 100 : (uint8_t)pct;
  }
  if (powered != st.powered) {
    pendingEvent = powered ? BatteryEvent::Plugged : BatteryEvent::Unplugged;
    log_i("carregador %s", powered ? "ligado" : "desligado");
  }
  st.powered = powered;
  st.charging = powered && st.present && st.percent < 100;
  if (!st.present) return;
  bool full = powered && st.percent >= 100;
  if (full && !wasFull && pendingEvent == BatteryEvent::None) pendingEvent = BatteryEvent::Full;
  wasFull = full;
  uint8_t b = band(st.percent);
  if (!powered && lastBand != 255 && b > lastBand && pendingEvent == BatteryEvent::None) {
    pendingEvent = b == 3 ? BatteryEvent::Critical : b == 2 ? BatteryEvent::VeryLow : BatteryEvent::Low;
  }
  lastBand = b;
}

bool batteryBegin() {
  pinMode(PIN_PWR_GOOD, INPUT);  // pull-up de 10k na placa (R24)
  bool ok = write16(REG_POWER_MODE, 0x0001) && write16(REG_APA, BATTERY_APA) && write16(REG_PROFILE, 0x0001) &&
            write16(REG_STATUS_BIT, 0x0000) && write16(REG_CELL_TEMP, 2982);  // 25,0 °C
  if (!ok) log_w("medidor de bateria (LC709203F) nao responde");
  readNow();
  pendingEvent = BatteryEvent::None;  // no arranque não há "ligou/desligou"
  if (st.present) {
    log_i("bateria %u%% (%u mV)%s", st.percent, st.mv, st.powered ? ", carregador ligado" : "");
    wasFull = st.powered && st.percent >= 100;
  }
  return st.present;
}

void batteryLoop() {
  if (millis() - lastRead < 10000) {
    // o cabo muda de estado de imediato (o medidor só a cada 10 s)
    bool powered = digitalRead(PIN_PWR_GOOD) == LOW;
    if (powered == st.powered) return;
  }
  lastRead = millis();
  readNow();
}

BatteryEvent batteryTakeEvent() {
  BatteryEvent e = pendingEvent;
  pendingEvent = BatteryEvent::None;
  return e;
}

const BatteryStatus &battery() { return st; }

bool batteryCritical() {
  if (!st.present || st.powered) return false;
  return st.percent <= BATTERY_SHUTDOWN_PCT || (st.mv > 0 && st.mv < BATTERY_SHUTDOWN_MV);
}

void batterySleepUntilCharger() {
  log_w("bateria vazia (%u%%, %u mV): a dormir ate ligar o carregador", st.percent, st.mv);
  Serial.flush();
  digitalWrite(PIN_AMP_SD, LOW);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_PWR_GOOD, 0);  // PWR_GOOD desce quando se liga o cabo
  esp_sleep_enable_timer_wakeup(3600ULL * 1000000ULL);         // e a cada hora volta a medir
  esp_deep_sleep_start();
}
