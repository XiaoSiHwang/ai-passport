#pragma once

#include "workout_model.h"
#include "ai_quota.h"
#include "ai_tokens.h"

bool ai_quota_decode_response(const char *json, size_t length, unsigned provider, ai_quota_data_t *data);
bool ai_tokens_decode_response(const char *json, size_t length, unsigned provider, ai_tokens_data_t *data);

/* Bounded decoders; a failed response never modifies the caller's last data. */
bool workout_decode_response(const char *json, size_t length, workout_data_t *data);
bool workout_decode_config(const char *json, size_t length, workout_config_t *config,
                           char token[33]);
