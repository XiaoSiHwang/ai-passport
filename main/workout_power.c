#include "workout_power.h"

unsigned workout_idle_brightness(int64_t idle_ms) {
    return idle_ms >= 60000 ? 0 : idle_ms >= 30000 ? 20 : 100;
}

uint32_t workout_deadline_wait_ms(int64_t now_ms, const int64_t *deadlines,
                                 size_t count, uint32_t max_wait_ms) {
    uint32_t wait = max_wait_ms;
    for (size_t i = 0; i < count; i++) {
        if (deadlines[i] <= now_ms) return 0;
        int64_t remaining = deadlines[i] - now_ms;
        if (remaining < wait) wait = (uint32_t)remaining;
    }
    return wait;
}
