#include "wifi_manager.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

static const char *TAG = "EcoPower_WiFi";

namespace {

constexpr size_t kMaxScanResults = 12;
constexpr int kMaximumReconnectAttempts = 5;

constexpr const char *kNvsNamespace = "ecopower_wifi";
constexpr const char *kNvsSsidKey = "ssid";
constexpr const char *kNvsPasswordKey = "password";

SemaphoreHandle_t g_mutex = nullptr;

bool g_initialized = false;
bool g_scanning = false;
bool g_scan_ready = false;
bool g_manual_disconnect = false;
bool g_saved_credentials_available = false;
bool g_pending_credentials_save = false;

EcoPowerWifiNetwork g_networks[kMaxScanResults] = {};
size_t g_network_count = 0;

EcoPowerWifiState g_state = ECOPOWER_WIFI_DISCONNECTED;

char g_connected_ssid[33] = {};
char g_pending_password[65] = {};
char g_ip_address[16] = {};

int8_t g_rssi = 0;
int g_reconnect_attempts = 0;

esp_event_handler_instance_t g_wifi_event_instance = nullptr;
esp_event_handler_instance_t g_ip_event_instance = nullptr;

void clear_results_locked()
{
    std::memset(g_networks, 0, sizeof(g_networks));
    g_network_count = 0;
    g_scan_ready = false;
}

void clear_connection_data_locked()
{
    std::memset(g_ip_address, 0, sizeof(g_ip_address));
    g_rssi = 0;
}

esp_err_t save_credentials(
    const char *ssid,
    const char *password)
{
    if (ssid == nullptr || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(
        kNvsNamespace,
        NVS_READWRITE,
        &handle);

    if (error != ESP_OK) {
        return error;
    }

    error = nvs_set_str(handle, kNvsSsidKey, ssid);

    if (error == ESP_OK) {
        error = nvs_set_str(
            handle,
            kNvsPasswordKey,
            password != nullptr ? password : "");
    }

    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }

    nvs_close(handle);

    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi credentials saved for SSID=%s", ssid);
    } else {
        ESP_LOGE(
            TAG,
            "Unable to save Wi-Fi credentials: %s",
            esp_err_to_name(error));
    }

    return error;
}

bool load_credentials(
    char *ssid,
    size_t ssid_size,
    char *password,
    size_t password_size)
{
    if (ssid == nullptr ||
        ssid_size == 0U ||
        password == nullptr ||
        password_size == 0U) {
        return false;
    }

    ssid[0] = '\0';
    password[0] = '\0';

    nvs_handle_t handle = 0;
    const esp_err_t open_error = nvs_open(
        kNvsNamespace,
        NVS_READONLY,
        &handle);

    if (open_error != ESP_OK) {
        return false;
    }

    size_t stored_ssid_size = ssid_size;
    esp_err_t error = nvs_get_str(
        handle,
        kNvsSsidKey,
        ssid,
        &stored_ssid_size);

    if (error == ESP_OK) {
        size_t stored_password_size = password_size;
        error = nvs_get_str(
            handle,
            kNvsPasswordKey,
            password,
            &stored_password_size);
    }

    nvs_close(handle);

    if (error != ESP_OK || ssid[0] == '\0') {
        ssid[0] = '\0';
        password[0] = '\0';
        return false;
    }

    return true;
}

esp_err_t erase_credentials()
{
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(
        kNvsNamespace,
        NVS_READWRITE,
        &handle);

    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }

    if (error != ESP_OK) {
        return error;
    }

    error = nvs_erase_all(handle);

    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }

    nvs_close(handle);
    return error;
}

esp_err_t start_connection(
    const char *ssid,
    const char *password,
    bool save_after_success)
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

    snprintf(
        reinterpret_cast<char *>(config.sta.password),
        sizeof(config.sta.password),
        "%s",
        password != nullptr ? password : "");

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

    error = esp_wifi_set_config(WIFI_IF_STA, &config);

    if (error != ESP_OK) {
        return error;
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        snprintf(
            g_connected_ssid,
            sizeof(g_connected_ssid),
            "%s",
            ssid);

        snprintf(
            g_pending_password,
            sizeof(g_pending_password),
            "%s",
            password != nullptr ? password : "");

        clear_connection_data_locked();

        g_reconnect_attempts = 0;
        g_manual_disconnect = false;
        g_pending_credentials_save = save_after_success;
        g_state = ECOPOWER_WIFI_CONNECTING;

        xSemaphoreGive(g_mutex);
    }

    error = esp_wifi_connect();

    if (error != ESP_OK) {
        if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
            g_state = ECOPOWER_WIFI_FAILED;
            g_pending_credentials_save = false;
            xSemaphoreGive(g_mutex);
        }

        return error;
    }

    ESP_LOGI(
        TAG,
        "%s to SSID=%s",
        save_after_success ? "Connecting" : "Auto-connecting",
        ssid);

    return ESP_OK;
}

void wifi_event_handler(
    void *,
    esp_event_base_t event_base,
    int32_t event_id,
    void *)
{
    if (event_base != WIFI_EVENT ||
        event_id != WIFI_EVENT_STA_DISCONNECTED ||
        g_mutex == nullptr) {
        return;
    }

    bool retry = false;
    int retry_number = 0;

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        clear_connection_data_locked();

        retry =
            !g_manual_disconnect &&
            g_state == ECOPOWER_WIFI_CONNECTING &&
            g_reconnect_attempts < kMaximumReconnectAttempts;

        if (retry) {
            ++g_reconnect_attempts;
            retry_number = g_reconnect_attempts;
        } else {
            g_state = g_manual_disconnect
                ? ECOPOWER_WIFI_DISCONNECTED
                : ECOPOWER_WIFI_FAILED;

            g_pending_credentials_save = false;
            g_manual_disconnect = false;
        }

        xSemaphoreGive(g_mutex);
    }

    if (retry) {
        ESP_LOGW(
            TAG,
            "Connection failed, retry %d/%d",
            retry_number,
            kMaximumReconnectAttempts);

        const esp_err_t retry_error = esp_wifi_connect();

        if (retry_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Retry failed to start: %s",
                esp_err_to_name(retry_error));
        }
    } else {
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
        event_data == nullptr ||
        g_mutex == nullptr) {
        return;
    }

    auto *event =
        static_cast<ip_event_got_ip_t *>(event_data);

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

    char ssid_to_save[33] = {};
    char password_to_save[65] = {};
    bool save_now = false;

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        snprintf(
            g_ip_address,
            sizeof(g_ip_address),
            "%s",
            ip_buffer);

        g_rssi = rssi;
        g_reconnect_attempts = 0;
        g_manual_disconnect = false;
        g_state = ECOPOWER_WIFI_CONNECTED;

        save_now = g_pending_credentials_save;

        if (save_now) {
            snprintf(
                ssid_to_save,
                sizeof(ssid_to_save),
                "%s",
                g_connected_ssid);

            snprintf(
                password_to_save,
                sizeof(password_to_save),
                "%s",
                g_pending_password);
        }

        g_pending_credentials_save = false;
        std::memset(
            g_pending_password,
            0,
            sizeof(g_pending_password));

        xSemaphoreGive(g_mutex);
    }

    if (save_now) {
        const esp_err_t save_error =
            save_credentials(
                ssid_to_save,
                password_to_save);

        if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
            g_saved_credentials_available =
                save_error == ESP_OK;
            xSemaphoreGive(g_mutex);
        }

        std::memset(
            password_to_save,
            0,
            sizeof(password_to_save));
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

    g_mutex = xSemaphoreCreateMutex();

    if (g_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
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

    g_initialized = true;

    char saved_ssid[33] = {};
    char saved_password[65] = {};

    const bool credentials_loaded =
        load_credentials(
            saved_ssid,
            sizeof(saved_ssid),
            saved_password,
            sizeof(saved_password));

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        g_saved_credentials_available =
            credentials_loaded;
        xSemaphoreGive(g_mutex);
    }

    ESP_LOGI(
        TAG,
        "Wi-Fi manager initialized in station mode");

    if (credentials_loaded) {
        const esp_err_t connect_error =
            start_connection(
                saved_ssid,
                saved_password,
                false);

        std::memset(
            saved_password,
            0,
            sizeof(saved_password));

        if (connect_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Automatic Wi-Fi connection failed to start: %s",
                esp_err_to_name(connect_error));
        }
    } else {
        ESP_LOGI(TAG, "No saved Wi-Fi credentials");
    }

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
    return start_connection(
        ssid,
        password,
        true);
}

extern "C" esp_err_t ecopower_wifi_manager_disconnect(void)
{
    if (!g_initialized || g_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        g_manual_disconnect = true;
        g_reconnect_attempts = 0;
        g_pending_credentials_save = false;
        clear_connection_data_locked();
        g_state = ECOPOWER_WIFI_DISCONNECTED;
        xSemaphoreGive(g_mutex);
    }

    const esp_err_t error = esp_wifi_disconnect();

    if (error == ESP_ERR_WIFI_NOT_CONNECT) {
        return ESP_OK;
    }

    return error;
}

extern "C" esp_err_t ecopower_wifi_manager_forget_network(void)
{
    if (!g_initialized || g_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t disconnect_error =
        ecopower_wifi_manager_disconnect();

    if (disconnect_error != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Disconnect while forgetting network returned: %s",
            esp_err_to_name(disconnect_error));
    }

    const esp_err_t erase_error =
        erase_credentials();

    if (erase_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Unable to erase Wi-Fi credentials: %s",
            esp_err_to_name(erase_error));
        return erase_error;
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        g_saved_credentials_available = false;
        g_connected_ssid[0] = '\0';
        g_pending_password[0] = '\0';
        g_pending_credentials_save = false;
        g_state = ECOPOWER_WIFI_DISCONNECTED;
        xSemaphoreGive(g_mutex);
    }

    ESP_LOGI(TAG, "Saved Wi-Fi network forgotten");
    return ESP_OK;
}

extern "C" bool ecopower_wifi_manager_has_saved_credentials(void)
{
    if (g_mutex == nullptr) {
        return false;
    }

    bool available = false;

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        available = g_saved_credentials_available;
        xSemaphoreGive(g_mutex);
    }

    return available;
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

    EcoPowerWifiState state =
        ECOPOWER_WIFI_DISCONNECTED;

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
        copied = std::min(
            g_network_count,
            max_networks);

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
