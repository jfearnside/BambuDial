#include "setup_portal.h"
#include "setup_page.h"
#include "config_store.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "lwip/sockets.h"
#include <string.h>

static const char *TAG = "portal";

static httpd_handle_t s_server = NULL;
static TaskHandle_t s_dns_task = NULL;

/* --- DNS Captive Portal --- */
/* Simple DNS responder: responds to ALL queries with 192.168.4.1 */

static void dns_server_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    uint8_t buf[512];
    struct sockaddr_in client;
    socklen_t client_len;

    ESP_LOGI(TAG, "DNS captive portal started");

    while (1) {
        client_len = sizeof(client);
        int len = recvfrom(sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&client, &client_len);
        if (len < 12) continue;

        /* Build response: copy query, set response flags, add A record → 192.168.4.1 */
        uint8_t resp[512];
        memcpy(resp, buf, len);

        /* Set QR=1 (response), AA=1, RCODE=0 */
        resp[2] = 0x84;
        resp[3] = 0x00;
        /* ANCOUNT = 1 */
        resp[6] = 0x00;
        resp[7] = 0x01;

        int rlen = len;
        /* Pointer to question name */
        resp[rlen++] = 0xC0;
        resp[rlen++] = 0x0C;
        /* Type A */
        resp[rlen++] = 0x00; resp[rlen++] = 0x01;
        /* Class IN */
        resp[rlen++] = 0x00; resp[rlen++] = 0x01;
        /* TTL = 60 */
        resp[rlen++] = 0x00; resp[rlen++] = 0x00;
        resp[rlen++] = 0x00; resp[rlen++] = 0x3C;
        /* RDLENGTH = 4 */
        resp[rlen++] = 0x00; resp[rlen++] = 0x04;
        /* RDATA = 192.168.4.1 */
        resp[rlen++] = 192; resp[rlen++] = 168;
        resp[rlen++] = 4;   resp[rlen++] = 1;

        sendto(sock, resp, rlen, 0,
               (struct sockaddr *)&client, client_len);
    }

    close(sock);
    vTaskDelete(NULL);
}

/* --- HTTP Handlers --- */

/* GET / — serve the setup page HTML */
static esp_err_t handle_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SETUP_HTML, strlen(SETUP_HTML));
    return ESP_OK;
}

/* Captive portal detection endpoints — redirect to root */
static esp_err_t handle_captive_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* GET /api/config — return current config as JSON */
static esp_err_t handle_get_config(httpd_req_t *req)
{
    device_config_t cfg;
    config_store_load(&cfg);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifi_ssid", cfg.wifi_ssid);
    cJSON_AddStringToObject(root, "wifi_pass", cfg.wifi_pass);
    cJSON_AddStringToObject(root, "mode", cfg.cloud_mode ? "cloud" : "local");
    cJSON_AddStringToObject(root, "cloud_email", cfg.cloud_email);
    cJSON_AddStringToObject(root, "cloud_region", cfg.cloud_region);
    cJSON_AddStringToObject(root, "timezone", cfg.timezone);
    cJSON_AddBoolToObject(root, "auto_rotate", cfg.auto_rotate);
    cJSON_AddNumberToObject(root, "auto_rotate_s", cfg.auto_rotate_s);

    cJSON *printers = cJSON_AddArrayToObject(root, "printers");
    for (int i = 0; i < cfg.num_printers; i++) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", cfg.printers[i].name);
        cJSON_AddStringToObject(p, "ip", cfg.printers[i].ip);
        cJSON_AddStringToObject(p, "serial", cfg.printers[i].serial);
        cJSON_AddStringToObject(p, "access_code", cfg.printers[i].access_code);
        cJSON_AddItemToArray(printers, p);
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/* POST /api/config — save config and reboot */
static esp_err_t handle_post_config(httpd_req_t *req)
{
    /* Read POST body */
    char *body = malloc(req->content_len + 1);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) { free(body); httpd_resp_send_500(req); return ESP_FAIL; }
        received += ret;
    }
    body[received] = '\0';

    /* Parse JSON */
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    device_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    /* WiFi */
    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "wifi_ssid")) && cJSON_IsString(item))
        strncpy(cfg.wifi_ssid, item->valuestring, sizeof(cfg.wifi_ssid) - 1);
    if ((item = cJSON_GetObjectItem(root, "wifi_pass")) && cJSON_IsString(item))
        strncpy(cfg.wifi_pass, item->valuestring, sizeof(cfg.wifi_pass) - 1);

    /* Mode */
    if ((item = cJSON_GetObjectItem(root, "mode")) && cJSON_IsString(item))
        cfg.cloud_mode = (strcmp(item->valuestring, "cloud") == 0);

    /* Cloud */
    if ((item = cJSON_GetObjectItem(root, "cloud_email")) && cJSON_IsString(item))
        strncpy(cfg.cloud_email, item->valuestring, sizeof(cfg.cloud_email) - 1);
    if ((item = cJSON_GetObjectItem(root, "cloud_pass")) && cJSON_IsString(item))
        strncpy(cfg.cloud_pass, item->valuestring, sizeof(cfg.cloud_pass) - 1);
    if ((item = cJSON_GetObjectItem(root, "cloud_region")) && cJSON_IsString(item))
        strncpy(cfg.cloud_region, item->valuestring, sizeof(cfg.cloud_region) - 1);
    if ((item = cJSON_GetObjectItem(root, "cloud_token")) && cJSON_IsString(item))
        strncpy(cfg.cloud_token, item->valuestring, sizeof(cfg.cloud_token) - 1);

    /* Display */
    if ((item = cJSON_GetObjectItem(root, "timezone")) && cJSON_IsString(item))
        strncpy(cfg.timezone, item->valuestring, sizeof(cfg.timezone) - 1);
    if ((item = cJSON_GetObjectItem(root, "auto_rotate")) && cJSON_IsNumber(item))
        cfg.auto_rotate = item->valueint != 0;
    if ((item = cJSON_GetObjectItem(root, "auto_rotate_s")) && cJSON_IsNumber(item))
        cfg.auto_rotate_s = item->valueint;

    /* Printers */
    cJSON *printers = cJSON_GetObjectItem(root, "printers");
    if (cJSON_IsArray(printers)) {
        int count = cJSON_GetArraySize(printers);
        if (count > MAX_PRINTERS) count = MAX_PRINTERS;
        cfg.num_printers = count;
        for (int i = 0; i < count; i++) {
            cJSON *p = cJSON_GetArrayItem(printers, i);
            if (!p) continue;
            if ((item = cJSON_GetObjectItem(p, "name")) && cJSON_IsString(item))
                strncpy(cfg.printers[i].name, item->valuestring, sizeof(cfg.printers[i].name) - 1);
            if ((item = cJSON_GetObjectItem(p, "ip")) && cJSON_IsString(item))
                strncpy(cfg.printers[i].ip, item->valuestring, sizeof(cfg.printers[i].ip) - 1);
            if ((item = cJSON_GetObjectItem(p, "serial")) && cJSON_IsString(item))
                strncpy(cfg.printers[i].serial, item->valuestring, sizeof(cfg.printers[i].serial) - 1);
            if ((item = cJSON_GetObjectItem(p, "access_code")) && cJSON_IsString(item))
                strncpy(cfg.printers[i].access_code, item->valuestring, sizeof(cfg.printers[i].access_code) - 1);
        }
    }

    cJSON_Delete(root);

    /* Defaults */
    if (cfg.timezone[0] == '\0') strcpy(cfg.timezone, "EST5EDT,M3.2.0,M11.1.0");
    if (cfg.cloud_region[0] == '\0') strcpy(cfg.cloud_region, "us");
    if (cfg.auto_rotate_s < 5) cfg.auto_rotate_s = 30;

    /* Save to NVS */
    esp_err_t err = config_store_save(&cfg);
    if (err != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    /* Send success response */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", 15);

    /* Reboot after a short delay to let the response send */
    ESP_LOGI(TAG, "Config saved, rebooting in 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

/* --- Portal Startup --- */

void setup_portal_start(void)
{
    ESP_LOGI(TAG, "Starting setup portal...");

    /* Start WiFi in AP mode — netif and event loop must already be initialized */
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_cfg);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = "BambuDial-Setup",
            .ssid_len = 14,
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started: BambuDial-Setup");

    /* Start DNS captive portal */
    xTaskCreate(dns_server_task, "dns_srv", 4096, NULL, 5, &s_dns_task);

    /* Start HTTP server */
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 10;
    http_cfg.uri_match_fn = httpd_uri_match_wildcard;
    http_cfg.stack_size = 16384;  /* larger stack for serving HTML page */

    ESP_ERROR_CHECK(httpd_start(&s_server, &http_cfg));

    /* Register handlers */
    httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = handle_root };
    httpd_uri_t uri_get_cfg = { .uri = "/api/config", .method = HTTP_GET, .handler = handle_get_config };
    httpd_uri_t uri_post_cfg = { .uri = "/api/config", .method = HTTP_POST, .handler = handle_post_config };

    /* Captive portal detection URLs */
    httpd_uri_t uri_gen204 = { .uri = "/generate_204", .method = HTTP_GET, .handler = handle_captive_redirect };
    httpd_uri_t uri_hotspot = { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = handle_captive_redirect };
    httpd_uri_t uri_ncsi = { .uri = "/connecttest.txt", .method = HTTP_GET, .handler = handle_captive_redirect };
    httpd_uri_t uri_fallback = { .uri = "/*", .method = HTTP_GET, .handler = handle_captive_redirect };

    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_get_cfg);
    httpd_register_uri_handler(s_server, &uri_post_cfg);
    httpd_register_uri_handler(s_server, &uri_gen204);
    httpd_register_uri_handler(s_server, &uri_hotspot);
    httpd_register_uri_handler(s_server, &uri_ncsi);
    httpd_register_uri_handler(s_server, &uri_fallback);

    ESP_LOGI(TAG, "HTTP server started at http://192.168.4.1/");
}

void setup_portal_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    if (s_dns_task) {
        vTaskDelete(s_dns_task);
        s_dns_task = NULL;
    }
    esp_wifi_stop();
}
