#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ECOPOWER_MODBUS_OK = 0,
    ECOPOWER_MODBUS_TIMEOUT,
    ECOPOWER_MODBUS_BAD_CRC,
    ECOPOWER_MODBUS_BAD_RESPONSE,
    ECOPOWER_MODBUS_EXCEPTION,
    ECOPOWER_MODBUS_IO_ERROR
} EcoPowerModbusStatus;

typedef struct {
    EcoPowerModbusStatus status;
    uint8_t exception_code;
    size_t received_bytes;
} EcoPowerModbusResult;

uint16_t ecopower_modbus_crc16(
    const uint8_t *data,
    size_t size);

EcoPowerModbusResult ecopower_modbus_read_holding_registers(
    uint8_t slave_address,
    uint16_t start_address,
    uint16_t register_count,
    uint16_t *registers,
    uint32_t timeout_ms);

EcoPowerModbusResult ecopower_modbus_read_input_registers(
    uint8_t slave_address,
    uint16_t start_address,
    uint16_t register_count,
    uint16_t *registers,
    uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
