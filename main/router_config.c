#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"

#include "router_config.h"
#include "router_storage.h"

static const char *TAG = "router_config";
static const char *ROUTER_OLD_PROBE_URL = "http://connectivitycheck.gstatic.com/generate_204";
static const char *ROUTER_NEVERSL_PROBE_URL = "http://neverssl.com/";

#ifdef CONFIG_ROUTER_VERBOSE_WIFI_LOG
#define ROUTER_VERBOSE_WIFI_LOG_DEFAULT true
#else
#define ROUTER_VERBOSE_WIFI_LOG_DEFAULT false
#endif

#ifdef CONFIG_ROUTER_VERBOSE_DNS_LOG
#define ROUTER_VERBOSE_DNS_LOG_DEFAULT true
#else
#define ROUTER_VERBOSE_DNS_LOG_DEFAULT false
#endif

#ifdef CONFIG_ROUTER_VERBOSE_PROBE_LOG
#define ROUTER_VERBOSE_PROBE_LOG_DEFAULT true
#else
#define ROUTER_VERBOSE_PROBE_LOG_DEFAULT false
#endif

#ifdef CONFIG_ROUTER_VERBOSE_DIAG_LOG
#define ROUTER_VERBOSE_DIAG_LOG_DEFAULT true
#else
#define ROUTER_VERBOSE_DIAG_LOG_DEFAULT false
#endif

#ifdef CONFIG_ROUTER_STA_MAC
#define ROUTER_STA_MAC_DEFAULT CONFIG_ROUTER_STA_MAC
#else
#define ROUTER_STA_MAC_DEFAULT ""
#endif

void router_config_load_defaults(router_config_t *config)
{
    memset(config, 0, sizeof(*config));

    snprintf(config->sta_ssid, sizeof(config->sta_ssid), "%s", CONFIG_ROUTER_STA_SSID);
    snprintf(config->sta_password, sizeof(config->sta_password), "%s", CONFIG_ROUTER_STA_PASSWORD);
    snprintf(config->sta_mac, sizeof(config->sta_mac), "%s", ROUTER_STA_MAC_DEFAULT);
    config->sta_max_retry = CONFIG_ROUTER_STA_MAXIMUM_RETRY;

    snprintf(config->ap_ssid, sizeof(config->ap_ssid), "%s", CONFIG_ROUTER_AP_SSID);
    snprintf(config->ap_password, sizeof(config->ap_password), "%s", CONFIG_ROUTER_AP_PASSWORD);
    config->ap_channel = CONFIG_ROUTER_AP_CHANNEL;
    config->ap_max_connection = CONFIG_ROUTER_AP_MAX_CONNECTIONS;
    config->ap_auth_open = (config->ap_password[0] == '\0');

    snprintf(config->ap_ip, sizeof(config->ap_ip), "%s", CONFIG_ROUTER_AP_IP_ADDR);
    snprintf(config->ap_gateway, sizeof(config->ap_gateway), "%s", CONFIG_ROUTER_AP_GW_ADDR);
    snprintf(config->ap_netmask, sizeof(config->ap_netmask), "%s", CONFIG_ROUTER_AP_NETMASK_ADDR);
    snprintf(config->ap_dns_fallback, sizeof(config->ap_dns_fallback), "%s", CONFIG_ROUTER_AP_DNS_FALLBACK);
    snprintf(config->probe_url, sizeof(config->probe_url), "%s", CONFIG_ROUTER_PROBE_URL);
    config->probe_interval_ms = CONFIG_ROUTER_PROBE_INTERVAL_MS;
    config->diag_interval_ms = CONFIG_ROUTER_DIAG_INTERVAL_MS;
    config->verbose_wifi_log = ROUTER_VERBOSE_WIFI_LOG_DEFAULT;
    config->verbose_dns_log = ROUTER_VERBOSE_DNS_LOG_DEFAULT;
    config->verbose_probe_log = ROUTER_VERBOSE_PROBE_LOG_DEFAULT;
    config->verbose_diag_log = ROUTER_VERBOSE_DIAG_LOG_DEFAULT;
}

void router_config_load(router_config_t *config)
{
    router_config_load_defaults(config);
    if (router_storage_load(config) != ESP_OK) {
        router_config_load_defaults(config);
    }

    if (config->probe_url[0] == '\0' || strcmp(config->probe_url, ROUTER_OLD_PROBE_URL) == 0) {
        strncpy(config->probe_url, ROUTER_NEVERSL_PROBE_URL, sizeof(config->probe_url) - 1);
        config->probe_url[sizeof(config->probe_url) - 1] = '\0';
        ESP_LOGI(TAG, "Probe URL normalized to %s", config->probe_url);
    }
}
