#include "api_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "app_state.h"
#include "audio_io.h"
#include "battery.h"
#include "config.h"
#include "rtc_clock.h"
#include "scheduler.h"
#include "storage.h"

static String caPem;

const char *apiResultName(ApiResult r) {
  switch (r) {
    case ApiResult::Ok: return "ok";
    case ApiResult::NotModified: return "sem_alteracoes";
    case ApiResult::Unauthorized: return "token_invalido";
    case ApiResult::NetworkError: return "rede";
    case ApiResult::ServerError: return "servidor";
    case ApiResult::BadResponse: return "resposta_invalida";
  }
  return "?";
}

// Cliente HTTP com TLS a verificar a CA (config.h ou /tessela/ca.pem); sem CA, cifra sem verificar.
struct Http {
  WiFiClientSecure tls;
  WiFiClient plain;
  HTTPClient http;

  bool begin(const String &url) {
    http.setReuse(false);
    http.setTimeout(15000);
    http.setConnectTimeout(8000);
    if (url.startsWith("https://")) {
      if (!caPem.length()) caPem = readCaFromSd();
      if (caPem.length()) {
        tls.setCACert(caPem.c_str());
      } else if (TESSELA_ROOT_CA) {
        tls.setCACert(TESSELA_ROOT_CA);
      } else {
        log_w("TLS sem CA configurada: a ligacao NAO verifica o servidor (poe /tessela/ca.pem no cartao)");
        tls.setInsecure();
      }
      return http.begin(tls, url);
    }
    return http.begin(plain, url);
  }
  void auth() {
    if (app.settings.token.length()) http.addHeader("Authorization", "Bearer " + app.settings.token);
  }
};

static String apiUrl(const char *path) {
  String base = app.settings.server;
  while (base.endsWith("/")) base.remove(base.length() - 1);
  return base + path;
}

static String macId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(b);
}

ApiResult apiPair(const String &code, String &message) {
  if (!app.settings.server.length()) {
    message = "servidor por configurar";
    return ApiResult::BadResponse;
  }
  Http h;
  if (!h.begin(apiUrl("/v1/hardware/pair"))) return ApiResult::NetworkError;
  h.http.addHeader("Content-Type", "application/json");
  JsonDocument req;
  req["code"] = code;
  req["hardwareId"] = macId();
  req["model"] = DEVICE_MODEL;
  req["firmwareVersion"] = FIRMWARE_VERSION;
  String body;
  serializeJson(req, body);
  int status = h.http.POST(body);
  String resp = h.http.getString();
  h.http.end();
  if (status <= 0) {
    message = "sem ligacao ao servidor";
    return ApiResult::NetworkError;
  }
  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    message = "resposta invalida";
    return ApiResult::BadResponse;
  }
  if (status != 201) {
    String fallback = "erro " + String(status);
    message = doc["error"]["message"] | fallback;
    return status == 400 ? ApiResult::Unauthorized : ApiResult::ServerError;
  }
  app.settings.token = doc["token"] | "";
  app.settings.deviceId = doc["deviceId"] | "";
  app.settings.realtimeUrl = doc["realtimeUrl"] | "";
  app.settings.conversationId = "";
  if (doc["settings"]["timezone"].is<const char *>()) app.settings.timezone = doc["settings"]["timezone"].as<String>();
  settingsSave();
  message = String("emparelhado como ") + (doc["name"] | "Assistente");
  return app.settings.token.length() ? ApiResult::Ok : ApiResult::BadResponse;
}

ApiResult apiCheckToken() {
  Http h;
  if (!h.begin(apiUrl("/v1/hardware/me"))) return ApiResult::NetworkError;
  h.auth();
  int status = h.http.GET();
  String resp = status == 200 ? h.http.getString() : String();
  h.http.end();
  if (status == 401 || status == 404) return ApiResult::Unauthorized;
  if (status <= 0) return ApiResult::NetworkError;
  if (status != 200) return ApiResult::ServerError;
  JsonDocument doc;
  if (!deserializeJson(doc, resp)) {
    String url = doc["realtimeUrl"] | "";
    if (url.length() && url != app.settings.realtimeUrl) {
      app.settings.realtimeUrl = url;
      settingsSave();
    }
    String tz = doc["settings"]["timezone"] | (doc["user"]["timezone"] | "");
    if (tz.length() && tz != app.settings.timezone) {
      app.settings.timezone = tz;
      clockSetTimezone(tz);
      settingsSave();
    }
  }
  return ApiResult::Ok;
}

// Estado do dispositivo (§15): bateria e Wi-Fi. A conta vê-o na app (device.power).
ApiResult apiSendStatus() {
  Http h;
  if (!h.begin(apiUrl("/v1/hardware/me/status"))) return ApiResult::NetworkError;
  h.auth();
  h.http.addHeader("Content-Type", "application/json");
  JsonDocument d;
  const BatteryStatus &b = battery();
  if (b.present) {
    JsonObject o = d["battery"].to<JsonObject>();
    o["percent"] = b.percent;
    o["voltageMv"] = b.mv;
    o["charging"] = b.charging;
    o["powered"] = b.powered;
  }
  if (WiFi.isConnected()) d["wifiRssi"] = constrain((int)WiFi.RSSI(), -127, 0);
  d["uptimeSeconds"] = (uint32_t)(millis() / 1000);
  String body;
  serializeJson(d, body);
  int status = h.http.PUT(body);
  h.http.end();
  if (status == 401 || status == 404) return ApiResult::Unauthorized;
  if (status <= 0) return ApiResult::NetworkError;
  return status == 204 || status == 200 ? ApiResult::Ok : ApiResult::ServerError;
}

// Descarrega um áudio para o cartão (ficheiro temporário + rename: nunca fica um WAV a meio).
static bool downloadAudio(const String &url, const String &path) {
  Http h;
  if (!h.begin(url)) return false;
  h.auth();
  int status = h.http.GET();
  if (status != 200) {
    log_w("audio: HTTP %d", status);
    h.http.end();
    return false;
  }
  String tmp = path + ".part";
  File f = sd().open(tmp, FILE_WRITE);
  if (!f) {
    h.http.end();
    return false;
  }
  int written = h.http.writeToStream(&f);
  f.close();
  h.http.end();
  if (written <= 44) {
    sd().remove(tmp);
    return false;
  }
  sd().remove(path);
  bool ok = sd().rename(tmp, path);
  log_i("audio guardado: %s (%d bytes)", path.c_str(), written);
  return ok;
}

ApiResult apiSyncSchedule(bool force) {
  if (!app.sdOk) return ApiResult::BadResponse;
  static String etag;
  if (force) etag = "";
  Http h;
  if (!h.begin(apiUrl("/v1/hardware/me/schedule"))) return ApiResult::NetworkError;
  h.auth();
  if (etag.length()) h.http.addHeader("If-None-Match", etag);
  const char *keep[] = {"ETag"};
  h.http.collectHeaders(keep, 1);
  int status = h.http.GET();
  if (status == 304) {
    h.http.end();
    return ApiResult::NotModified;
  }
  if (status == 401) {
    h.http.end();
    return ApiResult::Unauthorized;
  }
  if (status <= 0) {
    h.http.end();
    return ApiResult::NetworkError;
  }
  if (status != 200) {
    h.http.end();
    return ApiResult::ServerError;
  }
  String newEtag = h.http.header("ETag");
  String body = h.http.getString();
  h.http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) return ApiResult::BadResponse;

  // 1) áudios em falta (o nome do ficheiro é o etag do áudio: só muda se o áudio mudar)
  bool allAudio = true;
  for (JsonObject m : doc["messages"].as<JsonArray>()) {
    String tag = m["audio"]["etag"] | "";
    String url = m["audio"]["url"] | "";
    if (!tag.length() || !url.length()) continue;
    String path = sdPath("audio/") + tag + ".wav";
    if (!sd().exists(path) && !downloadAudio(url, path)) allAudio = false;
  }
  // 2) apagar áudios que já não fazem parte da agenda
  File dir = sd().open(sdPath("audio"));
  if (dir) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      String name = String(f.name());
      f.close();
      String tag = name.endsWith(".wav") ? name.substring(0, name.length() - 4) : name;
      bool used = false;
      for (JsonObject m : doc["messages"].as<JsonArray>()) {
        if (tag == (m["audio"]["etag"] | "")) { used = true; break; }
      }
      if (!used) sd().remove(sdPath("audio/") + name);
    }
    dir.close();
  }
  // 3) definições que vêm com a agenda
  String tz = doc["timezone"] | "";
  if (tz.length() && tz != app.settings.timezone) {
    app.settings.timezone = tz;
    clockSetTimezone(tz);
  }
  if (doc["volume"].is<int>()) {
    app.settings.volume = constrain(doc["volume"].as<int>(), 0, 100);
    speakerSetVolume(app.settings.volume);
  }
  app.settings.scheduleEnabled = doc["enabled"] | true;
  settingsSave();
  // 4) a agenda fica no cartão (vale sem internet) e passa ao agendador
  if (!sdWriteAtomic(sdPath("schedule.json"), (const uint8_t *)body.c_str(), body.length())) return ApiResult::BadResponse;
  schedulerLoad();
  if (allAudio) etag = newEtag;  // com áudios em falta, tenta outra vez na próxima ronda
  log_i("agenda: %d mensagens", (int)doc["messages"].as<JsonArray>().size());
  return ApiResult::Ok;
}
