#pragma once
#include <stdio.h>
#define ESP_LOGE(tag, format, ...) fprintf(stderr, "%s: " format "\n", tag, ##__VA_ARGS__)
