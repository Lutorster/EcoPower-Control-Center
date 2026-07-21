#include "waveshare_rgb_lcd_port.h"
#include "ui/ui.h"
#include "ui/startup.h"
#include "drivers/sd_card.h"
#include "lvgl_port.h"
#include "app/app_version.h"
#include "drivers/wifi_manager.h"
#include "drivers/time_manager.h"
#include "drivers/mqtt_manager.h"
#include "deye/deye_driver.h"
#include "esp_log.h"

static const char *MAIN_TAG = "EcoPower";

void app_main(void)
{
    ESP_LOGI(MAIN_TAG, "%s %s starting", ECOPOWER_APP_NAME, ECOPOWER_APP_VERSION);
    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init());

    if (lvgl_port_lock(-1)) {
        ecopower_boot_show();
        lvgl_port_unlock();
    }

    const bool sd_ok = ecopower_sd_init();
    ESP_LOGI(MAIN_TAG, "SD initialization: %s", sd_ok ? "OK" : "FAILED");

    const esp_err_t wifi_init = ecopower_wifi_manager_init();
    if (wifi_init != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "Wi-Fi manager init failed: %s",
                 esp_err_to_name(wifi_init));
    }

    const esp_err_t time_init = ecopower_time_manager_init();
    if (time_init != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "Time manager init failed: %s",
                 esp_err_to_name(time_init));
    }

    const esp_err_t mqtt_init = ecopower_mqtt_manager_init();
    if (mqtt_init != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "MQTT manager init failed: %s",
                 esp_err_to_name(mqtt_init));
    }

    ESP_ERROR_CHECK(ecopower_deye_driver_init());

    if (lvgl_port_lock(-1)) {
        ecopower_ui_start();
        lvgl_port_unlock();
    }

    ESP_LOGI(MAIN_TAG, "%s %s UI started", ECOPOWER_APP_NAME, ECOPOWER_APP_VERSION);

}
