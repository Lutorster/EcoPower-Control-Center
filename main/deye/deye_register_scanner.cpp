#include "deye_register_scanner.h"

#include "deye_driver.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>

static const char *TAG = "EcoPower_DEYE_SCAN";

namespace {

TaskHandle_t g_task = nullptr;
volatile bool g_stop_requested = false;
EcoPowerDeyeScannerConfig g_config = {};

const char *status_text(EcoPowerModbusStatus status)
{
    switch (status) {
        case ECOPOWER_MODBUS_OK:
            return "OK";
        case ECOPOWER_MODBUS_TIMEOUT:
            return "TIMEOUT";
        case ECOPOWER_MODBUS_BAD_CRC:
            return "BAD_CRC";
        case ECOPOWER_MODBUS_BAD_RESPONSE:
            return "BAD_RESPONSE";
        case ECOPOWER_MODBUS_EXCEPTION:
            return "EXCEPTION";
        case ECOPOWER_MODBUS_IO_ERROR:
        default:
            return "IO_ERROR";
    }
}

void scanner_task(void *)
{
    ESP_LOGI(
        TAG,
        "Read-only holding-register scan started: %u..%u, block=%u",
        static_cast<unsigned>(g_config.start_address),
        static_cast<unsigned>(g_config.end_address),
        static_cast<unsigned>(g_config.block_size));

    uint8_t consecutive_failures = 0U;
    uint32_t successful_blocks = 0U;
    uint32_t nonzero_registers = 0U;
    uint16_t address = g_config.start_address;

    while (!g_stop_requested && address <= g_config.end_address) {
        const uint16_t remaining =
            static_cast<uint16_t>(
                g_config.end_address - address + 1U);

        const uint16_t count =
            std::min(g_config.block_size, remaining);

        uint16_t registers[32] = {};

        const EcoPowerModbusResult result =
            ecopower_deye_read_holding_registers(
                address,
                count,
                registers);

        if (result.status == ECOPOWER_MODBUS_OK) {
            ++successful_blocks;
            consecutive_failures = 0U;

            ESP_LOGI(
                TAG,
                "Block %u..%u OK (%u bytes)",
                static_cast<unsigned>(address),
                static_cast<unsigned>(address + count - 1U),
                static_cast<unsigned>(result.received_bytes));

            for (uint16_t index = 0U; index < count; ++index) {
                if (registers[index] == 0U) {
                    continue;
                }

                ++nonzero_registers;

                ESP_LOGI(
                    TAG,
                    "REG %5u (0x%04X) = %5u (0x%04X)",
                    static_cast<unsigned>(address + index),
                    static_cast<unsigned>(address + index),
                    static_cast<unsigned>(registers[index]),
                    static_cast<unsigned>(registers[index]));
            }
        } else {
            ++consecutive_failures;

            ESP_LOGW(
                TAG,
                "Block %u..%u failed: %s, exception=%u, received=%u",
                static_cast<unsigned>(address),
                static_cast<unsigned>(address + count - 1U),
                status_text(result.status),
                static_cast<unsigned>(result.exception_code),
                static_cast<unsigned>(result.received_bytes));

            if (consecutive_failures >=
                g_config.max_consecutive_failures) {
                ESP_LOGE(
                    TAG,
                    "Scan stopped after %u consecutive failures. "
                    "Check RS485 wiring, slave address and baud rate.",
                    static_cast<unsigned>(consecutive_failures));
                break;
            }
        }

        address = static_cast<uint16_t>(address + count);
        vTaskDelay(pdMS_TO_TICKS(g_config.pause_ms));
    }

    ESP_LOGI(
        TAG,
        "Scan finished: successful_blocks=%u, nonzero_registers=%u",
        static_cast<unsigned>(successful_blocks),
        static_cast<unsigned>(nonzero_registers));

    g_task = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

extern "C" esp_err_t ecopower_deye_scanner_start(
    const EcoPowerDeyeScannerConfig *config)
{
    if (config == nullptr ||
        config->block_size == 0U ||
        config->block_size > 32U ||
        config->start_address > config->end_address ||
        config->pause_ms < 50U ||
        config->max_consecutive_failures == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!ecopower_deye_driver_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_task != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    g_config = *config;
    g_stop_requested = false;

    const BaseType_t created = xTaskCreate(
        scanner_task,
        "deye_reg_scan",
        4096,
        nullptr,
        4,
        &g_task);

    if (created != pdPASS) {
        g_task = nullptr;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

extern "C" bool ecopower_deye_scanner_is_running(void)
{
    return g_task != nullptr;
}

extern "C" void ecopower_deye_scanner_stop(void)
{
    g_stop_requested = true;
}
