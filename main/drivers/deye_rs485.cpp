#include "deye_rs485.h"

#include <cstring>
#include <cstdio>
#include <inttypes.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace {

constexpr uart_port_t kUart = UART_NUM_1;
constexpr gpio_num_t kTxPin = GPIO_NUM_15;
constexpr gpio_num_t kRxPin = GPIO_NUM_16;
constexpr size_t kRxBufferSize = 512;
constexpr size_t kMaxFrameSize = 256;
constexpr TickType_t kInterByteGap = pdMS_TO_TICKS(20);

#ifndef CONFIG_ECOPOWER_DEYE_BAUD_RATE
#define CONFIG_ECOPOWER_DEYE_BAUD_RATE 9600
#endif
#ifndef CONFIG_ECOPOWER_DEYE_SLAVE_ADDRESS
#define CONFIG_ECOPOWER_DEYE_SLAVE_ADDRESS 1
#endif
#ifndef CONFIG_ECOPOWER_DEYE_POLL_INTERVAL_MS
#define CONFIG_ECOPOWER_DEYE_POLL_INTERVAL_MS 2000
#endif
#ifndef CONFIG_ECOPOWER_DEYE_PROBE_FUNCTION
#define CONFIG_ECOPOWER_DEYE_PROBE_FUNCTION 3
#endif
#ifndef CONFIG_ECOPOWER_DEYE_PROBE_REGISTER
#define CONFIG_ECOPOWER_DEYE_PROBE_REGISTER 0
#endif
#ifndef CONFIG_ECOPOWER_DEYE_PROBE_COUNT
#define CONFIG_ECOPOWER_DEYE_PROBE_COUNT 1
#endif

const char *TAG = "EcoPower_Deye";
TaskHandle_t g_task = nullptr;
portMUX_TYPE g_status_lock = portMUX_INITIALIZER_UNLOCKED;
DeyeRs485Status g_status{};
DeyeDiagnosticSnapshot g_diag{};
portMUX_TYPE g_diag_lock = portMUX_INITIALIZER_UNLOCKED;
volatile bool g_stop_requested = false;

uint16_t modbus_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x0001U) ? static_cast<uint16_t>((crc >> 1U) ^ 0xA001U)
                                  : static_cast<uint16_t>(crc >> 1U);
        }
    }
    return crc;
}

template <typename Fn>
void status_update(Fn &&fn)
{
    portENTER_CRITICAL(&g_status_lock);
    fn(g_status);
    portEXIT_CRITICAL(&g_status_lock);
}


void bytes_to_hex(const uint8_t *data, size_t length, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    out[0] = '\0';
    size_t offset = 0;
    for (size_t i = 0; i < length && offset + 4 < out_size; ++i) {
        const int written = snprintf(out + offset, out_size - offset, "%02X%s",
                                     data[i], (i + 1U < length) ? " " : "");
        if (written <= 0) break;
        offset += static_cast<size_t>(written);
    }
}

void diagnostic_update(const uint8_t *tx, size_t tx_len,
                       const uint8_t *rx, size_t rx_len,
                       const char *decoded)
{
    DeyeDiagnosticSnapshot next = {};
    bytes_to_hex(tx, tx_len, next.tx_hex, sizeof(next.tx_hex));
    if (rx && rx_len) bytes_to_hex(rx, rx_len, next.rx_hex, sizeof(next.rx_hex));
    else snprintf(next.rx_hex, sizeof(next.rx_hex), "(no response)");
    snprintf(next.decoded, sizeof(next.decoded), "%s", decoded ? decoded : "");
    portENTER_CRITICAL(&g_diag_lock);
    next.sequence = g_diag.sequence + 1U;
    g_diag = next;
    portEXIT_CRITICAL(&g_diag_lock);
}

void log_frame(const char *prefix, const uint8_t *data, size_t length)
{
    char line[kMaxFrameSize * 3 + 1] = {};
    size_t offset = 0;
    for (size_t i = 0; i < length && offset + 4 < sizeof(line); ++i) {
        const int written = snprintf(line + offset, sizeof(line) - offset, "%02X%s",
                                     data[i], (i + 1U < length) ? " " : "");
        if (written <= 0) break;
        offset += static_cast<size_t>(written);
    }
    ESP_LOGI(TAG, "%s [%u]: %s", prefix, static_cast<unsigned>(length), line);
}

int read_response(uint8_t *buffer, size_t capacity, uint32_t timeout_ms)
{
    const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000LL;
    size_t total = 0;
    bool received_any = false;

    while (esp_timer_get_time() < deadline && total < capacity) {
        const TickType_t wait_ticks = received_any ? kInterByteGap : pdMS_TO_TICKS(50);
        const int read = uart_read_bytes(kUart, buffer + total, capacity - total, wait_ticks);
        if (read > 0) {
            total += static_cast<size_t>(read);
            received_any = true;
            continue;
        }
        if (received_any) break;
    }
    return static_cast<int>(total);
}

esp_err_t transact(uint8_t function_code,
                   uint16_t start_register,
                   uint16_t register_count,
                   uint16_t *out_registers,
                   size_t out_capacity,
                   uint32_t timeout_ms)
{
    if (!g_status.initialized) return ESP_ERR_INVALID_STATE;
    if (function_code != 0x03 && function_code != 0x04) return ESP_ERR_INVALID_ARG;
    if (register_count == 0 || register_count > 125 || out_capacity < register_count) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t request[8] = {
        static_cast<uint8_t>(CONFIG_ECOPOWER_DEYE_SLAVE_ADDRESS),
        function_code,
        static_cast<uint8_t>(start_register >> 8U),
        static_cast<uint8_t>(start_register & 0xFFU),
        static_cast<uint8_t>(register_count >> 8U),
        static_cast<uint8_t>(register_count & 0xFFU),
        0,
        0,
    };
    const uint16_t request_crc = modbus_crc16(request, 6);
    request[6] = static_cast<uint8_t>(request_crc & 0xFFU);
    request[7] = static_cast<uint8_t>(request_crc >> 8U);

    uart_flush_input(kUart);
    const int written = uart_write_bytes(kUart, request, sizeof(request));
    if (written != static_cast<int>(sizeof(request))) {
        ESP_LOGE(TAG, "UART write failed: %d/%u", written, static_cast<unsigned>(sizeof(request)));
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(uart_wait_tx_done(kUart, pdMS_TO_TICKS(100)));
    status_update([](DeyeRs485Status &s) { ++s.requests_sent; });
    log_frame("TX", request, sizeof(request));
    char decoded_text[512] = {};
    snprintf(decoded_text, sizeof(decoded_text),
             "REQUEST\nSlave: %u\nFunction: 0x%02X\nStart register: %u (0x%04X)\nCount: %u\nCRC: 0x%04X",
             CONFIG_ECOPOWER_DEYE_SLAVE_ADDRESS, function_code,
             start_register, start_register, register_count, request_crc);
    diagnostic_update(request, sizeof(request), nullptr, 0, decoded_text);

    uint8_t response[kMaxFrameSize] = {};
    const int response_length = read_response(response, sizeof(response), timeout_ms);
    if (response_length <= 0) {
        status_update([](DeyeRs485Status &s) {
            ++s.timeouts;
            s.online = false;
        });
        snprintf(decoded_text, sizeof(decoded_text),
                 "TIMEOUT\nSlave: %u\nFunction: 0x%02X\nRegister: %u (0x%04X)\nTimeout: %" PRIu32 " ms\nRequests: %" PRIu32 "\nTimeouts: %" PRIu32,
                 CONFIG_ECOPOWER_DEYE_SLAVE_ADDRESS, function_code,
                 start_register, start_register, timeout_ms,
                 g_status.requests_sent, g_status.timeouts);
        diagnostic_update(request, sizeof(request), nullptr, 0, decoded_text);
        ESP_LOGW(TAG, "No Modbus response (slave=%u, function=0x%02X, register=%u)",
                 CONFIG_ECOPOWER_DEYE_SLAVE_ADDRESS, function_code, start_register);
        return ESP_ERR_TIMEOUT;
    }

    const size_t length = static_cast<size_t>(response_length);
    log_frame("RX", response, length);
    snprintf(decoded_text, sizeof(decoded_text),
             "RESPONSE\nLength: %u bytes\nSlave: %u\nFunction: 0x%02X",
             static_cast<unsigned>(length), response[0], response[1]);
    diagnostic_update(request, sizeof(request), response, length, decoded_text);
    if (length < 5U) {
        status_update([](DeyeRs485Status &s) { ++s.protocol_errors; });
        return ESP_ERR_INVALID_RESPONSE;
    }

    const uint16_t received_crc = static_cast<uint16_t>(response[length - 2U]) |
                                  static_cast<uint16_t>(response[length - 1U] << 8U);
    const uint16_t calculated_crc = modbus_crc16(response, length - 2U);
    if (received_crc != calculated_crc) {
        status_update([](DeyeRs485Status &s) { ++s.crc_errors; });
        ESP_LOGE(TAG, "CRC mismatch: received=0x%04X calculated=0x%04X",
                 received_crc, calculated_crc);
        return ESP_ERR_INVALID_CRC;
    }

    if (response[0] != CONFIG_ECOPOWER_DEYE_SLAVE_ADDRESS) {
        status_update([](DeyeRs485Status &s) { ++s.protocol_errors; });
        ESP_LOGE(TAG, "Unexpected slave address: %u", response[0]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if ((response[1] & 0x80U) != 0U) {
        const uint8_t exception = response[2];
        status_update([exception](DeyeRs485Status &s) {
            ++s.protocol_errors;
            s.last_exception = exception;
            s.online = true;
            s.last_response_time_us = esp_timer_get_time();
        });
        ESP_LOGW(TAG, "Modbus exception 0x%02X", exception);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (response[1] != function_code) {
        status_update([](DeyeRs485Status &s) { ++s.protocol_errors; });
        ESP_LOGE(TAG, "Unexpected function code: 0x%02X", response[1]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const uint8_t byte_count = response[2];
    const size_t expected_length = static_cast<size_t>(byte_count) + 5U;
    if (byte_count != register_count * 2U || length != expected_length) {
        status_update([](DeyeRs485Status &s) { ++s.protocol_errors; });
        ESP_LOGE(TAG, "Unexpected response size: bytes=%u frame=%u expected=%u",
                 byte_count, static_cast<unsigned>(length), static_cast<unsigned>(expected_length));
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < register_count; ++i) {
        out_registers[i] = static_cast<uint16_t>(response[3U + i * 2U] << 8U) |
                           static_cast<uint16_t>(response[4U + i * 2U]);
    }

    size_t text_offset = static_cast<size_t>(snprintf(
        decoded_text, sizeof(decoded_text),
        "VALID MODBUS RESPONSE\nSlave: %u\nFunction: 0x%02X\nByte count: %u\n",
        response[0], response[1], byte_count));
    for (size_t i = 0; i < register_count && text_offset + 40U < sizeof(decoded_text); ++i) {
        const int written_text = snprintf(decoded_text + text_offset,
                                          sizeof(decoded_text) - text_offset,
                                          "R%u = 0x%04X = %u\n",
                                          start_register + static_cast<unsigned>(i),
                                          out_registers[i], out_registers[i]);
        if (written_text <= 0) break;
        text_offset += static_cast<size_t>(written_text);
    }
    diagnostic_update(request, sizeof(request), response, length, decoded_text);
    status_update([](DeyeRs485Status &s) {
        ++s.valid_responses;
        s.online = true;
        s.last_exception = 0;
        s.last_response_time_us = esp_timer_get_time();
    });
    return ESP_OK;
}

void diagnostic_task(void *)
{
    status_update([](DeyeRs485Status &s) { s.task_running = true; });
    ESP_LOGI(TAG,
             "RS485 diagnostic started: UART1 TX=GPIO15 RX=GPIO16, %d 8N1, slave=%d",
             CONFIG_ECOPOWER_DEYE_BAUD_RATE, CONFIG_ECOPOWER_DEYE_SLAVE_ADDRESS);
    ESP_LOGI(TAG, "Automatic TX/RX direction is handled by the Waveshare board");

    while (!g_stop_requested) {
        uint16_t values[CONFIG_ECOPOWER_DEYE_PROBE_COUNT] = {};
        const esp_err_t result = transact(CONFIG_ECOPOWER_DEYE_PROBE_FUNCTION,
                                          CONFIG_ECOPOWER_DEYE_PROBE_REGISTER,
                                          CONFIG_ECOPOWER_DEYE_PROBE_COUNT,
                                          values,
                                          CONFIG_ECOPOWER_DEYE_PROBE_COUNT,
                                          800);
        if (result == ESP_OK) {
            for (size_t i = 0; i < CONFIG_ECOPOWER_DEYE_PROBE_COUNT; ++i) {
                ESP_LOGI(TAG, "Probe register %u = 0x%04X (%u)",
                         CONFIG_ECOPOWER_DEYE_PROBE_REGISTER + static_cast<unsigned>(i),
                         values[i], values[i]);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_ECOPOWER_DEYE_POLL_INTERVAL_MS));
    }

    status_update([](DeyeRs485Status &s) { s.task_running = false; });
    g_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

extern "C" esp_err_t ecopower_deye_rs485_init(void)
{
    if (g_status.initialized) return ESP_OK;

    uart_config_t config = {};
    config.baud_rate = CONFIG_ECOPOWER_DEYE_BAUD_RATE;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.rx_flow_ctrl_thresh = 0;
    config.source_clk = UART_SCLK_DEFAULT;

    ESP_RETURN_ON_ERROR(uart_driver_install(kUart, kRxBufferSize, 0, 0, nullptr, 0), TAG,
                        "uart_driver_install failed");
    ESP_RETURN_ON_ERROR(uart_param_config(kUart, &config), TAG, "uart_param_config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(kUart, kTxPin, kRxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        TAG, "uart_set_pin failed");
    ESP_RETURN_ON_ERROR(uart_set_mode(kUart, UART_MODE_UART), TAG, "uart_set_mode failed");

    std::memset(&g_status, 0, sizeof(g_status));
    g_status.initialized = true;
    g_status.slave_address = CONFIG_ECOPOWER_DEYE_SLAVE_ADDRESS;
    ESP_LOGI(TAG, "RS485 UART initialized successfully");
    return ESP_OK;
}

extern "C" esp_err_t ecopower_deye_rs485_start(void)
{
    if (!g_status.initialized) return ESP_ERR_INVALID_STATE;
    if (g_task != nullptr) return ESP_OK;

    g_stop_requested = false;
    const BaseType_t created = xTaskCreatePinnedToCore(diagnostic_task,
                                                       "deye_rs485",
                                                       4096,
                                                       nullptr,
                                                       5,
                                                       &g_task,
                                                       0);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

extern "C" void ecopower_deye_rs485_stop(void)
{
    g_stop_requested = true;
}

extern "C" void ecopower_deye_rs485_get_status(DeyeRs485Status *out_status)
{
    if (out_status == nullptr) return;
    portENTER_CRITICAL(&g_status_lock);
    *out_status = g_status;
    portEXIT_CRITICAL(&g_status_lock);
}

extern "C" esp_err_t ecopower_deye_read_registers(uint8_t function_code,
                                                    uint16_t start_register,
                                                    uint16_t register_count,
                                                    uint16_t *out_registers,
                                                    size_t out_capacity,
                                                    uint32_t timeout_ms)
{
    if (out_registers == nullptr) return ESP_ERR_INVALID_ARG;
    return transact(function_code, start_register, register_count,
                    out_registers, out_capacity, timeout_ms);
}


extern "C" void ecopower_deye_get_diagnostic(DeyeDiagnosticSnapshot *out_snapshot)
{
    if (out_snapshot == nullptr) return;
    portENTER_CRITICAL(&g_diag_lock);
    *out_snapshot = g_diag;
    portEXIT_CRITICAL(&g_diag_lock);
}
