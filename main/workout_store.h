#pragma once

#include "workout_model.h"
#include "ai_quota.h"
#include "esp_err.h"

esp_err_t workout_store_init(void);
esp_err_t workout_store_load_config(workout_config_t *config);
esp_err_t workout_store_save_config(const workout_config_t *config);
esp_err_t workout_store_clear_config(void);
esp_err_t workout_store_load_cache(workout_cache_t *cache);
esp_err_t workout_store_save_cache(const workout_cache_t *cache);
esp_err_t workout_store_load_quota(unsigned provider, ai_quota_cache_t *cache);
esp_err_t workout_store_save_quota(unsigned provider, const ai_quota_cache_t *cache);
