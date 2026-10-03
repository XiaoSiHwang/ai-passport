#pragma once

#include "workout_model.h"
#include "workout_profiles.h"
#include "ai_quota.h"
#include "ai_tokens.h"
#include "codex_monitor.h"
#include "wellness.h"
#include "esp_err.h"

typedef enum {
    WORKOUT_NET_OK, WORKOUT_NET_OFFLINE, WORKOUT_NET_HTTP,
    WORKOUT_NET_RESPONSE, WORKOUT_NET_INIT, WORKOUT_NET_STORAGE,
} workout_network_error_t;

typedef enum {
    WORKOUT_SETUP_WAITING, WORKOUT_SETUP_CONNECTING, WORKOUT_SETUP_SAVED,
    WORKOUT_SETUP_BAD_WIFI, WORKOUT_SETUP_STORAGE, WORKOUT_SETUP_FAILED,
    WORKOUT_SETUP_EXPIRED,
} workout_setup_result_t;

typedef enum {
    WORKOUT_SWITCH_IDLE, WORKOUT_SWITCH_CONNECTING, WORKOUT_SWITCH_SAVED,
    WORKOUT_SWITCH_FAILED, WORKOUT_SWITCH_STORAGE,
} workout_switch_result_t;

typedef struct {
    uint32_t revision;
    bool online;
    bool syncing;
    bool has_config;
    bool setup_active;
    bool pending;
    bool cache_error;
    int http_status;
    workout_network_error_t error;
    workout_setup_result_t setup_result;
    workout_switch_result_t switch_result;
    bool profile_busy, connecting, exhausted;
    unsigned wifi_count, server_count, active_wifi, active_server, connecting_wifi, attempted;
    char wifi_names[WORKOUT_PROFILE_LIMIT][33];
    char servers[WORKOUT_PROFILE_LIMIT][WORKOUT_URL_SIZE];
    char ap_ssid[24];
    char ap_password[13];
    char token[33];
} workout_network_status_t;

typedef struct {
    bool available;
    bool persisted;
    workout_data_t data;
} workout_network_update_t;

/* App-lifetime services; no worker/callback holds pointers to LVGL objects. */
/* Register once before start. Called from worker/event tasks: only signal the app,
 * never access LVGL or block. The callback and user must outlive the service. */
void workout_network_set_notify(void (*notify)(void *), void *user);
esp_err_t workout_network_start(const workout_config_t *config, const workout_cache_t *cache);
bool workout_network_request(workout_action_t action);
bool workout_network_select(workout_action_t action, unsigned selection);
bool workout_network_submit(const workout_config_t *config, const char *token);
void workout_network_status(workout_network_status_t *status);
bool workout_network_take_update(workout_network_update_t *update);
bool workout_network_take_quota(ai_quota_update_t *update);
bool workout_network_take_wellness(wellness_state_t *update);
bool workout_network_take_tokens(ai_tokens_update_t *update);
bool workout_network_take_codex(codex_monitor_state_t *update);
bool workout_network_take_alert(codex_alert_t *alert);
