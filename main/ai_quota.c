#include "ai_quota.h"

#include <stdio.h>
#include <string.h>

bool ai_quota_url(const char *workout_server, unsigned provider, char output[WORKOUT_URL_SIZE]) {
    char normalized[WORKOUT_URL_SIZE];
    if (provider >= AI_QUOTA_PROVIDERS || !workout_normalize_url(workout_server, normalized)
        || strcmp(normalized, workout_server) != 0) return false;
    const char *paths[] = {"/api/codex/quota", "/api/glm/quota"};
    size_t base = strlen(normalized) - strlen("/api/workout");
    if (base + strlen(paths[provider]) >= WORKOUT_URL_SIZE) return false;
    snprintf(output, WORKOUT_URL_SIZE, "%.*s%s", (int)base, normalized, paths[provider]);
    return true;
}

bool ai_quota_data_valid(const ai_quota_data_t *data) {
    int64_t stamp;
    if (!memchr(data->fetched_at, 0, sizeof(data->fetched_at))
        || !memchr(data->level, 0, sizeof(data->level))
        || !workout_parse_timestamp(data->fetched_at, &stamp) || stamp != data->fetched_seconds) return false;
    for (const char *c = data->level; *c; c++) if ((unsigned char)*c < 32 || (unsigned char)*c > 126) return false;
    bool available = false;
    for (unsigned i = 0; i < 2; i++) {
        const ai_quota_window_t *window = &data->windows[i];
        if (!memchr(window->reset_at, 0, sizeof(window->reset_at)) || window->remaining > 1000) return false;
        if (window->reset_at[0] && !workout_parse_timestamp(window->reset_at, &stamp)) return false;
        if (!window->available && (window->remaining || window->reset_at[0])) return false;
        available |= window->available;
    }
    return available;
}

static uint32_t cache_checksum(const ai_quota_cache_t *cache) {
    ai_quota_cache_t copy = *cache;
    copy.checksum = 0;
    return workout_checksum(&copy, sizeof(copy));
}

void ai_quota_cache_pack(ai_quota_cache_t *cache, const ai_quota_data_t *data,
                         const char *url, unsigned provider) {
    memset(cache, 0, sizeof(*cache));
    cache->magic = AI_QUOTA_MAGIC;
    cache->version = AI_QUOTA_VERSION;
    cache->provider = provider;
    cache->source = workout_checksum(url, strlen(url));
    cache->data = *data;
    cache->checksum = cache_checksum(cache);
}

bool ai_quota_cache_valid(const ai_quota_cache_t *cache, size_t size, unsigned provider) {
    return size == sizeof(*cache) && provider < AI_QUOTA_PROVIDERS && cache->provider == provider
        && cache->magic == AI_QUOTA_MAGIC && cache->version == AI_QUOTA_VERSION
        && cache->checksum == cache_checksum(cache) && ai_quota_data_valid(&cache->data);
}

bool ai_quota_newer(const ai_quota_cache_t *cache, const ai_quota_data_t *data, const char *url) {
    return !cache->magic || cache->source != workout_checksum(url, strlen(url))
        || data->fetched_seconds >= cache->data.fetched_seconds;
}
