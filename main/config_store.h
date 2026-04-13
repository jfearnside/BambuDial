#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define MAX_PRINTERS 3

/* Printer config loaded from NVS */
typedef struct {
    char name[32];
    char ip[16];
    char serial[24];
    char access_code[16];
} printer_config_t;

/* Full device configuration loaded from NVS */
typedef struct {
    /* WiFi */
    char wifi_ssid[33];
    char wifi_pass[65];

    /* Connection mode */
    bool cloud_mode;       /* false=local, true=cloud */

    /* Cloud (only used if cloud_mode) */
    char cloud_email[64];
    char cloud_pass[64];
    char cloud_region[4];  /* "us", "eu", "cn" */
    char cloud_token[2048];

    /* Display */
    char timezone[64];
    bool auto_rotate;
    int  auto_rotate_s;

    /* Printers (local mode) */
    int num_printers;
    printer_config_t printers[MAX_PRINTERS];
} device_config_t;

/* Initialize config store (call once at boot) */
esp_err_t config_store_init(void);

/* Load all config from NVS into the struct. Returns ESP_OK if WiFi creds exist. */
esp_err_t config_store_load(device_config_t *cfg);

/* Save all config to NVS */
esp_err_t config_store_save(const device_config_t *cfg);

/* Check if device has been configured (WiFi SSID is set) */
bool config_store_is_configured(void);

/* Clear all config (factory reset) */
esp_err_t config_store_clear(void);

/* Note: printer_manager_load_from_config() is declared in printer_state.c
 * and called from main.c. Include both config_store.h and printer_state.h
 * before calling it. */
