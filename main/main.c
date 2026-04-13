#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "nvs_flash.h"

#include "bsp/m5dial.h"
#include "iot_knob.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "lvgl.h"

#include "config_store.h"
#include "printer_state.h"

/* Defined in printer_state.c */
extern void printer_manager_load_from_config(printer_manager_t *mgr, const device_config_t *cfg);
#include "bambu_mqtt.h"
#include "bambu_cloud.h"
#include "setup_portal.h"
#include "ui.h"

static const char *TAG = "main";

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static printer_manager_t s_mgr;
static device_config_t s_cfg;

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

/* Try to connect to WiFi with timeout. Returns true if connected. */
static bool wifi_connect(const device_config_t *cfg, int timeout_s)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    esp_event_handler_instance_t inst_any, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &inst_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, &inst_got_ip));

    wifi_config_t wifi_cfg = { .sta = {} };
    strncpy((char *)wifi_cfg.sta.ssid, cfg->wifi_ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, cfg->wifi_pass, sizeof(wifi_cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi '%s' (timeout %ds)...", cfg->wifi_ssid, timeout_s);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                            pdFALSE, pdTRUE,
                                            pdMS_TO_TICKS(timeout_s * 1000));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected");

        /* Start SNTP time sync */
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
        setenv("TZ", cfg->timezone, 1);
        tzset();
        ESP_LOGI(TAG, "SNTP started, timezone=%s", cfg->timezone);

        return true;
    }

    ESP_LOGW(TAG, "WiFi connection timed out");
    esp_wifi_stop();
    esp_wifi_deinit();
    return false;
}

/* --- Encoder callbacks --- */

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

/* --- Button: dismiss DONE/FAILED or cycle info, long-press for setup --- */

static bool s_auto_rotate_paused = false;

static void button_press_cb(void *arg, void *data)
{
    int sel = s_mgr.selected;
    bool dismissed = false;

    if (printer_manager_lock(&s_mgr, pdMS_TO_TICKS(100))) {
        printer_state_t *ps = &s_mgr.printers[sel];
        if (ps->state == PRINT_STATE_FINISHED || ps->state == PRINT_STATE_FAILED) {
            ESP_LOGI(TAG, "Button: dismissed %s on printer %d (%s)",
                     print_state_name(ps->state), sel, ps->name);
            ps->state = PRINT_STATE_IDLE;
            ps->progress = 0;
            ps->error_message[0] = '\0';
            ps->print_error = 0;
            ps->dismissed = true;
            dismissed = true;
        }
        printer_manager_unlock(&s_mgr);
    }

    if (!dismissed) {
        ESP_LOGI(TAG, "Button: cycle info page");
        ui_cycle_info_page();
    }

    bsp_display_lock(0);
    ui_show_selected();
    bsp_display_unlock();
}

static void button_long_press_cb(void *arg, void *data)
{
    ESP_LOGW(TAG, "Long press: entering setup mode...");
    config_store_clear();
    esp_restart();
}

/* --- Auto-rotate helpers --- */

static bool is_printer_active(const printer_state_t *ps)
{
    switch (ps->state) {
        case PRINT_STATE_PRINTING:
        case PRINT_STATE_PAUSED:
        case PRINT_STATE_PREPARING:
        case PRINT_STATE_FAILED:
        case PRINT_STATE_FINISHED:
            return true;
        default:
            return false;
    }
}

static int find_next_active_printer(int current)
{
    if (!printer_manager_lock(&s_mgr, pdMS_TO_TICKS(100))) return -1;
    int num = s_mgr.num_printers;
    int found = -1;
    for (int i = 1; i <= num; i++) {
        int idx = (current + i) % num;
        if (is_printer_active(&s_mgr.printers[idx])) {
            found = idx;
            break;
        }
    }
    printer_manager_unlock(&s_mgr);
    return found;
}

/* --- App Entry --- */

void app_main(void)
{
    /* Initialize config store (NVS) */
    ESP_ERROR_CHECK(config_store_init());

    /* Initialize network stack early (needed by both WiFi STA and AP modes) */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Initialize printer state manager */
    printer_manager_init(&s_mgr);

    /* Initialize I2C + display + LVGL */
    bsp_i2c_init();
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = 240 * 40,
        .double_buffer = true,
        .flags = { .buff_dma = true, .buff_spiram = false },
    };
    bsp_display_start_with_config(&disp_cfg);
    bsp_display_brightness_set(80);

    /* Set up encoder */
    knob_config_t knob_cfg = {
        .default_direction = 0,
        .gpio_encoder_a = BSP_ENCODER_A,
        .gpio_encoder_b = BSP_ENCODER_B,
    };
    knob_handle_t knob = iot_knob_create(&knob_cfg);
    if (knob) {
        iot_knob_register_cb(knob, KNOB_RIGHT, knob_right_cb, NULL);
        iot_knob_register_cb(knob, KNOB_LEFT, knob_left_cb, NULL);
    }

    /* Set up front button (short press + long press) */
    button_config_t btn_cfg = {0};
    button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = BSP_BTN_PRESS,
        .active_level = 0,
    };
    button_handle_t btn = NULL;
    if (iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &btn) == ESP_OK && btn) {
        iot_button_register_cb(btn, BUTTON_PRESS_DOWN, NULL, button_press_cb, NULL);
        iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, button_long_press_cb, NULL);
    }

    /* ========== BOOT DECISION: configured or setup mode? ========== */

    esp_err_t cfg_err = config_store_load(&s_cfg);

    if (cfg_err != ESP_OK || s_cfg.wifi_ssid[0] == '\0') {
        /* No config → enter setup mode */
        ESP_LOGI(TAG, "No configuration found — starting setup portal");
        bsp_display_lock(0);
        ui_show_setup_screen();
        bsp_display_unlock();
        setup_portal_start();

        /* Portal runs until user saves config and device reboots */
        while (1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
        return;
    }

    /* Config exists → try WiFi connection */
    ESP_LOGI(TAG, "Config loaded, attempting WiFi connection...");

    /* Show a brief "connecting" message */
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_t *lbl = lv_label_create(scr);
    lv_obj_center(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl, "Connecting...");
    bsp_display_unlock();

    bool connected = wifi_connect(&s_cfg, 15);

    if (!connected) {
        /* WiFi failed → fall back to setup portal */
        ESP_LOGW(TAG, "WiFi failed — starting setup portal");
        bsp_display_lock(0);
        lv_obj_clean(lv_screen_active());
        ui_show_setup_screen();
        bsp_display_unlock();
        setup_portal_start();
        while (1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
        return;
    }

    /* ========== NORMAL OPERATION ========== */

    /* Load printer configs and create UI */
    printer_manager_load_from_config(&s_mgr, &s_cfg);

    bsp_display_lock(0);
    lv_obj_clean(lv_screen_active());
    ui_init(&s_mgr);
    bsp_display_unlock();

    /* Start MQTT */
    if (s_cfg.cloud_mode) {
        ESP_LOGI(TAG, "Starting CLOUD mode...");
        bambu_cloud_set_config(&s_cfg);
        if (bambu_cloud_login()) {
            bambu_cloud_fetch_printers(&s_mgr);
            bambu_cloud_mqtt_start(&s_mgr);
        } else {
            ESP_LOGE(TAG, "Cloud login failed");
        }
    } else {
        ESP_LOGI(TAG, "Starting LOCAL mode with %d printers", s_mgr.num_printers);
        bambu_mqtt_start(&s_mgr);
    }

    ESP_LOGI(TAG, "BambuDial running — long-press button to re-enter setup");

    /* ========== MAIN LOOP: auto-rotate ========== */

    int display_time_s = 0;
    int pushall_timer_s = 0;
    int poll_timer_s = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        display_time_s++;
        pushall_timer_s++;
        poll_timer_s++;

        if (pushall_timer_s >= 30) {
            bambu_mqtt_request_pushall();
            pushall_timer_s = 0;
        }

        if (!s_cfg.auto_rotate) continue;

        if (s_auto_rotate_paused) {
            if (display_time_s >= s_cfg.auto_rotate_s) {
                s_auto_rotate_paused = false;
                display_time_s = 0;
            }
            continue;
        }

        int show_duration = s_cfg.auto_rotate_s;
        if (printer_manager_lock(&s_mgr, pdMS_TO_TICKS(50))) {
            print_state_t cur_state = s_mgr.printers[s_mgr.selected].state;
            if (cur_state == PRINT_STATE_FINISHED || cur_state == PRINT_STATE_FAILED)
                show_duration = 5;
            printer_manager_unlock(&s_mgr);
        }

        if (display_time_s < show_duration) continue;

        if (poll_timer_s >= 15) {
            int current = s_mgr.selected;
            int num = s_mgr.num_printers;
            for (int i = 0; i < num; i++) {
                if (i == current) continue;
                bambu_mqtt_switch(i);
                vTaskDelay(pdMS_TO_TICKS(4000));
            }
            bambu_mqtt_switch(current);
            vTaskDelay(pdMS_TO_TICKS(3000));
            poll_timer_s = 0;
        }

        int current = s_mgr.selected;
        int next_active = find_next_active_printer(current);

        if (next_active >= 0 && next_active != current) {
            if (printer_manager_lock(&s_mgr, pdMS_TO_TICKS(100))) {
                s_mgr.selected = next_active;
                printer_manager_unlock(&s_mgr);
            }
            ESP_LOGI(TAG, "Auto-rotate -> printer %d", next_active);
            bambu_mqtt_switch(next_active);
            bsp_display_lock(0);
            ui_show_selected();
            bsp_display_unlock();
        }
        display_time_s = 0;
    }
}
