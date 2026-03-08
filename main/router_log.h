#pragma once

#include "esp_log.h"

#include "router_config.h"

#define ROUTER_LOG_VERBOSE(enabled, tag, fmt, ...) \
    do { \
        if ((enabled)) { \
            ESP_LOGI(tag, fmt, ##__VA_ARGS__); \
        } \
    } while (0)
