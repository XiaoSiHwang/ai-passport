#include "workout_power.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(workout_idle_brightness(0) == 100);
    assert(workout_idle_brightness(29999) == 100);
    assert(workout_idle_brightness(30000) == 20);
    assert(workout_idle_brightness(59999) == 20);
    assert(workout_idle_brightness(60000) == 0);
    assert(workout_idle_brightness(INT64_C(86400000)) == 0);
    const int64_t deadlines[] = {30000, 5100, 60000};
    assert(workout_deadline_wait_ms(5000, deadlines, 3, 60000) == 100);
    assert(workout_deadline_wait_ms(5000, deadlines, 3, 50) == 50);
    assert(workout_deadline_wait_ms(5100, deadlines, 3, 60000) == 0);
    assert(workout_deadline_wait_ms(60000, deadlines, 3, 60000) == 0);
    assert(workout_deadline_wait_ms(1000, NULL, 0, 60000) == 60000);
    puts("Workout idle and deadline policy: PASS");
    return 0;
}
