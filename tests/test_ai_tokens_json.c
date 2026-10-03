#include "workout_json.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cJSON *fixture(unsigned provider, unsigned known) {
    cJSON *root = cJSON_CreateObject(), *data = cJSON_CreateObject(), *days = cJSON_CreateArray();
    cJSON_AddBoolToObject(root, "success", true); cJSON_AddItemToObject(root, "data", data);
    cJSON_AddStringToObject(data, "source", provider ? "glm_model_usage" : "codex_app_server");
    if (provider) cJSON_AddStringToObject(data, "timezone", "Asia/Shanghai");
    else cJSON_AddNullToObject(data, "timezone");
    cJSON_AddStringToObject(data, "start_date", "2026-09-04");
    cJSON_AddStringToObject(data, "end_date", "2026-10-03");
    cJSON_AddStringToObject(data, "fetched_at", "2026-10-03T12:00:00+08:00");
    cJSON_AddBoolToObject(data, "stale", false);
    cJSON_AddNumberToObject(data, "available_days", known);
    uint64_t total = 0;
    for (unsigned i = 0; i < 30; i++) {
        cJSON *bucket = cJSON_CreateObject();
        char date[11];
        if (i < 27) snprintf(date, sizeof(date), "2026-09-%02u", i + 4);
        else snprintf(date, sizeof(date), "2026-10-%02u", i - 26);
        cJSON_AddStringToObject(bucket, "date", date);
        if (i < 30 - known) cJSON_AddNullToObject(bucket, "tokens");
        else { cJSON_AddNumberToObject(bucket, "tokens", i * 1000); total += i * 1000; }
        cJSON_AddItemToArray(days, bucket);
    }
    if (known) cJSON_AddNumberToObject(data, "total_tokens", (double)total);
    else cJSON_AddNullToObject(data, "total_tokens");
    cJSON_AddItemToObject(data, "daily_usage", days);
    return root;
}

static bool decode(cJSON *root, unsigned provider, ai_tokens_data_t *data) {
    char *json = cJSON_PrintUnformatted(root);
    assert(json);
    bool valid = ai_tokens_decode_response(json, strlen(json), provider, data);
    cJSON_free(json); return valid;
}

static void rejects(cJSON *root, unsigned provider) {
    ai_tokens_data_t data, saved;
    memset(&data, 0x5a, sizeof(data)); saved = data;
    assert(!decode(root, provider, &data) && memcmp(&data, &saved, sizeof(data)) == 0);
    cJSON_Delete(root);
}

static void valid_data(void) {
    for (unsigned provider = 0; provider < 2; provider++) {
        for (unsigned known = 0; known <= 30; known += 6) {
            cJSON *root = fixture(provider, known);
            ai_tokens_data_t data;
            assert(decode(root, provider, &data));
            assert(data.available_days == known && data.timezone_known == (provider == 1));
            assert(ai_tokens_known(&data, 29) == (known > 0));
            cJSON_Delete(root);
        }
    }
    cJSON *root = fixture(1, 30), *data = cJSON_GetObjectItem(root, "data");
    cJSON *last = cJSON_GetArrayItem(cJSON_GetObjectItem(data, "daily_usage"), 29);
    cJSON_SetNumberValue(cJSON_GetObjectItem(last, "tokens"), 5000000000.0);
    cJSON_SetNumberValue(cJSON_GetObjectItem(data, "total_tokens"), 5000406000.0);
    cJSON_ReplaceItemInObject(data, "stale", cJSON_CreateTrue());
    ai_tokens_data_t decoded;
    assert(decode(root, 1, &decoded) && decoded.daily[29] == UINT64_C(5000000000) && decoded.stale);
    cJSON_Delete(root);
}

static void malformed_data(void) {
    const char *keys[] = {"start_date","end_date","fetched_at","source","timezone","available_days","total_tokens","stale"};
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        cJSON *root = fixture(1, 30);
        cJSON_DeleteItemFromObject(cJSON_GetObjectItem(root, "data"), keys[i]); rejects(root, 1);
    }
    const double values[] = {-1,0.5,9007199254740992.0};
    for (unsigned i = 0; i < 3; i++) {
        cJSON *root = fixture(1, 30), *data = cJSON_GetObjectItem(root, "data");
        cJSON *bucket = cJSON_GetArrayItem(cJSON_GetObjectItem(data, "daily_usage"), 0);
        cJSON_SetNumberValue(cJSON_GetObjectItem(bucket, "tokens"), values[i]); rejects(root, 1);
    }
    cJSON *root = fixture(0, 30), *data = cJSON_GetObjectItem(root, "data");
    cJSON_AddNumberToObject(data, "available_days", 30); rejects(root, 0);
    root = fixture(0, 30); data = cJSON_GetObjectItem(root, "data");
    cJSON_SetNumberValue(cJSON_GetObjectItem(data, "total_tokens"), 0); rejects(root, 0);
    root = fixture(0, 30); data = cJSON_GetObjectItem(root, "data");
    cJSON_SetNumberValue(cJSON_GetObjectItem(data, "available_days"), 29); rejects(root, 0);
    root = fixture(0, 30); data = cJSON_GetObjectItem(root, "data");
    cJSON *days = cJSON_GetObjectItem(data, "daily_usage");
    cJSON_ReplaceItemInObject(cJSON_GetArrayItem(days, 0), "date", cJSON_CreateString("2026-09-05")); rejects(root, 0);
    root = fixture(0, 30); data = cJSON_GetObjectItem(root, "data");
    cJSON_DeleteItemFromArray(cJSON_GetObjectItem(data, "daily_usage"), 0); rejects(root, 0);
    root = fixture(0, 30); cJSON_ReplaceItemInObject(root, "data", cJSON_CreateNull()); rejects(root, 0);
    root = fixture(0, 30); cJSON_ReplaceItemInObject(root, "success", cJSON_CreateFalse()); rejects(root, 0);
    root = fixture(0, 30); rejects(root, 1);
}

static void bounds(void) {
    cJSON *root = fixture(0, 30);
    char *json = cJSON_PrintUnformatted(root);
    ai_tokens_data_t data;
    assert(!ai_tokens_decode_response(json, strlen(json) - 3, 0, &data));
    assert(!ai_tokens_decode_response(json, strlen(json), 2, &data));
    char oversized[WORKOUT_JSON_LIMIT + 2];
    memset(oversized, ' ', sizeof(oversized));
    assert(!ai_tokens_decode_response(oversized, sizeof(oversized), 0, &data));
    cJSON_free(json); cJSON_Delete(root);
}

int main(void) {
    valid_data(); malformed_data(); bounds();
    puts("AI tokens JSON: PASS");
    return 0;
}
