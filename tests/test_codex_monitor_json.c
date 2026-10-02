#include "codex_monitor.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *fixture(const char *name) {
    char path[120]; snprintf(path, sizeof(path), "tests/fixtures/codex-%s.json", name);
    FILE *file = fopen(path, "rb"); assert(file);
    char *text = calloc(1, 4097); assert(text);
    size_t length = fread(text, 1, 4096, file); assert(length && feof(file));
    fclose(file); return text;
}

static cJSON *envelope(const char *name) {
    char *text = fixture(name); cJSON *root = cJSON_Parse(text); free(text); assert(root); return root;
}

static cJSON *task(cJSON *root) {
    return cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "data"), "tasks"), 0);
}

static void assert_tasks(cJSON *root, bool expected) {
    char *text = cJSON_PrintUnformatted(root); assert(text);
    codex_tasks_data_t data, before; memset(&data, 0xA5, sizeof(data)); before = data;
    assert(codex_tasks_decode(text, strlen(text), &data) == expected);
    if (!expected) assert(memcmp(&data, &before, sizeof(data)) == 0);
    free(text); cJSON_Delete(root);
}

static void assert_alerts(cJSON *root, bool expected) {
    char *text = cJSON_PrintUnformatted(root); assert(text);
    codex_alert_page_t data, before; memset(&data, 0xA5, sizeof(data)); before = data;
    assert(codex_alerts_decode(text, strlen(text), &data) == expected);
    if (!expected) assert(memcmp(&data, &before, sizeof(data)) == 0);
    free(text); cJSON_Delete(root);
}

static void valid_fixtures(void) {
    char *text = fixture("approval-tasks");
    codex_tasks_data_t data;
    assert(codex_tasks_decode(text, strlen(text), &data)); free(text);
    assert(data.count == 1 && data.active_count == 1 && data.pending_count == 1 && data.revision == 3);
    assert(strcmp(data.tasks[0].title, "增加 Codex 任务与审批提醒") == 0);
    assert(data.tasks[0].approval == CODEX_APPROVAL_REQUESTED && data.tasks[0].fresh);
    text = fixture("ended-tasks"); assert(codex_tasks_decode(text, strlen(text), &data)); free(text);
    assert(data.tasks[0].status == CODEX_TASK_ENDED && data.tasks[0].approval == CODEX_APPROVAL_NONE);
    codex_alert_page_t alerts;
    text = fixture("approval-alerts"); assert(codex_alerts_decode(text, strlen(text), &alerts)); free(text);
    assert(alerts.count == 1 && alerts.next_cursor == 1 && alerts.alerts[0].fresh);
    assert(alerts.alerts[0].state == CODEX_APPROVAL_REQUESTED);
    text = fixture("resolved-alerts"); assert(codex_alerts_decode(text, strlen(text), &alerts)); free(text);
    assert(alerts.alerts[0].state == CODEX_APPROVAL_RESOLVED);
}

static void invalid_tasks(void) {
    const char *bad_keys[] = {"task_id", "status", "fresh", "started_at", "pending_approval"};
    for (unsigned i = 0; i < sizeof(bad_keys) / sizeof(bad_keys[0]); i++) {
        cJSON *root = envelope("approval-tasks"); cJSON_DeleteItemFromObject(task(root), bad_keys[i]); assert_tasks(root, false);
    }
    cJSON *root = envelope("approval-tasks");
    cJSON_AddStringToObject(task(root), "status", "running"); assert_tasks(root, false);
    root = envelope("approval-tasks"); cJSON_ReplaceItemInObject(task(root), "fresh", cJSON_CreateNumber(1)); assert_tasks(root, false);
    root = envelope("approval-tasks"); cJSON_ReplaceItemInObject(task(root), "status", cJSON_CreateString("approved")); assert_tasks(root, false);
    root = envelope("approval-tasks"); cJSON_ReplaceItemInObject(task(root), "task_id", cJSON_CreateString("_bad")); assert_tasks(root, false);
    root = envelope("approval-tasks"); cJSON_ReplaceItemInObject(task(root), "title", cJSON_CreateString("line\nline")); assert_tasks(root, false);
    root = envelope("approval-tasks");
    char long_title[100]; memset(long_title, 'W', 97); long_title[97] = 0;
    cJSON_ReplaceItemInObject(task(root), "title", cJSON_CreateString(long_title)); assert_tasks(root, false);
    root = envelope("approval-tasks");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON_ReplaceItemInObject(data, "revision", cJSON_CreateNumber(2147483648.0)); assert_tasks(root, false);
    root = envelope("approval-tasks"); data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON_ReplaceItemInObject(data, "schema_version", cJSON_CreateNumber(2)); assert_tasks(root, false);
    root = envelope("approval-tasks"); data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(data, "tasks"), cJSON_Duplicate(task(root), true)); assert_tasks(root, false);
}

static void unknown_and_bounds(void) {
    cJSON *root = envelope("approval-tasks");
    cJSON *approval = cJSON_GetObjectItemCaseSensitive(task(root), "pending_approval");
    cJSON_ReplaceItemInObject(approval, "state", cJSON_CreateString("unknown"));
    char *text = cJSON_PrintUnformatted(root); codex_tasks_data_t data;
    assert(codex_tasks_decode(text, strlen(text), &data) && !data.tasks[0].fresh);
    free(text); cJSON_Delete(root);
    root = envelope("approval-tasks"); cJSON *items = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "data"), "tasks");
    for (unsigned i = 1; i < 4; i++) {
        cJSON *copy = cJSON_Duplicate(task(root), true); char id[16]; snprintf(id, sizeof(id), "task_%u", i);
        cJSON_ReplaceItemInObject(copy, "task_id", cJSON_CreateString(id)); cJSON_AddItemToArray(items, copy);
    }
    assert_tasks(root, false);
    char oversized[4097]; memset(oversized, ' ', sizeof(oversized));
    assert(!codex_tasks_decode(oversized, sizeof(oversized), &data));
    assert(!codex_tasks_decode("{\"success\":true}junk", 20, &data));
}

static void alert_contract(void) {
    cJSON *root = envelope("approval-alerts");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON *items = cJSON_GetObjectItemCaseSensitive(data, "alerts");
    cJSON *alert = cJSON_GetArrayItem(items, 0);
    cJSON_AddItemToArray(items, cJSON_Duplicate(alert, true)); assert_alerts(root, false);
    root = envelope("approval-alerts"); data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON_ReplaceItemInObject(data, "has_more", cJSON_CreateTrue()); assert_alerts(root, false);
    root = envelope("approval-alerts"); data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON_ReplaceItemInObject(data, "next_cursor", cJSON_CreateNumber(0)); assert_alerts(root, false);
    root = envelope("approval-alerts"); data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON_ReplaceItemInObject(data, "alerts", cJSON_CreateArray()); assert_alerts(root, true); /* Silent baseline. */
    root = envelope("approval-alerts"); data = cJSON_GetObjectItemCaseSensitive(root, "data");
    items = cJSON_GetObjectItemCaseSensitive(data, "alerts"); alert = cJSON_GetArrayItem(items, 0);
    for (unsigned i = 2; i <= 5; i++) {
        cJSON *copy = cJSON_Duplicate(alert, true); char id[20]; snprintf(id, sizeof(id), "alert_%u", i);
        cJSON_ReplaceItemInObject(copy, "cursor", cJSON_CreateNumber(i));
        cJSON_ReplaceItemInObject(copy, "alert_id", cJSON_CreateString(id)); cJSON_AddItemToArray(items, copy);
    }
    cJSON_ReplaceItemInObject(data, "next_cursor", cJSON_CreateNumber(5));
    cJSON_ReplaceItemInObject(data, "latest_cursor", cJSON_CreateNumber(5)); assert_alerts(root, true);
}

int main(void) {
    valid_fixtures(); invalid_tasks(); unknown_and_bounds(); alert_contract();
    puts("Codex backend fixtures / actual cJSON: PASS"); return 0;
}
