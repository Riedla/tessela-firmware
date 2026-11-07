// Botões do tampo: VOL−, MUTE, VOL+ (activos em LOW, pull-up interno).
#pragma once

#include <Arduino.h>

enum class ButtonEvent : uint8_t { None, VolUp, VolDown, MuteShort, MuteLong };

void buttonsBegin();
ButtonEvent buttonsPoll();  // um evento de cada vez
