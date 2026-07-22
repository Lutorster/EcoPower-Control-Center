#include "mqtt_manager.h"

#include "wifi_manager.h"

#include "mqtt_client.h"
#include "nvs.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstring>

static const char *TAG = "EcoPower_MQTT";

namespace {

constexpr const char *kNamespace = "ecopower_mqtt";
constexpr const char *kUriKey = "uri";
constexpr const char *kUserKey = "user";
constexpr const char *kPasswordKey = "pass";
constexpr const char *kTopicKey = "topic";

SemaphoreHandle_t g_mutex = nullptr;
esp_mqtt_client_handle_t g_client = nullptr;
EcoPowerMqttConfig g_config = {};
EcoPowerMqttState g_state = ECOPOWER_MQTT_DISABLED;
bool g_initialized = false;
TaskHandle_t g_network_task = nullptr;

bool load_string(
    nvs_handle_t handle,
    const char *key,
    char *buffer,
    size_t size)
{
    size_t required = size;
    const esp_err_t error =
        nvs_get_str(handle, key, buffer, &required);

    if (error != ESP_OK) {
        buffer[0] = '\0';
        return false;
    }

    return true;
}

bool load_config(EcoPowerMqttConfig *config)
{
    if (config == nullptr) {
        return false;
    }

    *config = {};

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    load_string(handle, kUriKey, config->uri, sizeof(config->uri));
    load_string(handle, kUserKey, config->username, sizeof(config->username));
    load_string(handle, kPasswordKey, config->password, sizeof(config->password));
    load_string(handle, kTopicKey, config->base_topic, sizeof(config->base_topic));

    nvs_close(handle);
    return config->uri[0] != '\0';
}

esp_err_t save_config_to_nvs(const EcoPowerMqttConfig *config)
{
    nvs_handle_t handle = 0;
    esp_err_t error =
        nvs_open(kNamespace, NVS_READWRITE, &handle);

    if (error != ESP_OK) {
        return error;
    }

    error = nvs_set_str(handle, kUriKey, config->uri);
    if (error == ESP_OK) {
        error = nvs_set_str(handle, kUserKey, config->username);
    }
    if (error == ESP_OK) {
        error = nvs_set_str(handle, kPasswordKey, config->password);
    }
    if (error == ESP_OK) {
        error = nvs_set_str(handle, kTopicKey, config->base_topic);
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }

    nvs_close(handle);
    return error;
}

void destroy_client()
{
    if (g_client != nullptr) {
        esp_mqtt_client_stop(g_client);
        esp_mqtt_client_destroy(g_client);
        g_client = nullptr;
    }
}

void mqtt_event_handler(
    void *,
    esp_event_base_t,
    int32_t event_id,
    void *)
{
    if (g_mutex == nullptr) {
        return;
    }

    EcoPowerMqttState new_state = g_state;

    switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
        case MQTT_EVENT_CONNECTED:
            new_state = ECOPOWER_MQTT_CONNECTED;
            ESP_LOGI(TAG, "Connected to MQTT broker");
            break;

        case MQTT_EVENT_DISCONNECTED:
            new_state = ECOPOWER_MQTT_DISCONNECTED;
            ESP_LOGW(TAG, "Disconnected from MQTT broker");
            break;

        case MQTT_EVENT_ERROR:
            new_state = ECOPOWER_MQTT_ERROR;
            ESP_LOGE(TAG, "MQTT transport error");
            break;

        default:
            return;
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        g_state = new_state;
        xSemaphoreGive(g_mutex);
    }
}

esp_err_t start_client()
{
    if (g_config.uri[0] == '\0') {
        g_state = ECOPOWER_MQTT_DISABLED;
        return ESP_ERR_INVALID_STATE;
    }

    if (ecopower_wifi_manager_get_state() != ECOPOWER_WIFI_CONNECTED) {
        g_state = ECOPOWER_MQTT_DISCONNECTED;
        ESP_LOGI(TAG, "Waiting for Wi-Fi before MQTT connect");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_client != nullptr) {
        return ESP_OK;
    }

    esp_mqtt_client_config_t config = {};
    config.broker.address.uri = g_config.uri;

    if (g_config.username[0] != '\0') {
        config.credentials.username = g_config.username;
    }

    if (g_config.password[0] != '\0') {
        config.credentials.authentication.password =
            g_config.password;
    }

    g_client = esp_mqtt_client_init(&config);
    if (g_client == nullptr) {
        g_state = ECOPOWER_MQTT_ERROR;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t error = esp_mqtt_client_register_event(
        g_client,
        static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
        mqtt_event_handler,
        nullptr);

    if (error != ESP_OK) {
        destroy_client();
        g_state = ECOPOWER_MQTT_ERROR;
        return error;
    }

    g_state = ECOPOWER_MQTT_CONNECTING;
    error = esp_mqtt_client_start(g_client);

    if (error != ESP_OK) {
        destroy_client();
        g_state = ECOPOWER_MQTT_ERROR;
        return error;
    }

    ESP_LOGI(TAG, "Connecting to %s", g_config.uri);
    return ESP_OK;
}

void network_task(void *)
{
    EcoPowerWifiState previous_wifi_state =
        ECOPOWER_WIFI_DISCONNECTED;

    while (true) {
        const EcoPowerWifiState wifi_state =
            ecopower_wifi_manager_get_state();

        if (wifi_state == ECOPOWER_WIFI_CONNECTED) {
            if (g_config.uri[0] != '\0' &&
                g_client == nullptr) {
                const esp_err_t error = start_client();
                if (error != ESP_OK &&
                    error != ESP_ERR_INVALID_STATE) {
                    ESP_LOGE(
                        TAG,
                        "MQTT start after Wi-Fi failed: %s",
                        esp_err_to_name(error));
                }
            }
        } else if (previous_wifi_state == ECOPOWER_WIFI_CONNECTED) {
            if (g_client != nullptr) {
                ESP_LOGW(
                    TAG,
                    "Wi-Fi lost; stopping MQTT client");
                destroy_client();

                if (xSemaphoreTake(
                        g_mutex,
                        portMAX_DELAY) == pdTRUE) {
                    g_state =
                        g_config.uri[0] != '\0'
                            ? ECOPOWER_MQTT_DISCONNECTED
                            : ECOPOWER_MQTT_DISABLED;
                    xSemaphoreGive(g_mutex);
                }
            }
        }

        previous_wifi_state = wifi_state;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

} // namespace

extern "C" esp_err_t ecopower_mqtt_manager_init(void)
{
    if (g_initialized) {
        return ESP_OK;
    }

    g_mutex = xSemaphoreCreateMutex();
    if (g_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    const bool configured = load_config(&g_config);
    g_state = configured
        ? ECOPOWER_MQTT_DISCONNECTED
        : ECOPOWER_MQTT_DISABLED;
    g_initialized = true;

    const BaseType_t task_created = xTaskCreate(
        network_task,
        "ecopower_mqtt_net",
        4096,
        nullptr,
        5,
        &g_network_task);

    if (task_created != pdPASS) {
        g_network_task = nullptr;
        g_state = ECOPOWER_MQTT_ERROR;
        return ESP_ERR_NO_MEM;
    }

    if (configured) {
        ESP_LOGI(
            TAG,
            "MQTT configured; waiting for Wi-Fi");
    } else {
        ESP_LOGI(TAG, "MQTT is not configured");
    }

    return ESP_OK;
}

extern "C" esp_err_t ecopower_mqtt_manager_save_config(
    const EcoPowerMqttConfig *config)
{
    if (!g_initialized ||
        config == nullptr ||
        config->uri[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t error = save_config_to_nvs(config);
    if (error != ESP_OK) {
        return error;
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        g_config = *config;
        g_state = ECOPOWER_MQTT_DISCONNECTED;
        xSemaphoreGive(g_mutex);
    }

    ESP_LOGI(TAG, "MQTT configuration saved");
    return ESP_OK;
}

extern "C" bool ecopower_mqtt_manager_get_config(
    EcoPowerMqttConfig *config)
{
    if (!g_initialized || config == nullptr) {
        return false;
    }

    bool available = false;

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        *config = g_config;
        available = g_config.uri[0] != '\0';
        xSemaphoreGive(g_mutex);
    }

    return available;
}

extern "C" esp_err_t ecopower_mqtt_manager_connect(void)
{
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_config.uri[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    if (ecopower_wifi_manager_get_state() !=
        ECOPOWER_WIFI_CONNECTED) {
        if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
            g_state = ECOPOWER_MQTT_DISCONNECTED;
            xSemaphoreGive(g_mutex);
        }

        ESP_LOGI(
            TAG,
            "Connect requested; waiting for Wi-Fi");
        return ESP_OK;
    }

    return start_client();
}

extern "C" esp_err_t ecopower_mqtt_manager_disconnect(void)
{
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    destroy_client();

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        g_state = g_config.uri[0] != '\0'
            ? ECOPOWER_MQTT_DISCONNECTED
            : ECOPOWER_MQTT_DISABLED;
        xSemaphoreGive(g_mutex);
    }

    return ESP_OK;
}

extern "C" esp_err_t ecopower_mqtt_manager_forget_config(void)
{
    ecopower_mqtt_manager_disconnect();

    nvs_handle_t handle = 0;
    esp_err_t error =
        nvs_open(kNamespace, NVS_READWRITE, &handle);

    if (error == ESP_OK) {
        error = nvs_erase_all(handle);
        if (error == ESP_OK) {
            error = nvs_commit(handle);
        }
        nvs_close(handle);
    }

    if (error == ESP_OK &&
        xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        g_config = {};
        g_state = ECOPOWER_MQTT_DISABLED;
        xSemaphoreGive(g_mutex);
    }

    return error;
}

extern "C" EcoPowerMqttState
ecopower_mqtt_manager_get_state(void)
{
    if (!g_initialized || g_mutex == nullptr) {
        return ECOPOWER_MQTT_DISABLED;
    }

    EcoPowerMqttState state = ECOPOWER_MQTT_DISABLED;

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        state = g_state;
        xSemaphoreGive(g_mutex);
    }

    return state;
}

extern "C" bool ecopower_mqtt_manager_is_connected(void)
{
    return ecopower_mqtt_manager_get_state() ==
           ECOPOWER_MQTT_CONNECTED;
}

extern "C" int ecopower_mqtt_manager_publish_topic(
    const char *topic,
    const char *payload,
    int qos,
    bool retain)
{
    if (!ecopower_mqtt_manager_is_connected() ||
        g_client == nullptr ||
        topic == nullptr ||
        payload == nullptr) {
        return -1;
    }

    return esp_mqtt_client_publish(
        g_client,
        topic,
        payload,
        0,
        qos,
        retain);
}

extern "C" int ecopower_mqtt_manager_publish(
    const char *topic_suffix,
    const char *payload,
    int qos,
    bool retain)
{
    if (topic_suffix == nullptr || payload == nullptr) {
        return -1;
    }

    char topic[160] = {};

    if (g_config.base_topic[0] != '\0') {
        snprintf(
            topic,
            sizeof(topic),
            "%s/%s",
            g_config.base_topic,
            topic_suffix);
    } else {
        snprintf(topic, sizeof(topic), "%s", topic_suffix);
    }

    return ecopower_mqtt_manager_publish_topic(
        topic,
        payload,
        qos,
        retain);
}
