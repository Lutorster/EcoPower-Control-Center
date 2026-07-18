#include "sd_card.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/i2c.h"
#include "sdmmc_cmd.h"

static const char *TAG_SD = "EcoPower_SD";
static sdmmc_card_t *card = nullptr;
static sdmmc_host_t host = SDSPI_HOST_DEFAULT();
static bool mounted = false;

#define SD_MOUNT_POINT "/sdcard"
#define SD_PIN_MOSI GPIO_NUM_11
#define SD_PIN_MISO GPIO_NUM_13
#define SD_PIN_CLK  GPIO_NUM_12
#define SD_PIN_CS   GPIO_NUM_NC

bool ecopower_sd_prepare_access(void)
{
    uint8_t write_buf = 0x01;
    esp_err_t ret = i2c_master_write_to_device(
        I2C_NUM_0, 0x24, &write_buf, 1, 1000 / portTICK_PERIOD_MS);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SD, "CH422G mode write failed: %s", esp_err_to_name(ret));
        return false;
    }

    /* 0x2E preserves LCD/touch state and enables stable SD operation
     * on this Waveshare board. */
    write_buf = 0x2E;
    ret = i2c_master_write_to_device(
        I2C_NUM_0, 0x38, &write_buf, 1, 1000 / portTICK_PERIOD_MS);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SD, "CH422G output write failed: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

static void check_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG_SD, "Required file not found: %s", path);
        return;
    }

    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fclose(f);
    ESP_LOGI(TAG_SD, "File ready: %s (%ld bytes)", path, size);
}

bool ecopower_sd_init(void)
{
    if (mounted) return true;

    ESP_LOGI(TAG_SD, "Initializing SD card...");

    if (!ecopower_sd_prepare_access()) return false;

    host.slot = SPI2_HOST;
    host.max_freq_khz = 400;

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = SD_PIN_MOSI;
    bus_cfg.miso_io_num = SD_PIN_MISO;
    bus_cfg.sclk_io_num = SD_PIN_CLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4096;

    esp_err_t ret = spi_bus_initialize(
        static_cast<spi_host_device_t>(host.slot),
        &bus_cfg,
        SDSPI_DEFAULT_DMA);

    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG_SD, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return false;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_PIN_CS;
    slot_config.host_id = static_cast<spi_host_device_t>(host.slot);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 8;
    mount_config.allocation_unit_size = 16 * 1024;

    ret = esp_vfs_fat_sdspi_mount(
        SD_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &card);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SD, "SD mount failed: %s", esp_err_to_name(ret));
        return false;
    }

    mounted = true;
    sdmmc_card_print_info(stdout, card);

    check_file("/sdcard/assets/dashboard.png");

    return true;
}
