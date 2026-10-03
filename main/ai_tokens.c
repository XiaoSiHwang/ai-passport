#include "ai_tokens.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

bool ai_tokens_url(const char *workout_server, unsigned provider, char output[WORKOUT_URL_SIZE]) {
    char normalized[WORKOUT_URL_SIZE];
    if (provider >= AI_QUOTA_PROVIDERS || !workout_normalize_url(workout_server, normalized)
        || strcmp(normalized, workout_server) != 0) return false;
    const char *paths[] = {"/api/codex/tokens", "/api/glm/tokens"};
    size_t base = strlen(normalized) - strlen("/api/workout");
    if (base + strlen(paths[provider]) >= WORKOUT_URL_SIZE) return false;
    snprintf(output, WORKOUT_URL_SIZE, "%.*s%s", (int)base, normalized, paths[provider]);
    return true;
}

bool ai_tokens_known(const ai_tokens_data_t *data, unsigned index) {
    return index < AI_TOKENS_DAYS && (data->known & (UINT32_C(1) << index)) != 0;
}

bool ai_tokens_data_valid(const ai_tokens_data_t *data) {
    int64_t start, end, fetched;
    if (!memchr(data->start_date, 0, sizeof(data->start_date))
        || !memchr(data->end_date, 0, sizeof(data->end_date))
        || !memchr(data->fetched_at, 0, sizeof(data->fetched_at))
        || strlen(data->start_date) != 10 || strlen(data->end_date) != 10
        || !workout_parse_date(data->start_date, &start) || start != data->start_day
        || !workout_parse_date(data->end_date, &end) || end - start != AI_TOKENS_DAYS - 1
        || !workout_parse_timestamp(data->fetched_at, &fetched) || fetched != data->fetched_seconds
        || (fetched + 28800) / 86400 != end || data->known >> AI_TOKENS_DAYS) return false;
    uint64_t total = 0;
    unsigned count = 0;
    for (unsigned i = 0; i < AI_TOKENS_DAYS; i++) {
        if (!ai_tokens_known(data, i)) { if (data->daily[i]) return false; continue; }
        if (data->daily[i] > AI_TOKENS_LIMIT - total) return false;
        total += data->daily[i];
        count++;
    }
    return total == data->total && count == data->available_days;
}

void ai_tokens_date(const ai_tokens_data_t *data, unsigned index, char output[11]) {
    unsigned year, month, day;
    if (index >= AI_TOKENS_DAYS || sscanf(data->start_date, "%u-%u-%u", &year, &month, &day) != 3
        || month < 1 || month > 12) { strcpy(output, "--"); return; }
    static const unsigned lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    day += index;
    for (;;) {
        unsigned length = lengths[month - 1]
            + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        if (day <= length) break;
        day -= length;
        if (++month > 12) { month = 1; year++; }
    }
    snprintf(output, 11, "%04u-%02u-%02u", year % 10000, month % 100, day % 100);
}

unsigned ai_tokens_cell(const ai_tokens_data_t *data, unsigned index) {
    return (unsigned)(data->start_day + 3) % 7 + index; /* Monday first; Unix epoch was Thursday. */
}

unsigned ai_tokens_level(uint64_t tokens) {
    return !tokens ? 0 : tokens < 200000 ? 1 : tokens < 500000 ? 2 : tokens < 1000000 ? 3 : 4;
}

void ai_tokens_format(uint64_t tokens, bool known, char output[32]) {
    if (!known) { strcpy(output, "--"); return; }
    if (tokens < 1000) { snprintf(output, 32, "%" PRIu64, tokens); return; }
    uint64_t unit = tokens < 1000000 ? 1000 : tokens < 100000000 ? 1000000 : 100000000;
    uint64_t value = tokens / unit * 100 + ((tokens % unit) * 100 + unit / 2) / unit;
    if ((unit == 1000 && value >= 100000) || (unit == 1000000 && value >= 10000)) {
        unit = unit == 1000 ? 1000000 : 100000000;
        value = tokens / unit * 100 + ((tokens % unit) * 100 + unit / 2) / unit;
    }
    const char *suffix = unit == 1000 ? "K" : unit == 1000000 ? "M" : "亿";
    unsigned fraction = (unsigned)(value % 100);
    if (!fraction) snprintf(output, 32, "%" PRIu64 "%s", value / 100, suffix);
    else if (!(fraction % 10)) snprintf(output, 32, "%" PRIu64 ".%u%s", value / 100, fraction / 10, suffix);
    else snprintf(output, 32, "%" PRIu64 ".%02u%s", value / 100, fraction, suffix);
}

bool ai_tokens_current(const ai_tokens_data_t *data, int32_t today) {
    return data->start_day + AI_TOKENS_DAYS - 1 == today;
}

static uint32_t cache_checksum(const ai_tokens_cache_t *cache) {
    ai_tokens_cache_t copy = *cache;
    copy.checksum = 0;
    return workout_checksum(&copy, sizeof(copy));
}

void ai_tokens_cache_pack(ai_tokens_cache_t *cache, const ai_tokens_data_t *data,
                          const char *url, unsigned provider) {
    memset(cache, 0, sizeof(*cache));
    cache->magic = AI_TOKENS_MAGIC;
    cache->version = AI_TOKENS_VERSION;
    cache->provider = provider;
    cache->source = workout_checksum(url, strlen(url));
    cache->data = *data;
    cache->checksum = cache_checksum(cache);
}

bool ai_tokens_cache_valid(const ai_tokens_cache_t *cache, size_t size, unsigned provider) {
    return size == sizeof(*cache) && provider < AI_QUOTA_PROVIDERS && cache->provider == provider
        && cache->magic == AI_TOKENS_MAGIC && cache->version == AI_TOKENS_VERSION
        && cache->checksum == cache_checksum(cache) && ai_tokens_data_valid(&cache->data);
}

bool ai_tokens_newer(const ai_tokens_cache_t *cache, const ai_tokens_data_t *data, const char *url) {
    return !cache->magic || cache->source != workout_checksum(url, strlen(url))
        || (data->start_day >= cache->data.start_day && data->fetched_seconds >= cache->data.fetched_seconds);
}
