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

SemaphoreHandle_t g_mutex = nullptr;
bool g_initialized = false;
bool g_scanning = false;
bool g_scan_ready = false;
EcoPowerWifiNetwork g_networks[kMaxScanResults] = {};
size_t g_network_count = 0;

void clear_results_locked()
{
    std::memset(g_networks, 0, sizeof(g_networks));
    g_network_count = 0;
    g_scan_ready = false;
}

void scan_task(void *)
{
    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;

    ESP_LOGI(TAG, "Starting Wi-Fi scan");

    const esp_err_t scan_error = esp_wifi_scan_start(&scan_config, true);

    EcoPowerWifiNetwork local_results[kMaxScanResults] = {};
    size_t local_count = 0;

    if (scan_error == ESP_OK) {
        uint16_t ap_count = 0;
        if (esp_wifi_scan_get_ap_num(&ap_count) == ESP_OK && ap_count > 0) {
            const uint16_t requested =
                static_cast<uint16_t>(std::min<size_t>(
                    ap_count, kMaxScanResults));

            wifi_ap_record_t records[kMaxScanResults] = {};
            uint16_t received = requested;

            if (esp_wifi_scan_get_ap_records(&received, records) == ESP_OK) {
                for (uint16_t i = 0; i < received; ++i) {
                    if (records[i].ssid[0] == '\0') {
                        continue;
                    }

                    EcoPowerWifiNetwork &network = local_results[local_count];
                    std::strncpy(
                        network.ssid,
                        reinterpret_cast<const char *>(records[i].ssid),
                        sizeof(network.ssid) - 1U);
                    network.ssid[sizeof(network.ssid) - 1U] = '\0';
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

        ESP_LOGI(TAG, "Wi-Fi scan complete: %u network(s)",
                 static_cast<unsigned>(local_count));
    } else {
        ESP_LOGE(TAG, "Wi-Fi scan failed: %s",
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
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        return error;
    }

    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        return error;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    error = esp_wifi_init(&init_config);
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
    ESP_LOGI(TAG, "Wi-Fi manager initialized in station mode");
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

extern "C" size_t ecopower_wifi_manager_get_scan_results(
    EcoPowerWifiNetwork *networks,
    size_t max_networks)
{
    if (g_mutex == nullptr || networks == nullptr || max_networks == 0U) {
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
