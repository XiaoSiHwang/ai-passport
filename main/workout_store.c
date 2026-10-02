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
    esp_err_t err = read_blob("network", config, sizeof(*config));
    if (err == ESP_OK && !workout_config_valid(config)) err = ESP_ERR_INVALID_ARG;
    if (err != ESP_OK) memset(config, 0, sizeof(*config));
    return err;
}

esp_err_t workout_store_save_config(const workout_config_t *config) {
    if (!workout_config_valid(config)) return ESP_ERR_INVALID_ARG;
    return write_blob("network", config, sizeof(*config));
}

esp_err_t workout_store_clear_config(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("workout", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(handle, "network");
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
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
