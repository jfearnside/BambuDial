#pragma once

#include "printer_state.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

static const char *PARSE_TAG = "bambu_parse";

/* Parse AMS and stage data from a Bambu "print" JSON object into printer state.
 * Shared between bambu_mqtt.c and bambu_cloud.c to avoid duplication. */
static inline void parse_ams_and_stage(printer_state_t *ps, cJSON *print_obj)
{
    cJSON *item;

    /* Print stage */
    if ((item = cJSON_GetObjectItem(print_obj, "stg_cur")) && cJSON_IsNumber(item))
        ps->stg_cur = (int8_t)item->valueint;

    /* AMS data */
    cJSON *ams_obj = cJSON_GetObjectItem(print_obj, "ams");
    if (!ams_obj) return;

    /* Active tray — Bambu sends as string or number depending on firmware */
    item = cJSON_GetObjectItem(ams_obj, "tray_now");
    if (item) {
        if (cJSON_IsString(item))
            ps->tray_now = (uint8_t)atoi(item->valuestring);
        else if (cJSON_IsNumber(item))
            ps->tray_now = (uint8_t)item->valueint;
    }

    /* AMS units array (confusingly also named "ams" inside the "ams" object) */
    cJSON *ams_arr = cJSON_GetObjectItem(ams_obj, "ams");
    if (!cJSON_IsArray(ams_arr)) return;

    int unit_count = cJSON_GetArraySize(ams_arr);
    if (unit_count > 4) unit_count = 4;
    ps->ams_count = (uint8_t)unit_count;

    for (int u = 0; u < unit_count; u++) {
        cJSON *unit = cJSON_GetArrayItem(ams_arr, u);
        if (!unit) continue;

        /* Humidity: prefer humidity_raw (0-100), fallback to humidity (1-5 scale)
         * Bambu sends these as strings OR numbers depending on firmware version */
        cJSON *hum_raw = cJSON_GetObjectItem(unit, "humidity_raw");
        cJSON *hum_idx = cJSON_GetObjectItem(unit, "humidity");
        if (hum_raw) {
            if (cJSON_IsNumber(hum_raw))
                ps->ams[u].humidity = (uint8_t)hum_raw->valueint;
            else if (cJSON_IsString(hum_raw))
                ps->ams[u].humidity = (uint8_t)atoi(hum_raw->valuestring);
        } else if (hum_idx) {
            int idx_val = 0;
            if (cJSON_IsNumber(hum_idx))
                idx_val = hum_idx->valueint;
            else if (cJSON_IsString(hum_idx))
                idx_val = atoi(hum_idx->valuestring);
            if (idx_val >= 1 && idx_val <= 5) {
                static const uint8_t hum_map[] = {0, 10, 30, 50, 70, 90};
                ps->ams[u].humidity = hum_map[idx_val];
            } else if (idx_val > 5 && idx_val <= 100) {
                /* Some firmware sends raw 0-100 in the "humidity" field */
                ps->ams[u].humidity = (uint8_t)idx_val;
            }
        }

        ESP_LOGI(PARSE_TAG, "AMS unit %d: humidity=%d (raw_present=%d idx_present=%d)",
                 u, ps->ams[u].humidity, hum_raw != NULL, hum_idx != NULL);

        /* Trays */
        cJSON *tray_arr = cJSON_GetObjectItem(unit, "tray");
        if (!cJSON_IsArray(tray_arr)) continue;

        int tray_count = cJSON_GetArraySize(tray_arr);
        if (tray_count > 4) tray_count = 4;

        for (int t = 0; t < tray_count; t++) {
            cJSON *tray = cJSON_GetArrayItem(tray_arr, t);
            if (!tray) continue;

            cJSON *tt = cJSON_GetObjectItem(tray, "tray_type");
            if (tt && cJSON_IsString(tt))
                ps->ams[u].trays[t].type = parse_filament_type(tt->valuestring);

            cJSON *tc = cJSON_GetObjectItem(tray, "tray_color");
            if (tc && cJSON_IsString(tc) && strlen(tc->valuestring) >= 6) {
                /* Parse RRGGBBAA or RRGGBB hex → keep RRGGBB */
                uint32_t full = (uint32_t)strtoul(tc->valuestring, NULL, 16);
                if (strlen(tc->valuestring) >= 8)
                    ps->ams[u].trays[t].color = full >> 8;  /* strip alpha */
                else
                    ps->ams[u].trays[t].color = full;
            }

            cJSON *tr = cJSON_GetObjectItem(tray, "remain");
            if (tr && cJSON_IsNumber(tr))
                ps->ams[u].trays[t].remain = (uint8_t)tr->valueint;
        }
    }
}
