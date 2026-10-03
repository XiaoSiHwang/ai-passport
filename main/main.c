#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "workout_store.h"
#include "workout_ui.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>

static const char *TAG = "workout";
static QueueHandle_t s_input;
static workout_ui_state_t s_state;
static bool s_network_started;
static bool s_ui_dirty;
static int64_t s_last_activity;
static unsigned s_brightness = 100;

static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *user) {
    (void)user;
    workout_input_t input;
    if (event == BSP_BTN_LONG && button == BSP_BTN_OK) input = WORKOUT_INPUT_MENU;
    else if (event == BSP_BTN_LONG && button == BSP_BTN_DOWN) input = WORKOUT_INPUT_CLEAR;
    else if (event == BSP_BTN_CLICK) input = button == BSP_BTN_OK ? WORKOUT_INPUT_OK
                                             : button == BSP_BTN_UP ? WORKOUT_INPUT_UP : WORKOUT_INPUT_DOWN;
    else return;
    if (s_input) (void)xQueueSend(s_input, &input, 0);
}

static void refresh(void) {
    s_ui_dirty = true;
    if (!bsp_lvgl_lock(500)) return;
    workout_ui_update(&s_state);
    s_ui_dirty = false;
    bsp_lvgl_unlock();
}

static void navigate_input(workout_input_t input) {
    workout_action_t action;
    if (s_state.network.profile_busy && input == WORKOUT_INPUT_OK
        && s_state.navigation.view != WORKOUT_VIEW_DASHBOARD && s_state.navigation.view != WORKOUT_VIEW_AI
        && s_state.navigation.view != WORKOUT_VIEW_CODEX && s_state.navigation.view != WORKOUT_VIEW_CODEX_DETAILS
        && s_state.navigation.view != WORKOUT_VIEW_HOME && s_state.navigation.view != WORKOUT_VIEW_CALENDAR
        && s_state.navigation.view != WORKOUT_VIEW_ALMANAC
        && s_state.navigation.view != WORKOUT_VIEW_DETAILS && s_state.navigation.view != WORKOUT_VIEW_MENU) {
        s_state.request_failed = true;
        refresh();
        return;
    }
    workout_navigation_t previous = s_state.navigation;
    s_state.request_failed = false;
    if ((s_state.navigation.view == WORKOUT_VIEW_CODEX || s_state.navigation.view == WORKOUT_VIEW_CODEX_DETAILS)
        && input == WORKOUT_INPUT_OK && (s_state.codex.failed || s_state.codex.alerts_failed)) {
        action = WORKOUT_ACTION_CODEX_SYNC;
    } else if (s_state.navigation.view == WORKOUT_VIEW_SETUP && input == WORKOUT_INPUT_OK
        && s_state.network.setup_active) {
        s_state.navigation.setup_step ^= 1;
        action = WORKOUT_ACTION_NONE;
    } else action = workout_navigate(&s_state.navigation, input);
    if (action != WORKOUT_ACTION_NONE) {
        bool select = action == WORKOUT_ACTION_SWITCH_WIFI || action == WORKOUT_ACTION_SWITCH_SERVER;
        bool accepted = select ? workout_network_select(action, previous.selection) : workout_network_request(action);
        if (!accepted) {
            s_state.request_failed = true;
            s_state.navigation = previous;
        } else if (action == WORKOUT_ACTION_SWITCH_WIFI) s_state.navigation.selection = 0;
        workout_network_status(&s_state.network);
    }
    refresh();
}

static void handle_input(workout_input_t input) {
    bool waking = s_brightness == 0;
    s_last_activity = esp_timer_get_time();
    s_brightness = 100;
    bsp_display_backlight(100);
    if (waking) return; /* The first key wakes the screen without changing pages. */
    if (s_state.codex_popup) {
        if (input == WORKOUT_INPUT_OK || input == WORKOUT_INPUT_MENU) {
            s_state.codex_popup = false;
            refresh(); /* Local dismissal never sends an approval decision. */
        }
        return;
    }
    navigate_input(input);
}

static void idle_backlight(void) {
    int64_t idle = esp_timer_get_time() - s_last_activity;
    unsigned brightness = idle >= 60000000 ? 0 : idle >= 30000000 ? 20 : 100;
    if (brightness == s_brightness) return;
    s_brightness = brightness;
    bsp_display_backlight((uint8_t)brightness);
}

static bool update_codex(void) {
    char selected[CODEX_ID_SIZE] = {0};
    unsigned index = s_state.navigation.codex_selection;
    if (index < s_state.codex.data.count) strcpy(selected, s_state.codex.data.tasks[index].task_id);
    bool changed = workout_network_take_codex(&s_state.codex);
    if (changed) {
        s_state.navigation.codex_count = s_state.codex.data.count;
        s_state.navigation.codex_selection = codex_monitor_selection(&s_state.codex.data, selected);
    }
    s_state.now_ms = esp_timer_get_time() / 1000;
    if (s_state.codex_popup && !codex_alert_current(&s_state.codex, &s_state.codex_alert,
        s_state.network.online, s_state.now_ms)) {
        s_state.codex_popup = false;
        changed = true;
    }
    codex_alert_t alert;
    while (!s_state.codex_popup && workout_network_take_alert(&alert)) {
        s_state.now_ms = esp_timer_get_time() / 1000;
        if (s_state.now_ms - alert.received_ms > CODEX_FRESH_MS
            || !codex_alert_current(&s_state.codex, &alert, s_state.network.online, s_state.now_ms)) continue;
        s_state.codex_alert = alert;
        s_state.codex_popup = true;
        s_last_activity = esp_timer_get_time();
        s_brightness = 100;
        bsp_display_backlight(100);
        changed = true;
    }
    return changed;
}

static void update_network(void) {
    if (!s_network_started) return;
    workout_network_status_t status;
    workout_network_status(&status);
    bool changed = status.revision != s_state.network.revision;
    if (!status.online && status.has_config
        && (s_state.network.online || (status.exhausted && !s_state.network.exhausted))
        && s_state.navigation.view == WORKOUT_VIEW_DASHBOARD) {
        s_state.navigation.view = WORKOUT_VIEW_CONNECTION;
        s_state.navigation.selection = 0;
        changed = true;
    }
    s_state.network = status;
    s_state.navigation.wifi_count = status.wifi_count;
    s_state.navigation.server_count = status.server_count;
    workout_network_update_t update;
    if (workout_network_take_update(&update)) {
        s_state.has_data = update.available;
        if (update.available) s_state.data = update.data;
        s_state.from_cache = false;
        changed = true;
    }
    if (workout_network_take_quota(&s_state.quota)) changed = true;
    if (update_codex()) changed = true;
    if (changed) refresh();
}

static void start_network(void) {
    if (workout_store_init() != ESP_OK) {
        s_state.network.error = WORKOUT_NET_STORAGE;
        ESP_LOGE(TAG, "NVS unavailable; retained data was not erased");
        return;
    }
    workout_config_t config = {0};
    workout_cache_t cache = {0};
    (void)workout_store_load_config(&config);
    if (workout_store_load_cache(&cache) == ESP_OK) {
        uint32_t source = workout_checksum(config.server, strlen(config.server));
        if (!config.server[0] || cache.source == source) {
            s_state.data = cache.data;
            s_state.has_data = true;
            s_state.from_cache = true;
        }
    }
    s_network_started = workout_network_start(&config, &cache) == ESP_OK;
    if (!s_network_started) s_state.network.error = WORKOUT_NET_INIT;
    memset(&config, 0, sizeof(config));
}

static void update_clock(void) {
    passport_clock_t clock;
    (void)passport_calendar_clock((int64_t)time(NULL), &clock);
    if (memcmp(&clock, &s_state.clock, sizeof(clock)) == 0) return;
    s_state.clock = clock;
    s_state.navigation.clock_valid = clock.valid;
    s_state.navigation.today = clock.day;
    refresh();
}

static void app_loop(void) {
    int64_t next_battery = 0, next_codex_frame = 0;
    for (;;) {
        workout_input_t input;
        update_clock();
        if (xQueueReceive(s_input, &input, pdMS_TO_TICKS(100)) == pdTRUE) handle_input(input);
        s_state.now_ms = esp_timer_get_time() / 1000;
        update_network();
        int64_t now = esp_timer_get_time();
        if (now >= next_battery) {
            int battery = bsp_battery_soc();
            if (battery != s_state.battery) { s_state.battery = battery; refresh(); }
            next_battery = now + 30000000;
        }
        idle_backlight();
        if (s_state.now_ms >= next_codex_frame) {
            if (s_brightness && (s_state.navigation.view == WORKOUT_VIEW_CODEX
                || s_state.navigation.view == WORKOUT_VIEW_CODEX_DETAILS)) refresh();
            next_codex_frame = (s_state.now_ms / 1000 + 1) * 1000;
        }
        if (s_ui_dirty) refresh();
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Workout dashboard starting");
    const esp_pm_config_t power = {.max_freq_mhz = 160, .min_freq_mhz = 80, .light_sleep_enable = true};
    (void)esp_pm_configure(&power);
    (void)bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }
    (void)bsp_battery_init();
    s_state.battery = bsp_battery_soc();
    s_input = xQueueCreate(8, sizeof(workout_input_t));
    if (!s_input || !bsp_lvgl_lock(1000)) { ESP_LOGE(TAG, "UI initialization failed"); return; }
    bool ready = workout_ui_create();
    bsp_lvgl_unlock();
    if (!ready) { ESP_LOGE(TAG, "UI/font initialization failed"); return; }
    start_network();
    refresh();
    if (bsp_button_init(on_key, NULL) != ESP_OK) ESP_LOGE(TAG, "Buttons unavailable");
    s_last_activity = esp_timer_get_time();
    bsp_display_backlight(100);
    app_loop();
}
