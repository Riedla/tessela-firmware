# Tessela Assistente — firmware (ESP32-S3)

Firmware da placa **Assistente IA v1.4** (ESP32-S3-WROOM-1-N16R8, 4 × TDK T5838 PDM, MAX98357A, microSD,
RTC DS3231, anel WS2812, **bateria LiPo com carregamento por USB-C**). Liga-se à **Tessela API**
(`tessela-api`) e faz duas coisas:

1. **Conversa em tempo real com o agente Tessela.** Chama-se pelo nome ("Hi ESP"), fala-se
   normalmente e a resposta sai pelo altifalante enquanto é gerada. Pode-se interromper a
   resposta a falar e, durante 8 s depois de cada resposta, continuar a conversa sem repetir o nome.
2. **Mensagens de voz agendadas.** As gravações feitas na app (`voice_message`) ficam no cartão
   microSD e tocam à hora marcada, **mesmo sem internet** (relógio RTC com pilha).

Protocolo: [`tessela-api/docs/DEVICE_PROTOCOL.md`](../tessela-api/docs/DEVICE_PROTOCOL.md)
(v1 + §14 mensagens de voz + §15 estado da bateria). Compila com **Arduino-ESP32 3.0.5**: 1,5 MB de 3 MB, com espaço para
OTA.

> **Estado:** compila sem erros nem avisos no nosso código. Ainda **não foi testado em hardware**
> (a placa não está fabricada). Ao receber a placa, seguir os testes por fases do fim deste ficheiro.

## Como funciona

```
 MIC1 ─┐                       ┌─► WakeNet "Hi ESP" ─────────► começa a conversa
 MIC2 ─┼─► AFE (ESP-SR) ───────┼─► VAD local
 eco  ─┘   AEC · BSS · NS      └─► áudio limpo 16 kHz ──► wss://…/v1/realtime ──► agente (STT → Claude → TTS)
  ▲                                                                     │
  └──────── referência ◄── altifalante ◄── buffer 64 KB ◄── voz da resposta (PCM16 16 kHz)

 RTC DS3231 ──► agendador ──► /tessela/audio/<etag>.wav (microSD) ──► altifalante
 GET /v1/hardware/me/schedule (ETag, a cada 5 min) ──► schedule.json + WAVs no cartão
```

- **Cancelamento de eco (AEC):** a cópia exata do que vai para o altifalante entra no AFE como
  referência. Por isso o firmware pede ao servidor `vadBargeIn: true`: a pessoa pode interromper a
  resposta a falar.
- **Fim da fala:** quem decide é o servidor (`vad: "server"`), com o áudio já limpo.
- **Ligação permanente:** o WebSocket fica aberto (ping a cada 60 s), por isso não há o atraso
  do TLS depois da palavra de ativação. Se a ligação ainda estiver a abrir, o AFE guarda ~1 s de
  áudio.

### Tarefas FreeRTOS

| Tarefa | Núcleo | O que faz |
|---|---|---|
| `afe_feed` | 0 | lê MK1/MK2 (PDM no I2S0, 16 bits) + referência e alimenta o AFE em blocos de 32 ms |
| `afe_fetch` | 1 | palavra de ativação, VAD, nível; entrega o áudio limpo ao WebSocket |
| `altifalante` | 1 | buffer → volume → I2S1 (MAX98357A) ao ritmo real; copia para a referência |
| `loop()` | 1 | Wi-Fi, WebSocket, máquina de estados, agendador, botões, LEDs, consola |

### Estados

`em_espera` → (palavra) → `a_ouvir` → (`vad.end`) → `a_pensar` → (`audio.start`) → `a_falar` →
(fim da voz) → `seguimento` (8 s) → `em_espera`. Também: `mensagem` (agendada), `silencio`
(microfones desligados), `configuracao`, `erro`.

## Ligações (placa Assistente IA v1.2)

| Função | GPIO | | Função | GPIO |
|---|---|---|---|---|
| PDM CLK · THSEL · DATA0 (MK1/MK2) | 4 · 5 · 6 | | microSD CLK · CMD | 39 · 38 |
| I2S1 BCLK · WS (amplificador) | 15 · 16 | | microSD D0 · D1 · D2 · D3 | 40 · 41 · 42 · 47 |
| Amplificador DIN · SD_MODE | 17 · 7 | | Deteção do cartão | 13 |
| PDM DATA1 (MK3/MK4, reservado) · WAKE | 18 · 1 | | RTC SDA · SCL · INT | 8 · 9 · 11 |
| Botões VOL+ · VOL− · MUTE | 2 · 21 · 14 | | Anel WS2812 (J5) | 48 |
| LED de estado | 12 | | Carregador ligado (PGOOD, LOW) | 10 |
| Medidor de bateria LC709203F | I2C 0x0B (GPIO 8/9) | | | |

Os pinos estão todos em `tessela_assistente/config.h`.

## Compilar e gravar

Precisa do Arduino IDE 2 (traz o `arduino-cli`) ou do `arduino-cli`, com a plataforma
**esp32:esp32 3.0.5** e a biblioteca **ArduinoJson 7.x**. O script instala a **WebSockets 2.6.1**
só dentro deste projeto (pasta `.arduino/`), sem mexer nas bibliotecas globais. A versão 2.5.x
não compila com o Arduino-ESP32 3.x.

```powershell
.\scripts\build.ps1               # compila
.\scripts\build.ps1 -Port COM5    # compila e grava pela USB-C
```

```bash
./scripts/build.sh                # compila
./scripts/build.sh /dev/ttyACM0   # compila e grava
```

Opções da placa (FQBN): `esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,FlashMode=qio,PartitionScheme=esp_sr_16,USBMode=hwcdc,CDCOnBoot=cdc`.
A partição **`esp_sr_16`** é obrigatória: grava os modelos de voz (`srmodels.bin`) na partição
`model`. Primeira gravação: carregar em BOOT, tocar em RESET e largar o BOOT.

No Arduino IDE: placa "ESP32S3 Dev Module", PSRAM "OPI PSRAM", Flash 16 MB, partição
"ESP SR 16M", USB CDC On Boot "Enabled", e em Sketchbook a pasta `.arduino/user` (ou
instalar a WebSockets 2.6.1).

## Primeira configuração

1. **Na app Tessela:** criar o dispositivo físico (`POST /v1/hardware`). Aparece um código como
   `K7PM-Q2XD`, válido 10 minutos.
2. **No assistente** (sem configuração, o anel pisca a azul): ligar o telemóvel à rede Wi-Fi
   **`Tessela-XXXX`**. Abre-se a página de configuração; se não abrir, ir a `http://192.168.4.1`.
   Preencher a rede Wi-Fi, o servidor (`https://api…`) e o código, e carregar em **Guardar e ligar**.
3. O assistente emparelha (`POST /v1/hardware/pair`), guarda o token na NVS, reinicia, liga ao
   tempo real e toca dois tons. Está pronto: dizer **"Hi ESP"** e fazer a pergunta.

Também pela porta série (115200 baud, USB-C):

```
wifi A-Minha-Rede a-palavra-passe
server https://api.exemplo.co.mz
pair K7PM-Q2XD
status
```

Outros comandos: `volume 0-100`, `sync` (descarregar a agenda já), `setup` (abrir o ponto de
acesso), `forget` (apagar o token), `reboot`.

**Voltar a configurar:** segurar o **MUTE durante 5 s** abre o ponto de acesso `Tessela-XXXX`.

### TLS

Em produção a API está em `https://`/`wss://`. Para o dispositivo **verificar** o servidor, há
duas opções: pôr o certificado da CA raiz (PEM, ex.: ISRG Root X1 do Let's Encrypt) no cartão em
**`/tessela/ca.pem`**, ou colá-lo em `TESSELA_ROOT_CA` (`config.h`). Sem CA, a ligação vai cifrada
mas **sem verificar** o servidor, e o registo avisa. Isso só serve para desenvolvimento.

## Utilização

| Ação | Resultado |
|---|---|
| "Hi ESP" + pergunta | conversa com o agente; o anel acende a laranja |
| Falar durante a resposta | interrompe a resposta (barge-in) e faz outra pergunta |
| Nos 8 s a seguir à resposta | continuar a conversa sem dizer "Hi ESP" |
| **−** / **+** | volume (o anel mostra o nível durante 1,5 s) |
| **MUTE** (curto) | liga/desliga os microfones (anel vermelho). Durante uma conversa: termina-a. Com uma mensagem por ouvir: toca-a |
| **MUTE** (5 s) | modo de configuração (ponto de acesso) |
| **−** e **+** juntos | mostra a carga da bateria no anel durante 3 s |
| Interruptor lateral | liga/desliga o assistente (a bateria carrega mesmo desligado) |

**Anel e barra de luz:**
- laranja a respirar: a ouvir;
- laranja a rodar: a pensar;
- laranja a pulsar: a falar;
- âmbar: mensagem agendada (a piscar quando está à espera de ser ouvida);
- vermelho fixo: microfones desligados;
- azul a piscar: configuração;
- vermelho a piscar: erro.

O LED verde de estado acende quando está ligado ao servidor.

## Bateria

A placa (v1.3 em diante) tem uma bateria **LiPo 3,7 V 3000 mAh** (tipo 605080, com proteção e ficha JST-PH 2,0)
no fundo da caixa, por baixo da PCB.

- **Carregamento:** pelo **USB-C** (5 V), a 1 A, pelo BQ24074. O assistente funciona enquanto carrega.
  O LED laranja ao lado do USB-C fica aceso enquanto carrega, mesmo com o assistente desligado.
  A carga completa demora cerca de 3,5 h.
- **Autonomia:** cerca de **20–24 h** à escuta da palavra de ativação com o Wi-Fi ligado. Conversar
  e tocar alto gasta mais.
- **No anel:** **−** e **+** juntos mostram a carga durante 3 s: verde ≥ 50 %, âmbar ≥ 20 %, vermelho
  abaixo disso. Ao ligar o cabo, toca um som duplo e o anel mostra a carga. Enquanto carrega, a barra
  frontal respira a verde.
- **Bateria fraca:**
  - a 20 % e a 10 %: dois tons e o nível no anel; em espera, a barra pisca a vermelho a cada 5 s;
  - abaixo de 10 %: o volume fica limitado a 60, para os picos do altifalante não fazerem a tensão cair;
  - a 5 %: três tons a descer;
  - a 3 % (ou 3,3 V): o assistente entra em **deep sleep** e acorda sozinho quando se liga o cabo.
- **Na app:** o firmware envia o estado (`PUT /v1/hardware/me/status`: %, mV, a carregar, cabo
  ligado, sinal do Wi-Fi). Envia no arranque, a cada 5 min e sempre que algo muda. A app mostra-o em
  `device.power`.
- **Consola série:** `status` mostra a bateria.
- **Outra bateria:** mudar `BATTERY_APA` em `config.h`. A tabela do LC709203F tem 0x2D para
  2000 mAh e 0x36 para 3000 mAh. Usar sempre uma bateria **com circuito de proteção**.
  **Confirmar a polaridade** da ficha: o + vai para o pino marcado "BAT +" (J7).

## Mensagens de voz agendadas

- **Sincronização:** a cada 5 minutos (e no arranque), `GET /v1/hardware/me/schedule` com
  `If-None-Match`. Sem alterações, o servidor responde `304` e não se gastam dados.
- **Áudios:** chegam em WAV PCM16 mono 16 kHz (`GET /v1/hardware/me/audio/<id>`) e ficam em
  `/tessela/audio/<etag>.wav`. Só se descarrega o que muda. Cada ficheiro é gravado primeiro como
  temporário e depois renomeado, por isso um corte de luz nunca deixa um WAV a meio.
- **Sem internet:** a agenda fica em `/tessela/schedule.json` e continua a tocar.
- **Relógio:** o RTC guarda a hora em UTC e o NTP acerta-o a cada 6 h. O fuso vem da agenda
  (`Africa/Maputo` → `CAT-2`); ver `rtc_clock.cpp`.
- **Regras de reprodução:**
  - `autoPlay: true`: dois tons e a mensagem.
  - `autoPlay: false`: um tom e o anel a piscar a âmbar; um toque no MUTE toca a mensagem.
  - Durante uma conversa, a mensagem espera e toca a seguir.
  - `scheduleEnabled: false` (definição do dispositivo na app) desliga a agenda.

## Palavra de ativação (nome do assistente)

O Arduino-ESP32 traz um único modelo WakeNet: **"Hi ESP"** (`wn9_hiesp`), que funciona logo.
Para um **nome próprio** (ex.: "Olá Nexus") há dois caminhos:

1. **WakeNet personalizado da Espressif.** É treinado pela Espressif (serviço pago) ou com o
   processo do ESP-SR. Depois gera-se o `srmodels.bin` com o modelo novo, muda-se
   `WAKENET_KEYWORD` em `config.h` e grava-se.
2. **Modelos de outros nomes já incluídos no ESP-SR** (ex.: `jarvis`, `sophia`, `computer`).
   Exige compilar o `srmodels.bin` com ESP-IDF e escolher o modelo no `menuconfig`.

Escolher um nome de 3–4 sílabas, pouco comum no dia a dia, reduz as ativações falsas.

## Microfones

A placa v1.4 tem **4 × TDK T5838**, microfones digitais **PDM** a **1,8 V** (regulador U9). Ligam ao
ESP32-S3 através dos tradutores de nível U10 (CLK, THSEL, DATA0, DATA1) e U11 (WAKE).

- **Relógio:** o PDM corre no I2S0 com downsampling × 128, ou seja CLK = 2,048 MHz. É o modo
  "High Quality" do T5838, que precisa de 2,0–3,7 MHz. O ESP-IDF usa × 64 por omissão, o que dava 1,024 MHz:
  um relógio fora dos modos do microfone. Por isso o firmware usa o driver `i2s_pdm` do ESP-IDF
  e não o `ESP_I2S`.
- **Canais:** a linha DATA0 leva o MK2 (esquerda, SELECT = 1,8 V) e o MK1 (direita, SELECT = GND).
- **Processamento:** o AFE usa **2 microfones + 1 referência** (AEC, BSS, NS, WakeNet).
- **MK3/MK4 (DATA1, GPIO 18):** ficam ligados mas reservados. O ESP32-S3 lê até 4 linhas PDM.
- **Ganho:** o T5838 tem −41 dBFS de sensibilidade, por isso o firmware aplica +18 dB
  (`MIC_GAIN_SHIFT` em `config.h`).
- **Acordar com som (AAD):** o MK1 tem o THSEL (GPIO 5) e o WAKE (GPIO 1, via U11) ligados. Assim, o T5838
  pode detetar som sozinho, com cerca de 20 µA, e acordar o ESP32. O firmware deixa o THSEL em LOW, ou
  seja, a função está desligada. Ativá-la (protocolo de configuração pelo THSEL, datasheet DS-000383 §5)
  fica para uma versão seguinte, para poupar bateria em espera.

## Testes por fases (quando a placa chegar)

1. **Arranque:** `status` na consola série. Confirmar memória livre, `cartao ok`, `rtc ok`, a hora e a
   `bateria` (% e mV).
   Antes de ligar a bateria pela primeira vez, medir os 3,3 V só com o USB-C (sem bateria).
2. **Altifalante:** `volume 60` deve tocar um tom.
3. **Wi-Fi e emparelhamento:** pela página `Tessela-XXXX`. Na app, o dispositivo passa a `active`.
4. **Microfones e palavra:** medir 1,8 V na saída do U9 e 2,048 MHz no CLK (pino 6 de um microfone). "Hi ESP" deve
   dar um tom e o anel laranja. Se não reagir, ajustar o ganho em `MIC_GAIN_SHIFT`.
5. **Conversa:** perguntar "o que tenho hoje?". Medir a latência no registo do servidor
   (`reply.end.latency`).
6. **Barge-in:** interromper a resposta a falar.
7. **Agenda:** criar uma mensagem na app para daqui a 2 minutos, depois `sync`, e esperar que toque.
8. **Sem internet:** desligar o router e confirmar que a mensagem seguinte toca na mesma.
9. **Bateria:** ligar e desligar o USB-C (som, anel, LED laranja). Depois deixar descarregar sem cabo
   e confirmar os avisos a 20 % e 10 % e o deep sleep. Ao ligar o cabo, deve voltar a arrancar.

## Estrutura

```
tessela_assistente/
  tessela_assistente.ino  arranque + máquina de estados
  config.h                pinos e constantes
  app_state.*             estado partilhado
  audio_io.*              I2S dos microfones e do altifalante, volume, referência do AEC
  voice_frontend.*        AFE do ESP-SR (AEC, BSS, NS, VAD, WakeNet)
  afe_config.c            AFE_CONFIG_DEFAULT (macro só para C)
  realtime.*              WebSocket /v1/realtime (protocolo v1)
  api_client.*            emparelhamento, /me, agenda e downloads
  scheduler.*             agenda no cartão, relógio, reprodução dos WAV
  rtc_clock.*             DS3231 + NTP + fuso horário
  storage.*               NVS (definições, token) e microSD
  battery.*               medidor LC709203F, carregador (PGOOD), avisos e deep sleep
  provisioning.*          ponto de acesso + página + consola série
  buttons.*, leds.*       botões e anel de luz WS2812 (RMT)
scripts/build.ps1, build.sh
```
