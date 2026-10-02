#include "workout_json.h"

#include "cJSON.h"
#include <math.h>
#include <string.h>

static cJSON *bounded_json(const char *json, size_t length) {
    if (!json || !length || length > WORKOUT_JSON_LIMIT) return NULL;
    unsigned depth = 0, tokens = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; i++) {
        char c = json[i];
        if (c == '\0') return NULL;
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 10 || ++tokens > 192) return NULL; }
        else if (c == '}' || c == ']') { if (!depth) return NULL; depth--; }
        else if (c == ':' || c == ',') { if (++tokens > 192) return NULL; }
        /* cJSON strings contain embedded NULs after decoding this escape. */
        if (c == '\\' && i + 5 < length && memcmp(json + i + 1, "u0000", 5) == 0) return NULL;
    }
    if (depth || quoted) return NULL;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (!root) return NULL;
    while (end < json + length && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) end++;
    if (end != json + length) { cJSON_Delete(root); return NULL; }
    return root;
}

static bool unique_object(const cJSON *object) {
    if (!cJSON_IsObject(object)) return false;
    for (const cJSON *item = object->child; item; item = item->next) {
        for (const cJSON *other = item->next; other; other = other->next) {
            if (strcmp(item->string, other->string) == 0) return false;
        }
    }
    return true;
}

static bool number(const cJSON *object, const char *key, double scale, uint32_t limit,
                   uint32_t *output) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)
        || item->valuedouble < 0 || item->valuedouble * scale > limit) return false;
    if (scale == 1 && floor(item->valuedouble) != item->valuedouble) return false;
    *output = (uint32_t)floor(item->valuedouble * scale + 0.5);
    return true;
}

static bool string(const cJSON *object, const char *key, char *output, size_t size) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || strlen(item->valuestring) >= size) return false;
    strcpy(output, item->valuestring);
    return true;
}

static bool summary(const cJSON *object, workout_summary_t *result) {
    return unique_object(object)
        && number(object, "distance_km", 10, WORKOUT_DISTANCE_LIMIT, &result->distance)
        && number(object, "duration_seconds", 1, 366U * 86400U, &result->duration)
        && number(object, "average_pace_seconds_per_km", 1, 86400, &result->pace)
        && number(object, "goal_distance_km", 10, WORKOUT_DISTANCE_LIMIT, &result->goal);
}

static bool month_buckets(const cJSON *monthly, workout_data_t *result) {
    const cJSON *weeks = cJSON_GetObjectItemCaseSensitive(monthly, "weeks");
    if (!cJSON_IsArray(weeks)) return false;
    int count = cJSON_GetArraySize(weeks);
    if (count < 4 || count > 5) return false;
    result->week_count = (uint8_t)count;
    for (int i = 0; i < count; i++) {
        const cJSON *bucket = cJSON_GetArrayItem(weeks, i);
        uint32_t week;
        if (!unique_object(bucket) || !number(bucket, "week", 1, 5, &week) || week != (uint32_t)i + 1
            || !number(bucket, "distance_km", 10, WORKOUT_DISTANCE_LIMIT, &result->weeks[i])) return false;
    }
    return true;
}

static bool week_buckets(const cJSON *weekly, workout_data_t *result) {
    if (!string(weekly, "week_start", result->week_start, sizeof(result->week_start))
        || !string(weekly, "week_end", result->week_end, sizeof(result->week_end))) return false;
    int64_t start;
    if (!workout_parse_date(result->week_start, &start)) return false;
    const cJSON *days = cJSON_GetObjectItemCaseSensitive(weekly, "days");
    if (!cJSON_IsArray(days) || cJSON_GetArraySize(days) != 7) return false;
    for (unsigned i = 0; i < 7; i++) {
        const cJSON *bucket = cJSON_GetArrayItem(days, (int)i);
        uint32_t weekday;
        char date[11];
        int64_t day;
        if (!unique_object(bucket) || !number(bucket, "weekday", 1, 7, &weekday) || weekday != i + 1
            || !string(bucket, "date", date, sizeof(date)) || !workout_parse_date(date, &day)
            || day != start + i
            || !number(bucket, "distance_km", 10, WORKOUT_DISTANCE_LIMIT, &result->days[i])) return false;
    }
    return true;
}

static bool decode_data(const cJSON *root, workout_data_t *result) {
    if (!unique_object(root) || !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "success"))) return false;
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    uint32_t year, month;
    if (!unique_object(data) || !number(data, "year", 1, 2199, &year)
        || !number(data, "month", 1, 12, &month)) return false;
    result->year = (uint16_t)year;
    result->month = (uint8_t)month;
    const cJSON *monthly = cJSON_GetObjectItemCaseSensitive(data, "monthly");
    const cJSON *weekly = cJSON_GetObjectItemCaseSensitive(data, "weekly");
    const cJSON *stale = cJSON_GetObjectItemCaseSensitive(data, "stale");
    if (stale && !cJSON_IsBool(stale)) return false;
    result->stale = cJSON_IsTrue(stale);
    return summary(monthly, &result->monthly) && summary(weekly, &result->weekly)
        && month_buckets(monthly, result) && week_buckets(weekly, result)
        && string(data, "updated_at", result->updated_at, sizeof(result->updated_at))
        && workout_parse_timestamp(result->updated_at, &result->updated_seconds)
        && workout_data_valid(result);
}

bool workout_decode_response(const char *json, size_t length, workout_data_t *data) {
    cJSON *root = bounded_json(json, length);
    if (!root) return false;
    workout_data_t result = {0};
    bool valid = decode_data(root, &result);
    cJSON_Delete(root);
    if (valid) *data = result;
    return valid;
}

static bool quota_window(const cJSON *object, ai_quota_window_t *window) {
    if (cJSON_IsNull(object)) return true;
    uint32_t remaining;
    if (!unique_object(object) || !number(object, "remaining_percent", 10, 1000, &remaining)) return false;
    window->available = true;
    window->remaining = (uint16_t)remaining;
    const cJSON *reset = cJSON_GetObjectItemCaseSensitive(object, "reset_at");
    if (cJSON_IsNull(reset)) return true;
    int64_t seconds;
    return string(object, "reset_at", window->reset_at, sizeof(window->reset_at))
        && workout_parse_timestamp(window->reset_at, &seconds);
}

static bool quota_data(const cJSON *root, unsigned provider, ai_quota_data_t *result) {
    if (provider >= AI_QUOTA_PROVIDERS || !unique_object(root)
        || !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "success"))) return false;
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!unique_object(data)) return false;
    const cJSON *stale = cJSON_GetObjectItemCaseSensitive(data, "stale");
    if (stale && !cJSON_IsBool(stale)) return false;
    result->stale = cJSON_IsTrue(stale);
    if (provider == 1) {
        const cJSON *level = cJSON_GetObjectItemCaseSensitive(data, "level");
        if (level && !cJSON_IsNull(level)) {
            if (!cJSON_IsString(level)) return false;
            /* Arbitrary plan names stay off the fixed-subset display. */
            bool printable = strlen(level->valuestring) < sizeof(result->level);
            for (const char *c = level->valuestring; *c; c++)
                if ((unsigned char)*c < 32 || (unsigned char)*c > 126) printable = false;
            if (printable) strcpy(result->level, level->valuestring);
        }
    }
    return quota_window(cJSON_GetObjectItemCaseSensitive(data, "five_hour"), &result->windows[0])
        && quota_window(cJSON_GetObjectItemCaseSensitive(data, "seven_day"), &result->windows[1])
        && string(data, "fetched_at", result->fetched_at, sizeof(result->fetched_at))
        && workout_parse_timestamp(result->fetched_at, &result->fetched_seconds)
        && ai_quota_data_valid(result);
}

bool ai_quota_decode_response(const char *json, size_t length, unsigned provider, ai_quota_data_t *data) {
    cJSON *root = bounded_json(json, length);
    if (!root) return false;
    ai_quota_data_t result = {0};
    bool valid = quota_data(root, provider, &result);
    cJSON_Delete(root);
    if (valid) *data = result;
    return valid;
}

bool workout_decode_config(const char *json, size_t length, workout_config_t *config,
                           char token[33]) {
    cJSON *root = bounded_json(json, length);
    if (!root) return false;
    workout_config_t result = {0};
    char server[WORKOUT_URL_SIZE], candidate_token[33];
    bool valid = unique_object(root)
        && string(root, "ssid", result.ssid, sizeof(result.ssid))
        && string(root, "password", result.password, sizeof(result.password))
        && string(root, "server", server, sizeof(server))
        && string(root, "token", candidate_token, sizeof(candidate_token))
        && strlen(candidate_token) == 32 && workout_normalize_url(server, result.server)
        && workout_config_valid(&result);
    for (size_t i = 0; valid && result.ssid[i]; i++) if ((unsigned char)result.ssid[i] < 32) valid = false;
    cJSON_Delete(root);
    if (valid) { *config = result; strcpy(token, candidate_token); }
    return valid;
}
