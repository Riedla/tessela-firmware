// Conversa em tempo real com o agente (docs/DEVICE_PROTOCOL.md §3–§8), WebSocket /v1/realtime.
//
// O dispositivo usa `vad: "server"` com `vadBargeIn: true`: depois da palavra de activação envia
// o áudio limpo do AFE em contínuo e o servidor decide onde acaba cada fala. Como há
// cancelamento de eco, a pessoa pode interromper a resposta a falar.
#pragma once

#include <Arduino.h>

enum class RtEvent : uint8_t {
  None,
  Ready,
  SpeechStarted,   // vad.start
  SpeechEnded,     // vad.end → o servidor vai responder
  ReplyStarted,
  AudioStarted,
  AudioEnded,
  ReplyEnded,
  Revoked,         // token deixou de valer → emparelhar de novo
  Replaced,        // outra ligação deste dispositivo tomou o lugar
  Closed,
};

void rtBegin();
void rtLoop();
bool rtConnected();      // WebSocket aberto e `ready` recebido
void rtConnect();        // liga (ou volta a ligar) se ainda não estiver
void rtDisconnect();
RtEvent rtPoll();        // próximo evento para a máquina de estados
bool rtStartUtterance(); // `audio.start` (força o início da fala, a seguir à palavra de activação)
bool rtSendAudio(const uint8_t *pcm, size_t len);
void rtCancel();         // barge-in local (ex.: botão ou palavra de activação durante a resposta)
bool rtPlayingTurn();    // há áudio do turno actual a chegar
