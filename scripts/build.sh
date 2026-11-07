#!/usr/bin/env bash
# Compila (e opcionalmente grava) o firmware com o arduino-cli.
#   ./scripts/build.sh                  # só compila
#   ./scripts/build.sh /dev/ttyACM0     # compila e grava na placa (USB-C)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI="${ARDUINO_CLI:-arduino-cli}"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,FlashMode=qio,PartitionScheme=esp_sr_16,USBMode=hwcdc,CDCOnBoot=cdc"
LIBDIR="$ROOT/.arduino/user"
WSLIB="$LIBDIR/libraries/WebSockets"

"$CLI" core list | grep -q "esp32:esp32" || "$CLI" core install esp32:esp32@3.0.5
# WebSockets 2.6.1 só para este projecto (a 2.5.x não compila com o Arduino-ESP32 3.x)
[ -d "$WSLIB" ] || ARDUINO_DIRECTORIES_USER="$LIBDIR" "$CLI" lib install "WebSockets@2.6.1"
"$CLI" lib list | grep -q ArduinoJson || "$CLI" lib install "ArduinoJson@7.2.1"

"$CLI" compile --fqbn "$FQBN" --library "$WSLIB" --build-path "$ROOT/build" --warnings default "$ROOT/tessela_assistente"

if [ "${1:-}" != "" ]; then
  # A partição esp_sr_16 grava também o srmodels.bin (modelos de voz) em 0xD10000.
  "$CLI" upload --fqbn "$FQBN" --input-dir "$ROOT/build" -p "$1" "$ROOT/tessela_assistente"
fi
