#pragma once

#include "esp_err.h"

#include "router_netif.h"

const char *router_stage_to_string(router_stage_t stage);
esp_err_t router_portal_start(router_state_t *state);
