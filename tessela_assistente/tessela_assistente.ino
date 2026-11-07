// Tessela Assistente — firmware da placa "Assistente IA" (ESP32-S3-WROOM-1-N16R8).
//
//  • Conversa em tempo real com o agente Tessela: palavra de activação local ("Hi ESP"),
//    áudio limpo pelo AFE (AEC + BSS + NS) em streaming para wss://…/v1/realtime, resposta falada
//    em streaming, interrupção a falar (barge-in) e 8 s para continuar sem repetir a palavra.
//  • Mensagens de voz gravadas na app: agenda e áudios no cartão microSD, tocadas à hora certa
//    pelo relógio RTC, mesmo sem internet.
//  • Botões VOL− · MUTE · VOL+, anel de luz e barra frontal, configuração por ponto de acesso.
//
// Protocolo: tessela-api/docs/DEVICE_PROTOCOL.md (v1). Compilar: ver README.md.

#include <ESP_SR.h>  // traz o srmodels.bin (modelos de voz) para a partição "model" ao gravar
#include <WiFi.h>

#include "api_client.h"
#include "app_state.h"
#include "audio_io.h"
#include "buttons.h"
#include "config.h"
#include "leds.h"
#include "provisioning.h"
#include "realtime.h"
#include "rtc_clock.h"
#include "scheduler.h"
#include "storage.h"
#include "voice_frontend.h"

static bool frontendOk = false;
static bool awaitingStart = false;   // palavra dita antes de a ligação estar pronta
static bool replyDone = false;
static uint32_t convStartedAt = 0;
static uint32_t followUpAt = 0;
static uint32_t lastScheduleSync = 0;
static uint32_t lastNtp = 0;
static uint32_t lastWifiTry = 0;
static uint32_t volumeChangedAt = 0;

static bool inConversation() {
  Mode m = app.mode;
  return m == Mode::Listening || m == Mode::Thinking || m == Mode::Speaking || m == Mode::FollowUp;
}

static void endConversation(bool beep) {
  frontendSetStreaming(false);
  awaitingStart = false;
  replyDone = false;
  if (beep) speakerBeep(440, 80, 20);
  setMode(app.settings.muted ? Mode::Muted : Mode::Idle);
}

static void startConversation() {
  if (schedulerPlaying()) schedulerStop();
  if (app.mode == Mode::Speaking || app.mode == Mode::Thinking) rtCancel();
  speakerFlush();
  speakerBeep(1046, 70, 25);
  frontendSetStreaming(true);
  convStartedAt = millis();
  replyDone = false;
  setMode(Mode::Listening);
  if (rtConnected()) {
    rtStartUtterance();
    awaitingStart = false;
  } else {
    awaitingStart = true;  // o AFE guarda ~1 s de áudio enquanto liga
    rtConnect();
  }
}

static void setMuted(bool muted) {
  app.settings.muted = muted;
  settingsSave();
  frontendSetPaused(muted);
  if (muted) {
    if (inConversation()) {
      rtCancel();
      endConversation(false);
    }
    speakerBeep(330, 150, 25);
    setMode(Mode::Muted);
  } else {
    speakerBeep(660, 80, 25);
    speakerBeep(880, 80, 25);
    setMode(Mode::Idle);
  }
}

static void handleButtons() {
  ButtonEvent ev = buttonsPoll();
  switch (ev) {
    case ButtonEvent::VolUp:
    case ButtonEvent::VolDown: {
      bool up = ev == ButtonEvent::VolUp;
      int v = (int)app.settings.volume + (up ? VOLUME_STEP : -VOLUME_STEP);
      app.settings.volume = constrain(v, 0, 100);
      speakerSetVolume(app.settings.volume);
      ledsVolume(app.settings.volume);
      if (!inConversation() && !schedulerPlaying()) speakerBeep(880, 60, 30);
      volumeChangedAt = millis();
      break;
    }
    case ButtonEvent::MuteShort:
      if (schedulerHasPending() && !app.settings.muted) {
        schedulerPlayPending();  // mensagem à espera (autoPlay=false): ouvir agora
      } else if (inConversation()) {
        rtCancel();              // interromper a resposta e terminar a conversa
        endConversation(true);
      } else {
        setMuted(!app.settings.muted);
      }
      break;
    case ButtonEvent::MuteLong:
      speakerBeep(523, 100, 25);
      provisioningStart();
      break;
    default:
      break;
  }
  if (volumeChangedAt && millis() - volumeChangedAt > 3000) {  // grava na NVS só depois de parar de mexer
    settingsSave();
    volumeChangedAt = 0;
  }
}

static void handleRealtime() {
  for (RtEvent e = rtPoll(); e != RtEvent::None; e = rtPoll()) {
    switch (e) {
      case RtEvent::Ready:
        if (awaitingStart && app.mode == Mode::Listening) {
          rtStartUtterance();
          awaitingStart = false;
        }
        break;
      case RtEvent::SpeechStarted:
        if (app.mode == Mode::Speaking) speakerFlush();  // a pessoa interrompeu (o servidor já cancelou)
        if (inConversation()) setMode(Mode::Listening);
        break;
      case RtEvent::SpeechEnded:
        if (inConversation()) setMode(Mode::Thinking);
        break;
      case RtEvent::ReplyStarted:
        replyDone = false;
        break;
      case RtEvent::AudioStarted:
        if (inConversation()) setMode(Mode::Speaking);
        break;
      case RtEvent::AudioEnded:
      case RtEvent::ReplyEnded:
        replyDone = true;
        break;
      case RtEvent::Revoked:
        endConversation(false);
        rtDisconnect();
        settingsForgetPairing();
        provisioningStart();
        break;
      case RtEvent::Replaced:
        endConversation(true);
        break;
      case RtEvent::Closed:
        if (inConversation() && !awaitingStart) endConversation(true);
        break;
      default:
        break;
    }
  }
  // depois de acabar de tocar a resposta: janela para continuar sem palavra de activação
  if (replyDone && (app.mode == Mode::Speaking || app.mode == Mode::Thinking) && speakerIdle() && !rtPlayingTurn()) {
    replyDone = false;
    followUpAt = millis();
    setMode(Mode::FollowUp);
  }
  if (app.mode == Mode::FollowUp && millis() - followUpAt > FOLLOWUP_WINDOW_MS && !frontendSpeech()) {
    endConversation(false);
  }
  if (inConversation() && millis() - convStartedAt > CONVERSATION_MAX_MS) {
    rtCancel();
    endConversation(true);
  }
  if (awaitingStart && millis() - convStartedAt > 10000) {  // não conseguiu ligar
    log_w("sem ligacao ao servidor para conversar");
    speakerBeep(300, 300, 30);
    endConversation(false);
  }
}

static void pumpMicrophone() {
  if (!inConversation() || awaitingStart) return;
  uint8_t frame[WS_FRAME_MS * AUDIO_SAMPLE_RATE / 1000 * 2];  // 32 ms = 1024 bytes
  while (frontendAvailable() >= sizeof(frame)) {
    size_t n = frontendRead(frame, sizeof(frame));
    if (!rtSendAudio(frame, n)) break;
  }
}

static void maintainNetwork() {
  if (provisioningActive()) return;
  if (WiFi.status() != WL_CONNECTED) {
    if (app.wifiUp) log_w("Wi-Fi perdido");
    app.wifiUp = false;
    app.wsReady = false;
    if (app.settings.ssid.length() && millis() - lastWifiTry > 15000) {
      lastWifiTry = millis();
      WiFi.reconnect();
    }
    return;
  }
  if (!app.wifiUp) {
    app.wifiUp = true;
    log_i("Wi-Fi de volta");
    rtConnect();
  }
  bool idle = app.mode == Mode::Idle || app.mode == Mode::Muted;
  if (idle && (lastNtp == 0 || millis() - lastNtp > NTP_RESYNC_MS)) {
    lastNtp = millis();
    clockSyncNtp(5000);
  }
  if (idle && app.settings.token.length() && (lastScheduleSync == 0 || millis() - lastScheduleSync > SCHEDULE_POLL_MS)) {
    lastScheduleSync = millis();
    ApiResult r = apiSyncSchedule();
    if (r == ApiResult::Unauthorized) {
      log_w("token recusado: emparelhar de novo");
      rtDisconnect();
      settingsForgetPairing();
      provisioningStart();
    }
  }
  if (app.settings.token.length()) rtConnect();  // ligação sempre pronta (menos espera depois da palavra)
}

void setup() {
  Serial.begin(115200);
  delay(200);
  log_i("Tessela Assistente %s", FIRMWARE_VERSION);
  settingsLoad();
  buttonsBegin();
  ledsBegin();
  if (!audioBegin()) {
    app.lastError = "audio";
    setMode(Mode::Error);
  }
  speakerSetVolume(app.settings.volume);
  app.sdOk = sdBegin();
  clockBegin();
  schedulerLoad();
  frontendOk = frontendBegin();
  if (!frontendOk) app.lastError = "frente de voz (modelos)";
  frontendSetPaused(app.settings.muted);
  rtBegin();

  if (!app.settings.ssid.length() || !app.settings.server.length()) {
    provisioningStart();
    return;
  }
  if (wifiConnect()) {
    clockSyncNtp();
    lastNtp = millis();
    if (app.settings.token.length()) {
      ApiResult r = apiCheckToken();
      if (r == ApiResult::Unauthorized) {
        settingsForgetPairing();
      } else {
        apiSyncSchedule(true);
        lastScheduleSync = millis();
        rtConnect();
      }
    }
  }
  if (!app.settings.token.length()) {
    provisioningStart();
    return;
  }
  setMode(app.settings.muted ? Mode::Muted : (frontendOk ? Mode::Idle : Mode::Error));
  speakerBeep(523, 90, 25);
  speakerBeep(784, 140, 25);
}

void loop() {
  consoleLoop();
  provisioningLoop();
  handleButtons();
  maintainNetwork();
  rtLoop();
  if (frontendOk && frontendTakeWake() && !app.settings.muted && app.mode != Mode::Setup) {
    log_i("palavra de activacao");
    startConversation();
  }
  handleRealtime();
  pumpMicrophone();
  bool canPlay = !inConversation() && app.mode != Mode::Setup;
  schedulerLoop(canPlay);
  ledsUpdate();
  delay(1);
}
