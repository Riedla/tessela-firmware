// Microfones (T5838 PDM no I2S0, MK1+MK2) e altifalante (I2S1, MAX98357A), PCM16 a 16 kHz.
//
// O altifalante tem uma tarefa própria que tira áudio de um buffer circular (o WebSocket e o
// leitor de WAV só escrevem lá, nunca bloqueiam) e escreve no I2S ao ritmo real. Cada amostra
// tocada (já com o volume aplicado) é copiada para a "referência", que o cancelamento de eco
// (AEC) do AFE usa para tirar a voz do próprio altifalante do sinal dos microfones.
#pragma once

#include <Arduino.h>

bool audioBegin();

// --- altifalante
size_t speakerWrite(const uint8_t *pcm, size_t len, uint32_t waitMs = 0);  // devolve bytes aceites
size_t speakerFree();
size_t speakerQueued();
void speakerFlush();                    // pára já (barge-in) e esvazia o buffer
bool speakerIdle();                     // nada em fila nem a tocar
void speakerSetVolume(uint8_t volume);  // 0..100
uint8_t speakerVolume();
void speakerBeep(uint16_t freqHz, uint16_t ms, uint8_t level = 40);  // som curto (feedback)

// --- microfones e referência (usados pelo voice_frontend)
size_t micRead(int16_t *left, int16_t *right, size_t frames);  // bloqueia até ter `frames` amostras
bool micWakeActive();  // WAKE do MK1 (detecção de actividade acústica, se configurada)
void referenceRead(int16_t *out, size_t frames);               // o que foi tocado (ou zeros)
