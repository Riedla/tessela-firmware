// Frente de voz com o AFE do ESP-SR (Espressif): 2 microfones + 1 referência do altifalante.
//
//   MIC1, MIC2, referência ──► AEC (tira o eco do altifalante)
//                           ──► BSS/beamforming (separa a voz de quem fala) + NS (ruído)
//                           ──► WakeNet ("Hi ESP")  +  VAD (há fala?)
//                           ──► áudio limpo, mono, 16 kHz  ──►  WebSocket (só quando pedido)
#pragma once

#include <Arduino.h>

bool frontendBegin();
void frontendSetPaused(bool paused);     // mute: os microfones deixam de ser lidos
void frontendSetStreaming(bool on);      // entregar o áudio limpo ao WebSocket
size_t frontendRead(uint8_t *buf, size_t max);  // áudio limpo em fila (não bloqueia)
size_t frontendAvailable();
bool frontendTakeWake();                 // true uma vez por cada palavra de activação detectada
bool frontendSpeech();                   // VAD local: há fala agora?
float frontendLevelDb();                 // volume da entrada (para a animação do anel)
