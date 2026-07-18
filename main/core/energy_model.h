#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float pv_power_kw;
    float pv_voltage_v;
    float pv_current_a;

    float inverter_power_kw;
    float inverter_voltage_v;
    float inverter_frequency_hz;
    float inverter_temp_c;

    float battery_soc_pct;
    float battery_voltage_v;
    float battery_current_a;
    float battery_power_kw;      /* Negative = charging, positive = discharging. */
    float battery_temp_c;

    float grid_power_kw;         /* Negative = import, positive = export. */
    float grid_voltage_v;
    float grid_frequency_hz;

    float house_load_kw;
    float load_l1_kw;
    float load_l2_kw;
    float load_l3_kw;

    float efficiency_pct;
    unsigned uptime_days;
    unsigned uptime_hours;
    bool deye_online;
    bool bms_online;
} EnergyData;

void ecopower_energy_model_init(void);
void ecopower_energy_model_tick(unsigned elapsed_ms);
void ecopower_energy_model_get(EnergyData *out);
void ecopower_energy_model_set(const EnergyData *data);
void ecopower_energy_model_enable_demo(bool enabled);
bool ecopower_energy_model_demo_enabled(void);

#ifdef __cplusplus
}
#endif
