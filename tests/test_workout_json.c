#include "workout_json.h"
#include "cJSON.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_fixture[WORKOUT_JSON_LIMIT + 1];

static void rejects(cJSON *root) {
    char *json = cJSON_PrintUnformatted(root);
    assert(json);
    workout_data_t data, before;
    memset(&data, 0x5a, sizeof(data));
    before = data;
    assert(!workout_decode_response(json, strlen(json), &data));
    assert(memcmp(&data, &before, sizeof(data)) == 0);
    cJSON_free(json);
    cJSON_Delete(root);
}

static cJSON *fixture(void) {
    cJSON *root = cJSON_Parse(s_fixture);
    assert(root);
    return root;
}

static void response(void) {
    workout_data_t data;
    assert(workout_decode_response(s_fixture, strlen(s_fixture), &data));
    assert(data.year == 2026 && data.month == 10 && data.week_count == 5);
    assert(data.weekly.distance == 246 && data.monthly.distance == 140 && data.days[4] == 140);
    cJSON *root = fixture();
    cJSON *object = cJSON_GetObjectItem(root, "data");
    cJSON_ReplaceItemInObject(object, "stale", cJSON_CreateTrue());
    char *json = cJSON_PrintUnformatted(root);
    assert(workout_decode_response(json, strlen(json), &data) && data.stale);
    cJSON_free(json); cJSON_Delete(root);
    root = fixture(); cJSON_ReplaceItemInObject(root, "success", cJSON_CreateFalse()); rejects(root);
    root = fixture(); cJSON_DeleteItemFromObject(cJSON_GetObjectItem(root, "data"), "weekly"); rejects(root);
    root = fixture(); object = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "data"), "weekly");
    cJSON_ReplaceItemInObject(object, "distance_km", cJSON_CreateNumber(-1)); rejects(root);
    root = fixture(); object = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "data"), "monthly");
    cJSON_ReplaceItemInObject(object, "duration_seconds", cJSON_CreateNumber(1.2)); rejects(root);
    root = fixture(); object = cJSON_GetObjectItem(root, "data");
    cJSON_ReplaceItemInObject(object, "updated_at", cJSON_CreateString("not-a-date")); rejects(root);
    root = fixture(); object = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "data"), "weekly");
    cJSON *days = cJSON_GetObjectItem(object, "days");
    cJSON_ReplaceItemInObject(cJSON_GetArrayItem(days, 1), "weekday", cJSON_CreateNumber(1)); rejects(root);
    root = fixture(); cJSON_AddBoolToObject(root, "success", true); rejects(root);
    assert(!workout_decode_response("{}junk", 6, &data));
    assert(!workout_decode_response(s_fixture, strlen(s_fixture) - 2, &data));
    assert(!workout_decode_response("[[[[[[[[[[[[0]]]]]]]]]]]]", 25, &data));
    assert(!workout_decode_response(s_fixture, WORKOUT_JSON_LIMIT + 1, &data));
}

static void configuration(void) {
    const char *json = "{\"ssid\":\"Example\",\"password\":\"\",\"server\":\"http://192.168.1.20:8000\",\"token\":\"0123456789abcdef0123456789abcdef\"}";
    workout_config_t config;
    char token[33];
    assert(workout_decode_config(json, strlen(json), &config, token));
    assert(strcmp(config.server, "http://192.168.1.20:8000/api/workout") == 0);
    assert(strcmp(token, "0123456789abcdef0123456789abcdef") == 0);
    cJSON *root = cJSON_Parse(json);
    cJSON_ReplaceItemInObject(root, "password", cJSON_CreateString("tiny"));
    char *bad = cJSON_PrintUnformatted(root);
    assert(!workout_decode_config(bad, strlen(bad), &config, token));
    cJSON_free(bad);
    cJSON_ReplaceItemInObject(root, "password", cJSON_CreateString("test-example"));
    cJSON_ReplaceItemInObject(root, "ssid", cJSON_CreateString("测试网络"));
    bad = cJSON_PrintUnformatted(root);
    assert(workout_decode_config(bad, strlen(bad), &config, token));
    assert(strcmp(config.ssid, "测试网络") == 0);
    cJSON_free(bad);
    cJSON_ReplaceItemInObject(root, "server", cJSON_CreateString("http://localhost:8000"));
    bad = cJSON_PrintUnformatted(root);
    assert(!workout_decode_config(bad, strlen(bad), &config, token));
    cJSON_free(bad); cJSON_Delete(root);
    const char *nul = "{\"ssid\":\"a\\u0000rest\"}";
    assert(!workout_decode_config(nul, strlen(nul), &config, token));
}

int main(void) {
    FILE *file = fopen("tests/fixtures/workout.json", "rb");
    assert(file);
    size_t size = fread(s_fixture, 1, WORKOUT_JSON_LIMIT, file);
    assert(size && feof(file));
    fclose(file);
    response(); configuration();
    puts("Workout JSON protocol: PASS");
    return 0;
}
