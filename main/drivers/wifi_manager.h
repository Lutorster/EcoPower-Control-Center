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

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ecopower_wifi_manager_init(void);
esp_err_t ecopower_wifi_manager_scan_async(void);

bool ecopower_wifi_manager_is_initialized(void);
bool ecopower_wifi_manager_is_scanning(void);
bool ecopower_wifi_manager_scan_ready(void);

size_t ecopower_wifi_manager_get_scan_results(
    EcoPowerWifiNetwork *networks,
    size_t max_networks);

#ifdef __cplusplus
}
#endif
