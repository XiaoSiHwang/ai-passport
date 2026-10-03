// No LVGL task may see the display until the rounding callback is registered.
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
static void *test_calloc(size_t, size_t);
#define calloc test_calloc
#include "../components/bsp/src/bsp_display_lvgl.c"
#undef calloc

static lv_display_t display;
static int panel_present = 1, lock_depth, port_live, display_live, callback_live;
static int fail_lock, fail_port, fail_display, fail_event, init_calls, unlocked_flushes;
static int panel_token, io_token;
static lv_timer_t timers[3] = {{false}, {true}, {false}};
static bool handler_enabled = true, tick_running = true;
static int fail_alloc, fail_stop, fail_sleep, fail_wake, fail_resume, fail_flush;
static unsigned sleeps, wakes, redraws, tick_ms;
static int64_t clock_us;
static void *test_calloc(size_t count, size_t size) { return fail_alloc ? NULL : calloc(count, size); }
int64_t esp_timer_get_time(void) { return clock_us; }
lv_timer_t *lv_timer_get_next(lv_timer_t *timer) {
    assert(lock_depth);
    return !timer ? timers : timer == &timers[2] ? NULL : timer + 1;
}
bool lv_timer_get_paused(lv_timer_t *timer) { assert(lock_depth); return timer->paused; }
void lv_timer_pause(lv_timer_t *timer) { assert(lock_depth); timer->paused = true; }
void lv_timer_resume(lv_timer_t *timer) { assert(lock_depth); timer->paused = false; }
void lv_timer_enable(bool enabled) { assert(lock_depth); handler_enabled = enabled; }
void lv_tick_inc(uint32_t elapsed) { assert(!tick_running && lock_depth); tick_ms += elapsed; }
esp_err_t lvgl_port_stop(void) {
    assert(lock_depth); handler_enabled = false;
    if (fail_stop) return ESP_FAIL;
    tick_running = false; return ESP_OK;
}
esp_err_t lvgl_port_resume(void) {
    assert(lock_depth); handler_enabled = true;
    if (fail_resume) return ESP_FAIL;
    tick_running = true; return ESP_OK;
}
esp_err_t bsp_display_sleep(void) {
    assert(lock_depth && !tick_running && handler_enabled);
    for (unsigned i = 0; i < 3; i++) assert(timers[i].paused);
    sleeps++; return fail_sleep ? ESP_FAIL : ESP_OK;
}
esp_err_t bsp_display_wake(void) { assert(lock_depth && !tick_running); wakes++; return fail_wake ? ESP_FAIL : ESP_OK; }
void lv_refr_now(lv_display_t *disp) { assert(disp == &display && lock_depth && !s_suspended); redraws++; }
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int command, const void *param, size_t length) {
    assert(lock_depth && io == &io_token && command == -1 && !param && !length && redraws);
    return fail_flush ? ESP_FAIL : ESP_OK;
}
esp_lcd_panel_handle_t bsp_display_panel(void) { return panel_present ? &panel_token : NULL; }
esp_lcd_panel_io_handle_t bsp_display_io(void) { return &io_token; }
esp_err_t lvgl_port_init(const lvgl_port_cfg_t *cfg) {
    (void)cfg; ++init_calls; assert(!port_live);
    if (fail_port) return ESP_ERR_NO_MEM;
    port_live = 1; return ESP_OK;
}
esp_err_t lvgl_port_deinit(void) {
    // The real API is asynchronous: returning does not release the context.
    assert(false && "Display rollback must not deinit/reinitialize the live port");
    return ESP_OK;
}
bool lvgl_port_lock(uint32_t timeout) {
    (void)timeout; assert(port_live);
    if (fail_lock) return false;
    ++lock_depth; return true;
}
void lvgl_port_unlock(void) {
    assert(lock_depth > 0);
    --lock_depth;
    if (!lock_depth && display_live) {
        assert(callback_live); // Simulate rendering as soon as the lock is free.
        ++unlocked_flushes;
    }
}
lv_display_t *lvgl_port_add_disp(const lvgl_port_display_cfg_t *cfg) {
    assert(lock_depth > 0 && cfg->panel_handle == &panel_token && !display_live);
    assert(lvgl_port_lock(0)); // Real port takes and releases a recursive lock.
    if (!fail_display) display_live = 1;
    lvgl_port_unlock();
    return fail_display ? NULL : &display;
}
esp_err_t lvgl_port_remove_disp(lv_display_t *disp) {
    assert(disp == &display && lock_depth && display_live);
    display_live = callback_live = 0; return ESP_OK;
}
void lv_display_add_event_cb(lv_display_t *disp, void (*cb)(lv_event_t *), int code, void *user) {
    (void)user;
    assert(lock_depth && disp == &display && code == LV_EVENT_FLUSH_START && cb == rounded_flush_event);
    if (!fail_event) callback_live = 1;
}
uint32_t lv_display_get_event_count(lv_display_t *disp) {
    assert(disp == &display && lock_depth);
    return 1 + callback_live; // The display already owns an internal callback.
}
static void expect_failure(void) {
    assert(bsp_lvgl_init() == NULL);
    assert(!s_disp && !lock_depth && !display_live);
    assert(!bsp_lvgl_lock(0));
}
static void standby(void) {
    fail_lock = 1; assert(bsp_lvgl_sleep() == ESP_ERR_TIMEOUT); fail_lock = 0;
    fail_alloc = 1; assert(bsp_lvgl_sleep() == ESP_ERR_NO_MEM); fail_alloc = 0;
    assert(!s_suspended && !s_paused_count && !timers[0].paused && timers[1].paused);
    fail_stop = 1; assert(bsp_lvgl_sleep() == ESP_FAIL); fail_stop = 0;
    assert(handler_enabled && tick_running && !s_suspended && !s_paused_timers);
    clock_us = 1000000; fail_sleep = 1; assert(bsp_lvgl_sleep() == ESP_FAIL);
    assert(s_suspended && !s_sleeping && !tick_running);
    fail_sleep = 0; assert(bsp_lvgl_sleep() == ESP_OK && s_sleeping);
    unsigned before = sleeps; assert(bsp_lvgl_sleep() == ESP_OK && sleeps == before);
    assert(bsp_lvgl_refresh() == ESP_ERR_INVALID_STATE);
    fail_wake = 1; assert(bsp_lvgl_wake() == ESP_FAIL && s_suspended); fail_wake = 0;
    clock_us = 61000000; fail_resume = 1;
    assert(bsp_lvgl_wake() == ESP_FAIL && s_suspended && handler_enabled && tick_ms == 60000);
    clock_us += 1000000; fail_resume = 0;
    assert(bsp_lvgl_wake() == ESP_OK && !s_suspended && !s_sleeping && tick_ms == 61000);
    assert(tick_running && !s_paused_timers && !timers[0].paused && timers[1].paused && !timers[2].paused);
    before = wakes; assert(bsp_lvgl_wake() == ESP_OK && wakes == before);
    fail_flush = 1; assert(bsp_lvgl_refresh() == ESP_FAIL); fail_flush = 0;
    assert(bsp_lvgl_refresh() == ESP_OK && redraws == 2 && !lock_depth);
}
int main(void) {
    panel_present = 0; expect_failure(); panel_present = 1;
    fail_port = 1; expect_failure(); fail_port = 0;
    const int failed_init_calls = init_calls;
    expect_failure(); // A partially initialized port cannot safely be re-created.
    assert(init_calls == failed_init_calls);
    s_port_init_failed = false; // Simulate reboot for remaining scenarios.
    fail_lock = 1; expect_failure(); fail_lock = 0;
    const int retained_init_calls = init_calls;
    fail_display = 1; expect_failure(); fail_display = 0;
    fail_event = 1; expect_failure(); fail_event = 0;
    assert(bsp_lvgl_init() == &display);
    assert(init_calls == retained_init_calls); // Display retries reuse the port.
    assert(callback_live && !lock_depth && unlocked_flushes == 1);
    const int before = init_calls;
    assert(bsp_lvgl_init() == &display && init_calls == before);
    assert(bsp_lvgl_lock(5)); bsp_lvgl_unlock();
    uint16_t pixels[BSP_LCD_W] = {0};
    for (int x = 0; x < BSP_LCD_W; ++x) pixels[x] = 0xffff;
    display.buffer = (lv_draw_buf_t){ .data = (uint8_t *)pixels, .header.stride = sizeof(pixels) };
    lv_area_t area = { .x1 = 0, .y1 = 0, .x2 = BSP_LCD_W - 1, .y2 = 0 };
    lv_event_t ev = { .target = &display, .area = &area };
    rounded_flush_event(&ev);
    assert(pixels[0] == 0 && pixels[BSP_LCD_W - 1] == 0 && pixels[BSP_LCD_W / 2] == 0xffff);
    standby();
    puts("BSP LVGL initialization tests: PASS");
}
