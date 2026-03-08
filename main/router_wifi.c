#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_mac.h"
#include "esp_log.h"

#include "router_log.h"
#include "router_nat.h"
#include "router_wifi.h"

static const char *TAG = "router_wifi";

static int router_hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static esp_err_t router_parse_mac_string(const char *text, uint8_t mac[6])
{
    if (text == NULL || mac == NULL || text[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(text) != 17) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < 6; ++i) {
        int hi = router_hex_nibble(text[i * 3]);
        int lo = router_hex_nibble(text[i * 3 + 1]);

        if (hi < 0 || lo < 0) {
            return ESP_ERR_INVALID_ARG;
        }
        if (i < 5 && text[i * 3 + 2] != ':') {
            return ESP_ERR_INVALID_ARG;
        }

        mac[i] = (uint8_t)((hi << 4) | lo);
    }

    if (mac[0] & 0x01) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static esp_err_t router_apply_sta_mac(router_state_t *state)
{
    uint8_t mac[6] = {0};
    esp_err_t err;

    if (state->config.sta_mac[0] == '\0') {
        ESP_LOGI(TAG, "STA MAC: factory default");
        return ESP_OK;
    }

    err = router_parse_mac_string(state->config.sta_mac, mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Invalid STA MAC override: %s", state->config.sta_mac);
        return err;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, mac));
    ESP_LOGI(TAG, "STA MAC override applied: " MACSTR, MAC2STR(mac));
    return ESP_OK;
}

static void router_log_sta_ap_join(router_state_t *state, const wifi_event_ap_staconnected_t *event)
{
    ROUTER_LOG_VERBOSE(state->config.verbose_wifi_log,
                       TAG,
                       "AP client joined: MAC=" MACSTR " aid=%d",
                       MAC2STR(event->mac),
                       event->aid);
}

static void router_log_sta_ap_leave(router_state_t *state, const wifi_event_ap_stadisconnected_t *event)
{
    ROUTER_LOG_VERBOSE(state->config.verbose_wifi_log,
                       TAG,
                       "AP client left: MAC=" MACSTR " aid=%d",
                       MAC2STR(event->mac),
                       event->aid);
}

static void router_copy_wifi_string(uint8_t *dst, size_t dst_size, const char *src)
{
    memset(dst, 0, dst_size);
    strncpy((char *)dst, src, dst_size - 1);
}

static esp_err_t router_build_ap_config(const router_state_t *state, wifi_config_t *wifi_config)
{
    size_t password_len = strlen(state->config.ap_password);

    memset(wifi_config, 0, sizeof(*wifi_config));
    router_copy_wifi_string(wifi_config->ap.ssid, sizeof(wifi_config->ap.ssid), state->config.ap_ssid);
    router_copy_wifi_string(wifi_config->ap.password, sizeof(wifi_config->ap.password), state->config.ap_password);

    if (!state->config.ap_auth_open && password_len < 8) {
        ESP_LOGE(TAG, "SoftAP password must be empty or >= 8 chars");
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config->ap.channel = state->config.ap_channel;
    wifi_config->ap.max_connection = state->config.ap_max_connection;
    wifi_config->ap.ssid_len = strlen(state->config.ap_ssid);
    wifi_config->ap.authmode = state->config.ap_auth_open ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config->ap.pmf_cfg.required = false;

    return ESP_OK;
}

static esp_err_t router_build_sta_config(const router_state_t *state, wifi_config_t *wifi_config)
{
    memset(wifi_config, 0, sizeof(*wifi_config));
    router_copy_wifi_string(wifi_config->sta.ssid, sizeof(wifi_config->sta.ssid), state->config.sta_ssid);
    router_copy_wifi_string(wifi_config->sta.password, sizeof(wifi_config->sta.password), state->config.sta_password);

    wifi_config->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config->sta.failure_retry_cnt = state->config.sta_max_retry;
    wifi_config->sta.threshold.authmode =
        (state->config.sta_password[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config->sta.pmf_cfg.capable = true;
    wifi_config->sta.pmf_cfg.required = false;

    return ESP_OK;
}

static void router_apply_upstream_dns(router_state_t *state)
{
    esp_netif_dns_info_t dns_info = {0};

    if (esp_netif_get_dns_info(state->sta_netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK &&
        dns_info.ip.type == ESP_IPADDR_TYPE_V4 &&
        dns_info.ip.u_addr.ip4.addr != 0) {
        ESP_ERROR_CHECK(router_netif_set_upstream_dns(state, &dns_info));
        ESP_ERROR_CHECK(router_netif_set_ap_dns_to_upstream(state));
        return;
    }

    ESP_LOGW(TAG, "STA DNS unavailable, keep fallback AP DNS");
}

static void router_handle_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    router_state_t *state = (router_state_t *)arg;

    if (event_base != WIFI_EVENT) {
        return;
    }

    switch (event_id) {
    case WIFI_EVENT_STA_START:
        if (state->config.sta_ssid[0] == '\0') {
            ESP_LOGW(TAG, "STA SSID not configured, skip connect");
            state->stage = ROUTER_STAGE_AP_READY;
            break;
        }

        state->stage = ROUTER_STAGE_STA_CONNECTING;
        ROUTER_LOG_VERBOSE(state->config.verbose_wifi_log, TAG, "STA start, connecting to %s", state->config.sta_ssid);
        ESP_ERROR_CHECK(esp_wifi_connect());
        break;
    case WIFI_EVENT_STA_CONNECTED:
        state->sta_connected = true;
        state->sta_retry_count = 0;
        state->probe_success = false;
        state->last_probe_status_code = -1;
        state->last_probe_redirect = false;
        state->last_error[0] = '\0';
        state->stage = ROUTER_STAGE_STA_CONNECTED_NO_IP;
        ROUTER_LOG_VERBOSE(state->config.verbose_wifi_log, TAG, "STA connected");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        state->sta_connected = false;
        state->sta_has_ip = false;
        state->auth_check_suspect_captive = false;
        state->probe_success = false;
        state->last_probe_status_code = -1;
        state->last_probe_redirect = false;
        state->sta_disconnect_count++;
        state->upstream_dns_valid = false;
        memset(&state->upstream_dns, 0, sizeof(state->upstream_dns));
        memset(&state->sta_ip_info, 0, sizeof(state->sta_ip_info));
        state->stage = ROUTER_STAGE_STA_CONNECTING;
        ESP_ERROR_CHECK(router_nat_disable(state));

        if (state->sta_retry_count < state->config.sta_max_retry) {
            state->sta_retry_count++;
            snprintf(state->last_error, sizeof(state->last_error), "STA disconnected, retry (%u/%u)",
                     state->sta_retry_count, state->config.sta_max_retry);
            ESP_LOGW(TAG, "STA disconnected, retry (%u/%u)",
                     state->sta_retry_count, state->config.sta_max_retry);
        } else {
            snprintf(state->last_error, sizeof(state->last_error), "STA disconnected, retry limit reached");
            ESP_LOGW(TAG, "STA disconnected, retry limit reached");
        }

        if (state->config.sta_ssid[0] != '\0') {
            ESP_ERROR_CHECK(esp_wifi_connect());
        }
        break;
    case WIFI_EVENT_AP_STACONNECTED:
        router_log_sta_ap_join(state, (const wifi_event_ap_staconnected_t *)event_data);
        break;
    case WIFI_EVENT_AP_STADISCONNECTED:
        router_log_sta_ap_leave(state, (const wifi_event_ap_stadisconnected_t *)event_data);
        break;
    default:
        break;
    }
}

static void router_handle_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    router_state_t *state = (router_state_t *)arg;

    if (event_base != IP_EVENT) {
        return;
    }

    switch (event_id) {
    case IP_EVENT_STA_GOT_IP: {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;

        state->sta_has_ip = true;
        state->auth_check_suspect_captive = false;
        state->probe_success = true;
        state->last_probe_status_code = -1;
        state->last_probe_redirect = false;
        memcpy(&state->sta_ip_info, &event->ip_info, sizeof(state->sta_ip_info));
        state->last_error[0] = '\0';
        state->stage = ROUTER_STAGE_UPSTREAM_READY;
        ROUTER_LOG_VERBOSE(state->config.verbose_wifi_log,
                           TAG,
                           "STA got IP: ip=" IPSTR " mask=" IPSTR " gw=" IPSTR,
                           IP2STR(&event->ip_info.ip),
                           IP2STR(&event->ip_info.netmask),
                           IP2STR(&event->ip_info.gw));

        router_apply_upstream_dns(state);
        ESP_ERROR_CHECK(router_nat_enable(state));
        state->stage = ROUTER_STAGE_ROUTING_ACTIVE;
        break;
    }
    case IP_EVENT_STA_LOST_IP:
        state->sta_has_ip = false;
        state->auth_check_suspect_captive = false;
        state->probe_success = false;
        state->last_probe_status_code = -1;
        state->last_probe_redirect = false;
        state->upstream_dns_valid = false;
        memset(&state->upstream_dns, 0, sizeof(state->upstream_dns));
        memset(&state->sta_ip_info, 0, sizeof(state->sta_ip_info));
        state->stage = ROUTER_STAGE_STA_CONNECTED_NO_IP;
        ESP_ERROR_CHECK(router_netif_set_ap_dns_by_ipv4(state, state->config.ap_dns_fallback));
        snprintf(state->last_error, sizeof(state->last_error), "STA lost IP");
        ESP_LOGW(TAG, "STA lost IP");
        ESP_ERROR_CHECK(router_nat_disable(state));
        break;
    default:
        break;
    }
}

esp_err_t router_wifi_init(router_state_t *state)
{
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t ap_cfg;
    wifi_config_t sta_cfg;

    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(router_apply_sta_mac(state));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &router_handle_wifi_event, state, &state->wifi_event_instance));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID, &router_handle_ip_event, state, &state->ip_event_instance));

    ESP_ERROR_CHECK(router_build_ap_config(state, &ap_cfg));
    ESP_ERROR_CHECK(router_build_sta_config(state, &sta_cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));

    ROUTER_LOG_VERBOSE(state->config.verbose_wifi_log,
                       TAG,
                       "Wi-Fi init: AP SSID='%s' channel=%u max_conn=%u STA SSID='%s'",
                       state->config.ap_ssid,
                       state->config.ap_channel,
                       state->config.ap_max_connection,
                       state->config.sta_ssid);
    return ESP_OK;
}

esp_err_t router_wifi_start(router_state_t *state)
{
    ESP_ERROR_CHECK(esp_wifi_start());

    if (state->config.sta_ssid[0] == '\0') {
        ESP_LOGW(TAG, "STA SSID not configured; AP only mode for now");
    }

    return ESP_OK;
}
