#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "bsp/m5dial.h"
#include "iot_knob.h"
#include "lvgl.h"

#include "config.h"
#include "printer_state.h"
#include "bambu_mqtt.h"
#include "bambu_cloud.h"
#include "ui.h"

static const char *TAG = "main";

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static printer_manager_t s_mgr;

/* --- WiFi --- */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &inst_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, &inst_got_ip));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi '%s'...", WIFI_SSID);

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi connected");
}

/* --- Encoder callbacks (direct knob API, bypasses LVGL) --- */

static void knob_right_cb(void *arg, void *data)
{
    printer_manager_select_next(&s_mgr);
    int sel = s_mgr.selected;
    ESP_LOGI(TAG, "Encoder: RIGHT -> printer %d", sel);
    bambu_mqtt_switch(sel);
    bsp_display_lock(0);
    ui_show_selected();
    bsp_display_unlock();
}

static void knob_left_cb(void *arg, void *data)
{
    printer_manager_select_prev(&s_mgr);
    int sel = s_mgr.selected;
    ESP_LOGI(TAG, "Encoder: LEFT -> printer %d", sel);
    bambu_mqtt_switch(sel);
    bsp_display_lock(0);
    ui_show_selected();
    bsp_display_unlock();
}

/* --- Cloud mode startup --- */

static void start_cloud_mode(void)
{
    ESP_LOGI(TAG, "Starting in CLOUD mode...");

    if (!bambu_cloud_login()) {
        ESP_LOGE(TAG, "Cloud login failed! Check credentials in config.h");
        ESP_LOGE(TAG, "If 2FA/email verification required, set BAMBU_TOKEN directly.");
        return;
    }

    if (!bambu_cloud_fetch_printers(&s_mgr)) {
        ESP_LOGE(TAG, "No printers found in cloud account");
        return;
    }

    bambu_cloud_mqtt_start(&s_mgr);
    ESP_LOGI(TAG, "Cloud mode started - monitoring %d printers", s_mgr.num_printers);
}

/* --- Local mode startup --- */

static void start_local_mode(void)
{
    ESP_LOGI(TAG, "Starting in LOCAL mode...");
    bambu_mqtt_start(&s_mgr);
    ESP_LOGI(TAG, "Local mode started - monitoring %d printers", s_mgr.num_printers);
}

void app_main(void)
{
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize printer state manager */
    printer_manager_init(&s_mgr);

    /* Initialize I2C (needed for touch) */
    bsp_i2c_init();

    /* Initialize display + LVGL */
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = 240 * 40,
        .double_buffer = true,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
        },
    };
    bsp_display_start_with_config(&disp_cfg);
    bsp_display_brightness_set(80);

    /* Create UI */
    bsp_display_lock(0);
    ui_init(&s_mgr);
    bsp_display_unlock();

    /* Set up encoder using raw knob API — direct callbacks, no LVGL group needed */
    knob_config_t knob_cfg = {
        .default_direction = 0,
        .gpio_encoder_a = BSP_ENCODER_A,
        .gpio_encoder_b = BSP_ENCODER_B,
    };
    knob_handle_t knob = iot_knob_create(&knob_cfg);
    if (knob) {
        iot_knob_register_cb(knob, KNOB_RIGHT, knob_right_cb, NULL);
        iot_knob_register_cb(knob, KNOB_LEFT, knob_left_cb, NULL);
        ESP_LOGI(TAG, "Encoder configured via direct knob API");
    } else {
        ESP_LOGW(TAG, "Failed to create knob");
    }

    /* Connect WiFi */
    wifi_init();

    /* Start in configured mode */
    connection_mode_t mode = CONNECTION_MODE;
    if (mode == MODE_CLOUD) {
        start_cloud_mode();
    } else {
        start_local_mode();
    }

    /* Main loop */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
