#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RS485_TRANSPORT_OK = 0,
    RS485_TRANSPORT_TIMEOUT,
    RS485_TRANSPORT_UART_ERROR,
    RS485_TRANSPORT_INVALID_ARG,
    RS485_TRANSPORT_INVALID_STATE,
    RS485_TRANSPORT_BUSY,
    RS485_TRANSPORT_COLLISION,
    RS485_TRANSPORT_MALFORMED_FRAME,
} rs485_transport_status_t;

typedef struct {
    uart_port_t uart_port;
    int tx_pin;
    int rx_pin;
    int en_pin;
    uint32_t baud_rate;
    uart_word_length_t data_bits;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
    uint16_t inter_frame_timeout_ms;
    uint16_t request_timeout_ms;
    uint16_t tx_pre_delay_us;
    uint16_t tx_post_delay_us;
} rs485_transport_config_t;

typedef struct {
    rs485_transport_config_t config;
    void *lock;
    bool installed;
    rs485_transport_status_t last_status;
    uint32_t last_round_trip_ms;
} rs485_transport_t;

bool rs485_transport_init(rs485_transport_t *transport, const rs485_transport_config_t *config);
void rs485_transport_deinit(rs485_transport_t *transport);
bool rs485_transport_reconfigure(rs485_transport_t *transport, const rs485_transport_config_t *config);

rs485_transport_status_t rs485_transport_send_frame(rs485_transport_t *transport, const uint8_t *data, size_t length);
rs485_transport_status_t rs485_transport_receive_frame(rs485_transport_t *transport,
                                                       uint8_t *buffer,
                                                       size_t capacity,
                                                       size_t *received_length,
                                                       uint32_t overall_timeout_ms);
rs485_transport_status_t rs485_transport_transact(rs485_transport_t *transport,
                                                  const uint8_t *tx_data,
                                                  size_t tx_length,
                                                  uint8_t *rx_buffer,
                                                  size_t rx_capacity,
                                                  size_t *rx_length,
                                                  uint32_t timeout_ms);

const char *rs485_transport_status_to_string(rs485_transport_status_t status);

#ifdef __cplusplus
}
#endif