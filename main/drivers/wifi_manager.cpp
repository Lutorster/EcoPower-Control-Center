#include "wifi_manager.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

static const char *TAG = "EcoPower_WiFi";

namespace {

constexpr size_t kMaxScanResults = 12;
constexpr int kMaximumReconnectAttempts = 5;

SemaphoreHandle_t g_mutex = nullptr;
bool g_initialized = false;
bool g_scanning = false;
bool g_scan_ready = false;

EcoPowerWifiNetwork g_networks[kMaxScanResults] = {};
size_t g_network_count = 0;

EcoPowerWifiState g_state = ECOPOWER_WIFI_DISCONNECTED;
char g_connected_ssid[33] = {};
char g_ip_address[16] = {};
int8_t g_rssi = 0;
int g_reconnect_attempts = 0;
bool g_manual_disconnect = false;

esp_event_handler_instance_t g_wifi_event_instance = nullptr;
esp_event_handler_instance_t g_ip_event_instance = nullptr;

void clear_results_locked()
{
    std::memset(g_networks, 0, sizeof(g_networks));
    g_network_count = 0;
    g_scan_ready = false;
}

void set_state_locked(EcoPowerWifiState state)
{
    g_state = state;
}

void wifi_event_handler(
    void *,
    esp_event_base_t event_base,
    int32_t event_id,
    void *)
{
    if (event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
            std::memset(g_ip_address, 0, sizeof(g_ip_address));
            g_rssi = 0;

            const bool should_retry =
                !g_manual_disconnect &&
                g_state == ECOPOWER_WIFI_CONNECTING &&
                g_reconnect_attempts < kMaximumReconnectAttempts;

            if (should_retry) {
                ++g_reconnect_attempts;
                xSemaphoreGive(g_mutex);

                ESP_LOGW(
                    TAG,
                    "Connection lost, retry %d/%d",
                    g_reconnect_attempts,
                    kMaximumReconnectAttempts);

                esp_wifi_connect();
                return;
            }

            set_state_locked(
                g_manual_disconnect
                    ? ECOPOWER_WIFI_DISCONNECTED
                    : ECOPOWER_WIFI_FAILED);

            g_manual_disconnect = false;
            xSemaphoreGive(g_mutex);
        }

        ESP_LOGW(TAG, "Wi-Fi disconnected");
    }
}

void ip_event_handler(
    void *,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    if (event_base != IP_EVENT ||
        event_id != IP_EVENT_STA_GOT_IP ||
        event_data == nullptr) {
        return;
    }

    auto *event = static_cast<ip_event_got_ip_t *>(event_data);

    char ip_buffer[16] = {};
    snprintf(
        ip_buffer,
        sizeof(ip_buffer),
        IPSTR,
        IP2STR(&event->ip_info.ip));

    wifi_ap_record_t ap_info = {};
    int8_t rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        snprintf(
            g_ip_address,
            sizeof(g_ip_address),
            "%s",
            ip_buffer);

        g_rssi = rssi;
        g_reconnect_attempts = 0;
        g_manual_disconnect = false;
        set_state_locked(ECOPOWER_WIFI_CONNECTED);

        xSemaphoreGive(g_mutex);
    }

    ESP_LOGI(
        TAG,
        "Connected to %s, IP=%s, RSSI=%d dBm",
        g_connected_ssid,
        ip_buffer,
        static_cast<int>(rssi));
}

void scan_task(void *)
{
    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;

    ESP_LOGI(TAG, "Starting Wi-Fi scan");

    const esp_err_t scan_error =
        esp_wifi_scan_start(&scan_config, true);

    EcoPowerWifiNetwork local_results[kMaxScanResults] = {};
    size_t local_count = 0;

    if (scan_error == ESP_OK) {
        uint16_t ap_count = 0;

        if (esp_wifi_scan_get_ap_num(&ap_count) == ESP_OK &&
            ap_count > 0) {
            const uint16_t requested =
                static_cast<uint16_t>(
                    std::min<size_t>(
                        ap_count,
                        kMaxScanResults));

            wifi_ap_record_t records[kMaxScanResults] = {};
            uint16_t received = requested;

            if (esp_wifi_scan_get_ap_records(
                    &received,
                    records) == ESP_OK) {
                for (uint16_t i = 0; i < received; ++i) {
                    if (records[i].ssid[0] == '\0') {
                        continue;
                    }

                    EcoPowerWifiNetwork &network =
                        local_results[local_count];

                    std::strncpy(
                        network.ssid,
                        reinterpret_cast<const char *>(
                            records[i].ssid),
                        sizeof(network.ssid) - 1U);

                    network.ssid[
                        sizeof(network.ssid) - 1U] = '\0';

                    network.rssi = records[i].rssi;
                    network.secured =
                        records[i].authmode != WIFI_AUTH_OPEN;

                    ++local_count;

                    if (local_count >= kMaxScanResults) {
                        break;
                    }
                }
            }
        }

        std::sort(
            local_results,
            local_results + local_count,
            [](const EcoPowerWifiNetwork &a,
               const EcoPowerWifiNetwork &b) {
                return a.rssi > b.rssi;
            });

        ESP_LOGI(
            TAG,
            "Wi-Fi scan complete: %u network(s)",
            static_cast<unsigned>(local_count));
    } else {
        ESP_LOGE(
            TAG,
            "Wi-Fi scan failed: %s",
            esp_err_to_name(scan_error));
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        clear_results_locked();

        for (size_t i = 0; i < local_count; ++i) {
            g_networks[i] = local_results[i];
        }

        g_network_count = local_count;
        g_scan_ready = true;
        g_scanning = false;

        xSemaphoreGive(g_mutex);
    }

    vTaskDelete(nullptr);
}

} // namespace

extern "C" esp_err_t ecopower_wifi_manager_init(void)
{
    if (g_initialized) {
        return ESP_OK;
    }

    esp_err_t error = nvs_flash_init();

    if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
        error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }

    if (error != ESP_OK) {
        return error;
    }

    error = esp_netif_init();
    if (error != ESP_OK &&
        error != ESP_ERR_INVALID_STATE) {
        return error;
    }

    error = esp_event_loop_create_default();
    if (error != ESP_OK &&
        error != ESP_ERR_INVALID_STATE) {
        return error;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config =
        WIFI_INIT_CONFIG_DEFAULT();

    error = esp_wifi_init(&init_config);
    if (error != ESP_OK) {
        return error;
    }

    error = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        nullptr,
        &g_wifi_event_instance);

    if (error != ESP_OK) {
        return error;
    }

    error = esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &ip_event_handler,
        nullptr,
        &g_ip_event_instance);

    if (error != ESP_OK) {
        return error;
    }

    error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (error != ESP_OK) {
        return error;
    }

    error = esp_wifi_set_mode(WIFI_MODE_STA);
    if (error != ESP_OK) {
        return error;
    }

    error = esp_wifi_start();
    if (error != ESP_OK) {
        return error;
    }

    g_mutex = xSemaphoreCreateMutex();
    if (g_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    g_initialized = true;

    ESP_LOGI(
        TAG,
        "Wi-Fi manager initialized in station mode");

    return ESP_OK;
}

extern "C" esp_err_t ecopower_wifi_manager_scan_async(void)
{
    if (!g_initialized || g_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    if (g_scanning) {
        xSemaphoreGive(g_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    clear_results_locked();
    g_scanning = true;

    xSemaphoreGive(g_mutex);

    const BaseType_t created = xTaskCreate(
        scan_task,
        "ecopower_wifi_scan",
        4096,
        nullptr,
        4,
        nullptr);

    if (created != pdPASS) {
        if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
            g_scanning = false;
            g_scan_ready = true;
            xSemaphoreGive(g_mutex);
        }

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

extern "C" esp_err_t ecopower_wifi_manager_connect(
    const char *ssid,
    const char *password)
{
    if (!g_initialized ||
        g_mutex == nullptr ||
        ssid == nullptr ||
        ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t config = {};

    snprintf(
        reinterpret_cast<char *>(config.sta.ssid),
        sizeof(config.sta.ssid),
        "%s",
        ssid);

    if (password != nullptr) {
        snprintf(
            reinterpret_cast<char *>(config.sta.password),
            sizeof(config.sta.password),
            "%s",
            password);
    }

    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    esp_err_t error = esp_wifi_disconnect();
    if (error != ESP_OK &&
        error != ESP_ERR_WIFI_NOT_CONNECT) {
        ESP_LOGW(
            TAG,
            "Disconnect before connect returned: %s",
            esp_err_to_name(error));
    }

    error = esp_wifi_set_config(
        WIFI_IF_STA,
        &config);

    if (error != ESP_OK) {
        return error;
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        snprintf(
            g_connected_ssid,
            sizeof(g_connected_ssid),
            "%s",
            ssid);

        std::memset(g_ip_address, 0, sizeof(g_ip_address));
        g_rssi = 0;
        g_reconnect_attempts = 0;
        g_manual_disconnect = false;
        set_state_locked(ECOPOWER_WIFI_CONNECTING);

        xSemaphoreGive(g_mutex);
    }

    error = esp_wifi_connect();

    if (error != ESP_OK) {
        if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
            set_state_locked(ECOPOWER_WIFI_FAILED);
            xSemaphoreGive(g_mutex);
        }

        return error;
    }

    ESP_LOGI(TAG, "Connecting to SSID=%s", ssid);
    return ESP_OK;
}

extern "C" esp_err_t ecopower_wifi_manager_disconnect(void)
{
    if (!g_initialized || g_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        g_manual_disconnect = true;
        g_reconnect_attempts = 0;
        std::memset(g_ip_address, 0, sizeof(g_ip_address));
        g_rssi = 0;
        set_state_locked(ECOPOWER_WIFI_DISCONNECTED);
        xSemaphoreGive(g_mutex);
    }

    const esp_err_t error = esp_wifi_disconnect();

    if (error == ESP_ERR_WIFI_NOT_CONNECT) {
        return ESP_OK;
    }

    return error;
}

extern "C" bool ecopower_wifi_manager_is_initialized(void)
{
    return g_initialized;
}

extern "C" bool ecopower_wifi_manager_is_scanning(void)
{
    if (g_mutex == nullptr) {
        return false;
    }

    bool scanning = false;

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        scanning = g_scanning;
        xSemaphoreGive(g_mutex);
    }

    return scanning;
}

extern "C" bool ecopower_wifi_manager_scan_ready(void)
{
    if (g_mutex == nullptr) {
        return false;
    }

    bool ready = false;

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        ready = g_scan_ready;
        xSemaphoreGive(g_mutex);
    }

    return ready;
}

extern "C" EcoPowerWifiState ecopower_wifi_manager_get_state(void)
{
    if (g_mutex == nullptr) {
        return ECOPOWER_WIFI_DISCONNECTED;
    }

    EcoPowerWifiState state = ECOPOWER_WIFI_DISCONNECTED;

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        state = g_state;
        xSemaphoreGive(g_mutex);
    }

    return state;
}

extern "C" size_t ecopower_wifi_manager_get_scan_results(
    EcoPowerWifiNetwork *networks,
    size_t max_networks)
{
    if (g_mutex == nullptr ||
        networks == nullptr ||
        max_networks == 0U) {
        return 0U;
    }

    size_t copied = 0U;

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        copied = std::min(g_network_count, max_networks);

        for (size_t i = 0; i < copied; ++i) {
            networks[i] = g_networks[i];
        }

        xSemaphoreGive(g_mutex);
    }

    return copied;
}

extern "C" bool ecopower_wifi_manager_get_connected_ssid(
    char *buffer,
    size_t buffer_size)
{
    if (g_mutex == nullptr ||
        buffer == nullptr ||
        buffer_size == 0U) {
        return false;
    }

    bool available = false;

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        available = g_connected_ssid[0] != '\0';

        snprintf(
            buffer,
            buffer_size,
            "%s",
            g_connected_ssid);

        xSemaphoreGive(g_mutex);
    }

    return available;
}

extern "C" bool ecopower_wifi_manager_get_ip_address(
    char *buffer,
    size_t buffer_size)
{
    if (g_mutex == nullptr ||
        buffer == nullptr ||
        buffer_size == 0U) {
        return false;
    }

    bool available = false;

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        available = g_ip_address[0] != '\0';

        snprintf(
            buffer,
            buffer_size,
            "%s",
            g_ip_address);

        xSemaphoreGive(g_mutex);
    }

    return available;
}

extern "C" int8_t ecopower_wifi_manager_get_rssi(void)
{
    if (g_mutex == nullptr) {
        return 0;
    }

    int8_t rssi = 0;

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        rssi = g_rssi;
        xSemaphoreGive(g_mutex);
    }

    return rssi;
}
