#pragma once

#include "workout_model.h"

#define CODEX_TASK_LIMIT 3
#define CODEX_ALERT_LIMIT 5
#define CODEX_ID_SIZE 65
#define CODEX_SEEN_LIMIT 16
#define CODEX_FRESH_MS 15000
#define CODEX_URL_SIZE (WORKOUT_URL_SIZE + 64)

typedef enum {
    CODEX_TASK_RUNNING, CODEX_TASK_APPROVAL, CODEX_TASK_ENDED,
    CODEX_TASK_INTERRUPTED, CODEX_TASK_UNKNOWN,
} codex_task_status_t;

typedef enum {
    CODEX_APPROVAL_NONE, CODEX_APPROVAL_REQUESTED, CODEX_APPROVAL_UNKNOWN,
    CODEX_APPROVAL_RESOLVED, CODEX_APPROVAL_SUPERSEDED,
} codex_approval_state_t;

typedef struct {
    char task_id[CODEX_ID_SIZE];
    char project[49], title[97], step[129];
    char approval_id[CODEX_ID_SIZE], approval_summary[161];
    codex_task_status_t status;
    codex_approval_state_t approval;
    bool fresh;
    int64_t started_seconds, updated_seconds;
} codex_task_t;

typedef struct {
    char stream_id[CODEX_ID_SIZE], generated_at[40];
    uint32_t revision, active_count, pending_count, omitted_count;
    int64_t generated_seconds;
    bool source_online;
    unsigned count;
    codex_task_t tasks[CODEX_TASK_LIMIT];
} codex_tasks_data_t;

typedef struct {
    char stream_id[CODEX_ID_SIZE], alert_id[CODEX_ID_SIZE];
    char approval_id[CODEX_ID_SIZE], task_id[CODEX_ID_SIZE];
    char project[49], title[97], summary[161];
    uint32_t cursor;
    int64_t created_seconds, received_ms;
    codex_approval_state_t state;
    bool fresh;
} codex_alert_t;

typedef struct {
    char stream_id[CODEX_ID_SIZE];
    uint32_t next_cursor, latest_cursor;
    bool has_more;
    unsigned count;
    codex_alert_t alerts[CODEX_ALERT_LIMIT];
} codex_alert_page_t;

typedef struct {
    char stream_id[CODEX_ID_SIZE];
    uint32_t cursor;
    bool baseline;
    unsigned seen_count, seen_next;
    char seen[CODEX_SEEN_LIMIT][CODEX_ID_SIZE];
} codex_cursor_t;

typedef struct {
    codex_tasks_data_t data;
    bool available, failed, alerts_failed;
    int http_status;
    int64_t received_ms, checked_ms;
} codex_monitor_state_t;

bool codex_monitor_url(const char *workout_url, bool alerts, bool baseline,
                       uint32_t cursor, char output[CODEX_URL_SIZE]);
bool codex_cursor_stream(codex_cursor_t *cursor, const char *stream_id);
bool codex_cursor_baseline(codex_cursor_t *cursor, const codex_alert_page_t *page);
bool codex_alert_seen(const codex_cursor_t *cursor, const codex_alert_t *alert);
bool codex_alert_notify(const codex_alert_t *alert);
void codex_cursor_accept(codex_cursor_t *cursor, const codex_alert_t *alert);
bool codex_monitor_fresh(const codex_monitor_state_t *state, const codex_task_t *task,
                         bool connected, int64_t now_ms);
unsigned codex_monitor_selection(const codex_tasks_data_t *data, const char *task_id);
bool codex_monitor_elapsed(const codex_monitor_state_t *state, const codex_task_t *task,
                           bool connected, int64_t now_ms, uint32_t *seconds);
bool codex_alert_finished(const codex_monitor_state_t *state, const codex_alert_t *alert);
bool codex_alert_current(const codex_monitor_state_t *state, const codex_alert_t *alert,
                         bool connected, int64_t now_ms);

/* Bounded JSON decoders leave the output untouched on failure. */
bool codex_tasks_decode(const char *json, size_t length, codex_tasks_data_t *data);
bool codex_alerts_decode(const char *json, size_t length, codex_alert_page_t *page);
