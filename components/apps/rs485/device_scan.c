#include "device_scan.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void callback_log(const rs485_scan_callbacks_t *callbacks, const char *message)
{
    if ((callbacks != NULL) && (callbacks->on_log != NULL) && (message != NULL)) {
        callbacks->on_log(callbacks->user_ctx, message);
    }
}

bool rs485_device_scan_run(modbus_rtu_master_t *master,
                           const rs485_scan_config_t *config,
                           const rs485_scan_callbacks_t *callbacks)
{
    if ((master == NULL) || (config == NULL)) {
        return false;
    }

    const uint8_t start = config->start_address < 1 ? 1 : config->start_address;
    const uint8_t end = config->end_address > 247 ? 247 : config->end_address;
    if (start > end) {
        callback_log(callbacks, "Invalid scan range");
        return false;
    }

    const uint8_t total = (uint8_t)(end - start + 1U);
    for (uint8_t offset = 0; offset < total; ++offset) {
        if ((callbacks != NULL) && (callbacks->cancel_flag != NULL) && *callbacks->cancel_flag) {
            callback_log(callbacks, "Scan canceled");
            return false;
        }

        const uint8_t address = (uint8_t)(start + offset);
        if ((callbacks != NULL) && (callbacks->on_progress != NULL)) {
            callbacks->on_progress(callbacks->user_ctx, (uint8_t)(offset + 1U), total);
        }

        rs485_discovered_device_t discovered = {0};
        discovered.slave_address = address;
        discovered.protocol_mode = RS485_PROTOCOL_MODE_MODBUS_RTU;
        bool found = false;

        modbus_response_t response = {0};
        if ((config->probe_mask & RS485_SCAN_PROBE_READ_HOLDING) != 0U) {
            if (modbus_rtu_read_holding_registers(master, address, 0, 1, &response) == MODBUS_STATUS_OK) {
                discovered.supported_functions_mask |= RS485_SCAN_PROBE_READ_HOLDING;
                discovered.response_time_ms = response.response_time_ms;
                found = true;
            } else if (response.status == MODBUS_STATUS_EXCEPTION) {
                discovered.last_exception_code = response.exception_code;
            }
            vTaskDelay(pdMS_TO_TICKS(config->probe_spacing_ms));
        }

        if ((config->probe_mask & RS485_SCAN_PROBE_READ_INPUT) != 0U) {
            if (modbus_rtu_read_input_registers(master, address, 0, 1, &response) == MODBUS_STATUS_OK) {
                discovered.supported_functions_mask |= RS485_SCAN_PROBE_READ_INPUT;
                discovered.response_time_ms = response.response_time_ms;
                found = true;
            }
            vTaskDelay(pdMS_TO_TICKS(config->probe_spacing_ms));
        }

        if ((config->probe_mask & RS485_SCAN_PROBE_READ_COILS) != 0U) {
            if (modbus_rtu_read_coils(master, address, 0, 8, &response) == MODBUS_STATUS_OK) {
                discovered.supported_functions_mask |= RS485_SCAN_PROBE_READ_COILS;
                discovered.response_time_ms = response.response_time_ms;
                found = true;
            }
            vTaskDelay(pdMS_TO_TICKS(config->probe_spacing_ms));
        }

        if (found) {
            snprintf(discovered.user_name, sizeof(discovered.user_name), "Slave %u", (unsigned)address);
            snprintf(discovered.summary,
                     sizeof(discovered.summary),
                     "Addr %u responded in %lu ms",
                     (unsigned)address,
                     (unsigned long)discovered.response_time_ms);
            if ((callbacks != NULL) && (callbacks->on_device != NULL)) {
                callbacks->on_device(callbacks->user_ctx, &discovered);
            }
        }
    }

    callback_log(callbacks, "Scan finished");
    return true;
}