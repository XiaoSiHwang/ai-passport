#pragma once

#include "workout_model.h"

#define WELLNESS_DAYS 7
#define WELLNESS_JSON_LIMIT 16384

typedef enum {
    WELLNESS_ENERGY, WELLNESS_SLEEP, WELLNESS_HR, WELLNESS_HRV,
    WELLNESS_WEIGHT, WELLNESS_STEPS, WELLNESS_SLEEP_SCORE, WELLNESS_SPO2,
    WELLNESS_METRICS,
} wellness_metric_t;

typedef struct {
    uint32_t values[WELLNESS_METRICS]; /* Tenths, except sleep seconds and steps. */
    uint16_t present, known; /* Missing, explicit null and zero remain distinct. */
    bool recorded;
} wellness_record_t;

typedef struct {
    wellness_record_t days[WELLNESS_DAYS];
    int32_t newest;
    char fetched_at[40];
    bool stale, complete, degraded;
} wellness_data_t;

typedef struct {
    wellness_data_t data;
    bool available, syncing, failed;
    int http_status;
    int64_t received_ms;
} wellness_state_t;

bool wellness_url(const char *server, int32_t today, char output[WORKOUT_URL_SIZE]);
bool wellness_decode_response(const char *json, size_t length, wellness_data_t *data);
const char *wellness_metric_name(wellness_metric_t metric);
const char *wellness_metric_unit(wellness_metric_t metric);
void wellness_format(const wellness_record_t *record, wellness_metric_t metric,
                     char *output, size_t size);
