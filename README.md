# BambuDial

Bambu Lab 3D printer status monitor for the [M5Stack M5Dial](https://docs.m5stack.com/en/core/M5Dial).

Monitor up to 3 Bambu Lab printers from a compact 1.28" round display with rotary encoder. Turn the dial to switch printers. Works without putting printers in LAN-only mode.

## Features

- **Real-time status** — print progress, nozzle/bed temperatures, remaining time, layer count, file name
- **Local MQTT** — connects directly to printers on your network via MQTT over TLS
- **Cloud MQTT** — connects via Bambu Lab cloud with auto-discovery of bound printers
- **Rotary encoder** — turn the dial to switch between printers
- **Color-coded progress arc** — green (printing/done), blue (idle), amber (paused), red (error), purple (preparing)
- **Error messages** — 900+ Bambu error codes resolved to human-readable descriptions
- **TLS secured** — embedded Bambu CA certificates for proper certificate verification

## Hardware

- [M5Stack M5Dial](https://docs.m5stack.com/en/core/M5Dial) — ESP32-S3, 240x240 round GC9A01 LCD, rotary encoder, touch screen

## Prerequisites

- [ESP-IDF v5.4+](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
- USB-C cable for flashing
- Bambu Lab printer(s) on the same network

## Quick Start

### 1. Install ESP-IDF

```bash
mkdir -p ~/esp && cd ~/esp
git clone -b v5.4.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
. ./export.sh
```

> **macOS note:** If you see SSL certificate errors during install, run:
> `"/Applications/Python 3.x/Install Certificates.command"`

### 2. Clone and Configure

```bash
git clone https://github.com/jfearnside/BambuDial.git
cd BambuDial
```

Edit `main/config.h` with your details:

```c
/* WiFi */
#define WIFI_SSID       "YourNetwork"
#define WIFI_PASS       "YourPassword"

/* Printers */
#define NUM_PRINTERS    2
#define PRINTER_CONFIGS { \
    { .name = "X1C",  .ip = "192.168.1.100", .serial = "...", .access_code = "..." }, \
    { .name = "P2S",  .ip = "192.168.1.101", .serial = "...", .access_code = "..." }, \
}
```

**Where to find printer details:**

| Field | Where to find it |
|-------|-----------------|
| IP address | Printer LCD > Network, or your router's DHCP client list |
| Serial number | Printer LCD > Device Info, or Bambu Studio |
| Access code | Printer LCD > Network > LAN Access Code |

You can also find serial numbers and access codes in Bambu Studio's config file:
`~/Library/Application Support/BambuStudio/BambuStudio.conf` (look for `access_code` and `dev_id` keys).

### 3. Build and Flash

```bash
. ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbmodem* flash monitor
```

Press `Ctrl+]` to exit the serial monitor.

## Cloud Mode

To use cloud mode instead of local connections:

1. Set `CONNECTION_MODE` to `MODE_CLOUD` in `config.h`
2. Set `BAMBU_EMAIL` and `BAMBU_PASSWORD`
3. Printers are auto-discovered from your Bambu Lab account — no IP/serial needed

If your account uses 2FA or email verification, set `BAMBU_TOKEN` directly with a JWT token obtained from Bambu Studio logs or browser dev tools.

## Display Layout

```
        ┌─────────────┐
       ╱  Printer Name ●╲      ● = connection indicator
      │      1/2         │      (green=connected, red=disconnected)
      │                  │
      │    PRINTING      │      Status (color-coded)
      │      73%         │      Progress percentage
      │                  │
      │  N:210/210  B:60/60     Nozzle / Bed temps
      │  benchy.3mf      │      File name (scrolling)
      │    1h 23m left   │      Remaining time
       ╲   Layer 42/100 ╱       Layer progress
        └─────────────┘
   ════════════════════════     Progress arc (outer ring)
```

## Project Structure

```
BambuDial/
├── CMakeLists.txt              # ESP-IDF project file
├── sdkconfig.defaults          # Build configuration
├── main/
│   ├── CMakeLists.txt          # Component build file
│   ├── idf_component.yml       # M5Dial BSP dependency
│   ├── config.h                # User configuration (WiFi, printers)
│   ├── main.c                  # App entry: WiFi, encoder, mode selection
│   ├── bambu_mqtt.h/.c         # Local MQTT connection + JSON parsing
│   ├── bambu_cloud.h/.c        # Cloud auth + cloud MQTT connection
│   ├── printer_state.h/.c      # Thread-safe printer state management
│   ├── ui.h/.c                 # LVGL round display UI
│   ├── error_lookup.h/.c       # HMS error code lookup
│   ├── error_lookup.tsv        # 900+ error code database
│   └── certs/
│       └── bambu_ca_bundle.pem # Bambu Lab TLS CA certificates
```

## Technical Notes

- **Single MQTT connection** — Only one printer is connected at a time to fit within M5Dial's 204KB RAM. Rotating the encoder disconnects from the current printer and connects to the next.
- **Message reassembly** — Bambu printers send large JSON payloads (up to 23KB for X1C). Messages are reassembled from MQTT fragments before parsing.
- **Incremental updates** — P1/A1/P2S series printers send only changed fields. The state struct merges updates incrementally.
- **No PSRAM** — The M5Dial has no external RAM, so memory is carefully managed (48KB LVGL, 16KB MQTT buffer, 24KB reassembly buffer).
- **No LAN-only mode required** — Local MQTT works while the printer remains cloud-connected with full Bambu Studio functionality.

## Dependencies

Managed automatically via ESP-IDF component manager:

- [espressif/m5dial](https://components.espressif.com/components/espressif/m5dial) — BSP (display, touch, encoder)
- LVGL — Graphics library (pulled in by BSP)
- esp_mqtt — MQTT client (built into ESP-IDF)
- cJSON — JSON parser (built into ESP-IDF)
- mbedTLS — TLS (built into ESP-IDF)

## Credits

- Bambu CA certificates and error database from [PrintSphere](https://github.com/cptkirki/PrintSphere)
- MQTT protocol details from [OpenBambuAPI](https://github.com/Doridian/OpenBambuAPI)

## License

MIT
