#pragma once

#include "workout_network.h"
#include "codex_monitor.h"

typedef struct {
    workout_navigation_t navigation;
    workout_data_t data;
    workout_network_status_t network;
    ai_quota_update_t quota;
    codex_monitor_state_t codex;
    codex_alert_t codex_alert;
    bool codex_popup;
    int64_t now_ms;
    bool has_data;
    bool from_cache;
    bool request_failed;
    int battery;
    passport_clock_t clock;
} workout_ui_state_t;

/* All calls require the LVGL lock; one screen, owned for the app lifetime. */
bool workout_ui_create(void);
void workout_ui_update(const workout_ui_state_t *state);
