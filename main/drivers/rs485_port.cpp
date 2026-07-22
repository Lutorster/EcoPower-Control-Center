#include "rs485_port.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <algorithm>

static const char *TAG = "EcoPower_RS485";

namespace {

bool g_initialized = false;
uart_port_t g_uart_port = UART_NUM_1;
SemaphoreHandle_t g_bus_mutex = nullptr;

} // namespace

extern "C" esp_err_t ecopower_rs485_init(
    const EcoPowerRs485Config *config)
{
    if (config == nullptr ||
        config->tx_gpio < 0 ||
        config->rx_gpio < 0 ||
        config->baud_rate <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_initialized) {
        return ESP_OK;
    }

    if (config->uart_port < UART_NUM_0 ||
        config->uart_port >= UART_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    g_bus_mutex = xSemaphoreCreateMutex();
    if (g_bus_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    g_uart_port = static_cast<uart_port_t>(config->uart_port);

    uart_config_t uart_config = {};
    uart_config.baud_rate = config->baud_rate;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    esp_err_t error = uart_param_config(g_uart_port, &uart_config);
    if (error != ESP_OK) {
        return error;
    }

    /*
     * Waveshare ESP32-S3-Touch-LCD-7 has an onboard RS485
     * transceiver with automatic transmit/receive direction control.
     * Therefore RTS/DE is not used here.
     */
    error = uart_set_pin(
        g_uart_port,
        config->tx_gpio,
        config->rx_gpio,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE);

    if (error != ESP_OK) {
        return error;
    }

    const int rx_buffer_size = static_cast<int>(
        std::max<size_t>(config->rx_buffer_size, 256U));
    const int tx_buffer_size = static_cast<int>(
        std::max<size_t>(config->tx_buffer_size, 256U));

    error = uart_driver_install(
        g_uart_port,
        rx_buffer_size,
        tx_buffer_size,
        0,
        nullptr,
        0);

    if (error != ESP_OK) {
        return error;
    }

    error = uart_flush_input(g_uart_port);
    if (error != ESP_OK) {
        return error;
    }

    g_initialized = true;

    ESP_LOGI(
        TAG,
        "RS485 initialized: UART%d TX=%d RX=%d baud=%d 8N1",
        config->uart_port,
        config->tx_gpio,
        config->rx_gpio,
        config->baud_rate);

    return ESP_OK;
}

extern "C" bool ecopower_rs485_is_initialized(void)
{
    return g_initialized;
}

extern "C" esp_err_t ecopower_rs485_transceive(
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity,
    size_t expected_response_size,
    uint32_t response_timeout_ms,
    size_t *received_size)
{
    if (!g_initialized || g_bus_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (request == nullptr ||
        request_size == 0U ||
        response == nullptr ||
        response_capacity == 0U ||
        expected_response_size == 0U ||
        expected_response_size > response_capacity) {
        return ESP_ERR_INVALID_ARG;
    }

    if (received_size != nullptr) {
        *received_size = 0U;
    }

    if (xSemaphoreTake(
            g_bus_mutex,
            pdMS_TO_TICKS(response_timeout_ms + 100U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = uart_flush_input(g_uart_port);

    if (result == ESP_OK) {
        const int written = uart_write_bytes(
            g_uart_port,
            request,
            request_size);

        if (written != static_cast<int>(request_size)) {
            result = ESP_FAIL;
        }
    }

    if (result == ESP_OK) {
        result = uart_wait_tx_done(
            g_uart_port,
            pdMS_TO_TICKS(100));
    }

    size_t total_received = 0U;
    const TickType_t start_tick = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(response_timeout_ms);

    while (result == ESP_OK && total_received < expected_response_size) {
        const TickType_t elapsed = xTaskGetTickCount() - start_tick;

        if (elapsed >= timeout_ticks) {
            result = ESP_ERR_TIMEOUT;
            break;
        }

        const TickType_t remaining = timeout_ticks - elapsed;

        const int bytes_read = uart_read_bytes(
            g_uart_port,
            response + total_received,
            expected_response_size - total_received,
            remaining);

        if (bytes_read < 0) {
            result = ESP_FAIL;
            break;
        }

        if (bytes_read == 0) {
            result = ESP_ERR_TIMEOUT;
            break;
        }

        total_received += static_cast<size_t>(bytes_read);
    }

    if (received_size != nullptr) {
        *received_size = total_received;
    }

    xSemaphoreGive(g_bus_mutex);
    return result;
}
