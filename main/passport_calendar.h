#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PASSPORT_CALENDAR_FIRST_DAY 10957 /* 2000-01-01, days since Unix epoch. */
#define PASSPORT_CALENDAR_LAST_DAY 47481  /* 2099-12-31. */

typedef struct {
    bool valid;
    int32_t day;
    unsigned hour, minute;
} passport_clock_t;

typedef struct {
    unsigned year, month, day, weekday; /* Monday = 0. */
    unsigned lunar_year, lunar_month, lunar_day;
    bool leap_month, auspicious;
    const char *deity, *term, *yi, *ji;
    const char *festival;
    bool holiday, workday;
    unsigned holiday_day;
    char lunar[32], year_ganzhi[16], day_ganzhi[16];
} passport_calendar_t;

/* Pure logic; no allocations, platform calls or process-global timezone changes.
 * The clock uses UTC+8 and stays invalid until it has a plausible 2024+ epoch.
 * Calendar/huangli coverage is 2000..2099; annual statutory arrangements are 2026 only. */
bool passport_calendar_clock(int64_t utc_seconds, passport_clock_t *clock);
bool passport_calendar_get(int32_t day, passport_calendar_t *calendar);
unsigned passport_calendar_month_days(unsigned year, unsigned month);
unsigned passport_calendar_month_offset(const passport_calendar_t *calendar);
int32_t passport_calendar_shift(int32_t day, int delta);
const char *passport_calendar_lunar_day(unsigned day);
