#include "audio_io.h"

#include <ESP_I2S.h>
#include <driver/i2s_pdm.h>
#include <esp_heap_caps.h>
#include <freertos/stream_buffer.h>
#include <math.h>

#include "config.h"

static i2s_chan_handle_t micChan = nullptr;  // I2S0 em PDM RX (driver do ESP-IDF: precisa do downsampling ×128)
static I2SClass i2sSpk;

static StreamBufferHandle_t spkBuf = nullptr;
static volatile bool spkFlushReq = false;
static volatile bool spkPlaying = false;
static volatile uint8_t spkVolume = VOLUME_DEFAULT;
static volatile int32_t spkGainQ15 = 0;  // ganho em Q15 (curva quadrática do volume)

// Referência do AEC: anel simples, escrito pela tarefa do altifalante, lido pela tarefa do AFE.
static const size_t REF_LEN = 8192;  // ~0,5 s
static int16_t *refRing = nullptr;
static uint32_t refWrite = 0;  // protegidos por refMux
static uint32_t refRead = 0;
static portMUX_TYPE refMux = portMUX_INITIALIZER_UNLOCKED;

static void updateGain(uint8_t v) {
  float g = (float)v / 100.0f;
  spkGainQ15 = (int32_t)(g * g * 32767.0f);
}

static void refPush(const int16_t *s, size_t n) {
  portENTER_CRITICAL(&refMux);
  for (size_t i = 0; i < n; i++) {
    refRing[refWrite % REF_LEN] = s[i];
    refWrite++;
  }
  if (refWrite - refRead > REF_LEN) refRead = refWrite - REF_LEN;  // leitor atrasado: salta
  portEXIT_CRITICAL(&refMux);
}

void referenceRead(int16_t *out, size_t frames) {
  portENTER_CRITICAL(&refMux);
  size_t avail = refWrite - refRead;
  size_t n = avail < frames ? avail : frames;
  for (size_t i = 0; i < n; i++) out[i] = refRing[(refRead + i) % REF_LEN];
  refRead += n;
  portEXIT_CRITICAL(&refMux);
  for (size_t i = n; i < frames; i++) out[i] = 0;
}

static void ampEnable(bool on) {
  static bool state = false;
  if (on == state) return;
  state = on;
  digitalWrite(PIN_AMP_SD, on ? HIGH : LOW);
}

static void speakerTask(void *) {
  const size_t N = 256;  // 16 ms
  int16_t mono[N];
  int16_t stereo[N * 2];
  uint32_t silentSince = millis();
  for (;;) {
    if (spkFlushReq) {
      xStreamBufferReset(spkBuf);
      spkFlushReq = false;
    }
    size_t got = xStreamBufferReceive(spkBuf, mono, sizeof(mono), pdMS_TO_TICKS(8));
    size_t n = got / 2;
    if (n > 0) {
      spkPlaying = true;
      silentSince = millis();
      ampEnable(true);
      int32_t g = spkGainQ15;
      for (size_t i = 0; i < n; i++) {
        int32_t v = ((int32_t)mono[i] * g) >> 15;
        mono[i] = (int16_t)v;
      }
    } else {
      spkPlaying = false;
      n = N;
      memset(mono, 0, sizeof(mono));
      if (millis() - silentSince > 3000) ampEnable(false);  // sem chiado em repouso
    }
    for (size_t i = 0; i < n; i++) {
      stereo[2 * i] = mono[i];
      stereo[2 * i + 1] = mono[i];
    }
    refPush(mono, n);
    i2sSpk.write((uint8_t *)stereo, n * 4);  // bloqueia ao ritmo do I2S (relógio do áudio)
  }
}

bool audioBegin() {
  pinMode(PIN_AMP_SD, OUTPUT);
  digitalWrite(PIN_AMP_SD, LOW);
  updateGain(spkVolume);

  refRing = (int16_t *)heap_caps_calloc(REF_LEN, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  uint8_t *storage = (uint8_t *)heap_caps_malloc(SPEAKER_BUFFER_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  static StaticStreamBuffer_t spkBufStruct;
  if (!refRing || !storage) {
    log_e("sem memoria para os buffers de audio");
    return false;
  }
  spkBuf = xStreamBufferCreateStatic(SPEAKER_BUFFER_BYTES, 2, storage, &spkBufStruct);

  // Microfones T5838: I2S0 em PDM RX, 16 bits, estéreo (esquerda = MK2, direita = MK1).
  // Criado antes do altifalante para ficar com o I2S0 (o único com PDM RX).
  pinMode(PIN_MIC_THSEL, OUTPUT);
  digitalWrite(PIN_MIC_THSEL, LOW);  // sem AAD: o microfone funciona só pelo relógio
  pinMode(PIN_MIC_WAKE, INPUT);      // pull-up de 10k na placa (R28); Q2 puxa a LOW quando o MK1 detecta som
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = 6;
  cc.dma_frame_num = 256;
  i2s_pdm_rx_config_t pc = {
      .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
      .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {.clk = (gpio_num_t)PIN_PDM_CLK, .din = (gpio_num_t)PIN_PDM_DATA0, .invert_flags = {.clk_inv = false}},
  };
  pc.clk_cfg.dn_sample_mode = I2S_PDM_DSR_16S;  // CLK = 128 × 16 kHz = 2,048 MHz (o T5838 precisa de 2,0–3,7 MHz)
  if (i2s_new_channel(&cc, nullptr, &micChan) != ESP_OK || i2s_channel_init_pdm_rx_mode(micChan, &pc) != ESP_OK ||
      i2s_channel_enable(micChan) != ESP_OK) {
    log_e("PDM dos microfones falhou");
    return false;
  }
  // Altifalante: I2S1 standard, 16 bits, estéreo (o MAX98357A toca o canal esquerdo).
  i2sSpk.setPins(PIN_I2S1_BCLK, PIN_I2S1_WS, PIN_AMP_DIN);
  if (!i2sSpk.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    log_e("I2S do altifalante falhou");
    return false;
  }
  xTaskCreatePinnedToCore(speakerTask, "altifalante", 6144, nullptr, 6, nullptr, 1);
  return true;
}

size_t micRead(int16_t *left, int16_t *right, size_t frames) {
  static int16_t raw[512 * 2];
  size_t done = 0;
  while (done < frames) {
    size_t chunk = frames - done;
    if (chunk > 512) chunk = 512;
    size_t want = chunk * 2 * sizeof(int16_t);
    size_t got = 0;
    while (got < want) {
      size_t r = 0;
      if (i2s_channel_read(micChan, (uint8_t *)raw + got, want - got, &r, pdMS_TO_TICKS(100)) != ESP_OK && r == 0) vTaskDelay(1);
      got += r;
    }
    for (size_t i = 0; i < chunk; i++) {
      int32_t l = (int32_t)raw[2 * i] << MIC_GAIN_SHIFT;
      int32_t r = (int32_t)raw[2 * i + 1] << MIC_GAIN_SHIFT;
      left[done + i] = (int16_t)constrain(l, -32768, 32767);
      right[done + i] = (int16_t)constrain(r, -32768, 32767);
    }
    done += chunk;
  }
  return done;
}

bool micWakeActive() { return digitalRead(PIN_MIC_WAKE) == LOW; }

size_t speakerWrite(const uint8_t *pcm, size_t len, uint32_t waitMs) {
  if (!spkBuf || len == 0) return 0;
  len &= ~(size_t)1;
  return xStreamBufferSend(spkBuf, pcm, len, pdMS_TO_TICKS(waitMs));
}

size_t speakerFree() { return spkBuf ? xStreamBufferSpacesAvailable(spkBuf) : 0; }
size_t speakerQueued() { return spkBuf ? xStreamBufferBytesAvailable(spkBuf) : 0; }

void speakerFlush() {
  spkFlushReq = true;
  if (spkBuf) xStreamBufferReset(spkBuf);
}

bool speakerIdle() { return !spkPlaying && speakerQueued() == 0; }

void speakerSetVolume(uint8_t volume) {
  if (volume > 100) volume = 100;
  spkVolume = volume;
  updateGain(volume);
}

uint8_t speakerVolume() { return spkVolume; }

void speakerBeep(uint16_t freqHz, uint16_t ms, uint8_t level) {
  const size_t n = (size_t)AUDIO_SAMPLE_RATE * ms / 1000;
  int16_t block[128];
  float amp = 32767.0f * level / 100.0f;
  size_t i = 0;
  while (i < n) {
    size_t m = min((size_t)128, n - i);
    for (size_t k = 0; k < m; k++) {
      size_t idx = i + k;
      float env = 1.0f;  // rampa de 5 ms para não estalar
      size_t ramp = AUDIO_SAMPLE_RATE / 200;
      if (idx < ramp) env = (float)idx / ramp;
      else if (n - idx < ramp) env = (float)(n - idx) / ramp;
      block[k] = (int16_t)(amp * env * sinf(2.0f * PI * freqHz * idx / AUDIO_SAMPLE_RATE));
    }
    speakerWrite((uint8_t *)block, m * 2, 50);
    i += m;
  }
}
