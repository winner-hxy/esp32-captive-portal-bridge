#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "router_config.h"
#include "router_diag.h"
#include "router_nat.h"
#include "router_netif.h"
#include "router_system.h"
#include "router_wifi.h"

static const char *TAG = "nat_router_main";
static const char *ROUTER_FW_REV = "nat-mac-test-20260308-01";
static router_state_t g_router_state;

void app_main(void)
{
    router_state_t *state = &g_router_state;

    memset(state, 0, sizeof(*state));
    state->last_probe_status_code = -1;

    ESP_LOGI(TAG, "Starting ESP32-C6 NAT router");
    ESP_LOGI(TAG, "FW_REV=%s BUILD=%s %s", ROUTER_FW_REV, __DATE__, __TIME__);
    ESP_ERROR_CHECK(router_system_init());
    router_config_load(&state->config);
    ESP_LOGI(TAG, "Config: STA_SSID=%s STA_PASSWORD=%s STA_MAC=%s AP_SSID=%s AP_PASSWORD=%s PROBE_URL=%s",
             state->config.sta_ssid[0] ? state->config.sta_ssid : "(empty)",
             state->config.sta_password[0] ? state->config.sta_password : "(empty)",
             state->config.sta_mac[0] ? state->config.sta_mac : "(factory)",
             state->config.ap_ssid[0] ? state->config.ap_ssid : "(empty)",
             state->config.ap_password[0] ? state->config.ap_password : "(empty)",
             state->config.probe_url);

    ESP_ERROR_CHECK(router_netif_init(state));
    ESP_ERROR_CHECK(router_nat_init(state));
    ESP_ERROR_CHECK(router_wifi_init(state));
    ESP_ERROR_CHECK(router_wifi_start(state));
    ESP_ERROR_CHECK(router_diag_start(state));

    ESP_LOGI(TAG, "Router started. Connect phone to AP '%s'.", state->config.ap_ssid);
}
