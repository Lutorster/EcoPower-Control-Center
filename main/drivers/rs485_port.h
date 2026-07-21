#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int uart_port;
    int tx_gpio;
    int rx_gpio;
    int baud_rate;
    size_t rx_buffer_size;
    size_t tx_buffer_size;
} EcoPowerRs485Config;

esp_err_t ecopower_rs485_init(const EcoPowerRs485Config *config);
bool ecopower_rs485_is_initialized(void);

esp_err_t ecopower_rs485_transceive(
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity,
    size_t expected_response_size,
    uint32_t response_timeout_ms,
    size_t *received_size);

#ifdef __cplusplus
}
#endif
