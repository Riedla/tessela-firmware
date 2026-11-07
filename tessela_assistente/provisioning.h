// Configuração inicial sem cabos: ponto de acesso "Tessela-XXXX" com uma página para Wi-Fi,
// servidor e código de emparelhamento. Também há comandos na consola série (115200).
#pragma once

#include <Arduino.h>

void provisioningStart();     // abre o ponto de acesso + página (modo Setup)
void provisioningStop();
bool provisioningActive();
void provisioningLoop();
void consoleLoop();           // comandos série: wifi, server, pair, status, volume, forget, setup, reboot
bool wifiConnect(uint32_t timeoutMs = 15000);
