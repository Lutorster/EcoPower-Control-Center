#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ECOPOWER_MAX_INVERTERS 8
#define ECOPOWER_MAX_PHASES 3
#define ECOPOWER_MAX_PV_INPUTS 4

typedef enum {
    ECOPOWER_INVERTER_UNKNOWN = 0,
    ECOPOWER_INVERTER_DEYE_HYBRID_1P,
    ECOPOWER_INVERTER_DEYE_HYBRID_3P
} EcoPowerInverterType;

typedef struct {
    bool available;
    float voltage_v;
    float current_a;
    float power_kw;
} EcoPowerPhaseData;

typedef struct {
    bool available;
    float voltage_v;
    float current_a;
    float power_kw;
} EcoPowerPvInputData;

typedef struct {
    bool available;
    float voltage_v;
    float current_a;
    float power_kw; /* EcoPower: negative=charge, positive=discharge. */
    float soc_pct;
    float temperature_c;
} EcoPowerBatteryData;

typedef struct {
    uint8_t device_id;
    uint8_t slave_address;
    EcoPowerInverterType type;
    uint8_t phase_count;
    uint8_t pv_input_count;

    bool online;
    uint32_t successful_polls;
    uint32_t failed_polls;

    EcoPowerPvInputData pv[ECOPOWER_MAX_PV_INPUTS];
    EcoPowerBatteryData battery;
    EcoPowerPhaseData grid[ECOPOWER_MAX_PHASES];
    EcoPowerPhaseData load[ECOPOWER_MAX_PHASES];

    float grid_total_power_kw; /* EcoPower: negative=import, positive=export. */
    float load_total_power_kw;
    float inverter_power_kw;
    float frequency_hz;
    float temperature_c;
} EcoPowerInverterData;

typedef struct {
    uint8_t online_inverters;
    uint8_t configured_inverters;

    float pv_total_kw;
    float battery_total_kw;
    float grid_total_kw;
    float load_total_kw;
    float inverter_total_kw;

    float battery_soc_pct;
    float battery_voltage_v;
    float battery_current_a;
    float battery_temperature_c;

    float primary_pv_voltage_v;
    float primary_pv_current_a;
    float primary_grid_voltage_v;
    float frequency_hz;
    float inverter_temperature_c;

    EcoPowerPhaseData grid[ECOPOWER_MAX_PHASES];
    EcoPowerPhaseData load[ECOPOWER_MAX_PHASES];
} EcoPowerEnergyAggregate;

#ifdef __cplusplus
}
#endif
