#include "workout_model.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static workout_data_t sample(void) {
    workout_data_t data = {0};
    data.year = 2026;
    data.month = 10;
    data.week_count = 5;
    data.weekly.distance = 246;
    data.weekly.goal = 300;
    strcpy(data.week_start, "2026-09-28");
    strcpy(data.week_end, "2026-10-04");
    strcpy(data.updated_at, "2026-10-02T10:30:00+08:00");
    assert(workout_parse_timestamp(data.updated_at, &data.updated_seconds));
    return data;
}

static void dates(void) {
    int64_t days, other, stamp, utc;
    assert(workout_parse_date("2024-02-29", &days));
    assert(!workout_parse_date("2026-02-29", &days));
    assert(!workout_parse_date("2100-02-29", &days));
    assert(!workout_parse_date("2026-13-01", &days));
    assert(!workout_parse_date("2026-04-31", &days));
    assert(!workout_parse_date("2026", &days));
    assert(workout_parse_date("2026-09-28", &days));
    assert(workout_parse_date("2026-10-04", &other) && other - days == 6);
    assert(workout_parse_timestamp("2026-10-02T10:30:00+08:00", &stamp));
    assert(workout_parse_timestamp("2026-10-02T02:30:00Z", &utc) && stamp == utc);
    assert(workout_parse_timestamp("2026-10-01T19:30:00-07:00", &utc) && stamp == utc);
    assert(workout_parse_timestamp("2026-10-02T10:30:00.123456+08:00", &utc) && stamp == utc);
    assert(!workout_parse_timestamp("2026-10-02T24:30:00Z", &stamp));
    assert(!workout_parse_timestamp("2026-10-02T00:30:00+14:01", &stamp));
    assert(!workout_parse_timestamp("2026-10-02T00:30:00", &stamp));
    assert(!workout_parse_timestamp("2026-10-02T00:30:00.Z", &stamp));
    assert(!workout_parse_timestamp("2026-10-02T00:30:00Zjunk", &stamp));
}

static void urls(void) {
    char url[WORKOUT_URL_SIZE];
    assert(workout_normalize_url("http://192.168.1.20:8000", url));
    assert(strcmp(url, "http://192.168.1.20:8000/api/workout") == 0);
    assert(workout_normalize_url("https://example.com/api/workout/", url));
    assert(strcmp(url, "https://example.com/api/workout") == 0);
    assert(workout_normalize_url("https://example.com/service/", url));
    assert(strcmp(url, "https://example.com/service/api/workout") == 0);
    const char *invalid[] = {"", "file:///data", "http://", "http://localhost:8000",
        "http://127.0.0.1:8000", "http://user:secret@example.com", "http://example.com/?token=x",
        "http://example.com/#x", "http://example.com:99999", "http://example.com:",
        "http://example.com:11111111111111111111", "http://exa mple.com", "http://example.com\\x"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) assert(!workout_normalize_url(invalid[i], url));
    char oversized[300];
    memset(oversized, 'a', sizeof(oversized));
    memcpy(oversized, "http://", 7);
    oversized[sizeof(oversized) - 1] = '\0';
    assert(!workout_normalize_url(oversized, url));
    workout_config_t config = {.ssid = "example", .server = "http://192.168.1.20:8000/api/workout"};
    assert(workout_config_valid(&config));
    strcpy(config.password, "short");
    assert(!workout_config_valid(&config));
    memset(config.ssid, 'x', sizeof(config.ssid));
    assert(!workout_config_valid(&config));
}

static void cache(void) {
    workout_data_t data = sample();
    assert(workout_data_valid(&data));
    workout_cache_t first, restored;
    workout_cache_pack(&first, &data, "http://example.com/api/workout");
    assert(workout_cache_valid(&first, sizeof(first)));
    memcpy(&restored, &first, sizeof(first)); /* Reboot reconstructs from bytes, not globals. */
    assert(workout_cache_valid(&restored, sizeof(restored)));
    assert(restored.data.weekly.distance == 246);
    assert(!workout_cache_valid(&restored, sizeof(restored) - 1));
    restored.data.weekly.distance++;
    assert(!workout_cache_valid(&restored, sizeof(restored)));
    restored = first;
    restored.version++;
    assert(!workout_cache_valid(&restored, sizeof(restored)));
    restored = first;
    memset(restored.data.updated_at, 'x', sizeof(restored.data.updated_at));
    restored.checksum = 0;
    restored.checksum = workout_checksum(&restored, sizeof(restored));
    assert(!workout_cache_valid(&restored, sizeof(restored)));
    data.week_count = 4;
    assert(!workout_data_valid(&data));
    data = sample();
    strcpy(data.week_start, "2026-09-29");
    strcpy(data.week_end, "2026-10-05");
    assert(!workout_data_valid(&data));
    assert(workout_progress(246, 300, 20) == 16);
    assert(workout_progress(0, 300, 20) == 0);
    assert(workout_progress(400, 300, 20) == 20);
    assert(workout_progress(UINT32_MAX, 1, 20) == 20);
    assert(workout_progress(200, 0, 20) == 0);
}

static void navigation(void) {
    workout_navigation_t nav = {0};
    assert(workout_navigate(&nav, WORKOUT_INPUT_UP) == WORKOUT_ACTION_NONE && nav.monthly);
    workout_navigate(&nav, WORKOUT_INPUT_OK);
    assert(nav.view == WORKOUT_VIEW_DETAILS);
    workout_navigate(&nav, WORKOUT_INPUT_MENU);
    assert(nav.view == WORKOUT_VIEW_MENU);
    workout_navigate(&nav, WORKOUT_INPUT_DOWN);
    assert(workout_navigate(&nav, WORKOUT_INPUT_OK) == WORKOUT_ACTION_NONE);
    assert(nav.view == WORKOUT_VIEW_AI && nav.ai_provider == 0);
    workout_navigate(&nav, WORKOUT_INPUT_UP);
    assert(nav.ai_provider == 1 && !nav.ai_weekly);
    workout_navigate(&nav, WORKOUT_INPUT_OK);
    assert(nav.ai_weekly);
    workout_navigate(&nav, WORKOUT_INPUT_DOWN);
    assert(nav.ai_provider == 0 && !nav.ai_weekly);
    workout_navigate(&nav, WORKOUT_INPUT_MENU);
    workout_navigate(&nav, WORKOUT_INPUT_DOWN);
    workout_navigate(&nav, WORKOUT_INPUT_DOWN);
    assert(workout_navigate(&nav, WORKOUT_INPUT_OK) == WORKOUT_ACTION_NONE);
    assert(nav.view == WORKOUT_VIEW_NETWORK);
    workout_navigate(&nav, WORKOUT_INPUT_OK);
    assert(nav.view == WORKOUT_VIEW_WIFI);
    assert(workout_navigate(&nav, WORKOUT_INPUT_OK) == WORKOUT_ACTION_SETUP_START);
    assert(nav.view == WORKOUT_VIEW_SETUP);
    workout_navigate(&nav, WORKOUT_INPUT_DOWN);
    assert(nav.setup_step == 1);
    workout_navigate(&nav, WORKOUT_INPUT_CLEAR);
    assert(nav.view == WORKOUT_VIEW_CLEAR);
    assert(workout_navigate(&nav, WORKOUT_INPUT_UP) == WORKOUT_ACTION_SETUP_STOP);
    assert(nav.view == WORKOUT_VIEW_NETWORK);
    workout_navigate(&nav, WORKOUT_INPUT_CLEAR);
    assert(workout_navigate(&nav, WORKOUT_INPUT_OK) == WORKOUT_ACTION_CLEAR);
    assert(workout_navigate(&nav, WORKOUT_INPUT_MENU) == WORKOUT_ACTION_SETUP_STOP);
    workout_navigate(&nav, WORKOUT_INPUT_MENU);
    workout_navigate(&nav, WORKOUT_INPUT_UP);
    assert(nav.selection == 4);
    workout_navigate(&nav, WORKOUT_INPUT_OK);
    assert(nav.view == WORKOUT_VIEW_CODEX);
    workout_navigate(&nav, WORKOUT_INPUT_MENU);
    workout_navigate(&nav, WORKOUT_INPUT_DOWN);
    workout_navigate(&nav, WORKOUT_INPUT_DOWN);
    workout_navigate(&nav, WORKOUT_INPUT_DOWN);
    assert(workout_navigate(&nav, WORKOUT_INPUT_OK) == WORKOUT_ACTION_SYNC);
    assert(nav.view == WORKOUT_VIEW_DASHBOARD && nav.monthly);
}

static void profile_navigation(void) {
    workout_navigation_t nav = {.view = WORKOUT_VIEW_NETWORK, .wifi_count = 5, .server_count = 2};
    workout_navigate(&nav, WORKOUT_INPUT_OK);
    assert(nav.view == WORKOUT_VIEW_WIFI);
    workout_navigate(&nav, WORKOUT_INPUT_UP);
    assert(nav.selection == 4);
    assert(workout_navigate(&nav, WORKOUT_INPUT_OK) == WORKOUT_ACTION_SWITCH_WIFI);
    assert(nav.view == WORKOUT_VIEW_CONNECTION && nav.selection == 4);
    nav.selection = 0;
    assert(workout_navigate(&nav, WORKOUT_INPUT_OK) == WORKOUT_ACTION_RECONNECT);
    workout_navigate(&nav, WORKOUT_INPUT_DOWN);
    assert(workout_navigate(&nav, WORKOUT_INPUT_OK) == WORKOUT_ACTION_SETUP_START);
    assert(nav.view == WORKOUT_VIEW_SETUP);
    assert(workout_navigate(&nav, WORKOUT_INPUT_MENU) == WORKOUT_ACTION_SETUP_STOP);
    assert(nav.view == WORKOUT_VIEW_NETWORK);
    workout_navigate(&nav, WORKOUT_INPUT_DOWN);
    workout_navigate(&nav, WORKOUT_INPUT_OK);
    assert(nav.view == WORKOUT_VIEW_SERVER);
    workout_navigate(&nav, WORKOUT_INPUT_DOWN);
    assert(workout_navigate(&nav, WORKOUT_INPUT_OK) == WORKOUT_ACTION_SWITCH_SERVER && nav.selection == 1);
    assert(nav.view == WORKOUT_VIEW_SERVER);
    workout_navigate(&nav, WORKOUT_INPUT_MENU);
    workout_navigate(&nav, WORKOUT_INPUT_UP);
    assert(nav.selection == 2);
    assert(workout_navigate(&nav, WORKOUT_INPUT_OK) == WORKOUT_ACTION_SETUP_START);
}

int main(void) {
    dates(); urls(); cache(); navigation(); profile_navigation();
    puts("Workout model: PASS");
    return 0;
}
