#include "wellness.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    int64_t day;
    assert(workout_parse_date("2026-10-03", &day));
    char url[WORKOUT_URL_SIZE], text[48];
    assert(wellness_url("http://server.example.com:8000/api/workout", (int32_t)day, url));
    assert(strcmp(url, "http://server.example.com:8000/api/wellness?oldest=2026-09-27&newest=2026-10-03") == 0);
    assert(!wellness_url("invalid", (int32_t)day, url));
    assert(!wellness_url("http://server.example.com/api/workout", 0, url));
    wellness_record_t record = {0};
    wellness_format(&record, WELLNESS_STEPS, text, sizeof(text)); assert(strcmp(text, "未提供") == 0);
    record.present = 1U << WELLNESS_STEPS;
    wellness_format(&record, WELLNESS_STEPS, text, sizeof(text)); assert(strcmp(text, "空值") == 0);
    record.known = record.present;
    wellness_format(&record, WELLNESS_STEPS, text, sizeof(text)); assert(strcmp(text, "0") == 0);
    record.present |= 1U << WELLNESS_SLEEP; record.known |= 1U << WELLNESS_SLEEP;
    record.values[WELLNESS_SLEEP] = 25200;
    wellness_format(&record, WELLNESS_SLEEP, text, sizeof(text)); assert(strcmp(text, "7时00分") == 0);
    record.present |= 1U << WELLNESS_WEIGHT; record.known |= 1U << WELLNESS_WEIGHT;
    record.values[WELLNESS_WEIGHT] = 655;
    wellness_format(&record, WELLNESS_WEIGHT, text, sizeof(text)); assert(strcmp(text, "65.5") == 0);
    workout_navigation_t nav = {.view = WORKOUT_VIEW_WELLNESS};
    workout_navigate(&nav, WORKOUT_INPUT_DOWN); assert(nav.wellness_day == 0);
    for (unsigned i = 0; i < 10; i++) workout_navigate(&nav, WORKOUT_INPUT_UP);
    assert(nav.wellness_day == 6);
    for (unsigned i = 0; i < 3; i++) workout_navigate(&nav, WORKOUT_INPUT_OK);
    assert(nav.wellness_style == 0);
    assert(workout_navigate(&nav, WORKOUT_INPUT_CLEAR) == WORKOUT_ACTION_WELLNESS_SYNC);
    workout_navigate(&nav, WORKOUT_INPUT_MENU); assert(nav.view == WORKOUT_VIEW_MENU && nav.selection == 4);
    puts("Wellness model: PASS");
}
