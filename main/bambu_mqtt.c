#include "bambu_mqtt.h"
#include "bambu_parse.h"
#include "error_lookup.h"
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "bambu_mqtt";

/* Embedded Bambu CA certificate bundle */
extern const uint8_t bambu_ca_pem_start[] asm("_binary_bambu_ca_bundle_pem_start");
extern const uint8_t bambu_ca_pem_end[]   asm("_binary_bambu_ca_bundle_pem_end");

/* Single active MQTT connection (one printer at a time to save RAM) */
static esp_mqtt_client_handle_t s_client = NULL;
static printer_manager_t *s_mgr = NULL;
static int s_active_idx = -1;
static char s_topic_report[64];
static char s_topic_request[64];

/* Reassembly buffer for fragmented MQTT messages */
#define REASSEMBLY_BUF_SIZE 24576
static char *s_reassembly_buf = NULL;
static int s_reassembly_len = 0;
static int s_reassembly_total = 0;

static void parse_print_report(printer_state_t *ps, const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed (len=%d)", len);
        return;
    }

    cJSON *print_obj = cJSON_GetObjectItem(root, "print");
    if (!print_obj) {
        cJSON_Delete(root);
        return;
    }

    cJSON *item;

    if ((item = cJSON_GetObjectItem(print_obj, "gcode_state")) && cJSON_IsString(item)) {
        print_state_t new_state = parse_gcode_state(item->valuestring);
        /* If user dismissed DONE/FAILED, keep it as IDLE until printer actually changes state */
        if (ps->dismissed) {
            if (new_state != PRINT_STATE_FINISHED && new_state != PRINT_STATE_FAILED) {
                /* Printer moved to a genuinely different state — clear dismiss */
                ps->dismissed = false;
                ps->state = new_state;
            }
            /* else: still FINISH/FAILED from MQTT — keep our IDLE override */
        } else {
            ps->state = new_state;
        }
    }
    if ((item = cJSON_GetObjectItem(print_obj, "mc_percent")) && cJSON_IsNumber(item))
        ps->progress = item->valueint;
    if ((item = cJSON_GetObjectItem(print_obj, "mc_remaining_time")) && cJSON_IsNumber(item))
        ps->remaining_min = item->valueint;
    if ((item = cJSON_GetObjectItem(print_obj, "nozzle_temper")) && cJSON_IsNumber(item))
        ps->nozzle_temp = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(print_obj, "nozzle_target_temper")) && cJSON_IsNumber(item))
        ps->nozzle_target = (float)item->valuedouble;
    /* Secondary nozzle (H2D dual extruder) — try multiple field names */
    if ((item = cJSON_GetObjectItem(print_obj, "secondary_nozzle_temper")) && cJSON_IsNumber(item))
        ps->nozzle2_temp = (float)item->valuedouble;
    else if ((item = cJSON_GetObjectItem(print_obj, "right_nozzle_temper")) && cJSON_IsNumber(item))
        ps->nozzle2_temp = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(print_obj, "secondary_nozzle_target_temper")) && cJSON_IsNumber(item))
        ps->nozzle2_target = (float)item->valuedouble;
    else if ((item = cJSON_GetObjectItem(print_obj, "right_nozzle_target_temper")) && cJSON_IsNumber(item))
        ps->nozzle2_target = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(print_obj, "bed_temper")) && cJSON_IsNumber(item))
        ps->bed_temp = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(print_obj, "bed_target_temper")) && cJSON_IsNumber(item))
        ps->bed_target = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(print_obj, "subtask_name")) && cJSON_IsString(item))
        strncpy(ps->subtask_name, item->valuestring, sizeof(ps->subtask_name) - 1);
    if ((item = cJSON_GetObjectItem(print_obj, "layer_num")) && cJSON_IsNumber(item))
        ps->layer_num = item->valueint;
    if ((item = cJSON_GetObjectItem(print_obj, "total_layer_num")) && cJSON_IsNumber(item))
        ps->total_layers = item->valueint;
    if ((item = cJSON_GetObjectItem(print_obj, "print_error")) && cJSON_IsNumber(item)) {
        int prev_error = ps->print_error;
        ps->print_error = item->valueint;
        if (ps->print_error != 0 && ps->print_error != prev_error)
            error_lookup(ps->print_error, ps->error_message, sizeof(ps->error_message));
        else if (ps->print_error == 0)
            ps->error_message[0] = '\0';
    }

    /* AMS and print stage */
    parse_ams_and_stage(ps, print_obj);

    cJSON_Delete(root);
}

static void handle_complete_message(const char *data, int len)
{
    if (s_active_idx < 0 || !s_mgr) return;
    ESP_LOGI(TAG, "[%d] Received complete message (%d bytes)", s_active_idx, len);
    if (printer_manager_lock(s_mgr, pdMS_TO_TICKS(200))) {
        parse_print_report(&s_mgr->printers[s_active_idx], data, len);
        ESP_LOGI(TAG, "[%d] State: %s, nozzle=%.0f/%.0f",
                 s_active_idx,
                 print_state_name(s_mgr->printers[s_active_idx].state),
                 s_mgr->printers[s_active_idx].nozzle_temp,
                 s_mgr->printers[s_active_idx].nozzle_target);
        printer_manager_unlock(s_mgr);
    }
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "[%d] Connected to %s", s_active_idx,
                 s_mgr->printers[s_active_idx].ip);

        if (printer_manager_lock(s_mgr, pdMS_TO_TICKS(100))) {
            s_mgr->printers[s_active_idx].mqtt_connected = true;
            printer_manager_unlock(s_mgr);
        }

        esp_mqtt_client_subscribe(s_client, s_topic_report, 0);

        /* Request full status dump */
        {
            const char *cmd = "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}";
            esp_mqtt_client_publish(s_client, s_topic_request, cmd, 0, 0, 0);
            ESP_LOGI(TAG, "[%d] Sent pushall request", s_active_idx);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "[%d] Disconnected", s_active_idx);
        if (s_active_idx >= 0 && printer_manager_lock(s_mgr, pdMS_TO_TICKS(100))) {
            s_mgr->printers[s_active_idx].mqtt_connected = false;
            printer_manager_unlock(s_mgr);
        }
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "[%d] DATA: data_len=%d total=%d offset=%d",
                 s_active_idx, event->data_len, event->total_data_len,
                 event->current_data_offset);
        if (event->total_data_len == event->data_len) {
            /* Single unfragmented message */
            handle_complete_message(event->data, event->data_len);
        } else {
            /* Fragmented — reassemble */
            if (event->current_data_offset == 0) {
                s_reassembly_total = event->total_data_len;
                s_reassembly_len = 0;
                if (!s_reassembly_buf) {
                    s_reassembly_buf = malloc(REASSEMBLY_BUF_SIZE);
                    ESP_LOGI(TAG, "[%d] Allocated reassembly buffer: %s",
                             s_active_idx, s_reassembly_buf ? "OK" : "FAILED");
                }
            }
            if (s_reassembly_buf &&
                s_reassembly_len + event->data_len < REASSEMBLY_BUF_SIZE - 1) {
                memcpy(s_reassembly_buf + s_reassembly_len,
                       event->data, event->data_len);
                s_reassembly_len += event->data_len;

                if (s_reassembly_len >= s_reassembly_total) {
                    s_reassembly_buf[s_reassembly_len] = '\0';
                    handle_complete_message(s_reassembly_buf, s_reassembly_len);
                    s_reassembly_len = 0;
                    s_reassembly_total = 0;
                }
            } else {
                ESP_LOGW(TAG, "[%d] Reassembly overflow (need %d, have %d)",
                         s_active_idx, s_reassembly_total, REASSEMBLY_BUF_SIZE);
                s_reassembly_len = 0;
                s_reassembly_total = 0;
            }
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "[%d] MQTT error type=%d", s_active_idx,
                 event->error_handle->error_type);
        break;

    default:
        break;
    }
}

static void stop_current_connection(void)
{
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    if (s_active_idx >= 0 && s_mgr) {
        if (printer_manager_lock(s_mgr, pdMS_TO_TICKS(100))) {
            s_mgr->printers[s_active_idx].mqtt_connected = false;
            printer_manager_unlock(s_mgr);
        }
    }
    s_reassembly_len = 0;
    s_reassembly_total = 0;
}

static void connect_to_printer(int idx)
{
    if (idx < 0 || idx >= s_mgr->num_printers) return;

    stop_current_connection();
    s_active_idx = idx;

    printer_state_t *ps = &s_mgr->printers[idx];

    snprintf(s_topic_report, sizeof(s_topic_report), "device/%s/report", ps->serial);
    snprintf(s_topic_request, sizeof(s_topic_request), "device/%s/request", ps->serial);

    char uri[64];
    snprintf(uri, sizeof(uri), "mqtts://%s:8883", ps->ip);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = uri,
            },
            .verification = {
                .certificate = (const char *)bambu_ca_pem_start,
                .skip_cert_common_name_check = true,
            },
        },
        .credentials = {
            .username = "bblp",
            .authentication = {
                .password = ps->access_code,
            },
        },
        .buffer = {
            .size = 16384,
            .out_size = 1024,
        },
        .network = {
            .reconnect_timeout_ms = 5000,
            .disable_auto_reconnect = false,
        },
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);

    ESP_LOGI(TAG, "Connecting to printer %d (%s) at %s", idx, ps->name, ps->ip);
}

void bambu_mqtt_start(printer_manager_t *mgr)
{
    s_mgr = mgr;
    /* Connect to the first (selected) printer */
    connect_to_printer(mgr->selected);
}

void bambu_mqtt_switch(int printer_idx)
{
    if (!s_mgr) return;
    connect_to_printer(printer_idx);
}

void bambu_mqtt_request_pushall(void)
{
    if (s_client && s_active_idx >= 0) {
        const char *cmd = "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}";
        esp_mqtt_client_publish(s_client, s_topic_request, cmd, 0, 0, 0);
        ESP_LOGI(TAG, "[%d] Re-sent pushall request", s_active_idx);
    }
}

void bambu_mqtt_stop(void)
{
    stop_current_connection();
    s_active_idx = -1;
    if (s_reassembly_buf) {
        free(s_reassembly_buf);
        s_reassembly_buf = NULL;
    }
}
