// Anel de luz (12 WS2812) + barra frontal (6 WS2812) em cadeia, e o LED de estado.
// A cor diz o que o assistente está a fazer:
//   laranja suave a respirar = a ouvir · laranja a rodar = a pensar · laranja a pulsar = a falar
//   âmbar intermitente = mensagem por ouvir · vermelho fixo = microfones desligados
//   azul a piscar = modo de configuração · vermelho a piscar = erro · apagado = em espera
#pragma once

#include <Arduino.h>

void ledsBegin();
void ledsUpdate();            // chamar no loop (~30 fps): animação conforme o modo
void ledsFlash(uint8_t r, uint8_t g, uint8_t b, uint16_t ms);  // destaque curto (ex.: volume)
void ledsVolume(uint8_t volume);  // mostra o volume no anel durante 1,5 s
void ledsSetPendingMessage(bool on);
