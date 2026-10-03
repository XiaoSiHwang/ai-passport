#pragma once

#include "ai_quota.h"

#define AI_TOKENS_DAYS 30
#define AI_TOKENS_MAGIC 0x4149544BU
#define AI_TOKENS_VERSION 1U
#define AI_TOKENS_LIMIT UINT64_C(9007199254740991) /* Exact cJSON/IEEE-754 integers. */

typedef struct {
    uint64_t daily[AI_TOKENS_DAYS], total;
    uint32_t known; /* A missing day is not a known zero. */
    int32_t start_day;
    char start_date[11], end_date[11], fetched_at[40];
    int64_t fetched_seconds;
    unsigned available_days;
    bool stale, timezone_known;
} ai_tokens_data_t;

typedef struct {
    uint32_t magic, version, provider, source, checksum;
    ai_tokens_data_t data;
} ai_tokens_cache_t;

typedef struct {
    ai_tokens_data_t data;
    bool available, from_cache, persisted, failed, syncing;
    int http_status;
} ai_tokens_state_t;

typedef struct { ai_tokens_state_t providers[AI_QUOTA_PROVIDERS]; } ai_tokens_update_t;

bool ai_tokens_url(const char *workout_server, unsigned provider, char output[WORKOUT_URL_SIZE]);
bool ai_tokens_data_valid(const ai_tokens_data_t *data);
bool ai_tokens_known(const ai_tokens_data_t *data, unsigned index);
void ai_tokens_date(const ai_tokens_data_t *data, unsigned index, char output[11]);
unsigned ai_tokens_cell(const ai_tokens_data_t *data, unsigned index);
unsigned ai_tokens_level(uint64_t tokens);
void ai_tokens_format(uint64_t tokens, bool known, char output[32]);
bool ai_tokens_current(const ai_tokens_data_t *data, int32_t today);
void ai_tokens_cache_pack(ai_tokens_cache_t *cache, const ai_tokens_data_t *data,
                          const char *url, unsigned provider);
bool ai_tokens_cache_valid(const ai_tokens_cache_t *cache, size_t size, unsigned provider);
bool ai_tokens_newer(const ai_tokens_cache_t *cache, const ai_tokens_data_t *data, const char *url);
