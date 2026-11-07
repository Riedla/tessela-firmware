// Mensagens de voz agendadas (docs/DEVICE_PROTOCOL.md §14): agenda no cartão, relógio RTC,
// reprodução do WAV do cartão. Funciona sem internet.
#pragma once

#include <Arduino.h>

void schedulerLoad();                 // (re)lê /tessela/schedule.json
void schedulerLoop(bool canPlayNow);  // chamar no loop; `canPlayNow` = não há conversa em curso
bool schedulerPlaying();
void schedulerStop();
bool schedulerHasPending();           // mensagem com autoPlay=false à espera de ser ouvida
void schedulerPlayPending();
size_t schedulerCount();
