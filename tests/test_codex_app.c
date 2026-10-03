/* Exercise the actual application input/wake boundary, without a screen or radio. */
#define time passport_test_time
#include "../main/main.c"
#undef time
#include <assert.h>
#include <stdio.h>

static int64_t s_test_clock;
static unsigned s_backlight_calls, s_network_calls, s_ui_calls;
static workout_action_t s_requested;
static bool s_update_ready, s_locked;
static codex_monitor_state_t s_next_update;
static codex_alert_t s_test_alert;
static bool s_alert_ready;
static workout_input_t s_queued_input;
static time_t s_wall_clock;

time_t passport_test_time(time_t *value) {
    if (value) *value = s_wall_clock;
    return s_wall_clock;
}

void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
int64_t esp_timer_get_time(void) { return s_test_clock * 1000; }
bool bsp_lvgl_lock(int timeout) { (void)timeout; s_locked = true; return true; }
void bsp_lvgl_unlock(void) { s_locked = false; }
void bsp_display_backlight(uint8_t percent) { assert(percent <= 100); s_backlight_calls++; }
void workout_ui_update(const workout_ui_state_t *state) { assert(s_locked && state); s_ui_calls++; }
bool workout_network_request(workout_action_t action) { s_network_calls++; s_requested = action; return true; }
bool workout_network_select(workout_action_t action, unsigned selection) { (void)selection; return workout_network_request(action); }
void workout_network_status(workout_network_status_t *status) { *status = s_state.network; }
bool workout_network_take_codex(codex_monitor_state_t *update) {
    if (!s_update_ready) return false;
    *update = s_next_update; s_update_ready = false; return true;
}
bool workout_network_take_alert(codex_alert_t *alert) {
    if (!s_alert_ready) return false;
    *alert = s_test_alert; s_alert_ready = false; return true;
}
int xQueueSend(QueueHandle_t queue, const void *value, unsigned timeout) {
    (void)queue; assert(timeout == 0); s_queued_input = *(const workout_input_t *)value; return pdTRUE;
}

static void fixture(void) {
    memset(&s_state, 0, sizeof(s_state)); memset(&s_test_alert, 0, sizeof(s_test_alert));
    s_state.navigation.view = WORKOUT_VIEW_DASHBOARD;
    s_backlight_calls = s_network_calls = s_ui_calls = 0;
    s_update_ready = s_alert_ready = false; s_test_clock = s_state.now_ms = 1000;
    s_state.network.online = true; s_state.codex.available = true;
    s_state.codex.received_ms = 1000; s_state.codex.data.source_online = true;
    strcpy(s_state.codex.data.stream_id, "stream_a"); s_state.codex.data.count = 1;
    codex_task_t *task = &s_state.codex.data.tasks[0];
    strcpy(task->task_id, "task_a"); strcpy(task->approval_id, "approval_a");
    task->status = CODEX_TASK_APPROVAL; task->approval = CODEX_APPROVAL_REQUESTED; task->fresh = true;
    s_state.navigation.codex_count = 1; s_brightness = 0;
    s_input = (QueueHandle_t)&s_queued_input;
    strcpy(s_test_alert.stream_id, "stream_a"); strcpy(s_test_alert.task_id, "task_a");
    strcpy(s_test_alert.approval_id, "approval_a"); strcpy(s_test_alert.alert_id, "alert_a");
    s_test_alert.state = CODEX_APPROVAL_REQUESTED; s_test_alert.fresh = true;
    s_test_alert.received_ms = 1000;
}

static void popup_and_keys(void) {
    fixture(); s_alert_ready = true;
    assert(update_codex() && s_state.codex_popup && s_brightness == 100 && s_backlight_calls == 1);
    assert(s_state.navigation.view == WORKOUT_VIEW_DASHBOARD);
    handle_input(WORKOUT_INPUT_DOWN); assert(s_state.codex_popup && !s_state.navigation.monthly);
    handle_input(WORKOUT_INPUT_OK);
    assert(!s_state.codex_popup && s_state.codex.data.tasks[0].approval == CODEX_APPROVAL_REQUESTED);
    assert(!s_network_calls && s_state.navigation.view == WORKOUT_VIEW_DASHBOARD);
    s_state.navigation.view = WORKOUT_VIEW_SETUP;
    s_alert_ready = true; assert(update_codex() && s_state.codex_popup);
    handle_input(WORKOUT_INPUT_MENU);
    assert(!s_state.codex_popup && s_state.navigation.view == WORKOUT_VIEW_SETUP && !s_network_calls);
    s_brightness = 0; s_state.navigation.view = WORKOUT_VIEW_CODEX;
    handle_input(WORKOUT_INPUT_OK); assert(s_state.navigation.view == WORKOUT_VIEW_CODEX && s_brightness == 100);
    handle_input(WORKOUT_INPUT_OK); assert(s_state.navigation.view == WORKOUT_VIEW_CODEX_DETAILS);
    s_state.codex.failed = true; handle_input(WORKOUT_INPUT_OK);
    assert(s_network_calls == 1 && s_requested == WORKOUT_ACTION_CODEX_SYNC);
}

static void stale_and_source(void) {
    fixture(); s_test_alert.fresh = false; s_alert_ready = true;
    assert(!update_codex() && !s_backlight_calls && s_brightness == 0);
    s_test_alert.fresh = true; s_test_alert.state = CODEX_APPROVAL_UNKNOWN; s_alert_ready = true;
    assert(!update_codex() && !s_backlight_calls);
    s_test_alert.state = CODEX_APPROVAL_REQUESTED; strcpy(s_test_alert.stream_id, "stream_old"); s_alert_ready = true;
    assert(!update_codex() && !s_backlight_calls);
    strcpy(s_test_alert.stream_id, "stream_a"); s_alert_ready = true; s_state.network.online = false;
    assert(!update_codex() && !s_backlight_calls);
    s_state.network.online = true; s_alert_ready = true; s_test_clock = s_state.now_ms = 16001;
    assert(!update_codex() && !s_backlight_calls);
}

static void snapshot_and_callback(void) {
    fixture(); s_alert_ready = true; assert(update_codex());
    s_next_update = s_state.codex; s_next_update.received_ms = 1001;
    s_next_update.data.tasks[0].approval = CODEX_APPROVAL_NONE;
    s_next_update.data.tasks[0].approval_id[0] = 0; s_next_update.data.tasks[0].status = CODEX_TASK_RUNNING;
    s_update_ready = true; assert(update_codex() && !s_state.codex_popup);
    s_state.navigation.codex_selection = 0;
    s_next_update.data.count = 2; s_next_update.data.tasks[1] = s_next_update.data.tasks[0];
    strcpy(s_next_update.data.tasks[0].task_id, "task_new"); s_update_ready = true;
    assert(update_codex() && s_state.navigation.codex_selection == 1);
    unsigned before = s_ui_calls;
    on_key(BSP_BTN_OK, BSP_BTN_CLICK, NULL);
    assert(s_queued_input == WORKOUT_INPUT_OK && s_ui_calls == before && !s_network_calls);
    on_key(BSP_BTN_OK, BSP_BTN_LONG, NULL); assert(s_queued_input == WORKOUT_INPUT_MENU);
}

static void independent_alert_freshness(void) {
    fixture(); s_state.codex.failed = true; s_state.codex.checked_ms = 900;
    s_alert_ready = true; assert(update_codex() && s_state.codex_popup);
    assert(!update_codex() && s_state.codex_popup); /* Fresh alerts can recover from an older task-fetch error. */
    s_state.codex.checked_ms = 1001;
    assert(update_codex() && !s_state.codex_popup);
}

static void home_clock_and_busy_navigation(void) {
    fixture(); s_wall_clock = 0; s_brightness = 100;
    s_state.navigation.view = WORKOUT_VIEW_HOME;
    s_state.network.profile_busy = true;
    update_clock(); handle_input(WORKOUT_INPUT_OK);
    assert(!s_state.clock.valid && s_state.navigation.view == WORKOUT_VIEW_HOME);
    int64_t stamp;
    assert(workout_parse_timestamp("2026-10-03T09:41:00+08:00", &stamp));
    s_wall_clock = (time_t)stamp; update_clock();
    assert(s_state.clock.valid && s_state.clock.hour == 9 && s_state.clock.minute == 41);
    unsigned before = s_ui_calls;
    s_wall_clock += 10; update_clock();
    assert(s_ui_calls == before); /* Avoid redrawing a minute-only clock ten times per second. */
    s_wall_clock += 60; update_clock();
    assert(s_ui_calls == before + 1 && s_state.clock.minute == 42);
    handle_input(WORKOUT_INPUT_OK);
    assert(s_state.navigation.view == WORKOUT_VIEW_CALENDAR && !s_network_calls);
    int32_t selected = s_state.navigation.calendar_day;
    handle_input(WORKOUT_INPUT_OK);
    assert(s_state.navigation.view == WORKOUT_VIEW_ALMANAC && !s_network_calls);
    assert(workout_parse_timestamp("2026-10-04T00:00:00+08:00", &stamp));
    s_wall_clock = (time_t)stamp; update_clock();
    assert(s_state.navigation.today == selected + 1 && s_state.navigation.calendar_day == selected);
    handle_input(WORKOUT_INPUT_MENU); handle_input(WORKOUT_INPUT_MENU); handle_input(WORKOUT_INPUT_OK);
    assert(s_state.navigation.view == WORKOUT_VIEW_CALENDAR && s_state.navigation.calendar_day == selected + 1);
}

int main(void) {
    popup_and_keys(); stale_and_source(); snapshot_and_callback(); independent_alert_freshness();
    home_clock_and_busy_navigation();
    puts("Codex actual app popup, read-only keys, wake and callback: PASS"); return 0;
}
