#include "bambu_cloud.h"
#include "bambu_parse.h"
#include "error_lookup.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "bambu_cloud";

/* Cloud API endpoints */
static const char *API_BASE_US = "https://api.bambulab.com";
static const char *API_BASE_CN = "https://api.bambulab.cn";
static const char *LOGIN_PATH  = "/v1/user-service/user/login";
static const char *BIND_PATH   = "/v1/iot-service/api/user/bind";

/* Cloud MQTT brokers */
static const char *MQTT_HOST_US = "us.mqtt.bambulab.com";
static const char *MQTT_HOST_CN = "cn.mqtt.bambulab.com";

/* Embedded Bambu CA certificate bundle */
extern const uint8_t bambu_ca_pem_start[] asm("_binary_bambu_ca_bundle_pem_start");
extern const uint8_t bambu_ca_pem_end[]   asm("_binary_bambu_ca_bundle_pem_end");

/* Stored config */
static char s_email[64];
static char s_password[64];
static char s_region[4] = "us";
static char s_direct_token[2048];

/* Session state */
static char s_access_token[2048];
static char s_mqtt_username[64];

void bambu_cloud_set_config(const device_config_t *cfg)
{
    strncpy(s_email, cfg->cloud_email, sizeof(s_email) - 1);
    strncpy(s_password, cfg->cloud_pass, sizeof(s_password) - 1);
    strncpy(s_region, cfg->cloud_region, sizeof(s_region) - 1);
    strncpy(s_direct_token, cfg->cloud_token, sizeof(s_direct_token) - 1);
    if (s_region[0] == '\0') strcpy(s_region, "us");
}

/* HTTP response buffer */
typedef struct {
    char *data;
    int len;
    int capacity;
} http_response_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *resp = (http_response_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && resp) {
        if (resp->len + evt->data_len < resp->capacity - 1) {
            memcpy(resp->data + resp->len, evt->data, evt->data_len);
            resp->len += evt->data_len;
            resp->data[resp->len] = '\0';
        }
    }
    return ESP_OK;
}

static const char *get_api_base(void)
{
    if (strcmp(s_region, "cn") == 0) return API_BASE_CN;
    return API_BASE_US;
}

static const char *get_mqtt_host(void)
{
    if (strcmp(s_region, "cn") == 0) return MQTT_HOST_CN;
    return MQTT_HOST_US;
}

/* Decode MQTT username from JWT access token */
static bool decode_jwt_username(const char *token)
{
    /* Find the payload section (between first and second dots) */
    const char *first_dot = strchr(token, '.');
    if (!first_dot) return false;
    const char *second_dot = strchr(first_dot + 1, '.');
    if (!second_dot) return false;

    size_t payload_b64_len = second_dot - (first_dot + 1);
    if (payload_b64_len > 2048) return false;

    /* Copy and fix base64url → base64 */
    char b64[2052];
    memcpy(b64, first_dot + 1, payload_b64_len);
    b64[payload_b64_len] = '\0';

    for (size_t i = 0; i < payload_b64_len; i++) {
        if (b64[i] == '-') b64[i] = '+';
        else if (b64[i] == '_') b64[i] = '/';
    }
    /* Pad to multiple of 4 */
    while (payload_b64_len % 4 != 0) {
        b64[payload_b64_len++] = '=';
        b64[payload_b64_len] = '\0';
    }

    /* Decode */
    unsigned char decoded[2048];
    size_t decoded_len = 0;
    int ret = mbedtls_base64_decode(decoded, sizeof(decoded), &decoded_len,
                                     (const unsigned char *)b64, payload_b64_len);
    if (ret != 0 || decoded_len == 0) return false;
    decoded[decoded_len] = '\0';

    /* Parse JSON payload */
    cJSON *root = cJSON_ParseWithLength((const char *)decoded, decoded_len);
    if (!root) return false;

    cJSON *username_item = cJSON_GetObjectItem(root, "username");
    if (cJSON_IsString(username_item) && username_item->valuestring[0]) {
        snprintf(s_mqtt_username, sizeof(s_mqtt_username), "%s", username_item->valuestring);
    } else {
        cJSON *uid_item = cJSON_GetObjectItem(root, "uid");
        if (cJSON_IsNumber(uid_item) && uid_item->valueint > 0) {
            snprintf(s_mqtt_username, sizeof(s_mqtt_username), "u_%d", uid_item->valueint);
        } else if (cJSON_IsString(uid_item) && uid_item->valuestring[0]) {
            snprintf(s_mqtt_username, sizeof(s_mqtt_username), "u_%s", uid_item->valuestring);
        } else {
            cJSON_Delete(root);
            return false;
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "MQTT username: %s", s_mqtt_username);
    return true;
}

bool bambu_cloud_login(void)
{
    /* Check if a direct token is provided */
    if (strlen(s_direct_token) > 0) {
        strncpy(s_access_token, s_direct_token, sizeof(s_access_token) - 1);
        if (decode_jwt_username(s_access_token)) {
            ESP_LOGI(TAG, "Using direct token, username=%s", s_mqtt_username);
            return true;
        }
        ESP_LOGE(TAG, "Direct token invalid — could not decode JWT");
        return false;
    }

    /* Check email/password are set */
    if (strlen(s_email) == 0 || strlen(s_password) == 0) {
        ESP_LOGE(TAG, "Cloud mode requires s_email and s_password in config.h");
        return false;
    }

    /* Build login URL */
    char url[128];
    snprintf(url, sizeof(url), "%s%s", get_api_base(), LOGIN_PATH);

    /* Build request body */
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "account", s_email);
    cJSON_AddStringToObject(body, "password", s_password);
    cJSON_AddStringToObject(body, "apiError", "");
    char *payload = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    /* Allocate response buffer */
    char *resp_buf = malloc(4096);
    if (!resp_buf) { free(payload); return false; }
    http_response_t resp = { .data = resp_buf, .len = 0, .capacity = 4096 };

    /* Perform HTTP POST */
    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(payload);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Login HTTP request failed: %s", esp_err_to_name(err));
        free(resp_buf);
        return false;
    }

    ESP_LOGI(TAG, "Login response status=%d", status);

    /* Parse response */
    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!root) {
        ESP_LOGE(TAG, "Login response JSON parse failed");
        return false;
    }

    /* Check for email verification or 2FA */
    cJSON *login_type = cJSON_GetObjectItem(root, "loginType");
    if (!login_type) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data) login_type = cJSON_GetObjectItem(data, "loginType");
    }
    if (cJSON_IsString(login_type)) {
        if (strcmp(login_type->valuestring, "verifyCode") == 0) {
            ESP_LOGW(TAG, "Email verification required! Set s_direct_token in config.h instead.");
            cJSON_Delete(root);
            return false;
        }
        if (strcmp(login_type->valuestring, "tfa") == 0) {
            ESP_LOGW(TAG, "2FA required! Set s_direct_token in config.h instead.");
            cJSON_Delete(root);
            return false;
        }
    }

    /* Extract access token */
    cJSON *token_item = cJSON_GetObjectItem(root, "accessToken");
    if (!token_item || !cJSON_IsString(token_item)) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data) token_item = cJSON_GetObjectItem(data, "accessToken");
    }
    if (!cJSON_IsString(token_item) || !token_item->valuestring[0]) {
        cJSON *msg = cJSON_GetObjectItem(root, "msg");
        ESP_LOGE(TAG, "Login failed: %s",
                 (cJSON_IsString(msg) && msg->valuestring) ? msg->valuestring : "no token returned");
        cJSON_Delete(root);
        return false;
    }

    strncpy(s_access_token, token_item->valuestring, sizeof(s_access_token) - 1);
    cJSON_Delete(root);

    if (!decode_jwt_username(s_access_token)) {
        ESP_LOGE(TAG, "Failed to decode MQTT username from token");
        return false;
    }

    ESP_LOGI(TAG, "Cloud login successful");
    return true;
}

bool bambu_cloud_fetch_printers(printer_manager_t *mgr)
{
    char url[128];
    snprintf(url, sizeof(url), "%s%s", get_api_base(), BIND_PATH);

    char auth_header[2100];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_access_token);

    char *resp_buf = malloc(8192);
    if (!resp_buf) return false;
    http_response_t resp = { .data = resp_buf, .len = 0, .capacity = 8192 };

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Fetch bindings failed: err=%s status=%d", esp_err_to_name(err), status);
        free(resp_buf);
        return false;
    }

    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!root) return false;

    /* Find devices array */
    cJSON *devices = cJSON_GetObjectItem(root, "devices");
    if (!devices) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data) devices = cJSON_GetObjectItem(data, "devices");
    }

    if (!cJSON_IsArray(devices)) {
        ESP_LOGE(TAG, "No devices array in bindings response");
        cJSON_Delete(root);
        return false;
    }

    if (printer_manager_lock(mgr, pdMS_TO_TICKS(500))) {
        int count = 0;
        int array_size = cJSON_GetArraySize(devices);
        for (int i = 0; i < array_size && count < MAX_PRINTERS; i++) {
            cJSON *dev = cJSON_GetArrayItem(devices, i);
            cJSON *serial = cJSON_GetObjectItem(dev, "dev_id");
            if (!serial) serial = cJSON_GetObjectItem(dev, "serial");
            if (!cJSON_IsString(serial)) continue;

            printer_state_t *ps = &mgr->printers[count];

            /* Get display name */
            cJSON *name = cJSON_GetObjectItem(dev, "name");
            if (!name) name = cJSON_GetObjectItem(dev, "dev_product_name");
            if (!name) name = cJSON_GetObjectItem(dev, "device_name");
            if (cJSON_IsString(name)) {
                strncpy(ps->name, name->valuestring, sizeof(ps->name) - 1);
            } else {
                snprintf(ps->name, sizeof(ps->name), "Printer %d", count + 1);
            }

            strncpy(ps->serial, serial->valuestring, sizeof(ps->serial) - 1);
            ps->ip[0] = '\0';  /* Not needed for cloud mode */
            ps->access_code[0] = '\0';
            ps->state = PRINT_STATE_UNKNOWN;
            ps->mqtt_connected = false;

            ESP_LOGI(TAG, "Discovered printer: %s (serial=%s)", ps->name, ps->serial);
            count++;
        }
        mgr->num_printers = count;
        mgr->selected = 0;
        printer_manager_unlock(mgr);
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Discovered %d printers from cloud", mgr->num_printers);
    return mgr->num_printers > 0;
}

/* --- Cloud MQTT --- */

typedef struct {
    esp_mqtt_client_handle_t client;
    printer_manager_t *mgr;
    int printer_idx;
    char topic_report[64];
    char topic_request[64];
} cloud_mqtt_ctx_t;

static cloud_mqtt_ctx_t s_cloud_ctx[MAX_PRINTERS];

static void parse_cloud_report(printer_state_t *ps, const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) return;

    cJSON *print_obj = cJSON_GetObjectItem(root, "print");
    if (!print_obj) {
        cJSON_Delete(root);
        return;
    }

    cJSON *item;

    if ((item = cJSON_GetObjectItem(print_obj, "gcode_state")) && cJSON_IsString(item)) {
        print_state_t new_state = parse_gcode_state(item->valuestring);
        if (ps->dismissed) {
            if (new_state != PRINT_STATE_FINISHED && new_state != PRINT_STATE_FAILED) {
                ps->dismissed = false;
                ps->state = new_state;
            }
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
        int prev = ps->print_error;
        ps->print_error = item->valueint;
        if (ps->print_error != 0 && ps->print_error != prev)
            error_lookup(ps->print_error, ps->error_message, sizeof(ps->error_message));
        else if (ps->print_error == 0)
            ps->error_message[0] = '\0';
    }

    /* AMS and print stage */
    parse_ams_and_stage(ps, print_obj);

    cJSON_Delete(root);
}

static void cloud_mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    cloud_mqtt_ctx_t *ctx = (cloud_mqtt_ctx_t *)arg;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "[cloud:%d] Connected", ctx->printer_idx);
        if (printer_manager_lock(ctx->mgr, pdMS_TO_TICKS(100))) {
            ctx->mgr->printers[ctx->printer_idx].mqtt_connected = true;
            printer_manager_unlock(ctx->mgr);
        }
        esp_mqtt_client_subscribe(ctx->client, ctx->topic_report, 0);
        /* Request full status dump */
        const char *pushall = "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}";
        esp_mqtt_client_publish(ctx->client, ctx->topic_request, pushall, 0, 0, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "[cloud:%d] Disconnected", ctx->printer_idx);
        if (printer_manager_lock(ctx->mgr, pdMS_TO_TICKS(100))) {
            ctx->mgr->printers[ctx->printer_idx].mqtt_connected = false;
            printer_manager_unlock(ctx->mgr);
        }
        break;

    case MQTT_EVENT_DATA:
        if (event->data_len > 0 && printer_manager_lock(ctx->mgr, pdMS_TO_TICKS(200))) {
            parse_cloud_report(&ctx->mgr->printers[ctx->printer_idx],
                               event->data, event->data_len);
            printer_manager_unlock(ctx->mgr);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "[cloud:%d] MQTT error type=%d", ctx->printer_idx,
                 event->error_handle->error_type);
        break;

    default:
        break;
    }
}

void bambu_cloud_mqtt_start(printer_manager_t *mgr)
{
    const char *mqtt_host = get_mqtt_host();

    for (int i = 0; i < mgr->num_printers; i++) {
        cloud_mqtt_ctx_t *ctx = &s_cloud_ctx[i];
        ctx->mgr = mgr;
        ctx->printer_idx = i;

        printer_state_t *ps = &mgr->printers[i];

        snprintf(ctx->topic_report, sizeof(ctx->topic_report),
                 "device/%s/report", ps->serial);
        snprintf(ctx->topic_request, sizeof(ctx->topic_request),
                 "device/%s/request", ps->serial);

        char uri[128];
        snprintf(uri, sizeof(uri), "mqtts://%s:8883", mqtt_host);

        esp_mqtt_client_config_t mqtt_cfg = {
            .broker = {
                .address = {
                    .uri = uri,
                },
                .verification = {
                    .crt_bundle_attach = esp_crt_bundle_attach,
                },
            },
            .credentials = {
                .username = s_mqtt_username,
                .authentication = {
                    .password = s_access_token,
                },
            },
            .buffer = {
                .size = 8192,
                .out_size = 2048,
            },
            .network = {
                .reconnect_timeout_ms = 5000,
                .disable_auto_reconnect = false,
            },
        };

        ctx->client = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(ctx->client, ESP_EVENT_ANY_ID,
                                       cloud_mqtt_event_handler, ctx);
        esp_mqtt_client_start(ctx->client);

        ESP_LOGI(TAG, "Started cloud MQTT for printer %d (%s) serial=%s",
                 i, ps->name, ps->serial);
    }
}

void bambu_cloud_mqtt_stop(void)
{
    for (int i = 0; i < MAX_PRINTERS; i++) {
        if (s_cloud_ctx[i].client) {
            esp_mqtt_client_stop(s_cloud_ctx[i].client);
            esp_mqtt_client_destroy(s_cloud_ctx[i].client);
            s_cloud_ctx[i].client = NULL;
        }
    }
}

const char *bambu_cloud_get_token(void)
{
    return s_access_token;
}
