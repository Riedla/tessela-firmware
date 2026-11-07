#include "storage.h"

#include <Preferences.h>
#include <SD_MMC.h>

#include "app_state.h"
#include "config.h"

static Preferences prefs;

void settingsLoad() {
  prefs.begin("tessela", true);
  Settings &s = app.settings;
  s.ssid = prefs.getString("ssid", "");
  s.pass = prefs.getString("pass", "");
  s.server = prefs.getString("server", "");
  s.token = prefs.getString("token", "");
  s.deviceId = prefs.getString("deviceId", "");
  s.realtimeUrl = prefs.getString("rtUrl", "");
  s.conversationId = prefs.getString("conv", "");
  s.timezone = prefs.getString("tz", "Africa/Maputo");
  s.volume = prefs.getUChar("volume", VOLUME_DEFAULT);
  s.scheduleEnabled = prefs.getBool("schedOn", true);
  s.muted = prefs.getBool("muted", false);
  prefs.end();
}

void settingsSave() {
  prefs.begin("tessela", false);
  const Settings &s = app.settings;
  prefs.putString("ssid", s.ssid);
  prefs.putString("pass", s.pass);
  prefs.putString("server", s.server);
  prefs.putString("token", s.token);
  prefs.putString("deviceId", s.deviceId);
  prefs.putString("rtUrl", s.realtimeUrl);
  prefs.putString("conv", s.conversationId);
  prefs.putString("tz", s.timezone);
  prefs.putUChar("volume", s.volume);
  prefs.putBool("schedOn", s.scheduleEnabled);
  prefs.putBool("muted", s.muted);
  prefs.end();
}

void settingsForgetPairing() {
  app.settings.token = "";
  app.settings.deviceId = "";
  app.settings.realtimeUrl = "";
  app.settings.conversationId = "";
  settingsSave();
}

bool sdPresent() { return digitalRead(PIN_SD_DET) == LOW; }

bool sdBegin() {
  pinMode(PIN_SD_DET, INPUT_PULLUP);
  if (!sdPresent()) {
    log_w("sem cartao microSD");
    return false;
  }
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
  if (!SD_MMC.begin("/sdcard", false /* 4 bits */, false, 20000)) {
    log_e("cartao microSD nao montou");
    return false;
  }
  if (!SD_MMC.exists(SD_ROOT)) SD_MMC.mkdir(SD_ROOT);
  String audio = String(SD_ROOT) + "/audio";
  if (!SD_MMC.exists(audio)) SD_MMC.mkdir(audio);
  log_i("cartao microSD: %llu MB livres", sdFreeBytes() / (1024ULL * 1024ULL));
  return true;
}

fs::FS &sd() { return SD_MMC; }

String sdPath(const char *rel) { return String(SD_ROOT) + "/" + rel; }

bool sdWriteAtomic(const String &path, const uint8_t *data, size_t len) {
  String tmp = path + ".tmp";
  File f = SD_MMC.open(tmp, FILE_WRITE);
  if (!f) return false;
  size_t w = f.write(data, len);
  f.close();
  if (w != len) {
    SD_MMC.remove(tmp);
    return false;
  }
  SD_MMC.remove(path);
  return SD_MMC.rename(tmp, path);
}

String sdReadText(const String &path, size_t maxLen) {
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return "";
  String out;
  out.reserve(min((size_t)f.size(), maxLen));
  while (f.available() && out.length() < maxLen) out += (char)f.read();
  f.close();
  return out;
}

uint64_t sdFreeBytes() { return SD_MMC.totalBytes() - SD_MMC.usedBytes(); }

String readCaFromSd() {
  if (!app.sdOk) return "";
  return sdReadText(sdPath("ca.pem"), 8192);
}
