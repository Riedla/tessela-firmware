#include "voice_frontend.h"

#include <esp_heap_caps.h>
#include <freertos/stream_buffer.h>

#include "audio_io.h"
#include "config.h"

extern "C" {
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"
void tessela_afe_default_config(afe_config_t *cfg);
}

static const esp_afe_sr_iface_t *afe = nullptr;
static esp_afe_sr_data_t *afeData = nullptr;
static StreamBufferHandle_t outBuf = nullptr;
static volatile bool paused = false;
static volatile bool streaming = false;
static volatile bool wakeFlag = false;
static volatile bool speech = false;
static volatile float levelDb = -90.0f;

static void feedTask(void *) {
  const int chunk = afe->get_feed_chunksize(afeData);
  const int channels = 3;  // MIC1, MIC2, referência
  int16_t *buf = (int16_t *)heap_caps_malloc(chunk * channels * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  int16_t *l = (int16_t *)heap_caps_malloc(chunk * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  int16_t *r = (int16_t *)heap_caps_malloc(chunk * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  int16_t *ref = (int16_t *)heap_caps_malloc(chunk * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf || !l || !r || !ref) {
    log_e("sem memoria para o AFE");
    vTaskDelete(nullptr);
  }
  log_i("AFE: blocos de %d amostras, %d canais", chunk, channels);
  for (;;) {
    if (paused) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    micRead(l, r, chunk);
    referenceRead(ref, chunk);
    for (int i = 0; i < chunk; i++) {
      buf[i * channels + 0] = l[i];
      buf[i * channels + 1] = r[i];
      buf[i * channels + 2] = ref[i];
    }
    afe->feed(afeData, buf);
  }
}

static void fetchTask(void *) {
  for (;;) {
    afe_fetch_result_t *res = afe->fetch(afeData);
    if (!res || res->ret_value == ESP_FAIL) {
      vTaskDelay(1);
      continue;
    }
    if (res->wakeup_state == WAKENET_DETECTED && !paused) wakeFlag = true;
    speech = res->vad_state == AFE_VAD_SPEECH;
    levelDb = res->data_volume;
    if (streaming && !paused && res->data && res->data_size > 0) {
      // Se o WebSocket se atrasar, o mais antigo perde-se (nunca bloqueia o AFE).
      if (xStreamBufferSpacesAvailable(outBuf) < (size_t)res->data_size) {
        uint8_t drop[512];
        xStreamBufferReceive(outBuf, drop, sizeof(drop), 0);
      }
      xStreamBufferSend(outBuf, res->data, res->data_size, 0);
    }
  }
}

bool frontendBegin() {
  srmodel_list_t *models = esp_srmodel_init("model");
  if (!models) {
    log_e("modelos de voz nao encontrados: grava com PartitionScheme=esp_sr_16 (srmodels.bin)");
    return false;
  }
  char *wn = esp_srmodel_filter(models, ESP_WN_PREFIX, WAKENET_KEYWORD);
  if (!wn) {
    log_e("modelo de palavra de activacao '%s' nao existe no srmodels.bin", WAKENET_KEYWORD);
    return false;
  }
  afe = &ESP_AFE_SR_HANDLE;
  afe_config_t cfg;
  tessela_afe_default_config(&cfg);
  cfg.aec_init = true;            // cancelamento de eco com a referência do altifalante
  cfg.se_init = true;             // BSS: separação da voz com 2 microfones
  cfg.vad_init = true;
  cfg.wakenet_init = true;
  cfg.wakenet_model_name = wn;
  cfg.wakenet_mode = DET_MODE_2CH_90;
  cfg.afe_mode = SR_MODE_HIGH_PERF;
  cfg.afe_perferred_core = 1;
  cfg.afe_perferred_priority = 5;
  cfg.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
  cfg.pcm_config.total_ch_num = 3;
  cfg.pcm_config.mic_num = 2;
  cfg.pcm_config.ref_num = 1;
  cfg.pcm_config.sample_rate = AUDIO_SAMPLE_RATE;
  afeData = afe->create_from_config(&cfg);
  if (!afeData) {
    log_e("AFE nao arrancou");
    return false;
  }
  // 1 s de áudio limpo em fila para o WebSocket (cobre o tempo de ligar depois da palavra).
  outBuf = xStreamBufferCreate(AUDIO_SAMPLE_RATE * 2, 2);
  xTaskCreatePinnedToCore(feedTask, "afe_feed", 8192, nullptr, 5, nullptr, 0);
  xTaskCreatePinnedToCore(fetchTask, "afe_fetch", 8192, nullptr, 5, nullptr, 1);
  log_i("frente de voz pronta (palavra: %s)", wn);
  return true;
}

void frontendSetPaused(bool p) {
  paused = p;
  if (p) {
    streaming = false;
    wakeFlag = false;
  }
}

void frontendSetStreaming(bool on) {
  if (on && !streaming && outBuf) xStreamBufferReset(outBuf);
  streaming = on;
}

size_t frontendRead(uint8_t *buf, size_t max) {
  return outBuf ? xStreamBufferReceive(outBuf, buf, max & ~(size_t)1, 0) : 0;
}

size_t frontendAvailable() { return outBuf ? xStreamBufferBytesAvailable(outBuf) : 0; }

bool frontendTakeWake() {
  if (!wakeFlag) return false;
  wakeFlag = false;
  return true;
}

bool frontendSpeech() { return speech; }
float frontendLevelDb() { return levelDb; }
