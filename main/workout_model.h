#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "passport_calendar.h"

#define WORKOUT_URL_SIZE 192
#define WORKOUT_JSON_LIMIT 4096
#define WORKOUT_CACHE_MAGIC 0x574F524B
#define WORKOUT_CACHE_VERSION 1
#define WORKOUT_DISTANCE_LIMIT 10000000U /* Tenths of a kilometre. */

typedef struct {
    uint32_t distance;
    uint32_t duration;
    uint32_t pace;
    uint32_t goal;
} workout_summary_t;

typedef struct {
    workout_summary_t monthly;
    workout_summary_t weekly;
    uint32_t weeks[5]; /* Day ranges 1..7, 8..14, etc.; not ISO weeks. */
    uint32_t days[7];
    uint16_t year;
    uint8_t month;
    uint8_t week_count;
    char week_start[11];
    char week_end[11];
    char updated_at[40];
    int64_t updated_seconds;
    bool stale;
} workout_data_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t source;
    uint32_t checksum;
    workout_data_t data;
} workout_cache_t;

typedef struct {
    char ssid[33];
    char password[64];
    char server[WORKOUT_URL_SIZE]; /* Complete /api/workout URL. */
} workout_config_t;

typedef enum {
    WORKOUT_VIEW_HOME, WORKOUT_VIEW_DASHBOARD, WORKOUT_VIEW_DETAILS, WORKOUT_VIEW_MENU,
    WORKOUT_VIEW_NETWORK, WORKOUT_VIEW_FUTURE, WORKOUT_VIEW_CLEAR, WORKOUT_VIEW_AI,
    WORKOUT_VIEW_SETUP, WORKOUT_VIEW_WIFI, WORKOUT_VIEW_SERVER, WORKOUT_VIEW_CONNECTION,
    WORKOUT_VIEW_CODEX, WORKOUT_VIEW_CODEX_DETAILS,
    WORKOUT_VIEW_CALENDAR, WORKOUT_VIEW_ALMANAC,
} workout_view_t;

#define WORKOUT_MENU_COUNT 6

typedef enum {
    WORKOUT_INPUT_UP, WORKOUT_INPUT_DOWN, WORKOUT_INPUT_OK,
    WORKOUT_INPUT_MENU, WORKOUT_INPUT_CLEAR,
} workout_input_t;

typedef enum {
    WORKOUT_ACTION_NONE, WORKOUT_ACTION_SETUP_START, WORKOUT_ACTION_SETUP_STOP,
    WORKOUT_ACTION_SYNC, WORKOUT_ACTION_CLEAR,
    WORKOUT_ACTION_SWITCH_WIFI, WORKOUT_ACTION_SWITCH_SERVER, WORKOUT_ACTION_RECONNECT,
    WORKOUT_ACTION_CODEX_SYNC,
} workout_action_t;

typedef struct {
    workout_view_t view;
    bool monthly;
    unsigned selection;
    unsigned setup_step;
    unsigned ai_provider;
    bool ai_weekly;
    unsigned wifi_count, server_count;
    unsigned codex_count, codex_selection;
    unsigned home_focus;
    bool clock_valid;
    int32_t today, calendar_day;
} workout_navigation_t;

uint32_t workout_checksum(const void *bytes, size_t size);
void workout_cache_pack(workout_cache_t *cache, const workout_data_t *data,
                        const char *server);
bool workout_cache_valid(const workout_cache_t *cache, size_t size);
bool workout_data_valid(const workout_data_t *data);
bool workout_config_valid(const workout_config_t *config);
bool workout_normalize_url(const char *input, char output[WORKOUT_URL_SIZE]);
bool workout_parse_date(const char *date, int64_t *days);
bool workout_parse_timestamp(const char *timestamp, int64_t *seconds);
unsigned workout_progress(uint32_t distance, uint32_t goal, unsigned segments);
workout_action_t workout_navigate(workout_navigation_t *nav, workout_input_t input);
workout_view_t workout_menu_view(unsigned selection);
