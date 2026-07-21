#include "ha_discovery.h"

#include "mqtt_manager.h"
#include "wifi_manager.h"
#include "rs485_port.h"
#include "app/app_version.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstring>
#include <inttypes.h>

static const char *TAG = "EcoPower_HA";

namespace {

constexpr const char *kDeviceId = "ecopower_control_center";
constexpr uint32_t kStatePublishPeriodMs = 10000U;
constexpr uint32_t kRetryPeriodMs = 10000U;

SemaphoreHandle_t g_mutex = nullptr;
TaskHandle_t g_task = nullptr;
EcoPowerHaState g_state = ECOPOWER_HA_DISABLED;
bool g_initialized = false;

void set_state(EcoPowerHaState state)
{
    if (g_mutex == nullptr) {
        g_state = state;
        return;
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        g_state = state;
        xSemaphoreGive(g_mutex);
    }
}

bool get_base_topic(char *buffer, size_t size)
{
    if (buffer == nullptr || size == 0U) {
        return false;
    }

    EcoPowerMqttConfig config = {};
    if (ecopower_mqtt_manager_get_config(&config) &&
        config.base_topic[0] != '\0') {
        snprintf(buffer, size, "%s", config.base_topic);
    } else {
        snprintf(buffer, size, "ecopower");
    }

    return true;
}

bool publish_absolute(
    const char *topic,
    const char *payload,
    bool retain = true)
{
    const int message_id =
        ecopower_mqtt_manager_publish_topic(
            topic,
            payload,
            1,
            retain);

    if (message_id < 0) {
        ESP_LOGE(TAG, "Publish failed: %s", topic);
        return false;
    }

    return true;
}

bool publish_discovery_config(
    const char *domain,
    const char *object_id,
    const char *payload)
{
    char topic[192] = {};
    snprintf(
        topic,
        sizeof(topic),
        "homeassistant/%s/%s/%s/config",
        domain,
        kDeviceId,
        object_id);

    return publish_absolute(topic, payload, true);
}

void append_device_json(
    char *buffer,
    size_t size)
{
    snprintf(
        buffer,
        size,
        "\"device\":{"
        "\"identifiers\":[\"%s\"],"
        "\"name\":\"EcoPower Control Center\","
        "\"manufacturer\":\"EcoPower\","
        "\"model\":\"ESP32-S3 7-inch HMI\","
        "\"sw_version\":\"%s\""
        "}",
        kDeviceId,
        ECOPOWER_APP_VERSION);
}

bool publish_discovery()
{
    if (!ecopower_mqtt_manager_is_connected()) {
        return false;
    }

    set_state(ECOPOWER_HA_PUBLISHING);

    char base[64] = {};
    get_base_topic(base, sizeof(base));

    char availability_topic[128] = {};
    snprintf(
        availability_topic,
        sizeof(availability_topic),
        "%s/availability",
        base);

    char device_json[320] = {};
    append_device_json(device_json, sizeof(device_json));

    char payload[1024] = {};
    bool ok = true;

    snprintf(
        payload,
        sizeof(payload),
        "{"
        "\"name\":\"Wi-Fi Signal\","
        "\"unique_id\":\"%s_wifi_rssi\","
        "\"state_topic\":\"%s/ha/wifi_rssi\","
        "\"availability_topic\":\"%s\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"device_class\":\"signal_strength\","
        "\"state_class\":\"measurement\","
        "\"unit_of_measurement\":\"dBm\","
        "\"entity_category\":\"diagnostic\","
        "%s"
        "}",
        kDeviceId,
        base,
        availability_topic,
        device_json);
    ok = publish_discovery_config(
             "sensor", "wifi_rssi", payload) && ok;

    snprintf(
        payload,
        sizeof(payload),
        "{"
        "\"name\":\"MQTT Connection\","
        "\"unique_id\":\"%s_mqtt_connected\","
        "\"state_topic\":\"%s/ha/mqtt_connected\","
        "\"availability_topic\":\"%s\","
        "\"payload_on\":\"ON\","
        "\"payload_off\":\"OFF\","
        "\"device_class\":\"connectivity\","
        "\"entity_category\":\"diagnostic\","
        "%s"
        "}",
        kDeviceId,
        base,
        availability_topic,
        device_json);
    ok = publish_discovery_config(
             "binary_sensor", "mqtt_connected", payload) && ok;

    snprintf(
        payload,
        sizeof(payload),
        "{"
        "\"name\":\"RS485 Transport\","
        "\"unique_id\":\"%s_rs485_ready\","
        "\"state_topic\":\"%s/ha/rs485_ready\","
        "\"availability_topic\":\"%s\","
        "\"payload_on\":\"ON\","
        "\"payload_off\":\"OFF\","
        "\"device_class\":\"connectivity\","
        "\"entity_category\":\"diagnostic\","
        "%s"
        "}",
        kDeviceId,
        base,
        availability_topic,
        device_json);
    ok = publish_discovery_config(
             "binary_sensor", "rs485_ready", payload) && ok;

    snprintf(
        payload,
        sizeof(payload),
        "{"
        "\"name\":\"SD Card\","
        "\"unique_id\":\"%s_sd_ready\","
        "\"state_topic\":\"%s/ha/sd_ready\","
        "\"availability_topic\":\"%s\","
        "\"payload_on\":\"ON\","
        "\"payload_off\":\"OFF\","
        "\"device_class\":\"connectivity\","
        "\"entity_category\":\"diagnostic\","
        "%s"
        "}",
        kDeviceId,
        base,
        availability_topic,
        device_json);
    ok = publish_discovery_config(
             "binary_sensor", "sd_ready", payload) && ok;

    snprintf(
        payload,
        sizeof(payload),
        "{"
        "\"name\":\"Uptime\","
        "\"unique_id\":\"%s_uptime\","
        "\"state_topic\":\"%s/ha/uptime\","
        "\"availability_topic\":\"%s\","
        "\"device_class\":\"duration\","
        "\"unit_of_measurement\":\"s\","
        "\"entity_category\":\"diagnostic\","
        "%s"
        "}",
        kDeviceId,
        base,
        availability_topic,
        device_json);
    ok = publish_discovery_config(
             "sensor", "uptime", payload) && ok;

    snprintf(
        payload,
        sizeof(payload),
        "{"
        "\"name\":\"Firmware Version\","
        "\"unique_id\":\"%s_firmware\","
        "\"state_topic\":\"%s/ha/firmware\","
        "\"availability_topic\":\"%s\","
        "\"entity_category\":\"diagnostic\","
        "%s"
        "}",
        kDeviceId,
        base,
        availability_topic,
        device_json);
    ok = publish_discovery_config(
             "sensor", "firmware", payload) && ok;

    if (!publish_absolute(
            availability_topic,
            "online",
            true)) {
        ok = false;
    }

    if (ok) {
        set_state(ECOPOWER_HA_READY);
        ESP_LOGI(
            TAG,
            "Home Assistant discovery published");
    } else {
        set_state(ECOPOWER_HA_ERROR);
        ESP_LOGE(
            TAG,
            "Home Assistant discovery failed");
    }

    return ok;
}

bool publish_states()
{
    if (!ecopower_mqtt_manager_is_connected()) {
        return false;
    }

    char base[64] = {};
    get_base_topic(base, sizeof(base));

    char topic[128] = {};
    char payload[64] = {};
    bool ok = true;

    snprintf(topic, sizeof(topic), "%s/availability", base);
    ok = publish_absolute(topic, "online", true) && ok;

    snprintf(topic, sizeof(topic), "%s/ha/wifi_rssi", base);
    snprintf(
        payload,
        sizeof(payload),
        "%d",
        static_cast<int>(ecopower_wifi_manager_get_rssi()));
    ok = publish_absolute(topic, payload, true) && ok;

    snprintf(topic, sizeof(topic), "%s/ha/mqtt_connected", base);
    ok = publish_absolute(topic, "ON", true) && ok;

    snprintf(topic, sizeof(topic), "%s/ha/rs485_ready", base);
    ok = publish_absolute(
             topic,
             ecopower_rs485_is_initialized() ? "ON" : "OFF",
             true) && ok;

    bool sd_ready = false;
    FILE *file =
        fopen("/sdcard/assets/dashboard.png", "rb");
    if (file != nullptr) {
        sd_ready = true;
        fclose(file);
    }

    snprintf(topic, sizeof(topic), "%s/ha/sd_ready", base);
    ok = publish_absolute(
             topic,
             sd_ready ? "ON" : "OFF",
             true) && ok;

    const uint64_t uptime_seconds =
        static_cast<uint64_t>(esp_timer_get_time()) /
        1000000ULL;

    snprintf(topic, sizeof(topic), "%s/ha/uptime", base);
    snprintf(
        payload,
        sizeof(payload),
        "%" PRIu64,
        uptime_seconds);
    ok = publish_absolute(topic, payload, true) && ok;

    snprintf(topic, sizeof(topic), "%s/ha/firmware", base);
    ok = publish_absolute(
             topic,
             ECOPOWER_APP_VERSION,
             true) && ok;

    return ok;
}

void discovery_task(void *)
{
    bool previous_connected = false;
    TickType_t last_state_publish = 0;
    TickType_t last_retry = 0;

    while (true) {
        const bool connected =
            ecopower_mqtt_manager_is_connected();
        const TickType_t now = xTaskGetTickCount();

        if (!connected) {
            set_state(ECOPOWER_HA_WAITING_MQTT);
            previous_connected = false;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        const EcoPowerHaState state =
            ecopower_ha_discovery_get_state();

        const bool should_publish_discovery =
            !previous_connected ||
            (state == ECOPOWER_HA_ERROR &&
             (now - last_retry) >=
                 pdMS_TO_TICKS(kRetryPeriodMs));

        if (should_publish_discovery) {
            last_retry = now;

            if (publish_discovery()) {
                publish_states();
                last_state_publish = now;
            }
        } else if (
            (now - last_state_publish) >=
            pdMS_TO_TICKS(kStatePublishPeriodMs)) {
            if (!publish_states()) {
                set_state(ECOPOWER_HA_ERROR);
            }
            last_state_publish = now;
        }

        previous_connected = true;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

} // namespace

extern "C" esp_err_t ecopower_ha_discovery_init(void)
{
    if (g_initialized) {
        return ESP_OK;
    }

    g_mutex = xSemaphoreCreateMutex();
    if (g_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    g_state = ECOPOWER_HA_WAITING_MQTT;
    g_initialized = true;

    const BaseType_t created = xTaskCreate(
        discovery_task,
        "ecopower_ha",
        6144,
        nullptr,
        4,
        &g_task);

    if (created != pdPASS) {
        g_task = nullptr;
        g_state = ECOPOWER_HA_ERROR;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "Home Assistant discovery service initialized");
    return ESP_OK;
}

extern "C" EcoPowerHaState
ecopower_ha_discovery_get_state(void)
{
    if (!g_initialized || g_mutex == nullptr) {
        return ECOPOWER_HA_DISABLED;
    }

    EcoPowerHaState state = ECOPOWER_HA_DISABLED;

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        state = g_state;
        xSemaphoreGive(g_mutex);
    }

    return state;
}

extern "C" bool ecopower_ha_discovery_is_ready(void)
{
    return ecopower_ha_discovery_get_state() ==
           ECOPOWER_HA_READY;
}
