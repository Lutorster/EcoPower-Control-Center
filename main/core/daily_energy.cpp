#include "daily_energy.h"

#include "energy_model.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

static const char *TAG = "EcoPower_Daily";

namespace {

constexpr const char *kNamespace = "eco_daily";
constexpr const char *kBlobKey = "state";
constexpr uint32_t kStorageVersion = 1U;
constexpr uint32_t kTaskPeriodMs = 1000U;
constexpr int64_t kSavePeriodUs = 300LL * 1000000LL;
constexpr std::time_t kSocPeriodSeconds = 5 * 60;

struct PersistedState {
    uint32_t version;
    int32_t date_key;

    float pv_production_kwh;
    float battery_charged_kwh;
    float battery_discharged_kwh;
    float consumption_kwh;
    float grid_import_kwh;
    float grid_export_kwh;

    float soc_history[ECOPOWER_SOC_HISTORY_POINTS];
    uint16_t soc_write_index;
    uint16_t soc_count;
    int64_t last_soc_bucket;
};

PersistedState g_state = {};
SemaphoreHandle_t g_mutex = nullptr;
TaskHandle_t g_task = nullptr;
bool g_initialized = false;
int64_t g_last_update_us = 0;
int64_t g_last_save_us = 0;

bool get_local_time(
    std::time_t *epoch,
    std::tm *local)
{
    if (epoch == nullptr || local == nullptr) {
        return false;
    }

    std::time(epoch);
    localtime_r(epoch, local);

    return local->tm_year >= (2024 - 1900);
}

int32_t make_date_key(const std::tm &local)
{
    return
        (local.tm_year + 1900) * 10000 +
        (local.tm_mon + 1) * 100 +
        local.tm_mday;
}

void reset_daily_locked(int32_t date_key)
{
    g_state.date_key = date_key;
    g_state.pv_production_kwh = 0.0f;
    g_state.battery_charged_kwh = 0.0f;
    g_state.battery_discharged_kwh = 0.0f;
    g_state.consumption_kwh = 0.0f;
    g_state.grid_import_kwh = 0.0f;
    g_state.grid_export_kwh = 0.0f;

    ESP_LOGI(
        TAG,
        "Daily counters reset for date %ld",
        static_cast<long>(date_key));
}

esp_err_t save_locked()
{
    nvs_handle_t handle = 0;
    esp_err_t error =
        nvs_open(kNamespace, NVS_READWRITE, &handle);

    if (error != ESP_OK) {
        return error;
    }

    g_state.version = kStorageVersion;

    error = nvs_set_blob(
        handle,
        kBlobKey,
        &g_state,
        sizeof(g_state));

    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }

    nvs_close(handle);
    return error;
}

void load_state()
{
    std::memset(&g_state, 0, sizeof(g_state));
    g_state.version = kStorageVersion;
    g_state.last_soc_bucket = -1;

    nvs_handle_t handle = 0;
    esp_err_t error =
        nvs_open(kNamespace, NVS_READONLY, &handle);

    if (error != ESP_OK) {
        ESP_LOGI(TAG, "No saved daily energy state");
        return;
    }

    size_t size = sizeof(g_state);
    PersistedState loaded = {};

    error = nvs_get_blob(
        handle,
        kBlobKey,
        &loaded,
        &size);

    nvs_close(handle);

    if (error == ESP_OK &&
        size == sizeof(loaded) &&
        loaded.version == kStorageVersion &&
        loaded.soc_count <= ECOPOWER_SOC_HISTORY_POINTS &&
        loaded.soc_write_index < ECOPOWER_SOC_HISTORY_POINTS) {
        g_state = loaded;
        ESP_LOGI(
            TAG,
            "Daily energy restored: PV=%.3f Load=%.3f Export=%.3f kWh",
            g_state.pv_production_kwh,
            g_state.consumption_kwh,
            g_state.grid_export_kwh);
    } else {
        ESP_LOGW(TAG, "Saved daily energy state is invalid; starting clean");
    }
}

void add_soc_sample_locked(
    float soc,
    int64_t bucket)
{
    if (!std::isfinite(soc)) {
        return;
    }

    soc = std::clamp(soc, 0.0f, 100.0f);

    g_state.soc_history[g_state.soc_write_index] = soc;
    g_state.soc_write_index =
        static_cast<uint16_t>(
            (g_state.soc_write_index + 1U) %
            ECOPOWER_SOC_HISTORY_POINTS);

    if (g_state.soc_count < ECOPOWER_SOC_HISTORY_POINTS) {
        ++g_state.soc_count;
    }

    g_state.last_soc_bucket = bucket;
}

void integrate_locked(
    const EnergyData &data,
    float hours)
{
    if (!data.deye_online ||
        hours <= 0.0f ||
        hours > (10.0f / 3600.0f)) {
        return;
    }

    if (std::isfinite(data.pv_power_kw)) {
        g_state.pv_production_kwh +=
            std::max(data.pv_power_kw, 0.0f) * hours;
    }

    if (std::isfinite(data.house_load_kw)) {
        g_state.consumption_kwh +=
            std::max(data.house_load_kw, 0.0f) * hours;
    }

    if (std::isfinite(data.battery_power_kw)) {
        if (data.battery_power_kw < 0.0f) {
            g_state.battery_charged_kwh +=
                -data.battery_power_kw * hours;
        } else {
            g_state.battery_discharged_kwh +=
                data.battery_power_kw * hours;
        }
    }

    if (std::isfinite(data.grid_power_kw)) {
        if (data.grid_power_kw < 0.0f) {
            g_state.grid_import_kwh +=
                -data.grid_power_kw * hours;
        } else {
            g_state.grid_export_kwh +=
                data.grid_power_kw * hours;
        }
    }
}

void task_main(void *)
{
    ESP_LOGI(
        TAG,
        "Daily energy engine started; SOC history=%d points",
        ECOPOWER_SOC_HISTORY_POINTS);

    g_last_update_us = esp_timer_get_time();
    g_last_save_us = g_last_update_us;

    while (true) {
        const int64_t now_us = esp_timer_get_time();
        const int64_t delta_us = now_us - g_last_update_us;
        g_last_update_us = now_us;

        EnergyData data = {};
        ecopower_energy_model_get(&data);

        std::time_t epoch = 0;
        std::tm local = {};
        const bool time_valid =
            get_local_time(&epoch, &local);

        if (g_mutex != nullptr &&
            xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
            if (time_valid) {
                const int32_t current_date =
                    make_date_key(local);

                if (g_state.date_key != current_date) {
                    reset_daily_locked(current_date);
                    save_locked();
                    g_last_save_us = now_us;
                }

                const int64_t soc_bucket =
                    static_cast<int64_t>(epoch / kSocPeriodSeconds);

                if (data.deye_online &&
                    soc_bucket != g_state.last_soc_bucket) {
                    add_soc_sample_locked(
                        data.battery_soc_pct,
                        soc_bucket);
                }
            }

            const float hours =
                static_cast<float>(delta_us) /
                3600000000.0f;

            integrate_locked(data, hours);

            if ((now_us - g_last_save_us) >= kSavePeriodUs) {
                const esp_err_t error = save_locked();
                if (error != ESP_OK) {
                    ESP_LOGW(
                        TAG,
                        "NVS save failed: %s",
                        esp_err_to_name(error));
                }
                g_last_save_us = now_us;
            }

            xSemaphoreGive(g_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(kTaskPeriodMs));
    }
}

} // namespace

extern "C" esp_err_t ecopower_daily_energy_init(void)
{
    if (g_initialized) {
        return ESP_OK;
    }

    g_mutex = xSemaphoreCreateMutex();
    if (g_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    load_state();

    std::time_t epoch = 0;
    std::tm local = {};
    if (get_local_time(&epoch, &local)) {
        const int32_t current_date = make_date_key(local);
        if (g_state.date_key != current_date) {
            reset_daily_locked(current_date);
        }
    }

    const BaseType_t created = xTaskCreate(
        task_main,
        "daily_energy",
        4096,
        nullptr,
        3,
        &g_task);

    if (created != pdPASS) {
        vSemaphoreDelete(g_mutex);
        g_mutex = nullptr;
        return ESP_ERR_NO_MEM;
    }

    g_initialized = true;
    return ESP_OK;
}

extern "C" bool ecopower_daily_energy_get(
    EcoPowerDailyEnergyData *out)
{
    if (!g_initialized ||
        g_mutex == nullptr ||
        out == nullptr) {
        return false;
    }

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    std::memset(out, 0, sizeof(*out));

    out->pv_production_kwh = g_state.pv_production_kwh;
    out->battery_charged_kwh = g_state.battery_charged_kwh;
    out->battery_discharged_kwh =
        g_state.battery_discharged_kwh;
    out->consumption_kwh = g_state.consumption_kwh;
    out->grid_import_kwh = g_state.grid_import_kwh;
    out->grid_export_kwh = g_state.grid_export_kwh;

    std::time_t epoch = 0;
    std::tm local = {};
    out->time_valid = get_local_time(&epoch, &local);

    out->soc_history_count = g_state.soc_count;

    const size_t oldest =
        g_state.soc_count < ECOPOWER_SOC_HISTORY_POINTS
            ? 0U
            : g_state.soc_write_index;

    for (size_t index = 0U;
         index < g_state.soc_count;
         ++index) {
        const size_t source =
            (oldest + index) %
            ECOPOWER_SOC_HISTORY_POINTS;

        out->soc_history[index] =
            g_state.soc_history[source];
    }

    xSemaphoreGive(g_mutex);
    return true;
}

extern "C" esp_err_t ecopower_daily_energy_reset_today(void)
{
    if (!g_initialized || g_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    std::time_t epoch = 0;
    std::tm local = {};
    const int32_t date_key =
        get_local_time(&epoch, &local)
            ? make_date_key(local)
            : 0;

    if (xSemaphoreTake(g_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    reset_daily_locked(date_key);
    const esp_err_t error = save_locked();
    xSemaphoreGive(g_mutex);
    return error;
}
