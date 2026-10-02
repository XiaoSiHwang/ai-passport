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
LV_FONT_DECLARE(workout_network_font_16);
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
    workout_ui_state_t state = {.battery = 82, .has_data = true};
    state.network.online = true;
    state.data.year = 2026; state.data.month = 10; state.data.week_count = 5;
    strcpy(state.data.week_start, "2026-09-28"); strcpy(state.data.week_end, "2026-10-04");
    strcpy(state.data.updated_at, "2026-10-02T10:30:00+08:00");
    state.data.weekly = (workout_summary_t){246,9028,367,300};
    state.data.monthly = (workout_summary_t){140,5151,367,1000};
    state.data.days[0] = 52; state.data.days[2] = 54; state.data.days[4] = 140;
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
    lv_mem_monitor_t before, after;
    lv_mem_monitor(&before);
    for (unsigned i = 0; i < 30; i++) {
        state.network.setup_active = true;
        state.navigation.view = WORKOUT_VIEW_SETUP;
        state.navigation.setup_step = i % 2;
        workout_ui_update(&state); lv_refr_now(NULL);
        state.navigation.view = WORKOUT_VIEW_MENU;
        workout_ui_update(&state); lv_refr_now(NULL);
    }
    lv_mem_monitor(&after);
    assert(before.free_size == after.free_size);
    puts("Repeated QR/page lifecycle: PASS");
    return 0;
}
