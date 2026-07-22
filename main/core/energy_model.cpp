#include "energy_model.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <cmath>
#include <cstring>

namespace {

EnergyData g_data = {};
float g_phase = 0.0f;
bool g_demo_enabled = false;
bool g_initialized = false;
SemaphoreHandle_t g_mutex = nullptr;

void ensure_initialized()
{
    if (g_initialized) {
        return;
    }

    g_mutex = xSemaphoreCreateMutex();
    std::memset(&g_data, 0, sizeof(g_data));
    g_initialized = true;
}

} // namespace

extern "C" void ecopower_energy_model_init(void)
{
    ensure_initialized();
}

extern "C" void ecopower_energy_model_tick(unsigned elapsed_ms)
{
    ensure_initialized();

    if (!g_demo_enabled || g_mutex == nullptr) {
        return;
    }

    if (xSemaphoreTake(g_mutex, 0) != pdTRUE) {
        return;
    }

    g_phase += static_cast<float>(elapsed_ms) * 0.00045f;
    const float sun = 0.55f + 0.45f * std::sin(g_phase);
    const float load_wave = std::sin(g_phase * 1.7f + 0.8f);
    const float grid_wave = std::sin(g_phase * 0.72f + 2.0f);

    g_data.pv_power_kw = 6.2f + 2.05f * sun;
    g_data.pv_voltage_v = 423.0f + 4.0f * std::sin(g_phase * 0.55f);
    g_data.pv_current_a =
        (g_data.pv_power_kw * 1000.0f) / g_data.pv_voltage_v;

    g_data.house_load_kw = 2.10f + 0.36f * load_wave;
    g_data.load_l1_kw = g_data.house_load_kw * 0.52f;
    g_data.load_l2_kw = g_data.house_load_kw * 0.27f;
    g_data.load_l3_kw =
        g_data.house_load_kw -
        g_data.load_l1_kw -
        g_data.load_l2_kw;

    g_data.grid_power_kw = 0.85f + 0.42f * grid_wave;
    g_data.inverter_power_kw =
        g_data.house_load_kw + g_data.grid_power_kw;
    g_data.battery_power_kw =
        g_data.inverter_power_kw - g_data.pv_power_kw;

    if (g_data.battery_voltage_v > 0.1f) {
        g_data.battery_current_a =
            (g_data.battery_power_kw * 1000.0f) /
            g_data.battery_voltage_v;
    }

    g_data.inverter_temp_c =
        37.8f + 1.2f * std::sin(g_phase * 0.36f);
    g_data.battery_temp_c =
        28.0f + 0.7f * std::sin(g_phase * 0.28f + 1.1f);
    g_data.inverter_voltage_v =
        230.0f + 0.8f * std::sin(g_phase * 0.42f);
    g_data.grid_voltage_v =
        230.2f + 1.1f * std::sin(g_phase * 0.39f + 0.4f);
    g_data.inverter_frequency_hz =
        50.0f + 0.02f * std::sin(g_phase * 0.5f);
    g_data.grid_frequency_hz =
        50.0f + 0.02f * std::sin(g_phase * 0.47f);
    g_data.efficiency_pct =
        96.5f + 0.5f * std::sin(g_phase * 0.31f);

    xSemaphoreGive(g_mutex);
}

extern "C" void ecopower_energy_model_get(EnergyData *out)
{
    ensure_initialized();

    if (out == nullptr || g_mutex == nullptr) {
        return;
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        *out = g_data;
        xSemaphoreGive(g_mutex);
    }
}

extern "C" void ecopower_energy_model_set(const EnergyData *data)
{
    ensure_initialized();

    if (data == nullptr || g_mutex == nullptr) {
        return;
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        g_data = *data;
        xSemaphoreGive(g_mutex);
    }
}

extern "C" void ecopower_energy_model_enable_demo(bool enabled)
{
    ensure_initialized();
    g_demo_enabled = enabled;
}

extern "C" bool ecopower_energy_model_demo_enabled(void)
{
    ensure_initialized();
    return g_demo_enabled;
}
