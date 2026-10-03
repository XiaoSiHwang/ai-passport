#include "wellness.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool decode(const char *records, bool stale, bool complete, const char *error, wellness_data_t *data) {
    char json[4096];
    int length = snprintf(json, sizeof(json), "{\"success\":true,\"data\":{\"source\":\"intervals_icu\","
        "\"oldest\":\"2026-09-27\",\"newest\":\"2026-10-03\",\"records\":%s,"
        "\"stale\":%s,\"complete\":%s,\"fetched_at\":null},\"error\":%s}",
        records, stale ? "true" : "false", complete ? "true" : "false", error);
    return wellness_decode_response(json, (size_t)length, data);
}

int main(void) {
    wellness_data_t data = {0};
    assert(decode("[{\"id\":\"2026-10-03\",\"weight\":65.5,\"restingHR\":50,\"hrv\":null,"
        "\"sleepSecs\":25200,\"steps\":0,\"BodyBatteryMax\":90,\"customMetric\":{\"value\":3}}]", false, true, "null", &data));
    assert(data.days[0].recorded && data.days[0].values[WELLNESS_WEIGHT] == 655);
    assert(data.days[0].present & (1U << WELLNESS_HRV));
    assert(!(data.days[0].known & (1U << WELLNESS_HRV)));
    assert(data.days[0].known & (1U << WELLNESS_STEPS));
    assert(!data.days[0].values[WELLNESS_STEPS] && !(data.days[0].present & (1U << WELLNESS_SPO2)));
    assert(decode("[]", false, true, "null", &data) && data.complete && !data.days[0].recorded);
    assert(decode("[]", true, false, "{\"message\":\"Upstream failure\"}", &data));
    assert(data.stale && !data.complete && data.degraded);
    wellness_data_t saved = data;
    assert(!decode("[{\"id\":\"2026-10-04\"}]", false, true, "null", &data));
    assert(!decode("[{\"id\":\"2026-10-03\"},{\"id\":\"2026-10-03\"}]", false, true, "null", &data));
    assert(!decode("[{\"id\":\"2026-10-03\",\"hrv\":\"50\"}]", false, true, "null", &data));
    assert(!decode("[{\"id\":\"2026-10-03\",\"weight\":-1}]", false, true, "null", &data));
    assert(memcmp(&saved, &data, sizeof(data)) == 0);
    assert(!wellness_decode_response("{\"success\":false,\"data\":null,\"error\":{\"message\":\"fail\"}}", 60, &data));
    char oversized[WELLNESS_JSON_LIMIT + 2]; memset(oversized, ' ', sizeof(oversized));
    assert(!wellness_decode_response(oversized, sizeof(oversized), &data));
    puts("Wellness JSON: PASS");
}
