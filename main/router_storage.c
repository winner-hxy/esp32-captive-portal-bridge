#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#include "router_storage.h"

static const char *TAG = "router_storage";
static const char *NAMESPACE = "router_cfg";

static void router_storage_try_load_string(nvs_handle_t nvs,
                                           const char *key,
                                           char *value,
                                           size_t value_size)
{
    size_t required = value_size;

    if (nvs_get_str(nvs, key, value, &required) == ESP_OK) {
        value[value_size - 1] = '\0';
    }
}

esp_err_t router_storage_load(router_config_t *config)
{
    nvs_handle_t nvs;
    uint8_t u8_value = 0;
    uint32_t u32_value = 0;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &nvs);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved config in NVS");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    router_storage_try_load_string(nvs, "sta_ssid", config->sta_ssid, sizeof(config->sta_ssid));
    router_storage_try_load_string(nvs, "sta_pwd", config->sta_password, sizeof(config->sta_password));
    router_storage_try_load_string(nvs, "sta_mac", config->sta_mac, sizeof(config->sta_mac));
    router_storage_try_load_string(nvs, "ap_ssid", config->ap_ssid, sizeof(config->ap_ssid));
    router_storage_try_load_string(nvs, "ap_pwd", config->ap_password, sizeof(config->ap_password));
    router_storage_try_load_string(nvs, "probe_url", config->probe_url, sizeof(config->probe_url));

    if (nvs_get_u8(nvs, "ap_ch", &u8_value) == ESP_OK) {
        config->ap_channel = u8_value;
    }
    if (nvs_get_u8(nvs, "ap_max", &u8_value) == ESP_OK) {
        config->ap_max_connection = u8_value;
    }
    if (nvs_get_u32(nvs, "probe_ms", &u32_value) == ESP_OK) {
        config->probe_interval_ms = u32_value;
    }
    if (nvs_get_u8(nvs, "v_wifi", &u8_value) == ESP_OK) {
        config->verbose_wifi_log = (u8_value != 0);
    }
    if (nvs_get_u8(nvs, "v_dns", &u8_value) == ESP_OK) {
        config->verbose_dns_log = (u8_value != 0);
    }
    if (nvs_get_u8(nvs, "v_probe", &u8_value) == ESP_OK) {
        config->verbose_probe_log = (u8_value != 0);
    }
    if (nvs_get_u8(nvs, "v_diag", &u8_value) == ESP_OK) {
        config->verbose_diag_log = (u8_value != 0);
    }

    config->ap_auth_open = (config->ap_password[0] == '\0');
    nvs_close(nvs);
    ESP_LOGI(TAG, "Loaded config from NVS");
    ESP_LOGI(TAG, "Config: STA_SSID=%s STA_PASSWORD=%s STA_MAC=%s AP_SSID=%s AP_PASSWORD=%s PROBE_URL=%s",
             config->sta_ssid[0] ? config->sta_ssid : "(empty)",
             config->sta_password[0] ? config->sta_password : "(empty)",
             config->sta_mac[0] ? config->sta_mac : "(factory)",
             config->ap_ssid[0] ? config->ap_ssid : "(empty)",
             config->ap_password[0] ? config->ap_password : "(empty)",
             config->probe_url);
    ESP_LOGI(TAG, "Verbose: WIFI=%s DNS=%s PROBE=%s DIAG=%s",
             config->verbose_wifi_log ? "on" : "off",
             config->verbose_dns_log ? "on" : "off",
             config->verbose_probe_log ? "on" : "off",
             config->verbose_diag_log ? "on" : "off");
    return ESP_OK;
}

esp_err_t router_storage_save(const router_config_t *config)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    ESP_ERROR_CHECK(nvs_set_str(nvs, "sta_ssid", config->sta_ssid));
    ESP_ERROR_CHECK(nvs_set_str(nvs, "sta_pwd", config->sta_password));
    ESP_ERROR_CHECK(nvs_set_str(nvs, "sta_mac", config->sta_mac));
    ESP_ERROR_CHECK(nvs_set_str(nvs, "ap_ssid", config->ap_ssid));
    ESP_ERROR_CHECK(nvs_set_str(nvs, "ap_pwd", config->ap_password));
    ESP_ERROR_CHECK(nvs_set_str(nvs, "probe_url", config->probe_url));
    ESP_ERROR_CHECK(nvs_set_u8(nvs, "ap_ch", config->ap_channel));
    ESP_ERROR_CHECK(nvs_set_u8(nvs, "ap_max", config->ap_max_connection));
    ESP_ERROR_CHECK(nvs_set_u32(nvs, "probe_ms", config->probe_interval_ms));
    ESP_ERROR_CHECK(nvs_set_u8(nvs, "v_wifi", config->verbose_wifi_log ? 1 : 0));
    ESP_ERROR_CHECK(nvs_set_u8(nvs, "v_dns", config->verbose_dns_log ? 1 : 0));
    ESP_ERROR_CHECK(nvs_set_u8(nvs, "v_probe", config->verbose_probe_log ? 1 : 0));
    ESP_ERROR_CHECK(nvs_set_u8(nvs, "v_diag", config->verbose_diag_log ? 1 : 0));
    ESP_ERROR_CHECK(nvs_commit(nvs));

    nvs_close(nvs);
    ESP_LOGI(TAG, "Saved config to NVS");
    ESP_LOGI(TAG, "Config: STA_SSID=%s STA_PASSWORD=%s STA_MAC=%s AP_SSID=%s AP_PASSWORD=%s PROBE_URL=%s",
             config->sta_ssid[0] ? config->sta_ssid : "(empty)",
             config->sta_password[0] ? config->sta_password : "(empty)",
             config->sta_mac[0] ? config->sta_mac : "(factory)",
             config->ap_ssid[0] ? config->ap_ssid : "(empty)",
             config->ap_password[0] ? config->ap_password : "(empty)",
             config->probe_url);
    ESP_LOGI(TAG, "Verbose: WIFI=%s DNS=%s PROBE=%s DIAG=%s",
             config->verbose_wifi_log ? "on" : "off",
             config->verbose_dns_log ? "on" : "off",
             config->verbose_probe_log ? "on" : "off",
             config->verbose_diag_log ? "on" : "off");
    return ESP_OK;
}
