// Tessela Assistente — configuração da placa "Assistente IA" v1.2 (ESP32-S3-WROOM-1-N16R8).
// Os pinos seguem o esquemático (Assistente_IA.kicad_sch / LEIA-ME.md da placa).
#pragma once

#include <Arduino.h>

#define FIRMWARE_VERSION "1.0.0"
#define DEVICE_MODEL "tessela-assistente-esp32s3"

// ------------------------------------------------------------------ áudio
#define AUDIO_SAMPLE_RATE 16000      // microfones, AFE, WebSocket e altifalante (protocolo v1: 16 kHz)

// I2S0: MIC1 (L) + MIC2 (R), ICS-43434 — par estéreo usado pelo AFE (AEC + BSS + NS + VAD + WakeNet)
#define PIN_MIC_SCK 4
#define PIN_MIC_WS 5
#define PIN_MIC_SD 6
// Os ICS-43434 entregam 24 bits num slot de 32; >> 14 dá PCM16 com +12 dB de ganho digital.
#define MIC_SHIFT 14

// I2S1: amplificador MAX98357A (TX). MIC3/MIC4 (RX no mesmo I2S1, pino 18) ficam reservados.
#define PIN_I2S1_BCLK 15
#define PIN_I2S1_WS 16
#define PIN_AMP_DIN 17
#define PIN_MIC34_SD 18
#define PIN_AMP_SD 7                 // HIGH = amplificador ligado (canal esquerdo); LOW = desligado

#define SPEAKER_BUFFER_BYTES (64 * 1024)  // ~2 s a 16 kHz (o servidor envia no máximo 0,6 s à frente)
#define VOLUME_DEFAULT 70                // 0..100
#define VOLUME_STEP 10

// ------------------------------------------------------------------ cartão microSD (SDMMC 4 bits)
#define PIN_SD_CLK 39
#define PIN_SD_CMD 38
#define PIN_SD_D0 40
#define PIN_SD_D1 41
#define PIN_SD_D2 42
#define PIN_SD_D3 47
#define PIN_SD_DET 13                // LOW = cartão presente
#define SD_ROOT "/tessela"

// ------------------------------------------------------------------ RTC DS3231 (I2C)
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9
#define PIN_RTC_INT 11
#define DS3231_ADDR 0x68

// ------------------------------------------------------------------ interface
#define PIN_BTN_VOLUP 2
#define PIN_BTN_VOLDN 21
#define PIN_BTN_MUTE 14
#define PIN_LED_STATUS 12
#define PIN_LED_RGB 48               // anel WS2812 (12) + barra frontal (6), em cadeia a partir de J5
#define LED_RING_COUNT 12
#define LED_BAR_COUNT 6
#define PIN_VIN_SENSE 10             // divisor 100k/10k: VIN = leitura × 11
#define BUTTON_DEBOUNCE_MS 30
#define BUTTON_LONG_MS 5000          // MUTE premido 5 s = modo de configuração (ponto de acesso Wi-Fi)

// ------------------------------------------------------------------ conversa (docs/DEVICE_PROTOCOL.md)
#define PROTOCOL_VERSION 1
#define WS_FRAME_MS 32               // o AFE entrega blocos de 32 ms (512 amostras); enviados assim
#define FOLLOWUP_WINDOW_MS 8000      // depois de responder, continua a ouvir 8 s sem palavra de activação
#define CONVERSATION_MAX_MS 300000   // 5 min de conversa contínua no máximo
#define APP_PING_INTERVAL_MS 60000   // `ping` da aplicação: a sessão não fecha por inactividade
#define RECONNECT_MIN_MS 1000
#define RECONNECT_MAX_MS 60000

// ------------------------------------------------------------------ agenda de mensagens de voz
#define SCHEDULE_POLL_MS (5UL * 60UL * 1000UL)  // pergunta ao servidor a cada 5 min (ETag → 304)
#define NTP_RESYNC_MS (6UL * 3600UL * 1000UL)

// ------------------------------------------------------------------ palavra de activação
// O modelo incluído no Arduino-ESP32 é "Hi ESP" (wn9_hiesp). Um nome próprio exige um modelo
// WakeNet treinado e o srmodels.bin regenerado (ver README.md, "Palavra de activação").
#define WAKENET_KEYWORD "hiesp"

// ------------------------------------------------------------------ TLS
// Certificado da CA raiz do servidor (PEM). Se o cartão tiver /tessela/ca.pem, usa esse.
// Sem CA e com https:// a ligação é cifrada mas NÃO verifica o servidor (aviso no registo).
inline constexpr const char *TESSELA_ROOT_CA = nullptr;
