#include "ha_discovery.h"

#include "mqtt_manager.h"
#include "wifi_manager.h"
#include "rs485_port.h"
#include "core/energy_model.h"
#include "core/inverter_manager.h"
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
constexpr uint32_t kStatePublishPeriodMs = 5000U;
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

void get_base_topic(char *buffer, size_t size)
{
    EcoPowerMqttConfig config = {};
    if (ecopower_mqtt_manager_get_config(&config) &&
        config.base_topic[0] != '\0') {
        snprintf(buffer, size, "%s", config.base_topic);
    } else {
        snprintf(buffer, size, "ecopower");
    }
}

bool publish_absolute(
    const char *topic,
    const char *payload,
    bool retain = true)
{
    return ecopower_mqtt_manager_publish_topic(
               topic, payload, 1, retain) >= 0;
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

void append_device_json(char *buffer, size_t size)
{
    snprintf(
        buffer,
        size,
        "\"device\":{"
        "\"identifiers\":[\"%s\"],"
        "\"name\":\"EcoPower Control Center\","
        "\"manufacturer\":\"EcoPower\","
        "\"model\":\"Universal Energy Controller\","
        "\"sw_version\":\"%s\""
        "}",
        kDeviceId,
        ECOPOWER_APP_VERSION);
}

bool append_json(
    char *buffer,
    size_t capacity,
    size_t *used,
    const char *text)
{
    if (buffer == nullptr ||
        used == nullptr ||
        text == nullptr ||
        *used >= capacity) {
        return false;
    }

    const int written = snprintf(
        buffer + *used,
        capacity - *used,
        "%s",
        text);

    if (written < 0 ||
        static_cast<size_t>(written) >= capacity - *used) {
        return false;
    }

    *used += static_cast<size_t>(written);
    return true;
}

bool append_json_string_field(
    char *buffer,
    size_t capacity,
    size_t *used,
    const char *key,
    const char *value,
    bool trailing_comma = true)
{
    if (value == nullptr || value[0] == '\0') {
        return true;
    }

    if (buffer == nullptr ||
        used == nullptr ||
        key == nullptr ||
        *used >= capacity) {
        return false;
    }

    const int written = snprintf(
        buffer + *used,
        capacity - *used,
        "\"%s\":\"%s\"%s",
        key,
        value,
        trailing_comma ? "," : "");

    if (written < 0 ||
        static_cast<size_t>(written) >= capacity - *used) {
        return false;
    }

    *used += static_cast<size_t>(written);
    return true;
}

bool publish_sensor(
    const char *object_id,
    const char *name,
    const char *state_topic,
    const char *device_class,
    const char *state_class,
    const char *unit,
    const char *availability_topic,
    const char *device_json,
    bool diagnostic = false)
{
    char payload[1200] = {};
    size_t used = 0U;
    bool ok = true;

    ok = append_json(payload, sizeof(payload), &used, "{") && ok;
    ok = append_json_string_field(
             payload, sizeof(payload), &used,
             "name", name) && ok;

    char unique_id[160] = {};
    snprintf(
        unique_id,
        sizeof(unique_id),
        "%s_%s",
        kDeviceId,
        object_id);

    ok = append_json_string_field(
             payload, sizeof(payload), &used,
             "unique_id", unique_id) && ok;
    ok = append_json_string_field(
             payload, sizeof(payload), &used,
             "state_topic", state_topic) && ok;
    ok = append_json_string_field(
             payload, sizeof(payload), &used,
             "availability_topic", availability_topic) && ok;
    ok = append_json_string_field(
             payload, sizeof(payload), &used,
             "device_class", device_class) && ok;
    ok = append_json_string_field(
             payload, sizeof(payload), &used,
             "state_class", state_class) && ok;
    ok = append_json_string_field(
             payload, sizeof(payload), &used,
             "unit_of_measurement", unit) && ok;

    if (diagnostic) {
        ok = append_json(
                 payload,
                 sizeof(payload),
                 &used,
                 "\"entity_category\":\"diagnostic\",") && ok;
    }

    ok = append_json(
             payload,
             sizeof(payload),
             &used,
             device_json) && ok;
    ok = append_json(payload, sizeof(payload), &used, "}") && ok;

    if (!ok) {
        ESP_LOGE(
            TAG,
            "Discovery payload overflow for sensor %s",
            object_id);
        return false;
    }

    return publish_discovery_config(
        "sensor", object_id, payload);
}

bool publish_binary_sensor(
    const char *object_id,
    const char *name,
    const char *state_topic,
    const char *availability_topic,
    const char *device_json,
    bool diagnostic)
{
    char payload[1024] = {};
    snprintf(
        payload,
        sizeof(payload),
        "{"
        "\"name\":\"%s\","
        "\"unique_id\":\"%s_%s\","
        "\"state_topic\":\"%s\","
        "\"availability_topic\":\"%s\","
        "\"payload_on\":\"ON\","
        "\"payload_off\":\"OFF\","
        "\"device_class\":\"connectivity\","
        "%s"
        "%s"
        "}",
        name,
        kDeviceId,
        object_id,
        state_topic,
        availability_topic,
        diagnostic
            ? "\"entity_category\":\"diagnostic\","
            : "",
        device_json);

    return publish_discovery_config(
        "binary_sensor", object_id, payload);
}

bool publish_discovery()
{
    if (!ecopower_mqtt_manager_is_connected()) {
        return false;
    }

    set_state(ECOPOWER_HA_PUBLISHING);

    char base[64] = {};
    get_base_topic(base, sizeof(base));

    char availability[128] = {};
    snprintf(
        availability,
        sizeof(availability),
        "%s/availability",
        base);

    char device[320] = {};
    append_device_json(device, sizeof(device));

    char topic[160] = {};
    bool ok = true;

#define SENSOR(ID, NAME, CLASS, STATE, UNIT, DIAG) \
    snprintf(topic, sizeof(topic), "%s/ha/" ID, base); \
    ok = publish_sensor( \
        ID, NAME, topic, CLASS, STATE, UNIT, \
        availability, device, DIAG) && ok

    SENSOR("wifi_rssi", "Wi-Fi Signal",
           "signal_strength", "measurement", "dBm", true);
    SENSOR("uptime", "Uptime",
           "duration", "", "s", true);
    SENSOR("firmware", "Firmware Version",
           "", "", "", true);
    SENSOR("online_inverters", "Online Inverters",
           "", "measurement", "", true);

    SENSOR("pv_power", "PV Power",
           "power", "measurement", "kW", false);
    SENSOR("pv_voltage", "PV Voltage",
           "voltage", "measurement", "V", false);
    SENSOR("pv_current", "PV Current",
           "current", "measurement", "A", false);

    SENSOR("battery_soc", "Battery SOC",
           "battery", "measurement", "%", false);
    SENSOR("battery_voltage", "Battery Voltage",
           "voltage", "measurement", "V", false);
    SENSOR("battery_current", "Battery Current",
           "current", "measurement", "A", false);
    SENSOR("battery_power", "Battery Power",
           "power", "measurement", "kW", false);
    SENSOR("battery_temperature", "Battery Temperature",
           "temperature", "measurement", "°C", false);

    SENSOR("grid_power", "Grid Power",
           "power", "measurement", "kW", false);
    SENSOR("grid_voltage", "Grid Voltage",
           "voltage", "measurement", "V", false);
    SENSOR("grid_frequency", "Grid Frequency",
           "frequency", "measurement", "Hz", false);

    SENSOR("load_power", "House Load",
           "power", "measurement", "kW", false);
    SENSOR("load_l1", "Load L1",
           "power", "measurement", "kW", false);
    SENSOR("load_l2", "Load L2",
           "power", "measurement", "kW", false);
    SENSOR("load_l3", "Load L3",
           "power", "measurement", "kW", false);

    SENSOR("inverter_power", "Inverter Power",
           "power", "measurement", "kW", false);
    SENSOR("inverter_temperature", "Inverter Temperature",
           "temperature", "measurement", "°C", false);

#undef SENSOR

    snprintf(topic, sizeof(topic), "%s/ha/mqtt_connected", base);
    ok = publish_binary_sensor(
        "mqtt_connected", "MQTT Connection",
        topic, availability, device, true) && ok;

    snprintf(topic, sizeof(topic), "%s/ha/rs485_ready", base);
    ok = publish_binary_sensor(
        "rs485_ready", "RS485 Transport",
        topic, availability, device, true) && ok;

    snprintf(topic, sizeof(topic), "%s/ha/inverter_online", base);
    ok = publish_binary_sensor(
        "inverter_online", "Inverter Connection",
        topic, availability, device, false) && ok;

    ok = publish_absolute(
             availability, "online", true) && ok;

    set_state(ok ? ECOPOWER_HA_READY : ECOPOWER_HA_ERROR);
    ESP_LOGI(
        TAG,
        "Home Assistant telemetry discovery: %s",
        ok ? "READY" : "FAILED");
    return ok;
}

bool publish_float(
    const char *base,
    const char *suffix,
    float value,
    int decimals)
{
    char topic[160] = {};
    char payload[48] = {};
    snprintf(topic, sizeof(topic), "%s/ha/%s", base, suffix);
    snprintf(payload, sizeof(payload), "%.*f", decimals, value);
    return publish_absolute(topic, payload, true);
}

bool publish_states()
{
    if (!ecopower_mqtt_manager_is_connected()) {
        return false;
    }

    char base[64] = {};
    get_base_topic(base, sizeof(base));

    char topic[160] = {};
    char payload[64] = {};
    bool ok = true;

    snprintf(topic, sizeof(topic), "%s/availability", base);
    ok = publish_absolute(topic, "online", true) && ok;

    snprintf(topic, sizeof(topic), "%s/ha/wifi_rssi", base);
    snprintf(
        payload,
        sizeof(payload),
        "%d",
        static_cast<int>(
            ecopower_wifi_manager_get_rssi()));
    ok = publish_absolute(topic, payload, true) && ok;

    snprintf(topic, sizeof(topic), "%s/ha/mqtt_connected", base);
    ok = publish_absolute(topic, "ON", true) && ok;

    snprintf(topic, sizeof(topic), "%s/ha/rs485_ready", base);
    ok = publish_absolute(
             topic,
             ecopower_rs485_is_initialized() ? "ON" : "OFF",
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
             topic, ECOPOWER_APP_VERSION, true) && ok;

    EnergyData data = {};
    ecopower_energy_model_get(&data);

    EcoPowerEnergyAggregate aggregate = {};
    ecopower_inverter_manager_get_aggregate(&aggregate);

    snprintf(
        topic,
        sizeof(topic),
        "%s/ha/online_inverters",
        base);
    snprintf(
        payload,
        sizeof(payload),
        "%u",
        static_cast<unsigned>(
            aggregate.online_inverters));
    ok = publish_absolute(topic, payload, true) && ok;

    snprintf(topic, sizeof(topic), "%s/ha/inverter_online", base);
    ok = publish_absolute(
             topic,
             data.deye_online ? "ON" : "OFF",
             true) && ok;

    ok = publish_float(base, "pv_power", data.pv_power_kw, 3) && ok;
    ok = publish_float(base, "pv_voltage", data.pv_voltage_v, 1) && ok;
    ok = publish_float(base, "pv_current", data.pv_current_a, 1) && ok;

    ok = publish_float(base, "battery_soc", data.battery_soc_pct, 0) && ok;
    ok = publish_float(base, "battery_voltage", data.battery_voltage_v, 2) && ok;
    ok = publish_float(base, "battery_current", data.battery_current_a, 2) && ok;
    ok = publish_float(base, "battery_power", data.battery_power_kw, 3) && ok;
    ok = publish_float(base, "battery_temperature", data.battery_temp_c, 1) && ok;

    ok = publish_float(base, "grid_power", data.grid_power_kw, 3) && ok;
    ok = publish_float(base, "grid_voltage", data.grid_voltage_v, 1) && ok;
    ok = publish_float(base, "grid_frequency", data.grid_frequency_hz, 2) && ok;

    ok = publish_float(base, "load_power", data.house_load_kw, 3) && ok;
    ok = publish_float(base, "load_l1", data.load_l1_kw, 3) && ok;
    ok = publish_float(base, "load_l2", data.load_l2_kw, 3) && ok;
    ok = publish_float(base, "load_l3", data.load_l3_kw, 3) && ok;

    ok = publish_float(base, "inverter_power", data.inverter_power_kw, 3) && ok;
    ok = publish_float(base, "inverter_temperature", data.inverter_temp_c, 1) && ok;

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

        const bool should_publish_discovery =
            !previous_connected ||
            (ecopower_ha_discovery_get_state() ==
                 ECOPOWER_HA_ERROR &&
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

    const BaseType_t created = xTaskCreate(
        discovery_task,
        "ecopower_ha",
        6144,
        nullptr,
        4,
        &g_task);

    if (created != pdPASS) {
        vSemaphoreDelete(g_mutex);
        g_mutex = nullptr;
        return ESP_ERR_NO_MEM;
    }

    g_initialized = true;
    set_state(ECOPOWER_HA_WAITING_MQTT);
    ESP_LOGI(TAG, "Home Assistant telemetry initialized");
    return ESP_OK;
}

extern "C" EcoPowerHaState
ecopower_ha_discovery_get_state(void)
{
    if (g_mutex == nullptr) {
        return g_state;
    }

    EcoPowerHaState state = g_state;
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
