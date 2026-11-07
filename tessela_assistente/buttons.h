// Botões do tampo: VOL−, MUTE, VOL+ (activos em LOW, pull-up interno).
#pragma once

#include <Arduino.h>

// Battery = VOL− e VOL+ premidos ao mesmo tempo (mostra a carga no anel).
enum class ButtonEvent : uint8_t { None, VolUp, VolDown, MuteShort, MuteLong, Battery };

void buttonsBegin();
ButtonEvent buttonsPoll();  // um evento de cada vez
