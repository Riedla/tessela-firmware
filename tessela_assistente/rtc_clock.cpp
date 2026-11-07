#include "rtc_clock.h"

#include <Wire.h>
#include <sys/time.h>

#include "app_state.h"
#include "config.h"

// IANA → POSIX TZ para os fusos mais prováveis. Outro nome: tenta-se o próprio texto como POSIX.
struct TzMap { const char *iana; const char *posix; };
static const TzMap TZS[] = {
  {"Africa/Maputo", "CAT-2"},
  {"Africa/Harare", "CAT-2"},
  {"Africa/Lusaka", "CAT-2"},
  {"Africa/Blantyre", "CAT-2"},
  {"Africa/Johannesburg", "SAST-2"},
  {"Africa/Luanda", "WAT-1"},
  {"Africa/Lagos", "WAT-1"},
  {"Africa/Nairobi", "EAT-3"},
  {"Africa/Dar_es_Salaam", "EAT-3"},
  {"Atlantic/Cape_Verde", "<-01>1"},
  {"Africa/Bissau", "GMT0"},
  {"Africa/Sao_Tome", "GMT0"},
  {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
  {"Atlantic/Madeira", "WET0WEST,M3.5.0/1,M10.5.0"},
  {"Atlantic/Azores", "<-01>1<+00>,M3.5.0/0,M10.5.0/1"},
  {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
  {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"America/Sao_Paulo", "<-03>3"},
  {"Asia/Dili", "<+09>-9"},
  {"Asia/Macau", "CST-8"},
  {"UTC", "UTC0"},
  {"Etc/UTC", "UTC0"},
};

static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static bool rtcRead(struct tm &t) {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(DS3231_ADDR, 7) != 7) return false;
  uint8_t s = Wire.read(), m = Wire.read(), h = Wire.read();
  Wire.read();  // dia da semana (não usado)
  uint8_t d = Wire.read(), mo = Wire.read(), y = Wire.read();
  memset(&t, 0, sizeof(t));
  t.tm_sec = bcd2dec(s & 0x7F);
  t.tm_min = bcd2dec(m & 0x7F);
  t.tm_hour = bcd2dec(h & 0x3F);
  t.tm_mday = bcd2dec(d & 0x3F);
  t.tm_mon = bcd2dec(mo & 0x1F) - 1;
  t.tm_year = bcd2dec(y) + 100 + ((mo & 0x80) ? 100 : 0);
  return t.tm_year >= 124;  // >= 2024: o RTC já foi acertado alguma vez
}

static bool rtcWrite(const struct tm &t) {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(0x00);
  Wire.write(dec2bcd(t.tm_sec));
  Wire.write(dec2bcd(t.tm_min));
  Wire.write(dec2bcd(t.tm_hour));  // 24 h
  Wire.write(dec2bcd(t.tm_wday + 1));
  Wire.write(dec2bcd(t.tm_mday));
  Wire.write(dec2bcd(t.tm_mon + 1));
  Wire.write(dec2bcd(t.tm_year % 100));
  return Wire.endTransmission() == 0;
}

static time_t utcMktime(struct tm *t) {
  // mktime() usa o fuso actual: calcula-se em UTC com o TZ temporariamente a zero.
  char *old = getenv("TZ");
  String saved = old ? String(old) : String();
  setenv("TZ", "UTC0", 1);
  tzset();
  time_t r = mktime(t);
  if (saved.length()) setenv("TZ", saved.c_str(), 1); else unsetenv("TZ");
  tzset();
  return r;
}

bool clockBegin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  pinMode(PIN_RTC_INT, INPUT_PULLUP);
  clockSetTimezone(app.settings.timezone);
  struct tm t;
  if (rtcRead(t)) {
    app.rtcOk = true;
    struct timeval tv = {utcMktime(&t), 0};
    settimeofday(&tv, nullptr);
    app.timeValid = true;
    log_i("hora do RTC: %s", clockIso().c_str());
  } else {
    app.rtcOk = false;
    log_w("RTC sem hora valida (pilha CR2032?): espera pelo NTP");
  }
  return app.rtcOk;
}

void clockSetTimezone(const String &iana) {
  const char *posix = nullptr;
  for (auto &z : TZS) {
    if (iana == z.iana) { posix = z.posix; break; }
  }
  String tz = posix ? String(posix) : iana;
  if (!tz.length()) tz = "CAT-2";
  setenv("TZ", tz.c_str(), 1);
  tzset();
}

bool clockSyncNtp(uint32_t timeoutMs) {
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  clockSetTimezone(app.settings.timezone);  // configTime repõe o TZ
  uint32_t t0 = millis();
  time_t now = 0;
  while (millis() - t0 < timeoutMs) {
    time(&now);
    if (now > 1700000000) break;
    delay(200);
  }
  if (now <= 1700000000) return false;
  struct tm utc;
  gmtime_r(&now, &utc);
  if (rtcWrite(utc)) app.rtcOk = true;
  app.timeValid = true;
  log_i("NTP: %s (local)", clockIso().c_str());
  return true;
}

bool clockValid() {
  time_t now;
  time(&now);
  return app.timeValid && now > 1700000000;
}

bool clockLocal(struct tm &out) {
  if (!clockValid()) return false;
  time_t now;
  time(&now);
  localtime_r(&now, &out);
  return true;
}

String clockIso() {
  struct tm t;
  if (!clockLocal(t)) return "sem-hora";
  char b[24];
  strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &t);
  return String(b);
}
