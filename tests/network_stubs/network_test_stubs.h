#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
static inline const char *esp_err_to_name(esp_err_t err) { (void)err; return "injected"; }

#define ESP_ERR_INVALID_STATE 6
#define BIT0 1U
#define BIT1 2U
#define BIT2 4U
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(value) (value)
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
typedef int portMUX_TYPE;
typedef unsigned EventBits_t;
typedef EventBits_t *EventGroupHandle_t;
typedef struct test_queue *QueueHandle_t;
typedef void *TaskHandle_t;
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void xTaskNotifyGive(TaskHandle_t task);
unsigned ulTaskNotifyTake(int clear, unsigned timeout);
EventGroupHandle_t xEventGroupCreate(void);
void vEventGroupDelete(EventGroupHandle_t group);
EventBits_t xEventGroupGetBits(EventGroupHandle_t group);
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits);
QueueHandle_t xQueueCreate(unsigned count, size_t size);
int xQueueSend(QueueHandle_t queue, const void *value, unsigned timeout);
int xQueueReceive(QueueHandle_t queue, void *value, unsigned timeout);
int xQueueOverwrite(QueueHandle_t queue, const void *value);
void vQueueDelete(QueueHandle_t queue);
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle);

void test_log(const char *tag, const char *format, ...);
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
int64_t esp_timer_get_time(void);
#define MALLOC_CAP_8BIT 1
size_t heap_caps_get_free_size(unsigned capabilities);
size_t heap_caps_get_largest_free_block(unsigned capabilities);

typedef const char *esp_event_base_t;
typedef int esp_event_handler_instance_t;
extern const char test_wifi_event[], test_ip_event[];
#define WIFI_EVENT test_wifi_event
#define IP_EVENT test_ip_event
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_DISCONNECTED 1
#define IP_EVENT_STA_GOT_IP 2
#define IP_EVENT_STA_LOST_IP 3
typedef void (*test_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id,
    test_event_handler_t handler, void *arg, esp_event_handler_instance_t *instance);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id, esp_event_handler_instance_t instance);

typedef struct { int value; } esp_netif_t;
esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
esp_netif_t *esp_netif_create_default_wifi_ap(void);
void esp_netif_destroy_default_wifi(esp_netif_t *netif);
typedef struct { int value; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef struct {
    struct { uint8_t ssid[33], password[64]; struct { bool capable; } pmf_cfg; } sta;
    struct { uint8_t ssid[33], password[64], ssid_len, max_connection, channel; int authmode; } ap;
} wifi_config_t;
typedef struct { uint8_t ssid[33]; } wifi_ap_record_t;
#define WIFI_STORAGE_RAM 1
#define WIFI_MODE_STA 1
#define WIFI_MODE_APSTA 2
#define WIFI_PS_MIN_MODEM 1
#define WIFI_IF_STA 0
#define WIFI_IF_AP 1
#define WIFI_AUTH_WPA2_PSK 1
esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_storage(int storage);
esp_err_t esp_wifi_set_mode(int mode);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_set_ps(int mode);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_set_config(int interface, const wifi_config_t *config);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *access);
void esp_fill_random(void *output, size_t size);
#define ESP_MAC_WIFI_SOFTAP 1
esp_err_t esp_read_mac(uint8_t *output, int kind);

typedef void *esp_http_client_handle_t;
typedef struct { int event_id, data_len; void *user_data, *data; } esp_http_client_event_t;
#define HTTP_EVENT_ON_DATA 1
typedef struct {
    const char *url;
    int timeout_ms, buffer_size, buffer_size_tx;
    bool disable_auto_redirect;
    void *user_data;
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    esp_err_t (*crt_bundle_attach)(void *);
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char *url);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client, int timeout);
esp_err_t esp_http_client_set_user_data(esp_http_client_handle_t client, void *user);
esp_err_t esp_crt_bundle_attach(void *config);
typedef struct { const char *server; } esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(value) ((esp_sntp_config_t){.server = (value)})
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config);
esp_err_t esp_netif_sntp_sync_wait(unsigned timeout);
