# BambuDial

Bambu Lab 3D printer status monitor for the [M5Stack M5Dial](https://docs.m5stack.com/en/core/M5Dial).

Monitor up to 3 Bambu Lab printers from a compact 1.28" round display. Turn the dial to switch printers. Auto-rotates between active prints. Works without putting printers in LAN-only mode.

![M5Dial](https://shop.m5stack.com/cdn/shop/files/1_61f0c846-f03d-4f09-afb1-c1b0bc8d1c77_1200x1200.webp?v=1696659476)

## Features

- **Real-time status** — print progress, nozzle/bed temperatures, remaining time, ETA, layer count
- **AMS filament display** — active filament type with color dot, remaining %, per-unit humidity
- **Print stage detail** — shows leveling, heating, calibrating, filament changes etc.
- **Local MQTT** — connects directly to printers on your network via MQTT over TLS
- **Cloud MQTT** — connects via Bambu Lab cloud with auto-discovery of bound printers
- **Auto-rotate** — automatically cycles between active printers (configurable interval)
- **Rotary encoder** — turn the dial to manually switch printers
- **Color-coded UI** — progress arc, state colors, heating/cooling icons, humidity warnings
- **Error messages** — 900+ Bambu error codes resolved to human-readable descriptions
- **TLS secured** — embedded Bambu CA certificates for proper certificate verification
- **SNTP time sync** — accurate ETA display with configurable timezone

## Easy Install (No Coding Required)

**[Install BambuDial from your browser →](https://jfearnside.github.io/BambuDial/)**

1. Open the link above in **Chrome or Edge**
2. Connect your M5Dial via USB
3. Click **"Install BambuDial"**
4. Connect your phone to **"BambuDial-Setup"** WiFi
5. Enter your printer details in the web form
6. Done!

No tools, no compiling, no command line needed.

---

## Hardware Required

### M5Stack M5Dial
- **Purchase**: [M5Stack Official Store](https://shop.m5stack.com/products/m5stack-dial-esp32-s3-smart-rotary-knob-w-1-28-round-touch-screen) (~$20-25 USD)
- **Purchase** [Ali Express Store] (https://www.aliexpress.us/item/3256809697970615.html) (Currently $13 with free shipping)
- **Specs**: ESP32-S3, 240x240 round GC9A01 LCD, rotary encoder, touch screen, USB-C
- **Also available on**: Amazon, Mouser, DigiKey

### USB-C Cable
Any USB-C data cable for flashing and power (not charge-only).

### Bambu Lab Printer(s)
Tested with X1C, P2S. Should work with any Bambu Lab printer (P1P, P1S, A1, A1 Mini, X1E, etc.).

## Developer Setup (Building from Source)

### 1. Install ESP-IDF v5.4+

**macOS:**
```bash
mkdir -p ~/esp && cd ~/esp
git clone -b v5.4.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
. ./export.sh
```

> **macOS SSL error?** Run `"/Applications/Python 3.x/Install Certificates.command"` first.

> **cmake not found?** Run `brew install cmake` then retry.

**Linux:**
```bash
mkdir -p ~/esp && cd ~/esp
git clone -b v5.4.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
. ./export.sh
```

**Windows (WSL2 recommended):**
Follow the same Linux steps inside WSL2, or use the [ESP-IDF Windows Installer](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/windows-setup.html).

### 2. Clone the Repository

```bash
git clone https://github.com/jfearnside/BambuDial.git
cd BambuDial
```

### 3. Build and Flash

```bash
. ~/esp/esp-idf/export.sh      # required each terminal session
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbmodem* flash monitor
```

Press `Ctrl+]` to exit the serial monitor.

On first boot, BambuDial starts in setup mode — connect to the "BambuDial-Setup" WiFi and configure via the web portal. No need to edit config files.

## Finding Your Printer Details

### IP Address
- **On printer LCD**: Settings → Network → IP Address
- **In your router**: Check the DHCP client list for your printer
- **Tip**: Set a static DHCP lease in your router so the IP doesn't change

### Serial Number
- **On printer LCD**: Settings → Device Info
- **In Bambu Studio**: Device tab shows serial numbers
- **In Bambu Studio config file** (all printers at once):
  - macOS: `~/Library/Application Support/BambuStudio/BambuStudio.conf`
  - Windows: `%APPDATA%\BambuStudio\BambuStudio.conf`
  - Look for `"access_code"` and `"dev_id"` entries

### LAN Access Code
- **On printer LCD**: Settings → Network → LAN Access Code
- **In Bambu Studio config file**: Look for `"access_code"` section — maps serial numbers to access codes

> **Important**: You do NOT need to put your printer in "LAN Only" mode. Local MQTT works while the printer remains fully cloud-connected.

## Cloud Mode

To use Bambu Lab's cloud instead of direct local connections:

1. Set `CONNECTION_MODE` to `MODE_CLOUD` in `config.h`
2. Set `BAMBU_EMAIL` and `BAMBU_PASSWORD`
3. Set `BAMBU_REGION` to `"us"`, `"eu"`, or `"cn"`
4. Printers are auto-discovered — no IP/serial/access code needed

**If your account uses 2FA or email verification**: Set `BAMBU_TOKEN` directly with a JWT token. You can find this in Bambu Studio's network logs or browser developer tools after logging into bambulab.com.

## Usage

### Display Layout

```
         ╭──────────────╮
        ╱   Printer Name ●╲      ● = connection dot
       │       1/2         │      green=connected
       │                   │
       │     PRINTING      │      Status (color-coded)
       │       73%         │      Progress percentage
       │                   │
       │  🔧210/210 🔲60/60│      Nozzle + Bed temps with icons
       │   benchy.3mf      │      ← rotating info area
       │    1h 23m left    │      ← (3 pages, 5 sec each)
        ╲   Layer 42/100  ╱
         ╰──────────────╯
    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓      Progress arc (outer ring)
```

### Rotating Info Pages

The bottom 3 lines cycle every 5 seconds:

| Page | Line 1 | Line 2 | Line 3 |
|------|--------|--------|--------|
| 1 | File name | Time remaining | Layer X/Y |
| 2 | File name | ETA (clock time) | Layer X/Y |
| 3* | Active filament + color | AMS humidity | Print stage |

*Page 3 only appears for printers with AMS units connected.

### Controls

| Action | What it does |
|--------|-------------|
| **Rotate dial** | Switch between printers |
| **Press front button** | Dismiss DONE/FAILED, or cycle info pages |

### Auto-Rotate

When multiple printers are active (printing, paused, preparing, or recently finished), the display automatically cycles between them:
- **Active printers** shown for 30 seconds (configurable)
- **Done/Failed printers** shown for 5 seconds (brief glance)
- **Idle printers** are skipped
- Turning the dial pauses auto-rotate for one cycle
- Press the button to dismiss DONE/FAILED — sets printer to idle and stops rotating to it

### Icon Colors

| Nozzle/Bed Icon | Meaning |
|----------------|---------|
| 🟠 Orange | Heating up (below target) |
| 🔵 Blue | Cooling down (above target) |
| ⚪ White | At temperature or heater off |

The bed icon changes shape: heat-wave arrows when heating, neutral radiator when at temp or cooling.

### Status Colors

| Arc Color | State |
|-----------|-------|
| Green | Printing |
| Cyan | Done |
| Orange | Paused |
| Warm orange | Preparing |
| Red | Error/Failed |
| Gray | Idle |

### AMS Humidity Colors

| Color | Humidity Level |
|-------|---------------|
| Green | Good (< 40%) |
| Yellow | Moderate (40-60%) |
| Red | High (> 60%) — consider drying filament |

## Customization

### Adding More Printers

Change `NUM_PRINTERS` and add entries to `PRINTER_CONFIGS` in `config.h`. Maximum 3 printers.

### Changing Auto-Rotate Timing

In `config.h`:
```c
#define AUTO_ROTATE_ENABLED     1       /* set to 0 to disable */
#define AUTO_ROTATE_INTERVAL_S  30      /* seconds per active printer */
```

### Changing Timezone

Set `TIMEZONE` in `config.h` using POSIX TZ format. Common examples are provided in the file. For a full reference, see the [GNU TZ documentation](https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html).

### Adjusting Display Brightness

In `main.c`, change:
```c
bsp_display_brightness_set(80);  /* 0-100% */
```

### Changing Display Colors

State colors are in `printer_state.c` in the `print_state_color()` function. Arc, text, and icon colors are in `ui.c`.

### Adding Custom Icon Glyphs

The icon font (`main/fonts/mdi_30.c`) can be regenerated with additional [Material Design Icons](https://materialdesignicons.com/):

```bash
npm pack @mdi/font && tar xzf mdi-font-*.tgz
npx lv_font_conv --bpp 4 --size 30 --no-compress \
  --font package/fonts/materialdesignicons-webfont.ttf \
  --range 0xF0E5B,0xF185B,0xF0438,0xYOUR_CODEPOINT \
  --format lvgl -o main/fonts/mdi_30.c
```

Find icon codepoints at [Pictogrammers MDI Library](https://pictogrammers.com/library/mdi/).

## Project Structure

```
BambuDial/
├── CMakeLists.txt              # ESP-IDF project file
├── sdkconfig.defaults          # Build configuration
├── CLAUDE.md                   # Development guide
├── main/
│   ├── CMakeLists.txt          # Component build file
│   ├── idf_component.yml       # M5Dial BSP dependency
│   ├── config.h                # User configuration (WiFi, printers, timezone)
│   ├── main.c                  # App entry: WiFi, SNTP, encoder, button, auto-rotate
│   ├── bambu_mqtt.h/.c         # Local MQTT: connect, subscribe, parse, reassemble
│   ├── bambu_cloud.h/.c        # Cloud auth (login/JWT) + cloud MQTT connection
│   ├── bambu_parse.h           # Shared AMS/stage JSON parser
│   ├── printer_state.h/.c      # Printer state struct, AMS trays, helpers
│   ├── ui.h/.c                 # LVGL UI: arc, temps, rotating info pages
│   ├── error_lookup.h/.c       # Error code → message lookup
│   ├── error_lookup.tsv        # 900+ error code database
│   ├── fonts/
│   │   └── mdi_30.c            # Material Design Icons (nozzle, bed, etc.)
│   └── certs/
│       └── bambu_ca_bundle.pem # Bambu Lab TLS CA certificates
```

## Technical Notes

- **Single MQTT connection** — Only one printer connected at a time to fit within 204KB RAM. Auto-rotate briefly polls other printers to check their state.
- **Message reassembly** — Bambu printers send large JSON payloads (up to 23KB for X1C). Messages are reassembled from MQTT fragments before parsing.
- **Incremental updates** — All Bambu printer series send changed fields only. The state struct merges updates incrementally.
- **No PSRAM** — Memory is carefully managed: 48KB LVGL, 16KB MQTT buffer, 24KB reassembly buffer.
- **Flash budget** — Binary uses ~97% of the 1.5MB partition. The error TSV was trimmed from 700KB to 108KB by excluding HMS codes.
- **Dismissed state** — When you press the button to dismiss DONE, a `dismissed` flag prevents MQTT from reverting the state. It clears automatically when the printer starts a new print.

## Troubleshooting

### "cmake" not found
Run `brew install cmake` (macOS) or `sudo apt install cmake` (Linux).

### SSL certificate errors during ESP-IDF install
macOS: `"/Applications/Python 3.x/Install Certificates.command"`

### PSRAM init failed / abort
Ensure `CONFIG_SPIRAM=n` in `sdkconfig.defaults`. The M5Dial has no PSRAM.

### MQTT connection timeout
- Verify the printer IP is correct and reachable (`ping <ip>`)
- Verify the serial number and access code match
- Check that the printer is on and connected to your network
- The printer does NOT need to be in LAN-only mode

### Display shows wrong time/ETA
Check your `TIMEZONE` setting in `config.h`. SNTP needs a few seconds after boot to sync.

### Build: "app partition too small"
The flash is nearly full. If you've added large resources, consider trimming the error TSV or reducing font sizes.

### Serial port not found
- Try `ls /dev/tty.usb*` (macOS) or `ls /dev/ttyACM*` (Linux) to find the port
- Ensure the USB cable supports data (not charge-only)
- Press and hold the reset button while plugging in if the device doesn't enumerate

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
- [Material Design Icons](https://materialdesignicons.com/) by Pictogrammers

## License

MIT
