#pragma once

#include <stddef.h>
#include <stdint.h>

/* Pure idle/deadline policy, in monotonic milliseconds; independent of the RTOS. */
unsigned workout_idle_brightness(int64_t idle_ms);
uint32_t workout_deadline_wait_ms(int64_t now_ms, const int64_t *deadlines,
                                 size_t count, uint32_t max_wait_ms);
