#pragma once

#include "workout_model.h"

#define WORKOUT_PROFILE_LIMIT 5
#define WORKOUT_PROFILES_MAGIC 0x57505246U
#define WORKOUT_PROFILES_VERSION 1
#define WORKOUT_CONNECT_WAIT_MS 20000
#define WORKOUT_RETRY_PAUSE_MS 30000

typedef struct {
    char ssid[33];
    char password[64];
} workout_wifi_profile_t;

/* One versioned blob; Wi-Fi and server selections are independent. */
typedef struct {
    uint32_t magic, version, checksum;
    unsigned wifi_count, server_count, active_wifi, active_server;
    workout_wifi_profile_t wifi[WORKOUT_PROFILE_LIMIT];
    char servers[WORKOUT_PROFILE_LIMIT][WORKOUT_URL_SIZE];
} workout_profiles_t;

typedef struct {
    unsigned index, attempted;
    int64_t deadline;
    bool waiting, exhausted;
} workout_retry_t;

void workout_profiles_seal(workout_profiles_t *profiles);
bool workout_profiles_valid(const workout_profiles_t *profiles);
bool workout_profiles_add(workout_profiles_t *profiles, const workout_config_t *config);
bool workout_profiles_config(const workout_profiles_t *profiles, workout_config_t *config);
void workout_retry_start(workout_retry_t *retry, unsigned preferred, int64_t now);
/* Returns one profile index when due, or -1 during an attempt/cooldown. */
int workout_retry_next(workout_retry_t *retry, unsigned count, int64_t now);
/* Decode one UTF-8 scalar; invalid bytes become U+FFFD and still advance. */
uint32_t workout_text_next(const char *text, size_t *offset);
