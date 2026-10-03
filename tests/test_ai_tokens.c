#include "ai_tokens.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static ai_tokens_data_t sample(const char *start, const char *end, const char *fetched) {
    ai_tokens_data_t data = {0};
    strcpy(data.start_date, start); strcpy(data.end_date, end); strcpy(data.fetched_at, fetched);
    int64_t day;
    assert(workout_parse_date(start, &day)); data.start_day = (int32_t)day;
    assert(workout_parse_timestamp(fetched, &data.fetched_seconds));
    return data;
}

static void amounts(void) {
    const uint64_t values[] = {0,999,1000,1234,999999,1000000,99999999,100000000,125000000,
        AI_TOKENS_LIMIT};
    const char *expected[] = {"0","999","1K","1.23K","1M","1M","1亿","1亿","1.25亿","90071992.55亿"};
    char output[32];
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        ai_tokens_format(values[i], true, output);
        assert(strcmp(output, expected[i]) == 0);
    }
    ai_tokens_format(0, false, output); assert(strcmp(output, "--") == 0);
    assert(ai_tokens_level(0) == 0 && ai_tokens_level(1) == 1);
    assert(ai_tokens_level(200000) == 2 && ai_tokens_level(500000) == 3 && ai_tokens_level(1000000) == 4);
}

static void dates(void) {
    ai_tokens_data_t data = sample("2026-09-04", "2026-10-03", "2026-10-03T12:00:00+08:00");
    assert(ai_tokens_data_valid(&data)); /* All unknown is a successful, empty dataset. */
    assert(!ai_tokens_known(&data, 29) && !ai_tokens_known(&data, 30));
    char date[11];
    ai_tokens_date(&data, 29, date); assert(strcmp(date, "2026-10-03") == 0);
    assert(ai_tokens_cell(&data, 0) == 4 && ai_tokens_cell(&data, 29) == 33);
    assert(ai_tokens_current(&data, data.start_day + 29) && !ai_tokens_current(&data, data.start_day + 30));
    data = sample("2024-02-01", "2024-03-01", "2024-03-01T00:00:00+08:00");
    assert(ai_tokens_data_valid(&data));
    ai_tokens_date(&data, 28, date); assert(strcmp(date, "2024-02-29") == 0);
    ai_tokens_date(&data, 29, date); assert(strcmp(date, "2024-03-01") == 0);
    data = sample("2026-12-20", "2027-01-18", "2027-01-18T23:59:59+08:00");
    assert(ai_tokens_cell(&data, 29) == 35); /* Six rows required for a Sunday start. */
    ai_tokens_date(&data, 29, date); assert(strcmp(date, "2027-01-18") == 0);
    assert(ai_tokens_data_valid(&data));
    data.known = UINT32_C(1) << 29; data.available_days = 1;
    assert(ai_tokens_data_valid(&data) && ai_tokens_known(&data, 29)); /* Known zero. */
    data.daily[29] = AI_TOKENS_LIMIT; data.total = AI_TOKENS_LIMIT;
    assert(ai_tokens_data_valid(&data));
    data.known |= 1; data.available_days++; data.daily[0] = 1;
    assert(!ai_tokens_data_valid(&data)); /* Sum exceeds exact-integer range. */
}

static void caches(void) {
    ai_tokens_data_t data = sample("2026-09-04", "2026-10-03", "2026-10-03T12:00:00+08:00");
    char url[WORKOUT_URL_SIZE];
    assert(ai_tokens_url("https://example.com/prefix/api/workout", 0, url));
    assert(strcmp(url, "https://example.com/prefix/api/codex/tokens") == 0);
    assert(!ai_tokens_url("http://localhost/api/workout", 0, url));
    assert(!ai_tokens_url("http://example.com/api/workout", 2, url));
    assert(ai_tokens_url("http://example.com/api/workout", 1, url));
    assert(strcmp(url, "http://example.com/api/glm/tokens") == 0);
    ai_tokens_cache_t cache;
    ai_tokens_cache_pack(&cache, &data, url, 1);
    assert(ai_tokens_cache_valid(&cache, sizeof(cache), 1));
    assert(!ai_tokens_cache_valid(&cache, sizeof(cache), 0));
    assert(!ai_tokens_cache_valid(&cache, sizeof(cache) - 1, 1));
    assert(ai_tokens_newer(&cache, &data, url));
    data.fetched_seconds--;
    assert(!ai_tokens_newer(&cache, &data, url));
    assert(ai_tokens_newer(&cache, &data, "http://other.example/api/glm/tokens"));
    cache.data.daily[0]++;
    assert(!ai_tokens_cache_valid(&cache, sizeof(cache), 1));
}

int main(void) {
    amounts(); dates(); caches();
    puts("AI tokens model: PASS");
    return 0;
}
