#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ECOPOWER_SOC_HISTORY_POINTS 288

typedef struct {
    float pv_production_kwh;
    float battery_charged_kwh;
    float battery_discharged_kwh;
    float consumption_kwh;
    float grid_import_kwh;
    float grid_export_kwh;

    float soc_history[ECOPOWER_SOC_HISTORY_POINTS];
    size_t soc_history_count;
    bool time_valid;
} EcoPowerDailyEnergyData;

esp_err_t ecopower_daily_energy_init(void);
bool ecopower_daily_energy_get(EcoPowerDailyEnergyData *out);
esp_err_t ecopower_daily_energy_reset_today(void);

#ifdef __cplusplus
}
#endif
