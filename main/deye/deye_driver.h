#pragma once

#include "esp_err.h"
#include "drivers/modbus_rtu_master.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t successful_requests;
    uint32_t failed_requests;
    EcoPowerModbusStatus last_status;
    uint8_t last_exception_code;
} EcoPowerDeyeDiagnostics;

esp_err_t ecopower_deye_driver_init(void);
bool ecopower_deye_driver_is_initialized(void);

/* Safe read-only diagnostic call. Function code 0x03. */
EcoPowerModbusResult ecopower_deye_read_holding_registers(
    uint16_t start_address,
    uint16_t register_count,
    uint16_t *registers);

/* Multi-inverter read. Uses the same RS485 bus with a selected slave ID. */
EcoPowerModbusResult ecopower_deye_read_holding_registers_for_slave(
    uint8_t slave_address,
    uint16_t start_address,
    uint16_t register_count,
    uint16_t *registers);

void ecopower_deye_get_diagnostics(
    EcoPowerDeyeDiagnostics *diagnostics);

#ifdef __cplusplus
}
#endif
