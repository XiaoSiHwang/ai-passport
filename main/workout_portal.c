#include "workout_portal.h"

#include "workout_json.h"
#include "workout_network.h"
#include "esp_http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t workout_setup_html_start[] asm("_binary_workout_setup_html_start");
extern const uint8_t workout_setup_html_end[] asm("_binary_workout_setup_html_end");
static httpd_handle_t s_server;

static bool authorized(httpd_req_t *request) {
    char query[80], token[33];
    workout_network_status_t status;
    workout_network_status(&status);
    if (!status.setup_active || !status.token[0]
        || httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK
        || httpd_query_key_value(query, "token", token, sizeof(token)) != ESP_OK
        || strlen(token) != 32) return false;
    unsigned difference = 0;
    for (unsigned i = 0; i < 32; i++) difference |= (unsigned char)token[i] ^ (unsigned char)status.token[i];
    return difference == 0;
}

static void private_headers(httpd_req_t *request) {
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Connection", "close");
}

static esp_err_t fail(httpd_req_t *request, const char *status, const char *body) {
    private_headers(request);
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, body);
}

static esp_err_t page_handler(httpd_req_t *request) {
    if (!authorized(request)) return fail(request, "403 Forbidden", "{\"error\":\"Scan the device configuration QR code.\"}");
    private_headers(request);
    httpd_resp_set_hdr(request, "Content-Security-Policy",
        "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'");
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, (const char *)workout_setup_html_start,
                           workout_setup_html_end - workout_setup_html_start - 1);
}

static esp_err_t config_handler(httpd_req_t *request) {
    if (request->content_len == 0 || request->content_len > 768) {
        return fail(request, "413 Content Too Large", "{\"error\":\"Invalid configuration size.\"}");
    }
    char body[769] = {0};
    size_t used = 0;
    unsigned timeouts = 0;
    while (used < request->content_len) {
        int received = httpd_req_recv(request, body + used, request->content_len - used);
        if (received == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 2) continue;
        if (received <= 0) return fail(request, "408 Request Timeout", "{\"error\":\"Please submit again.\"}");
        used += (size_t)received;
    }
    workout_config_t config;
    char token[33];
    if (!workout_decode_config(body, used, &config, token)) {
        return fail(request, "400 Bad Request", "{\"error\":\"Check Wi-Fi and server address.\"}");
    }
    bool accepted = workout_network_submit(&config, token);
    memset(body, 0, sizeof(body));
    memset(&config, 0, sizeof(config));
    if (!accepted) return fail(request, "409 Conflict", "{\"error\":\"Session expired or connection already in progress.\"}");
    private_headers(request);
    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"accepted\":true}");
}

static esp_err_t status_handler(httpd_req_t *request) {
    if (!authorized(request)) return fail(request, "403 Forbidden", "{\"error\":\"Session expired.\"}");
    workout_network_status_t status;
    workout_network_status(&status);
    static const char *results[] = {"waiting", "connecting", "saved", "bad_wifi", "storage", "failed", "expired"};
    char body[160];
    snprintf(body, sizeof(body), "{\"result\":\"%s\",\"online\":%s,\"http_status\":%d}",
             results[status.setup_result], status.online ? "true" : "false", status.http_status);
    private_headers(request);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, body);
}

esp_err_t workout_portal_start(void) {
    if (s_server) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.max_open_sockets = 3;
    config.backlog_conn = 2;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page_handler},
        {.uri = "/api/config", .method = HTTP_POST, .handler = config_handler},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        err = httpd_register_uri_handler(s_server, &routes[i]);
        if (err != ESP_OK) { workout_portal_stop(); return err; }
    }
    return ESP_OK;
}

void workout_portal_stop(void) {
    if (!s_server) return;
    httpd_stop(s_server);
    s_server = NULL;
}
