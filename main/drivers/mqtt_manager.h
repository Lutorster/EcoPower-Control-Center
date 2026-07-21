#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ECOPOWER_MQTT_DISABLED = 0,
    ECOPOWER_MQTT_DISCONNECTED,
    ECOPOWER_MQTT_CONNECTING,
    ECOPOWER_MQTT_CONNECTED,
    ECOPOWER_MQTT_ERROR
} EcoPowerMqttState;

typedef struct {
    char uri[128];
    char username[64];
    char password[64];
    char base_topic[64];
} EcoPowerMqttConfig;

esp_err_t ecopower_mqtt_manager_init(void);
esp_err_t ecopower_mqtt_manager_save_config(
    const EcoPowerMqttConfig *config);
bool ecopower_mqtt_manager_get_config(
    EcoPowerMqttConfig *config);
esp_err_t ecopower_mqtt_manager_connect(void);
esp_err_t ecopower_mqtt_manager_disconnect(void);
esp_err_t ecopower_mqtt_manager_forget_config(void);

EcoPowerMqttState ecopower_mqtt_manager_get_state(void);
bool ecopower_mqtt_manager_is_connected(void);

int ecopower_mqtt_manager_publish(
    const char *topic_suffix,
    const char *payload,
    int qos,
    bool retain);

#ifdef __cplusplus
}
#endif
