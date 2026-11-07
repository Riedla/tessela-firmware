// Hora certa sem internet: RTC DS3231 (com pilha CR2032) guarda UTC; NTP acerta-o quando há Wi-Fi.
// O fuso vem da agenda do servidor (nome IANA, ex.: Africa/Maputo) e é convertido para POSIX.
#pragma once

#include <Arduino.h>
#include <time.h>

bool clockBegin();                       // lê o RTC e acerta o relógio do sistema
void clockSetTimezone(const String &iana);
bool clockSyncNtp(uint32_t timeoutMs = 8000);  // NTP → sistema → RTC
bool clockValid();
bool clockLocal(struct tm &out);         // hora local actual
String clockIso();                       // "2026-11-07 07:00:00"
