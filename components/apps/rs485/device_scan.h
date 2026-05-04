#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_state.h"
#include "modbus_rtu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RS485_SCAN_PROBE_READ_COILS (1U << 0)
#define RS485_SCAN_PROBE_READ_HOLDING (1U << 1)
#define RS485_SCAN_PROBE_READ_INPUT (1U << 2)

typedef struct {
    uint8_t start_address;
    uint8_t end_address;
    uint8_t probe_mask;
    uint8_t retries;
    uint16_t probe_spacing_ms;
} rs485_scan_config_t;

typedef struct {
    volatile bool *cancel_flag;
    void *user_ctx;
    void (*on_progress)(void *user_ctx, uint8_t current, uint8_t total);
    void (*on_device)(void *user_ctx, const rs485_discovered_device_t *device);
    void (*on_log)(void *user_ctx, const char *message);
} rs485_scan_callbacks_t;

bool rs485_device_scan_run(modbus_rtu_master_t *master,
                           const rs485_scan_config_t *config,
                           const rs485_scan_callbacks_t *callbacks);

#ifdef __cplusplus
}
#endif