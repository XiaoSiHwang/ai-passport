#include "wellness.h"

#include <stdio.h>
#include <string.h>

static const char *s_names[] = {"身体电量", "睡眠时长", "静息心率", "HRV",
    "体重", "步数", "睡眠评分", "血氧"};
static const char *s_units[] = {"/100", "", "bpm", "ms", "kg", "步", "/100", "%"};

const char *wellness_metric_name(wellness_metric_t metric) { return s_names[metric]; }
const char *wellness_metric_unit(wellness_metric_t metric) { return s_units[metric]; }

bool wellness_url(const char *server, int32_t today, char output[WORKOUT_URL_SIZE]) {
    char normalized[WORKOUT_URL_SIZE];
    passport_calendar_t oldest, newest;
    if (!workout_normalize_url(server, normalized)
        || !passport_calendar_get(today - WELLNESS_DAYS + 1, &oldest)
        || !passport_calendar_get(today, &newest)) return false;
    normalized[strlen(normalized) - strlen("/api/workout")] = '\0';
    int length = snprintf(output, WORKOUT_URL_SIZE,
        "%s/api/wellness?oldest=%04u-%02u-%02u&newest=%04u-%02u-%02u", normalized,
        oldest.year, oldest.month, oldest.day, newest.year, newest.month, newest.day);
    return length > 0 && length < WORKOUT_URL_SIZE;
}

void wellness_format(const wellness_record_t *record, wellness_metric_t metric,
                     char *output, size_t size) {
    unsigned bit = 1U << metric;
    if (!(record->present & bit)) { snprintf(output, size, "未提供"); return; }
    if (!(record->known & bit)) { snprintf(output, size, "空值"); return; }
    unsigned long value = record->values[metric];
    if (metric == WELLNESS_SLEEP) snprintf(output, size, "%lu时%02lu分", value / 3600, value / 60 % 60);
    else if (metric == WELLNESS_STEPS) snprintf(output, size, "%lu", value);
    else if (value % 10) snprintf(output, size, "%lu.%lu", value / 10, value % 10);
    else snprintf(output, size, "%lu", value / 10);
}
