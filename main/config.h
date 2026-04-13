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
