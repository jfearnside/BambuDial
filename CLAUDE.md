# CLAUDE.md — BambuDial Development Guide

## Project Overview

BambuDial is an ESP-IDF (v5.4+) C project targeting the M5Stack M5Dial (ESP32-S3, no PSRAM, 8MB flash). It monitors Bambu Lab 3D printers via MQTT and displays status on a 240x240 round LCD.

## Build Commands

```bash
# Source ESP-IDF environment (required each terminal session)
. ~/esp/esp-idf/export.sh

# Build
idf.py build

# Flash and monitor (Ctrl+] to exit monitor)
idf.py -p /dev/tty.usbmodem* flash monitor

# Full clean rebuild (needed when sdkconfig.defaults changes)
idf.py fullclean && idf.py set-target esp32s3 && idf.py build
```

## Architecture

### Module Responsibilities

- **main.c** — App entry point. Initializes NVS, WiFi, BSP display, encoder (via raw `iot_knob` API), and starts MQTT in the configured mode (local or cloud).
- **bambu_mqtt.c** — Local MQTT. One connection at a time (switched by encoder). Handles TLS with embedded Bambu CA certs, MQTT message reassembly for fragmented payloads, and JSON parsing of printer reports.
- **bambu_cloud.c** — Cloud MQTT. Authenticates via Bambu Lab REST API (email/password or direct JWT token), fetches bound printers, extracts MQTT username from JWT, connects to cloud MQTT broker.
- **printer_state.c** — Thread-safe printer state management. Mutex-protected struct array. Encoder rotation cycles `selected` index.
- **ui.c** — LVGL-based round display UI. 1-second update timer reads printer state and refreshes all widgets. Progress arc, temps, status text, file name, error messages.
- **error_lookup.c** — Searches embedded TSV database for Bambu print error codes. Returns human-readable error messages.

### Key Design Decisions

- **Single MQTT connection**: M5Dial has only 204KB RAM. Two simultaneous TLS connections (~16KB each for buffers) plus LVGL and WiFi exhaust available memory. Solution: connect to one printer at a time, switch on encoder rotation.
- **24KB reassembly buffer**: X1C sends ~23KB pushall responses. P2S sends smaller incremental updates. Buffer sized for the largest known response.
- **Raw knob API**: LVGL's encoder input device didn't reliably deliver key events to widgets. Using `iot_knob` callbacks directly for encoder rotation works reliably.
- **Embedded files**: CA cert bundle and error TSV are embedded via `EMBED_FILES`/`EMBED_TXTFILES` in CMakeLists.txt, accessed via `_binary_*_start`/`_binary_*_end` symbols.

### Threading Model

- **Main task**: Initializes everything, then sleeps.
- **LVGL task**: Created by BSP, runs the display loop. UI updates via `lv_timer` (1s interval).
- **MQTT task**: Created by esp_mqtt internally. Calls event handler on connect/disconnect/data.
- **Knob timer**: Created by `iot_knob`, fires callbacks on rotation events.
- **Synchronization**: `printer_manager_t.mutex` protects all printer state reads/writes.

### Bambu MQTT Protocol

- **Local**: `mqtts://<printer_ip>:8883`, username `bblp`, password = LAN access code
- **Cloud**: `mqtts://us.mqtt.bambulab.com:8883`, username `u_<uid>` (from JWT), password = access token
- **Topics**: Subscribe `device/<serial>/report`, publish to `device/<serial>/request`
- **Pushall**: Send `{"pushing":{"sequence_id":"0","command":"pushall"}}` after connect to get full state
- **Key JSON fields** in `print` object: `gcode_state`, `mc_percent`, `mc_remaining_time`, `nozzle_temper`, `nozzle_target_temper`, `bed_temper`, `bed_target_temper`, `subtask_name`, `layer_num`, `total_layer_num`, `print_error`

## Memory Constraints

The M5Dial has no PSRAM. Budget (204KB total):

| Component | Approximate Usage |
|-----------|------------------|
| WiFi + LWIP | ~50KB |
| mbedTLS (1 TLS conn) | ~35KB |
| LVGL | 48KB (configurable) |
| MQTT buffers | 16KB in + 1KB out |
| Reassembly buffer | 24KB (heap allocated) |
| Stack + other | ~30KB |

If adding features, watch `heap_caps_get_free_size(MALLOC_CAP_DEFAULT)` — anything below 10KB free risks instability.

## Configuration

All user config is in `main/config.h`. This file is committed with placeholder values. Users copy and edit with real credentials. The `.gitignore` excludes `config.h.local` for keeping a personal backup.

## Adding Error Codes

The error database is `main/error_lookup.tsv` — tab-separated with format: `E<TAB>hex_code<TAB>models<TAB>message`. Only domain `E` (print errors) is included to save flash space. The full database with HMS codes is in PrintSphere's source if needed.

## Flash Size

Binary is ~1.4MB in 1.5MB partition (4% free). Be cautious adding large embedded resources. The error TSV was trimmed from 700KB to 108KB by excluding HMS codes.
