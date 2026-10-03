#include "workout_ui.h"
#include "bsp_display_rounding.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(workout_font_12);
LV_FONT_DECLARE(workout_font_16);
LV_FONT_DECLARE(workout_font_20);
LV_FONT_DECLARE(workout_digits_35);
LV_FONT_DECLARE(workout_clock_50);
LV_FONT_DECLARE(workout_network_font_16);
LV_FONT_DECLARE(workout_monitor_font_12);
static uint16_t s_frame[240 * 320], s_buffer[240 * 20];

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    const uint16_t *input = (const uint16_t *)pixels;
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            uint16_t color = *input++;
            s_frame[y * 240 + x] = bsp_display_pixel_outside_rounded_rect(x, y, 240, 320, 30) ? 0 : color;
        }
    }
    lv_display_flush_ready(display);
}

static void screenshot(const char *name) {
    lv_refr_now(NULL);
    FILE *file = fopen(name, "wb");
    assert(file);
    fprintf(file, "P6\n240 320\n255\n");
    for (unsigned i = 0; i < 240 * 320; i++) {
        uint16_t color = s_frame[i];
        unsigned char rgb[3] = {(unsigned char)((color >> 11) * 255 / 31),
            (unsigned char)(((color >> 5) & 63) * 255 / 63), (unsigned char)((color & 31) * 255 / 31)};
        assert(fwrite(rgb, 1, 3, file) == 3);
    }
    fclose(file);
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    assert(memory.free_size > 1024);
    printf("%s: LVGL peak=%lu free=%lu\n", name, (unsigned long)memory.max_used, (unsigned long)memory.free_size);
}

static void quota_screens(workout_ui_state_t *state) {
    state->navigation.view = WORKOUT_VIEW_AI;
    for (unsigned i = 0; i < AI_QUOTA_PROVIDERS; i++) {
        ai_quota_state_t *quota = &state->quota.providers[i];
        quota->available = quota->persisted = true;
        strcpy(quota->data.fetched_at, "2026-10-02T14:32:00+08:00");
        assert(workout_parse_timestamp(quota->data.fetched_at, &quota->data.fetched_seconds));
        quota->data.windows[0] = (ai_quota_window_t){.available = true, .remaining = i ? 350 : 725};
        strcpy(quota->data.windows[0].reset_at, "2026-10-02T18:30:00+08:00");
        quota->data.windows[1] = (ai_quota_window_t){.available = true, .remaining = i ? 810 : 580};
        strcpy(quota->data.windows[1].reset_at, "2026-10-05T12:00:00+08:00");
    }
    strcpy(state->quota.providers[1].data.level, "PRO");
    state->network.online = true;
    state->network.has_config = true;
    workout_ui_update(state); screenshot("codex-quota.ppm");
    state->navigation.ai_weekly = true;
    workout_ui_update(state); screenshot("codex-weekly.ppm");
    state->navigation.ai_provider = 1; state->navigation.ai_weekly = false;
    workout_ui_update(state); screenshot("glm-quota.ppm");
    state->quota.providers[1].data.windows[0].remaining = 0;
    state->quota.providers[1].data.windows[0].reset_at[0] = 0;
    workout_ui_update(state); screenshot("quota-exhausted.ppm");
    state->quota.providers[1].data.windows[0].available = false;
    workout_ui_update(state); screenshot("quota-window-missing.ppm");
    state->quota.providers[1].failed = true; state->network.online = false;
    workout_ui_update(state); screenshot("quota-offline.ppm");
    state->quota.providers[1].available = false; state->quota.providers[1].http_status = 503;
    state->network.online = true;
    workout_ui_update(state); screenshot("quota-unavailable.ppm");
}

static void calendar_time(workout_ui_state_t *state, const char *timestamp) {
    int64_t seconds;
    assert(workout_parse_timestamp(timestamp, &seconds));
    assert(passport_calendar_clock(seconds, &state->clock));
    state->navigation.clock_valid = true;
    state->navigation.today = state->navigation.calendar_day = state->clock.day;
}

static uint32_t clock_pixels(void) {
    uint32_t hash = 2166136261u;
    for (unsigned y = 34; y < 90; y++)
        for (unsigned x = 20; x < 220; x++) hash = (hash ^ s_frame[y * 240 + x]) * 16777619u;
    return hash;
}

static void home_screens(workout_ui_state_t *state) {
    state->navigation.view = WORKOUT_VIEW_HOME;
    state->clock = (passport_clock_t){0};
    workout_ui_update(state); screenshot("home-uncalibrated.ppm");
    for (unsigned i = 0; i < AI_QUOTA_PROVIDERS; i++) {
        state->quota.providers[i].available = true;
        state->quota.providers[i].data.windows[0] = (ai_quota_window_t){.available = true, .remaining = i ? 860 : 725};
    }
    calendar_time(state, "2026-10-03T09:41:00+08:00");
    workout_ui_update(state); screenshot("home-national-day.ppm");
    uint32_t before = clock_pixels(); state->clock.minute++;
    workout_ui_update(state); screenshot("home-next-minute.ppm");
    assert(before != clock_pixels());
    state->navigation.home_focus = 1;
    workout_ui_update(state); screenshot("home-ai-focus.ppm");
    state->navigation.home_focus = 0;
    calendar_time(state, "2026-09-25T09:41:00+08:00");
    workout_ui_update(state); screenshot("home-mid-autumn.ppm");
    calendar_time(state, "2026-10-10T09:41:00+08:00");
    workout_ui_update(state); screenshot("home-auspicious.ppm");
    state->navigation.view = WORKOUT_VIEW_ALMANAC;
    workout_ui_update(state); screenshot("almanac-auspicious.ppm");
    state->navigation.view = WORKOUT_VIEW_CALENDAR;
    workout_ui_update(state); screenshot("calendar-october.ppm");
    calendar_time(state, "2026-08-01T09:41:00+08:00");
    workout_ui_update(state); screenshot("calendar-six-weeks.ppm");
    state->navigation.view = WORKOUT_VIEW_HOME;
    calendar_time(state, "2026-10-12T09:41:00+08:00");
    workout_ui_update(state); screenshot("home-ordinary.ppm");
    calendar_time(state, "2025-07-25T09:41:00+08:00");
    workout_ui_update(state); screenshot("home-leap-month.ppm");
    state->network.online = false; state->quota.providers[0].data.windows[0].remaining = 0;
    state->quota.providers[1].data.windows[0].available = false;
    workout_ui_update(state); screenshot("home-offline-quota-unknown.ppm");
    state->network.online = true; state->navigation.view = WORKOUT_VIEW_DASHBOARD;
}

static void connectivity_screens(workout_ui_state_t *state) {
    state->network = (workout_network_status_t){.wifi_count = 5, .server_count = 2, .has_config = true, .online = true};
    const char *names[] = {"Home_2.4G", "Office_2.4G", "Phone_Hotspot", "家庭无线网络", "Long_Network_Name_1234567890123456"};
    for (unsigned i = 0; i < 5; i++) strcpy(state->network.wifi_names[i], names[i]);
    strcpy(state->network.servers[0], "http://192.168.1.20:8000/api/workout");
    strcpy(state->network.servers[1], "http://example.com:8000/long-proxy-prefix/api/workout");
    state->navigation.view = WORKOUT_VIEW_NETWORK; state->navigation.selection = 0;
    workout_ui_update(state); screenshot("network-hub.ppm");
    state->navigation.view = WORKOUT_VIEW_WIFI; state->navigation.selection = 1;
    workout_ui_update(state); screenshot("wifi-profiles.ppm");
    assert(strcmp(lv_label_get_text(lv_obj_get_child(lv_screen_active(), 0)), names[1]) == 0);
    state->navigation.selection = 3;
    workout_ui_update(state); screenshot("wifi-chinese.ppm");
    assert(strcmp(lv_label_get_text(lv_obj_get_child(lv_screen_active(), 0)), names[3]) == 0);
    state->navigation.selection = 4;
    workout_ui_update(state); screenshot("wifi-long-name.ppm");
    strcpy(state->network.wifi_names[4], "WiFi-\xF0\x9F\x98\x80");
    workout_ui_update(state); screenshot("wifi-unsupported-glyph.ppm");
    assert(strstr(lv_label_get_text(lv_obj_get_child(lv_screen_active(), 0)), "[U+1F600]"));
    state->navigation.view = WORKOUT_VIEW_SERVER; state->navigation.selection = 1;
    workout_ui_update(state); screenshot("server-profiles.ppm");
    state->network.error = WORKOUT_NET_HTTP;
    workout_ui_update(state); screenshot("server-unavailable.ppm");
    state->network.switch_result = WORKOUT_SWITCH_STORAGE;
    workout_ui_update(state); screenshot("profile-storage-failed.ppm");
    state->network.switch_result = WORKOUT_SWITCH_IDLE; state->network.error = WORKOUT_NET_OFFLINE;
    state->network.online = false; state->network.connecting = true;
    state->network.connecting_wifi = 1; state->network.attempted = 2;
    state->navigation.view = WORKOUT_VIEW_CONNECTION; state->navigation.selection = 0;
    workout_ui_update(state); screenshot("wifi-auto-switch.ppm");
    state->network.connecting = false; state->network.exhausted = true;
    workout_ui_update(state); screenshot("wifi-all-offline.ppm");
    state->network.exhausted = false; state->network.switch_result = WORKOUT_SWITCH_FAILED;
    workout_ui_update(state); screenshot("wifi-switch-failed.ppm");
    state->network.online = true;
    workout_ui_update(state); screenshot("wifi-switch-restored.ppm");
    state->network.wifi_count = 0; state->navigation.view = WORKOUT_VIEW_WIFI;
    workout_ui_update(state); screenshot("wifi-profiles-empty.ppm");
    lv_font_glyph_dsc_t glyph;
    assert(lv_font_get_glyph_dsc(&workout_network_font_16, &glyph, 0x9F98, 0) && !glyph.is_placeholder);
    assert(!lv_font_get_glyph_dsc(&workout_network_font_16, &glyph, 0x1F600, 0) || glyph.is_placeholder);
}

static void codex_text_screens(workout_ui_state_t *state) {
    codex_task_t *task = &state->codex.data.tasks[0];
    state->codex.data.count = 1; state->navigation.view = WORKOUT_VIEW_CODEX_DETAILS;
    memset(task->title, 'W', 96); task->title[96] = 0;
    workout_ui_update(state); screenshot("codex-task-marquee.ppm");
    lv_obj_t *title_label = lv_obj_get_child(lv_screen_active(), 1);
    assert(!lv_obj_has_flag(title_label, LV_OBJ_FLAG_HIDDEN));
    assert(strlen(lv_label_get_text(title_label)) == 96);
    memcpy(task->title, "\xF0\x9F\x98\x80", 4);
    workout_ui_update(state); screenshot("codex-task-unsupported.ppm");
    assert(strstr(lv_label_get_text(title_label), "[U+1F600]"));
    const lv_font_t *font = lv_obj_get_style_text_font(title_label, LV_PART_MAIN);
    lv_font_glyph_dsc_t glyph = {0};
    assert(lv_font_get_glyph_dsc(font, &glyph, 0x9F98, 0) && !glyph.is_placeholder);
    assert(lv_font_get_glyph_dsc(&workout_monitor_font_12, &glyph, 0x9F98, 0) && !glyph.is_placeholder);
    strcpy(task->title, "龘项目动态标题"); strcpy(task->project, "任意项目"); strcpy(task->step, "请确认执行权限");
    workout_ui_update(state); screenshot("codex-task-dynamic-chinese.ppm");
    state->navigation.view = WORKOUT_VIEW_MENU; workout_ui_update(state); lv_refr_now(NULL);
}

static uint32_t duration_pixels(void) {
    uint32_t hash = 2166136261u;
    for (unsigned y = 233; y < 268; y++)
        for (unsigned x = 20; x < 185; x++) hash = (hash ^ s_frame[y * 240 + x]) * 16777619u;
    return hash;
}

static void codex_clock_screens(workout_ui_state_t *state) {
    memset(&state->codex, 0, sizeof(state->codex));
    state->network.online = state->codex.available = state->codex.data.source_online = true;
    state->navigation.view = WORKOUT_VIEW_CODEX; state->navigation.codex_selection = 0;
    state->codex.data.count = state->navigation.codex_count = 1;
    state->codex.data.generated_seconds = 200; state->codex.received_ms = state->now_ms = 1250;
    strcpy(state->codex.data.stream_id, "stream_clock");
    codex_task_t *task = &state->codex.data.tasks[0];
    *task = (codex_task_t){.task_id = "task_clock", .title = "检查本地连续计时", .project = "ai-passport",
        .status = CODEX_TASK_RUNNING, .started_seconds = 100, .updated_seconds = 140};
    workout_ui_update(state); screenshot("codex-clock-nonlive.ppm");
    uint32_t initial = duration_pixels();
    state->now_ms = 2250;
    workout_ui_update(state); screenshot("codex-clock-next-second.ppm");
    assert(initial != duration_pixels()); /* Actual screen changes without receiving another snapshot. */
    state->codex.received_ms = state->now_ms = 3250;
    task->status = CODEX_TASK_APPROVAL; task->approval = CODEX_APPROVAL_REQUESTED;
    workout_ui_update(state); screenshot("codex-clock-approval.ppm");
    initial = duration_pixels(); state->now_ms += 1000;
    workout_ui_update(state); lv_refr_now(NULL); assert(initial != duration_pixels());
    state->codex.received_ms = state->now_ms;
    task->status = CODEX_TASK_ENDED; task->approval = CODEX_APPROVAL_NONE; task->updated_seconds = 180;
    workout_ui_update(state); screenshot("codex-ended-nonlive.ppm");
    initial = duration_pixels(); state->now_ms += 60000; state->network.online = false;
    workout_ui_update(state); screenshot("codex-ended-offline.ppm");
    assert(initial == duration_pixels()); /* Confirmed stop stays fixed even with stale collector data. */
    state->network.online = true; state->codex.received_ms = state->now_ms;
    state->navigation.view = WORKOUT_VIEW_CODEX_DETAILS;
    workout_ui_update(state); screenshot("codex-ended-nonlive-details.ppm");
}

static void codex_screens(workout_ui_state_t *state) {
    state->navigation.view = WORKOUT_VIEW_CODEX;
    state->navigation.codex_count = state->codex.data.count = 3;
    state->network.online = state->network.has_config = true;
    state->codex.available = state->codex.data.source_online = true;
    state->codex.received_ms = state->now_ms = 1000;
    state->codex.data.active_count = 2;
    strcpy(state->codex.data.stream_id, "stream_a");
    assert(workout_parse_timestamp("2026-10-02T10:10:28Z", &state->codex.data.generated_seconds));
    for (unsigned i = 0; i < 3; i++) {
        codex_task_t *task = &state->codex.data.tasks[i];
        snprintf(task->task_id, sizeof(task->task_id), "task_%u", i);
        strcpy(task->project, "ai-passport"); task->fresh = true;
        strcpy(task->title, "增加 Codex 任务与审批提醒"); strcpy(task->step, "正在修改代码");
        task->started_seconds = state->codex.data.generated_seconds - 628;
        task->updated_seconds = state->codex.data.generated_seconds;
    }
    workout_ui_update(state); screenshot("codex-tasks.ppm");
    strcpy(state->codex.data.tasks[0].title, "为 AI Passport 增加任务提醒与断网后的状态恢复");
    state->navigation.view = WORKOUT_VIEW_CODEX_DETAILS;
    workout_ui_update(state); screenshot("codex-task-details.ppm");
    codex_task_t *task = &state->codex.data.tasks[0];
    state->codex.received_ms = ++state->now_ms;
    task->status = CODEX_TASK_APPROVAL; task->approval = CODEX_APPROVAL_REQUESTED;
    strcpy(task->approval_summary, "需要在电脑端授权：Bash");
    state->codex.data.pending_count = 1; state->navigation.view = WORKOUT_VIEW_CODEX;
    workout_ui_update(state); screenshot("codex-task-approval-badge.ppm");
    state->codex_popup = true;
    strcpy(state->codex_alert.title, "增加 Codex 任务与审批提醒");
    strcpy(state->codex_alert.summary, "需要在电脑端授权：Bash");
    workout_ui_update(state); screenshot("codex-approval-popup.ppm");
    assert(lv_obj_has_flag(lv_obj_get_child(lv_screen_active(), 0), LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(lv_obj_get_child(lv_screen_active(), 1), LV_OBJ_FLAG_HIDDEN));
    state->codex_popup = false; task->approval = CODEX_APPROVAL_UNKNOWN; task->fresh = false;
    state->codex.received_ms = ++state->now_ms;
    workout_ui_update(state); screenshot("codex-approval-unknown.ppm");
    task->approval = CODEX_APPROVAL_NONE; task->status = CODEX_TASK_ENDED; task->fresh = true;
    state->codex.received_ms = ++state->now_ms;
    state->codex.data.pending_count = 0;
    workout_ui_update(state); screenshot("codex-task-ended.ppm");
    task->status = CODEX_TASK_INTERRUPTED;
    state->codex.received_ms = ++state->now_ms;
    workout_ui_update(state); screenshot("codex-task-interrupted.ppm");
    state->network.online = false; state->now_ms = 61000;
    workout_ui_update(state); screenshot("codex-tasks-offline.ppm");
    state->network.online = true; state->codex.failed = true;
    workout_ui_update(state); screenshot("codex-tasks-sync-failed.ppm");
    state->codex.failed = false; state->now_ms = 1000; state->codex.data.count = 0;
    workout_ui_update(state); screenshot("codex-tasks-empty.ppm");
    codex_clock_screens(state);
    codex_text_screens(state);
}

int main(void) {
    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, s_buffer, NULL, sizeof(s_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    assert(workout_ui_create());
    const lv_font_t *fonts[] = {&workout_font_12, &workout_font_16, &workout_font_20, &workout_digits_35};
    for (unsigned i = 0; i < 4; i++) {
        lv_font_glyph_dsc_t glyph = {0};
        assert(!lv_font_get_glyph_dsc(fonts[i], &glyph, 0x9F98, 0) || glyph.is_placeholder);
    }
    workout_ui_state_t state = {.battery = 82, .has_data = true, .navigation.view = WORKOUT_VIEW_DASHBOARD};
    state.network.online = true;
    state.data.year = 2026; state.data.month = 10; state.data.week_count = 5;
    strcpy(state.data.week_start, "2026-09-28"); strcpy(state.data.week_end, "2026-10-04");
    strcpy(state.data.updated_at, "2026-10-02T10:30:00+08:00");
    state.data.weekly = (workout_summary_t){246,9028,367,300};
    state.data.monthly = (workout_summary_t){140,5151,367,1000};
    state.data.days[0] = 52; state.data.days[2] = 54; state.data.days[4] = 140;
    home_screens(&state);
    workout_ui_update(&state); screenshot("dashboard.ppm");
    state.from_cache = true; state.network.online = false;
    workout_ui_update(&state); screenshot("offline.ppm");
    state.navigation.view = WORKOUT_VIEW_DETAILS;
    workout_ui_update(&state); screenshot("details.ppm");
    state.navigation.view = WORKOUT_VIEW_SETUP; state.network.setup_active = true;
    strcpy(state.network.ap_ssid, "Passport-TEST"); strcpy(state.network.ap_password, "TESTPASS1234");
    strcpy(state.network.token, "0123456789abcdef0123456789abcdef");
    workout_ui_update(&state); screenshot("wifi-qr.ppm");
    state.navigation.setup_step = 1;
    workout_ui_update(&state); screenshot("config-qr.ppm");
    state.navigation.view = WORKOUT_VIEW_MENU;
    workout_ui_update(&state); screenshot("menu.ppm");
    state.navigation.view = WORKOUT_VIEW_DASHBOARD; state.has_data = false;
    workout_ui_update(&state); screenshot("empty.ppm");
    quota_screens(&state);
    connectivity_screens(&state);
    codex_screens(&state);
    lv_mem_monitor_t before, after;
    lv_mem_monitor(&before);
    for (unsigned i = 0; i < 30; i++) {
        state.navigation.view = WORKOUT_VIEW_HOME;
        workout_ui_update(&state); lv_refr_now(NULL);
        state.navigation.view = WORKOUT_VIEW_CALENDAR;
        workout_ui_update(&state); lv_refr_now(NULL);
        state.navigation.view = WORKOUT_VIEW_ALMANAC;
        workout_ui_update(&state); lv_refr_now(NULL);
        state.network.setup_active = true;
        state.navigation.view = WORKOUT_VIEW_SETUP;
        state.navigation.setup_step = i % 2;
        workout_ui_update(&state); lv_refr_now(NULL);
        state.navigation.view = WORKOUT_VIEW_MENU;
        workout_ui_update(&state); lv_refr_now(NULL);
        state.navigation.view = WORKOUT_VIEW_CODEX; state.codex_popup = true;
        workout_ui_update(&state); lv_refr_now(NULL);
        state.codex_popup = false; state.navigation.view = WORKOUT_VIEW_CODEX_DETAILS;
        workout_ui_update(&state); lv_refr_now(NULL);
        state.navigation.view = WORKOUT_VIEW_MENU;
        workout_ui_update(&state); lv_refr_now(NULL);
    }
    lv_mem_monitor(&after);
    assert(before.free_size == after.free_size);
    puts("Repeated QR/page lifecycle: PASS");
    return 0;
}
