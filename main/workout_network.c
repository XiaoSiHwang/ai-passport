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
#define SYNC_INTERVAL_MS 300000
#define RECONNECT_INTERVAL_MS 30000
#define SETUP_TIMEOUT_MS 600000
#define CONNECT_TIMEOUT_MS 20000

static const char *TAG = "workout_net";
static QueueHandle_t s_commands, s_updates;
static EventGroupHandle_t s_events;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static workout_network_status_t s_status;
static workout_config_t s_config, s_candidate;
static workout_cache_t s_cache;
static bool s_persisted, s_ready;
static bool s_time_started;
static esp_netif_t *s_sta, *s_ap;
static esp_event_handler_instance_t s_wifi_handler, s_ip_handler;
static bool s_wifi_registered, s_ip_registered;
static int64_t s_next_sync, s_next_connect, s_setup_deadline, s_pending_deadline, s_close_at;

typedef struct {
    int kind;
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

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupClearBits(s_events, DISCONNECTED_BIT);
        xEventGroupSetBits(s_events, CONNECTED_BIT | JUST_CONNECTED_BIT);
        portENTER_CRITICAL(&s_status_lock);
        s_status.online = true;
        s_status.revision++;
        portEXIT_CRITICAL(&s_status_lock);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
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
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL, &s_ip_handler);
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
    s_next_connect = now_ms() + RECONNECT_INTERVAL_MS;
    return err;
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
    if (!s_commands || action == WORKOUT_ACTION_NONE) return false;
    const network_command_t command = {.kind = action};
    return xQueueSend(s_commands, &command, 0) == pdTRUE;
}

bool workout_network_submit(const workout_config_t *config, const char *token) {
    if (!s_commands || !workout_config_valid(config) || strlen(token) != 32) return false;
    unsigned difference = 0;
    portENTER_CRITICAL(&s_status_lock);
    for (unsigned i = 0; i < 32; i++) difference |= (unsigned char)token[i] ^ (unsigned char)s_status.token[i];
    bool accepted = s_status.setup_active && !s_status.pending && difference == 0;
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

static bool https_clock(void) {
    if (strncmp(s_config.server, "https://", 8) != 0 || time(NULL) >= 1704067200) return true;
    if (!s_time_started) {
        const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        if (esp_netif_sntp_init(&config) != ESP_OK) return false;
        s_time_started = true;
    }
    return esp_netif_sntp_sync_wait(pdMS_TO_TICKS(5000)) == ESP_OK;
}

static bool fetch_data(workout_data_t *data) {
    if (!https_clock()) { network_error(WORKOUT_NET_HTTP, 0); return false; }
    http_body_t body = {.body = calloc(1, WORKOUT_JSON_LIMIT + 1)};
    if (!body.body) { network_error(WORKOUT_NET_RESPONSE, 0); return false; }
    esp_http_client_config_t config = {
        .url = s_config.server, .timeout_ms = 8000, .buffer_size = 1024,
        .buffer_size_tx = 512, .event_handler = http_event, .user_data = &body,
        .disable_auto_redirect = true, .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = client ? esp_http_client_perform(client) : ESP_ERR_NO_MEM;
    int code = client ? esp_http_client_get_status_code(client) : 0;
    if (client) esp_http_client_cleanup(client);
    bool valid = err == ESP_OK && code == 200 && !body.oversized
                 && workout_decode_response(body.body, body.length, data);
    free(body.body);
    if (!valid) network_error(err != ESP_OK || code != 200 ? WORKOUT_NET_HTTP : WORKOUT_NET_RESPONSE, code);
    return valid;
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
    s_next_sync = now_ms() + (valid ? SYNC_INTERVAL_MS : RECONNECT_INTERVAL_MS);
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
    if (s_config.ssid[0]) connect_config(&s_config);
    else esp_wifi_disconnect();
    setup_result(result, false);
}

static void confirm_candidate(void) {
    if (workout_store_save_config(&s_candidate) != ESP_OK) {
        cancel_candidate(WORKOUT_SETUP_STORAGE);
        return;
    }
    uint32_t source = workout_checksum(s_candidate.server, strlen(s_candidate.server));
    bool new_source = s_cache.magic && s_cache.source != source;
    s_config = s_candidate;
    memset(&s_candidate, 0, sizeof(s_candidate));
    s_pending_deadline = 0;
    if (new_source) {
        memset(&s_cache, 0, sizeof(s_cache));
        s_persisted = false;
        const workout_network_update_t update = {0};
        xQueueOverwrite(s_updates, &update);
    }
    portENTER_CRITICAL(&s_status_lock);
    s_status.has_config = true;
    portEXIT_CRITICAL(&s_status_lock);
    setup_result(WORKOUT_SETUP_SAVED, false);
    s_close_at = now_ms() + 10000;
    s_next_sync = 0;
}

static void clear_config(void) {
    if (workout_store_clear_config() != ESP_OK) { setup_result(WORKOUT_SETUP_STORAGE, false); return; }
    s_pending_deadline = 0;
    memset(&s_candidate, 0, sizeof(s_candidate));
    memset(&s_config, 0, sizeof(s_config));
    esp_wifi_disconnect();
    xEventGroupClearBits(s_events, CONNECTED_BIT);
    portENTER_CRITICAL(&s_status_lock);
    s_status.has_config = false;
    s_status.online = false;
    portEXIT_CRITICAL(&s_status_lock);
    setup_result(WORKOUT_SETUP_WAITING, false);
    s_close_at = 0;
    stop_setup();
    start_setup();
}

static void process_command(const network_command_t *command) {
    if (!s_ready) {
        s_ready = wifi_init() == ESP_OK;
        if (!s_ready) { network_error(WORKOUT_NET_INIT, 0); return; }
        if (s_config.ssid[0]) connect_config(&s_config);
    }
    if (command->kind == WORKOUT_ACTION_SETUP_START) start_setup();
    else if (command->kind == WORKOUT_ACTION_SETUP_STOP) {
        if (s_pending_deadline) cancel_candidate(WORKOUT_SETUP_WAITING);
        stop_setup();
    } else if (command->kind == WORKOUT_ACTION_SYNC) {
        if (s_config.ssid[0] && !s_pending_deadline) s_next_sync = 0;
    } else if (command->kind == WORKOUT_ACTION_CLEAR) clear_config();
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
        s_next_sync = 0;
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
    if (s_config.ssid[0] && !(bits & CONNECTED_BIT) && now >= s_next_connect) connect_config(&s_config);
    if (s_config.ssid[0] && now >= s_next_sync) sync_data();
}

static void network_task(void *arg) {
    (void)arg;
    s_ready = wifi_init() == ESP_OK;
    if (s_ready) {
        if (s_config.ssid[0]) connect_config(&s_config);
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
    if (workout_cache_valid(cache, sizeof(*cache))) { s_cache = *cache; s_persisted = true; }
    s_status.has_config = s_config.ssid[0] != '\0';
    s_events = xEventGroupCreate();
    s_commands = xQueueCreate(3, sizeof(network_command_t));
    s_updates = xQueueCreate(1, sizeof(workout_network_update_t));
    if (s_events && s_commands && s_updates
        && xTaskCreate(network_task, "workout_net", 7168, NULL, 4, NULL) == pdPASS) return ESP_OK;
    if (s_events) vEventGroupDelete(s_events);
    if (s_commands) vQueueDelete(s_commands);
    if (s_updates) vQueueDelete(s_updates);
    s_commands = s_updates = NULL;
    s_events = NULL;
    network_error(WORKOUT_NET_INIT, 0);
    return ESP_ERR_NO_MEM;
}
