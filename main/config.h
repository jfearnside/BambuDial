#pragma once

/* WiFi Configuration */
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASS       "YOUR_WIFI_PASSWORD"

/* Maximum number of printers supported */
#define MAX_PRINTERS    3

/*
 * Connection Mode
 *   MODE_LOCAL - connect directly to printers on local network via MQTT
 *   MODE_CLOUD - connect via Bambu Lab cloud (auto-discovers printers)
 */
typedef enum { MODE_LOCAL, MODE_CLOUD } connection_mode_t;
#define CONNECTION_MODE  MODE_LOCAL

/*
 * Cloud Configuration (only needed for MODE_CLOUD)
 * Region: "us" (default), "eu", or "cn"
 *
 * If your account requires email verification or 2FA,
 * you can set BAMBU_TOKEN directly instead of email/password.
 * Get it from Bambu Studio network logs or browser dev tools.
 */
#define BAMBU_EMAIL     ""
#define BAMBU_PASSWORD  ""
#define BAMBU_REGION    "us"
#define BAMBU_TOKEN     ""   /* Optional: paste JWT token directly to bypass login */

/*
 * Timezone (POSIX TZ format for ETA display)
 * Examples:
 *   "MST7MDT,M3.2.0,M11.1.0"    — US Mountain (Denver)
 *   "EST5EDT,M3.2.0,M11.1.0"    — US Eastern (New York)
 *   "CST6CDT,M3.2.0,M11.1.0"    — US Central (Chicago)
 *   "PST8PDT,M3.2.0,M11.1.0"    — US Pacific (LA)
 *   "GMT0BST-1,M3.5.0/1,M10.5.0/2" — UK (London)
 *   "CET-1CEST,M3.5.0,M10.5.0/3"  — Central Europe
 *   "AEST-10AEDT,M10.1.0,M4.1.0/3" — Australia Eastern
 */
#define TIMEZONE        "EST5EDT,M3.2.0,M11.1.0"

/*
 * Auto-rotate Display
 * When enabled, automatically cycles between printers that are actively
 * printing. If only one printer is active, stays on that one.
 * Manual encoder rotation temporarily pauses auto-rotate for one cycle.
 */
#define AUTO_ROTATE_ENABLED     1       /* 1 = on (default), 0 = off */
#define AUTO_ROTATE_INTERVAL_S  30      /* seconds between switches */

/*
 * Local Printer Configuration (only needed for MODE_LOCAL)
 * For each printer you need:
 *   - IP address on local network
 *   - Serial number (visible on printer LCD or in Bambu Studio)
 *   - LAN Access Code (printer LCD -> Network -> LAN Access Code)
 *   - A friendly display name
 *
 * NOTE: You do NOT need to put your printer in "LAN Only" mode.
 * Local MQTT works with the printer in normal cloud-connected mode.
 */
#define NUM_PRINTERS    2

#define PRINTER_CONFIGS { \
    { \
        .name        = "Printer 1", \
        .ip          = "192.168.1.100", \
        .serial      = "SERIAL_NUMBER_1", \
        .access_code = "ACCESS_CODE_1", \
    }, \
    { \
        .name        = "Printer 2", \
        .ip          = "192.168.1.101", \
        .serial      = "SERIAL_NUMBER_2", \
        .access_code = "ACCESS_CODE_2", \
    }, \
}
