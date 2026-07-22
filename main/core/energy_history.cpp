#include "energy_history.h"

#include "daily_energy.h"
#include "energy_model.h"
#include "storage_manager.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

constexpr const char *kTag = "EcoPower_History";
constexpr const char *kCursorFile = "state/history_cursor.bin";
constexpr uint32_t kCursorVersion = 1U;
constexpr std::time_t kSamplePeriodSeconds = 5 * 60;
constexpr uint32_t kTaskPeriodMs = 1000U;
constexpr uint32_t kTaskStackSizeBytes = 8192U;

struct HistoryCursor {
    uint32_t version;
    int64_t last_bucket;
};

TaskHandle_t g_task = nullptr;
bool g_initialized = false;
HistoryCursor g_cursor = {kCursorVersion, -1};

bool get_local_time(std::time_t *epoch, std::tm *local)
{
    if (epoch == nullptr || local == nullptr) {
        return false;
    }

    std::time(epoch);
    localtime_r(epoch, local);
    return local->tm_year >= (2024 - 1900);
}

void load_cursor()
{
    HistoryCursor loaded = {};
    size_t size = 0U;

    const esp_err_t error = ecopower_storage_read(
        kCursorFile,
        &loaded,
        sizeof(loaded),
        &size);

    if (error == ESP_OK &&
        size == sizeof(loaded) &&
        loaded.version == kCursorVersion) {
        g_cursor = loaded;
    }
}

void save_cursor()
{
    g_cursor.version = kCursorVersion;
    const esp_err_t error = ecopower_storage_write_atomic(
        kCursorFile,
        &g_cursor,
        sizeof(g_cursor));

    if (error != ESP_OK) {
        ESP_LOGW(kTag, "Cannot save history cursor: %s",
                 esp_err_to_name(error));
    }
}

esp_err_t prepare_daily_file(
    const std::tm &local,
    char *relative_path,
    size_t relative_path_size)
{
    char year_dir[24] = {};
    char month_dir[32] = {};

    const int year = local.tm_year + 1900;
    const int month = local.tm_mon + 1;

    std::snprintf(year_dir, sizeof(year_dir),
                  "history/%04d", year);
    std::snprintf(month_dir, sizeof(month_dir),
                  "history/%04d/%02d", year, month);

    esp_err_t error = ecopower_storage_ensure_directory(year_dir);
    if (error != ESP_OK) {
        return error;
    }

    error = ecopower_storage_ensure_directory(month_dir);
    if (error != ESP_OK) {
        return error;
    }

    const int written = std::snprintf(
        relative_path,
        relative_path_size,
        "history/%04d/%02d/%04d-%02d-%02d.csv",
        year,
        month,
        year,
        month,
        local.tm_mday);

    if (written < 0 ||
        static_cast<size_t>(written) >= relative_path_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!ecopower_storage_exists(relative_path)) {
        static constexpr const char kHeader[] =
            "timestamp,pv_kw,load_kw,grid_kw,battery_kw,soc_pct,"
            "battery_v,battery_a,pv_today_kwh,load_today_kwh,"
            "grid_import_today_kwh,grid_export_today_kwh\n";

        error = ecopower_storage_append(
            relative_path,
            kHeader,
            sizeof(kHeader) - 1U);
    }

    return error;
}

esp_err_t append_sample(
    std::time_t epoch,
    const std::tm &local)
{
    EnergyData energy = {};
    ecopower_energy_model_get(&energy);

    if (!energy.deye_online ||
        !std::isfinite(energy.battery_soc_pct)) {
        return ESP_ERR_INVALID_STATE;
    }

    EcoPowerDailyEnergyData daily = {};
    if (!ecopower_daily_energy_get(&daily)) {
        return ESP_ERR_INVALID_STATE;
    }

    static char relative_path[96] = {};
    relative_path[0] = '\0';
    esp_err_t error = prepare_daily_file(
        local,
        relative_path,
        sizeof(relative_path));
    if (error != ESP_OK) {
        return error;
    }

    static char timestamp[24] = {};
    timestamp[0] = '\0';
    std::strftime(
        timestamp,
        sizeof(timestamp),
        "%Y-%m-%d %H:%M:%S",
        &local);

    static char row[320] = {};
    row[0] = '\0';
    const int written = std::snprintf(
        row,
        sizeof(row),
        "%s,%.3f,%.3f,%.3f,%.3f,%.1f,%.2f,%.2f,%.4f,%.4f,%.4f,%.4f\n",
        timestamp,
        energy.pv_power_kw,
        energy.house_load_kw,
        energy.grid_power_kw,
        energy.battery_power_kw,
        energy.battery_soc_pct,
        energy.battery_voltage_v,
        energy.battery_current_a,
        daily.pv_production_kwh,
        daily.consumption_kwh,
        daily.grid_import_kwh,
        daily.grid_export_kwh);

    if (written < 0 || static_cast<size_t>(written) >= sizeof(row)) {
        return ESP_ERR_INVALID_SIZE;
    }

    error = ecopower_storage_append(
        relative_path,
        row,
        static_cast<size_t>(written));

    if (error == ESP_OK) {
        ESP_LOGI(kTag, "History sample saved: %s", relative_path);
    }

    (void)epoch;
    return error;
}

void task_main(void *)
{
    ESP_LOGI(kTag, "Local history recorder started (5 minute interval)");
    load_cursor();

    while (true) {
        std::time_t epoch = 0;
        std::tm local = {};

        if (get_local_time(&epoch, &local)) {
            const int64_t bucket =
                static_cast<int64_t>(epoch / kSamplePeriodSeconds);

            if (bucket != g_cursor.last_bucket) {
                const esp_err_t error = append_sample(epoch, local);
                if (error == ESP_OK) {
                    g_cursor.last_bucket = bucket;
                    save_cursor();
                } else if (error != ESP_ERR_INVALID_STATE) {
                    ESP_LOGW(kTag, "History sample failed: %s",
                             esp_err_to_name(error));
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(kTaskPeriodMs));
    }
}

} // namespace

extern "C" esp_err_t ecopower_energy_history_init(void)
{
    if (g_initialized) {
        return ESP_OK;
    }

    if (!ecopower_storage_manager_is_ready()) {
        ESP_LOGW(kTag, "History disabled because local storage is not ready");
        return ESP_ERR_INVALID_STATE;
    }

    const BaseType_t created = xTaskCreate(
        task_main,
        "energy_history",
        kTaskStackSizeBytes,
        nullptr,
        3,
        &g_task);

    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    g_initialized = true;
    return ESP_OK;
}
