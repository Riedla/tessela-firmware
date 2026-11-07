// Definições na NVS (Wi-Fi, servidor, token) e ficheiros no cartão microSD (agenda e áudios).
#pragma once

#include <Arduino.h>
#include <FS.h>

void settingsLoad();
void settingsSave();          // grava tudo (token incluído) na NVS
void settingsForgetPairing(); // apaga token, deviceId e conversa

bool sdBegin();
bool sdPresent();
fs::FS &sd();
String sdPath(const char *rel);     // "/tessela/<rel>"
bool sdWriteAtomic(const String &path, const uint8_t *data, size_t len);
String sdReadText(const String &path, size_t maxLen = 64 * 1024);
uint64_t sdFreeBytes();
String readCaFromSd();              // /tessela/ca.pem (vazio se não houver)
