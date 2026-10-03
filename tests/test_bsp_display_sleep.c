/* Verify reversible panel commands without ever releasing/holding its pins. */
#include <assert.h>
#include <stdio.h>
#include "../components/bsp/src/bsp_display_sleep.c"

static int s_panel, s_step, s_failure;
static bool s_present = true;
esp_lcd_panel_handle_t bsp_display_panel(void) { return s_present ? &s_panel : NULL; }
void bsp_display_backlight(uint8_t percent) { assert(percent == 0 && s_step++ == 0); }
esp_err_t ledc_stop(int mode, int channel, uint32_t idle) {
    assert(mode == BSP_BL_LEDC_MODE && channel == BSP_BL_LEDC_CHANNEL && idle == 0);
    assert(s_step++ == 1);
    return s_failure == 1 ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t panel, bool on) {
    assert(panel == &s_panel && s_step++ == (on ? 1 : 2));
    return s_failure == (on ? 5 : 2) ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_lcd_panel_disp_sleep(esp_lcd_panel_handle_t panel, bool sleep) {
    assert(panel == &s_panel && s_step++ == (sleep ? 3 : 0));
    return s_failure == (sleep ? 3 : 4) ? ESP_FAIL : ESP_OK;
}
int main(void) {
    s_present = false;
    assert(bsp_display_sleep() == ESP_ERR_INVALID_STATE && bsp_display_wake() == ESP_ERR_INVALID_STATE);
    s_present = true;
    for (s_failure = 0; s_failure <= 3; s_failure++) {
        s_step = 0;
        assert(bsp_display_sleep() == (s_failure ? ESP_FAIL : ESP_OK));
    }
    for (s_failure = 4; s_failure <= 5; s_failure++) {
        s_step = 0; assert(bsp_display_wake() == ESP_FAIL);
        int failed = s_failure; s_failure = 0;
        s_step = 0; assert(bsp_display_wake() == ESP_OK && s_step == 2);
        s_failure = failed;
    }
    puts("BSP reversible LCD sleep/wake failures and retry: PASS");
    return 0;
}
