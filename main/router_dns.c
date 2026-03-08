#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "router_dns.h"
#include "router_log.h"

static const char *TAG = "router_dns";

#define ROUTER_DNS_PORT 53
#define ROUTER_DNS_MAX_PACKET 512

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} router_dns_header_t;

static size_t router_dns_skip_name(const uint8_t *packet, size_t packet_len, size_t offset)
{
    while (offset < packet_len) {
        uint8_t len = packet[offset];
        if (len == 0) {
            return offset + 1;
        }
        if ((len & 0xC0) == 0xC0) {
            return offset + 2;
        }
        offset += (size_t)len + 1;
    }

    return 0;
}

static bool router_dns_build_servfail_reply(const uint8_t *request,
                                            size_t request_len,
                                            uint8_t *response,
                                            size_t *response_len)
{
    router_dns_header_t header;

    if (request_len < sizeof(router_dns_header_t) || response == NULL || response_len == NULL) {
        return false;
    }

    memcpy(&header, request, sizeof(header));
    memcpy(response, request, request_len);
    header.flags = htons(0x8182);
    header.ancount = 0;
    header.nscount = 0;
    header.arcount = 0;
    memcpy(response, &header, sizeof(header));
    *response_len = request_len;
    return true;
}

static bool router_dns_proxy_request(router_state_t *state,
                                     const uint8_t *request,
                                     size_t request_len,
                                     uint8_t *response,
                                     size_t *response_len)
{
    int sock = -1;
    struct sockaddr_in upstream = {0};
    struct timeval timeout = {
        .tv_sec = 2,
        .tv_usec = 0,
    };
    int len;

    if (!state->upstream_dns_valid || state->upstream_dns.type != ESP_IPADDR_TYPE_V4) {
        state->dns_proxy_fail_count++;
        snprintf(state->last_error, sizeof(state->last_error), "DNS proxy failed: invalid upstream DNS");
        return false;
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        state->dns_proxy_fail_count++;
        snprintf(state->last_error, sizeof(state->last_error), "DNS proxy failed: socket");
        return false;
    }

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    upstream.sin_family = AF_INET;
    upstream.sin_port = htons(ROUTER_DNS_PORT);
    upstream.sin_addr.s_addr = state->upstream_dns.u_addr.ip4.addr;

    if (sendto(sock, request, request_len, 0, (struct sockaddr *)&upstream, sizeof(upstream)) < 0) {
        state->dns_proxy_fail_count++;
        snprintf(state->last_error, sizeof(state->last_error), "DNS proxy failed: sendto");
        close(sock);
        return false;
    }

    len = recvfrom(sock, response, ROUTER_DNS_MAX_PACKET, 0, NULL, NULL);
    close(sock);
    if (len <= 0) {
        state->dns_proxy_fail_count++;
        snprintf(state->last_error, sizeof(state->last_error), "DNS proxy failed: no upstream reply");
        return false;
    }

    *response_len = (size_t)len;
    return true;
}

static void router_dns_task(void *arg)
{
    router_state_t *state = (router_state_t *)arg;
    int sock = -1;
    struct sockaddr_in listen_addr = {0};
    uint8_t request[ROUTER_DNS_MAX_PACKET];
    uint8_t response[ROUTER_DNS_MAX_PACKET];

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        snprintf(state->last_error, sizeof(state->last_error), "Local DNS start failed: socket");
        ESP_LOGE(TAG, "Failed to create local DNS socket");
        vTaskDelete(NULL);
        return;
    }

    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(ROUTER_DNS_PORT);
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        snprintf(state->last_error, sizeof(state->last_error), "Local DNS start failed: bind");
        ESP_LOGE(TAG, "Failed to bind local DNS port %d", ROUTER_DNS_PORT);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Local DNS started on UDP/%d", ROUTER_DNS_PORT);

    while (true) {
        struct sockaddr_in source_addr = {0};
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, request, sizeof(request), 0, (struct sockaddr *)&source_addr, &socklen);
        size_t response_len = 0;
        bool sent = false;

        if (len <= 0) {
            continue;
        }

        if (state->upstream_dns_valid &&
            router_dns_proxy_request(state, request, (size_t)len, response, &response_len)) {
            sendto(sock, response, response_len, 0, (struct sockaddr *)&source_addr, socklen);
            state->last_dns_mode = ROUTER_DNS_MODE_UPSTREAM;
            state->dns_upstream_reply_count++;
            ROUTER_LOG_VERBOSE(state->config.verbose_dns_log,
                               TAG,
                               "DNS upstream reply count=%" PRIu32,
                               state->dns_upstream_reply_count);
            sent = true;
        }

        if (!sent && router_dns_build_servfail_reply(request, (size_t)len, response, &response_len)) {
            sendto(sock, response, response_len, 0, (struct sockaddr *)&source_addr, socklen);
            state->last_dns_mode = ROUTER_DNS_MODE_NONE;
            state->dns_captive_reply_count++;
            ROUTER_LOG_VERBOSE(state->config.verbose_dns_log,
                               TAG,
                               "DNS servfail fallback count=%" PRIu32,
                               state->dns_captive_reply_count);
        }
    }
}

esp_err_t router_dns_start(router_state_t *state)
{
    BaseType_t ok = xTaskCreate(
        router_dns_task,
        "router_dns",
        6144,
        state,
        4,
        &state->dns_task_handle);

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
