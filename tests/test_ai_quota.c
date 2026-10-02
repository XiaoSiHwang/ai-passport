#include "ai_quota.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void urls(void) {
    char url[WORKOUT_URL_SIZE];
    assert(ai_quota_url("http://example.com:8000/api/workout", 0, url));
    assert(strcmp(url, "http://example.com:8000/api/codex/quota") == 0);
    assert(ai_quota_url("https://example.com/prefix/api/workout", 1, url));
    assert(strcmp(url, "https://example.com/prefix/api/glm/quota") == 0);
    assert(!ai_quota_url("http://localhost/api/workout", 0, url));
    assert(!ai_quota_url("http://example.com", 0, url));
    assert(!ai_quota_url("http://example.com/api/workout", 2, url));
    char large[WORKOUT_URL_SIZE];
    memset(large, 'a', sizeof(large));
    memcpy(large, "http://example.com/", 19);
    memcpy(large + sizeof(large) - 13, "/api/workout", 12);
    large[sizeof(large) - 1] = 0;
    assert(!ai_quota_url(large, 0, url));
}

static void caches(void) {
    ai_quota_data_t data = {0};
    data.windows[0].available = true;
    strcpy(data.fetched_at, "2026-10-02T10:30:00+08:00");
    assert(workout_parse_timestamp(data.fetched_at, &data.fetched_seconds));
    assert(ai_quota_data_valid(&data)); /* An available zero is exhausted. */
    ai_quota_cache_t cache;
    const char *url = "http://example.com/api/codex/quota";
    ai_quota_cache_pack(&cache, &data, url, 0);
    assert(ai_quota_cache_valid(&cache, sizeof(cache), 0));
    assert(!ai_quota_cache_valid(&cache, sizeof(cache), 1));
    assert(!ai_quota_cache_valid(&cache, sizeof(cache) - 1, 0));
    assert(ai_quota_newer(&cache, &data, url));
    data.fetched_seconds--;
    assert(!ai_quota_newer(&cache, &data, url));
    assert(ai_quota_newer(&cache, &data, "http://other.example/api/codex/quota"));
    cache.data.windows[0].remaining++;
    assert(!ai_quota_cache_valid(&cache, sizeof(cache), 0));
    data.fetched_seconds++;
    data.windows[0].remaining = 1001;
    assert(!ai_quota_data_valid(&data));
    data.windows[0].remaining = 0;
    data.windows[0].available = false;
    assert(!ai_quota_data_valid(&data)); /* No windows is unavailable, not zero. */
}

int main(void) {
    urls(); caches();
    puts("AI quota model: PASS");
    return 0;
}
