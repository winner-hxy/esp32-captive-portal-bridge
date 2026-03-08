#include "esp_log.h"
#include "lwip/lwip_napt.h"

#include "router_nat.h"

static const char *TAG = "router_nat";

esp_err_t router_nat_init(router_state_t *state)
{
    state->napt_enabled = false;
    return ESP_OK;
}

esp_err_t router_nat_enable(router_state_t *state)
{
    esp_netif_ip_info_t ap_ip = {0};

    if (state->napt_enabled) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(esp_netif_get_ip_info(state->ap_netif, &ap_ip));
    ip_napt_enable(ap_ip.ip.addr, 1);
    state->napt_enabled = true;
    state->nat_enable_count++;
    state->last_error[0] = '\0';

    ESP_LOGI(TAG, "NAPT enabled on " IPSTR, IP2STR(&ap_ip.ip));
    return ESP_OK;
}

esp_err_t router_nat_disable(router_state_t *state)
{
    esp_netif_ip_info_t ap_ip = {0};

    if (!state->napt_enabled) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(esp_netif_get_ip_info(state->ap_netif, &ap_ip));
    ip_napt_enable(ap_ip.ip.addr, 0);
    state->napt_enabled = false;
    state->nat_disable_count++;

    ESP_LOGI(TAG, "NAPT disabled on " IPSTR, IP2STR(&ap_ip.ip));
    return ESP_OK;
}
