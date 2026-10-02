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

static void freshness(void) {
    codex_monitor_state_t state = {.available = true, .received_ms = 1000};
    state.data.source_online = true; state.data.generated_seconds = 200;
    codex_task_t task = {.fresh = true, .status = CODEX_TASK_RUNNING, .started_seconds = 100, .updated_seconds = 140};
    assert(codex_monitor_fresh(&state, &task, true, 16000));
    assert(!codex_monitor_fresh(&state, &task, true, 16001));
    assert(!codex_monitor_fresh(&state, &task, false, 1000));
    task.fresh = false; task.status = CODEX_TASK_ENDED;
    assert(codex_monitor_synced(&state, true, 1000) && codex_task_finished(&task));
    assert(!codex_monitor_fresh(&state, &task, true, 1000));
    task.status = CODEX_TASK_INTERRUPTED; assert(codex_task_finished(&task));
    task.fresh = true;
    task.status = CODEX_TASK_RUNNING; task.approval = CODEX_APPROVAL_UNKNOWN;
    assert(!codex_monitor_fresh(&state, &task, true, 1000));
    task.approval = CODEX_APPROVAL_NONE; state.failed = true;
    assert(!codex_monitor_fresh(&state, &task, true, 1000));
    state.failed = false; state.data.source_online = false;
    assert(!codex_monitor_fresh(&state, &task, true, 1000));
}

static codex_monitor_state_t clock_fixture(void) {
    codex_monitor_state_t state = {.available = true, .received_ms = 1250};
    strcpy(state.data.stream_id, "stream_a"); state.data.count = 1; state.data.generated_seconds = 200;
    state.data.tasks[0] = (codex_task_t){.task_id = "task_a", .status = CODEX_TASK_RUNNING,
        .started_seconds = 100, .updated_seconds = 140};
    return state; /* Incomplete/offline collector evidence must not stall an active local timer. */
}

static void elapsed_is(const codex_clock_t *clock, const codex_task_t *task, int64_t now, uint32_t value) {
    uint32_t seconds;
    assert(codex_monitor_elapsed(clock, task, now, &seconds) && seconds == value);
}

static void continuous_time(void) {
    codex_monitor_state_t state = clock_fixture();
    codex_task_t *task = &state.data.tasks[0];
    codex_clock_t clock = {0};
    codex_monitor_clock_update(&clock, &state);
    elapsed_is(&clock, task, 2249, 100); elapsed_is(&clock, task, 2250, 101);
    /* Repeated rounded/delayed snapshots must preserve the fraction of a second. */
    for (unsigned i = 1; i <= 25; i++) {
        state.received_ms = 1250 + i * 100;
        codex_monitor_clock_update(&clock, &state);
        elapsed_is(&clock, task, state.received_ms, 100 + i / 10);
    }
    assert(clock.tasks[0].anchor_ms == 1250);
    state.failed = true; codex_monitor_clock_update(&clock, &state);
    elapsed_is(&clock, task, 61250, 160); /* No further successful API call for a minute. */
    state.failed = false; state.received_ms = 61250;
    task->status = CODEX_TASK_APPROVAL; task->approval = CODEX_APPROVAL_REQUESTED;
    codex_monitor_clock_update(&clock, &state);
    elapsed_is(&clock, task, 62250, 161); /* Waiting for approval counts toward total turn time. */
    state.received_ms = 62250; task->status = CODEX_TASK_RUNNING; task->approval = CODEX_APPROVAL_NONE;
    state.data.generated_seconds = 270; codex_monitor_clock_update(&clock, &state);
    elapsed_is(&clock, task, 63250, 171); /* Correct forwards from a newer server timestamp. */
    state.received_ms = 63250; task->status = CODEX_TASK_ENDED; task->updated_seconds = 180;
    codex_monitor_clock_update(&clock, &state);
    elapsed_is(&clock, task, 120000, 80); /* Authoritative stop time can correct the earlier estimate. */
    state.received_ms++; task->status = CODEX_TASK_INTERRUPTED; task->updated_seconds = 190;
    codex_monitor_clock_update(&clock, &state); elapsed_is(&clock, task, 180000, 90);
}

static void clock_identity_and_invalid_time(void) {
    codex_monitor_state_t state = clock_fixture();
    codex_clock_t clock = {0};
    state.data.count = 2; state.data.tasks[1] = state.data.tasks[0];
    strcpy(state.data.tasks[1].task_id, "task_b"); state.data.tasks[1].started_seconds = 150;
    codex_monitor_clock_update(&clock, &state);
    codex_task_t swap = state.data.tasks[0];
    state.data.tasks[0] = state.data.tasks[1]; state.data.tasks[1] = swap;
    state.received_ms = 4250; codex_monitor_clock_update(&clock, &state);
    elapsed_is(&clock, &state.data.tasks[0], 5250, 54);
    elapsed_is(&clock, &state.data.tasks[1], 5250, 104);
    state.received_ms = 5250; state.data.tasks[1].started_seconds = 199;
    codex_monitor_clock_update(&clock, &state); elapsed_is(&clock, &state.data.tasks[1], 6250, 2);
    strcpy(state.data.stream_id, "stream_b"); state.received_ms = 6250;
    codex_monitor_clock_update(&clock, &state); elapsed_is(&clock, &state.data.tasks[0], 7250, 51);
    state.received_ms = 7250; state.data.tasks[0].approval = CODEX_APPROVAL_UNKNOWN;
    state.data.tasks[0].updated_seconds = 180;
    codex_monitor_clock_update(&clock, &state); elapsed_is(&clock, &state.data.tasks[0], 20000, 30);
    uint32_t seconds;
    state.received_ms++; state.data.tasks[0].updated_seconds = 140;
    codex_monitor_clock_update(&clock, &state);
    assert(!codex_monitor_elapsed(&clock, &state.data.tasks[0], 20000, &seconds));
    state.received_ms++; state.data.tasks[0].updated_seconds = 180;
    codex_monitor_clock_update(&clock, &state); elapsed_is(&clock, &state.data.tasks[0], 20000, 30);
    assert(!codex_monitor_elapsed(&clock, &state.data.tasks[0], 0, &seconds));
    state.received_ms++; state.data.count = 0; codex_monitor_clock_update(&clock, &state);
    assert(!codex_monitor_elapsed(&clock, &swap, 20000, &seconds));
    state.available = false; codex_monitor_clock_update(&clock, &state); assert(!clock.available);
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
    urls(); cursors(); freshness(); continuous_time(); clock_identity_and_invalid_time(); navigation_and_resolution();
    printf("Codex monitor: PASS; snapshot=%zu alert=%zu cursor=%zu clocks=%zu bytes\n",
           sizeof(codex_monitor_state_t), sizeof(codex_alert_t), sizeof(codex_cursor_t), sizeof(codex_clock_t));
    return 0;
}
