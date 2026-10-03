#include "workout_store.h"

#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

/* One blob per transaction preserves the previous committed value after power loss. */
static esp_err_t read_blob(const char *key, void *output, size_t size) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("workout", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    size_t actual = size;
    err = nvs_get_blob(handle, key, output, &actual);
    nvs_close(handle);
    if (err == ESP_OK && actual != size) return ESP_ERR_INVALID_SIZE;
    return err;
}

static esp_err_t write_blob(const char *key, const void *value, size_t size) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("workout", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, key, value, size);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t workout_store_init(void) {
    /* Never erase NVS as an automatic response to an initialization error. */
    return nvs_flash_init();
}

esp_err_t workout_store_load_config(workout_config_t *config) {
    workout_profiles_t profiles;
    esp_err_t err = workout_store_load_profiles(&profiles);
    if (err == ESP_OK && !workout_profiles_config(&profiles, config)) err = ESP_ERR_INVALID_ARG;
    if (err != ESP_OK) memset(config, 0, sizeof(*config));
    return err;
}

esp_err_t workout_store_save_config(const workout_config_t *config) {
    if (!workout_config_valid(config)) return ESP_ERR_INVALID_ARG;
    workout_profiles_t profiles;
    esp_err_t err = workout_store_load_profiles(&profiles);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    if (!workout_profiles_add(&profiles, config)) return ESP_ERR_INVALID_ARG;
    return workout_store_save_profiles(&profiles);
}

esp_err_t workout_store_load_profiles(workout_profiles_t *profiles) {
    esp_err_t err = read_blob("profiles", profiles, sizeof(*profiles));
    if (err == ESP_OK && !workout_profiles_valid(profiles)) err = ESP_ERR_INVALID_CRC;
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Only a missing new key permits legacy migration; corruption is not an empty history. */
        workout_config_t legacy;
        err = read_blob("network", &legacy, sizeof(legacy));
        memset(profiles, 0, sizeof(*profiles));
        if (err == ESP_OK && !workout_profiles_add(profiles, &legacy)) err = ESP_ERR_INVALID_ARG;
        memset(&legacy, 0, sizeof(legacy));
    }
    if (err != ESP_OK) memset(profiles, 0, sizeof(*profiles));
    return err;
}

esp_err_t workout_store_save_profiles(const workout_profiles_t *profiles) {
    if (!workout_profiles_valid(profiles)) return ESP_ERR_INVALID_ARG;
    return write_blob("profiles", profiles, sizeof(*profiles));
}

esp_err_t workout_store_clear_config(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("workout", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(handle, "network");
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) {
        err = nvs_erase_key(handle, "profiles");
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t workout_store_load_cache(workout_cache_t *cache) {
    esp_err_t err = read_blob("snapshot", cache, sizeof(*cache));
    if (err == ESP_OK && !workout_cache_valid(cache, sizeof(*cache))) err = ESP_ERR_INVALID_CRC;
    if (err != ESP_OK) memset(cache, 0, sizeof(*cache));
    return err;
}

esp_err_t workout_store_save_cache(const workout_cache_t *cache) {
    if (!workout_cache_valid(cache, sizeof(*cache))) return ESP_ERR_INVALID_ARG;
    return write_blob("snapshot", cache, sizeof(*cache));
}

esp_err_t workout_store_load_quota(unsigned provider, ai_quota_cache_t *cache) {
    if (provider >= AI_QUOTA_PROVIDERS) return ESP_ERR_INVALID_ARG;
    esp_err_t err = read_blob(provider ? "glm" : "codex", cache, sizeof(*cache));
    if (err == ESP_OK && !ai_quota_cache_valid(cache, sizeof(*cache), provider)) err = ESP_ERR_INVALID_CRC;
    if (err != ESP_OK) memset(cache, 0, sizeof(*cache));
    return err;
}

esp_err_t workout_store_save_quota(unsigned provider, const ai_quota_cache_t *cache) {
    if (!ai_quota_cache_valid(cache, sizeof(*cache), provider)) return ESP_ERR_INVALID_ARG;
    return write_blob(provider ? "glm" : "codex", cache, sizeof(*cache));
}

esp_err_t workout_store_load_tokens(unsigned provider, ai_tokens_cache_t *cache) {
    if (provider >= AI_QUOTA_PROVIDERS) return ESP_ERR_INVALID_ARG;
    esp_err_t err = read_blob(provider ? "glm_tokens" : "codex_tokens", cache, sizeof(*cache));
    if (err == ESP_OK && !ai_tokens_cache_valid(cache, sizeof(*cache), provider)) err = ESP_ERR_INVALID_CRC;
    if (err != ESP_OK) memset(cache, 0, sizeof(*cache));
    return err;
}

esp_err_t workout_store_save_tokens(unsigned provider, const ai_tokens_cache_t *cache) {
    if (!ai_tokens_cache_valid(cache, sizeof(*cache), provider)) return ESP_ERR_INVALID_ARG;
    return write_blob(provider ? "glm_tokens" : "codex_tokens", cache, sizeof(*cache));
}
