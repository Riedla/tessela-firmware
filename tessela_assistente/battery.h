// Bateria Li-ion 1S (placa v1.3): medidor de carga LC709203F no I2C (0x0B) e o carregador BQ24074,
// que avisa no PWR_GOOD (GPIO 10, LOW = cabo USB-C ligado). O carregador trabalha sozinho; aqui só
// se lê o estado, se avisa a pessoa e se protege a bateria quando está quase vazia.
#pragma once

#include <Arduino.h>

struct BatteryStatus {
  bool present = false;   // o medidor respondeu (placa com bateria)
  uint8_t percent = 0;    // 0..100 (RSOC do medidor)
  uint16_t mv = 0;        // tensão da célula
  bool powered = false;   // cabo USB-C ligado (PWR_GOOD)
  bool charging = false;  // ligado e ainda não está cheia
};

enum class BatteryEvent : uint8_t { None, Plugged, Unplugged, Low, VeryLow, Critical, Full };

bool batteryBegin();                // depois de clockBegin() (Wire já iniciado)
void batteryLoop();                 // lê a cada 10 s
BatteryEvent batteryTakeEvent();    // um evento de cada vez (para avisar e enviar à API)
const BatteryStatus &battery();
bool batteryCritical();             // sem cabo e abaixo do mínimo: hora de dormir
void batterySleepUntilCharger();    // deep sleep; acorda ao ligar o cabo (ou a cada hora, para rever)
