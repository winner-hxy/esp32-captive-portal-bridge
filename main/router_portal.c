#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "router_portal.h"
#include "router_storage.h"

static const char *TAG = "router_portal";

static const char *router_stage_message(router_stage_t stage)
{
    switch (stage) {
    case ROUTER_STAGE_BOOT:
        return "Router is booting.";
    case ROUTER_STAGE_AP_READY:
        return "SoftAP is ready. Waiting for upstream Wi-Fi.";
    case ROUTER_STAGE_STA_CONNECTING:
        return "Connecting to upstream Wi-Fi.";
    case ROUTER_STAGE_STA_CONNECTED_NO_IP:
        return "Connected to upstream AP, waiting for DHCP.";
    case ROUTER_STAGE_UPSTREAM_READY:
        return "Upstream IP is ready. Captive portal may still require login.";
    case ROUTER_STAGE_AUTH_REQUIRED:
        return "Authentication is probably still required.";
    case ROUTER_STAGE_ROUTING_ACTIVE:
        return "Routing is active.";
    default:
        return "Unknown state.";
    }
}

const char *router_stage_to_string(router_stage_t stage)
{
    switch (stage) {
    case ROUTER_STAGE_BOOT:
        return "boot";
    case ROUTER_STAGE_AP_READY:
        return "ap_ready";
    case ROUTER_STAGE_STA_CONNECTING:
        return "sta_connecting";
    case ROUTER_STAGE_STA_CONNECTED_NO_IP:
        return "sta_connected_no_ip";
    case ROUTER_STAGE_UPSTREAM_READY:
        return "upstream_ready";
    case ROUTER_STAGE_AUTH_REQUIRED:
        return "auth_required";
    case ROUTER_STAGE_ROUTING_ACTIVE:
        return "routing_active";
    default:
        return "unknown";
    }
}

static esp_err_t router_portal_write_common_headers(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    return ESP_OK;
}

static bool router_portal_has_form_key(const char *body, const char *key)
{
    char value[8] = {0};
    return httpd_query_key_value(body, key, value, sizeof(value)) == ESP_OK;
}

static void router_portal_copy_form_value(const char *body, const char *key, char *dst, size_t dst_size)
{
    char value[160] = {0};
    size_t i = 0;
    size_t j = 0;

    if (httpd_query_key_value(body, key, value, sizeof(value)) != ESP_OK) {
        return;
    }

    while (value[i] != '\0' && j + 1 < dst_size) {
        if (value[i] == '+') {
            dst[j++] = ' ';
            i++;
            continue;
        }
        if (value[i] == '%' && value[i + 1] != '\0' && value[i + 2] != '\0') {
            char hex[3] = {value[i + 1], value[i + 2], '\0'};
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
            continue;
        }

        dst[j++] = value[i++];
    }

    dst[j] = '\0';
}

static esp_err_t router_portal_root_get(httpd_req_t *req)
{
    router_state_t *state = (router_state_t *)req->user_ctx;
    char *body = NULL;
    const char *wifi_checked = state->config.verbose_wifi_log ? "checked" : "";
    const char *dns_checked = state->config.verbose_dns_log ? "checked" : "";
    const char *probe_checked = state->config.verbose_probe_log ? "checked" : "";
    const char *diag_checked = state->config.verbose_diag_log ? "checked" : "";
    const char *upstream_dns = "(unknown)";
    char upstream_dns_buf[32] = {0};
    char sta_ip_buf[32] = "none";
    char sta_gw_buf[32] = "none";
    const char *last_dns_mode =
        state->last_dns_mode == ROUTER_DNS_MODE_CAPTIVE ? "captive" :
        (state->last_dns_mode == ROUTER_DNS_MODE_UPSTREAM ? "upstream" : "none");

    if (state->upstream_dns_valid && state->upstream_dns.type == ESP_IPADDR_TYPE_V4) {
        snprintf(upstream_dns_buf, sizeof(upstream_dns_buf), IPSTR, IP2STR(&state->upstream_dns.u_addr.ip4));
        upstream_dns = upstream_dns_buf;
    }
    if (state->sta_has_ip) {
        snprintf(sta_ip_buf, sizeof(sta_ip_buf), IPSTR, IP2STR(&state->sta_ip_info.ip));
        snprintf(sta_gw_buf, sizeof(sta_gw_buf), IPSTR, IP2STR(&state->sta_ip_info.gw));
    }

    body = calloc(1, 8192);
    if (body == NULL) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "portal page alloc failed", HTTPD_RESP_USE_STRLEN);
    }

    snprintf(
        body,
        8192,
        "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32-C6 NAT Helper</title>"
        "<style>body{font-family:system-ui,-apple-system,sans-serif;max-width:760px;margin:24px auto;padding:0 16px;line-height:1.5}"
        ".card{border:1px solid #ddd;border-radius:12px;padding:16px;margin:12px 0}.muted{color:#555}.mono{font-family:ui-monospace,monospace}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px}"
        "a.btn{display:inline-block;background:#0a7cff;color:#fff;text-decoration:none;padding:10px 14px;border-radius:10px;margin-right:8px}</style></head><body>"
        "<h1>ESP32-C6 NAT Helper</h1>"
        "<div class='card'><strong>Stage:</strong> %s<br><span class='muted'>%s</span></div>"
        "<div class='card'><strong>Upstream SSID:</strong> %s<br><strong>Upstream STA MAC:</strong> %s<br><strong>SoftAP SSID:</strong> %s<br><strong>SoftAP IP:</strong> %s</div>"
        "<div class='card'><strong>Diagnostics</strong><div class='grid'>"
        "<div><strong>STA connected</strong><br>%s</div>"
        "<div><strong>STA has IP</strong><br>%s</div>"
        "<div><strong>NAPT</strong><br>%s</div>"
        "<div><strong>Auth required</strong><br>%s</div>"
        "<div><strong>Probe success</strong><br>%s</div>"
        "<div><strong>Last probe</strong><br>status=%d redirect=%s</div>"
        "<div><strong>STA IP</strong><br><span class='mono'>%s</span></div>"
        "<div><strong>STA GW</strong><br><span class='mono'>%s</span></div>"
        "<div><strong>Upstream DNS</strong><br><span class='mono'>%s</span></div>"
        "<div><strong>Last DNS mode</strong><br>%s</div>"
        "<div><strong>DNS captive</strong><br>%" PRIu32 "</div>"
        "<div><strong>DNS upstream</strong><br>%" PRIu32 "</div>"
        "<div><strong>STA disconnects</strong><br>%" PRIu32 "</div>"
        "<div><strong>NAPT on count</strong><br>%" PRIu32 "</div>"
        "<div><strong>NAPT off count</strong><br>%" PRIu32 "</div>"
        "<div><strong>Probe fail count</strong><br>%" PRIu32 "</div>"
        "<div><strong>DNS proxy fail</strong><br>%" PRIu32 "</div>"
        "</div><div style='margin-top:10px'><strong>Last error</strong><br><span class='mono'>%s</span></div></div>"
        "<div class='card'><strong>How to use</strong><ol>"
        "<li>Connect your phone to <code>%s</code>.</li>"
        "<li>Tap <em>Open HTTP Test Page</em>.</li>"
        "<li>Complete the hotel login page.</li>"
        "<li>If auto-detection does not switch state, tap <em>Authentication Finished</em>.</li>"
        "</ol>"
        "<a class='btn' href='http://neverssl.com/' target='_blank' rel='noreferrer'>Open HTTP Test Page</a>"
        "<a class='btn' href='/mark_auth_ok'>Authentication Finished</a>"
        "<a class='btn' href='/status'>View JSON Status</a></div>"
        "<div class='card'><strong>Saved Config</strong>"
        "<form method='post' action='/config'>"
        "Upstream SSID<br><input name='sta_ssid' value='%s' style='width:100%%;margin:6px 0'><br>"
        "Upstream Password<br><input name='sta_password' value='%s' style='width:100%%;margin:6px 0'><br>"
        "Upstream STA MAC<br><input name='sta_mac' value='%s' placeholder='aa:bb:cc:dd:ee:ff' style='width:100%%;margin:6px 0'><br>"
        "SoftAP SSID<br><input name='ap_ssid' value='%s' style='width:100%%;margin:6px 0'><br>"
        "SoftAP Password<br><input name='ap_password' value='%s' style='width:100%%;margin:6px 0'><br>"
        "Probe URL<br><input name='probe_url' value='%s' style='width:100%%;margin:6px 0'><br>"
        "<div style='margin:10px 0'>"
        "<label><input type='checkbox' name='verbose_wifi_log' %s> Verbose WIFI</label><br>"
        "<label><input type='checkbox' name='verbose_dns_log' %s> Verbose DNS</label><br>"
        "<label><input type='checkbox' name='verbose_probe_log' %s> Verbose PROBE</label><br>"
        "<label><input type='checkbox' name='verbose_diag_log' %s> Periodic DIAG</label>"
        "</div>"
        "<button type='submit'>Save To NVS</button>"
        "</form><div class='muted'>Saved values take effect after reboot.</div></div>"
        "</body></html>",
        router_stage_to_string(state->stage),
        router_stage_message(state->stage),
        state->config.sta_ssid[0] ? state->config.sta_ssid : "(not configured)",
        state->config.sta_mac[0] ? state->config.sta_mac : "(factory)",
        state->config.ap_ssid,
        state->config.ap_ip,
        state->sta_connected ? "yes" : "no",
        state->sta_has_ip ? "yes" : "no",
        state->napt_enabled ? "on" : "off",
        state->auth_check_suspect_captive ? "yes" : "no",
        state->probe_success ? "yes" : "no",
        state->last_probe_status_code,
        state->last_probe_redirect ? "yes" : "no",
        sta_ip_buf,
        sta_gw_buf,
        upstream_dns,
        last_dns_mode,
        state->dns_captive_reply_count,
        state->dns_upstream_reply_count,
        state->sta_disconnect_count,
        state->nat_enable_count,
        state->nat_disable_count,
        state->probe_fail_count,
        state->dns_proxy_fail_count,
        state->last_error[0] ? state->last_error : "(none)",
        state->config.ap_ssid,
        state->config.sta_ssid,
        state->config.sta_password,
        state->config.sta_mac,
        state->config.ap_ssid,
        state->config.ap_password,
        state->config.probe_url,
        wifi_checked,
        dns_checked,
        probe_checked,
        diag_checked);

    ESP_ERROR_CHECK(router_portal_write_common_headers(req));
    esp_err_t ret = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return ret;
}

static esp_err_t router_portal_status_get(httpd_req_t *req)
{
    router_state_t *state = (router_state_t *)req->user_ctx;
    char body[512];

    snprintf(body,
             sizeof(body),
             "{\"stage\":\"%s\",\"sta_connected\":%s,\"sta_has_ip\":%s,\"napt_enabled\":%s,"
             "\"auth_suspected\":%s,\"probe_success\":%s,\"ap_ssid\":\"%s\",\"upstream_ssid\":\"%s\"}",
             router_stage_to_string(state->stage),
             state->sta_connected ? "true" : "false",
             state->sta_has_ip ? "true" : "false",
             state->napt_enabled ? "true" : "false",
             state->auth_check_suspect_captive ? "true" : "false",
             state->probe_success ? "true" : "false",
             state->config.ap_ssid,
             state->config.sta_ssid);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t router_portal_mark_auth_ok(httpd_req_t *req)
{
    router_state_t *state = (router_state_t *)req->user_ctx;

    state->auth_check_suspect_captive = false;
    state->probe_success = true;
    state->stage = state->napt_enabled ? ROUTER_STAGE_ROUTING_ACTIVE : ROUTER_STAGE_UPSTREAM_READY;

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t router_portal_config_post(httpd_req_t *req)
{
    router_state_t *state = (router_state_t *)req->user_ctx;
    char body[768];
    int received = 0;
    router_config_t updated = state->config;

    if (req->content_len <= 0 || req->content_len >= (int)sizeof(body)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "invalid form body", HTTPD_RESP_USE_STRLEN);
    }

    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            httpd_resp_set_status(req, "400 Bad Request");
            return httpd_resp_send(req, "failed to read request body", HTTPD_RESP_USE_STRLEN);
        }
        received += ret;
    }
    body[received] = '\0';

    router_portal_copy_form_value(body, "sta_ssid", updated.sta_ssid, sizeof(updated.sta_ssid));
    router_portal_copy_form_value(body, "sta_password", updated.sta_password, sizeof(updated.sta_password));
    router_portal_copy_form_value(body, "sta_mac", updated.sta_mac, sizeof(updated.sta_mac));
    router_portal_copy_form_value(body, "ap_ssid", updated.ap_ssid, sizeof(updated.ap_ssid));
    router_portal_copy_form_value(body, "ap_password", updated.ap_password, sizeof(updated.ap_password));
    router_portal_copy_form_value(body, "probe_url", updated.probe_url, sizeof(updated.probe_url));
    updated.verbose_wifi_log = router_portal_has_form_key(body, "verbose_wifi_log");
    updated.verbose_dns_log = router_portal_has_form_key(body, "verbose_dns_log");
    updated.verbose_probe_log = router_portal_has_form_key(body, "verbose_probe_log");
    updated.verbose_diag_log = router_portal_has_form_key(body, "verbose_diag_log");
    updated.ap_auth_open = (updated.ap_password[0] == '\0');

    if (!updated.ap_auth_open && strlen(updated.ap_password) < 8) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "ap_password must be empty or at least 8 chars", HTTPD_RESP_USE_STRLEN);
    }

    ESP_ERROR_CHECK(router_storage_save(&updated));
    state->config = updated;
    ESP_LOGI(TAG, "Portal saved config. Reboot to apply.");
    ESP_LOGI(TAG, "Verbose flags: WIFI=%s DNS=%s PROBE=%s DIAG=%s",
             state->config.verbose_wifi_log ? "on" : "off",
             state->config.verbose_dns_log ? "on" : "off",
             state->config.verbose_probe_log ? "on" : "off",
             state->config.verbose_diag_log ? "on" : "off");

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t router_portal_generate_204(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t router_portal_hotspot_detect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t router_portal_ncsi(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "Microsoft NCSI redirected to local helper portal", HTTPD_RESP_USE_STRLEN);
}

esp_err_t router_portal_start(router_state_t *state)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = router_portal_root_get,
        .user_ctx = state,
    };
    httpd_uri_t status = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = router_portal_status_get,
        .user_ctx = state,
    };
    httpd_uri_t config_post = {
        .uri = "/config",
        .method = HTTP_POST,
        .handler = router_portal_config_post,
        .user_ctx = state,
    };
    httpd_uri_t generate_204 = {
        .uri = "/generate_204",
        .method = HTTP_GET,
        .handler = router_portal_generate_204,
        .user_ctx = state,
    };
    httpd_uri_t mark_auth_ok = {
        .uri = "/mark_auth_ok",
        .method = HTTP_GET,
        .handler = router_portal_mark_auth_ok,
        .user_ctx = state,
    };
    httpd_uri_t hotspot_detect = {
        .uri = "/hotspot-detect.html",
        .method = HTTP_GET,
        .handler = router_portal_hotspot_detect,
        .user_ctx = state,
    };
    httpd_uri_t ncsi = {
        .uri = "/ncsi.txt",
        .method = HTTP_GET,
        .handler = router_portal_ncsi,
        .user_ctx = state,
    };
    httpd_uri_t connecttest = {
        .uri = "/connecttest.txt",
        .method = HTTP_GET,
        .handler = router_portal_ncsi,
        .user_ctx = state,
    };
    httpd_uri_t fwlink = {
        .uri = "/fwlink",
        .method = HTTP_GET,
        .handler = router_portal_hotspot_detect,
        .user_ctx = state,
    };

    config.server_port = 80;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 12;

    if (httpd_start(&state->portal_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start helper portal");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(httpd_register_uri_handler(state->portal_server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(state->portal_server, &status));
    ESP_ERROR_CHECK(httpd_register_uri_handler(state->portal_server, &config_post));
    ESP_ERROR_CHECK(httpd_register_uri_handler(state->portal_server, &generate_204));
    ESP_ERROR_CHECK(httpd_register_uri_handler(state->portal_server, &mark_auth_ok));
    ESP_ERROR_CHECK(httpd_register_uri_handler(state->portal_server, &hotspot_detect));
    ESP_ERROR_CHECK(httpd_register_uri_handler(state->portal_server, &ncsi));
    ESP_ERROR_CHECK(httpd_register_uri_handler(state->portal_server, &connecttest));
    ESP_ERROR_CHECK(httpd_register_uri_handler(state->portal_server, &fwlink));

    state->stage = ROUTER_STAGE_AP_READY;
    ESP_LOGI(TAG, "Helper portal started at http://%s/", state->config.ap_ip);
    return ESP_OK;
}
