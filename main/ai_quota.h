#pragma once

#include "workout_model.h"

#define AI_QUOTA_PROVIDERS 2
#define AI_QUOTA_MAGIC 0x41495154U
#define AI_QUOTA_VERSION 1U

typedef struct {
    uint16_t remaining; /* Tenths of a percent; 0 and unavailable are distinct. */
    bool available;
    char reset_at[40]; /* Empty when the upstream reset time is unknown. */
} ai_quota_window_t;

typedef struct {
    ai_quota_window_t windows[2]; /* Five hours, seven days. */
    char fetched_at[40];
    int64_t fetched_seconds;
    char level[17]; /* Optional printable ASCII label, never credentials. */
    bool stale;
} ai_quota_data_t;

typedef struct {
    uint32_t magic, version, provider, source, checksum;
    ai_quota_data_t data;
} ai_quota_cache_t;

typedef struct {
    ai_quota_data_t data;
    bool available, from_cache, persisted, failed, syncing;
    int http_status;
} ai_quota_state_t;

typedef struct { ai_quota_state_t providers[AI_QUOTA_PROVIDERS]; } ai_quota_update_t;

bool ai_quota_url(const char *workout_server, unsigned provider, char output[WORKOUT_URL_SIZE]);
bool ai_quota_data_valid(const ai_quota_data_t *data);
void ai_quota_cache_pack(ai_quota_cache_t *cache, const ai_quota_data_t *data,
                         const char *url, unsigned provider);
bool ai_quota_cache_valid(const ai_quota_cache_t *cache, size_t size, unsigned provider);
bool ai_quota_newer(const ai_quota_cache_t *cache, const ai_quota_data_t *data, const char *url);
