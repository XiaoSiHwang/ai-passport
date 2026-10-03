/* Fault injection against the actual worker, without radio/HTTP/NVS hardware. */
#include "network_test_stubs.h"
#include "../main/workout_network.c"
#include <assert.h>

const char test_wifi_event[] = "wifi", test_ip_event[] = "ip";
struct test_queue { size_t size; unsigned count, capacity; unsigned char data[4096]; };
static int64_t s_clock;
static wifi_config_t s_station;
static wifi_ap_record_t s_associated;
static workout_profiles_t s_disk;
static bool s_association, s_write_failure, s_queue_failure;
static unsigned s_connect_calls, s_write_calls;
static char s_http_url[CODEX_URL_SIZE];
static esp_http_client_config_t s_http_config;
static bool s_http_monitor;
static bool s_monitor_http;
static int s_monitor_code;
static unsigned s_legacy_delay;
static unsigned s_sntp_calls;
static esp_err_t s_sntp_result;
static codex_tasks_data_t s_wire_tasks;
static codex_alert_page_t s_wire_alerts;
static ai_tokens_data_t s_wire_tokens;
static ai_tokens_cache_t s_token_disk[AI_QUOTA_PROVIDERS];
static bool s_tokens_http;
static unsigned s_token_writes;
static unsigned s_http_inits, s_http_cleanups, s_worker_signals, s_app_signals;
static bool s_http_live, s_http_set_failure;

void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
int64_t esp_timer_get_time(void) { return s_clock * 1000; }
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 50000; }
size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 30000; }
EventGroupHandle_t xEventGroupCreate(void) { return calloc(1, sizeof(EventBits_t)); }
void vEventGroupDelete(EventGroupHandle_t group) { free(group); }
EventBits_t xEventGroupGetBits(EventGroupHandle_t group) { return *group; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits) { return *group |= bits; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits) { return *group &= ~bits; }
QueueHandle_t xQueueCreate(unsigned count, size_t size) {
    QueueHandle_t queue = calloc(1, sizeof(*queue));
    assert(queue && count * size <= sizeof(queue->data));
    queue->capacity = count; queue->size = size;
    return queue;
}
int xQueueSend(QueueHandle_t queue, const void *value, unsigned timeout) {
    (void)timeout;
    if (s_queue_failure || queue->count == queue->capacity) return pdFALSE;
    memcpy(queue->data + queue->count++ * queue->size, value, queue->size);
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t queue, void *value, unsigned timeout) {
    (void)timeout;
    if (!queue->count) return pdFALSE;
    memcpy(value, queue->data, queue->size);
    memmove(queue->data, queue->data + queue->size, --queue->count * queue->size);
    return pdTRUE;
}
int xQueueOverwrite(QueueHandle_t queue, const void *value) {
    assert(queue->capacity == 1);
    memcpy(queue->data, value, queue->size); queue->count = 1;
    return pdTRUE;
}
void vQueueDelete(QueueHandle_t queue) { free(queue); }
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle) {
    (void)task; (void)name; (void)stack; (void)arg; (void)priority;
    if (handle) *(TaskHandle_t *)handle = &s_disk;
    return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) { assert(task == &s_disk); s_worker_signals++; }
unsigned ulTaskNotifyTake(int clear, unsigned timeout) { assert(clear == pdTRUE); (void)timeout; return 0; }
esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id,
    test_event_handler_t handler, void *arg, esp_event_handler_instance_t *instance) {
    (void)base; (void)id; (void)handler; (void)arg; *instance = 1; return ESP_OK;
}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id, esp_event_handler_instance_t instance) {
    (void)base; (void)id; (void)instance; return ESP_OK;
}
esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_netif_t *esp_netif_create_default_wifi_sta(void) { static esp_netif_t netif; return &netif; }
esp_netif_t *esp_netif_create_default_wifi_ap(void) { static esp_netif_t netif; return &netif; }
void esp_netif_destroy_default_wifi(esp_netif_t *netif) { (void)netif; }
esp_err_t esp_wifi_init(const wifi_init_config_t *config) { (void)config; return ESP_OK; }
esp_err_t esp_wifi_set_storage(int storage) { (void)storage; return ESP_OK; }
esp_err_t esp_wifi_set_mode(int mode) { (void)mode; return ESP_OK; }
esp_err_t esp_wifi_start(void) { return ESP_OK; }
esp_err_t esp_wifi_stop(void) { return ESP_OK; }
esp_err_t esp_wifi_deinit(void) { return ESP_OK; }
esp_err_t esp_wifi_set_ps(int mode) { (void)mode; return ESP_OK; }
esp_err_t esp_wifi_disconnect(void) { s_association = false; return ESP_OK; }
esp_err_t esp_wifi_set_config(int interface, const wifi_config_t *config) {
    if (interface == WIFI_IF_STA) s_station = *config;
    return ESP_OK;
}
esp_err_t esp_wifi_connect(void) { s_connect_calls++; return ESP_OK; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *access) {
    if (!s_association) return ESP_FAIL;
    *access = s_associated; return ESP_OK;
}
void esp_fill_random(void *output, size_t size) { memset(output, 7, size); }
esp_err_t esp_read_mac(uint8_t *output, int kind) { (void)kind; memset(output, 8, 6); return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    assert(!s_http_live); s_http_live = true; s_http_inits++;
    strcpy(s_http_url, config->url); s_http_config = *config;
    s_http_monitor = strstr(s_http_url, "/api/codex/tasks") || strstr(s_http_url, "/api/codex/alerts");
    return &s_station;
}
esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char *url) {
    assert(client == &s_station && s_http_live);
    if (s_http_set_failure) return ESP_FAIL;
    strcpy(s_http_url, url);
    s_http_monitor = strstr(url, "/api/codex/tasks") || strstr(url, "/api/codex/alerts");
    return ESP_OK;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client, int timeout) {
    assert(client == &s_station && s_http_live); s_http_config.timeout_ms = timeout; return ESP_OK;
}
esp_err_t esp_http_client_set_user_data(esp_http_client_handle_t client, void *user) {
    assert(client == &s_station && s_http_live); s_http_config.user_data = user; return ESP_OK;
}
esp_err_t esp_http_client_perform(esp_http_client_handle_t client) {
    (void)client;
    if (s_tokens_http && strstr(s_http_url, "/tokens")) {
        esp_http_client_event_t event = {.event_id = HTTP_EVENT_ON_DATA, .user_data = s_http_config.user_data,
            .data = "tokens", .data_len = 6};
        assert(s_http_config.timeout_ms == 8000);
        return s_http_config.event_handler(&event);
    }
    if (!s_monitor_http || !s_http_monitor) { s_clock += s_legacy_delay; return ESP_FAIL; }
    esp_http_client_event_t event = {.event_id = HTTP_EVENT_ON_DATA, .user_data = s_http_config.user_data,
                                    .data = "monitor", .data_len = 7};
    assert(s_http_config.timeout_ms == 3000);
    return s_http_config.event_handler(&event);
}
int esp_http_client_get_status_code(esp_http_client_handle_t client) {
    (void)client;
    if (s_tokens_http && strstr(s_http_url, "/tokens")) return 200;
    return s_monitor_http && s_http_monitor ? s_monitor_code : 503;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    assert(client == &s_station && s_http_live);
    s_http_live = false; s_http_cleanups++; return ESP_OK;
}
esp_err_t esp_crt_bundle_attach(void *config) { (void)config; return ESP_OK; }
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config) {
    assert(strcmp(config->server, "pool.ntp.org") == 0);
    s_sntp_calls++;
    return s_sntp_result;
}
esp_err_t esp_netif_sntp_sync_wait(unsigned timeout) { (void)timeout; return ESP_OK; }
esp_err_t workout_portal_start(void) { return ESP_OK; }
void workout_portal_stop(void) {}
esp_err_t workout_store_load_profiles(workout_profiles_t *profiles) { *profiles = s_disk; return ESP_OK; }
esp_err_t workout_store_save_profiles(const workout_profiles_t *profiles) {
    assert(workout_profiles_valid(profiles));
    if (s_write_failure) return ESP_FAIL;
    s_disk = *profiles; s_write_calls++; return ESP_OK;
}
esp_err_t workout_store_clear_config(void) { memset(&s_disk, 0, sizeof(s_disk)); return ESP_OK; }
esp_err_t workout_store_load_quota(unsigned provider, ai_quota_cache_t *cache) {
    (void)provider; memset(cache, 0, sizeof(*cache)); return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t workout_store_save_quota(unsigned provider, const ai_quota_cache_t *cache) {
    (void)provider; (void)cache; return ESP_OK;
}
esp_err_t workout_store_load_tokens(unsigned provider, ai_tokens_cache_t *cache) {
    *cache = s_token_disk[provider];
    return cache->magic ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t workout_store_save_tokens(unsigned provider, const ai_tokens_cache_t *cache) {
    assert(ai_tokens_cache_valid(cache, sizeof(*cache), provider));
    if (s_write_failure) return ESP_FAIL;
    s_token_disk[provider] = *cache; s_token_writes++;
    return ESP_OK;
}
esp_err_t workout_store_save_cache(const workout_cache_t *cache) { (void)cache; return ESP_OK; }
bool workout_decode_response(const char *json, size_t size, workout_data_t *data) {
    (void)json; (void)size; (void)data; return false;
}
bool ai_quota_decode_response(const char *json, size_t size, unsigned provider, ai_quota_data_t *data) {
    (void)json; (void)size; (void)provider; (void)data; return false;
}
bool ai_tokens_decode_response(const char *json, size_t size, unsigned provider, ai_tokens_data_t *data) {
    (void)json; (void)size; (void)provider;
    if (!s_tokens_http || !ai_tokens_data_valid(&s_wire_tokens)) return false;
    *data = s_wire_tokens;
    return true;
}

bool codex_tasks_decode(const char *json, size_t size, codex_tasks_data_t *data) {
    (void)json; (void)size; *data = s_wire_tasks; return s_monitor_http;
}

bool codex_alerts_decode(const char *json, size_t size, codex_alert_page_t *page) {
    (void)json; (void)size;
    *page = s_wire_alerts;
    const char *after = strstr(s_http_url, "?after=");
    page->count = 0;
    page->next_cursor = after ? (uint32_t)strtoul(after + 7, NULL, 10) : page->latest_cursor;
    for (unsigned i = 0; after && i < s_wire_alerts.count; i++) {
        if (s_wire_alerts.alerts[i].cursor <= page->next_cursor) continue;
        page->alerts[page->count++] = s_wire_alerts.alerts[i];
        page->next_cursor = s_wire_alerts.alerts[i].cursor;
    }
    page->has_more = page->next_cursor < page->latest_cursor;
    return s_monitor_http;
}

static void fixture(void) {
    close_http_client();
    workout_network_set_notify(NULL, NULL);
    s_http_inits = s_http_cleanups = s_worker_signals = s_app_signals = 0;
    s_http_set_failure = false;
    if (s_commands) {
        vQueueDelete(s_commands); vQueueDelete(s_updates); vQueueDelete(s_quota_updates);
        vQueueDelete(s_tokens_updates);
        vQueueDelete(s_codex_updates); vQueueDelete(s_codex_alerts); vEventGroupDelete(s_events);
    }
    memset(&s_disk, 0, sizeof(s_disk));
    workout_config_t config = {.ssid = "Home", .server = "http://home.example.com/api/workout"};
    assert(workout_profiles_add(&s_disk, &config));
    strcpy(config.ssid, "Office"); strcpy(config.server, "http://office.example.com/api/workout");
    assert(workout_profiles_add(&s_disk, &config));
    s_disk.active_wifi = s_disk.active_server = 0; workout_profiles_seal(&s_disk);
    assert(workout_profiles_config(&s_disk, &config));
    memset(&s_status, 0, sizeof(s_status)); memset(&s_retry, 0, sizeof(s_retry));
    memset(&s_cache, 0, sizeof(s_cache)); memset(&s_quota, 0, sizeof(s_quota));
    memset(&s_tokens, 0, sizeof(s_tokens)); memset(s_tokens_cache, 0, sizeof(s_tokens_cache));
    memset(s_token_disk, 0, sizeof(s_token_disk)); memset(&s_wire_tokens, 0, sizeof(s_wire_tokens));
    s_tokens_http = false; s_token_writes = 0;
    s_profile_dirty = s_profile_error = s_manual_switch = false;
    s_pending_deadline = s_setup_deadline = s_close_at = s_next_sync = 0;
    s_write_failure = s_queue_failure = s_association = false;
    s_clock = s_connect_calls = s_write_calls = 0;
    s_monitor_http = false; s_monitor_code = 200; s_legacy_delay = 0;
    s_time_started = false; s_sntp_calls = 0; s_sntp_result = ESP_OK;
    memset(&s_wire_tasks, 0, sizeof(s_wire_tasks)); memset(&s_wire_alerts, 0, sizeof(s_wire_alerts));
    workout_cache_t cache = {0};
    assert(workout_network_start(&config, &cache) == ESP_OK);
    s_ready = true;
    retry_start(0);
}

static void got_ip(const char *ssid) {
    strcpy((char *)s_associated.ssid, ssid); s_association = true;
    wifi_event(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
}

static void command(void) {
    network_command_t value;
    assert(xQueueReceive(s_commands, &value, 0) == pdTRUE);
    process_command(&value);
}

static void automatic_fallback(void) {
    fixture(); network_tick();
    assert(strcmp((char *)s_station.sta.ssid, "Home") == 0 && s_connect_calls == 1);
    wifi_event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    s_clock = 19999; network_tick(); assert(s_connect_calls == 1);
    s_clock = 20000; network_tick();
    assert(strcmp((char *)s_station.sta.ssid, "Office") == 0 && s_connect_calls == 2);
    got_ip("Home"); assert(!(xEventGroupGetBits(s_events) & CONNECTED_BIT));
    got_ip("Office"); network_tick();
    assert(s_status.online && !s_status.connecting && s_disk.active_wifi == 1 && s_disk.active_server == 0);
    assert(strcmp(s_config.server, "http://home.example.com/api/workout") == 0);
    assert(s_status.error == WORKOUT_NET_HTTP && s_write_calls == 1);
    wifi_event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    assert(s_status.online); /* A delayed disconnect must not drop a new association. */
    s_clock = 60000; network_tick(); assert(s_connect_calls == 2 && s_write_calls == 1);
    wifi_event(NULL, IP_EVENT, IP_EVENT_STA_LOST_IP, NULL); network_tick();
    assert(s_connect_calls == 3 && strcmp((char *)s_station.sta.ssid, "Office") == 0);
}

static void manual_wifi(void) {
    fixture();
    assert(workout_network_select(WORKOUT_ACTION_SWITCH_WIFI, 1));
    assert(!workout_network_select(WORKOUT_ACTION_SWITCH_SERVER, 1));
    command(); got_ip("Office"); network_tick();
    assert(s_status.switch_result == WORKOUT_SWITCH_SAVED && !s_status.profile_busy);
    assert(s_disk.active_wifi == 1 && s_disk.active_server == 0);
    fixture(); assert(workout_network_select(WORKOUT_ACTION_SWITCH_WIFI, 1)); command();
    s_clock = 20000; network_tick();
    assert(s_status.switch_result == WORKOUT_SWITCH_FAILED && !s_status.profile_busy && s_disk.active_wifi == 0);
    assert(strcmp((char *)s_station.sta.ssid, "Home") == 0);
    got_ip("Home"); network_tick(); assert(s_status.online && s_write_calls == 0);
    fixture(); assert(workout_network_select(WORKOUT_ACTION_SWITCH_WIFI, 1)); command();
    s_write_failure = true; got_ip("Office"); network_tick();
    assert(s_status.switch_result == WORKOUT_SWITCH_STORAGE && !s_status.online && s_disk.active_wifi == 0);
    s_write_failure = false; network_tick();
    assert(strcmp((char *)s_station.sta.ssid, "Home") == 0);
}

static void server_switch(void) {
    fixture(); network_tick(); got_ip("Home"); network_tick();
    s_cache.magic = WORKOUT_CACHE_MAGIC; s_quota_cache[0].magic = AI_QUOTA_MAGIC;
    s_quota_cache[0].source = 123; s_quota.providers[0].available = true;
    s_tokens_cache[0].magic = AI_TOKENS_MAGIC;
    s_tokens_cache[0].source = 123; s_tokens.providers[0].available = true;
    unsigned connections = s_connect_calls;
    assert(workout_network_select(WORKOUT_ACTION_SWITCH_SERVER, 1)); command();
    assert(s_disk.active_wifi == 0 && s_disk.active_server == 1 && s_connect_calls == connections);
    assert(!s_cache.magic && !s_quota.providers[0].available);
    assert(!s_tokens_cache[0].magic && !s_tokens.providers[0].available);
    workout_network_update_t update;
    assert(workout_network_take_update(&update) && !update.available);
    network_tick(); assert(strcmp(s_http_url, "http://office.example.com/api/workout") == 0);
    assert(workout_network_select(WORKOUT_ACTION_SWITCH_SERVER, 0));
    s_write_failure = true; command();
    assert(s_status.switch_result == WORKOUT_SWITCH_STORAGE && s_disk.active_server == 1);
    assert(strcmp(s_config.server, "http://office.example.com/api/workout") == 0);
}

static void cooldown_and_setup(void) {
    fixture(); network_tick(); s_clock = 20000; network_tick(); s_clock = 40000; network_tick();
    assert(s_status.exhausted && !s_status.connecting && s_connect_calls == 2);
    wifi_event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    s_clock = 69999; network_tick(); assert(s_connect_calls == 2);
    s_clock = 70000; network_tick(); assert(s_connect_calls == 3 && !s_status.exhausted);
    start_setup(); assert(!workout_network_select(WORKOUT_ACTION_SWITCH_WIFI, 0));
    s_clock = 100000; network_tick(); assert(s_connect_calls == 3);
    stop_setup(); network_tick(); assert(s_connect_calls == 4);
    fixture(); s_queue_failure = true;
    assert(!workout_network_select(WORKOUT_ACTION_SWITCH_SERVER, 1) && !s_status.profile_busy);
    s_queue_failure = false; assert(!workout_network_select(WORKOUT_ACTION_SWITCH_SERVER, 99));
    s_status.error = WORKOUT_NET_STORAGE;
    clear_config(); assert(!s_status.has_config && s_status.setup_active && !s_disk.wifi_count);
    assert(s_status.error == WORKOUT_NET_OFFLINE && !s_expected_ssid[0]);
}

static void automatic_storage_retry(void) {
    fixture(); network_tick(); s_clock = 20000; network_tick();
    s_write_failure = true; got_ip("Office"); network_tick();
    assert(s_status.online && s_profile_dirty && s_config.ssid[0] && s_disk.active_wifi == 0);
    s_write_failure = false; s_clock = 50000; network_tick();
    assert(!s_profile_dirty && s_disk.active_wifi == 1 && s_write_calls == 1);
}

static void monitor_fixture(void) {
    fixture(); got_ip("Home"); network_tick();
    s_monitor_http = true;
    strcpy(s_wire_tasks.stream_id, "stream_a"); strcpy(s_wire_alerts.stream_id, "stream_a");
    s_wire_tasks.revision = 2; s_wire_tasks.source_online = true; s_wire_tasks.count = 1;
    strcpy(s_wire_tasks.tasks[0].task_id, "task_a"); s_wire_tasks.tasks[0].fresh = true;
    sync_codex_tasks();
    s_wire_alerts.latest_cursor = 1;
    sync_codex_alerts();
    assert(s_codex.available && s_codex_cursor.baseline && s_codex_cursor.cursor == 1);
    assert(!s_codex_alerts->count); /* Existing approvals are a badge, not startup notifications. */
}

static void monitor_page(unsigned first, unsigned count, codex_approval_state_t state, bool fresh) {
    s_wire_alerts.count = count; s_wire_alerts.latest_cursor = first + count - 1;
    for (unsigned i = 0; i < count; i++) {
        codex_alert_t *alert = &s_wire_alerts.alerts[i];
        memset(alert, 0, sizeof(*alert)); alert->cursor = first + i; alert->state = state; alert->fresh = fresh;
        strcpy(alert->stream_id, s_wire_alerts.stream_id); strcpy(alert->task_id, "task_a");
        snprintf(alert->alert_id, sizeof(alert->alert_id), "alert_%u", first + i);
    }
}

static void monitoring_replay(void) {
    monitor_fixture(); monitor_page(2, 1, CODEX_APPROVAL_REQUESTED, true);
    sync_codex_alerts(); assert(s_codex_cursor.cursor == 2 && s_codex_alerts->count == 1);
    codex_alert_t alert; assert(workout_network_take_alert(&alert) && alert.cursor == 2);
    sync_codex_alerts(); assert(!s_codex_alerts->count && s_codex_cursor.cursor == 2);
    wifi_event(NULL, IP_EVENT, IP_EVENT_STA_LOST_IP, NULL);
    assert(s_codex_cursor.baseline && s_codex_cursor.cursor == 2);
    got_ip("Home"); network_tick(); assert(s_codex_cursor.cursor == 2);
    monitor_page(3, 1, CODEX_APPROVAL_REQUESTED, false); sync_codex_alerts();
    monitor_page(4, 1, CODEX_APPROVAL_UNKNOWN, true); sync_codex_alerts();
    monitor_page(5, 1, CODEX_APPROVAL_RESOLVED, true); sync_codex_alerts();
    monitor_page(6, 1, CODEX_APPROVAL_SUPERSEDED, true); sync_codex_alerts();
    assert(s_codex_cursor.cursor == 6 && !s_codex_alerts->count);
    s_monitor_code = 503; sync_codex_tasks();
    assert(s_codex.failed && s_codex.available && s_codex.data.revision == 2);
    s_monitor_code = 200; s_wire_tasks.revision = 1; sync_codex_tasks();
    assert(s_codex.failed && s_codex.data.revision == 2);
    s_wire_tasks.revision = 3; sync_codex_tasks(); assert(!s_codex.failed);
}

static void monitoring_backpressure(void) {
    monitor_fixture(); monitor_page(2, 5, CODEX_APPROVAL_REQUESTED, true); sync_codex_alerts();
    assert(s_codex_alerts->count == 5 && s_codex_cursor.cursor == 6);
    monitor_page(7, 1, CODEX_APPROVAL_REQUESTED, true); sync_codex_alerts();
    assert(s_codex_cursor.cursor == 6 && s_codex.alerts_failed);
    codex_alert_t alert;
    while (workout_network_take_alert(&alert)) { }
    sync_codex_alerts(); assert(s_codex_cursor.cursor == 7 && s_codex_alerts->count == 1);
    assert(workout_network_take_alert(&alert));
    monitor_page(8, 1, CODEX_APPROVAL_REQUESTED, true);
    strcpy(s_wire_alerts.alerts[0].alert_id, "alert_7"); sync_codex_alerts();
    assert(s_codex_cursor.cursor == 8 && !s_codex_alerts->count);
    s_next_sync = 123; s_next_quota[0] = 456;
    assert(workout_network_request(WORKOUT_ACTION_CODEX_SYNC)); command();
    assert(!s_next_codex_tasks && !s_next_codex_alerts && s_next_sync == 123 && s_next_quota[0] == 456);
}

static void monitoring_source_and_expiry(void) {
    monitor_fixture(); monitor_page(2, 1, CODEX_APPROVAL_REQUESTED, true); sync_codex_alerts();
    strcpy(s_wire_tasks.stream_id, "stream_b"); sync_codex_tasks();
    assert(!s_codex_cursor.baseline && !s_codex_alerts->count && strcmp(s_codex.data.stream_id, "stream_b") == 0);
    strcpy(s_wire_alerts.stream_id, "stream_b"); sync_codex_alerts();
    assert(s_codex_cursor.baseline && !s_codex_alerts->count);
    s_monitor_code = 410; sync_codex_alerts();
    assert(!s_codex_cursor.baseline && !s_codex.available && !s_codex_alerts->count);
    s_monitor_code = 200; sync_codex_alerts(); sync_codex_alerts(); sync_codex_tasks();
    assert(s_codex_cursor.baseline && s_codex.available);
    assert(workout_network_select(WORKOUT_ACTION_SWITCH_SERVER, 1)); command();
    assert(!s_codex.available && !s_codex_cursor.baseline && !s_codex_alerts->count);
    s_monitor_http = false;
    for (unsigned i = 0; i < 5; i++) { s_clock++; network_tick(); }
    assert(s_next_sync > s_clock && s_next_quota[0] > s_clock && s_next_quota[1] > s_clock);
    assert(s_next_codex_tasks > s_clock && s_next_codex_alerts > s_clock);
}

static void monitoring_slow_legacy(void) {
    monitor_fixture();
    s_legacy_delay = 8000;
    s_next_sync = s_next_quota[0] = s_next_quota[1] = 0;
    s_next_codex_tasks = s_next_codex_alerts = 0;
    s_monitor_budget = s_sync_turn = 0;
    int64_t received = s_codex.received_ms;
    unsigned updates = 0;
    for (unsigned i = 0; i < 10; i++) {
        s_clock += 250; network_tick();
        if (s_codex.received_ms == received) continue;
        assert(s_codex.received_ms - received <= 9000);
        received = s_codex.received_ms; updates++;
    }
    assert(updates >= 3 && s_next_quota[0] && s_next_quota[1]);
}

static void home_clock_sync(void) {
    fixture();
    got_ip("Home"); network_tick();
    assert(s_time_started && s_sntp_calls == 1); /* HTTP-configured devices start background SNTP. */
    network_tick(); assert(https_clock() && s_sntp_calls == 1);
    fixture(); s_sntp_result = ESP_FAIL;
    assert(https_clock() && !s_time_started && s_sntp_calls == 1); /* Failed SNTP must not block HTTP data. */
    s_sntp_result = ESP_OK;
    assert(https_clock() && s_time_started && s_sntp_calls == 2);
}

static void token_sync_and_cache(void) {
    fixture(); got_ip("Home"); network_tick();
    s_tokens_http = true;
    strcpy(s_wire_tokens.start_date, "2026-09-04"); strcpy(s_wire_tokens.end_date, "2026-10-03");
    strcpy(s_wire_tokens.fetched_at, "2026-10-03T12:00:00+08:00");
    int64_t day;
    assert(workout_parse_date(s_wire_tokens.start_date, &day)); s_wire_tokens.start_day = (int32_t)day;
    assert(workout_parse_timestamp(s_wire_tokens.fetched_at, &s_wire_tokens.fetched_seconds));
    s_wire_tokens.known = UINT32_C(1) << 29; s_wire_tokens.available_days = 1;
    s_wire_tokens.total = s_wire_tokens.daily[29] = 5000000000;
    s_quota.providers[0].available = true;
    sync_tokens(0);
    assert(strcmp(s_http_url, "http://home.example.com/api/codex/tokens") == 0);
    assert(s_token_writes == 1 && s_tokens.providers[0].persisted && s_tokens.providers[0].available);
    sync_tokens(0); assert(s_token_writes == 1); /* Unchanged snapshots do not write NVS. */
    ai_tokens_update_t update;
    assert(workout_network_take_tokens(&update) && update.providers[0].data.total == 5000000000);
    s_tokens_http = false; sync_tokens(0);
    assert(s_tokens.providers[0].failed && s_tokens.providers[0].data.total == 5000000000);
    assert(s_quota.providers[0].available && s_next_tokens[0] == s_clock + RECONNECT_INTERVAL_MS);
    s_tokens_http = true; s_write_failure = true; sync_tokens(1);
    assert(s_tokens.providers[1].available && !s_tokens.providers[1].persisted);
    s_write_failure = false; sync_tokens(1);
    assert(s_tokens.providers[1].persisted && s_token_writes == 2);
    s_wire_tokens.fetched_seconds--; sync_tokens(0);
    assert(s_tokens.providers[0].failed && s_token_writes == 2);
    s_wire_tokens.fetched_seconds++;
    memset(&s_tokens, 0, sizeof(s_tokens)); tokens_restore();
    assert(s_tokens.providers[0].from_cache && s_tokens.providers[1].from_cache);
    assert(workout_network_request(WORKOUT_ACTION_SYNC)); command();
    assert(!s_next_tokens[0] && !s_next_tokens[1]);
}

static void app_signal(void *user) { assert(user == &s_app_signals); s_app_signals++; }

static void idle_deadlines_and_connections(void) {
    monitor_fixture();
    workout_network_set_notify(app_signal, &s_app_signals);
    s_next_sync = s_clock + 300000;
    for (unsigned i = 0; i < AI_QUOTA_PROVIDERS; i++) s_next_quota[i] = s_next_tokens[i] = s_clock + 60000;
    s_next_codex_tasks = s_clock + 5000; s_next_codex_alerts = s_clock + 7000;
    assert(network_wait_ms() == 5000); /* No fixed 250ms wake while nothing is due. */
    unsigned inits = s_http_inits;
    sync_codex_tasks(); sync_codex_alerts();
    assert(s_http_inits == inits && s_http_live && !s_http_config.user_data && s_app_signals == 2);
    unsigned signals = s_worker_signals;
    assert(workout_network_request(WORKOUT_ACTION_CODEX_SYNC));
    assert(s_worker_signals == signals + 1); command();
    assert(network_wait_ms() == 0);
    s_http_set_failure = true; sync_codex_tasks();
    assert(!s_http_live && !s_http_client && s_codex.failed);
    s_http_set_failure = false; sync_codex_tasks(); assert(s_http_live && !s_codex.failed);
    s_monitor_code = 503; sync_codex_tasks(); assert(!s_http_live && !s_http_client);
    s_monitor_code = 200; sync_codex_tasks();
    signals = s_worker_signals;
    wifi_event(NULL, IP_EVENT, IP_EVENT_STA_LOST_IP, NULL);
    assert(s_worker_signals == signals + 1 && !s_commands->count); /* Events use no command slots. */
    network_tick(); assert(!s_http_live && !s_http_client);
    got_ip("Home"); network_tick(); sync_codex_tasks();
    assert(s_http_live);
    start_setup(); assert(!s_http_live);
    stop_setup(); sync_codex_tasks(); assert(s_http_live);
    assert(workout_network_select(WORKOUT_ACTION_SWITCH_SERVER, 1)); command();
    assert(!s_http_live && !s_codex.available);
    s_ready = false; assert(network_wait_ms() == 60000);
}

int main(void) {
    automatic_fallback(); manual_wifi(); server_switch(); cooldown_and_setup(); automatic_storage_retry();
    monitoring_replay(); monitoring_backpressure(); monitoring_source_and_expiry();
    monitoring_slow_legacy();
    home_clock_sync();
    token_sync_and_cache();
    idle_deadlines_and_connections();
    close_http_client();
    vQueueDelete(s_commands); vQueueDelete(s_updates); vQueueDelete(s_quota_updates); vEventGroupDelete(s_events);
    vQueueDelete(s_codex_updates); vQueueDelete(s_codex_alerts);
    vQueueDelete(s_tokens_updates);
    puts("Workout worker network fault injection: PASS");
    return 0;
}
