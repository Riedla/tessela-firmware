#include "provisioning.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "api_client.h"
#include "app_state.h"
#include "audio_io.h"
#include "battery.h"
#include "config.h"
#include "rtc_clock.h"
#include "scheduler.h"
#include "storage.h"

static WebServer *server = nullptr;
static DNSServer dns;
static bool active = false;

bool wifiConnect(uint32_t timeoutMs) {
  if (!app.settings.ssid.length()) return false;
  WiFi.mode(active ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(false);  // latência mínima na voz
  WiFi.begin(app.settings.ssid.c_str(), app.settings.pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(200);
  app.wifiUp = WiFi.status() == WL_CONNECTED;
  if (app.wifiUp) log_i("Wi-Fi: %s, RSSI %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  else log_w("Wi-Fi: sem ligacao a '%s'", app.settings.ssid.c_str());
  return app.wifiUp;
}

static String htmlEscape(const String &s) {
  String o;
  for (char c : s) {
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static String page(const String &msg) {
  String h = F("<!doctype html><html lang=pt><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Tessela Assistente</title><style>body{font-family:sans-serif;max-width:420px;margin:24px auto;padding:0 16px;"
               "background:#0d1320;color:#eee}input{width:100%;padding:10px;margin:6px 0 14px;border-radius:8px;border:1px solid #445;"
               "background:#18213a;color:#fff;box-sizing:border-box}button{width:100%;padding:12px;border:0;border-radius:8px;"
               "background:#ff7a1a;color:#111;font-weight:bold}.m{background:#18213a;padding:10px;border-radius:8px}</style>"
               "<h2>Assistente Tessela</h2>");
  if (msg.length()) h += "<p class=m>" + msg + "</p>";
  h += F("<form method=post action=/save><label>Rede Wi-Fi</label><input name=ssid value='");
  h += htmlEscape(app.settings.ssid);
  h += F("'><label>Palavra-passe do Wi-Fi</label><input name=pass type=password><label>Servidor</label><input name=server placeholder='https://api.exemplo.co.mz' value='");
  h += htmlEscape(app.settings.server);
  h += F("'><label>C&oacute;digo de emparelhamento (da app)</label><input name=code placeholder='K7PM-Q2XD'>"
         "<button>Guardar e ligar</button></form><p style='opacity:.6;font-size:12px'>Firmware " FIRMWARE_VERSION "</p></html>");
  return h;
}

static void handleRoot() { server->send(200, "text/html", page("")); }

static void handleSave() {
  String ssid = server->arg("ssid"), pass = server->arg("pass"), srv = server->arg("server"), code = server->arg("code");
  ssid.trim();
  srv.trim();
  code.trim();
  if (ssid.length()) {
    app.settings.ssid = ssid;
    if (pass.length()) app.settings.pass = pass;
  }
  if (srv.length()) app.settings.server = srv;
  settingsSave();
  String msg;
  if (!wifiConnect(20000)) {
    msg = "N&atilde;o foi poss&iacute;vel ligar &agrave; rede Wi-Fi. Confirme o nome e a palavra-passe.";
  } else if (code.length()) {
    String detail;
    ApiResult r = apiPair(code, detail);
    msg = r == ApiResult::Ok ? "Pronto! " + htmlEscape(detail) + ". O assistente vai reiniciar." : "Emparelhamento falhou: " + htmlEscape(detail);
    if (r == ApiResult::Ok) {
      server->send(200, "text/html", page(msg));
      delay(1500);
      ESP.restart();
    }
  } else {
    msg = app.settings.token.length() ? "Wi-Fi guardado. A reiniciar…" : "Wi-Fi ligado. Falta o c&oacute;digo de emparelhamento.";
    if (app.settings.token.length()) {
      server->send(200, "text/html", page(msg));
      delay(1500);
      ESP.restart();
    }
  }
  server->send(200, "text/html", page(msg));
}

void provisioningStart() {
  if (active) return;
  active = true;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ssid[20];
  snprintf(ssid, sizeof(ssid), "Tessela-%02X%02X", mac[4], mac[5]);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssid);
  dns.start(53, "*", WiFi.softAPIP());  // portal: qualquer nome vai dar à página
  server = new WebServer(80);
  server->on("/", HTTP_GET, handleRoot);
  server->on("/save", HTTP_POST, handleSave);
  server->onNotFound(handleRoot);
  server->begin();
  setMode(Mode::Setup);
  log_i("configuracao: liga-te a '%s' e abre http://%s", ssid, WiFi.softAPIP().toString().c_str());
}

void provisioningStop() {
  if (!active) return;
  server->stop();
  delete server;
  server = nullptr;
  dns.stop();
  WiFi.softAPdisconnect(true);
  active = false;
}

bool provisioningActive() { return active; }

void provisioningLoop() {
  if (!active) return;
  dns.processNextRequest();
  server->handleClient();
}

// ------------------------------------------------------------------ consola série

static void printStatus() {
  Serial.printf("firmware %s  modo %s\n", FIRMWARE_VERSION, modeName(app.mode));
  Serial.printf("wifi %s (%s)  servidor %s\n", app.wifiUp ? "ligado" : "desligado", app.settings.ssid.c_str(), app.settings.server.c_str());
  Serial.printf("emparelhado %s  dispositivo %s  tempo-real %s\n", app.settings.token.length() ? "sim" : "nao",
                app.settings.deviceId.c_str(), app.wsReady ? "pronto" : "-");
  Serial.printf("hora %s (%s)  rtc %s  cartao %s  mensagens %u\n", clockIso().c_str(), app.settings.timezone.c_str(),
                app.rtcOk ? "ok" : "falha", app.sdOk ? "ok" : "falha", (unsigned)schedulerCount());
  const BatteryStatus &b = battery();
  if (b.present) {
    Serial.printf("bateria %u%% (%u mV)  %s\n", b.percent, b.mv,
                  b.charging ? "a carregar" : b.powered ? "carregada (cabo ligado)" : "sem cabo");
  } else {
    Serial.printf("bateria: medidor nao responde  cabo %s\n", b.powered ? "ligado" : "desligado");
  }
  Serial.printf("volume %u  microfones %s  memoria livre %u / psram %u\n", app.settings.volume,
                app.settings.muted ? "desligados" : "ligados", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
}

void consoleLoop() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (line.length() < 200) line += c;
      continue;
    }
    line.trim();
    String cmd = line, arg;
    int sp = line.indexOf(' ');
    if (sp > 0) {
      cmd = line.substring(0, sp);
      arg = line.substring(sp + 1);
      arg.trim();
    }
    line = "";
    if (cmd == "wifi") {
      int s2 = arg.indexOf(' ');
      app.settings.ssid = s2 > 0 ? arg.substring(0, s2) : arg;
      app.settings.pass = s2 > 0 ? arg.substring(s2 + 1) : "";
      settingsSave();
      Serial.println(wifiConnect() ? "wifi ligado" : "wifi falhou");
    } else if (cmd == "server") {
      app.settings.server = arg;
      settingsSave();
      Serial.println("servidor guardado");
    } else if (cmd == "pair") {
      String detail;
      ApiResult r = apiPair(arg, detail);
      Serial.printf("emparelhar: %s (%s)\n", apiResultName(r), detail.c_str());
      if (r == ApiResult::Ok) ESP.restart();
    } else if (cmd == "volume") {
      app.settings.volume = constrain(arg.toInt(), 0, 100);
      speakerSetVolume(app.settings.volume);
      settingsSave();
      speakerBeep(880, 120);
    } else if (cmd == "sync") {
      Serial.printf("agenda: %s\n", apiResultName(apiSyncSchedule(true)));
    } else if (cmd == "forget") {
      settingsForgetPairing();
      Serial.println("token apagado; a reiniciar");
      ESP.restart();
    } else if (cmd == "setup") {
      provisioningStart();
    } else if (cmd == "reboot") {
      ESP.restart();
    } else if (cmd == "status") {
      printStatus();
    } else if (cmd.length()) {
      Serial.println("comandos: status | wifi <rede> <senha> | server <url> | pair <codigo> | volume <0-100> | sync | setup | forget | reboot");
    }
  }
}
