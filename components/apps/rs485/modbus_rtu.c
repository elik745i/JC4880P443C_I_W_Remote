#include "modbus_rtu.h"

#include <string.h>

static modbus_status_t execute_request(modbus_rtu_master_t *master,
                                       const uint8_t *request,
                                       size_t request_length,
                                       modbus_response_t *response)
{
    if ((master == NULL) || (master->transport == NULL) || (response == NULL)) {
        return MODBUS_STATUS_INVALID_RESPONSE;
    }

    memset(response, 0, sizeof(*response));
    response->transport_status = RS485_TRANSPORT_INVALID_STATE;

    for (uint8_t attempt = 0; attempt <= master->retries; ++attempt) {
        size_t received_length = 0;
        const rs485_transport_status_t transport_status = rs485_transport_transact(master->transport,
                                                                                   request,
                                                                                   request_length,
                                                                                   response->raw_frame,
                                                                                   sizeof(response->raw_frame),
                                                                                   &received_length,
                                                                                   master->timeout_ms);
        response->transport_status = transport_status;
        response->response_time_ms = master->transport->last_round_trip_ms;
        response->raw_length = received_length;
        if (transport_status == RS485_TRANSPORT_TIMEOUT) {
            continue;
        }
        if (transport_status != RS485_TRANSPORT_OK) {
            response->status = MODBUS_STATUS_TRANSPORT;
            return response->status;
        }

        if (received_length < 5) {
            response->status = MODBUS_STATUS_INVALID_RESPONSE;
            return response->status;
        }

        const uint16_t expected_crc = modbus_rtu_crc16(response->raw_frame, received_length - 2);
        const uint16_t actual_crc = (uint16_t)(response->raw_frame[received_length - 2] | (response->raw_frame[received_length - 1] << 8));
        if (expected_crc != actual_crc) {
            response->status = MODBUS_STATUS_BAD_CRC;
            return response->status;
        }

        response->slave_address = response->raw_frame[0];
        response->function_code = response->raw_frame[1];
        if ((response->function_code & 0x80U) != 0U) {
            response->exception_code = response->raw_frame[2];
            response->status = MODBUS_STATUS_EXCEPTION;
            return response->status;
        }

        response->status = MODBUS_STATUS_OK;
        return response->status;
    }

    response->status = MODBUS_STATUS_TIMEOUT;
    return response->status;
}

static modbus_status_t parse_read_response(modbus_response_t *response, uint16_t quantity, bool registers)
{
    if ((response == NULL) || (response->raw_length < 5)) {
        return MODBUS_STATUS_INVALID_RESPONSE;
    }

    const uint8_t byte_count = response->raw_frame[2];
    if ((size_t)(byte_count + 5U) != response->raw_length) {
        return MODBUS_STATUS_INVALID_RESPONSE;
    }

    if (registers) {
        const size_t register_count = byte_count / 2U;
        response->register_count = register_count > MODBUS_MAX_REGISTERS ? MODBUS_MAX_REGISTERS : register_count;
        for (size_t index = 0; index < response->register_count; ++index) {
            response->registers[index] = (uint16_t)(response->raw_frame[3 + (index * 2)] << 8) |
                                         response->raw_frame[4 + (index * 2)];
        }
        return response->register_count == quantity ? MODBUS_STATUS_OK : MODBUS_STATUS_INVALID_RESPONSE;
    }

    response->coil_count = quantity > MODBUS_MAX_COILS ? MODBUS_MAX_COILS : quantity;
    for (size_t index = 0; index < response->coil_count; ++index) {
        const size_t byte_index = index / 8U;
        const size_t bit_index = index % 8U;
        response->coils[index] = (response->raw_frame[3 + byte_index] >> bit_index) & 0x01U;
    }
    return MODBUS_STATUS_OK;
}

static void append_crc(uint8_t *frame, size_t payload_length)
{
    const uint16_t crc = modbus_rtu_crc16(frame, payload_length);
    frame[payload_length] = (uint8_t)(crc & 0xFFU);
    frame[payload_length + 1] = (uint8_t)(crc >> 8);
}

void modbus_rtu_master_init(modbus_rtu_master_t *master, rs485_transport_t *transport, uint32_t timeout_ms, uint8_t retries)
{
    if (master == NULL) {
        return;
    }
    master->transport = transport;
    master->timeout_ms = timeout_ms;
    master->retries = retries;
}

uint16_t modbus_rtu_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001U) != 0U) {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            } else {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

const char *modbus_rtu_exception_to_string(uint8_t exception_code)
{
    switch (exception_code) {
    case 1:
        return "Illegal function";
    case 2:
        return "Illegal data address";
    case 3:
        return "Illegal data value";
    case 4:
        return "Slave device failure";
    case 5:
        return "Acknowledge";
    case 6:
        return "Slave device busy";
    case 8:
        return "Memory parity error";
    case 10:
        return "Gateway path unavailable";
    case 11:
        return "Gateway target no response";
    default:
        return "Unknown exception";
    }
}

const char *modbus_rtu_status_to_string(modbus_status_t status)
{
    switch (status) {
    case MODBUS_STATUS_OK:
        return "OK";
    case MODBUS_STATUS_TRANSPORT:
        return "Transport error";
    case MODBUS_STATUS_TIMEOUT:
        return "Timeout";
    case MODBUS_STATUS_BAD_CRC:
        return "CRC error";
    case MODBUS_STATUS_INVALID_RESPONSE:
        return "Invalid response";
    case MODBUS_STATUS_EXCEPTION:
        return "Exception";
    default:
        return "Unknown";
    }
}

modbus_status_t modbus_rtu_read_holding_registers(modbus_rtu_master_t *master,
                                                  uint8_t slave_address,
                                                  uint16_t start_address,
                                                  uint16_t quantity,
                                                  modbus_response_t *response)
{
    uint8_t frame[8] = {slave_address, MODBUS_FC_READ_HOLDING_REGISTERS,
                        (uint8_t)(start_address >> 8), (uint8_t)(start_address & 0xFF),
                        (uint8_t)(quantity >> 8), (uint8_t)(quantity & 0xFF), 0, 0};
    append_crc(frame, 6);
    modbus_status_t status = execute_request(master, frame, sizeof(frame), response);
    if (status == MODBUS_STATUS_OK) {
        status = parse_read_response(response, quantity, true);
        response->status = status;
    }
    return status;
}

modbus_status_t modbus_rtu_read_input_registers(modbus_rtu_master_t *master,
                                                uint8_t slave_address,
                                                uint16_t start_address,
                                                uint16_t quantity,
                                                modbus_response_t *response)
{
    uint8_t frame[8] = {slave_address, MODBUS_FC_READ_INPUT_REGISTERS,
                        (uint8_t)(start_address >> 8), (uint8_t)(start_address & 0xFF),
                        (uint8_t)(quantity >> 8), (uint8_t)(quantity & 0xFF), 0, 0};
    append_crc(frame, 6);
    modbus_status_t status = execute_request(master, frame, sizeof(frame), response);
    if (status == MODBUS_STATUS_OK) {
        status = parse_read_response(response, quantity, true);
        response->status = status;
    }
    return status;
}

modbus_status_t modbus_rtu_read_coils(modbus_rtu_master_t *master,
                                      uint8_t slave_address,
                                      uint16_t start_address,
                                      uint16_t quantity,
                                      modbus_response_t *response)
{
    uint8_t frame[8] = {slave_address, MODBUS_FC_READ_COILS,
                        (uint8_t)(start_address >> 8), (uint8_t)(start_address & 0xFF),
                        (uint8_t)(quantity >> 8), (uint8_t)(quantity & 0xFF), 0, 0};
    append_crc(frame, 6);
    modbus_status_t status = execute_request(master, frame, sizeof(frame), response);
    if (status == MODBUS_STATUS_OK) {
        status = parse_read_response(response, quantity, false);
        response->status = status;
    }
    return status;
}

modbus_status_t modbus_rtu_write_single_register(modbus_rtu_master_t *master,
                                                 uint8_t slave_address,
                                                 uint16_t register_address,
                                                 uint16_t value,
                                                 modbus_response_t *response)
{
    uint8_t frame[8] = {slave_address, MODBUS_FC_WRITE_SINGLE_REGISTER,
                        (uint8_t)(register_address >> 8), (uint8_t)(register_address & 0xFF),
                        (uint8_t)(value >> 8), (uint8_t)(value & 0xFF), 0, 0};
    append_crc(frame, 6);
    return execute_request(master, frame, sizeof(frame), response);
}

modbus_status_t modbus_rtu_write_single_coil(modbus_rtu_master_t *master,
                                             uint8_t slave_address,
                                             uint16_t coil_address,
                                             bool on,
                                             modbus_response_t *response)
{
    const uint16_t value = on ? 0xFF00U : 0x0000U;
    uint8_t frame[8] = {slave_address, MODBUS_FC_WRITE_SINGLE_COIL,
                        (uint8_t)(coil_address >> 8), (uint8_t)(coil_address & 0xFF),
                        (uint8_t)(value >> 8), (uint8_t)(value & 0xFF), 0, 0};
    append_crc(frame, 6);
    return execute_request(master, frame, sizeof(frame), response);
}

modbus_status_t modbus_rtu_write_multiple_registers(modbus_rtu_master_t *master,
                                                    uint8_t slave_address,
                                                    uint16_t start_address,
                                                    const uint16_t *values,
                                                    uint16_t quantity,
                                                    modbus_response_t *response)
{
    if ((values == NULL) || (quantity == 0) || (quantity > MODBUS_MAX_REGISTERS)) {
        return MODBUS_STATUS_INVALID_RESPONSE;
    }

    uint8_t frame[256] = {0};
    const size_t payload_length = 7U + ((size_t)quantity * 2U);
    frame[0] = slave_address;
    frame[1] = MODBUS_FC_WRITE_MULTIPLE_REGISTERS;
    frame[2] = (uint8_t)(start_address >> 8);
    frame[3] = (uint8_t)(start_address & 0xFF);
    frame[4] = (uint8_t)(quantity >> 8);
    frame[5] = (uint8_t)(quantity & 0xFF);
    frame[6] = (uint8_t)(quantity * 2U);
    for (uint16_t index = 0; index < quantity; ++index) {
        frame[7 + (index * 2U)] = (uint8_t)(values[index] >> 8);
        frame[8 + (index * 2U)] = (uint8_t)(values[index] & 0xFF);
    }
    append_crc(frame, payload_length);
    return execute_request(master, frame, payload_length + 2U, response);
}

modbus_status_t modbus_rtu_write_multiple_coils(modbus_rtu_master_t *master,
                                                uint8_t slave_address,
                                                uint16_t start_address,
                                                const uint8_t *values,
                                                uint16_t quantity,
                                                modbus_response_t *response)
{
    if ((values == NULL) || (quantity == 0) || (quantity > MODBUS_MAX_COILS)) {
        return MODBUS_STATUS_INVALID_RESPONSE;
    }

    const uint8_t byte_count = (uint8_t)((quantity + 7U) / 8U);
    uint8_t frame[256] = {0};
    frame[0] = slave_address;
    frame[1] = MODBUS_FC_WRITE_MULTIPLE_COILS;
    frame[2] = (uint8_t)(start_address >> 8);
    frame[3] = (uint8_t)(start_address & 0xFF);
    frame[4] = (uint8_t)(quantity >> 8);
    frame[5] = (uint8_t)(quantity & 0xFF);
    frame[6] = byte_count;
    for (uint16_t index = 0; index < quantity; ++index) {
        if (values[index] != 0U) {
            frame[7 + (index / 8U)] |= (uint8_t)(1U << (index % 8U));
        }
    }
    append_crc(frame, 7U + byte_count);
    return execute_request(master, frame, 9U + byte_count, response);
}