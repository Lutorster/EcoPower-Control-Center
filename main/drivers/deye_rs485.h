#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool initialized;
    bool task_running;
    bool online;
    uint8_t slave_address;
    uint32_t requests_sent;
    uint32_t valid_responses;
    uint32_t timeouts;
    uint32_t crc_errors;
    uint32_t protocol_errors;
    uint8_t last_exception;
    int64_t last_response_time_us;
} DeyeRs485Status;

typedef struct {
    uint32_t sequence;
    char tx_hex[192];
    char rx_hex[384];
    char decoded[512];
} DeyeDiagnosticSnapshot;

esp_err_t ecopower_deye_rs485_init(void);
esp_err_t ecopower_deye_rs485_start(void);
void ecopower_deye_rs485_stop(void);
void ecopower_deye_rs485_get_status(DeyeRs485Status *out_status);
void ecopower_deye_get_diagnostic(DeyeDiagnosticSnapshot *out_snapshot);

esp_err_t ecopower_deye_read_registers(uint8_t function_code,
                                       uint16_t start_register,
                                       uint16_t register_count,
                                       uint16_t *out_registers,
                                       size_t out_capacity,
                                       uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
