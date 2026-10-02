#pragma once
#include "network_test_stubs.h"

typedef enum { BSP_BTN_UP, BSP_BTN_DOWN, BSP_BTN_OK } bsp_btn_t;
typedef enum { BSP_BTN_CLICK, BSP_BTN_LONG } bsp_btn_ev_t;
typedef struct { int max_freq_mhz, min_freq_mhz; bool light_sleep_enable; } esp_pm_config_t;
esp_err_t esp_pm_configure(const esp_pm_config_t *config);
esp_err_t bsp_i2c_init(void);
esp_err_t bsp_display_init(void);
bool bsp_lvgl_init(void);
bool bsp_lvgl_lock(int timeout);
void bsp_lvgl_unlock(void);
void bsp_display_backlight(uint8_t percent);
esp_err_t bsp_battery_init(void);
int bsp_battery_soc(void);
esp_err_t bsp_button_init(void (*callback)(bsp_btn_t, bsp_btn_ev_t, void *), void *user);
