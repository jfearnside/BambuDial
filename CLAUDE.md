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

- **main.c** — App entry point. Initializes NVS, WiFi, SNTP, BSP display, encoder (via raw `iot_knob` API), front button, and starts MQTT in the configured mode (local or cloud). Manages auto-rotate logic for cycling between active printers.
- **bambu_mqtt.c** — Local MQTT. One connection at a time (switched by encoder or auto-rotate). Handles TLS with embedded Bambu CA certs, MQTT message reassembly for fragmented payloads, and JSON parsing of printer reports.
- **bambu_cloud.c** — Cloud MQTT. Authenticates via Bambu Lab REST API (email/password or direct JWT token), fetches bound printers, extracts MQTT username from JWT, connects to cloud MQTT broker.
- **bambu_parse.h** — Shared inline parser for AMS filament tray data, humidity, and print stage from MQTT JSON. Used by both bambu_mqtt.c and bambu_cloud.c.
- **printer_state.c** — Thread-safe printer state management. Mutex-protected struct array with AMS tray data, filament types, humidity, print stage. Encoder rotation cycles `selected` index.
- **ui.c** — LVGL-based round display UI. 1-second update timer reads printer state and refreshes all widgets. Progress arc, temps with MDI icons (color-coded by heating state), rotating info pages (time/ETA/AMS/stage), error messages.
- **error_lookup.c** — Searches embedded TSV database for Bambu print error codes. Returns human-readable error messages.

### Key Design Decisions

- **Single MQTT connection**: M5Dial has only 204KB RAM. Two simultaneous TLS connections (~35KB each for mbedTLS buffers) plus LVGL and WiFi exhaust available memory. Solution: connect to one printer at a time, switch on encoder rotation. Auto-rotate briefly polls each printer to check its state.
- **24KB reassembly buffer**: X1C sends ~23KB pushall responses. P2S sends ~18KB. Buffer sized for the largest known response.
- **Raw knob API**: LVGL's encoder input device didn't reliably deliver key events to widgets. Using `iot_knob` callbacks directly for encoder rotation works reliably.
- **Front button dual-purpose**: Dismisses DONE/FAILED state (with `dismissed` flag to prevent MQTT from reverting), or cycles info pages when printer is active.
- **Rotating info pages**: Bottom 3 lines cycle every 5 seconds: page 0 (time remaining + layer), page 1 (ETA + layer), page 2 (AMS filament + humidity + print stage). Page 2 only shown for printers with AMS.
- **Embedded files**: CA cert bundle, error TSV, and MDI icon fonts are embedded via `EMBED_FILES`/`EMBED_TXTFILES` in CMakeLists.txt.

### Threading Model

- **Main task**: Initializes everything, then runs auto-rotate polling loop.
- **LVGL task**: Created by BSP, runs the display loop. UI updates via `lv_timer` (1s interval).
- **MQTT task**: Created by esp_mqtt internally. Calls event handler on connect/disconnect/data.
- **Knob timer**: Created by `iot_knob`, fires callbacks on rotation events.
- **Synchronization**: `printer_manager_t.mutex` protects all printer state reads/writes.

### Bambu MQTT Protocol

- **Local**: `mqtts://<printer_ip>:8883`, username `bblp`, password = LAN access code
- **Cloud**: `mqtts://us.mqtt.bambulab.com:8883`, username `u_<uid>` (from JWT), password = access token
- **Topics**: Subscribe `device/<serial>/report`, publish to `device/<serial>/request`
- **Pushall**: Send `{"pushing":{"sequence_id":"0","command":"pushall"}}` after connect to get full state
- **Key JSON fields** in `print` object: `gcode_state`, `mc_percent`, `mc_remaining_time`, `nozzle_temper`, `nozzle_target_temper`, `bed_temper`, `bed_target_temper`, `subtask_name`, `layer_num`, `total_layer_num`, `print_error`, `stg_cur`
- **AMS fields** in `print.ams` object: `tray_now`, `ams[].humidity`/`humidity_raw`, `ams[].tray[].tray_type`, `tray_color`, `remain`

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

## Regenerating Icon Fonts

The MDI icon font (`main/fonts/mdi_30.c`) is generated from Material Design Icons using the LVGL font converter:

```bash
npm pack @mdi/font && tar xzf mdi-font-*.tgz
npx lv_font_conv --bpp 4 --size 30 --no-compress \
  --font package/fonts/materialdesignicons-webfont.ttf \
  --range 0xF0E5B,0xF185B,0xF144E,0xF16C7,0xF0438,0xF050F,0xF1A45 \
  --format lvgl -o main/fonts/mdi_30.c
```

After regenerating, add the `__has_include` block for the lvgl.h include (see existing file for pattern).

## Adding Error Codes

The error database is `main/error_lookup.tsv` — tab-separated with format: `E<TAB>hex_code<TAB>models<TAB>message`. Only domain `E` (print errors) is included to save flash space. The full database with HMS codes is in PrintSphere's source if needed.

## Flash Size

Binary is ~1.5MB in 1.5MB partition (3% free). Be cautious adding large embedded resources. The error TSV was trimmed from 700KB to 108KB by excluding HMS codes.
