#include <inttypes.h>

#include "esp_mac.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "router_diag.h"
#include "router_portal.h"
static const char *TAG = "router_diag";

static const char *router_diag_dns_mode(const router_state_t *state)
{
    if (!state->upstream_dns_valid) {
        return "fallback";
    }

    return "upstream";
}

static const char *router_diag_last_dns_mode(router_dns_mode_t mode)
{
    switch (mode) {
    case ROUTER_DNS_MODE_CAPTIVE:
        return "captive";
    case ROUTER_DNS_MODE_UPSTREAM:
        return "upstream";
    case ROUTER_DNS_MODE_NONE:
    default:
        return "none";
    }
}

static void router_diag_print_sta_ip(const router_state_t *state)
{
    if (!state->sta_has_ip) {
        ESP_LOGI(TAG, "STA IP: none");
        return;
    }

    ESP_LOGI(TAG, "STA IP=" IPSTR " mask=" IPSTR " gw=" IPSTR,
             IP2STR(&state->sta_ip_info.ip),
             IP2STR(&state->sta_ip_info.netmask),
             IP2STR(&state->sta_ip_info.gw));
}

static void router_diag_print_upstream_dns(const router_state_t *state)
{
    if (state->upstream_dns_valid && state->upstream_dns.type == ESP_IPADDR_TYPE_V4) {
        ESP_LOGI(TAG, "Upstream DNS=" IPSTR, IP2STR(&state->upstream_dns.u_addr.ip4));
        return;
    }

    ESP_LOGI(TAG, "Upstream DNS: none");
}

static void router_diag_print_ap_clients(void)
{
    wifi_sta_list_t sta_list = {0};
    esp_err_t err = esp_wifi_ap_get_sta_list(&sta_list);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get AP client list: %s", esp_err_to_name(err));
        return;
    }

    if (sta_list.num <= 0) {
        return;
    }

    ESP_LOGI(TAG, "AP clients=%d", sta_list.num);
    for (int i = 0; i < sta_list.num; ++i) {
        ESP_LOGI(TAG, "Client[%d] MAC=" MACSTR " RSSI=%d",
                 i, MAC2STR(sta_list.sta[i].mac), sta_list.sta[i].rssi);
    }
}

static void router_diag_task(void *arg)
{
    router_state_t *state = (router_state_t *)arg;

    ESP_LOGI(TAG, "Diag task started, interval=%" PRIu32 "ms", state->config.diag_interval_ms);

    while (true) {
        if (!state->config.verbose_diag_log) {
            vTaskDelay(pdMS_TO_TICKS(state->config.diag_interval_ms));
            continue;
        }

        ESP_LOGI(TAG, "diag: stage=%s sta=%s ip=%s napt=%s dns=%s",
                 router_stage_to_string(state->stage),
                 state->sta_connected ? "yes" : "no",
                 state->sta_has_ip ? "yes" : "no",
                 state->napt_enabled ? "on" : "off",
                 router_diag_dns_mode(state));
        ESP_LOGI(TAG, "diag: sta_ssid=%s",
                 state->config.sta_ssid[0] ? state->config.sta_ssid : "(empty)");
        router_diag_print_sta_ip(state);
        router_diag_print_upstream_dns(state);
        if (state->last_error[0] != '\0') {
            ESP_LOGW(TAG, "diag: last_error=%s", state->last_error);
        }
        if (state->dns_proxy_fail_count != 0 || state->sta_disconnect_count != 0) {
            ESP_LOGI(TAG,
                     "diag: counters sta_disc=%" PRIu32 " nat_on=%" PRIu32 " nat_off=%" PRIu32
                     " dns_fail=%" PRIu32 " dns_up=%" PRIu32 " dns_local=%" PRIu32 " last_dns=%s",
                     state->sta_disconnect_count,
                     state->nat_enable_count,
                     state->nat_disable_count,
                     state->dns_proxy_fail_count,
                     state->dns_upstream_reply_count,
                     state->dns_captive_reply_count,
                     router_diag_last_dns_mode(state->last_dns_mode));
        }
        router_diag_print_ap_clients();

        vTaskDelay(pdMS_TO_TICKS(state->config.diag_interval_ms));
    }
}

esp_err_t router_diag_start(router_state_t *state)
{
    BaseType_t ok = xTaskCreate(
        router_diag_task,
        "router_diag",
        6144,
        state,
        3,
        &state->diag_task_handle);

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create diag task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
