#include "config_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "config_store";
static const char *NVS_NAMESPACE = "bambudial";

/* Helper: read string from NVS, empty string if not found */
static void nvs_read_str(nvs_handle_t h, const char *key, char *buf, size_t buf_size)
{
    size_t len = buf_size;
    if (nvs_get_str(h, key, buf, &len) != ESP_OK) {
        buf[0] = '\0';
    }
}

/* Helper: read int from NVS, default if not found */
static int nvs_read_int(nvs_handle_t h, const char *key, int def)
{
    int32_t val = def;
    nvs_get_i32(h, key, &val);
    return (int)val;
}

esp_err_t config_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS...");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t config_store_load(device_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    /* Set defaults */
    strcpy(cfg->timezone, "EST5EDT,M3.2.0,M11.1.0");
    cfg->auto_rotate = true;
    cfg->auto_rotate_s = 30;
    strcpy(cfg->cloud_region, "us");

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No config found in NVS");
        return ESP_ERR_NOT_FOUND;
    }

    /* WiFi */
    nvs_read_str(h, "wifi_ssid", cfg->wifi_ssid, sizeof(cfg->wifi_ssid));
    nvs_read_str(h, "wifi_pass", cfg->wifi_pass, sizeof(cfg->wifi_pass));

    /* Mode */
    cfg->cloud_mode = nvs_read_int(h, "mode", 0) != 0;

    /* Cloud */
    nvs_read_str(h, "cloud_email", cfg->cloud_email, sizeof(cfg->cloud_email));
    nvs_read_str(h, "cloud_pass", cfg->cloud_pass, sizeof(cfg->cloud_pass));
    nvs_read_str(h, "cloud_region", cfg->cloud_region, sizeof(cfg->cloud_region));
    nvs_read_str(h, "cloud_token", cfg->cloud_token, sizeof(cfg->cloud_token));

    /* Display */
    nvs_read_str(h, "timezone", cfg->timezone, sizeof(cfg->timezone));
    cfg->auto_rotate = nvs_read_int(h, "auto_rotate", 1) != 0;
    cfg->auto_rotate_s = nvs_read_int(h, "auto_rotate_s", 30);

    /* Printers */
    cfg->num_printers = nvs_read_int(h, "prn_count", 0);
    if (cfg->num_printers > MAX_PRINTERS) cfg->num_printers = MAX_PRINTERS;

    for (int i = 0; i < cfg->num_printers; i++) {
        char key[24];
        snprintf(key, sizeof(key), "prn_%d_name", i);
        nvs_read_str(h, key, cfg->printers[i].name, sizeof(cfg->printers[i].name));
        snprintf(key, sizeof(key), "prn_%d_ip", i);
        nvs_read_str(h, key, cfg->printers[i].ip, sizeof(cfg->printers[i].ip));
        snprintf(key, sizeof(key), "prn_%d_serial", i);
        nvs_read_str(h, key, cfg->printers[i].serial, sizeof(cfg->printers[i].serial));
        snprintf(key, sizeof(key), "prn_%d_code", i);
        nvs_read_str(h, key, cfg->printers[i].access_code, sizeof(cfg->printers[i].access_code));
    }

    nvs_close(h);

    if (cfg->wifi_ssid[0] == '\0') {
        ESP_LOGW(TAG, "WiFi SSID not configured");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Config loaded: WiFi=%s, mode=%s, printers=%d",
             cfg->wifi_ssid, cfg->cloud_mode ? "cloud" : "local", cfg->num_printers);
    return ESP_OK;
}

esp_err_t config_store_save(const device_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    /* WiFi */
    nvs_set_str(h, "wifi_ssid", cfg->wifi_ssid);
    nvs_set_str(h, "wifi_pass", cfg->wifi_pass);

    /* Mode */
    nvs_set_i32(h, "mode", cfg->cloud_mode ? 1 : 0);

    /* Cloud */
    nvs_set_str(h, "cloud_email", cfg->cloud_email);
    nvs_set_str(h, "cloud_pass", cfg->cloud_pass);
    nvs_set_str(h, "cloud_region", cfg->cloud_region);
    nvs_set_str(h, "cloud_token", cfg->cloud_token);

    /* Display */
    nvs_set_str(h, "timezone", cfg->timezone);
    nvs_set_i32(h, "auto_rotate", cfg->auto_rotate ? 1 : 0);
    nvs_set_i32(h, "auto_rotate_s", cfg->auto_rotate_s);

    /* Printers */
    nvs_set_i32(h, "prn_count", cfg->num_printers);
    for (int i = 0; i < cfg->num_printers && i < MAX_PRINTERS; i++) {
        char key[24];
        snprintf(key, sizeof(key), "prn_%d_name", i);
        nvs_set_str(h, key, cfg->printers[i].name);
        snprintf(key, sizeof(key), "prn_%d_ip", i);
        nvs_set_str(h, key, cfg->printers[i].ip);
        snprintf(key, sizeof(key), "prn_%d_serial", i);
        nvs_set_str(h, key, cfg->printers[i].serial);
        snprintf(key, sizeof(key), "prn_%d_code", i);
        nvs_set_str(h, key, cfg->printers[i].access_code);
    }

    err = nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(TAG, "Config saved: WiFi=%s, printers=%d", cfg->wifi_ssid, cfg->num_printers);
    return err;
}

bool config_store_is_configured(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    char ssid[33] = {0};
    nvs_read_str(h, "wifi_ssid", ssid, sizeof(ssid));
    nvs_close(h);
    return ssid[0] != '\0';
}

esp_err_t config_store_clear(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Config cleared");
    return ESP_OK;
}
