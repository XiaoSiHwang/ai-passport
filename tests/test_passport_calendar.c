#include "passport_calendar.h"
#include "workout_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int32_t day(const char *text) {
    int64_t value;
    assert(workout_parse_date(text, &value));
    return (int32_t)value;
}

static passport_calendar_t calendar(const char *text) {
    passport_calendar_t value;
    assert(passport_calendar_get(day(text), &value));
    return value;
}

static void known_dates(void) {
    passport_calendar_t value = calendar("2026-10-03");
    assert(value.year == 2026 && value.month == 10 && value.day == 3 && value.weekday == 5);
    assert(strcmp(value.lunar, "八月廿三") == 0 && strcmp(value.day_ganzhi, "庚戌") == 0);
    assert(!value.auspicious && strcmp(value.deity, "天牢") == 0);
    assert(value.holiday && !value.workday && value.holiday_day == 3);
    assert(strcmp(value.festival, "国庆假期") == 0 && strstr(value.yi, "嫁娶") && strstr(value.ji, "针灸"));
    value = calendar("2026-09-25");
    assert(strcmp(value.lunar, "八月十五") == 0 && strcmp(value.festival, "中秋节") == 0 && value.auspicious);
    value = calendar("2026-10-10");
    assert(strcmp(value.lunar, "九月初一") == 0 && strcmp(value.deity, "明堂") == 0 && value.auspicious);
    assert(value.workday && !value.holiday && strcmp(value.festival, "调休上班") == 0);
    value = calendar("2026-10-12");
    assert(!value.holiday && !value.workday && !value.festival[0] && !value.auspicious);
    value = calendar("2025-07-25");
    assert(value.leap_month && strcmp(value.lunar, "闰六月初一") == 0 && !value.festival[0]);
    value = calendar("2026-02-16");
    assert(strcmp(value.festival, "除夕") == 0 && strcmp(value.lunar, "腊月廿九") == 0);
    value = calendar("2026-02-17");
    assert(strcmp(value.festival, "春节") == 0 && strcmp(value.lunar, "正月初一") == 0);
    value = calendar("2026-10-08");
    assert(strcmp(value.term, "寒露") == 0 && !value.holiday);
    value = calendar("2027-10-03");
    assert(!value.holiday && !value.workday); /* Annual adjustments never leak into a different year. */
}

static void calendar_layout_and_bounds(void) {
    assert(passport_calendar_month_days(2024, 2) == 29);
    assert(passport_calendar_month_days(2100, 2) == 28);
    assert(passport_calendar_month_days(2000, 2) == 29);
    assert(!passport_calendar_month_days(2026, 0) && !passport_calendar_month_days(2026, 13));
    passport_calendar_t value = calendar("2026-08-01");
    assert(passport_calendar_month_offset(&value) == 5);
    assert((passport_calendar_month_offset(&value) + passport_calendar_month_days(2026, 8) + 6) / 7 == 6);
    assert(passport_calendar_shift(day("2026-09-30"), 1) == day("2026-10-01"));
    assert(passport_calendar_shift(PASSPORT_CALENDAR_FIRST_DAY, -1) == PASSPORT_CALENDAR_FIRST_DAY);
    assert(passport_calendar_shift(PASSPORT_CALENDAR_LAST_DAY, 1) == PASSPORT_CALENDAR_LAST_DAY);
    assert(!passport_calendar_get(PASSPORT_CALENDAR_FIRST_DAY - 1, &value));
    assert(!passport_calendar_get(PASSPORT_CALENDAR_LAST_DAY + 1, &value));
    for (int32_t index = PASSPORT_CALENDAR_FIRST_DAY; index <= PASSPORT_CALENDAR_LAST_DAY; index++) {
        assert(passport_calendar_get(index, &value));
        assert(value.lunar_month >= 1 && value.lunar_month <= 12 && value.lunar_day >= 1 && value.lunar_day <= 30);
        assert(value.day <= passport_calendar_month_days(value.year, value.month));
        char text[16];
        snprintf(text, sizeof(text), "%04u-%02u-%02u", value.year, value.month, value.day);
        assert(day(text) == index && value.deity[0] && value.yi[0] && value.ji[0]);
    }
}

static void clock_and_midnight(void) {
    passport_clock_t clock;
    int64_t stamp;
    assert(!passport_calendar_clock(0, &clock) && !clock.valid);
    assert(!passport_calendar_clock(-1, &clock) && !clock.valid);
    assert(workout_parse_timestamp("2026-10-02T15:59:59Z", &stamp));
    assert(passport_calendar_clock(stamp, &clock) && clock.day == day("2026-10-02"));
    assert(clock.hour == 23 && clock.minute == 59);
    assert(passport_calendar_clock(stamp + 1, &clock) && clock.day == day("2026-10-03"));
    assert(!clock.hour && !clock.minute);
    assert(workout_parse_timestamp("2026-09-30T16:00:00Z", &stamp));
    assert(passport_calendar_clock(stamp, &clock) && clock.day == day("2026-10-01"));
    assert(workout_parse_timestamp("2099-12-31T15:59:59Z", &stamp));
    assert(passport_calendar_clock(stamp, &clock) && clock.day == PASSPORT_CALENDAR_LAST_DAY);
    assert(!passport_calendar_clock(stamp + 1, &clock));
}

int main(void) {
    known_dates(); calendar_layout_and_bounds(); clock_and_midnight();
    puts("Calendar, lunar, holidays and almanac: PASS");
    return 0;
}
