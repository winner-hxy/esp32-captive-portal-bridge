#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"

#include "router_log.h"
#include "router_portal.h"
#include "router_probe.h"

static const char *TAG = "router_probe";

typedef struct {
    int status_code;
    bool saw_redirect;
} router_probe_result_t;

static esp_err_t router_probe_http_event(esp_http_client_event_t *evt)
{
    router_probe_result_t *result = (router_probe_result_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_REDIRECT && result != NULL) {
        result->saw_redirect = true;
    }

    return ESP_OK;
}

static bool router_probe_is_required(const router_state_t *state)
{
    return state->sta_has_ip && state->auth_check_suspect_captive;
}

static bool router_probe_check_connectivity(router_state_t *state)
{
    router_probe_result_t result = {0};
    esp_http_client_config_t config = {
        .url = state->config.probe_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 4000,
        .disable_auto_redirect = true,
        .event_handler = router_probe_http_event,
        .user_data = &result,
        .buffer_size = 512,
        .buffer_size_tx = 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;

    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to create HTTP probe client");
        return false;
    }

    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        result.status_code = esp_http_client_get_status_code(client);
        state->last_probe_status_code = result.status_code;
        state->last_probe_redirect = result.saw_redirect;
        ROUTER_LOG_VERBOSE(state->config.verbose_probe_log,
                           TAG,
                           "Probe result: url=%s status=%d redirect=%s",
                           state->config.probe_url,
                           result.status_code,
                           result.saw_redirect ? "true" : "false");
    } else {
        state->last_probe_status_code = -1;
        state->last_probe_redirect = false;
        state->probe_fail_count++;
        snprintf(state->last_error, sizeof(state->last_error), "Probe failed: %s", esp_err_to_name(err));
        ESP_LOGW(TAG, "Probe request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        return false;
    }

    return !result.saw_redirect && result.status_code >= 200 && result.status_code < 300;
}

static void router_probe_task(void *arg)
{
    router_state_t *state = (router_state_t *)arg;

    ROUTER_LOG_VERBOSE(state->config.verbose_probe_log,
                       TAG,
                       "Probe task started: interval=%" PRIu32 " url=%s",
                       state->config.probe_interval_ms,
                       state->config.probe_url);

    while (true) {
        if (router_probe_is_required(state)) {
            bool ok = router_probe_check_connectivity(state);

            state->probe_success = ok;
            if (ok) {
                state->auth_check_suspect_captive = false;
                state->stage = state->napt_enabled ? ROUTER_STAGE_ROUTING_ACTIVE : ROUTER_STAGE_UPSTREAM_READY;
                state->last_error[0] = '\0';
                ROUTER_LOG_VERBOSE(state->config.verbose_probe_log,
                                   TAG,
                                   "Probe passed, stage=%s",
                                   router_stage_to_string(state->stage));
            } else {
                state->stage = ROUTER_STAGE_AUTH_REQUIRED;
                state->last_error[0] = '\0';
            }
        } else if (!state->sta_has_ip) {
            state->probe_success = false;
            state->last_probe_status_code = -1;
            state->last_probe_redirect = false;
        }

        vTaskDelay(pdMS_TO_TICKS(state->config.probe_interval_ms));
    }
}

esp_err_t router_probe_start(router_state_t *state)
{
    BaseType_t ok = xTaskCreate(
        router_probe_task,
        "router_probe",
        6144,
        state,
        4,
        &state->probe_task_handle);

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create probe task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
