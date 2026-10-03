#include "workout_network.h"

#include "workout_json.h"
#include "workout_portal.h"
#include "workout_store.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CONNECTED_BIT BIT0
#define DISCONNECTED_BIT BIT1
#define JUST_CONNECTED_BIT BIT2
#define COMMAND_APPLY 100
#define WORKOUT_SYNC_INTERVAL_MS 300000
#define QUOTA_SYNC_INTERVAL_MS 60000
#define CODEX_SYNC_INTERVAL_MS 5000
#define CODEX_RETRY_INTERVAL_MS 10000
#define RECONNECT_INTERVAL_MS 30000
#define SETUP_TIMEOUT_MS 600000
#define CONNECT_TIMEOUT_MS 20000

static const char *TAG = "workout_net";
static QueueHandle_t s_commands, s_updates, s_quota_updates, s_codex_updates, s_codex_alerts;
static QueueHandle_t s_tokens_updates;
static EventGroupHandle_t s_events;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static workout_network_status_t s_status;
static workout_config_t s_config, s_candidate;
static workout_profiles_t s_profiles, s_profile_candidate;
static workout_retry_t s_retry;
static bool s_profile_error, s_manual_switch;
static bool s_profile_dirty;
static int64_t s_next_profile_save;
static char s_expected_ssid[33];
static workout_cache_t s_cache;
static ai_quota_cache_t s_quota_cache[AI_QUOTA_PROVIDERS];
static ai_quota_update_t s_quota;
static int64_t s_next_quota[AI_QUOTA_PROVIDERS];
static ai_tokens_cache_t s_tokens_cache[AI_QUOTA_PROVIDERS];
static ai_tokens_update_t s_tokens;
static int64_t s_next_tokens[AI_QUOTA_PROVIDERS];
static codex_monitor_state_t s_codex;
static codex_cursor_t s_codex_cursor;
static int64_t s_next_codex_tasks, s_next_codex_alerts;
static unsigned s_sync_turn;
static unsigned s_monitor_budget;
static bool s_monitor_alert_turn;
static bool s_persisted, s_ready;
static bool s_time_started;
static esp_netif_t *s_sta, *s_ap;
static esp_event_handler_instance_t s_wifi_handler, s_ip_handler;
static bool s_wifi_registered, s_ip_registered;
static int64_t s_next_sync, s_setup_deadline, s_pending_deadline, s_close_at;
static void sync_data(void);

typedef struct {
    int kind;
    unsigned selection;
    workout_config_t config;
} network_command_t;

typedef struct {
    char *body;
    size_t length;
    bool oversized;
} http_body_t;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

void workout_network_status(workout_network_status_t *status) {
    portENTER_CRITICAL(&s_status_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_status_lock);
}

static void network_error(workout_network_error_t error, int code) {
    portENTER_CRITICAL(&s_status_lock);
    s_status.error = error;
    s_status.http_status = code;
    s_status.syncing = false;
    s_status.revision++;
    portEXIT_CRITICAL(&s_status_lock);
}

static void setup_result(workout_setup_result_t result, bool pending) {
    portENTER_CRITICAL(&s_status_lock);
    s_status.setup_result = result;
    s_status.pending = pending;
    s_status.revision++;
    portEXIT_CRITICAL(&s_status_lock);
}

static void profiles_publish(void) {
    portENTER_CRITICAL(&s_status_lock);
    s_status.wifi_count = s_profiles.wifi_count;
    s_status.server_count = s_profiles.server_count;
    s_status.active_wifi = s_profiles.active_wifi;
    s_status.active_server = s_profiles.active_server;
    memset(s_status.wifi_names, 0, sizeof(s_status.wifi_names));
    for (unsigned i = 0; i < s_profiles.wifi_count; i++)
        memcpy(s_status.wifi_names[i], s_profiles.wifi[i].ssid, sizeof(s_status.wifi_names[i]));
    memcpy(s_status.servers, s_profiles.servers, sizeof(s_status.servers));
    s_status.has_config = s_profiles.wifi_count != 0;
    s_status.revision++;
    portEXIT_CRITICAL(&s_status_lock);
}

static void switch_result(workout_switch_result_t result) {
    portENTER_CRITICAL(&s_status_lock);
    s_status.switch_result = result;
    s_status.profile_busy = result == WORKOUT_SWITCH_CONNECTING;
    s_status.revision++;
    portEXIT_CRITICAL(&s_status_lock);
}

static void retry_publish(void) {
    portENTER_CRITICAL(&s_status_lock);
    s_status.connecting = s_retry.waiting;
    s_status.exhausted = s_retry.exhausted;
    s_status.connecting_wifi = s_retry.index;
    s_status.attempted = s_retry.attempted;
    s_status.revision++;
    portEXIT_CRITICAL(&s_status_lock);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        /* A delayed DHCP event from an abandoned attempt must not save that candidate. */
        wifi_ap_record_t access;
        if (esp_wifi_sta_get_ap_info(&access) != ESP_OK) return;
        portENTER_CRITICAL(&s_status_lock);
        bool expected = strncmp((const char *)access.ssid, s_expected_ssid, sizeof(access.ssid)) == 0;
        portEXIT_CRITICAL(&s_status_lock);
        if (!expected) return;
        xEventGroupClearBits(s_events, DISCONNECTED_BIT);
        xEventGroupSetBits(s_events, CONNECTED_BIT | JUST_CONNECTED_BIT);
        portENTER_CRITICAL(&s_status_lock);
        s_status.online = true;
        s_status.revision++;
        portEXIT_CRITICAL(&s_status_lock);
    } else if ((base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
               || (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)) {
        wifi_ap_record_t access;
        if (base == WIFI_EVENT && esp_wifi_sta_get_ap_info(&access) == ESP_OK) return;
        xEventGroupClearBits(s_events, CONNECTED_BIT);
        xEventGroupSetBits(s_events, DISCONNECTED_BIT);
        portENTER_CRITICAL(&s_status_lock);
        s_status.online = false;
        s_status.revision++;
        portEXIT_CRITICAL(&s_status_lock);
    }
}

static void wifi_rollback(void) {
    if (s_ip_registered) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
    if (s_wifi_registered) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
    s_ip_registered = s_wifi_registered = false;
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_ap) esp_netif_destroy_default_wifi(s_ap);
    if (s_sta) esp_netif_destroy_default_wifi(s_sta);
    s_ap = s_sta = NULL;
}

static esp_err_t wifi_init(void) {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    s_sta = esp_netif_create_default_wifi_sta();
    s_ap = esp_netif_create_default_wifi_ap();
    if (!s_sta || !s_ap) { wifi_rollback(); return ESP_ERR_NO_MEM; }
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&config);
    if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, &s_wifi_handler);
        s_wifi_registered = err == ESP_OK;
    }
    if (err == ESP_OK) {
        err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, &s_ip_handler);
        s_ip_registered = err == ESP_OK;
    }
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err == ESP_OK) err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (err != ESP_OK) wifi_rollback();
    return err;
}

static esp_err_t connect_config(const workout_config_t *config) {
    esp_wifi_disconnect();
    xEventGroupClearBits(s_events, CONNECTED_BIT | DISCONNECTED_BIT | JUST_CONNECTED_BIT);
    portENTER_CRITICAL(&s_status_lock);
    memcpy(s_expected_ssid, config->ssid, sizeof(s_expected_ssid));
    s_status.online = false;
    s_status.revision++;
    portEXIT_CRITICAL(&s_status_lock);
    wifi_config_t station = {0};
    memcpy(station.sta.ssid, config->ssid, strlen(config->ssid));
    memcpy(station.sta.password, config->password, strlen(config->password));
    station.sta.pmf_cfg.capable = true;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &station);
    memset(&station, 0, sizeof(station));
    if (err == ESP_OK) err = esp_wifi_connect();
    return err;
}

static void connect_profile(unsigned index) {
    workout_config_t config = s_config;
    memcpy(config.ssid, s_profiles.wifi[index].ssid, sizeof(config.ssid));
    memcpy(config.password, s_profiles.wifi[index].password, sizeof(config.password));
    (void)connect_config(&config);
    memset(&config, 0, sizeof(config));
}

static void retry_start(unsigned preferred) {
    workout_retry_start(&s_retry, preferred, now_ms());
    retry_publish();
}

static void heap_log(const char *phase) {
    ESP_LOGI(TAG, "%s: free=%u largest=%u", phase,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static void stop_setup(void) {
    workout_portal_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    portENTER_CRITICAL(&s_status_lock);
    s_status.setup_active = false;
    memset(s_status.ap_password, 0, sizeof(s_status.ap_password));
    memset(s_status.token, 0, sizeof(s_status.token));
    s_status.revision++;
    portEXIT_CRITICAL(&s_status_lock);
    s_setup_deadline = s_close_at = 0;
    heap_log("setup stopped");
}

static void start_setup(void) {
    workout_network_status_t status;
    workout_network_status(&status);
    if (status.setup_active) return;
    uint8_t random[28], mac[6];
    esp_fill_random(random, sizeof(random));
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(status.ap_ssid, sizeof(status.ap_ssid), "Passport-%02X%02X", mac[4], mac[5]);
    const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (unsigned i = 0; i < 12; i++) status.ap_password[i] = alphabet[random[i] & 31];
    status.ap_password[12] = '\0';
    for (unsigned i = 0; i < 16; i++) snprintf(status.token + i * 2, 3, "%02x", random[i + 12]);
    wifi_config_t access = {0};
    memcpy(access.ap.ssid, status.ap_ssid, strlen(status.ap_ssid));
    access.ap.ssid_len = (uint8_t)strlen(status.ap_ssid);
    memcpy(access.ap.password, status.ap_password, 12);
    access.ap.authmode = WIFI_AUTH_WPA2_PSK;
    access.ap.max_connection = 1;
    access.ap.channel = 1;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &access);
    if (err == ESP_OK) err = workout_portal_start();
    memset(&access, 0, sizeof(access));
    if (err != ESP_OK) { stop_setup(); setup_result(WORKOUT_SETUP_FAILED, false); return; }
    portENTER_CRITICAL(&s_status_lock);
    memcpy(s_status.ap_ssid, status.ap_ssid, sizeof(status.ap_ssid));
    memcpy(s_status.ap_password, status.ap_password, sizeof(status.ap_password));
    memcpy(s_status.token, status.token, sizeof(status.token));
    s_status.setup_active = true;
    s_status.setup_result = WORKOUT_SETUP_WAITING;
    s_status.revision++;
    portEXIT_CRITICAL(&s_status_lock);
    s_setup_deadline = now_ms() + SETUP_TIMEOUT_MS;
    heap_log("setup ready");
}

bool workout_network_request(workout_action_t action) {
    if (!s_commands || action == WORKOUT_ACTION_NONE || action == WORKOUT_ACTION_SWITCH_WIFI
        || action == WORKOUT_ACTION_SWITCH_SERVER) return false;
    const network_command_t command = {.kind = action};
    return xQueueSend(s_commands, &command, 0) == pdTRUE;
}

bool workout_network_select(workout_action_t action, unsigned selection) {
    if (!s_commands || (action != WORKOUT_ACTION_SWITCH_WIFI && action != WORKOUT_ACTION_SWITCH_SERVER))
        return false;
    portENTER_CRITICAL(&s_status_lock);
    unsigned count = action == WORKOUT_ACTION_SWITCH_WIFI ? s_status.wifi_count : s_status.server_count;
    bool accepted = selection < count && !s_status.pending && !s_status.profile_busy && !s_status.setup_active;
    if (accepted) {
        s_status.profile_busy = true;
        s_status.switch_result = WORKOUT_SWITCH_CONNECTING;
        if (action == WORKOUT_ACTION_SWITCH_WIFI) s_status.connecting_wifi = selection;
        s_status.revision++;
    }
    portEXIT_CRITICAL(&s_status_lock);
    if (!accepted) return false;
    const network_command_t command = {.kind = action, .selection = selection};
    if (xQueueSend(s_commands, &command, 0) == pdTRUE) return true;
    switch_result(WORKOUT_SWITCH_IDLE);
    return false;
}

bool workout_network_submit(const workout_config_t *config, const char *token) {
    if (!s_commands || !workout_config_valid(config) || strlen(token) != 32) return false;
    unsigned difference = 0;
    portENTER_CRITICAL(&s_status_lock);
    for (unsigned i = 0; i < 32; i++) difference |= (unsigned char)token[i] ^ (unsigned char)s_status.token[i];
    bool accepted = s_status.setup_active && !s_status.pending && !s_status.profile_busy && difference == 0;
    if (accepted) { s_status.pending = true; s_status.setup_result = WORKOUT_SETUP_CONNECTING; s_status.revision++; }
    portEXIT_CRITICAL(&s_status_lock);
    if (!accepted) return false;
    network_command_t command = {.kind = COMMAND_APPLY, .config = *config};
    accepted = xQueueSend(s_commands, &command, 0) == pdTRUE;
    memset(&command, 0, sizeof(command));
    if (!accepted) setup_result(WORKOUT_SETUP_WAITING, false);
    return accepted;
}

bool workout_network_take_update(workout_network_update_t *update) {
    return s_updates && xQueueReceive(s_updates, update, 0) == pdTRUE;
}

bool workout_network_take_quota(ai_quota_update_t *update) {
    return s_quota_updates && xQueueReceive(s_quota_updates, update, 0) == pdTRUE;
}

bool workout_network_take_tokens(ai_tokens_update_t *update) {
    return s_tokens_updates && xQueueReceive(s_tokens_updates, update, 0) == pdTRUE;
}

bool workout_network_take_codex(codex_monitor_state_t *update) {
    return s_codex_updates && xQueueReceive(s_codex_updates, update, 0) == pdTRUE;
}

bool workout_network_take_alert(codex_alert_t *alert) {
    return s_codex_alerts && xQueueReceive(s_codex_alerts, alert, 0) == pdTRUE;
}

static void codex_publish(void) {
    if (s_codex_updates) xQueueOverwrite(s_codex_updates, &s_codex);
}

static void codex_reset(void) {
    memset(&s_codex, 0, sizeof(s_codex));
    strcpy(s_codex.data.stream_id, s_codex_cursor.stream_id);
    codex_alert_t alert;
    while (workout_network_take_alert(&alert)) { /* Discard notifications from the previous stream. */ }
    s_next_codex_tasks = s_next_codex_alerts = 0;
    codex_publish();
}

static void quota_publish(void) {
    xQueueOverwrite(s_quota_updates, &s_quota);
}

static void quota_restore(void) {
    for (unsigned i = 0; i < AI_QUOTA_PROVIDERS; i++) {
        ai_quota_cache_t *cache = &s_quota_cache[i];
        char url[WORKOUT_URL_SIZE];
        bool source = !s_config.server[0] || (ai_quota_url(s_config.server, i, url)
            && cache->source == workout_checksum(url, strlen(url)));
        if (cache->magic && source) {
            s_quota.providers[i] = (ai_quota_state_t){.data = cache->data,
                .available = true, .persisted = true, .from_cache = true};
        }
    }
    quota_publish();
}

static void tokens_publish(void) {
    xQueueOverwrite(s_tokens_updates, &s_tokens);
}

static void tokens_restore(void) {
    for (unsigned i = 0; i < AI_QUOTA_PROVIDERS; i++) {
        ai_tokens_cache_t *cache = &s_tokens_cache[i];
        char url[WORKOUT_URL_SIZE];
        bool source = !s_config.server[0] || (ai_tokens_url(s_config.server, i, url)
            && cache->source == workout_checksum(url, strlen(url)));
        if (cache->magic && source) {
            s_tokens.providers[i] = (ai_tokens_state_t){.data = cache->data,
                .available = true, .persisted = true, .from_cache = true};
        }
    }
    tokens_publish();
}

static esp_err_t http_event(esp_http_client_event_t *event) {
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    http_body_t *body = event->user_data;
    if (event->data_len < 0 || (size_t)event->data_len > WORKOUT_JSON_LIMIT - body->length) {
        body->oversized = true;
        return ESP_FAIL;
    }
    memcpy(body->body + body->length, event->data, (size_t)event->data_len);
    body->length += (size_t)event->data_len;
    return ESP_OK;
}

static void start_clock(void) {
    if (!s_time_started) {
        const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        s_time_started = esp_netif_sntp_init(&config) == ESP_OK;
    }
}

static bool https_clock(void) {
    start_clock(); /* HTTP installations also need the home-screen wall clock. */
    if (strncmp(s_config.server, "https://", 8) != 0 || time(NULL) >= 1704067200) return true;
    if (!s_time_started) return false;
    return esp_netif_sntp_sync_wait(pdMS_TO_TICKS(5000)) == ESP_OK;
}

static bool fetch_body(const char *url, http_body_t *body, int *code, int timeout_ms) {
    *code = 0;
    if (!https_clock()) return false;
    body->body = calloc(1, WORKOUT_JSON_LIMIT + 1);
    if (!body->body) return false;
    esp_http_client_config_t config = {
        .url = url, .timeout_ms = timeout_ms, .buffer_size = 1024,
        .buffer_size_tx = 512, .event_handler = http_event, .user_data = body,
        .disable_auto_redirect = true, .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = client ? esp_http_client_perform(client) : ESP_ERR_NO_MEM;
    *code = client ? esp_http_client_get_status_code(client) : 0;
    if (client) esp_http_client_cleanup(client);
    return err == ESP_OK && *code == 200 && !body->oversized;
}

static bool fetch_data(workout_data_t *data) {
    http_body_t body = {0};
    int code;
    bool received = fetch_body(s_config.server, &body, &code, 8000);
    bool valid = received && workout_decode_response(body.body, body.length, data);
    free(body.body);
    if (!valid) network_error(received ? WORKOUT_NET_RESPONSE : WORKOUT_NET_HTTP, code);
    return valid;
}

static void sync_quota(unsigned provider) {
    ai_quota_state_t *state = &s_quota.providers[provider];
    if (!(xEventGroupGetBits(s_events) & CONNECTED_BIT)) {
        s_next_quota[provider] = now_ms() + RECONNECT_INTERVAL_MS;
        return;
    }
    state->syncing = true;
    quota_publish();
    char url[WORKOUT_URL_SIZE];
    http_body_t body = {0};
    int code = 0;
    ai_quota_data_t data = {0};
    bool valid = ai_quota_url(s_config.server, provider, url) && fetch_body(url, &body, &code, 8000)
        && ai_quota_decode_response(body.body, body.length, provider, &data)
        && ai_quota_newer(&s_quota_cache[provider], &data, url);
    free(body.body);
    state->syncing = false;
    state->failed = !valid;
    state->http_status = code;
    s_next_quota[provider] = now_ms() + (valid ? QUOTA_SYNC_INTERVAL_MS : RECONNECT_INTERVAL_MS);
    if (valid) {
        ai_quota_cache_t cache;
        ai_quota_cache_pack(&cache, &data, url, provider);
        if (!state->persisted || memcmp(&cache, &s_quota_cache[provider], sizeof(cache)) != 0)
            state->persisted = workout_store_save_quota(provider, &cache) == ESP_OK;
        s_quota_cache[provider] = cache;
        state->data = data;
        state->available = true;
        state->from_cache = false;
    }
    quota_publish();
    heap_log("quota sync");
}

static void sync_tokens(unsigned provider) {
    ai_tokens_state_t *state = &s_tokens.providers[provider];
    if (!(xEventGroupGetBits(s_events) & CONNECTED_BIT)) {
        s_next_tokens[provider] = now_ms() + RECONNECT_INTERVAL_MS;
        return;
    }
    state->syncing = true;
    tokens_publish();
    char url[WORKOUT_URL_SIZE];
    http_body_t body = {0};
    int code = 0;
    ai_tokens_data_t data = {0};
    bool valid = ai_tokens_url(s_config.server, provider, url) && fetch_body(url, &body, &code, 8000)
        && ai_tokens_decode_response(body.body, body.length, provider, &data)
        && ai_tokens_newer(&s_tokens_cache[provider], &data, url);
    free(body.body);
    state->syncing = false;
    state->failed = !valid;
    state->http_status = code;
    s_next_tokens[provider] = now_ms() + (valid ? QUOTA_SYNC_INTERVAL_MS : RECONNECT_INTERVAL_MS);
    if (valid) {
        ai_tokens_cache_t cache;
        ai_tokens_cache_pack(&cache, &data, url, provider);
        if (!state->persisted || memcmp(&cache, &s_tokens_cache[provider], sizeof(cache)) != 0)
            state->persisted = workout_store_save_tokens(provider, &cache) == ESP_OK;
        s_tokens_cache[provider] = cache;
        state->data = data;
        state->available = true;
        state->from_cache = false;
    }
    tokens_publish();
    heap_log("token sync");
}

static void tokens_source_changed(void) {
    for (unsigned i = 0; i < AI_QUOTA_PROVIDERS; i++) {
        char url[WORKOUT_URL_SIZE];
        if (s_tokens_cache[i].magic && (!ai_tokens_url(s_config.server, i, url)
            || s_tokens_cache[i].source != workout_checksum(url, strlen(url)))) {
            memset(&s_tokens_cache[i], 0, sizeof(s_tokens_cache[i]));
            memset(&s_tokens.providers[i], 0, sizeof(s_tokens.providers[i]));
        }
        s_next_tokens[i] = 0;
    }
    tokens_publish();
}

static void quota_source_changed(void) {
    for (unsigned i = 0; i < AI_QUOTA_PROVIDERS; i++) {
        char url[WORKOUT_URL_SIZE];
        if (s_quota_cache[i].magic && (!ai_quota_url(s_config.server, i, url)
            || s_quota_cache[i].source != workout_checksum(url, strlen(url)))) {
            memset(&s_quota_cache[i], 0, sizeof(s_quota_cache[i]));
            memset(&s_quota.providers[i], 0, sizeof(s_quota.providers[i]));
        }
        s_next_quota[i] = 0;
    }
    quota_publish();
    tokens_source_changed();
}

static void sync_codex_tasks(void) {
    http_body_t body = {0};
    int code = 0;
    char url[CODEX_URL_SIZE];
    codex_tasks_data_t *data = calloc(1, sizeof(*data));
    bool valid = data && codex_monitor_url(s_config.server, false, false, 0, url)
        && fetch_body(url, &body, &code, 3000) && codex_tasks_decode(body.body, body.length, data);
    free(body.body);
    if (valid && strcmp(data->stream_id, s_codex_cursor.stream_id) == 0
        && s_codex.available && data->revision < s_codex.data.revision) valid = false;
    if (valid) {
        if (codex_cursor_stream(&s_codex_cursor, data->stream_id)) codex_reset();
        s_codex.data = *data;
        s_codex.available = true;
        s_codex.received_ms = now_ms();
    }
    free(data);
    s_codex.failed = !valid;
    s_codex.checked_ms = now_ms();
    s_codex.http_status = code;
    s_next_codex_tasks = now_ms() + (valid ? CODEX_SYNC_INTERVAL_MS
        : code == 404 ? RECONNECT_INTERVAL_MS : CODEX_RETRY_INTERVAL_MS);
    codex_publish();
}

static bool codex_accept_page(const codex_alert_page_t *page) {
    if (strcmp(page->stream_id, s_codex_cursor.stream_id) != 0) {
        (void)codex_cursor_stream(&s_codex_cursor, page->stream_id);
        codex_reset();
        return false; /* Fetch this stream's silent baseline instead of replaying another database. */
    }
    if (!s_codex_cursor.baseline) return codex_cursor_baseline(&s_codex_cursor, page);
    if (page->next_cursor < s_codex_cursor.cursor) return false;
    if (!page->count && (page->has_more || page->next_cursor != s_codex_cursor.cursor)) return false;
    for (unsigned i = 0; i < page->count; i++) {
        codex_alert_t alert = page->alerts[i];
        alert.received_ms = now_ms();
        if (alert.cursor <= s_codex_cursor.cursor) return false;
        if (!codex_alert_seen(&s_codex_cursor, &alert) && codex_alert_notify(&alert)
            && xQueueSend(s_codex_alerts, &alert, 0) != pdTRUE) return false;
        codex_cursor_accept(&s_codex_cursor, &alert);
    }
    s_codex_cursor.cursor = page->next_cursor;
    return true;
}

static void sync_codex_alerts(void) {
    http_body_t body = {0};
    int code = 0;
    char url[CODEX_URL_SIZE];
    codex_alert_page_t *page = calloc(1, sizeof(*page));
    bool valid = page && codex_monitor_url(s_config.server, true, !s_codex_cursor.baseline,
        s_codex_cursor.cursor, url) && fetch_body(url, &body, &code, 3000)
        && codex_alerts_decode(body.body, body.length, page);
    free(body.body);
    bool accepted = valid && codex_accept_page(page);
    if (code == 410) {
        memset(&s_codex_cursor, 0, sizeof(s_codex_cursor));
        codex_reset();
    }
    s_codex.alerts_failed = !accepted;
    bool again = valid && (!s_codex_cursor.baseline || (page->count && (!accepted || page->has_more)));
    s_next_codex_alerts = now_ms() + (again ? 250
        : accepted ? CODEX_SYNC_INTERVAL_MS : code == 410 ? 250
        : code == 404 ? RECONNECT_INTERVAL_MS : CODEX_RETRY_INTERVAL_MS);
    free(page);
    codex_publish();
}

static void sync_due(int64_t now, bool connected) {
    /* Give monitoring two turns between slow legacy requests, without starving either group. */
    int64_t due[] = {s_next_sync, s_next_quota[0], s_next_quota[1], s_next_tokens[0], s_next_tokens[1]};
    int legacy = -1;
    for (unsigned i = 0; i < 5; i++) {
        unsigned kind = (s_sync_turn + i) % 5;
        if (now >= due[kind]) { legacy = (int)kind; break; }
    }
    bool tasks = now >= s_next_codex_tasks, alerts = now >= s_next_codex_alerts;
    if (connected && (s_monitor_budget || legacy < 0) && (tasks || alerts)) {
        bool read_alerts = alerts && (!tasks || s_monitor_alert_turn);
        if (s_monitor_budget) s_monitor_budget--;
        s_monitor_alert_turn = !read_alerts;
        if (read_alerts) sync_codex_alerts();
        else sync_codex_tasks();
        return;
    }
    if (legacy >= 0) {
        s_sync_turn = ((unsigned)legacy + 1) % 5;
        s_monitor_budget = 2;
        if (!legacy) sync_data();
        else if (legacy < 3) sync_quota((unsigned)legacy - 1);
        else sync_tokens((unsigned)legacy - 3);
    } else if (!connected) {
        if (tasks) s_next_codex_tasks = now + CODEX_RETRY_INTERVAL_MS;
        if (alerts) s_next_codex_alerts = now + CODEX_RETRY_INTERVAL_MS;
    }
}

static void sync_data(void) {
    if (!s_config.ssid[0]) return;
    if (!(xEventGroupGetBits(s_events) & CONNECTED_BIT)) {
        network_error(WORKOUT_NET_OFFLINE, 0);
        s_next_sync = now_ms() + RECONNECT_INTERVAL_MS;
        return;
    }
    portENTER_CRITICAL(&s_status_lock);
    s_status.syncing = true;
    s_status.revision++;
    portEXIT_CRITICAL(&s_status_lock);
    workout_data_t data;
    bool valid = fetch_data(&data);
    s_next_sync = now_ms() + (valid ? WORKOUT_SYNC_INTERVAL_MS : RECONNECT_INTERVAL_MS);
    if (!valid) return;
    uint32_t source = workout_checksum(s_config.server, strlen(s_config.server));
    if (s_cache.magic && source == s_cache.source && data.updated_seconds < s_cache.data.updated_seconds) {
        network_error(WORKOUT_NET_RESPONSE, 200);
        return;
    }
    workout_cache_t cache;
    workout_cache_pack(&cache, &data, s_config.server);
    if (!s_persisted || memcmp(&cache, &s_cache, sizeof(cache)) != 0) {
        s_persisted = workout_store_save_cache(&cache) == ESP_OK;
    }
    s_cache = cache;
    const workout_network_update_t update = {.available = true, .persisted = s_persisted, .data = data};
    xQueueOverwrite(s_updates, &update);
    network_error(WORKOUT_NET_OK, 200);
    portENTER_CRITICAL(&s_status_lock);
    s_status.cache_error = !s_persisted;
    s_status.revision++;
    portEXIT_CRITICAL(&s_status_lock);
    heap_log("sync complete");
}

static void cancel_candidate(workout_setup_result_t result) {
    s_pending_deadline = 0;
    memset(&s_candidate, 0, sizeof(s_candidate));
    if (s_config.ssid[0]) {
        retry_start(s_profiles.active_wifi);
        (void)workout_retry_next(&s_retry, s_profiles.wifi_count, now_ms());
        connect_profile(s_retry.index);
        retry_publish();
    }
    else esp_wifi_disconnect();
    setup_result(result, false);
}

static void activate_config(const workout_config_t *config) {
    bool new_source = strcmp(s_config.server, config->server) != 0;
    s_config = *config;
    quota_source_changed();
    if (new_source) {
        memset(&s_codex_cursor, 0, sizeof(s_codex_cursor));
        codex_reset();
        memset(&s_cache, 0, sizeof(s_cache));
        s_persisted = false;
        const workout_network_update_t update = {0};
        xQueueOverwrite(s_updates, &update);
    }
    s_next_sync = 0;
    s_sync_turn = 0;
    s_monitor_budget = 0;
    s_monitor_alert_turn = false;
}

static void confirm_candidate(void) {
    s_profile_candidate = s_profiles;
    if (s_profile_error || !workout_profiles_add(&s_profile_candidate, &s_candidate)
        || workout_store_save_profiles(&s_profile_candidate) != ESP_OK) {
        cancel_candidate(WORKOUT_SETUP_STORAGE);
        memset(&s_profile_candidate, 0, sizeof(s_profile_candidate));
        return;
    }
    s_profiles = s_profile_candidate;
    s_profile_dirty = false;
    activate_config(&s_candidate);
    memset(&s_profile_candidate, 0, sizeof(s_profile_candidate));
    memset(&s_candidate, 0, sizeof(s_candidate));
    s_pending_deadline = 0;
    s_retry.waiting = s_retry.exhausted = false;
    s_retry.index = s_profiles.active_wifi;
    profiles_publish();
    retry_publish();
    setup_result(WORKOUT_SETUP_SAVED, false);
    s_close_at = now_ms() + 10000;
}

static void confirm_profile(void) {
    unsigned index = s_retry.index;
    bool changed = s_profiles.active_wifi != index;
    s_profile_candidate = s_profiles;
    s_profile_candidate.active_wifi = index;
    workout_profiles_seal(&s_profile_candidate);
    if (changed && workout_store_save_profiles(&s_profile_candidate) != ESP_OK) {
        if (s_manual_switch) {
            s_manual_switch = false;
            switch_result(WORKOUT_SWITCH_STORAGE);
            esp_wifi_disconnect();
            xEventGroupClearBits(s_events, CONNECTED_BIT | JUST_CONNECTED_BIT);
            portENTER_CRITICAL(&s_status_lock);
            s_status.online = false;
            s_status.revision++;
            portEXIT_CRITICAL(&s_status_lock);
            retry_start(s_profiles.active_wifi);
            memset(&s_profile_candidate, 0, sizeof(s_profile_candidate));
            return;
        }
        /* Keep an automatically recovered connection usable; retry its preference write later. */
        s_profile_dirty = true;
        s_next_profile_save = now_ms() + RECONNECT_INTERVAL_MS;
        switch_result(WORKOUT_SWITCH_STORAGE);
    } else if (changed) s_profile_dirty = false;
    s_profiles = s_profile_candidate;
    (void)workout_profiles_config(&s_profiles, &s_config);
    memset(&s_profile_candidate, 0, sizeof(s_profile_candidate));
    s_retry.waiting = s_retry.exhausted = false;
    profiles_publish();
    retry_publish();
    if (s_manual_switch) switch_result(WORKOUT_SWITCH_SAVED);
    s_manual_switch = false;
}

static void select_profile(const network_command_t *command) {
    workout_network_status_t status;
    workout_network_status(&status);
    if (s_pending_deadline || status.setup_active || s_profile_error) {
        switch_result(s_profile_error ? WORKOUT_SWITCH_STORAGE : WORKOUT_SWITCH_FAILED);
        return;
    }
    if (command->kind == WORKOUT_ACTION_SWITCH_WIFI) {
        if (command->selection >= s_profiles.wifi_count) { switch_result(WORKOUT_SWITCH_FAILED); return; }
        s_manual_switch = true;
        retry_start(command->selection);
        (void)workout_retry_next(&s_retry, s_profiles.wifi_count, now_ms());
        connect_profile(command->selection);
        retry_publish();
        return;
    }
    if (command->selection >= s_profiles.server_count) { switch_result(WORKOUT_SWITCH_FAILED); return; }
    s_profile_candidate = s_profiles;
    s_profile_candidate.active_server = command->selection;
    workout_profiles_seal(&s_profile_candidate);
    if ((s_profile_candidate.active_server != s_profiles.active_server || s_profile_dirty)
        && workout_store_save_profiles(&s_profile_candidate) != ESP_OK) {
        switch_result(WORKOUT_SWITCH_STORAGE);
    } else {
        s_profiles = s_profile_candidate;
        workout_config_t config;
        (void)workout_profiles_config(&s_profiles, &config);
        activate_config(&config);
        memset(&config, 0, sizeof(config));
        s_profile_dirty = false;
        profiles_publish();
        network_error(status.online ? WORKOUT_NET_OK : WORKOUT_NET_OFFLINE, 0);
        switch_result(WORKOUT_SWITCH_SAVED);
    }
    memset(&s_profile_candidate, 0, sizeof(s_profile_candidate));
}

static void retry_tick(int64_t now, EventBits_t bits) {
    if (bits & CONNECTED_BIT) {
        if (s_retry.waiting) confirm_profile();
        if (s_profile_dirty && now >= s_next_profile_save) {
            s_next_profile_save = now + RECONNECT_INTERVAL_MS;
            if (workout_store_save_profiles(&s_profiles) == ESP_OK) {
                s_profile_dirty = false;
                switch_result(WORKOUT_SWITCH_SAVED);
            }
        }
        return;
    }
    if (!s_profiles.wifi_count) return;
    if (s_manual_switch) {
        if (now < s_retry.deadline) return;
        s_manual_switch = false;
        switch_result(WORKOUT_SWITCH_FAILED);
        retry_start(s_profiles.active_wifi);
    } else if ((bits & DISCONNECTED_BIT) && !s_retry.waiting && !s_retry.exhausted) {
        retry_start(s_profiles.active_wifi);
    }
    xEventGroupClearBits(s_events, DISCONNECTED_BIT);
    bool exhausted = s_retry.exhausted;
    int index = workout_retry_next(&s_retry, s_profiles.wifi_count, now);
    if (index >= 0) { connect_profile((unsigned)index); retry_publish(); }
    else if (exhausted != s_retry.exhausted) {
        esp_wifi_disconnect();
        retry_publish();
    }
}

static void connection_cleared(void) {
    portENTER_CRITICAL(&s_status_lock);
    s_status.online = false;
    s_status.connecting = false;
    s_status.exhausted = false;
    s_status.switch_result = WORKOUT_SWITCH_IDLE;
    s_status.profile_busy = false;
    s_status.error = WORKOUT_NET_OFFLINE;
    s_status.http_status = 0;
    s_status.syncing = false;
    memset(s_expected_ssid, 0, sizeof(s_expected_ssid));
    s_status.revision++;
    portEXIT_CRITICAL(&s_status_lock);
}

static void clear_config(void) {
    if (workout_store_clear_config() != ESP_OK) { setup_result(WORKOUT_SETUP_STORAGE, false); return; }
    s_pending_deadline = 0;
    memset(&s_candidate, 0, sizeof(s_candidate));
    memset(&s_config, 0, sizeof(s_config));
    memset(&s_codex_cursor, 0, sizeof(s_codex_cursor));
    codex_reset();
    memset(&s_profiles, 0, sizeof(s_profiles));
    memset(&s_retry, 0, sizeof(s_retry));
    s_profile_error = s_profile_dirty = s_manual_switch = false;
    esp_wifi_disconnect();
    xEventGroupClearBits(s_events, CONNECTED_BIT | DISCONNECTED_BIT | JUST_CONNECTED_BIT);
    connection_cleared();
    profiles_publish();
    setup_result(WORKOUT_SETUP_WAITING, false);
    s_close_at = 0;
    stop_setup();
    start_setup();
}

static void process_command(const network_command_t *command) {
    if (!s_ready) {
        s_ready = wifi_init() == ESP_OK;
        if (!s_ready) { network_error(WORKOUT_NET_INIT, 0); switch_result(WORKOUT_SWITCH_FAILED); return; }
        if (s_config.ssid[0]) retry_start(s_profiles.active_wifi);
    }
    if (command->kind == WORKOUT_ACTION_SETUP_START) {
        if (!s_manual_switch) start_setup();
    }
    else if (command->kind == WORKOUT_ACTION_SETUP_STOP) {
        if (s_pending_deadline) cancel_candidate(WORKOUT_SETUP_WAITING);
        stop_setup();
    } else if (command->kind == WORKOUT_ACTION_SYNC) {
        if (s_config.ssid[0] && !s_pending_deadline) {
            s_next_sync = 0;
            for (unsigned i = 0; i < AI_QUOTA_PROVIDERS; i++) s_next_quota[i] = s_next_tokens[i] = 0;
            s_next_codex_tasks = s_next_codex_alerts = 0;
        }
    } else if (command->kind == WORKOUT_ACTION_CODEX_SYNC) {
        s_next_codex_tasks = s_next_codex_alerts = 0;
    } else if (command->kind == WORKOUT_ACTION_RECONNECT) {
        workout_network_status_t status;
        workout_network_status(&status);
        if (!s_pending_deadline && !s_manual_switch && !status.setup_active) {
            if (!(xEventGroupGetBits(s_events) & CONNECTED_BIT)) retry_start(s_profiles.active_wifi);
            s_next_sync = 0;
        }
    } else if (command->kind == WORKOUT_ACTION_SWITCH_WIFI || command->kind == WORKOUT_ACTION_SWITCH_SERVER)
        select_profile(command);
    else if (command->kind == WORKOUT_ACTION_CLEAR) clear_config();
    else if (command->kind == COMMAND_APPLY) {
        workout_network_status_t status;
        workout_network_status(&status);
        if (!status.setup_active) { setup_result(WORKOUT_SETUP_EXPIRED, false); return; }
        s_candidate = command->config;
        s_pending_deadline = now_ms() + CONNECT_TIMEOUT_MS;
        if (connect_config(&s_candidate) != ESP_OK) cancel_candidate(WORKOUT_SETUP_BAD_WIFI);
    }
}

static void network_tick(void) {
    int64_t now = now_ms();
    EventBits_t bits = xEventGroupGetBits(s_events);
    if (bits & JUST_CONNECTED_BIT) {
        xEventGroupClearBits(s_events, JUST_CONNECTED_BIT);
        start_clock();
        s_next_sync = 0;
        for (unsigned i = 0; i < AI_QUOTA_PROVIDERS; i++) s_next_quota[i] = s_next_tokens[i] = 0;
        s_next_codex_tasks = s_next_codex_alerts = 0;
        s_sync_turn = 0;
        s_monitor_budget = 0;
        s_monitor_alert_turn = false;
    }
    if (s_pending_deadline) {
        if (bits & CONNECTED_BIT) confirm_candidate();
        else if (now >= s_pending_deadline) cancel_candidate(WORKOUT_SETUP_BAD_WIFI);
        return;
    }
    if (s_close_at && now >= s_close_at) stop_setup();
    if (s_setup_deadline && now >= s_setup_deadline) {
        stop_setup();
        setup_result(WORKOUT_SETUP_EXPIRED, false);
    }
    workout_network_status_t status;
    workout_network_status(&status);
    if (!status.setup_active) retry_tick(now, bits);
    if (!s_config.ssid[0]) return;
    /* One request per tick lets provisioning commands run between endpoints. */
    sync_due(now, (bits & CONNECTED_BIT) != 0);
}

static void network_task(void *arg) {
    (void)arg;
    s_ready = wifi_init() == ESP_OK;
    if (s_ready) {
        if (s_config.ssid[0]) retry_start(s_profiles.active_wifi);
        else start_setup();
    } else network_error(WORKOUT_NET_INIT, 0);
    network_command_t command;
    for (;;) {
        if (xQueueReceive(s_commands, &command, pdMS_TO_TICKS(250)) == pdTRUE) {
            process_command(&command);
            memset(&command, 0, sizeof(command));
        }
        if (s_ready) network_tick();
    }
}

esp_err_t workout_network_start(const workout_config_t *config, const workout_cache_t *cache) {
    s_config = *config;
    esp_err_t err = workout_store_load_profiles(&s_profiles);
    s_profile_error = err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND;
    if (s_profile_error) s_status.error = WORKOUT_NET_STORAGE;
    if (err == ESP_OK) (void)workout_profiles_config(&s_profiles, &s_config);
    profiles_publish();
    if (workout_cache_valid(cache, sizeof(*cache))) { s_cache = *cache; s_persisted = true; }
    s_status.has_config = s_config.ssid[0] != '\0';
    s_events = xEventGroupCreate();
    s_commands = xQueueCreate(3, sizeof(network_command_t));
    s_updates = xQueueCreate(1, sizeof(workout_network_update_t));
    s_quota_updates = xQueueCreate(1, sizeof(ai_quota_update_t));
    s_tokens_updates = xQueueCreate(1, sizeof(ai_tokens_update_t));
    s_codex_updates = xQueueCreate(1, sizeof(codex_monitor_state_t));
    s_codex_alerts = xQueueCreate(CODEX_ALERT_LIMIT, sizeof(codex_alert_t));
    memset(&s_codex_cursor, 0, sizeof(s_codex_cursor));
    codex_reset();
    s_sync_turn = 0;
    s_monitor_budget = 0;
    s_monitor_alert_turn = false;
    bool queues = s_events && s_commands && s_updates && s_quota_updates && s_tokens_updates
        && s_codex_updates && s_codex_alerts;
    if (queues) {
        for (unsigned i = 0; i < AI_QUOTA_PROVIDERS; i++) {
            (void)workout_store_load_quota(i, &s_quota_cache[i]);
            (void)workout_store_load_tokens(i, &s_tokens_cache[i]);
        }
        quota_restore();
        tokens_restore();
    }
    if (queues && xTaskCreate(network_task, "workout_net", 7168, NULL, 4, NULL) == pdPASS) return ESP_OK;
    if (s_events) vEventGroupDelete(s_events);
    if (s_commands) vQueueDelete(s_commands);
    if (s_updates) vQueueDelete(s_updates);
    if (s_quota_updates) vQueueDelete(s_quota_updates);
    if (s_tokens_updates) vQueueDelete(s_tokens_updates);
    if (s_codex_updates) vQueueDelete(s_codex_updates);
    if (s_codex_alerts) vQueueDelete(s_codex_alerts);
    s_commands = s_updates = NULL;
    s_quota_updates = NULL;
    s_tokens_updates = NULL;
    s_codex_updates = s_codex_alerts = NULL;
    s_events = NULL;
    network_error(WORKOUT_NET_INIT, 0);
    return ESP_ERR_NO_MEM;
}
