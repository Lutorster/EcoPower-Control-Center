#include "energy_model.h"

#include <cmath>
#include <cstring>

static EnergyData g_data;
static float g_phase = 0.0f;
static bool g_demo_enabled = true;

extern "C" void ecopower_energy_model_init(void)
{
    std::memset(&g_data, 0, sizeof(g_data));
    g_data.pv_power_kw = 8.25f;
    g_data.pv_voltage_v = 426.3f;
    g_data.pv_current_a = 19.4f;
    g_data.inverter_power_kw = 2.30f;
    g_data.inverter_voltage_v = 230.1f;
    g_data.inverter_frequency_hz = 50.0f;
    g_data.inverter_temp_c = 38.5f;
    g_data.battery_soc_pct = 94.0f;
    g_data.battery_voltage_v = 53.45f;
    g_data.battery_current_a = -18.6f;
    g_data.battery_power_kw = -0.99f;
    g_data.battery_temp_c = 28.4f;
    g_data.grid_power_kw = 1.20f;
    g_data.grid_voltage_v = 230.2f;
    g_data.grid_frequency_hz = 50.0f;
    g_data.house_load_kw = 2.15f;
    g_data.load_l1_kw = 1.12f;
    g_data.load_l2_kw = 0.58f;
    g_data.load_l3_kw = 0.45f;
    g_data.efficiency_pct = 96.8f;
    g_data.uptime_days = 5;
    g_data.uptime_hours = 14;
    g_data.deye_online = false;
    g_data.bms_online = false;
}

extern "C" void ecopower_energy_model_tick(unsigned elapsed_ms)
{
    if (!g_demo_enabled) return;

    g_phase += static_cast<float>(elapsed_ms) * 0.00045f;
    const float sun = 0.55f + 0.45f * std::sin(g_phase);
    const float load_wave = std::sin(g_phase * 1.7f + 0.8f);
    const float grid_wave = std::sin(g_phase * 0.72f + 2.0f);

    g_data.pv_power_kw = 6.2f + 2.05f * sun;
    g_data.pv_voltage_v = 423.0f + 4.0f * std::sin(g_phase * 0.55f);
    g_data.pv_current_a = (g_data.pv_power_kw * 1000.0f) / g_data.pv_voltage_v;

    g_data.house_load_kw = 2.10f + 0.36f * load_wave;
    g_data.load_l1_kw = g_data.house_load_kw * 0.52f;
    g_data.load_l2_kw = g_data.house_load_kw * 0.27f;
    g_data.load_l3_kw = g_data.house_load_kw - g_data.load_l1_kw - g_data.load_l2_kw;

    g_data.grid_power_kw = 0.85f + 0.42f * grid_wave;
    g_data.inverter_power_kw = g_data.house_load_kw + g_data.grid_power_kw;
    g_data.battery_power_kw = g_data.inverter_power_kw - g_data.pv_power_kw;
    g_data.battery_current_a = (g_data.battery_power_kw * 1000.0f) / g_data.battery_voltage_v;

    g_data.battery_soc_pct += (-g_data.battery_power_kw) * static_cast<float>(elapsed_ms) / 36000000.0f * 2.5f;
    if (g_data.battery_soc_pct > 99.0f) g_data.battery_soc_pct = 99.0f;
    if (g_data.battery_soc_pct < 20.0f) g_data.battery_soc_pct = 20.0f;

    g_data.inverter_temp_c = 37.8f + 1.2f * std::sin(g_phase * 0.36f);
    g_data.battery_temp_c = 28.0f + 0.7f * std::sin(g_phase * 0.28f + 1.1f);
    g_data.inverter_voltage_v = 230.0f + 0.8f * std::sin(g_phase * 0.42f);
    g_data.grid_voltage_v = 230.2f + 1.1f * std::sin(g_phase * 0.39f + 0.4f);
    g_data.inverter_frequency_hz = 50.0f + 0.02f * std::sin(g_phase * 0.5f);
    g_data.grid_frequency_hz = 50.0f + 0.02f * std::sin(g_phase * 0.47f);
    g_data.efficiency_pct = 96.5f + 0.5f * std::sin(g_phase * 0.31f);
}

extern "C" void ecopower_energy_model_get(EnergyData *out)
{
    if (out != nullptr) *out = g_data;
}

extern "C" void ecopower_energy_model_set(const EnergyData *data)
{
    if (data != nullptr) g_data = *data;
}

extern "C" void ecopower_energy_model_enable_demo(bool enabled)
{
    g_demo_enabled = enabled;
}

extern "C" bool ecopower_energy_model_demo_enabled(void)
{
    return g_demo_enabled;
}
