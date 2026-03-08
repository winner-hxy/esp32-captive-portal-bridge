#pragma once

#include "esp_err.h"

#include "router_config.h"

esp_err_t router_storage_load(router_config_t *config);
esp_err_t router_storage_save(const router_config_t *config);
