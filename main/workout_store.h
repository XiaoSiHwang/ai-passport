#pragma once

#include "workout_model.h"
#include "workout_profiles.h"
#include "ai_quota.h"
#include "ai_tokens.h"
#include "esp_err.h"

esp_err_t workout_store_init(void);
esp_err_t workout_store_load_config(workout_config_t *config);
esp_err_t workout_store_save_config(const workout_config_t *config);
esp_err_t workout_store_load_profiles(workout_profiles_t *profiles);
esp_err_t workout_store_save_profiles(const workout_profiles_t *profiles);
esp_err_t workout_store_clear_config(void);
esp_err_t workout_store_load_cache(workout_cache_t *cache);
esp_err_t workout_store_save_cache(const workout_cache_t *cache);
esp_err_t workout_store_load_quota(unsigned provider, ai_quota_cache_t *cache);
esp_err_t workout_store_save_quota(unsigned provider, const ai_quota_cache_t *cache);
esp_err_t workout_store_load_tokens(unsigned provider, ai_tokens_cache_t *cache);
esp_err_t workout_store_save_tokens(unsigned provider, const ai_tokens_cache_t *cache);
