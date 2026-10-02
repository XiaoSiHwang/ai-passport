#include "workout_model.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

uint32_t workout_checksum(const void *bytes, size_t size) {
    const uint8_t *input = bytes;
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; i++) {
        crc ^= input[i];
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0xEDB88320U);
        }
    }
    return ~crc;
}

static bool fixed_string(const char *text, size_t size) {
    return memchr(text, '\0', size) != NULL;
}

static bool digits(const char *text, unsigned count, int *value) {
    *value = 0;
    for (unsigned i = 0; i < count; i++) {
        if (text[i] < '0' || text[i] > '9') return false;
        *value = *value * 10 + text[i] - '0';
    }
    return true;
}

bool workout_parse_date(const char *date, int64_t *days) {
    int year, month, day;
    if (strlen(date) < 10 || date[4] != '-' || date[7] != '-') return false;
    if (!digits(date, 4, &year) || !digits(date + 5, 2, &month)
        || !digits(date + 8, 2, &day)) return false;
    if (year < 2000 || year > 2199 || month < 1 || month > 12) return false;
    static const int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int limit = lengths[month - 1];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) limit++;
    if (day < 1 || day > limit) return false;
    /* Gregorian civil date to days since 1970-01-01, independent of host TZ. */
    year -= month <= 2;
    int era = year / 400;
    int yoe = year - era * 400;
    int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    *days = (int64_t)era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    return true;
}

bool workout_parse_timestamp(const char *timestamp, int64_t *seconds) {
    size_t length = strlen(timestamp);
    int64_t day;
    int hour, minute, second, offset_hour = 0, offset_minute = 0;
    if (length < 20 || length >= 40 || !workout_parse_date(timestamp, &day)) return false;
    if (timestamp[10] != 'T' || timestamp[13] != ':' || timestamp[16] != ':') return false;
    if (!digits(timestamp + 11, 2, &hour) || !digits(timestamp + 14, 2, &minute)
        || !digits(timestamp + 17, 2, &second)) return false;
    if (hour > 23 || minute > 59 || second > 59) return false;
    size_t index = 19;
    if (timestamp[index] == '.') {
        size_t start = ++index;
        while (index < length && isdigit((unsigned char)timestamp[index])) index++;
        if (index == start || index - start > 6) return false;
    }
    int sign = timestamp[index] == '-' ? -1 : 1;
    if (timestamp[index] == 'Z') {
        if (index + 1 != length) return false;
    } else {
        if (index + 6 != length || (timestamp[index] != '+' && timestamp[index] != '-')
            || timestamp[index + 3] != ':') return false;
        if (!digits(timestamp + index + 1, 2, &offset_hour)
            || !digits(timestamp + index + 4, 2, &offset_minute)) return false;
        if (offset_hour > 14 || offset_minute > 59 || (offset_hour == 14 && offset_minute)) return false;
    }
    *seconds = day * 86400 + hour * 3600 + minute * 60 + second
               - sign * (offset_hour * 3600 + offset_minute * 60);
    return true;
}

static bool summary_valid(const workout_summary_t *summary) {
    return summary->distance <= WORKOUT_DISTANCE_LIMIT
        && summary->goal <= WORKOUT_DISTANCE_LIMIT
        && summary->duration <= 366U * 86400U && summary->pace <= 86400U;
}

bool workout_data_valid(const workout_data_t *data) {
    int64_t start, end, updated;
    if (data->year < 2000 || data->year > 2199 || data->month < 1 || data->month > 12) return false;
    static const unsigned lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned days = lengths[data->month - 1];
    if (data->month == 2 && data->year % 4 == 0 && (data->year % 100 != 0 || data->year % 400 == 0)) days++;
    if (data->week_count != (days + 6) / 7) return false;
    if (!summary_valid(&data->monthly) || !summary_valid(&data->weekly)) return false;
    if (!fixed_string(data->week_start, sizeof(data->week_start))
        || !fixed_string(data->week_end, sizeof(data->week_end))
        || !fixed_string(data->updated_at, sizeof(data->updated_at))) return false;
    if (strlen(data->week_start) != 10 || strlen(data->week_end) != 10) return false;
    if (!workout_parse_date(data->week_start, &start) || !workout_parse_date(data->week_end, &end)
        || end - start != 6 || (start + 3) % 7 != 0) return false;
    if (!workout_parse_timestamp(data->updated_at, &updated) || updated != data->updated_seconds) return false;
    for (unsigned i = 0; i < 7; i++) if (data->days[i] > WORKOUT_DISTANCE_LIMIT) return false;
    for (unsigned i = 0; i < 5; i++) if (data->weeks[i] > WORKOUT_DISTANCE_LIMIT) return false;
    return true;
}

void workout_cache_pack(workout_cache_t *cache, const workout_data_t *data, const char *server) {
    memset(cache, 0, sizeof(*cache));
    cache->magic = WORKOUT_CACHE_MAGIC;
    cache->version = WORKOUT_CACHE_VERSION;
    cache->source = workout_checksum(server, strlen(server));
    cache->data = *data;
    cache->checksum = workout_checksum(cache, sizeof(*cache));
}

bool workout_cache_valid(const workout_cache_t *cache, size_t size) {
    if (size != sizeof(*cache) || cache->magic != WORKOUT_CACHE_MAGIC
        || cache->version != WORKOUT_CACHE_VERSION) return false;
    workout_cache_t copy = *cache;
    copy.checksum = 0;
    return cache->checksum == workout_checksum(&copy, sizeof(copy)) && workout_data_valid(&cache->data);
}

static bool authority_valid(const char *text, size_t size) {
    if (size == 0 || size >= 128 || memchr(text, '@', size)) return false;
    size_t host_size = size;
    const char *port = memchr(text, ':', size);
    if (port) {
        host_size = (size_t)(port - text);
        size_t port_size = size - host_size - 1;
        if (port_size == 0 || port_size > 5) return false;
        int number = 0;
        if (!digits(port + 1, (unsigned)port_size, &number) || number < 1 || number > 65535) return false;
    }
    if (!host_size || text[0] == '.' || text[host_size - 1] == '.') return false;
    for (size_t i = 0; i < host_size; i++) {
        if (!isalnum((unsigned char)text[i]) && text[i] != '.' && text[i] != '-') return false;
    }
    if ((host_size == 9 && strncmp(text, "localhost", 9) == 0)
        || (host_size >= 4 && strncmp(text, "127.", 4) == 0)) return false;
    return true;
}

bool workout_normalize_url(const char *input, char output[WORKOUT_URL_SIZE]) {
    size_t length = strlen(input);
    size_t prefix = strncmp(input, "http://", 7) == 0 ? 7
                  : strncmp(input, "https://", 8) == 0 ? 8 : 0;
    if (!prefix || length >= WORKOUT_URL_SIZE) return false;
    for (size_t i = prefix; i < length; i++) {
        unsigned char c = (unsigned char)input[i];
        if (c <= 32 || c >= 127 || c == '?' || c == '#' || c == '\\') return false;
    }
    const char *path = strchr(input + prefix, '/');
    size_t authority_size = path ? (size_t)(path - input) - prefix : length - prefix;
    if (!authority_valid(input + prefix, authority_size)) return false;
    while (length > prefix && input[length - 1] == '/') length--;
    const char suffix[] = "/api/workout";
    bool full = length >= sizeof(suffix) - 1
                && strncmp(input + length - sizeof(suffix) + 1, suffix, sizeof(suffix) - 1) == 0;
    size_t total = length + (full ? 0 : sizeof(suffix) - 1);
    if (total >= WORKOUT_URL_SIZE) return false;
    memcpy(output, input, length);
    if (!full) memcpy(output + length, suffix, sizeof(suffix) - 1);
    output[total] = '\0';
    return true;
}

bool workout_config_valid(const workout_config_t *config) {
    if (!fixed_string(config->ssid, sizeof(config->ssid))
        || !fixed_string(config->password, sizeof(config->password))
        || !fixed_string(config->server, sizeof(config->server))) return false;
    size_t password_length = strlen(config->password);
    if (!config->ssid[0] || (password_length && password_length < 8)) return false;
    char normalized[WORKOUT_URL_SIZE];
    return workout_normalize_url(config->server, normalized) && strcmp(normalized, config->server) == 0;
}

unsigned workout_progress(uint32_t distance, uint32_t goal, unsigned segments) {
    if (!goal) return 0;
    if (distance >= goal) return segments;
    return (unsigned)(((uint64_t)distance * segments + goal / 2) / goal);
}

static workout_action_t profiles_navigate(workout_navigation_t *nav, workout_input_t input) {
    unsigned count = nav->view == WORKOUT_VIEW_WIFI ? nav->wifi_count : nav->server_count;
    if (nav->view == WORKOUT_VIEW_NETWORK) count = 3;
    if (nav->view == WORKOUT_VIEW_CONNECTION) count = 2;
    if (count && nav->selection >= count) nav->selection = 0;
    if (count && input == WORKOUT_INPUT_UP) nav->selection = (nav->selection + count - 1) % count;
    if (count && input == WORKOUT_INPUT_DOWN) nav->selection = (nav->selection + 1) % count;
    if (input == WORKOUT_INPUT_CLEAR && nav->view == WORKOUT_VIEW_NETWORK) {
        nav->view = WORKOUT_VIEW_CLEAR;
        return WORKOUT_ACTION_NONE;
    }
    if (input != WORKOUT_INPUT_OK) return WORKOUT_ACTION_NONE;
    if (nav->view == WORKOUT_VIEW_NETWORK && nav->selection < 2) {
        nav->view = nav->selection ? WORKOUT_VIEW_SERVER : WORKOUT_VIEW_WIFI;
        nav->selection = 0;
        return WORKOUT_ACTION_NONE;
    }
    if (!count || nav->view == WORKOUT_VIEW_NETWORK
        || (nav->view == WORKOUT_VIEW_CONNECTION && nav->selection == 1)) {
        nav->view = WORKOUT_VIEW_SETUP;
        nav->setup_step = 0;
        return WORKOUT_ACTION_SETUP_START;
    }
    if (nav->view == WORKOUT_VIEW_CONNECTION) return WORKOUT_ACTION_RECONNECT;
    if (nav->view == WORKOUT_VIEW_SERVER) return WORKOUT_ACTION_SWITCH_SERVER;
    nav->view = WORKOUT_VIEW_CONNECTION;
    return WORKOUT_ACTION_SWITCH_WIFI;
}

static workout_action_t menu_navigate(workout_navigation_t *nav, workout_input_t input) {
    if (input == WORKOUT_INPUT_UP) nav->selection = (nav->selection + 4) % 5;
    if (input == WORKOUT_INPUT_DOWN) nav->selection = (nav->selection + 1) % 5;
    if (input != WORKOUT_INPUT_OK) return WORKOUT_ACTION_NONE;
    nav->view = nav->selection == 1 ? WORKOUT_VIEW_AI : nav->selection == 2 ? WORKOUT_VIEW_NETWORK
              : nav->selection == 4 ? WORKOUT_VIEW_CODEX : WORKOUT_VIEW_DASHBOARD;
    bool sync = nav->selection == 3;
    nav->selection = 0;
    return sync ? WORKOUT_ACTION_SYNC : WORKOUT_ACTION_NONE;
}

static void codex_navigate(workout_navigation_t *nav, workout_input_t input) {
    unsigned count = nav->codex_count;
    if (!count || nav->codex_selection >= count) nav->codex_selection = 0;
    if (count && input == WORKOUT_INPUT_UP) nav->codex_selection = (nav->codex_selection + count - 1) % count;
    if (count && input == WORKOUT_INPUT_DOWN) nav->codex_selection = (nav->codex_selection + 1) % count;
    if (count && input == WORKOUT_INPUT_OK) nav->view = nav->view == WORKOUT_VIEW_CODEX
        ? WORKOUT_VIEW_CODEX_DETAILS : WORKOUT_VIEW_CODEX;
}

workout_action_t workout_navigate(workout_navigation_t *nav, workout_input_t input) {
    if (input == WORKOUT_INPUT_MENU) {
        bool setup = nav->view == WORKOUT_VIEW_SETUP || nav->view == WORKOUT_VIEW_CLEAR;
        bool profiles = setup || nav->view == WORKOUT_VIEW_WIFI || nav->view == WORKOUT_VIEW_SERVER
                        || nav->view == WORKOUT_VIEW_CONNECTION;
        nav->view = profiles ? WORKOUT_VIEW_NETWORK
                  : nav->view == WORKOUT_VIEW_MENU ? WORKOUT_VIEW_DASHBOARD : WORKOUT_VIEW_MENU;
        nav->selection = 0;
        return setup ? WORKOUT_ACTION_SETUP_STOP : WORKOUT_ACTION_NONE;
    }
    if (nav->view == WORKOUT_VIEW_CLEAR) {
        nav->view = input == WORKOUT_INPUT_OK ? WORKOUT_VIEW_SETUP : WORKOUT_VIEW_NETWORK;
        nav->selection = nav->setup_step = 0;
        return input == WORKOUT_INPUT_OK ? WORKOUT_ACTION_CLEAR : WORKOUT_ACTION_SETUP_STOP;
    }
    if (nav->view == WORKOUT_VIEW_MENU) return menu_navigate(nav, input);
    if (nav->view == WORKOUT_VIEW_NETWORK || nav->view == WORKOUT_VIEW_WIFI
        || nav->view == WORKOUT_VIEW_SERVER || nav->view == WORKOUT_VIEW_CONNECTION)
        return profiles_navigate(nav, input);
    if (nav->view == WORKOUT_VIEW_CODEX || nav->view == WORKOUT_VIEW_CODEX_DETAILS) {
        codex_navigate(nav, input);
    } else if (nav->view == WORKOUT_VIEW_AI) {
        if (input == WORKOUT_INPUT_UP || input == WORKOUT_INPUT_DOWN) {
            nav->ai_provider ^= 1;
            nav->ai_weekly = false;
        }
        if (input == WORKOUT_INPUT_OK) nav->ai_weekly = !nav->ai_weekly;
    } else if (nav->view == WORKOUT_VIEW_SETUP) {
        if (input == WORKOUT_INPUT_CLEAR) nav->view = WORKOUT_VIEW_CLEAR;
        if (input == WORKOUT_INPUT_UP || input == WORKOUT_INPUT_DOWN) nav->setup_step ^= 1;
        if (input == WORKOUT_INPUT_OK) return WORKOUT_ACTION_SETUP_START;
    } else if (nav->view == WORKOUT_VIEW_DASHBOARD || nav->view == WORKOUT_VIEW_DETAILS) {
        if (input == WORKOUT_INPUT_UP || input == WORKOUT_INPUT_DOWN) nav->monthly = !nav->monthly;
        if (input == WORKOUT_INPUT_OK) nav->view = nav->view == WORKOUT_VIEW_DETAILS
                                                    ? WORKOUT_VIEW_DASHBOARD : WORKOUT_VIEW_DETAILS;
    }
    return WORKOUT_ACTION_NONE;
}
