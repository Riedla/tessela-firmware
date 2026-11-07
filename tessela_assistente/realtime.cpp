#include "realtime.h"

#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <deque>

#include "app_state.h"
#include "audio_io.h"
#include "config.h"
#include "storage.h"

static WebSocketsClient ws;
static bool started = false;
static bool wsOpen = false;
static bool ready = false;
static bool allowReconnect = true;
static uint32_t nextAttempt = 0;
static uint32_t backoffMs = RECONNECT_MIN_MS;
static uint32_t lastPing = 0;
static String currentTurn;       // turno cujo áudio podemos tocar
static String cancelledTurn;     // turno cancelado: os frames que ainda cheguem são descartados
static bool playingTurn = false;
static std::deque<RtEvent> events;
static String headers;
static String caPem;

static void push(RtEvent e) {
  if (events.size() < 32) events.push_back(e);
}

RtEvent rtPoll() {
  if (events.empty()) return RtEvent::None;
  RtEvent e = events.front();
  events.pop_front();
  return e;
}

bool rtConnected() { return wsOpen && ready; }
bool rtPlayingTurn() { return playingTurn; }

static void sendJson(JsonDocument &doc) {
  String s;
  serializeJson(doc, s);
  ws.sendTXT(s);
}

static void sendType(const char *type) {
  JsonDocument d;
  d["type"] = type;
  sendJson(d);
}

static void sendHello() {
  JsonDocument d;
  d["type"] = "hello";
  d["protocol"] = PROTOCOL_VERSION;
  d["device"]["model"] = DEVICE_MODEL;
  d["device"]["firmware"] = FIRMWARE_VERSION;
  JsonObject in = d["audio"]["in"].to<JsonObject>();
  in["encoding"] = "pcm16";
  in["sampleRate"] = AUDIO_SAMPLE_RATE;
  in["channels"] = 1;
  JsonObject out = d["audio"]["out"].to<JsonObject>();
  out["encoding"] = "pcm16";
  out["sampleRate"] = AUDIO_SAMPLE_RATE;
  out["channels"] = 1;
  d["vad"] = "server";
  d["vadBargeIn"] = true;  // o AFE tira o eco do altifalante
  if (app.settings.conversationId.length()) d["conversationId"] = app.settings.conversationId;
  else d["conversationId"] = nullptr;
  d["timezone"] = app.settings.timezone;
  sendJson(d);
}

static void scheduleRetry(uint32_t minimum) {
  uint32_t jitter = random(0, backoffMs / 5 + 1);
  nextAttempt = millis() + max(minimum, backoffMs + jitter);
  backoffMs = min((uint32_t)RECONNECT_MAX_MS, backoffMs * 2);
}

static void handleText(const char *payload, size_t len) {
  JsonDocument d;
  if (deserializeJson(d, payload, len)) return;
  String type = d["type"] | "";
  String turn = d["turnId"] | "";
  if (type == "ready") {
    ready = true;
    app.wsReady = true;
    backoffMs = RECONNECT_MIN_MS;
    String conv = d["conversationId"] | "";
    if (conv.length() && conv != app.settings.conversationId) {
      app.settings.conversationId = conv;
      settingsSave();
    }
    log_i("tempo real pronto (conversa %s, retomada=%d)", conv.c_str(), (int)(d["resumed"] | false));
    push(RtEvent::Ready);
  } else if (type == "vad.start") {
    push(RtEvent::SpeechStarted);
  } else if (type == "vad.end") {
    push(RtEvent::SpeechEnded);
  } else if (type == "reply.start") {
    push(RtEvent::ReplyStarted);
  } else if (type == "audio.start") {
    if (turn != cancelledTurn) {
      currentTurn = turn;
      playingTurn = true;
      push(RtEvent::AudioStarted);
    }
  } else if (type == "audio.end") {
    if (turn == currentTurn) {
      playingTurn = false;
      push(RtEvent::AudioEnded);
    }
  } else if (type == "reply.end") {
    String conv = d["conversationId"] | "";
    if (conv.length() && conv != app.settings.conversationId) {
      app.settings.conversationId = conv;
      settingsSave();
    }
    push(RtEvent::ReplyEnded);
  } else if (type == "error") {
    String code = d["code"] | "";
    log_w("erro do servidor: %s", code.c_str());
    if (code == "revoked") {
      allowReconnect = false;
      push(RtEvent::Revoked);
    } else if (code == "replaced") {
      allowReconnect = false;  // só volta a ligar por acção do utilizador (§8.1)
      push(RtEvent::Replaced);
    } else if (code == "conversation_not_found") {
      app.settings.conversationId = "";
      settingsSave();
    }
  }
}

static void onEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsOpen = true;
      ready = false;
      log_i("WebSocket ligado");
      sendHello();
      break;
    case WStype_DISCONNECTED:
      if (wsOpen) log_w("WebSocket fechado");
      wsOpen = false;
      ready = false;
      app.wsReady = false;
      playingTurn = false;
      push(RtEvent::Closed);
      if (allowReconnect) scheduleRetry(RECONNECT_MIN_MS);
      break;
    case WStype_TEXT:
      handleText((const char *)payload, length);
      break;
    case WStype_BIN:
      // voz da resposta: só depois do audio.start do turno actual (§7)
      if (playingTurn && length) {
        size_t done = speakerWrite(payload, length, 40);
        if (done < length) log_w("altifalante cheio: %u bytes perdidos", (unsigned)(length - done));
      }
      break;
    default:
      break;
  }
}

struct Url {
  bool tls = false;
  String host;
  uint16_t port = 0;
  String path = "/";
};

static bool parseUrl(const String &url, Url &u) {
  int p = url.indexOf("://");
  if (p < 0) return false;
  String scheme = url.substring(0, p);
  u.tls = scheme == "wss" || scheme == "https";
  String rest = url.substring(p + 3);
  int slash = rest.indexOf('/');
  String hostPort = slash >= 0 ? rest.substring(0, slash) : rest;
  u.path = slash >= 0 ? rest.substring(slash) : "/";
  int colon = hostPort.indexOf(':');
  u.host = colon >= 0 ? hostPort.substring(0, colon) : hostPort;
  u.port = colon >= 0 ? hostPort.substring(colon + 1).toInt() : (u.tls ? 443 : 80);
  return u.host.length() > 0;
}

void rtBegin() {
  ws.onEvent(onEvent);
  ws.setReconnectInterval(0xFFFFFFFF);  // a religação é nossa (backoff com aleatoriedade)
  ws.enableHeartbeat(25000, 10000, 2);
}

void rtConnect() {
  allowReconnect = true;
  if (started || !app.wifiUp || !app.settings.token.length() || !app.settings.realtimeUrl.length()) return;
  if (millis() < nextAttempt) return;
  Url u;
  if (!parseUrl(app.settings.realtimeUrl, u)) {
    log_e("realtimeUrl invalido");
    return;
  }
  headers = "Authorization: Bearer " + app.settings.token;
  ws.setExtraHeaders(headers.c_str());
  if (u.tls) {
    if (!caPem.length()) caPem = readCaFromSd();
    const char *ca = caPem.length() ? caPem.c_str() : TESSELA_ROOT_CA;
    if (ca) {
      ws.beginSslWithCA(u.host.c_str(), u.port, u.path.c_str(), ca, "");
    } else {
      log_w("WSS sem CA: cifrado mas sem verificar o servidor");
      ws.beginSSL(u.host.c_str(), u.port, u.path.c_str(), "", "");
    }
  } else {
    ws.begin(u.host.c_str(), u.port, u.path.c_str(), "");
  }
  started = true;
  lastPing = millis();
  nextAttempt = millis() + 15000;  // prazo desta tentativa (handshake TCP + TLS + upgrade)
}

void rtDisconnect() {
  if (started) {
    if (wsOpen) sendType("bye");
    ws.disconnect();
  }
  started = false;
  wsOpen = false;
  ready = false;
  app.wsReady = false;
}

void rtLoop() {
  if (!started) return;
  ws.loop();
  if (!wsOpen && allowReconnect && millis() >= nextAttempt) {
    // Tentativa sem sucesso dentro do prazo (ou espera de backoff cumprida): recomeça do zero.
    ws.disconnect();
    started = false;
    nextAttempt = millis();
    rtConnect();
    return;
  }
  if (!allowReconnect && !wsOpen) {
    started = false;
    return;
  }
  if (ready && millis() - lastPing > APP_PING_INTERVAL_MS) {
    lastPing = millis();
    sendType("ping");
  }
}

bool rtStartUtterance() {
  if (!rtConnected()) return false;
  cancelledTurn = "";
  sendType("audio.start");
  return true;
}

bool rtSendAudio(const uint8_t *pcm, size_t len) {
  if (!rtConnected() || !len) return false;
  return ws.sendBIN(pcm, len);
}

void rtCancel() {
  if (rtConnected()) sendType("cancel");
  cancelledTurn = currentTurn;
  playingTurn = false;
  speakerFlush();
}
