#include "modbus_rtu_master.h"
#include "rs485_port.h"

namespace {

EcoPowerModbusResult read_registers(
    uint8_t function_code,
    uint8_t slave_address,
    uint16_t start_address,
    uint16_t register_count,
    uint16_t *registers,
    uint32_t timeout_ms)
{
    EcoPowerModbusResult result = {};
    result.status = ECOPOWER_MODBUS_BAD_RESPONSE;

    if (slave_address == 0U ||
        register_count == 0U ||
        register_count > 125U ||
        registers == nullptr) {
        return result;
    }

    uint8_t request[8] = {};
    request[0] = slave_address;
    request[1] = function_code;
    request[2] = static_cast<uint8_t>(start_address >> 8U);
    request[3] = static_cast<uint8_t>(start_address & 0xFFU);
    request[4] = static_cast<uint8_t>(register_count >> 8U);
    request[5] = static_cast<uint8_t>(register_count & 0xFFU);

    const uint16_t request_crc = ecopower_modbus_crc16(request, 6U);
    request[6] = static_cast<uint8_t>(request_crc & 0xFFU);
    request[7] = static_cast<uint8_t>(request_crc >> 8U);

    const size_t normal_response_size =
        5U + static_cast<size_t>(register_count) * 2U;

    uint8_t response[256] = {};
    size_t received_size = 0U;

    const esp_err_t io_error = ecopower_rs485_transceive(
        request,
        sizeof(request),
        response,
        sizeof(response),
        normal_response_size,
        timeout_ms,
        &received_size);

    result.received_bytes = received_size;

    if (io_error == ESP_ERR_TIMEOUT) {
        result.status = ECOPOWER_MODBUS_TIMEOUT;
        return result;
    }

    if (io_error != ESP_OK) {
        result.status = ECOPOWER_MODBUS_IO_ERROR;
        return result;
    }

    if (received_size < 5U || response[0] != slave_address) {
        result.status = ECOPOWER_MODBUS_BAD_RESPONSE;
        return result;
    }

    const uint16_t received_crc =
        static_cast<uint16_t>(response[received_size - 2U]) |
        static_cast<uint16_t>(response[received_size - 1U] << 8U);

    const uint16_t calculated_crc = ecopower_modbus_crc16(
        response,
        received_size - 2U);

    if (received_crc != calculated_crc) {
        result.status = ECOPOWER_MODBUS_BAD_CRC;
        return result;
    }

    if (response[1] == static_cast<uint8_t>(function_code | 0x80U)) {
        result.status = ECOPOWER_MODBUS_EXCEPTION;
        result.exception_code = response[2];
        return result;
    }

    const uint8_t expected_byte_count =
        static_cast<uint8_t>(register_count * 2U);

    if (response[1] != function_code ||
        response[2] != expected_byte_count ||
        received_size != normal_response_size) {
        result.status = ECOPOWER_MODBUS_BAD_RESPONSE;
        return result;
    }

    for (uint16_t index = 0; index < register_count; ++index) {
        const size_t offset = 3U + static_cast<size_t>(index) * 2U;
        registers[index] =
            static_cast<uint16_t>(response[offset] << 8U) |
            static_cast<uint16_t>(response[offset + 1U]);
    }

    result.status = ECOPOWER_MODBUS_OK;
    return result;
}

} // namespace

extern "C" uint16_t ecopower_modbus_crc16(
    const uint8_t *data,
    size_t size)
{
    if (data == nullptr) {
        return 0U;
    }

    uint16_t crc = 0xFFFFU;

    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];

        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001U) != 0U) {
                crc = static_cast<uint16_t>((crc >> 1U) ^ 0xA001U);
            } else {
                crc = static_cast<uint16_t>(crc >> 1U);
            }
        }
    }

    return crc;
}

extern "C" EcoPowerModbusResult ecopower_modbus_read_holding_registers(
    uint8_t slave_address,
    uint16_t start_address,
    uint16_t register_count,
    uint16_t *registers,
    uint32_t timeout_ms)
{
    return read_registers(
        0x03U,
        slave_address,
        start_address,
        register_count,
        registers,
        timeout_ms);
}

extern "C" EcoPowerModbusResult ecopower_modbus_read_input_registers(
    uint8_t slave_address,
    uint16_t start_address,
    uint16_t register_count,
    uint16_t *registers,
    uint32_t timeout_ms)
{
    return read_registers(
        0x04U,
        slave_address,
        start_address,
        register_count,
        registers,
        timeout_ms);
}
