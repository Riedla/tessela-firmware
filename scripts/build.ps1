# Compila (e opcionalmente grava) o firmware com o arduino-cli.
#   .\scripts\build.ps1                 # só compila
#   .\scripts\build.ps1 -Port COM5      # compila e grava na placa (USB-C)
param(
    [string]$Port = "",
    [string]$Cli = ""
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Fqbn = "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,FlashMode=qio,PartitionScheme=esp_sr_16,USBMode=hwcdc,CDCOnBoot=cdc"
$LibDir = Join-Path $Root ".arduino\user"
$WsLib = Join-Path $LibDir "libraries\WebSockets"

if (-not $Cli) {
    $cmd = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($cmd) { $Cli = $cmd.Source }
    elseif (Test-Path "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe") {
        $Cli = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
    } else { throw "arduino-cli nao encontrado (instala o Arduino IDE 2 ou o arduino-cli)" }
}

# Plataforma: Arduino-ESP32 3.0.x
if (-not (& $Cli core list | Select-String "esp32:esp32")) { & $Cli core install esp32:esp32@3.0.5 }

# WebSockets 2.6.1 só para este projecto (a 2.5.x não compila com o Arduino-ESP32 3.x)
if (-not (Test-Path $WsLib)) {
    $env:ARDUINO_DIRECTORIES_USER = $LibDir
    & $Cli lib install "WebSockets@2.6.1"
    Remove-Item Env:\ARDUINO_DIRECTORIES_USER
}
if (-not (& $Cli lib list | Select-String "ArduinoJson")) { & $Cli lib install "ArduinoJson@7.2.1" }

$Build = Join-Path $Root "build"
& $Cli compile --fqbn $Fqbn --library $WsLib --build-path $Build --warnings default (Join-Path $Root "tessela_assistente")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Port) {
    # A partição esp_sr_16 grava também o srmodels.bin (modelos de voz) em 0xD10000.
    & $Cli upload --fqbn $Fqbn --input-dir $Build -p $Port (Join-Path $Root "tessela_assistente")
}
