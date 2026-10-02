#pragma once

#include "workout_model.h"

/* Bounded decoders; a failed response never modifies the caller's last data. */
bool workout_decode_response(const char *json, size_t length, workout_data_t *data);
bool workout_decode_config(const char *json, size_t length, workout_config_t *config,
                           char token[33]);
