#include "workout_profiles.h"

#include <string.h>

void workout_profiles_seal(workout_profiles_t *profiles) {
    profiles->magic = WORKOUT_PROFILES_MAGIC;
    profiles->version = WORKOUT_PROFILES_VERSION;
    profiles->checksum = 0;
    profiles->checksum = workout_checksum(profiles, sizeof(*profiles));
}

bool workout_profiles_config(const workout_profiles_t *profiles, workout_config_t *config) {
    memset(config, 0, sizeof(*config));
    if (!profiles->wifi_count || profiles->wifi_count > WORKOUT_PROFILE_LIMIT
        || !profiles->server_count || profiles->server_count > WORKOUT_PROFILE_LIMIT
        || profiles->active_wifi >= profiles->wifi_count
        || profiles->active_server >= profiles->server_count) return false;
    memcpy(config->ssid, profiles->wifi[profiles->active_wifi].ssid, sizeof(config->ssid));
    memcpy(config->password, profiles->wifi[profiles->active_wifi].password, sizeof(config->password));
    memcpy(config->server, profiles->servers[profiles->active_server], sizeof(config->server));
    return workout_config_valid(config);
}

bool workout_profiles_valid(const workout_profiles_t *profiles) {
    if (profiles->magic != WORKOUT_PROFILES_MAGIC || profiles->version != WORKOUT_PROFILES_VERSION)
        return false;
    workout_profiles_t copy = *profiles;
    copy.checksum = 0;
    if (workout_checksum(&copy, sizeof(copy)) != profiles->checksum) return false;
    workout_config_t config;
    if (!workout_profiles_config(profiles, &config)) return false;
    for (unsigned i = 0; i < profiles->wifi_count; i++) {
        memcpy(config.ssid, profiles->wifi[i].ssid, sizeof(config.ssid));
        memcpy(config.password, profiles->wifi[i].password, sizeof(config.password));
        if (!workout_config_valid(&config)) return false;
        for (unsigned j = 0; j < i; j++)
            if (strcmp(profiles->wifi[i].ssid, profiles->wifi[j].ssid) == 0) return false;
    }
    for (unsigned i = 0; i < profiles->server_count; i++) {
        memcpy(config.server, profiles->servers[i], sizeof(config.server));
        if (!workout_config_valid(&config)) return false;
        for (unsigned j = 0; j < i; j++)
            if (strcmp(profiles->servers[i], profiles->servers[j]) == 0) return false;
    }
    return true;
}

static unsigned wifi_slot(workout_profiles_t *profiles, const char *ssid) {
    for (unsigned i = 0; i < profiles->wifi_count; i++)
        if (strcmp(profiles->wifi[i].ssid, ssid) == 0) return i;
    if (profiles->wifi_count < WORKOUT_PROFILE_LIMIT) return profiles->wifi_count++;
    /* Evict the oldest entry other than the current selection. */
    unsigned drop = profiles->active_wifi == 0 ? 1 : 0;
    memmove(&profiles->wifi[drop], &profiles->wifi[drop + 1],
            (WORKOUT_PROFILE_LIMIT - drop - 1) * sizeof(profiles->wifi[0]));
    return WORKOUT_PROFILE_LIMIT - 1;
}

static unsigned server_slot(workout_profiles_t *profiles, const char *server) {
    for (unsigned i = 0; i < profiles->server_count; i++)
        if (strcmp(profiles->servers[i], server) == 0) return i;
    if (profiles->server_count < WORKOUT_PROFILE_LIMIT) return profiles->server_count++;
    unsigned drop = profiles->active_server == 0 ? 1 : 0;
    memmove(&profiles->servers[drop], &profiles->servers[drop + 1],
            (WORKOUT_PROFILE_LIMIT - drop - 1) * sizeof(profiles->servers[0]));
    return WORKOUT_PROFILE_LIMIT - 1;
}

bool workout_profiles_add(workout_profiles_t *profiles, const workout_config_t *config) {
    if (!workout_config_valid(config)) return false;
    if (profiles->magic && !workout_profiles_valid(profiles)) return false;
    if (!profiles->magic) memset(profiles, 0, sizeof(*profiles));
    unsigned wifi = wifi_slot(profiles, config->ssid);
    unsigned server = server_slot(profiles, config->server);
    memcpy(profiles->wifi[wifi].ssid, config->ssid, sizeof(config->ssid));
    memcpy(profiles->wifi[wifi].password, config->password, sizeof(config->password));
    memcpy(profiles->servers[server], config->server, sizeof(config->server));
    profiles->active_wifi = wifi;
    profiles->active_server = server;
    workout_profiles_seal(profiles);
    return true;
}

void workout_retry_start(workout_retry_t *retry, unsigned preferred, int64_t now) {
    *retry = (workout_retry_t){.index = preferred, .deadline = now};
}

int workout_retry_next(workout_retry_t *retry, unsigned count, int64_t now) {
    if (!count || count > WORKOUT_PROFILE_LIMIT || now < retry->deadline) return -1;
    retry->waiting = false;
    if (retry->attempted >= count) {
        retry->exhausted = true;
        retry->attempted = 0;
        retry->deadline = now + WORKOUT_RETRY_PAUSE_MS;
        return -1;
    }
    if (retry->attempted || retry->exhausted) retry->index = (retry->index + 1) % count;
    else retry->index %= count;
    retry->attempted++;
    retry->waiting = true;
    retry->exhausted = false;
    retry->deadline = now + WORKOUT_CONNECT_WAIT_MS;
    return (int)retry->index;
}

uint32_t workout_text_next(const char *text, size_t *offset) {
    const unsigned char *bytes = (const unsigned char *)text + *offset;
    uint32_t first = bytes[0];
    if (!first) return 0;
    (*offset)++;
    if (first < 0x80) return first;
    unsigned count = first >= 0xC2 && first <= 0xDF ? 2
                   : first >= 0xE0 && first <= 0xEF ? 3
                   : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
    if (!count) return 0xFFFD;
    uint32_t value = first & ((1U << (7 - count)) - 1);
    for (unsigned i = 1; i < count; i++) {
        if ((bytes[i] & 0xC0) != 0x80) return 0xFFFD;
        value = (value << 6) | (bytes[i] & 0x3F);
    }
    if ((count == 3 && value < 0x800) || (count == 4 && value < 0x10000)
        || (value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF) return 0xFFFD;
    *offset += count - 1;
    return value;
}
