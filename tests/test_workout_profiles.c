#include "workout_profiles.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static workout_config_t config(unsigned index) {
    workout_config_t value = {0};
    snprintf(value.ssid, sizeof(value.ssid), "Network_%u", index);
    snprintf(value.server, sizeof(value.server), "http://example%u.com/api/workout", index);
    return value;
}

static void history(void) {
    workout_profiles_t profiles = {0};
    workout_config_t first = config(0), second = config(1), loaded;
    assert(workout_profiles_add(&profiles, &first));
    assert(workout_profiles_add(&profiles, &second));
    assert(workout_profiles_valid(&profiles));
    assert(profiles.wifi_count == 2 && profiles.server_count == 2);
    profiles.active_wifi = 0;
    workout_profiles_seal(&profiles);
    assert(workout_profiles_config(&profiles, &loaded));
    assert(strcmp(loaded.ssid, first.ssid) == 0 && strcmp(loaded.server, second.server) == 0);
    strcpy(first.password, "EXAMPLE_ONLY");
    assert(workout_profiles_add(&profiles, &first));
    assert(profiles.wifi_count == 2 && profiles.server_count == 2);
    assert(strcmp(profiles.wifi[0].password, first.password) == 0);
    for (unsigned i = 2; i < WORKOUT_PROFILE_LIMIT; i++) {
        workout_config_t value = config(i);
        assert(workout_profiles_add(&profiles, &value));
    }
    profiles.active_wifi = profiles.active_server = 0;
    workout_profiles_seal(&profiles);
    workout_config_t sixth = config(5);
    assert(workout_profiles_add(&profiles, &sixth));
    assert(profiles.wifi_count == 5 && profiles.server_count == 5);
    assert(strcmp(profiles.wifi[0].ssid, first.ssid) == 0);
    assert(strcmp(profiles.servers[0], first.server) == 0);
    assert(strcmp(profiles.wifi[1].ssid, "Network_2") == 0);
    assert(workout_profiles_valid(&profiles));
    sixth.password[0] = 'x'; sixth.password[1] = 0;
    workout_profiles_t before = profiles;
    assert(!workout_profiles_add(&profiles, &sixth));
    assert(memcmp(&profiles, &before, sizeof(profiles)) == 0);
}

static void corruption(void) {
    workout_profiles_t profiles = {0};
    workout_config_t value = config(0);
    assert(workout_profiles_add(&profiles, &value));
    workout_profiles_t invalid = profiles;
    invalid.wifi[0].ssid[0] ^= 1;
    assert(!workout_profiles_valid(&invalid));
    invalid = profiles; invalid.wifi_count = 6; workout_profiles_seal(&invalid);
    assert(!workout_profiles_valid(&invalid));
    invalid = profiles; invalid.active_server = 1; workout_profiles_seal(&invalid);
    assert(!workout_profiles_valid(&invalid));
    invalid = profiles; invalid.version++; invalid.checksum = 0;
    invalid.checksum = workout_checksum(&invalid, sizeof(invalid));
    assert(!workout_profiles_valid(&invalid));
    invalid = profiles; memset(invalid.wifi[0].ssid, 'x', sizeof(invalid.wifi[0].ssid));
    workout_profiles_seal(&invalid);
    assert(!workout_profiles_valid(&invalid));
    invalid = profiles; invalid.wifi[1] = invalid.wifi[0]; invalid.wifi_count = 2;
    workout_profiles_seal(&invalid);
    assert(!workout_profiles_valid(&invalid));
    invalid = profiles; strcpy(invalid.servers[0], "http://localhost/api/workout");
    workout_profiles_seal(&invalid);
    assert(!workout_profiles_valid(&invalid));
}

static void retry_timing(void) {
    workout_retry_t retry;
    workout_retry_start(&retry, 2, 100);
    assert(workout_retry_next(&retry, 3, 99) == -1);
    assert(workout_retry_next(&retry, 3, 100) == 2);
    assert(retry.waiting && retry.attempted == 1);
    assert(workout_retry_next(&retry, 3, 20099) == -1);
    assert(workout_retry_next(&retry, 3, 20100) == 0);
    assert(workout_retry_next(&retry, 3, 40100) == 1);
    assert(workout_retry_next(&retry, 3, 60100) == -1);
    assert(retry.exhausted && !retry.waiting);
    assert(workout_retry_next(&retry, 3, 90099) == -1);
    assert(workout_retry_next(&retry, 3, 90100) == 2);
    workout_retry_start(&retry, 1, 100000);
    assert(workout_retry_next(&retry, 3, 100000) == 1);
    assert(workout_retry_next(&retry, 0, 120000) == -1);
    assert(workout_retry_next(&retry, 6, 120000) == -1);
    workout_retry_start(&retry, 99, 0);
    assert(workout_retry_next(&retry, 1, 0) == 0);
    assert(workout_retry_next(&retry, 1, 20000) == -1 && retry.exhausted);
    assert(workout_retry_next(&retry, 1, 50000) == 0);
}

static void unicode(void) {
    size_t index = 0;
    const char *text = "A家庭\xF0\x9F\x98\x80";
    assert(workout_text_next(text, &index) == 'A');
    assert(workout_text_next(text, &index) == 0x5BB6);
    assert(workout_text_next(text, &index) == 0x5EAD);
    assert(workout_text_next(text, &index) == 0x1F600);
    assert(workout_text_next(text, &index) == 0 && index == strlen(text));
    const char *invalid[] = {"\xE4", "\xF0\x80\x80\x80", "\xED\xA0\x80", "\xC0\x80", "\xF4\x90\x80\x80"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        index = 0;
        assert(workout_text_next(invalid[i], &index) == 0xFFFD && index == 1);
    }
}

int main(void) {
    history(); corruption(); retry_timing(); unicode();
    puts("Workout profiles and reconnection: PASS");
    return 0;
}
