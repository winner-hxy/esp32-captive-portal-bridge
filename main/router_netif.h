#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "router_config.h"

typedef enum {
    ROUTER_STAGE_BOOT = 0,
    ROUTER_STAGE_AP_READY,
    ROUTER_STAGE_STA_CONNECTING,
    ROUTER_STAGE_STA_CONNECTED_NO_IP,
    ROUTER_STAGE_UPSTREAM_READY,
    ROUTER_STAGE_AUTH_REQUIRED,
    ROUTER_STAGE_ROUTING_ACTIVE,
} router_stage_t;

typedef enum {
    ROUTER_DNS_MODE_NONE = 0,
    ROUTER_DNS_MODE_CAPTIVE,
    ROUTER_DNS_MODE_UPSTREAM,
} router_dns_mode_t;

typedef struct {
    router_config_t config;
    esp_netif_t *ap_netif;
    esp_netif_t *sta_netif;
    httpd_handle_t portal_server;
    esp_event_handler_instance_t wifi_event_instance;
    esp_event_handler_instance_t ip_event_instance;
    bool sta_connected;
    bool sta_has_ip;
    bool napt_enabled;
    bool auth_check_suspect_captive;
    bool probe_success;
    bool upstream_dns_valid;
    int last_probe_status_code;
    bool last_probe_redirect;
    uint16_t sta_retry_count;
    uint32_t sta_disconnect_count;
    uint32_t nat_enable_count;
    uint32_t nat_disable_count;
    uint32_t dns_proxy_fail_count;
    uint32_t probe_fail_count;
    router_stage_t stage;
    router_dns_mode_t last_dns_mode;
    uint32_t dns_captive_reply_count;
    uint32_t dns_upstream_reply_count;
    char last_error[128];
    TaskHandle_t probe_task_handle;
    TaskHandle_t dns_task_handle;
    TaskHandle_t diag_task_handle;
    esp_ip_addr_t upstream_dns;
    esp_netif_ip_info_t sta_ip_info;
} router_state_t;

esp_err_t router_netif_init(router_state_t *state);
esp_err_t router_netif_set_upstream_dns(router_state_t *state, const esp_netif_dns_info_t *dns_info);
esp_err_t router_netif_set_ap_dns_by_ipv4(router_state_t *state, const char *ipv4_text);
esp_err_t router_netif_set_ap_dns_to_upstream(router_state_t *state);
