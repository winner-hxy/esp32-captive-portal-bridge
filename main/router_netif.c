#include <stdbool.h>

#include "esp_log.h"
#include "lwip/ip4_addr.h"

#include "router_netif.h"

static const char *TAG = "router_netif";

static esp_err_t router_parse_ipv4(const char *text, esp_ip4_addr_t *out_addr)
{
    ip4_addr_t lwip_addr;

    if (!ip4addr_aton(text, &lwip_addr)) {
        return ESP_ERR_INVALID_ARG;
    }

    out_addr->addr = lwip_addr.addr;
    return ESP_OK;
}

static esp_err_t router_configure_ap_ip(router_state_t *state)
{
    esp_netif_ip_info_t ip_info = {0};

    ESP_ERROR_CHECK(router_parse_ipv4(state->config.ap_ip, &ip_info.ip));
    ESP_ERROR_CHECK(router_parse_ipv4(state->config.ap_gateway, &ip_info.gw));
    ESP_ERROR_CHECK(router_parse_ipv4(state->config.ap_netmask, &ip_info.netmask));

    ESP_ERROR_CHECK(esp_netif_dhcps_stop(state->ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(state->ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(state->ap_netif));

    ESP_LOGI(TAG, "SoftAP netif: ip=" IPSTR " gw=" IPSTR " mask=" IPSTR,
             IP2STR(&ip_info.ip),
             IP2STR(&ip_info.gw),
             IP2STR(&ip_info.netmask));
    return ESP_OK;
}

esp_err_t router_netif_init(router_state_t *state)
{
    state->ap_netif = esp_netif_create_default_wifi_ap();
    state->sta_netif = esp_netif_create_default_wifi_sta();
    if (state->ap_netif == NULL || state->sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create AP/STA netif");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(router_configure_ap_ip(state));
    ESP_ERROR_CHECK(router_netif_set_ap_dns_by_ipv4(state, state->config.ap_dns_fallback));
    return ESP_OK;
}

esp_err_t router_netif_set_upstream_dns(router_state_t *state, const esp_netif_dns_info_t *dns_info)
{
    if (dns_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    state->upstream_dns = dns_info->ip;
    state->upstream_dns_valid = (dns_info->ip.type == ESP_IPADDR_TYPE_V4 &&
                                 dns_info->ip.u_addr.ip4.addr != 0);
    if (dns_info->ip.type == ESP_IPADDR_TYPE_V4) {
        ESP_LOGI(TAG, "Upstream DNS: " IPSTR, IP2STR(&dns_info->ip.u_addr.ip4));
    }

    return ESP_OK;
}

esp_err_t router_netif_set_ap_dns_by_ipv4(router_state_t *state, const char *ipv4_text)
{
    esp_netif_dns_info_t dns_info = {0};

    if (state == NULL || ipv4_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(router_parse_ipv4(ipv4_text, &dns_info.ip.u_addr.ip4));
    dns_info.ip.type = ESP_IPADDR_TYPE_V4;
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(state->ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(state->ap_netif, ESP_NETIF_DNS_MAIN, &dns_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(state->ap_netif));

    ESP_LOGI(TAG, "SoftAP DHCP DNS set to " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
    return ESP_OK;
}

esp_err_t router_netif_set_ap_dns_to_upstream(router_state_t *state)
{
    esp_netif_dns_info_t dns_info = {0};

    if (state == NULL || !state->upstream_dns_valid || state->upstream_dns.type != ESP_IPADDR_TYPE_V4) {
        return ESP_ERR_INVALID_STATE;
    }

    dns_info.ip = state->upstream_dns;
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(state->ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(state->ap_netif, ESP_NETIF_DNS_MAIN, &dns_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(state->ap_netif));
    ESP_LOGI(TAG, "SoftAP DHCP DNS switched to upstream " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
    return ESP_OK;
}
