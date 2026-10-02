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
LV_FONT_DECLARE(workout_network_font_16);
LV_FONT_DECLARE(workout_monitor_font_12);

/* Fixed light palette from the approved preview, independent of host appearance. */
#define UI_BACKGROUND 0xEEF1E8
#define UI_PANEL 0xDCE3D5
#define UI_INK 0x1B3028
#define UI_MUTED 0x4C6254
#define UI_ACCENT 0x316644
#define UI_TRACK 0xC3CFBC
#define UI_AMBER 0x775119
#define UI_AMBER_BG 0xF0E5C8
#define UI_AMBER_LINE 0xB18A40
#define UI_DANGER 0x89473B

static lv_obj_t *s_screen, *s_qr, *s_qr_paper;
static lv_obj_t *s_profile_label;
static lv_obj_t *s_codex_title_label;
static lv_font_t s_codex_font_12, s_codex_font_16;
static char s_codex_title_text[384];
static char s_profile_text[384];
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
    if (s_state.network.profile_busy) return "切换中";
    if (s_state.network.connecting) return "换网中";
    if (s_state.navigation.view == WORKOUT_VIEW_CODEX || s_state.navigation.view == WORKOUT_VIEW_CODEX_DETAILS) {
        if (!s_state.network.online) return "离线";
        if (s_state.codex.failed) return "同步失败";
        if (!s_state.codex.available) return "等待同步";
        if (!s_state.codex.data.source_online) return "电脑离线";
        if (s_state.codex.alerts_failed) return "提醒同步失败";
        unsigned index = s_state.navigation.codex_selection;
        if (index < s_state.codex.data.count && !codex_monitor_fresh(&s_state.codex,
            &s_state.codex.data.tasks[index], true, s_state.now_ms)) return "状态未确认";
        return "Wi-Fi";
    }
    if (s_state.navigation.view == WORKOUT_VIEW_AI) {
        const ai_quota_state_t *quota = &s_state.quota.providers[s_state.navigation.ai_provider];
        if (!s_state.network.online) return "离线";
        if (quota->syncing) return "同步中";
        if (!quota->available) return quota->failed ? "接口错误" : "等待同步";
        if (quota->available && !quota->persisted) return "缓存失败";
        if (quota->from_cache || quota->failed || quota->data.stale) return "旧数据";
        return "Wi-Fi";
    }
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
    static const char *entries[] = {"运动看板", "AI 用量", "网络与接口", "立即同步", "Codex 任务"};
    for (unsigned i = 0; i < 5; i++) {
        bool selected = i == s_state.navigation.selection;
        rectangle(layer, 20, 86 + (int)i * 36, 200, 30, selected ? UI_ACCENT : UI_PANEL);
        label(layer, entries[i], 32, 93 + (int)i * 36, 170, &workout_font_16,
              selected ? UI_BACKGROUND : UI_INK);
    }
    hint(layer, "上下 选择  确定 进入");
}

static void quota_reset(lv_layer_t *layer, const ai_quota_window_t *window) {
    small(layer, "重置时间", 20, 231, 100);
    if (!window->reset_at[0]) { small(layer, "暂不可用", 141, 231, 79); return; }
    const char *stamp = window->reset_at;
    char value[32];
    snprintf(value, sizeof(value), "%.2s/%.2s %.5s", stamp + 5, stamp + 8, stamp + 11);
    small(layer, value, 121, 231, 100);
    size_t length = strlen(stamp);
    snprintf(value, sizeof(value), "接口时区 UTC%s", stamp[length - 1] == 'Z' ? "+00:00" : stamp + length - 6);
    small(layer, value, 20, 250, 200);
}

static void quota_timestamp(lv_layer_t *layer, const ai_quota_state_t *quota) {
    bool old = quota->from_cache || quota->failed || quota->data.stale || !s_state.network.online;
    const char *stamp = quota->data.fetched_at;
    char value[48];
    snprintf(value, sizeof(value), "%s %.2s/%.2s %.5s", old ? "上次同步" : "同步", stamp + 5, stamp + 8, stamp + 11);
    small(layer, value, 20, 270, 200);
}

static void quota_empty(lv_layer_t *layer, const ai_quota_state_t *quota) {
    label(layer, "额度暂不可用", 30, 133, 190, &workout_font_20, UI_INK);
    small(layer, "未知额度显示为 --", 30, 177, 180);
    if (!s_state.network.has_config) small(layer, "长按确定，进入网络配置", 30, 200, 190);
    else if (quota->http_status && quota->http_status != 200) {
        char value[40];
        snprintf(value, sizeof(value), "接口错误 HTTP %d", quota->http_status);
        small(layer, value, 30, 200, 185);
    } else small(layer, "可在菜单中重新同步", 30, 200, 190);
}

static void quota_page(lv_layer_t *layer) {
    unsigned provider = s_state.navigation.ai_provider;
    const ai_quota_state_t *quota = &s_state.quota.providers[provider];
    const ai_quota_window_t *window = &quota->data.windows[s_state.navigation.ai_weekly ? 1 : 0];
    title(layer, provider ? "GLM" : "Codex");
    small(layer, provider && quota->data.level[0] ? quota->data.level : "额度详情", 106, 40, 114);
    small(layer, s_state.navigation.ai_weekly ? "7天额度" : "5小时额度", 20, 64, 200);
    hint(layer, "上下 换平台  确定 周期");
    if (!quota->available) { quota_empty(layer, quota); return; }
    rectangle(layer, 20, 89, 200, 83, UI_PANEL);
    small(layer, "剩余额度", 30, 99, 170);
    char value[32];
    if (!window->available) strcpy(value, "--");
    else if (window->remaining % 10) snprintf(value, sizeof(value), "%u.%u", window->remaining / 10, window->remaining % 10);
    else snprintf(value, sizeof(value), "%u", window->remaining / 10);
    label(layer, value, 30, 121, 153, window->available ? &workout_digits_35 : &workout_font_20, UI_INK);
    if (window->available) small(layer, "%", 188, 148, 30);
    if (!window->available) strcpy(value, "该周期暂不可用");
    else if (!window->remaining) strcpy(value, "额度已用尽");
    else snprintf(value, sizeof(value), "已使用 %u.%u%%", (1000 - window->remaining) / 10, (1000 - window->remaining) % 10);
    small(layer, value, 20, 182, 200);
    unsigned filled = window->available ? workout_progress(window->remaining, 1000, 20) : 0;
    for (unsigned i = 0; i < 20; i++) rectangle(layer, 20 + (int)i * 10, 205, 8, 9, i < filled ? UI_ACCENT : UI_TRACK);
    quota_reset(layer, window);
    quota_timestamp(layer, quota);
}

static const codex_task_t *codex_task(void) {
    unsigned index = s_state.navigation.codex_selection;
    return index < s_state.codex.data.count ? &s_state.codex.data.tasks[index] : NULL;
}

static void codex_safe_text(const char *input, char output[384], const lv_font_t *font) {
    size_t index = 0, used = 0;
    while (input[index] && used + 16 < 384) {
        size_t start = index;
        uint32_t code = workout_text_next(input, &index);
        lv_font_glyph_dsc_t glyph = {0};
        bool supported = code >= 32 && code != 127 && code != 0xFFFD
            && lv_font_get_glyph_dsc(font, &glyph, code, 0) && !glyph.is_placeholder
            && glyph.ofs_y >= -(int)font->base_line
            && (int)glyph.box_h + glyph.ofs_y <= (int)font->line_height - font->base_line;
        if (supported) {
            memcpy(output + used, input + start, index - start);
            used += index - start;
        } else used += (size_t)snprintf(output + used, 384 - used, "[U+%04lX]", (unsigned long)code);
    }
    output[used] = '\0';
}

static void codex_block(lv_layer_t *layer, const char *value, int x, int y, int width,
                        const lv_font_t *font, unsigned lines, uint32_t color) {
    char text[384];
    codex_safe_text(value, text, font);
    lv_point_t extent;
    int height = (int)lines * (font->line_height + 2) - 2;
    lv_text_get_size(&extent, text, font, 0, 2, width, LV_TEXT_FLAG_NONE);
    size_t length = strlen(text);
    while (length && extent.y > height) {
        do { length--; } while (length && ((unsigned char)text[length] & 0xC0) == 0x80);
        strcpy(text + length, "...");
        lv_text_get_size(&extent, text, font, 0, 2, width, LV_TEXT_FLAG_NONE);
    }
    lv_draw_label_dsc_t descriptor;
    lv_draw_label_dsc_init(&descriptor);
    descriptor.font = font;
    descriptor.color = lv_color_hex(color);
    descriptor.text = text;
    descriptor.text_local = true;
    descriptor.line_space = 2;
    const lv_area_t area = {.x1 = x, .y1 = y, .x2 = x + width - 1, .y2 = y + height - 1};
    lv_draw_label(layer, &descriptor, &area);
}

static const char *codex_status(const codex_task_t *task) {
    if (task->approval == CODEX_APPROVAL_UNKNOWN) return "未确认";
    static const char *names[] = {"执行中", "需审批", "回合结束", "已中断", "未确认"};
    return names[task->status];
}

static uint32_t codex_status_color(const codex_task_t *task, bool fresh) {
    if (!fresh) return UI_MUTED;
    if (task->status == CODEX_TASK_APPROVAL) return UI_AMBER;
    return task->status == CODEX_TASK_INTERRUPTED ? UI_DANGER : UI_INK;
}

static void codex_status_mark(lv_layer_t *layer, const codex_task_t *task, bool fresh, int x, int y) {
    uint32_t color = codex_status_color(task, fresh);
    rectangle(layer, x, y + 4, 6, 6, color);
    if (!fresh) rectangle(layer, x + 1, y + 5, 4, 4, UI_PANEL);
    label(layer, codex_status(task), x + 10, y, 65, &workout_font_12, color);
}

static const char *codex_step(const codex_task_t *task) {
    if (task->approval != CODEX_APPROVAL_NONE && task->approval_summary[0]) return task->approval_summary;
    if (task->step[0]) return task->step;
    static const char *steps[] = {"正在执行任务", "已发起审批请求", "本回合已结束", "本回合已中断", "请在电脑确认状态"};
    return steps[task->status];
}

static void codex_duration(const codex_task_t *task, char value[32]) {
    uint32_t seconds;
    if (!codex_monitor_elapsed(&s_state.codex, task, s_state.network.online, s_state.now_ms, &seconds)) {
        strcpy(value, "--");
        return;
    }
    if (seconds < 3600) snprintf(value, 32, "%02lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
    else snprintf(value, 32, "%lu:%02lu:%02lu", (unsigned long)(seconds / 3600),
                  (unsigned long)(seconds / 60 % 60), (unsigned long)(seconds % 60));
}

static void codex_stamp(lv_layer_t *layer, bool fresh) {
    char value[64];
    int64_t seconds = (s_state.now_ms - s_state.codex.received_ms) / 1000;
    if (seconds < 0) seconds = 0;
    if (fresh && seconds < 10) strcpy(value, "刚刚同步");
    else snprintf(value, sizeof(value), "%s %lu%s前", fresh ? "同步" : "上次",
        (unsigned long)(seconds < 60 ? seconds : seconds < 3600 ? seconds / 60 : seconds / 3600),
        seconds < 60 ? "秒" : seconds < 3600 ? "分" : "小时");
    small(layer, value, 20, 272, 137);
    for (unsigned i = 0; i < s_state.codex.data.count; i++)
        rectangle(layer, 162 + (int)i * 7, 277, 4, 4, i == s_state.navigation.codex_selection ? UI_ACCENT : UI_TRACK);
    snprintf(value, sizeof(value), "%u/%u", s_state.navigation.codex_selection + 1, s_state.codex.data.count);
    small(layer, value, 190, 272, 30);
}

static void codex_empty(lv_layer_t *layer) {
    const codex_monitor_state_t *state = &s_state.codex;
    const char *heading = state->failed ? "任务同步失败" : !state->available ? "等待任务同步"
        : !state->data.source_online ? "电脑端未连接" : "现在没有任务";
    label(layer, heading, 20, 139, 200, &workout_font_20, UI_INK);
    const char *body = !s_state.network.has_config ? "长按确定，进入网络配置"
        : !s_state.network.online ? "连接网络后自动同步"
        : state->failed ? "确定重试，检查后端接口"
        : state->available && !state->data.source_online ? "请在电脑启动任务采集器" : "在电脑上开始一个 Codex 任务";
    small(layer, body, 20, 183, 200);
    small(layer, "进展会显示在这里", 20, 205, 200);
    if (state->http_status && state->http_status != 200) {
        char value[40];
        snprintf(value, sizeof(value), "接口错误 HTTP %d", state->http_status);
        small(layer, value, 20, 247, 200);
    }
    hint(layer, state->failed || state->alerts_failed ? "确定 重试  长按确定 菜单" : "长按确定：页面菜单");
}

static void codex_page(lv_layer_t *layer) {
    title(layer, "Codex 任务");
    const codex_task_t *task = codex_task();
    if (!s_state.codex.available || !task) { codex_empty(layer); return; }
    bool fresh = codex_monitor_fresh(&s_state.codex, task, s_state.network.online, s_state.now_ms);
    char value[80];
    unsigned active = s_state.codex.data.active_count, pending = s_state.codex.data.pending_count;
    if (pending) snprintf(value, sizeof(value), "%s%u%s 个进行中 · %u%s 待确认", fresh ? "" : "上次 ",
        active > 99 ? 99 : active, active > 99 ? "+" : "", pending > 99 ? 99 : pending, pending > 99 ? "+" : "");
    else snprintf(value, sizeof(value), "%s%u%s 个进行中", fresh ? "" : "上次 ", active > 99 ? 99 : active, active > 99 ? "+" : "");
    codex_block(layer, value, 20, 66, 200, &s_codex_font_12, 1, UI_MUTED);
    bool approval = fresh && task->approval == CODEX_APPROVAL_REQUESTED;
    rectangle(layer, 20, 90, 200, 116, approval ? UI_AMBER_BG : UI_PANEL);
    if (!fresh) for (int x = 20; x < 220; x += 8) rectangle(layer, x, 90, 4, 1, UI_MUTED);
    codex_block(layer, task->project, 30, 100, 107, &s_codex_font_12, 1, UI_MUTED);
    codex_status_mark(layer, task, fresh, 144, 100);
    codex_block(layer, task->title[0] ? task->title : "Codex 任务", 30, 125, 180, &s_codex_font_16, 2, UI_INK);
    codex_block(layer, codex_step(task), 30, 182, 180, &s_codex_font_12, 1, UI_MUTED);
    const char *message = task->approval == CODEX_APPROVAL_UNKNOWN ? "审批结果未确认，请在电脑查看"
        : !fresh ? "最后状态 · 现在无法确认" : task->status == CODEX_TASK_RUNNING ? "已运行"
        : task->status == CODEX_TASK_APPROVAL ? "请求时用时" : "本回合用时";
    label(layer, message, 20, 214, 200, &workout_font_12, !fresh ? UI_AMBER : UI_MUTED);
    codex_duration(task, value);
    label(layer, value, 20, 233, 178, strlen(value) <= 7 && value[0] != '-' ? &workout_digits_35 : &workout_font_20, UI_INK);
    if (fresh && strlen(value) <= 5) for (unsigned i = 0; i < 12; i++) rectangle(layer, 190 + (int)(i % 4) * 7,
        244 + (int)(i / 4) * 7, 4, 4, i % 3 ? UI_ACCENT : UI_TRACK);
    codex_stamp(layer, fresh);
    hint(layer, s_state.codex.failed || s_state.codex.alerts_failed ? "上下 切换  确定 重试" : "上下 切换  确定 详情");
}

static void codex_details(lv_layer_t *layer) {
    const codex_task_t *task = codex_task();
    if (!task) { codex_page(layer); return; }
    title(layer, "任务详情");
    codex_block(layer, task->project, 20, 69, 200, &s_codex_font_12, 1, UI_MUTED);
    if (lv_obj_has_flag(s_codex_title_label, LV_OBJ_FLAG_HIDDEN))
        codex_block(layer, task->title[0] ? task->title : "Codex 任务", 20, 90, 200, &s_codex_font_16, 4, UI_INK);
    bool fresh = codex_monitor_fresh(&s_state.codex, task, s_state.network.online, s_state.now_ms);
    small(layer, "当前状态", 20, 181, 100);
    codex_status_mark(layer, task, fresh, 145, 181);
    rectangle(layer, 20, 202, 200, 1, UI_TRACK);
    small(layer, fresh ? "回合用时" : "记录用时", 20, 211, 100);
    char value[32];
    codex_duration(task, value);
    label(layer, value, 127, 211, 93, &workout_font_12, UI_INK);
    rectangle(layer, 20, 232, 200, 1, UI_TRACK);
    codex_block(layer, codex_step(task), 20, 242, 200, &s_codex_font_12, 2, UI_MUTED);
    if (!fresh) small(layer, "旧状态，请在电脑确认", 20, 277, 200);
    else if (s_state.codex.data.omitted_count) {
        snprintf(value, sizeof(value), "另有 %lu 个任务", (unsigned long)s_state.codex.data.omitted_count);
        small(layer, value, 20, 277, 200);
    }
    hint(layer, s_state.codex.failed || s_state.codex.alerts_failed ? "上下 切换  确定 重试" : "上下 切换  确定 返回");
}

static void codex_popup(lv_layer_t *layer) {
    lv_draw_rect_dsc_t shade;
    lv_draw_rect_dsc_init(&shade);
    shade.bg_color = lv_color_hex(UI_INK);
    shade.bg_opa = 90;
    const lv_area_t area = {.x1 = 0, .y1 = 0, .x2 = 239, .y2 = 319};
    lv_draw_rect(layer, &shade, &area);
    rectangle(layer, 15, 40, 210, 244, UI_AMBER_LINE);
    rectangle(layer, 16, 41, 208, 242, UI_BACKGROUND);
    label(layer, "需要你处理", 30, 56, 180, &workout_font_12, UI_AMBER);
    label(layer, "审批请求", 30, 80, 180, &workout_font_20, UI_INK);
    codex_block(layer, s_state.codex_alert.title[0] ? s_state.codex_alert.title : "Codex 任务",
                30, 114, 180, &s_codex_font_16, 2, UI_INK);
    codex_block(layer, s_state.codex_alert.summary, 30, 164, 180, &s_codex_font_12, 2, UI_MUTED);
    rectangle(layer, 30, 212, 180, 30, UI_AMBER_BG);
    rectangle(layer, 30, 212, 3, 30, UI_AMBER_LINE);
    label(layer, "请在电脑上的 Codex 处理", 39, 220, 169, &workout_font_12, UI_AMBER);
    rectangle(layer, 30, 252, 180, 1, UI_TRACK);
    small(layer, "确定：关闭提示", 30, 263, 114);
    small(layer, "不代表批准", 150, 263, 65);
}

static const char *setup_message(void) {
    switch (s_state.network.setup_result) {
        case WORKOUT_SETUP_CONNECTING: return "正在连接路由器";
        case WORKOUT_SETUP_SAVED: return "配置已保存，正在同步";
        case WORKOUT_SETUP_BAD_WIFI: return "连接失败，请检查密码";
        case WORKOUT_SETUP_STORAGE: return s_state.network.error == WORKOUT_NET_STORAGE
                                          ? "存储损坏，长按下键清除" : "保存失败，请重试";
        case WORKOUT_SETUP_FAILED: return "热点启动失败，请重试";
        case WORKOUT_SETUP_EXPIRED: return "配网已超时，请重试";
        default: return "确定开始配网";
    }
}

static void setup(lv_layer_t *layer) {
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

static void profile_hint(lv_layer_t *layer, const char *value) {
    rectangle(layer, 20, 280, 200, 1, UI_TRACK);
    small(layer, value, 24, 285, 192);
    small(layer, "长按确定 返回", 24, 302, 192);
}

static void display_name(const char *input, char *output, size_t size) {
    size_t index = 0, used = 0;
    while (input[index] && used + 16 < size) {
        size_t start = index;
        uint32_t code = workout_text_next(input, &index);
        lv_font_glyph_dsc_t glyph = {0};
        if (code >= 32 && code != 127 && code != 0xFFFD
            && lv_font_get_glyph_dsc(&workout_network_font_16, &glyph, code, 0) && !glyph.is_placeholder) {
            memcpy(output + used, input + start, index - start);
            used += index - start;
        } else used += (size_t)snprintf(output + used, size - used, "[U+%04lX]", (unsigned long)code);
    }
    output[used] = '\0';
}

static void short_name(const char *input, char output[384], int width) {
    display_name(input, output, 384);
    lv_point_t extent;
    lv_text_get_size(&extent, output, &workout_network_font_16, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (extent.x <= width) return;
    size_t length = strlen(output);
    do {
        do { length--; } while (length && ((unsigned char)output[length] & 0xC0) == 0x80);
        strcpy(output + length, "...");
        lv_text_get_size(&extent, output, &workout_network_font_16, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    } while (length && extent.x > width);
}

static void profile_row(lv_layer_t *layer, int y, const char *name, const char *meta,
                        bool selected, bool current, bool dynamic) {
    uint32_t color = selected ? UI_BACKGROUND : UI_INK;
    rectangle(layer, 20, y, 200, 45, selected ? UI_ACCENT : UI_PANEL);
    char text[384];
    if (dynamic) short_name(name, text, current ? 145 : 180);
    label(layer, dynamic ? text : name, 30, dynamic ? y : y + 3, current ? 145 : 180,
          dynamic ? &workout_network_font_16 : &workout_font_16, color);
    label(layer, meta, 30, y + (dynamic ? 31 : 27), 180, &workout_font_12, selected ? UI_BACKGROUND : UI_MUTED);
    if (current) label(layer, "当前", 184, y + 6, 30, &workout_font_12, color);
}

static const char *profile_message(void) {
    if (s_state.request_failed) return "设备忙，请稍后重试";
    switch (s_state.network.switch_result) {
        case WORKOUT_SWITCH_CONNECTING: return "正在切换，请稍候";
        case WORKOUT_SWITCH_SAVED: return "切换已保存";
        case WORKOUT_SWITCH_FAILED: return s_state.network.online ? "切换失败，已恢复原网络" : "切换失败，正在恢复连接";
        case WORKOUT_SWITCH_STORAGE: return "保存失败，请重试";
        default: return "断线后自动尝试已保存 Wi-Fi";
    }
}

static void network(lv_layer_t *layer) {
    title(layer, "网络与接口");
    if (!s_state.network.has_config) small(layer, "尚未保存网络配置", 20, 65, 200);
    char count[48];
    snprintf(count, sizeof(count), "已保存 %u 个网络", s_state.network.wifi_count);
    profile_row(layer, 94, "切换 Wi-Fi", count, s_state.navigation.selection == 0, false, false);
    snprintf(count, sizeof(count), "已保存 %u 个地址", s_state.network.server_count);
    profile_row(layer, 145, "切换服务器", count, s_state.navigation.selection == 1, false, false);
    profile_row(layer, 196, "添加配置", "手机扫码配置并保存", s_state.navigation.selection == 2, false, false);
    const char *message = s_state.network.error == WORKOUT_NET_STORAGE ? "存储不可用，请清除或重试"
                        : s_state.network.exhausted ? "没有可连接的网络，稍后重试" : profile_message();
    small(layer, message, 20, 251, 200);
    profile_hint(layer, "上下 选择  确定 进入");
}

static void profiles(lv_layer_t *layer) {
    bool wifi = s_state.navigation.view == WORKOUT_VIEW_WIFI;
    unsigned count = wifi ? s_state.network.wifi_count : s_state.network.server_count;
    bool failed = !wifi && s_state.network.error == WORKOUT_NET_HTTP;
    title(layer, wifi ? "切换 Wi-Fi" : failed ? "服务器未响应" : "切换服务器");
    if (!count) {
        small(layer, wifi ? "尚未保存 Wi-Fi" : "尚未保存服务器", 20, 65, 200);
        label(layer, "确定添加第一组配置", 20, 132, 200, &workout_font_16, UI_INK);
        small(layer, "成功后会保留配置历史", 20, 180, 200);
        profile_hint(layer, "确定 添加配置");
        return;
    }
    unsigned selection = s_state.navigation.selection % count;
    char value[48];
    snprintf(value, sizeof(value), "已保存%s  %u / %u", wifi ? "网络" : "地址", selection + 1, count);
    const char *message = s_state.request_failed || s_state.network.switch_result == WORKOUT_SWITCH_STORAGE
                        ? profile_message() : failed ? "上下选择其他地址，确定切换" : value;
    small(layer, message, 20, 65, 205);
    unsigned first = selection / 3 * 3;
    for (unsigned i = first; i < count && i < first + 3; i++) {
        bool current = i == (wifi ? s_state.network.active_wifi : s_state.network.active_server);
        const char *name = wifi ? s_state.network.wifi_names[i] : s_state.network.servers[i];
        if (!wifi) name = strchr(name, ':') + 3;
        const char *meta = current ? wifi && s_state.network.online ? "当前已连接"
                         : failed ? "确定重试当前服务器" : "当前选择"
                         : i == selection ? "确定后尝试切换" : "已保存";
        profile_row(layer, 94 + (int)(i - first) * 51, name, meta, i == selection, current, true);
    }
    profile_hint(layer, s_state.network.profile_busy ? "切换中，请稍候" : "上下 选择  确定 切换");
}

static void connection(lv_layer_t *layer) {
    const workout_network_status_t *status = &s_state.network;
    title(layer, status->profile_busy ? "正在切换 Wi-Fi" : status->online ? "已连接 Wi-Fi"
               : status->connecting ? "正在自动换网" : "暂时没有网络");
    const char *message = status->exhausted ? "已尝试全部保存的 Wi-Fi" : profile_message();
    small(layer, message, 20, 65, 205);
    rectangle(layer, 20, 96, 200, 75, UI_PANEL);
    small(layer, status->online ? "当前网络" : status->connecting ? "正在连接" : "保留已保存配置和缓存", 30, 102, 180);
    char value[64];
    snprintf(value, sizeof(value), "第 %u 个 / 共 %u 个网络", status->attempted, status->wifi_count);
    small(layer, status->connecting ? value : status->online ? "连接成功，正在同步" : "稍后会自动重试", 30, 150, 180);
    profile_row(layer, 180, status->online ? "立即同步" : "重新连接", "尝试当前配置", s_state.navigation.selection == 0, false, false);
    profile_row(layer, 231, "添加配置", "手机扫码配置并保存", s_state.navigation.selection == 1, false, false);
    profile_hint(layer, "上下 选择  确定 执行");
}

static void draw(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    header(layer);
    switch (s_state.navigation.view) {
        case WORKOUT_VIEW_DASHBOARD: dashboard(layer); break;
        case WORKOUT_VIEW_DETAILS: details(layer); break;
        case WORKOUT_VIEW_MENU: menu(layer); break;
        case WORKOUT_VIEW_NETWORK: network(layer); break;
        case WORKOUT_VIEW_SETUP: setup(layer); break;
        case WORKOUT_VIEW_WIFI: case WORKOUT_VIEW_SERVER: profiles(layer); break;
        case WORKOUT_VIEW_CONNECTION: connection(layer); break;
        case WORKOUT_VIEW_AI: quota_page(layer); break;
        case WORKOUT_VIEW_CODEX: codex_page(layer); break;
        case WORKOUT_VIEW_CODEX_DETAILS: codex_details(layer); break;
        case WORKOUT_VIEW_CLEAR:
            title(layer, "清除网络设置");
            label(layer, "清除全部网络与服务器？", 20, 134, 205, &workout_font_16, UI_INK);
            small(layer, "保留运动和 AI 缓存", 20, 181, 205);
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
    if (s_state.codex_popup) codex_popup(layer);
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
    s_codex_font_12 = workout_font_12;
    s_codex_font_12.fallback = &workout_monitor_font_12;
    s_codex_font_16 = workout_font_16;
    s_codex_font_16.fallback = &workout_network_font_16;
    if (!font_coverage()) return false;
    s_screen = lv_obj_create(NULL);
    if (!s_screen) return false;
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(UI_BACKGROUND), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_add_event_cb(s_screen, draw, LV_EVENT_DRAW_MAIN, NULL);
    s_profile_label = lv_label_create(s_screen);
    if (!s_profile_label) { lv_obj_delete(s_screen); s_screen = NULL; return false; }
    lv_obj_set_style_text_font(s_profile_label, &workout_network_font_16, 0);
    lv_obj_set_style_text_color(s_profile_label, lv_color_hex(UI_MUTED), 0);
    lv_label_set_long_mode(s_profile_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_label_set_text(s_profile_label, "");
    lv_obj_add_flag(s_profile_label, LV_OBJ_FLAG_HIDDEN);
    s_codex_title_label = lv_label_create(s_screen);
    if (!s_codex_title_label) { lv_obj_delete(s_screen); s_screen = s_profile_label = NULL; return false; }
    lv_obj_set_style_text_font(s_codex_title_label, &s_codex_font_16, 0);
    lv_obj_set_style_text_color(s_codex_title_label, lv_color_hex(UI_INK), 0);
    lv_label_set_long_mode(s_codex_title_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_pos(s_codex_title_label, 20, 105);
    lv_obj_set_size(s_codex_title_label, 200, s_codex_font_16.line_height);
    lv_label_set_text(s_codex_title_label, "");
    lv_obj_add_flag(s_codex_title_label, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(s_screen);
    return true;
}

static void profile_label_update(void) {
    if (s_state.codex_popup) { lv_obj_add_flag(s_profile_label, LV_OBJ_FLAG_HIDDEN); return; }
    workout_view_t view = s_state.navigation.view;
    const char *name = NULL;
    char value[256];
    int x = 20, y = 244, width = 200;
    if (view == WORKOUT_VIEW_WIFI && s_state.network.wifi_count)
        name = s_state.network.wifi_names[s_state.navigation.selection % s_state.network.wifi_count];
    if (view == WORKOUT_VIEW_SERVER && s_state.network.server_count)
        name = s_state.network.servers[s_state.navigation.selection % s_state.network.server_count];
    unsigned index = view == WORKOUT_VIEW_CONNECTION && (s_state.network.connecting || s_state.network.profile_busy)
                   ? s_state.network.connecting_wifi : s_state.network.active_wifi;
    if (view == WORKOUT_VIEW_NETWORK && index < s_state.network.wifi_count) {
        snprintf(value, sizeof(value), "%s %s", s_state.network.online ? "已连接" : "离线",
                 s_state.network.wifi_names[index]);
        name = value;
        y = 58;
    }
    if (view == WORKOUT_VIEW_CONNECTION && index < s_state.network.wifi_count) {
        name = s_state.network.wifi_names[index];
        x = 30; y = 117; width = 180;
    }
    if (!name) { lv_obj_add_flag(s_profile_label, LV_OBJ_FLAG_HIDDEN); return; }
    char safe[384];
    display_name(name, safe, sizeof(safe));
    if (strcmp(safe, s_profile_text) != 0) {
        strcpy(s_profile_text, safe);
        lv_label_set_text(s_profile_label, s_profile_text);
    }
    lv_obj_set_pos(s_profile_label, x, y);
    lv_obj_set_size(s_profile_label, width, workout_network_font_16.line_height);
    lv_obj_remove_flag(s_profile_label, LV_OBJ_FLAG_HIDDEN);
}

static void qr_update(void) {
    bool show = !s_state.codex_popup && s_state.navigation.view == WORKOUT_VIEW_SETUP && s_state.network.setup_active;
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

static void codex_title_update(void) {
    const codex_task_t *task = codex_task();
    char text[384] = {0};
    bool show = !s_state.codex_popup && s_state.navigation.view == WORKOUT_VIEW_CODEX_DETAILS && task;
    if (show) {
        codex_safe_text(task->title[0] ? task->title : "Codex 任务", text, &s_codex_font_16);
        lv_point_t extent;
        lv_text_get_size(&extent, text, &s_codex_font_16, 0, 2, 200, LV_TEXT_FLAG_NONE);
        show = extent.y > 86;
    }
    if (!show) text[0] = '\0';
    if (strcmp(text, s_codex_title_text) != 0) {
        strcpy(s_codex_title_text, text);
        lv_label_set_text(s_codex_title_label, s_codex_title_text);
    }
    if (show) lv_obj_remove_flag(s_codex_title_label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_codex_title_label, LV_OBJ_FLAG_HIDDEN);
}

void workout_ui_update(const workout_ui_state_t *state) {
    s_state = *state;
    qr_update();
    profile_label_update();
    codex_title_update();
    lv_obj_invalidate(s_screen);
}
