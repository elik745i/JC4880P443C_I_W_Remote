#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rs485_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_MAX_REGISTERS 64
#define MODBUS_MAX_COILS 128

typedef enum {
    MODBUS_STATUS_OK = 0,
    MODBUS_STATUS_TRANSPORT,
    MODBUS_STATUS_TIMEOUT,
    MODBUS_STATUS_BAD_CRC,
    MODBUS_STATUS_INVALID_RESPONSE,
    MODBUS_STATUS_EXCEPTION,
} modbus_status_t;

typedef enum {
    MODBUS_FC_READ_COILS = 1,
    MODBUS_FC_READ_DISCRETE_INPUTS = 2,
    MODBUS_FC_READ_HOLDING_REGISTERS = 3,
    MODBUS_FC_READ_INPUT_REGISTERS = 4,
    MODBUS_FC_WRITE_SINGLE_COIL = 5,
    MODBUS_FC_WRITE_SINGLE_REGISTER = 6,
    MODBUS_FC_WRITE_MULTIPLE_COILS = 15,
    MODBUS_FC_WRITE_MULTIPLE_REGISTERS = 16,
} modbus_function_code_t;

typedef struct {
    rs485_transport_t *transport;
    uint32_t timeout_ms;
    uint8_t retries;
} modbus_rtu_master_t;

typedef struct {
    modbus_status_t status;
    rs485_transport_status_t transport_status;
    uint8_t slave_address;
    uint8_t function_code;
    uint8_t exception_code;
    uint32_t response_time_ms;
    size_t raw_length;
    uint8_t raw_frame[256];
    size_t register_count;
    uint16_t registers[MODBUS_MAX_REGISTERS];
    size_t coil_count;
    uint8_t coils[MODBUS_MAX_COILS];
} modbus_response_t;

void modbus_rtu_master_init(modbus_rtu_master_t *master, rs485_transport_t *transport, uint32_t timeout_ms, uint8_t retries);
uint16_t modbus_rtu_crc16(const uint8_t *data, size_t length);
const char *modbus_rtu_exception_to_string(uint8_t exception_code);
const char *modbus_rtu_status_to_string(modbus_status_t status);

modbus_status_t modbus_rtu_read_holding_registers(modbus_rtu_master_t *master,
                                                  uint8_t slave_address,
                                                  uint16_t start_address,
                                                  uint16_t quantity,
                                                  modbus_response_t *response);
modbus_status_t modbus_rtu_read_input_registers(modbus_rtu_master_t *master,
                                                uint8_t slave_address,
                                                uint16_t start_address,
                                                uint16_t quantity,
                                                modbus_response_t *response);
modbus_status_t modbus_rtu_read_coils(modbus_rtu_master_t *master,
                                      uint8_t slave_address,
                                      uint16_t start_address,
                                      uint16_t quantity,
                                      modbus_response_t *response);
modbus_status_t modbus_rtu_write_single_register(modbus_rtu_master_t *master,
                                                 uint8_t slave_address,
                                                 uint16_t register_address,
                                                 uint16_t value,
                                                 modbus_response_t *response);
modbus_status_t modbus_rtu_write_single_coil(modbus_rtu_master_t *master,
                                             uint8_t slave_address,
                                             uint16_t coil_address,
                                             bool on,
                                             modbus_response_t *response);
modbus_status_t modbus_rtu_write_multiple_registers(modbus_rtu_master_t *master,
                                                    uint8_t slave_address,
                                                    uint16_t start_address,
                                                    const uint16_t *values,
                                                    uint16_t quantity,
                                                    modbus_response_t *response);
modbus_status_t modbus_rtu_write_multiple_coils(modbus_rtu_master_t *master,
                                                uint8_t slave_address,
                                                uint16_t start_address,
                                                const uint8_t *values,
                                                uint16_t quantity,
                                                modbus_response_t *response);

#ifdef __cplusplus
}
#endif