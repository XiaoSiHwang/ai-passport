#include "codex_monitor.h"

#include <stdio.h>
#include <string.h>

bool codex_monitor_url(const char *workout_url, bool alerts, bool baseline,
                       uint32_t cursor, char output[CODEX_URL_SIZE]) {
    char normalized[WORKOUT_URL_SIZE];
    if (!workout_url || !workout_normalize_url(workout_url, normalized)
        || strcmp(normalized, workout_url) != 0 || cursor > INT32_MAX) return false;
    size_t prefix = strlen(normalized) - strlen("/api/workout");
    char candidate[CODEX_URL_SIZE];
    int length = snprintf(candidate, sizeof(candidate), "%.*s/api/codex/%s",
                          (int)prefix, normalized, alerts ? "alerts" : "tasks");
    if (alerts && !baseline)
        length += snprintf(candidate + length, sizeof(candidate) - (size_t)length,
                           "?after=%lu&limit=5", (unsigned long)cursor);
    if (length < 0 || (size_t)length >= sizeof(candidate)) return false;
    memcpy(output, candidate, (size_t)length + 1);
    return true;
}

bool codex_cursor_stream(codex_cursor_t *cursor, const char *stream_id) {
    if (strcmp(cursor->stream_id, stream_id) == 0) return false;
    memset(cursor, 0, sizeof(*cursor));
    snprintf(cursor->stream_id, sizeof(cursor->stream_id), "%s", stream_id);
    return true;
}

bool codex_cursor_baseline(codex_cursor_t *cursor, const codex_alert_page_t *page) {
    if (page->count || page->has_more || page->next_cursor != page->latest_cursor) return false;
    (void)codex_cursor_stream(cursor, page->stream_id);
    cursor->cursor = page->next_cursor;
    cursor->baseline = true;
    return true;
}

bool codex_alert_seen(const codex_cursor_t *cursor, const codex_alert_t *alert) {
    if (alert->cursor <= cursor->cursor) return true;
    for (unsigned i = 0; i < cursor->seen_count; i++)
        if (strcmp(cursor->seen[i], alert->alert_id) == 0) return true;
    return false;
}

bool codex_alert_notify(const codex_alert_t *alert) {
    return alert->fresh && alert->state == CODEX_APPROVAL_REQUESTED;
}

void codex_cursor_accept(codex_cursor_t *cursor, const codex_alert_t *alert) {
    if (!codex_alert_seen(cursor, alert)) {
        strcpy(cursor->seen[cursor->seen_next], alert->alert_id);
        cursor->seen_next = (cursor->seen_next + 1) % CODEX_SEEN_LIMIT;
        if (cursor->seen_count < CODEX_SEEN_LIMIT) cursor->seen_count++;
    }
    if (alert->cursor > cursor->cursor) cursor->cursor = alert->cursor;
}

bool codex_monitor_synced(const codex_monitor_state_t *state, bool connected, int64_t now_ms) {
    return connected && state->available && !state->failed && now_ms >= state->received_ms
        && now_ms - state->received_ms <= CODEX_FRESH_MS;
}

bool codex_monitor_fresh(const codex_monitor_state_t *state, const codex_task_t *task,
                         bool connected, int64_t now_ms) {
    return codex_monitor_synced(state, connected, now_ms) && state->data.source_online
        && task->fresh && task->status != CODEX_TASK_UNKNOWN && task->approval != CODEX_APPROVAL_UNKNOWN;
}

bool codex_task_finished(const codex_task_t *task) {
    return task->status == CODEX_TASK_ENDED || task->status == CODEX_TASK_INTERRUPTED;
}

unsigned codex_monitor_selection(const codex_tasks_data_t *data, const char *task_id) {
    for (unsigned i = 0; i < data->count; i++)
        if (strcmp(data->tasks[i].task_id, task_id) == 0) return i;
    return 0;
}

static const codex_task_clock_t *task_clock(const codex_clock_t *clock, const codex_task_t *task) {
    for (unsigned i = 0; i < CODEX_TASK_LIMIT; i++)
        if (clock->tasks[i].started_seconds == task->started_seconds
            && strcmp(clock->tasks[i].task_id, task->task_id) == 0) return &clock->tasks[i];
    return NULL;
}

static void task_clock_update(codex_task_clock_t *clock, const codex_task_t *task,
                              const codex_monitor_state_t *state) {
    bool ticking = (task->status == CODEX_TASK_RUNNING || task->status == CODEX_TASK_APPROVAL)
        && task->approval != CODEX_APPROVAL_UNKNOWN;
    int64_t end = ticking ? state->data.generated_seconds : task->updated_seconds;
    int64_t elapsed = end - task->started_seconds;
    if (elapsed < 0 || elapsed > 366 * 86400) { clock->valid = false; return; }
    /* Keep the original phase and never rewind an active clock on slow/rounded snapshots. */
    if (ticking && clock->valid && clock->ticking && state->received_ms >= clock->anchor_ms
        && elapsed * 1000 <= clock->elapsed_ms + state->received_ms - clock->anchor_ms) return;
    strcpy(clock->task_id, task->task_id);
    clock->started_seconds = task->started_seconds;
    clock->elapsed_ms = elapsed * 1000;
    clock->anchor_ms = state->received_ms;
    clock->valid = true;
    clock->ticking = ticking;
}

void codex_monitor_clock_update(codex_clock_t *clock, const codex_monitor_state_t *state) {
    if (!state->available) { memset(clock, 0, sizeof(*clock)); return; }
    bool same_stream = clock->available && strcmp(clock->stream_id, state->data.stream_id) == 0;
    if (state->failed || (same_stream && clock->received_ms == state->received_ms)) return;
    codex_clock_t next = {.available = true, .received_ms = state->received_ms};
    strcpy(next.stream_id, state->data.stream_id);
    for (unsigned i = 0; i < state->data.count; i++) {
        const codex_task_t *task = &state->data.tasks[i];
        const codex_task_clock_t *previous = same_stream ? task_clock(clock, task) : NULL;
        if (previous) next.tasks[i] = *previous;
        task_clock_update(&next.tasks[i], task, state);
    }
    *clock = next;
}

bool codex_monitor_elapsed(const codex_clock_t *clock, const codex_task_t *task,
                           int64_t now_ms, uint32_t *seconds) {
    const codex_task_clock_t *timer = clock->available ? task_clock(clock, task) : NULL;
    if (!timer || !timer->valid || now_ms < timer->anchor_ms) return false;
    int64_t elapsed = timer->elapsed_ms + (timer->ticking ? now_ms - timer->anchor_ms : 0);
    if (elapsed > (int64_t)366 * 86400 * 1000) return false;
    *seconds = (uint32_t)(elapsed / 1000);
    return true;
}

bool codex_alert_finished(const codex_monitor_state_t *state, const codex_alert_t *alert) {
    if (!state->available) return false;
    if (strcmp(state->data.stream_id, alert->stream_id) != 0) return true;
    if (state->received_ms <= alert->received_ms) return false;
    for (unsigned i = 0; i < state->data.count; i++) {
        const codex_task_t *task = &state->data.tasks[i];
        if (strcmp(task->task_id, alert->task_id) != 0) continue;
        if (!task->fresh || task->approval == CODEX_APPROVAL_UNKNOWN) return true;
        return strcmp(task->approval_id, alert->approval_id) != 0
            || task->approval != CODEX_APPROVAL_REQUESTED;
    }
    return false; /* The task may be outside the three-item response. */
}

bool codex_alert_current(const codex_monitor_state_t *state, const codex_alert_t *alert,
                         bool connected, int64_t now_ms) {
    if (!connected || !codex_alert_notify(alert) || strcmp(state->data.stream_id, alert->stream_id) != 0) return false;
    if (state->checked_ms > alert->received_ms && state->failed) return false;
    if (state->available && state->received_ms > alert->received_ms
        && (!state->data.source_online || codex_alert_finished(state, alert))) return false;
    if (now_ms >= alert->received_ms && now_ms - alert->received_ms <= CODEX_FRESH_MS) return true;
    for (unsigned i = 0; i < state->data.count; i++) {
        const codex_task_t *task = &state->data.tasks[i];
        if (strcmp(task->task_id, alert->task_id) == 0 && strcmp(task->approval_id, alert->approval_id) == 0
            && task->approval == CODEX_APPROVAL_REQUESTED)
            return codex_monitor_fresh(state, task, connected, now_ms);
    }
    return false;
}
