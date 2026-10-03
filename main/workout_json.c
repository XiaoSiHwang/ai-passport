#include "workout_json.h"
#include "codex_monitor.h"
#include "workout_profiles.h"

#include "cJSON.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static cJSON *bounded_json(const char *json, size_t length, unsigned token_limit) {
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
        else if (c == '{' || c == '[') { if (++depth > 10 || ++tokens > token_limit) return NULL; }
        else if (c == '}' || c == ']') { if (!depth) return NULL; depth--; }
        else if (c == ':' || c == ',') { if (++tokens > token_limit) return NULL; }
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
    cJSON *root = bounded_json(json, length, 192);
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
    cJSON *root = bounded_json(json, length, 192);
    if (!root) return false;
    ai_quota_data_t result = {0};
    bool valid = quota_data(root, provider, &result);
    cJSON_Delete(root);
    if (valid) *data = result;
    return valid;
}

static bool token_number(const cJSON *item, uint64_t *output) {
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) || item->valuedouble < 0
        || item->valuedouble > (double)AI_TOKENS_LIMIT || floor(item->valuedouble) != item->valuedouble)
        return false;
    *output = (uint64_t)item->valuedouble;
    return true;
}

static bool token_days(const cJSON *data, ai_tokens_data_t *result) {
    const cJSON *days = cJSON_GetObjectItemCaseSensitive(data, "daily_usage");
    if (!cJSON_IsArray(days) || cJSON_GetArraySize(days) != AI_TOKENS_DAYS) return false;
    for (unsigned i = 0; i < AI_TOKENS_DAYS; i++) {
        const cJSON *bucket = cJSON_GetArrayItem(days, (int)i);
        char date[11];
        int64_t day;
        if (!unique_object(bucket) || !string(bucket, "date", date, sizeof(date)) || strlen(date) != 10
            || !workout_parse_date(date, &day) || day != result->start_day + i) return false;
        const cJSON *tokens = cJSON_GetObjectItemCaseSensitive(bucket, "tokens");
        if (cJSON_IsNull(tokens)) continue;
        if (!token_number(tokens, &result->daily[i])) return false;
        result->known |= UINT32_C(1) << i;
    }
    return true;
}

static bool token_data(const cJSON *root, unsigned provider, ai_tokens_data_t *result) {
    if (provider >= AI_QUOTA_PROVIDERS || !unique_object(root)
        || !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "success"))) return false;
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    char source[24];
    int64_t day;
    if (!unique_object(data) || !string(data, "source", source, sizeof(source))
        || strcmp(source, provider ? "glm_model_usage" : "codex_app_server") != 0
        || !string(data, "start_date", result->start_date, sizeof(result->start_date))
        || !workout_parse_date(result->start_date, &day)) return false;
    result->start_day = (int32_t)day;
    const cJSON *timezone = cJSON_GetObjectItemCaseSensitive(data, "timezone");
    result->timezone_known = cJSON_IsString(timezone) && strcmp(timezone->valuestring, "Asia/Shanghai") == 0;
    if (!result->timezone_known && (!cJSON_IsNull(timezone) || provider)) return false;
    const cJSON *stale = cJSON_GetObjectItemCaseSensitive(data, "stale");
    if (!cJSON_IsBool(stale)) return false;
    result->stale = cJSON_IsTrue(stale);
    uint32_t count;
    if (!string(data, "end_date", result->end_date, sizeof(result->end_date))
        || !string(data, "fetched_at", result->fetched_at, sizeof(result->fetched_at))
        || !workout_parse_timestamp(result->fetched_at, &result->fetched_seconds)
        || !number(data, "available_days", 1, AI_TOKENS_DAYS, &count) || !token_days(data, result)) return false;
    result->available_days = count;
    const cJSON *total = cJSON_GetObjectItemCaseSensitive(data, "total_tokens");
    if (count ? !token_number(total, &result->total) : !cJSON_IsNull(total)) return false;
    return ai_tokens_data_valid(result);
}

bool ai_tokens_decode_response(const char *json, size_t length, unsigned provider, ai_tokens_data_t *data) {
    cJSON *root = bounded_json(json, length, 512);
    if (!root) return false;
    ai_tokens_data_t result = {0};
    bool valid = token_data(root, provider, &result);
    cJSON_Delete(root);
    if (valid) *data = result;
    return valid;
}

bool workout_decode_config(const char *json, size_t length, workout_config_t *config,
                           char token[33]) {
    cJSON *root = bounded_json(json, length, 192);
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

static bool monitor_text(const cJSON *object, const char *key, char *output, size_t size,
                         bool identifier) {
    if (!string(object, key, output, size) || (identifier && !output[0])) return false;
    if (identifier && !((output[0] >= 'A' && output[0] <= 'Z') || (output[0] >= 'a' && output[0] <= 'z')
        || (output[0] >= '0' && output[0] <= '9'))) return false;
    for (size_t index = 0; output[index];) {
        uint32_t code = workout_text_next(output, &index);
        if (code < 32 || code == 127 || code == 0xFFFD) return false;
        if (identifier && !((code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z')
            || (code >= '0' && code <= '9') || code == '_' || code == '.' || code == ':' || code == '-')) return false;
    }
    return true;
}

static bool monitor_bool(const cJSON *object, const char *key, bool *value) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsBool(item)) return false;
    *value = cJSON_IsTrue(item);
    return true;
}

static bool monitor_timestamp(const cJSON *object, const char *key, int64_t *seconds) {
    char timestamp[40];
    return string(object, key, timestamp, sizeof(timestamp)) && workout_parse_timestamp(timestamp, seconds);
}

static bool monitor_enum(const cJSON *object, const char *key, const char *const names[],
                         unsigned count, unsigned *value) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item)) return false;
    for (unsigned i = 0; i < count; i++) {
        if (strcmp(names[i], item->valuestring) == 0) { *value = i; return true; }
    }
    return false;
}

static bool monitor_approval(const cJSON *object, codex_task_t *task) {
    if (cJSON_IsNull(object)) return true;
    static const char *const states[] = {"requested", "unknown"};
    unsigned state;
    char tool[49];
    if (!unique_object(object)
        || !monitor_text(object, "approval_id", task->approval_id, sizeof(task->approval_id), true)
        || !monitor_text(object, "summary", task->approval_summary, sizeof(task->approval_summary), false)
        || !monitor_text(object, "tool", tool, sizeof(tool), false)
        || !monitor_enum(object, "state", states, 2, &state)) return false;
    task->approval = state ? CODEX_APPROVAL_UNKNOWN : CODEX_APPROVAL_REQUESTED;
    return true;
}

static bool monitor_task(const cJSON *object, codex_task_t *task) {
    static const char *const statuses[] = {"running", "approval_requested", "ended", "interrupted", "unknown"};
    unsigned status;
    if (!unique_object(object)
        || !monitor_text(object, "task_id", task->task_id, sizeof(task->task_id), true)
        || !monitor_text(object, "project", task->project, sizeof(task->project), false)
        || !monitor_text(object, "title", task->title, sizeof(task->title), false)
        || !monitor_text(object, "step", task->step, sizeof(task->step), false)
        || !monitor_enum(object, "status", statuses, 5, &status)
        || !monitor_timestamp(object, "started_at", &task->started_seconds)
        || !monitor_timestamp(object, "updated_at", &task->updated_seconds)
        || !monitor_bool(object, "fresh", &task->fresh)
        || !monitor_approval(cJSON_GetObjectItemCaseSensitive(object, "pending_approval"), task)) return false;
    task->status = (codex_task_status_t)status;
    if (task->status == CODEX_TASK_UNKNOWN || task->approval == CODEX_APPROVAL_UNKNOWN) task->fresh = false;
    if ((status == CODEX_TASK_ENDED || status == CODEX_TASK_INTERRUPTED) && task->approval != CODEX_APPROVAL_NONE) return false;
    return true;
}

static const cJSON *monitor_envelope(const cJSON *root, char stream_id[CODEX_ID_SIZE]) {
    if (!unique_object(root) || !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "success"))) return NULL;
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    uint32_t version;
    if (!unique_object(data) || !number(data, "schema_version", 1, 1, &version) || version != 1
        || !monitor_text(data, "stream_id", stream_id, CODEX_ID_SIZE, true)) return NULL;
    return data;
}

static bool monitor_tasks_data(const cJSON *root, codex_tasks_data_t *result) {
    const cJSON *data = monitor_envelope(root, result->stream_id);
    if (!data || !number(data, "revision", 1, INT32_MAX, &result->revision)
        || !number(data, "active_count", 1, INT32_MAX, &result->active_count)
        || !number(data, "pending_approval_count", 1, INT32_MAX, &result->pending_count)
        || !number(data, "omitted_count", 1, INT32_MAX, &result->omitted_count)
        || !monitor_bool(data, "source_online", &result->source_online)
        || !string(data, "generated_at", result->generated_at, sizeof(result->generated_at))
        || !workout_parse_timestamp(result->generated_at, &result->generated_seconds)) return false;
    const cJSON *tasks = cJSON_GetObjectItemCaseSensitive(data, "tasks");
    if (!cJSON_IsArray(tasks) || cJSON_GetArraySize(tasks) > CODEX_TASK_LIMIT) return false;
    result->count = (unsigned)cJSON_GetArraySize(tasks);
    for (unsigned i = 0; i < result->count; i++) {
        if (!monitor_task(cJSON_GetArrayItem(tasks, (int)i), &result->tasks[i])) return false;
        for (unsigned j = 0; j < i; j++)
            if (strcmp(result->tasks[i].task_id, result->tasks[j].task_id) == 0) return false;
    }
    return true;
}

bool codex_tasks_decode(const char *json, size_t length, codex_tasks_data_t *data) {
    cJSON *root = bounded_json(json, length, 384);
    if (!root) return false;
    codex_tasks_data_t *result = calloc(1, sizeof(*result));
    bool valid = result && monitor_tasks_data(root, result);
    cJSON_Delete(root);
    if (valid) *data = *result;
    free(result);
    return valid;
}

static bool monitor_alert(const cJSON *object, codex_alert_t *alert) {
    static const char *const states[] = {"requested", "resolved", "superseded", "unknown"};
    static const codex_approval_state_t values[] = {CODEX_APPROVAL_REQUESTED, CODEX_APPROVAL_RESOLVED,
        CODEX_APPROVAL_SUPERSEDED, CODEX_APPROVAL_UNKNOWN};
    unsigned state;
    if (!unique_object(object) || !number(object, "cursor", 1, INT32_MAX, &alert->cursor) || !alert->cursor
        || !monitor_text(object, "alert_id", alert->alert_id, sizeof(alert->alert_id), true)
        || !monitor_text(object, "approval_id", alert->approval_id, sizeof(alert->approval_id), true)
        || !monitor_text(object, "task_id", alert->task_id, sizeof(alert->task_id), true)
        || !monitor_text(object, "project", alert->project, sizeof(alert->project), false)
        || !monitor_text(object, "title", alert->title, sizeof(alert->title), false)
        || !monitor_text(object, "summary", alert->summary, sizeof(alert->summary), false)
        || !monitor_timestamp(object, "created_at", &alert->created_seconds)
        || !monitor_enum(object, "state", states, 4, &state)
        || !monitor_bool(object, "fresh", &alert->fresh)) return false;
    alert->state = values[state];
    return true;
}

static bool monitor_alerts_data(const cJSON *root, codex_alert_page_t *result) {
    const cJSON *data = monitor_envelope(root, result->stream_id);
    if (!data || !number(data, "next_cursor", 1, INT32_MAX, &result->next_cursor)
        || !number(data, "latest_cursor", 1, INT32_MAX, &result->latest_cursor)
        || !monitor_bool(data, "has_more", &result->has_more)
        || result->next_cursor > result->latest_cursor
        || result->has_more != (result->next_cursor < result->latest_cursor)) return false;
    const cJSON *alerts = cJSON_GetObjectItemCaseSensitive(data, "alerts");
    if (!cJSON_IsArray(alerts) || cJSON_GetArraySize(alerts) > CODEX_ALERT_LIMIT) return false;
    result->count = (unsigned)cJSON_GetArraySize(alerts);
    for (unsigned i = 0; i < result->count; i++) {
        codex_alert_t *alert = &result->alerts[i];
        if (!monitor_alert(cJSON_GetArrayItem(alerts, (int)i), alert)
            || alert->cursor > result->next_cursor || (i && alert->cursor <= result->alerts[i - 1].cursor)) return false;
        strcpy(alert->stream_id, result->stream_id);
    }
    return !result->count || result->alerts[result->count - 1].cursor == result->next_cursor;
}

bool codex_alerts_decode(const char *json, size_t length, codex_alert_page_t *page) {
    cJSON *root = bounded_json(json, length, 384);
    if (!root) return false;
    codex_alert_page_t *result = calloc(1, sizeof(*result));
    bool valid = result && monitor_alerts_data(root, result);
    cJSON_Delete(root);
    if (valid) *page = *result;
    free(result);
    return valid;
}
