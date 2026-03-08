#pragma once

#include "esp_err.h"

#include "router_netif.h"

esp_err_t router_nat_init(router_state_t *state);
esp_err_t router_nat_enable(router_state_t *state);
esp_err_t router_nat_disable(router_state_t *state);
