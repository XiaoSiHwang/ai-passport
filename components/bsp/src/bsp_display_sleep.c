#include "bsp_display.h"
#include "bsp_pins.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_ops.h"

esp_err_t bsp_display_sleep(void) {
    esp_lcd_panel_handle_t panel = bsp_display_panel();
    if (!panel) return ESP_ERR_INVALID_STATE;
    bsp_display_backlight(0);
    esp_err_t err = ledc_stop(BSP_BL_LEDC_MODE, BSP_BL_LEDC_CHANNEL, 0);
    if (err != ESP_OK) return err;
    // IDF's SPI parameter transaction drains queued color transfers before
    // sending DISPOFF. The owner must prevent any new LVGL flush meanwhile.
    err = esp_lcd_panel_disp_on_off(panel, false);
    if (err == ESP_OK) err = esp_lcd_panel_disp_sleep(panel, true);
    return err;
}

esp_err_t bsp_display_wake(void) {
    esp_lcd_panel_handle_t panel = bsp_display_panel();
    if (!panel) return ESP_ERR_INVALID_STATE;
    // The ST7789 driver waits for Sleep Out before accepting Display On.
    // LEDC duty/update will restart its output when the application lights it.
    esp_err_t err = esp_lcd_panel_disp_sleep(panel, false);
    if (err == ESP_OK) err = esp_lcd_panel_disp_on_off(panel, true);
    return err;
}
