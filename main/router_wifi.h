#pragma once

#include "esp_err.h"

#include "router_netif.h"

esp_err_t router_wifi_init(router_state_t *state);
esp_err_t router_wifi_start(router_state_t *state);
