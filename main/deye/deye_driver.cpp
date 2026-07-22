#include "deye_driver.h"
#include "deye_config.h"
#include "drivers/rs485_port.h"

#include "esp_log.h"

static const char *TAG = "EcoPower_DEYE";

namespace {

bool g_initialized = false;
EcoPowerDeyeDiagnostics g_diagnostics = {};

} // namespace

extern "C" esp_err_t ecopower_deye_driver_init(void)
{
    if (g_initialized) {
        return ESP_OK;
    }

    EcoPowerRs485Config config = {};
    config.uart_port = ECOPOWER_RS485_UART_PORT;
    config.tx_gpio = ECOPOWER_RS485_TX_GPIO;
    config.rx_gpio = ECOPOWER_RS485_RX_GPIO;
    config.baud_rate = ECOPOWER_DEYE_BAUD_RATE;
    config.rx_buffer_size = 512U;
    config.tx_buffer_size = 256U;

    const esp_err_t error = ecopower_rs485_init(&config);

    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Deye RS485 initialization failed: %s",
            esp_err_to_name(error));
        return error;
    }

    g_initialized = true;

    ESP_LOGI(
        TAG,
        "Deye transport ready: slave=%u baud=%d",
        static_cast<unsigned>(ECOPOWER_DEYE_SLAVE_ADDRESS),
        ECOPOWER_DEYE_BAUD_RATE);

    return ESP_OK;
}

extern "C" bool ecopower_deye_driver_is_initialized(void)
{
    return g_initialized;
}

extern "C" EcoPowerModbusResult
ecopower_deye_read_holding_registers_for_slave(
    uint8_t slave_address,
    uint16_t start_address,
    uint16_t register_count,
    uint16_t *registers)
{
    EcoPowerModbusResult result = {};
    result.status = ECOPOWER_MODBUS_IO_ERROR;

    if (!g_initialized || slave_address == 0U) {
        return result;
    }

    result = ecopower_modbus_read_holding_registers(
        slave_address,
        start_address,
        register_count,
        registers,
        ECOPOWER_DEYE_TIMEOUT_MS);

    g_diagnostics.last_status = result.status;
    g_diagnostics.last_exception_code = result.exception_code;

    if (result.status == ECOPOWER_MODBUS_OK) {
        ++g_diagnostics.successful_requests;
    } else {
        ++g_diagnostics.failed_requests;
    }

    return result;
}

extern "C" EcoPowerModbusResult ecopower_deye_read_holding_registers(
    uint16_t start_address,
    uint16_t register_count,
    uint16_t *registers)
{
    return ecopower_deye_read_holding_registers_for_slave(
        ECOPOWER_DEYE_SLAVE_ADDRESS,
        start_address,
        register_count,
        registers);
}

extern "C" void ecopower_deye_get_diagnostics(
    EcoPowerDeyeDiagnostics *diagnostics)
{
    if (diagnostics != nullptr) {
        *diagnostics = g_diagnostics;
    }
}
