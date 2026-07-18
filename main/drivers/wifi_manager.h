#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secured;
} EcoPowerWifiNetwork;

typedef enum {
    ECOPOWER_WIFI_DISCONNECTED = 0,
    ECOPOWER_WIFI_CONNECTING,
    ECOPOWER_WIFI_CONNECTED,
    ECOPOWER_WIFI_FAILED
} EcoPowerWifiState;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ecopower_wifi_manager_init(void);
esp_err_t ecopower_wifi_manager_scan_async(void);

esp_err_t ecopower_wifi_manager_connect(
    const char *ssid,
    const char *password);

esp_err_t ecopower_wifi_manager_disconnect(void);

bool ecopower_wifi_manager_is_initialized(void);
bool ecopower_wifi_manager_is_scanning(void);
bool ecopower_wifi_manager_scan_ready(void);

EcoPowerWifiState ecopower_wifi_manager_get_state(void);

size_t ecopower_wifi_manager_get_scan_results(
    EcoPowerWifiNetwork *networks,
    size_t max_networks);

bool ecopower_wifi_manager_get_connected_ssid(
    char *buffer,
    size_t buffer_size);

bool ecopower_wifi_manager_get_ip_address(
    char *buffer,
    size_t buffer_size);

int8_t ecopower_wifi_manager_get_rssi(void);

#ifdef __cplusplus
}
#endif
