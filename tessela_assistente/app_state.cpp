#include "app_state.h"

AppState app;

const char *modeName(Mode m) {
  switch (m) {
    case Mode::Booting: return "arranque";
    case Mode::Setup: return "configuracao";
    case Mode::Idle: return "em_espera";
    case Mode::Listening: return "a_ouvir";
    case Mode::Thinking: return "a_pensar";
    case Mode::Speaking: return "a_falar";
    case Mode::FollowUp: return "seguimento";
    case Mode::PlayingMessage: return "mensagem";
    case Mode::Muted: return "silencio";
    case Mode::Error: return "erro";
  }
  return "?";
}

void setMode(Mode m) {
  if (app.mode == m) return;
  log_i("modo: %s -> %s", modeName(app.mode), modeName(m));
  app.mode = m;
}
