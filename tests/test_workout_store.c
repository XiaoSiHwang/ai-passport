#include "workout_store.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char s_values[2][512], s_pending[2][512];
static size_t s_sizes[2], s_pending_sizes[2];
static bool s_fail_commit;
static unsigned s_open, s_close;

static unsigned slot(const char *key) { return strcmp(key, "network") == 0 ? 0 : 1; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    assert(strcmp(name, "workout") == 0);
    (void)mode;
    *handle = 1; s_open++;
    memcpy(s_pending, s_values, sizeof(s_values));
    memcpy(s_pending_sizes, s_sizes, sizeof(s_sizes));
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle == 1); s_close++; }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *output, size_t *size) {
    assert(handle == 1);
    unsigned index = slot(key);
    if (!s_sizes[index]) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < s_sizes[index]) return ESP_ERR_INVALID_SIZE;
    *size = s_sizes[index]; memcpy(output, s_values[index], *size);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size) {
    assert(handle == 1 && size <= 512);
    unsigned index = slot(key);
    s_pending_sizes[index] = size;
    memcpy(s_pending[index], value, size);
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    assert(handle == 1 && strcmp(key, "network") == 0);
    s_pending_sizes[0] = 0;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    assert(handle == 1);
    if (s_fail_commit) return ESP_FAIL;
    memcpy(s_values, s_pending, sizeof(s_values));
    memcpy(s_sizes, s_pending_sizes, sizeof(s_sizes));
    return ESP_OK;
}

int main(void) {
    workout_config_t config = {.ssid = "Example", .server = "http://example.com/api/workout"}, loaded;
    assert(workout_store_init() == ESP_OK);
    assert(workout_store_save_config(&config) == ESP_OK);
    strcpy(config.ssid, "New network");
    s_fail_commit = true;
    assert(workout_store_save_config(&config) == ESP_FAIL);
    assert(workout_store_load_config(&loaded) == ESP_OK && strcmp(loaded.ssid, "Example") == 0);
    s_fail_commit = false;
    workout_data_t data = {.year = 2026, .month = 10, .week_count = 5};
    strcpy(data.week_start, "2026-09-28"); strcpy(data.week_end, "2026-10-04");
    strcpy(data.updated_at, "2026-10-02T10:30:00+08:00");
    assert(workout_parse_timestamp(data.updated_at, &data.updated_seconds));
    workout_cache_t first, restored;
    workout_cache_pack(&first, &data, config.server);
    assert(workout_store_save_cache(&first) == ESP_OK);
    assert(workout_store_clear_config() == ESP_OK);
    assert(workout_store_load_config(&loaded) == ESP_ERR_NVS_NOT_FOUND && loaded.ssid[0] == '\0');
    assert(workout_store_load_cache(&restored) == ESP_OK && memcmp(&first, &restored, sizeof(first)) == 0);
    s_values[1][30] ^= 1;
    assert(workout_store_load_cache(&restored) == ESP_ERR_INVALID_CRC && restored.magic == 0);
    assert(s_open == s_close);
    puts("Workout storage: PASS");
    return 0;
}
