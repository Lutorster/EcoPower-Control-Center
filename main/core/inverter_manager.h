#pragma once

#include "inverter_types.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t slave_address;
    EcoPowerInverterType type;
    uint8_t phase_count;
    uint8_t pv_input_count;
} EcoPowerInverterConfig;

esp_err_t ecopower_inverter_manager_init(void);
esp_err_t ecopower_inverter_manager_add(
    const EcoPowerInverterConfig *config,
    uint8_t *device_id);
esp_err_t ecopower_inverter_manager_start(void);
void ecopower_inverter_manager_stop(void);

uint8_t ecopower_inverter_manager_count(void);
bool ecopower_inverter_manager_get(
    uint8_t device_id,
    EcoPowerInverterData *out);
void ecopower_inverter_manager_get_aggregate(
    EcoPowerEnergyAggregate *out);

#ifdef __cplusplus
}
#endif
