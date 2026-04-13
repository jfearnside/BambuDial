#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_sntp.h"

#include "bsp/m5dial.h"
#include "iot_knob.h"
#include "iot_button.h"
#include "button_gpio.h"
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

    /* Sync time via SNTP for accurate ETA display */
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    /* Set timezone from config.h */
    setenv("TZ", TIMEZONE, 1);
    tzset();
    ESP_LOGI(TAG, "SNTP time sync started");
}

/* --- Auto-rotate state --- */
static bool s_auto_rotate_paused = false;  /* pause after manual encoder use */

/* --- Encoder callbacks (direct knob API, bypasses LVGL) --- */

static void knob_right_cb(void *arg, void *data)
{
    s_auto_rotate_paused = true;  /* pause auto-rotate for one cycle */
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
    s_auto_rotate_paused = true;  /* pause auto-rotate for one cycle */
    printer_manager_select_prev(&s_mgr);
    int sel = s_mgr.selected;
    ESP_LOGI(TAG, "Encoder: LEFT -> printer %d", sel);
    bambu_mqtt_switch(sel);
    bsp_display_lock(0);
    ui_show_selected();
    bsp_display_unlock();
}

/* Check if a printer is worth auto-switching to.
 * PRINTING/PAUSED/PREPARING = actively working — always show
 * FAILED = error occurred — always show
 * FINISHED = completed print — always show (user wants to see both done)
 * IDLE/UNKNOWN = nothing interesting — don't switch to */
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

/* Find next active printer after current. Returns -1 if none active. */
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

/* --- Button callback: dismiss DONE/FAILED, or cycle info page --- */

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
            ps->dismissed = true;  /* prevent MQTT from reverting to FINISH */
            dismissed = true;
        }
        printer_manager_unlock(&s_mgr);
    }

    if (!dismissed) {
        /* Cycle through info pages */
        ESP_LOGI(TAG, "Button: cycle info page");
        ui_cycle_info_page();
    }
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

    /* Set up front button to dismiss DONE/FAILED states */
    button_config_t btn_cfg = {0};
    button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = BSP_BTN_PRESS,
        .active_level = 0,
    };
    button_handle_t btn = NULL;
    if (iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &btn) == ESP_OK && btn) {
        iot_button_register_cb(btn, BUTTON_PRESS_DOWN, NULL, button_press_cb, NULL);
        ESP_LOGI(TAG, "Front button configured for dismiss");
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

    /* Main loop — auto-rotate with different durations per state.
     * Active printers (PRINTING/PAUSED/PREPARING) show for AUTO_ROTATE_INTERVAL_S.
     * Done/Failed printers show for 5 seconds — just a brief glance.
     * We poll other printers periodically to know their state. */
    int display_time_s = 0;       /* how long current printer has been displayed */
    int pushall_timer_s = 0;      /* time since last pushall resend */
    int poll_timer_s = 0;         /* time since last poll of other printers */

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        display_time_s++;
        pushall_timer_s++;
        poll_timer_s++;

        /* Resend pushall every 30s to keep data fresh */
        if (pushall_timer_s >= 30) {
            bambu_mqtt_request_pushall();
            pushall_timer_s = 0;
        }

#if AUTO_ROTATE_ENABLED
        if (s_auto_rotate_paused) {
            if (display_time_s >= AUTO_ROTATE_INTERVAL_S) {
                s_auto_rotate_paused = false;
                display_time_s = 0;
            }
            continue;
        }

        /* Determine how long to show the current printer */
        int show_duration = AUTO_ROTATE_INTERVAL_S;
        if (printer_manager_lock(&s_mgr, pdMS_TO_TICKS(50))) {
            print_state_t cur_state = s_mgr.printers[s_mgr.selected].state;
            if (cur_state == PRINT_STATE_FINISHED || cur_state == PRINT_STATE_FAILED) {
                show_duration = 5;  /* brief glance at done/failed */
            }
            printer_manager_unlock(&s_mgr);
        }

        if (display_time_s < show_duration) continue;

        /* Time to consider switching — poll other printers first */
        if (poll_timer_s >= 15) {
            int current = s_mgr.selected;
            int num = s_mgr.num_printers;
            for (int i = 0; i < num; i++) {
                if (i == current) continue;
                bambu_mqtt_switch(i);
                vTaskDelay(pdMS_TO_TICKS(4000));
                ESP_LOGI(TAG, "Polled printer %d (%s): state=%s",
                         i, s_mgr.printers[i].name,
                         print_state_name(s_mgr.printers[i].state));
            }
            /* Reconnect to current */
            bambu_mqtt_switch(current);
            vTaskDelay(pdMS_TO_TICKS(3000));
            poll_timer_s = 0;
        }

        /* Find next active printer */
        int current = s_mgr.selected;
        int next_active = find_next_active_printer(current);

        if (next_active >= 0 && next_active != current) {
            if (printer_manager_lock(&s_mgr, pdMS_TO_TICKS(100))) {
                s_mgr.selected = next_active;
                printer_manager_unlock(&s_mgr);
            }
            ESP_LOGI(TAG, "Auto-rotate -> printer %d (%s)", next_active,
                     s_mgr.printers[next_active].name);
            bambu_mqtt_switch(next_active);
            bsp_display_lock(0);
            ui_show_selected();
            bsp_display_unlock();
        }
        display_time_s = 0;
#else
        (void)display_time_s;
        (void)poll_timer_s;
#endif
    }
}
