#pragma once
#include <stdint.h>
#include "esp_err.h"
#define LEDC_TIMER_0 0
#define LEDC_CHANNEL_0 0
#define LEDC_LOW_SPEED_MODE 0
esp_err_t ledc_stop(int, int, uint32_t);
