#include "workout_ui.h"

#include "lvgl.h"
#include "esp_log.h"
#include "workout_font_inventory.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(workout_font_12);
LV_FONT_DECLARE(workout_font_16);
LV_FONT_DECLARE(workout_font_20);
LV_FONT_DECLARE(workout_digits_35);

#define UI_BACKGROUND 0x121B18
#define UI_PANEL 0x203029
#define UI_INK 0xF1F5E8
#define UI_MUTED 0xABBCB0
#define UI_ACCENT 0xB7E665
#define UI_TRACK 0x36483B

static lv_obj_t *s_screen, *s_qr, *s_qr_paper;
static workout_ui_state_t s_state;
static char s_qr_payload[160];

static void rectangle(lv_layer_t *layer, int x, int y, int w, int h, uint32_t color) {
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = lv_color_hex(color);
    descriptor.bg_opa = LV_OPA_COVER;
    const lv_area_t area = {.x1 = x, .y1 = y, .x2 = x + w - 1, .y2 = y + h - 1};
    lv_draw_rect(layer, &descriptor, &area);
}

static void label(lv_layer_t *layer, const char *value, int x, int y, int width,
                  const lv_font_t *font, uint32_t color) {
    lv_draw_label_dsc_t descriptor;
    lv_draw_label_dsc_init(&descriptor);
    descriptor.font = font;
    descriptor.color = lv_color_hex(color);
    descriptor.text = value;
    descriptor.text_local = true; /* Draw tasks outlive this function's stack strings. */
    const lv_area_t area = {.x1 = x, .y1 = y, .x2 = x + width - 1, .y2 = y + font->line_height - 1};
    lv_draw_label(layer, &descriptor, &area);
}

static void small(lv_layer_t *layer, const char *value, int x, int y, int width) {
    label(layer, value, x, y, width, &workout_font_12, UI_MUTED);
}

static void title(lv_layer_t *layer, const char *value) {
    label(layer, value, 20, 35, 200, &workout_font_20, UI_INK);
}

static bool cached(void) {
    return s_state.from_cache || !s_state.network.online || s_state.data.stale
        || s_state.network.error != WORKOUT_NET_OK;
}

static const char *network_label(void) {
    if (s_state.network.setup_active) return "配网";
    if (s_state.network.syncing) return "同步中";
    if (!s_state.network.online) return "离线";
    if (s_state.network.cache_error) return "缓存失败";
    return cached() ? "旧数据" : "Wi-Fi";
}

static void header(lv_layer_t *layer) {
    small(layer, network_label(), 27, 12, 114);
    char battery[12];
    if (s_state.battery < 0) strcpy(battery, "--%");
    else snprintf(battery, sizeof(battery), "%d%%", s_state.battery);
    small(layer, battery, 181, 12, 34);
}

static void hint(lv_layer_t *layer, const char *value) {
    rectangle(layer, 20, 292, 200, 1, UI_TRACK);
    small(layer, value, 24, 300, 192);
}

static void timestamp(lv_layer_t *layer) {
    const char *stamp = s_state.data.updated_at;
    char value[64];
    snprintf(value, sizeof(value), "%s %.2s/%.2s %.5s",
             cached() ? "上次同步" : "同步", stamp + 5, stamp + 8, stamp + 11);
    small(layer, value, 20, 270, 200);
}

static void distance_string(uint32_t distance, char *output, size_t size) {
    snprintf(output, size, "%lu.%lu", (unsigned long)(distance / 10), (unsigned long)(distance % 10));
}

static void period(lv_layer_t *layer) {
    char value[48];
    if (s_state.navigation.monthly) snprintf(value, sizeof(value), "%u年%u月", s_state.data.year, s_state.data.month);
    else snprintf(value, sizeof(value), "%.4s %.5s ~ %.5s", s_state.data.week_start,
                  s_state.data.week_start + 5, s_state.data.week_end + 5);
    small(layer, value, 20, 63, 185);
    label(layer, s_state.navigation.monthly ? "月" : "周", 205, 40, 16, &workout_font_12, UI_ACCENT);
}

static void summary(lv_layer_t *layer, const workout_summary_t *data) {
    char value[40];
    small(layer, "时长", 22, 181, 92);
    small(layer, "配速 /km", 140, 181, 80);
    snprintf(value, sizeof(value), "%lu时%02lu分", (unsigned long)(data->duration / 3600),
             (unsigned long)(data->duration / 60 % 60));
    label(layer, value, 22, 199, 114, &workout_font_16, UI_INK);
    if (!data->pace || !data->distance) strcpy(value, "--");
    else snprintf(value, sizeof(value), "%lu'%02lu\"", (unsigned long)(data->pace / 60), (unsigned long)(data->pace % 60));
    label(layer, value, 140, 199, 80, &workout_font_16, UI_INK);
    if (!data->goal) strcpy(value, "目标未设置");
    else snprintf(value, sizeof(value), "目标 %lu.%lu km", (unsigned long)(data->goal / 10), (unsigned long)(data->goal % 10));
    small(layer, value, 20, 222, 150);
    if (!data->goal) strcpy(value, "--");
    else {
        uint64_t percent = (uint64_t)data->distance * 100 / data->goal;
        if (percent > 9999) strcpy(value, "9999%+");
        else snprintf(value, sizeof(value), "%lu%%", (unsigned long)percent);
    }
    small(layer, value, 174, 222, 46);
    unsigned filled = workout_progress(data->distance, data->goal, 20);
    for (unsigned i = 0; i < 20; i++) rectangle(layer, 20 + (int)i * 10, 242, 8, 12, i < filled ? UI_ACCENT : UI_TRACK);
}

static void dashboard(lv_layer_t *layer) {
    title(layer, "运动");
    if (!s_state.has_data) {
        label(layer, "还没有运动数据", 32, 135, 180, &workout_font_20, UI_INK);
        small(layer, s_state.network.has_config ? "等待同步，可在菜单中重试" : "长按确定，进入网络配置", 36, 185, 180);
        if (s_state.network.error == WORKOUT_NET_HTTP) {
            char value[40];
            snprintf(value, sizeof(value), "接口错误 HTTP %d", s_state.network.http_status);
            small(layer, value, 36, 218, 180);
        } else if (s_state.network.error == WORKOUT_NET_STORAGE) small(layer, "存储初始化失败", 36, 218, 180);
        else if (s_state.network.error == WORKOUT_NET_INIT) small(layer, "网络启动失败，请重试", 36, 218, 180);
        hint(layer, "长按确定：页面菜单");
        return;
    }
    period(layer);
    const workout_summary_t *data = s_state.navigation.monthly ? &s_state.data.monthly : &s_state.data.weekly;
    rectangle(layer, 20, 89, 200, 83, UI_PANEL);
    small(layer, s_state.navigation.monthly ? "本月跑量" : "本周跑量", 30, 99, 170);
    char value[24];
    distance_string(data->distance, value, sizeof(value));
    const lv_font_t *font = strlen(value) <= 5 ? &workout_digits_35 : &workout_font_20;
    label(layer, value, 30, 121, 153, font, UI_INK);
    small(layer, "km", 188, 148, 30);
    summary(layer, data);
    timestamp(layer);
    hint(layer, "上下 周/月  确定 明细");
}

static void details(lv_layer_t *layer) {
    if (!s_state.has_data) { dashboard(layer); return; }
    title(layer, s_state.navigation.monthly ? "月度明细" : "每周明细");
    period(layer);
    unsigned count = s_state.navigation.monthly ? s_state.data.week_count : 7;
    for (unsigned i = 0; i < count; i++) {
        char value[32];
        if (s_state.navigation.monthly) {
            unsigned last = (i + 1) * 7;
            static const unsigned lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
            unsigned days = lengths[s_state.data.month - 1];
            if (s_state.data.month == 2 && s_state.data.year % 4 == 0
                && (s_state.data.year % 100 != 0 || s_state.data.year % 400 == 0)) days++;
            if (last > days) last = days;
            snprintf(value, sizeof(value), "%u~%u日", i * 7 + 1, last);
        } else {
            static const char *weekdays[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
            strcpy(value, weekdays[i]);
        }
        int y = 103 + (int)i * 22;
        small(layer, value, 24, y, 88);
        uint32_t amount = s_state.navigation.monthly ? s_state.data.weeks[i] : s_state.data.days[i];
        snprintf(value, sizeof(value), "%lu.%lu km", (unsigned long)(amount / 10), (unsigned long)(amount % 10));
        label(layer, value, 123, y, 98, &workout_font_16, UI_INK);
    }
    timestamp(layer);
    hint(layer, "上下 周/月  确定 返回");
}

static void menu(lv_layer_t *layer) {
    title(layer, "页面");
    small(layer, "选择一个页面", 20, 65, 180);
    static const char *entries[] = {"运动看板", "网络与接口", "立即同步", "更多页面"};
    for (unsigned i = 0; i < 4; i++) {
        bool selected = i == s_state.navigation.selection;
        rectangle(layer, 20, 94 + (int)i * 42, 200, 34, selected ? UI_ACCENT : UI_PANEL);
        label(layer, entries[i], 32, 102 + (int)i * 42, 170, &workout_font_16,
              selected ? UI_BACKGROUND : UI_INK);
    }
    hint(layer, "上下 选择  确定 进入");
}

static const char *setup_message(void) {
    switch (s_state.network.setup_result) {
        case WORKOUT_SETUP_CONNECTING: return "正在连接路由器";
        case WORKOUT_SETUP_SAVED: return "配置已保存，正在同步";
        case WORKOUT_SETUP_BAD_WIFI: return "连接失败，请检查密码";
        case WORKOUT_SETUP_STORAGE: return "保存失败，请重试";
        case WORKOUT_SETUP_FAILED: return "热点启动失败，请重试";
        case WORKOUT_SETUP_EXPIRED: return "配网已超时，请重试";
        default: return "确定开始配网";
    }
}

static void network(lv_layer_t *layer) {
    title(layer, s_state.navigation.setup_step ? "扫码配置" : "连接热点");
    if (!s_state.network.setup_active) {
        small(layer, s_state.network.online ? "已连接 Wi-Fi" : "尚未连接 Wi-Fi", 20, 70, 200);
        const char *message = s_state.network.error == WORKOUT_NET_STORAGE ? "存储不可用，请检查设备"
                            : s_state.network.error == WORKOUT_NET_INIT ? "网络启动失败，请重试" : setup_message();
        label(layer, message, 20, 135, 205, &workout_font_16, UI_INK);
        small(layer, "手机配置 Wi-Fi 和后端地址", 20, 181, 205);
        small(layer, "长按下键：清除网络设置", 20, 225, 205);
        hint(layer, "确定 配网  长按确定 菜单");
        return;
    }
    small(layer, s_state.navigation.setup_step ? "第二步：手机保持连接热点" : "第一步：手机扫码加入热点", 20, 64, 210);
    if (s_state.navigation.setup_step) {
        small(layer, setup_message(), 20, 255, 210);
        small(layer, "192.168.4.1", 20, 273, 210);
    } else {
        char value[48];
        small(layer, s_state.network.ap_ssid, 20, 253, 200);
        snprintf(value, sizeof(value), "密码 %s", s_state.network.ap_password);
        small(layer, value, 20, 271, 205);
    }
    hint(layer, "上下/确定 下一步  长按 菜单");
}

static void draw(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    header(layer);
    switch (s_state.navigation.view) {
        case WORKOUT_VIEW_DASHBOARD: dashboard(layer); break;
        case WORKOUT_VIEW_DETAILS: details(layer); break;
        case WORKOUT_VIEW_MENU: menu(layer); break;
        case WORKOUT_VIEW_NETWORK: network(layer); break;
        case WORKOUT_VIEW_CLEAR:
            title(layer, "清除网络设置");
            label(layer, "清除 Wi-Fi 和接口地址？", 20, 134, 205, &workout_font_16, UI_INK);
            small(layer, "保留上次运动数据", 20, 181, 205);
            hint(layer, "确定 清除  上下 取消");
            break;
        case WORKOUT_VIEW_FUTURE:
            title(layer, "更多页面");
            for (unsigned i = 0; i < 9; i++) rectangle(layer, 90 + (int)(i % 3) * 22,
                                                     123 + (int)(i / 3) * 22, 18, 18, i < 4 ? UI_ACCENT : UI_TRACK);
            small(layer, "为下一项功能预留", 68, 218, 145);
            hint(layer, "长按确定：页面菜单");
            break;
    }
}

static bool font_coverage(void) {
    const lv_font_t *fonts[] = {&workout_font_12, &workout_font_16, &workout_font_20};
    for (unsigned i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        for (unsigned j = 0; j < sizeof(workout_font_codepoints) / sizeof(workout_font_codepoints[0]); j++) {
            lv_font_glyph_dsc_t glyph = {0};
            if (!lv_font_get_glyph_dsc(fonts[i], &glyph, workout_font_codepoints[j], 0) || glyph.is_placeholder) {
                ESP_LOGE("workout_ui", "Font %u missing U+%04lX", i, (unsigned long)workout_font_codepoints[j]);
                return false;
            }
        }
    }
    return true;
}

bool workout_ui_create(void) {
    if (!font_coverage()) return false;
    s_screen = lv_obj_create(NULL);
    if (!s_screen) return false;
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(UI_BACKGROUND), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_add_event_cb(s_screen, draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_screen_load(s_screen);
    return true;
}

static void qr_update(void) {
    bool show = s_state.navigation.view == WORKOUT_VIEW_NETWORK && s_state.network.setup_active;
    if (!show) {
        if (s_qr_paper) lv_obj_delete(s_qr_paper);
        s_qr = s_qr_paper = NULL;
        s_qr_payload[0] = '\0';
        return;
    }
    char payload[160];
    if (s_state.navigation.setup_step) snprintf(payload, sizeof(payload), "http://192.168.4.1/?token=%s", s_state.network.token);
    else snprintf(payload, sizeof(payload), "WIFI:T:WPA;S:%s;P:%s;;", s_state.network.ap_ssid, s_state.network.ap_password);
    if (strcmp(payload, s_qr_payload) == 0) return;
    if (!s_qr) {
        s_qr_paper = lv_obj_create(s_screen);
        if (!s_qr_paper) return;
        lv_obj_remove_style_all(s_qr_paper);
        lv_obj_remove_flag(s_qr_paper, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_qr_paper, 32, 82);
        lv_obj_set_size(s_qr_paper, 176, 166);
        lv_obj_set_style_bg_color(s_qr_paper, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(s_qr_paper, LV_OPA_COVER, 0);
        s_qr = lv_qrcode_create(s_qr_paper);
        if (!s_qr) { lv_obj_delete(s_qr_paper); s_qr_paper = NULL; return; }
        lv_qrcode_set_size(s_qr, 144);
        lv_qrcode_set_dark_color(s_qr, lv_color_black());
        lv_qrcode_set_light_color(s_qr, lv_color_white());
        lv_obj_center(s_qr);
    }
    if (lv_qrcode_update(s_qr, payload, strlen(payload)) == LV_RESULT_OK) strcpy(s_qr_payload, payload);
}

void workout_ui_update(const workout_ui_state_t *state) {
    s_state = *state;
    qr_update();
    lv_obj_invalidate(s_screen);
}
