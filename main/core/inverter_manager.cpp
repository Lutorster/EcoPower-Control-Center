#include "inverter_manager.h"

#include "energy_model.h"
#include "deye/deye_driver.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <cmath>
#include <cstring>

static const char *TAG = "EcoPower_INV_MGR";

namespace {

EcoPowerInverterData g_devices[ECOPOWER_MAX_INVERTERS] = {};
uint8_t g_device_count = 0U;
SemaphoreHandle_t g_mutex = nullptr;
TaskHandle_t g_task = nullptr;
volatile bool g_stop_requested = false;

constexpr uint32_t kPollPeriodMs = 2000U;

/* Deye hybrid one-phase register blocks from deye_hybrid.yaml. */
constexpr uint16_t kStatusStart = 0x004FU;
constexpr uint16_t kStatusCount = 13U; /* 0x004F..0x005B */
constexpr uint16_t kPvStart = 0x006DU;
constexpr uint16_t kPvCount = 4U;      /* 0x006D..0x0070 */
constexpr uint16_t kPowerStart = 0x0096U;
constexpr uint16_t kPowerCount = 43U;  /* 0x0096..0x00C0 */

int16_t s16(uint16_t value)
{
    return static_cast<int16_t>(value);
}

float signed_scaled(uint16_t value, float scale)
{
    return static_cast<float>(s16(value)) * scale;
}

uint16_t at(
    const uint16_t *block,
    uint16_t block_start,
    uint16_t address)
{
    return block[address - block_start];
}

bool read_block(
    uint8_t slave,
    uint16_t start,
    uint16_t count,
    uint16_t *registers)
{
    const EcoPowerModbusResult result =
        ecopower_deye_read_holding_registers_for_slave(
            slave, start, count, registers);

    return result.status == ECOPOWER_MODBUS_OK;
}

void clear_measurements(EcoPowerInverterData &device)
{
    for (auto &pv : device.pv) {
        pv = {};
    }
    device.battery = {};
    for (auto &phase : device.grid) {
        phase = {};
    }
    for (auto &phase : device.load) {
        phase = {};
    }
    device.grid_total_power_kw = 0.0f;
    device.load_total_power_kw = 0.0f;
    device.inverter_power_kw = 0.0f;
    device.frequency_hz = 0.0f;
    device.temperature_c = 0.0f;
}

bool poll_deye_hybrid_1p(EcoPowerInverterData &device)
{
    uint16_t status[kStatusCount] = {};
    uint16_t pv[kPvCount] = {};
    uint16_t power[kPowerCount] = {};

    if (!read_block(
            device.slave_address,
            kStatusStart,
            kStatusCount,
            status) ||
        !read_block(
            device.slave_address,
            kPvStart,
            kPvCount,
            pv) ||
        !read_block(
            device.slave_address,
            kPowerStart,
            kPowerCount,
            power)) {
        return false;
    }

    clear_measurements(device);

    device.frequency_hz =
        static_cast<float>(
            at(status, kStatusStart, 0x004F)) * 0.01f;

    const float dc_temperature =
        (signed_scaled(
            at(status, kStatusStart, 0x005A), 1.0f) -
         1000.0f) * 0.1f;
    const float ac_temperature =
        (signed_scaled(
            at(status, kStatusStart, 0x005B), 1.0f) -
         1000.0f) * 0.1f;
    device.temperature_c =
        std::fmax(dc_temperature, ac_temperature);

    const uint8_t pv_count =
        device.pv_input_count < 2U
            ? device.pv_input_count
            : 2U;

    if (pv_count >= 1U) {
        device.pv[0].available = true;
        device.pv[0].voltage_v =
            static_cast<float>(
                at(pv, kPvStart, 0x006D)) * 0.1f;
        device.pv[0].current_a =
            static_cast<float>(
                at(pv, kPvStart, 0x006E)) * 0.1f;
        device.pv[0].power_kw =
            static_cast<float>(
                at(power, kPowerStart, 0x00BA)) * 0.001f;
    }

    if (pv_count >= 2U) {
        device.pv[1].available = true;
        device.pv[1].voltage_v =
            static_cast<float>(
                at(pv, kPvStart, 0x006F)) * 0.1f;
        device.pv[1].current_a =
            static_cast<float>(
                at(pv, kPvStart, 0x0070)) * 0.1f;
        device.pv[1].power_kw =
            static_cast<float>(
                at(power, kPowerStart, 0x00BB)) * 0.001f;
    }

    device.grid[0].available = true;
    device.grid[0].voltage_v =
        static_cast<float>(
            at(power, kPowerStart, 0x0096)) * 0.1f;
    device.grid[0].current_a =
        static_cast<float>(
            at(power, kPowerStart, 0x00A0)) * 0.01f;

    /*
     * Deye/Solarman convention normally reports positive grid power as
     * import. EcoPower uses negative=import and positive=export.
     */
    const float deye_grid_kw =
        signed_scaled(
            at(power, kPowerStart, 0x00A9), 0.001f);
    device.grid_total_power_kw = -deye_grid_kw;
    device.grid[0].power_kw = device.grid_total_power_kw;

    device.load[0].available = true;
    device.load[0].voltage_v =
        static_cast<float>(
            at(power, kPowerStart, 0x009D)) * 0.1f;
    device.load[0].power_kw =
        static_cast<float>(
            at(power, kPowerStart, 0x00B0)) * 0.001f;

    device.load_total_power_kw =
        static_cast<float>(
            at(power, kPowerStart, 0x00B2)) * 0.001f;

    if (device.load[0].voltage_v > 1.0f) {
        device.load[0].current_a =
            (device.load[0].power_kw * 1000.0f) /
            device.load[0].voltage_v;
    }

    device.inverter_power_kw =
        signed_scaled(
            at(power, kPowerStart, 0x00AF), 0.001f);

    device.battery.available = true;
    device.battery.temperature_c =
        (static_cast<float>(
            at(power, kPowerStart, 0x00B6)) -
         1000.0f) * 0.1f;
    device.battery.voltage_v =
        static_cast<float>(
            at(power, kPowerStart, 0x00B7)) * 0.01f;
    device.battery.soc_pct =
        static_cast<float>(
            at(power, kPowerStart, 0x00B8));

    const uint16_t battery_status =
        at(power, kPowerStart, 0x00BD);
    const float raw_battery_power_kw =
        std::fabs(signed_scaled(
            at(power, kPowerStart, 0x00BE), 0.001f));
    const float raw_battery_current_a =
        std::fabs(signed_scaled(
            at(power, kPowerStart, 0x00BF), 0.01f));

    if (battery_status == 0U) { /* Charge */
        device.battery.power_kw = -raw_battery_power_kw;
        device.battery.current_a = -raw_battery_current_a;
    } else if (battery_status == 2U) { /* Discharge */
        device.battery.power_kw = raw_battery_power_kw;
        device.battery.current_a = raw_battery_current_a;
    } else {
        device.battery.power_kw =
            signed_scaled(
                at(power, kPowerStart, 0x00BE), 0.001f);
        device.battery.current_a =
            signed_scaled(
                at(power, kPowerStart, 0x00BF), 0.01f);
    }

    return true;
}

bool poll_device(EcoPowerInverterData &device)
{
    switch (device.type) {
        case ECOPOWER_INVERTER_DEYE_HYBRID_1P:
            return poll_deye_hybrid_1p(device);

        case ECOPOWER_INVERTER_DEYE_HYBRID_3P:
            /*
             * Architecture is ready, but SG04LP3 has a separate register
             * profile. It will be added as a dedicated decoder.
             */
            ESP_LOGW(
                TAG,
                "INV%u: three-phase profile not installed yet",
                static_cast<unsigned>(device.device_id));
            return false;

        default:
            return false;
    }
}

void build_aggregate_locked(EcoPowerEnergyAggregate &aggregate)
{
    std::memset(&aggregate, 0, sizeof(aggregate));
    aggregate.configured_inverters = g_device_count;

    float battery_soc_sum = 0.0f;
    float battery_voltage_sum = 0.0f;
    float battery_temperature_sum = 0.0f;
    uint8_t battery_count = 0U;
    uint8_t frequency_count = 0U;
    uint8_t temperature_count = 0U;
    bool primary_pv_set = false;
    bool primary_grid_set = false;

    for (uint8_t index = 0U; index < g_device_count; ++index) {
        const EcoPowerInverterData &device = g_devices[index];

        if (!device.online) {
            continue;
        }

        ++aggregate.online_inverters;
        aggregate.grid_total_kw += device.grid_total_power_kw;
        aggregate.load_total_kw += device.load_total_power_kw;
        aggregate.inverter_total_kw += device.inverter_power_kw;

        if (device.frequency_hz > 0.0f) {
            aggregate.frequency_hz += device.frequency_hz;
            ++frequency_count;
        }

        if (device.temperature_c != 0.0f) {
            aggregate.inverter_temperature_c +=
                device.temperature_c;
            ++temperature_count;
        }

        if (device.battery.available) {
            aggregate.battery_total_kw +=
                device.battery.power_kw;
            aggregate.battery_current_a +=
                device.battery.current_a;
            battery_soc_sum += device.battery.soc_pct;
            battery_voltage_sum += device.battery.voltage_v;
            battery_temperature_sum +=
                device.battery.temperature_c;
            ++battery_count;
        }

        for (uint8_t pv_index = 0U;
             pv_index < device.pv_input_count &&
             pv_index < ECOPOWER_MAX_PV_INPUTS;
             ++pv_index) {
            const EcoPowerPvInputData &input =
                device.pv[pv_index];

            if (!input.available) {
                continue;
            }

            aggregate.pv_total_kw += input.power_kw;

            if (!primary_pv_set &&
                (input.voltage_v > 0.0f ||
                 input.current_a > 0.0f)) {
                aggregate.primary_pv_voltage_v =
                    input.voltage_v;
                aggregate.primary_pv_current_a =
                    input.current_a;
                primary_pv_set = true;
            }
        }

        for (uint8_t phase = 0U;
             phase < device.phase_count &&
             phase < ECOPOWER_MAX_PHASES;
             ++phase) {
            if (device.grid[phase].available) {
                aggregate.grid[phase].available = true;
                aggregate.grid[phase].power_kw +=
                    device.grid[phase].power_kw;
                aggregate.grid[phase].current_a +=
                    device.grid[phase].current_a;

                if (!primary_grid_set &&
                    device.grid[phase].voltage_v > 0.0f) {
                    aggregate.primary_grid_voltage_v =
                        device.grid[phase].voltage_v;
                    primary_grid_set = true;
                }
            }

            if (device.load[phase].available) {
                aggregate.load[phase].available = true;
                aggregate.load[phase].power_kw +=
                    device.load[phase].power_kw;
                aggregate.load[phase].current_a +=
                    device.load[phase].current_a;
            }
        }
    }

    if (battery_count > 0U) {
        aggregate.battery_soc_pct =
            battery_soc_sum / battery_count;
        aggregate.battery_voltage_v =
            battery_voltage_sum / battery_count;
        aggregate.battery_temperature_c =
            battery_temperature_sum / battery_count;
    }

    if (frequency_count > 0U) {
        aggregate.frequency_hz /= frequency_count;
    }

    if (temperature_count > 0U) {
        aggregate.inverter_temperature_c /=
            temperature_count;
    }
}

void update_energy_model_locked()
{
    EcoPowerEnergyAggregate aggregate = {};
    build_aggregate_locked(aggregate);

    EnergyData data = {};
    data.pv_power_kw = aggregate.pv_total_kw;
    data.pv_voltage_v = aggregate.primary_pv_voltage_v;
    data.pv_current_a = aggregate.primary_pv_current_a;

    data.inverter_power_kw = aggregate.inverter_total_kw;
    data.inverter_voltage_v =
        aggregate.load[0].available
            ? aggregate.load[0].voltage_v
            : aggregate.primary_grid_voltage_v;
    data.inverter_frequency_hz = aggregate.frequency_hz;
    data.inverter_temp_c =
        aggregate.inverter_temperature_c;

    data.battery_soc_pct = aggregate.battery_soc_pct;
    data.battery_voltage_v = aggregate.battery_voltage_v;
    data.battery_current_a = aggregate.battery_current_a;
    data.battery_power_kw = aggregate.battery_total_kw;
    data.battery_temp_c =
        aggregate.battery_temperature_c;

    data.grid_power_kw = aggregate.grid_total_kw;
    data.grid_voltage_v = aggregate.primary_grid_voltage_v;
    data.grid_frequency_hz = aggregate.frequency_hz;

    data.house_load_kw = aggregate.load_total_kw;
    data.load_l1_kw = aggregate.load[0].power_kw;
    data.load_l2_kw = aggregate.load[1].power_kw;
    data.load_l3_kw = aggregate.load[2].power_kw;

    data.efficiency_pct = 0.0f;
    data.deye_online = aggregate.online_inverters > 0U;
    data.bms_online = false;

    ecopower_energy_model_set(&data);
}

void polling_task(void *)
{
    ESP_LOGI(TAG, "Universal real-telemetry polling started");

    while (!g_stop_requested) {
        if (g_mutex != nullptr &&
            xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
            for (uint8_t index = 0U;
                 index < g_device_count;
                 ++index) {
                EcoPowerInverterData &device = g_devices[index];
                const bool ok = poll_device(device);

                if (ok) {
                    device.online = true;
                    ++device.successful_polls;

                    ESP_LOGI(
                        TAG,
                        "INV%u S%u: PV=%.3fkW GRID=%.3fkW "
                        "LOAD=%.3fkW BAT=%.2fV %.0f%% %.3fkW",
                        static_cast<unsigned>(device.device_id),
                        static_cast<unsigned>(device.slave_address),
                        device.pv[0].power_kw +
                            device.pv[1].power_kw,
                        device.grid_total_power_kw,
                        device.load_total_power_kw,
                        device.battery.voltage_v,
                        device.battery.soc_pct,
                        device.battery.power_kw);
                } else {
                    device.online = false;
                    ++device.failed_polls;
                    ESP_LOGW(
                        TAG,
                        "INV%u slave=%u poll failed",
                        static_cast<unsigned>(device.device_id),
                        static_cast<unsigned>(device.slave_address));
                }
            }

            update_energy_model_locked();
            xSemaphoreGive(g_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(kPollPeriodMs));
    }

    g_task = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

extern "C" esp_err_t ecopower_inverter_manager_init(void)
{
    if (g_mutex != nullptr) {
        return ESP_OK;
    }

    g_mutex = xSemaphoreCreateMutex();
    if (g_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    std::memset(g_devices, 0, sizeof(g_devices));
    g_device_count = 0U;

    ecopower_energy_model_init();
    ecopower_energy_model_enable_demo(false);
    return ESP_OK;
}

extern "C" esp_err_t ecopower_inverter_manager_add(
    const EcoPowerInverterConfig *config,
    uint8_t *device_id)
{
    if (config == nullptr ||
        config->slave_address == 0U ||
        config->phase_count == 0U ||
        config->phase_count > ECOPOWER_MAX_PHASES ||
        config->pv_input_count > ECOPOWER_MAX_PV_INPUTS) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (g_device_count >= ECOPOWER_MAX_INVERTERS) {
        xSemaphoreGive(g_mutex);
        return ESP_ERR_NO_MEM;
    }

    for (uint8_t index = 0U; index < g_device_count; ++index) {
        if (g_devices[index].slave_address ==
            config->slave_address) {
            xSemaphoreGive(g_mutex);
            return ESP_ERR_INVALID_STATE;
        }
    }

    EcoPowerInverterData &device = g_devices[g_device_count];
    std::memset(&device, 0, sizeof(device));

    device.device_id = g_device_count;
    device.slave_address = config->slave_address;
    device.type = config->type;
    device.phase_count = config->phase_count;
    device.pv_input_count = config->pv_input_count;

    if (device_id != nullptr) {
        *device_id = device.device_id;
    }

    ++g_device_count;
    xSemaphoreGive(g_mutex);

    ESP_LOGI(
        TAG,
        "Added inverter: id=%u slave=%u type=%u phases=%u pv=%u",
        static_cast<unsigned>(device.device_id),
        static_cast<unsigned>(device.slave_address),
        static_cast<unsigned>(device.type),
        static_cast<unsigned>(device.phase_count),
        static_cast<unsigned>(device.pv_input_count));

    return ESP_OK;
}

extern "C" esp_err_t ecopower_inverter_manager_start(void)
{
    if (g_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_task != nullptr) {
        return ESP_OK;
    }

    g_stop_requested = false;

    const BaseType_t created = xTaskCreate(
        polling_task,
        "inverter_manager",
        6144,
        nullptr,
        4,
        &g_task);

    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

extern "C" void ecopower_inverter_manager_stop(void)
{
    g_stop_requested = true;
}

extern "C" uint8_t ecopower_inverter_manager_count(void)
{
    return g_device_count;
}

extern "C" bool ecopower_inverter_manager_get(
    uint8_t device_id,
    EcoPowerInverterData *out)
{
    if (out == nullptr ||
        g_mutex == nullptr ||
        device_id >= g_device_count) {
        return false;
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    *out = g_devices[device_id];
    xSemaphoreGive(g_mutex);
    return true;
}

extern "C" void ecopower_inverter_manager_get_aggregate(
    EcoPowerEnergyAggregate *out)
{
    if (out == nullptr) {
        return;
    }

    std::memset(out, 0, sizeof(*out));

    if (g_mutex == nullptr ||
        xSemaphoreTake(g_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    build_aggregate_locked(*out);
    xSemaphoreGive(g_mutex);
}
