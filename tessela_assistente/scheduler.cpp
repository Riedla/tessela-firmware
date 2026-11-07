#include "scheduler.h"

#include <ArduinoJson.h>
#include <vector>

#include "app_state.h"
#include "audio_io.h"
#include "config.h"
#include "leds.h"
#include "rtc_clock.h"
#include "storage.h"

struct Item {
  String id;
  String title;
  bool fixed;
  uint8_t weekdays;   // bit 1..7 (ISO: 1 = segunda)
  int16_t year;       // kind variable
  int8_t month, day;
  int16_t minute;
  bool autoPlay;
  String file;        // /tessela/audio/<etag>.wav
};

static std::vector<Item> items;
static std::vector<String> queue;    // ficheiros por tocar (por ordem)
static String pendingFile;           // autoPlay=false: espera que o utilizador peça
static File wav;
static uint32_t wavLeft = 0;
static bool playing = false;
static int lastCheckedMinute = -1;
static int lastCheckedYday = -1;

void schedulerLoad() {
  items.clear();
  if (!app.sdOk) return;
  String text = sdReadText(sdPath("schedule.json"));
  if (!text.length()) return;
  JsonDocument doc;
  if (deserializeJson(doc, text)) {
    log_w("schedule.json invalido");
    return;
  }
  for (JsonObject m : doc["messages"].as<JsonArray>()) {
    Item it;
    it.id = m["id"] | "";
    it.title = m["title"] | "";
    it.fixed = String(m["kind"] | "fixed") == "fixed";
    it.weekdays = 0;
    for (int d : m["weekdays"].as<JsonArray>()) {
      if (d >= 1 && d <= 7) it.weekdays |= (1 << d);
    }
    String date = m["date"] | "";
    it.year = date.length() == 10 ? date.substring(0, 4).toInt() : 0;
    it.month = date.length() == 10 ? date.substring(5, 7).toInt() : 0;
    it.day = date.length() == 10 ? date.substring(8, 10).toInt() : 0;
    it.minute = m["minute"] | -1;
    it.autoPlay = m["autoPlay"] | true;
    String tag = m["audio"]["etag"] | "";
    it.file = sdPath("audio/") + tag + ".wav";
    if (it.minute >= 0 && tag.length()) items.push_back(it);
  }
  log_i("agendador: %u mensagens", (unsigned)items.size());
}

size_t schedulerCount() { return items.size(); }

static bool dueNow(const Item &it, const struct tm &t) {
  if (it.minute != t.tm_hour * 60 + t.tm_min) return false;
  if (it.fixed) {
    int iso = t.tm_wday == 0 ? 7 : t.tm_wday;  // tm_wday: 0 = domingo
    return it.weekdays & (1 << iso);
  }
  return it.year == t.tm_year + 1900 && it.month == t.tm_mon + 1 && it.day == t.tm_mday;
}

// Lê o cabeçalho WAV (PCM16 mono 16 kHz, como o servidor o envia) e posiciona-se nos dados.
static bool openWav(const String &path) {
  wav = sd().open(path, FILE_READ);
  if (!wav) return false;
  uint8_t hdr[12];
  if (wav.read(hdr, 12) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
    wav.close();
    return false;
  }
  uint16_t channels = 0, bits = 0;
  uint32_t rate = 0;
  for (;;) {
    uint8_t ch[8];
    if (wav.read(ch, 8) != 8) break;
    uint32_t size = ch[4] | (ch[5] << 8) | (ch[6] << 16) | ((uint32_t)ch[7] << 24);
    if (!memcmp(ch, "fmt ", 4)) {
      uint8_t f[16];
      wav.read(f, 16);
      channels = f[2] | (f[3] << 8);
      rate = f[4] | (f[5] << 8) | (f[6] << 16) | ((uint32_t)f[7] << 24);
      bits = f[14] | (f[15] << 8);
      if (size > 16) wav.seek(wav.position() + size - 16);
    } else if (!memcmp(ch, "data", 4)) {
      if (channels != 1 || bits != 16 || rate != AUDIO_SAMPLE_RATE) {
        log_w("WAV com formato inesperado (%u canais, %u bits, %u Hz)", channels, bits, (unsigned)rate);
        wav.close();
        return false;
      }
      wavLeft = size;
      return true;
    } else {
      wav.seek(wav.position() + size + (size & 1));
    }
  }
  wav.close();
  return false;
}

static void startNext() {
  while (!queue.empty()) {
    String f = queue.front();
    queue.erase(queue.begin());
    if (openWav(f)) {
      playing = true;
      setMode(Mode::PlayingMessage);
      speakerBeep(660, 90, 25);  // aviso curto antes da mensagem
      speakerBeep(990, 120, 25);
      log_i("a tocar mensagem %s", f.c_str());
      return;
    }
    log_w("mensagem em falta no cartao: %s", f.c_str());
  }
}

void schedulerStop() {
  if (wav) wav.close();
  playing = false;
  wavLeft = 0;
  queue.clear();
  speakerFlush();
  if (app.mode == Mode::PlayingMessage) setMode(app.settings.muted ? Mode::Muted : Mode::Idle);
}

bool schedulerPlaying() { return playing; }
bool schedulerHasPending() { return pendingFile.length() > 0; }

void schedulerPlayPending() {
  if (!pendingFile.length()) return;
  queue.push_back(pendingFile);
  pendingFile = "";
  ledsSetPendingMessage(false);
}

void schedulerLoop(bool canPlayNow) {
  // 1) uma vez por minuto: o que está para tocar agora?
  struct tm t;
  if (clockLocal(t) && (t.tm_min != lastCheckedMinute || t.tm_yday != lastCheckedYday)) {
    lastCheckedMinute = t.tm_min;
    lastCheckedYday = t.tm_yday;
    if (app.settings.scheduleEnabled) {
      for (const Item &it : items) {
        if (!dueNow(it, t)) continue;
        if (it.autoPlay) {
          queue.push_back(it.file);
        } else {
          pendingFile = it.file;
          ledsSetPendingMessage(true);
          speakerBeep(880, 120, 30);
        }
        log_i("mensagem agendada: %s (%s)", it.title.c_str(), it.id.c_str());
      }
    }
  }
  // 2) tocar (se não houver conversa; caso contrário ficam em fila para logo a seguir)
  if (!playing) {
    if (canPlayNow && !queue.empty()) startNext();
    return;
  }
  uint8_t buf[2048];
  while (wavLeft > 0 && speakerFree() >= sizeof(buf)) {
    size_t want = min((uint32_t)sizeof(buf), wavLeft);
    int n = wav.read(buf, want);
    if (n <= 0) {
      wavLeft = 0;
      break;
    }
    speakerWrite(buf, n, 0);
    wavLeft -= n;
  }
  if (wavLeft == 0 && speakerIdle()) {
    wav.close();
    playing = false;
    if (!queue.empty()) startNext();
    else setMode(app.settings.muted ? Mode::Muted : Mode::Idle);
  }
}
