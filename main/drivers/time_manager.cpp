#include "time_manager.h"

#include "esp_log.h"
#include "esp_sntp.h"
#include <sys/time.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

static const char *TAG = "EcoPower_Time";

namespace {

bool g_initialized = false;
volatile bool g_synchronized = false;

constexpr const char *kTimezone =
    "EET-2EEST,M3.5.0/3,M10.5.0/4";

constexpr const char *kPrimaryServer =
    "pool.ntp.org";

constexpr const char *kSecondaryServer =
    "time.google.com";

bool system_time_is_valid()
{
    std::time_t now = 0;
    std::time(&now);

    std::tm local_time = {};
    localtime_r(&now, &local_time);

    return local_time.tm_year >= (2024 - 1900);
}

bool format_local_time(
    char *buffer,
    size_t buffer_size,
    const char *format)
{
    if (buffer == nullptr ||
        buffer_size == 0U ||
        format == nullptr) {
        return false;
    }

    buffer[0] = '\0';

    std::time_t now = 0;
    std::time(&now);

    std::tm local_time = {};
    localtime_r(&now, &local_time);

    if (local_time.tm_year < (2024 - 1900)) {
        return false;
    }

    return std::strftime(
               buffer,
               buffer_size,
               format,
               &local_time) > 0U;
}

void time_sync_notification(struct timeval *)
{
    g_synchronized = true;

    char datetime[32] = {};
    if (format_local_time(
            datetime,
            sizeof(datetime),
            "%d.%m.%Y %H:%M:%S")) {
        ESP_LOGI(TAG, "Time synchronized: %s", datetime);
    } else {
        ESP_LOGI(TAG, "Time synchronized");
    }
}

void start_sntp()
{
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);

    esp_sntp_setservername(
        0,
        const_cast<char *>(kPrimaryServer));

    esp_sntp_setservername(
        1,
        const_cast<char *>(kSecondaryServer));

    esp_sntp_set_time_sync_notification_cb(
        time_sync_notification);

    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    esp_sntp_init();
}

} // namespace

extern "C" esp_err_t ecopower_time_manager_init(void)
{
    if (g_initialized) {
        return ESP_OK;
    }

    setenv("TZ", kTimezone, 1);
    tzset();

    start_sntp();

    g_synchronized = system_time_is_valid();
    g_initialized = true;

    ESP_LOGI(TAG, "SNTP started; timezone=Europe/Kyiv");
    return ESP_OK;
}

extern "C" esp_err_t ecopower_time_manager_resynchronize(void)
{
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    g_synchronized = false;
    start_sntp();

    ESP_LOGI(TAG, "Manual SNTP synchronization requested");
    return ESP_OK;
}

extern "C" bool ecopower_time_manager_is_initialized(void)
{
    return g_initialized;
}

extern "C" bool ecopower_time_manager_is_synchronized(void)
{
    if (!g_initialized) {
        return false;
    }

    if (!g_synchronized && system_time_is_valid()) {
        g_synchronized = true;
    }

    return g_synchronized;
}

extern "C" bool ecopower_time_manager_get_time(
    char *buffer,
    size_t buffer_size)
{
    return format_local_time(
        buffer,
        buffer_size,
        "%H:%M");
}

extern "C" bool ecopower_time_manager_get_date(
    char *buffer,
    size_t buffer_size)
{
    return format_local_time(
        buffer,
        buffer_size,
        "%d.%m.%Y");
}

extern "C" bool ecopower_time_manager_get_datetime(
    char *buffer,
    size_t buffer_size)
{
    return format_local_time(
        buffer,
        buffer_size,
        "%d.%m.%Y %H:%M:%S");
}
