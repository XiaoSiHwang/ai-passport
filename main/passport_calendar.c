#include "passport_calendar.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t lunar;
    uint8_t deity, term;
    uint16_t yi, ji;
} passport_day_record_t;

#include "passport_calendar_data.inc"

static const char *const s_stems[] = {"甲","乙","丙","丁","戊","己","庚","辛","壬","癸"};
static const char *const s_branches[] = {"子","丑","寅","卯","辰","巳","午","未","申","酉","戌","亥"};
static const char *const s_months[] = {"正","二","三","四","五","六","七","八","九","十","冬","腊"};
static const char *const s_deities[] = {"青龙","明堂","天刑","朱雀","金匮","天德","白虎","玉堂","天牢","玄武","司命","勾陈"};
static const char *const s_terms[] = {"","冬至","小寒","大寒","立春","雨水","惊蛰","春分","清明","谷雨","立夏","小满","芒种",
    "夏至","小暑","大暑","立秋","处暑","白露","秋分","寒露","霜降","立冬","小雪","大雪"};

unsigned passport_calendar_month_days(unsigned year, unsigned month) {
    static const unsigned lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (!month || month > 12) return 0;
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return lengths[month - 1] + (month == 2 && leap);
}

const char *passport_calendar_lunar_day(unsigned day) {
    static const char *const names[] = {"初一","初二","初三","初四","初五","初六","初七","初八","初九","初十",
        "十一","十二","十三","十四","十五","十六","十七","十八","十九","二十",
        "廿一","廿二","廿三","廿四","廿五","廿六","廿七","廿八","廿九","三十"};
    return day && day <= 30 ? names[day - 1] : "--";
}

bool passport_calendar_clock(int64_t utc_seconds, passport_clock_t *clock) {
    memset(clock, 0, sizeof(*clock));
    if (utc_seconds < 1704067200 || utc_seconds > (int64_t)(PASSPORT_CALENDAR_LAST_DAY + 1) * 86400 - 28801) return false;
    int64_t local = utc_seconds + 28800;
    clock->valid = true;
    clock->day = (int32_t)(local / 86400);
    clock->hour = (unsigned)(local / 3600 % 24);
    clock->minute = (unsigned)(local / 60 % 60);
    return true;
}

int32_t passport_calendar_shift(int32_t day, int delta) {
    int64_t next = (int64_t)day + delta;
    if (next < PASSPORT_CALENDAR_FIRST_DAY) return PASSPORT_CALENDAR_FIRST_DAY;
    if (next > PASSPORT_CALENDAR_LAST_DAY) return PASSPORT_CALENDAR_LAST_DAY;
    return (int32_t)next;
}

unsigned passport_calendar_month_offset(const passport_calendar_t *calendar) {
    return (calendar->weekday + 7 - (calendar->day - 1) % 7) % 7;
}

static void solar_date(int32_t day, passport_calendar_t *calendar) {
    unsigned remaining = (unsigned)(day - PASSPORT_CALENDAR_FIRST_DAY);
    calendar->year = 2000;
    for (;;) {
        unsigned length = 365 + (passport_calendar_month_days(calendar->year, 2) == 29);
        if (remaining < length) break;
        remaining -= length;
        calendar->year++;
    }
    calendar->month = 1;
    while (remaining >= passport_calendar_month_days(calendar->year, calendar->month)) {
        remaining -= passport_calendar_month_days(calendar->year, calendar->month);
        calendar->month++;
    }
    calendar->day = remaining + 1;
    calendar->weekday = (unsigned)(day + 3) % 7;
}

static const char *festival(int32_t day, const passport_calendar_t *calendar) {
    unsigned solar = calendar->month * 100 + calendar->day;
    if (solar == 101) return "元旦";
    if (solar == 501) return "劳动节";
    if (solar == 1001) return "国庆节";
    if (calendar->leap_month) return "";
    unsigned lunar = calendar->lunar_month * 100 + calendar->lunar_day;
    switch (lunar) {
        case 101: return "春节";
        case 115: return "元宵节";
        case 505: return "端午节";
        case 815: return "中秋节";
        case 909: return "重阳节";
        case 1208: return "腊八节";
        default: break;
    }
    if (calendar->lunar_month == 12 && day < PASSPORT_CALENDAR_LAST_DAY
        && (s_calendar_records[day + 1 - PASSPORT_CALENDAR_FIRST_DAY].lunar & 1023) == 33) return "除夕";
    return "";
}

static void annual_holiday(passport_calendar_t *calendar) {
    if (calendar->year != 2026) return; /* Other years do not inherit this year's adjustments. */
    static const struct {unsigned start, end; const char *name;} holidays[] = {
        {101,103,"元旦假期"},{215,223,"春节假期"},{404,406,"清明假期"},{501,505,"劳动节假期"},
        {619,621,"端午假期"},{925,927,"中秋假期"},{1001,1007,"国庆假期"}
    };
    static const unsigned workdays[] = {104,214,228,509,920,1010};
    unsigned date = calendar->month * 100 + calendar->day;
    for (unsigned i = 0; i < sizeof(workdays) / sizeof(workdays[0]); i++) {
        if (date == workdays[i]) {calendar->workday = true; calendar->festival = "调休上班"; return;}
    }
    for (unsigned i = 0; i < sizeof(holidays) / sizeof(holidays[0]); i++) {
        if (date < holidays[i].start || date > holidays[i].end) continue;
        calendar->holiday = true;
        calendar->holiday_day = date - holidays[i].start + 1;
        if (!calendar->festival[0]) calendar->festival = holidays[i].name;
        return;
    }
}

bool passport_calendar_get(int32_t day, passport_calendar_t *calendar) {
    memset(calendar, 0, sizeof(*calendar));
    if (day < PASSPORT_CALENDAR_FIRST_DAY || day > PASSPORT_CALENDAR_LAST_DAY) return false;
    solar_date(day, calendar);
    const passport_day_record_t *record = &s_calendar_records[day - PASSPORT_CALENDAR_FIRST_DAY];
    unsigned month = record->lunar & 31;
    calendar->leap_month = month > 12;
    calendar->lunar_month = month > 12 ? month - 12 : month;
    calendar->lunar_day = (record->lunar >> 5) & 31;
    calendar->lunar_year = calendar->year - (record->lunar >> 10);
    snprintf(calendar->lunar, sizeof(calendar->lunar), "%s%s月%s", calendar->leap_month ? "闰" : "",
        s_months[calendar->lunar_month - 1], passport_calendar_lunar_day(calendar->lunar_day));
    snprintf(calendar->year_ganzhi, sizeof(calendar->year_ganzhi), "%s%s",
        s_stems[(calendar->lunar_year - 4) % 10], s_branches[(calendar->lunar_year - 4) % 12]);
    snprintf(calendar->day_ganzhi, sizeof(calendar->day_ganzhi), "%s%s", s_stems[(day + 17) % 10], s_branches[(day + 17) % 12]);
    calendar->deity = s_deities[record->deity];
    calendar->auspicious = ((1U << record->deity) & 0x4B3) != 0;
    calendar->term = s_terms[record->term];
    calendar->yi = s_calendar_phrases[record->yi];
    calendar->ji = s_calendar_phrases[record->ji];
    calendar->festival = festival(day, calendar);
    annual_holiday(calendar);
    return true;
}
