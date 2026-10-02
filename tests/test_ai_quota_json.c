#include "workout_json.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_json[WORKOUT_JSON_LIMIT + 1];

static cJSON *fixture(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file);
    size_t size = fread(s_json, 1, WORKOUT_JSON_LIMIT, file);
    fclose(file); s_json[size] = 0;
    cJSON *root = cJSON_Parse(s_json);
    assert(root); return root;
}

static bool decode(cJSON *root, unsigned provider, ai_quota_data_t *output) {
    char *text = cJSON_PrintUnformatted(root);
    assert(text);
    bool result = ai_quota_decode_response(text, strlen(text), provider, output);
    free(text); return result;
}

static void codex(void) {
    cJSON *root = fixture("tests/fixtures/codex-quota.json");
    ai_quota_data_t result = {0};
    assert(decode(root, 0, &result));
    assert(result.windows[0].available && result.windows[0].remaining == 725);
    assert(result.windows[1].remaining == 580 && !result.stale);
    cJSON *data = cJSON_GetObjectItem(root, "data");
    cJSON *window = cJSON_GetObjectItem(data, "five_hour");
    ai_quota_data_t saved = result;
    cJSON_AddNumberToObject(window, "remaining_percent", 50);
    assert(!decode(root, 0, &result) && memcmp(&saved, &result, sizeof(result)) == 0);
    cJSON_DeleteItemFromObject(window, "remaining_percent");
    cJSON_SetNumberValue(cJSON_GetObjectItem(window, "remaining_percent"), 101);
    assert(!decode(root, 0, &result));
    cJSON_SetNumberValue(cJSON_GetObjectItem(window, "remaining_percent"), -1);
    assert(!decode(root, 0, &result));
    cJSON_ReplaceItemInObject(data, "five_hour", cJSON_CreateNull());
    assert(decode(root, 0, &result) && !result.windows[0].available && result.windows[1].available);
    cJSON_ReplaceItemInObject(data, "seven_day", cJSON_CreateNull());
    assert(!decode(root, 0, &result));
    cJSON_Delete(root);
    root = fixture("tests/fixtures/codex-quota.json"); /* Use original bytes for truncation checks. */
    cJSON_Delete(root);
    assert(!ai_quota_decode_response(s_json, strlen(s_json) - 3, 0, &result));
    assert(!ai_quota_decode_response(s_json, strlen(s_json), 2, &result));
}

static void glm(void) {
    cJSON *root = fixture("tests/fixtures/glm-quota.json");
    ai_quota_data_t result;
    assert(decode(root, 1, &result));
    assert(result.windows[0].available && result.windows[0].remaining == 0);
    assert(!result.windows[0].reset_at[0] && result.stale && strcmp(result.level, "PRO") == 0);
    cJSON *data = cJSON_GetObjectItem(root, "data");
    cJSON_ReplaceItemInObject(data, "level", cJSON_CreateString("任意套餐名称"));
    assert(decode(root, 1, &result) && !result.level[0]);
    cJSON_ReplaceItemInObject(data, "fetched_at", cJSON_CreateString("2026-99-02T14:32:00+08:00"));
    assert(!decode(root, 1, &result));
    cJSON_Delete(root);
    const char error[] = "{\"success\":false,\"data\":null}";
    assert(!ai_quota_decode_response(error, sizeof(error) - 1, 1, &result));
}

int main(void) {
    codex(); glm();
    puts("AI quota JSON: PASS");
    return 0;
}
