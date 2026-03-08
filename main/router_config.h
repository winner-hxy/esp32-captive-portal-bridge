#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ROUTER_SSID_MAX_LEN 32
#define ROUTER_PASSWORD_MAX_LEN 64
#define ROUTER_IPV4_STR_LEN 16
#define ROUTER_MAC_STR_LEN 18

typedef struct {
    char sta_ssid[ROUTER_SSID_MAX_LEN + 1];
    char sta_password[ROUTER_PASSWORD_MAX_LEN + 1];
    char sta_mac[ROUTER_MAC_STR_LEN];
    uint16_t sta_max_retry;

    char ap_ssid[ROUTER_SSID_MAX_LEN + 1];
    char ap_password[ROUTER_PASSWORD_MAX_LEN + 1];
    uint8_t ap_channel;
    uint8_t ap_max_connection;
    bool ap_auth_open;

    char ap_ip[ROUTER_IPV4_STR_LEN];
    char ap_gateway[ROUTER_IPV4_STR_LEN];
    char ap_netmask[ROUTER_IPV4_STR_LEN];
    char ap_dns_fallback[ROUTER_IPV4_STR_LEN];

    char probe_url[128];
    uint32_t probe_interval_ms;
    uint32_t diag_interval_ms;
    bool verbose_wifi_log;
    bool verbose_dns_log;
    bool verbose_probe_log;
    bool verbose_diag_log;
} router_config_t;

void router_config_load(router_config_t *config);
void router_config_load_defaults(router_config_t *config);
