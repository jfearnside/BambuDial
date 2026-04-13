# BambuDial — Bambu Lab Printer Monitor for M5Dial

A compact printer status monitor for [Bambu Lab](https://bambulab.com) 3D printers, running on the [M5Stack M5Dial](https://docs.m5stack.com/en/core/M5Dial) (ESP32-S3 with 1.28" round display and rotary encoder).

## Features

- **Real-time monitoring** of up to 3 Bambu Lab printers
- **Local MQTT** — connects directly to printers on your network (no LAN-only mode required)
- **Cloud MQTT** — connects via Bambu Lab cloud with auto-discovery of bound printers
- **Round display UI** — progress arc, temperatures, print state, file name, layer info
- **Rotary encoder** — turn the dial to switch between printers
- **Color-coded status** — green (printing), blue (idle), amber (paused), red (error), purple (preparing)
- **Error lookup** — 900+ Bambu error codes resolved to human-readable messages
- **TLS secured** — uses embedded Bambu CA certificates

## Hardware

- [M5Stack M5Dial](https://docs.m5stack.com/en/core/M5Dial) (ESP32-S3, 240x240 round LCD, rotary encoder)

## Setup

### 1. Install ESP-IDF v5.4+

```bash
mkdir -p ~/esp && cd ~/esp
git clone -b v5.4.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
. ./export.sh
```

### 2. Configure

Copy `main/config.h` and fill in your details:

- **WiFi** credentials
- **Printer details** (IP, serial number, LAN access code)
  - IP: check your router or printer LCD under Network
  - Serial: printer LCD or Bambu Studio
  - Access code: printer LCD → Network → LAN Access Code

For **cloud mode**, set `CONNECTION_MODE` to `MODE_CLOUD` and provide your Bambu Lab email/password (or JWT token for 2FA accounts).

### 3. Build and Flash

```bash
. ~/esp/esp-idf/export.sh
cd BambuDial
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbmodem* flash monitor
```

## Usage

- **Rotate dial** — switch between printers
- Display shows: printer name, connection status, print progress, nozzle/bed temps, file name, remaining time, layer count

## Architecture

- **ESP-IDF** with [espressif/m5dial BSP](https://components.espressif.com/components/espressif/m5dial)
- **LVGL** for round display UI
- **esp_mqtt** for TLS MQTT connections
- **cJSON** for parsing Bambu printer reports
- Single active MQTT connection (switches on encoder rotation) to fit in M5Dial's 204KB RAM

## Credits

- Bambu CA certificates and error database sourced from [PrintSphere](https://github.com/cptkirki/PrintSphere)
- MQTT protocol details from [OpenBambuAPI](https://github.com/Doridian/OpenBambuAPI)

## License

MIT
