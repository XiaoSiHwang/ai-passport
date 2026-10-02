#include "codex_monitor.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void urls(void) {
    char output[CODEX_URL_SIZE] = "unchanged";
    assert(codex_monitor_url("http://example.com:8000/passport/api/workout", false, false, 0, output));
    assert(strcmp(output, "http://example.com:8000/passport/api/codex/tasks") == 0);
    assert(codex_monitor_url("https://example.com/api/workout", true, true, 0, output));
    assert(strcmp(output, "https://example.com/api/codex/alerts") == 0);
    assert(codex_monitor_url("https://example.com/api/workout", true, false, INT32_MAX, output));
    assert(strstr(output, "?after=2147483647&limit=5"));
    char before[CODEX_URL_SIZE]; strcpy(before, output);
    assert(!codex_monitor_url("http://127.0.0.1/api/workout", true, false, 0, output));
    assert(!codex_monitor_url("http://example.com", false, false, 0, output));
    assert(!codex_monitor_url("http://example.com/api/workout", true, false, UINT32_MAX, output));
    assert(strcmp(before, output) == 0);
}

static void cursors(void) {
    codex_cursor_t cursor = {0};
    codex_alert_page_t page = {.stream_id = "stream_a", .next_cursor = 30, .latest_cursor = 30};
    assert(codex_cursor_baseline(&cursor, &page) && cursor.baseline && cursor.cursor == 30);
    codex_alert_t alert = {.cursor = 31, .state = CODEX_APPROVAL_REQUESTED, .fresh = true,
                           .alert_id = "alert_31", .stream_id = "stream_a"};
    assert(!codex_alert_seen(&cursor, &alert) && codex_alert_notify(&alert));
    codex_cursor_accept(&cursor, &alert);
    assert(codex_alert_seen(&cursor, &alert));
    alert.cursor = 32; /* Same ID at a newer cursor must still not notify twice. */
    assert(codex_alert_seen(&cursor, &alert));
    codex_cursor_accept(&cursor, &alert); assert(cursor.cursor == 32);
    alert.state = CODEX_APPROVAL_UNKNOWN; assert(!codex_alert_notify(&alert));
    alert.state = CODEX_APPROVAL_RESOLVED; assert(!codex_alert_notify(&alert));
    alert.state = CODEX_APPROVAL_SUPERSEDED; assert(!codex_alert_notify(&alert));
    alert.state = CODEX_APPROVAL_REQUESTED; alert.fresh = false; assert(!codex_alert_notify(&alert));
    assert(!codex_cursor_stream(&cursor, "stream_a"));
    assert(codex_cursor_stream(&cursor, "stream_b") && !cursor.baseline && !cursor.cursor && !cursor.seen_count);
    page.count = 1; assert(!codex_cursor_baseline(&cursor, &page));
    page.count = 0; page.has_more = true; assert(!codex_cursor_baseline(&cursor, &page));
}

static void freshness_and_time(void) {
    codex_monitor_state_t state = {.available = true, .received_ms = 1000};
    state.data.source_online = true; state.data.generated_seconds = 200;
    codex_task_t task = {.fresh = true, .status = CODEX_TASK_RUNNING, .started_seconds = 100, .updated_seconds = 140};
    uint32_t seconds;
    assert(codex_monitor_fresh(&state, &task, true, 16000));
    assert(!codex_monitor_fresh(&state, &task, true, 16001));
    assert(!codex_monitor_fresh(&state, &task, false, 1000));
    assert(codex_monitor_elapsed(&state, &task, true, 4000, &seconds) && seconds == 103);
    assert(codex_monitor_elapsed(&state, &task, false, 4000, &seconds) && seconds == 100);
    task.status = CODEX_TASK_ENDED;
    assert(codex_monitor_elapsed(&state, &task, true, 4000, &seconds) && seconds == 40);
    task.updated_seconds = 90; assert(!codex_monitor_elapsed(&state, &task, true, 4000, &seconds));
    task.status = CODEX_TASK_RUNNING; task.approval = CODEX_APPROVAL_UNKNOWN;
    assert(!codex_monitor_fresh(&state, &task, true, 1000));
    task.approval = CODEX_APPROVAL_NONE; state.failed = true;
    assert(!codex_monitor_fresh(&state, &task, true, 1000));
    state.failed = false; state.data.source_online = false;
    assert(!codex_monitor_fresh(&state, &task, true, 1000));
}

static void navigation_and_resolution(void) {
    workout_navigation_t nav = {.view = WORKOUT_VIEW_CODEX, .codex_count = 3};
    workout_navigate(&nav, WORKOUT_INPUT_UP); assert(nav.codex_selection == 2);
    workout_navigate(&nav, WORKOUT_INPUT_OK); assert(nav.view == WORKOUT_VIEW_CODEX_DETAILS);
    workout_navigate(&nav, WORKOUT_INPUT_DOWN); assert(nav.codex_selection == 0);
    workout_navigate(&nav, WORKOUT_INPUT_OK); assert(nav.view == WORKOUT_VIEW_CODEX);
    nav.codex_count = 0; workout_navigate(&nav, WORKOUT_INPUT_OK); assert(nav.view == WORKOUT_VIEW_CODEX);
    workout_navigate(&nav, WORKOUT_INPUT_MENU); assert(nav.view == WORKOUT_VIEW_MENU);
    codex_monitor_state_t state = {.available = true, .received_ms = 50};
    strcpy(state.data.stream_id, "stream_a"); state.data.count = 2;
    strcpy(state.data.tasks[0].task_id, "task_b"); strcpy(state.data.tasks[1].task_id, "task_a");
    assert(codex_monitor_selection(&state.data, "task_a") == 1);
    assert(codex_monitor_selection(&state.data, "gone") == 0);
    codex_alert_t alert = {.stream_id = "stream_a", .task_id = "task_a", .approval_id = "approval_a", .received_ms = 60};
    assert(!codex_alert_finished(&state, &alert)); /* An older snapshot cannot dismiss a new request. */
    state.received_ms = 70; state.data.tasks[1].fresh = true;
    assert(codex_alert_finished(&state, &alert)); /* Tool returned within the same wall-clock second. */
    state.data.tasks[1].approval = CODEX_APPROVAL_REQUESTED;
    strcpy(state.data.tasks[1].approval_id, "approval_a");
    assert(!codex_alert_finished(&state, &alert));
    state.data.tasks[1].approval = CODEX_APPROVAL_UNKNOWN;
    assert(codex_alert_finished(&state, &alert));
    strcpy(state.data.stream_id, "stream_b"); assert(codex_alert_finished(&state, &alert));
}

int main(void) {
    urls(); cursors(); freshness_and_time(); navigation_and_resolution();
    printf("Codex monitor: PASS; snapshot=%zu alert=%zu cursor=%zu bytes\n",
           sizeof(codex_monitor_state_t), sizeof(codex_alert_t), sizeof(codex_cursor_t));
    return 0;
}
