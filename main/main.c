#include "waveshare_rgb_lcd_port.h"
#include "ui/ui.h"
#include "ui/startup.h"
#include "drivers/sd_card.h"
#include "lvgl_port.h"
#include "app/app_version.h"
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

    if (lvgl_port_lock(-1)) {
        ecopower_ui_start();
        lvgl_port_unlock();
    }

    ESP_LOGI(MAIN_TAG, "%s %s UI started", ECOPOWER_APP_NAME, ECOPOWER_APP_VERSION);
}
