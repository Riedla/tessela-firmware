// Estado partilhado entre os módulos (uma instância global, em app_state.cpp).
#pragma once

#include <Arduino.h>

enum class Mode : uint8_t {
  Booting,
  Setup,          // sem Wi-Fi ou sem token: ponto de acesso + consola série
  Idle,           // à espera da palavra de activação
  Listening,      // a enviar a fala ao servidor
  Thinking,       // o servidor está a perceber/pensar
  Speaking,       // a tocar a resposta
  FollowUp,       // janela para continuar a conversa sem palavra de activação
  PlayingMessage, // a tocar uma mensagem de voz agendada
  Muted,
  Error,
};

struct Settings {
  String ssid;
  String pass;
  String server;        // https://api.exemplo.co.mz
  String token;         // tsl_hw_… (nunca impresso)
  String deviceId;
  String realtimeUrl;   // wss://…/v1/realtime
  String conversationId;
  String timezone = "Africa/Maputo";
  uint8_t volume = 70;
  bool scheduleEnabled = true;
  bool muted = false;
};

struct AppState {
  volatile Mode mode = Mode::Booting;
  Settings settings;
  bool wifiUp = false;
  bool wsReady = false;   // `ready` recebido
  bool sdOk = false;
  bool rtcOk = false;
  bool timeValid = false;
  String lastError;
};

extern AppState app;

const char *modeName(Mode m);
void setMode(Mode m);
