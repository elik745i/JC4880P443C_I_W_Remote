#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RS485_MAX_PROFILES 16
#define RS485_MAX_FAVORITE_REGISTERS 12
#define RS485_MAX_TEMPLATES 16
#define RS485_MAX_TEMPLATE_NAME 32
#define RS485_MAX_TEMPLATE_BYTES 80
#define RS485_MAX_PROFILE_NAME 32
#define RS485_MAX_PROFILE_NOTES 112

typedef enum {
    RS485_SCREEN_HOME = 0,
    RS485_SCREEN_SCAN,
    RS485_SCREEN_TERMINAL,
    RS485_SCREEN_MODBUS,
    RS485_SCREEN_DEVICES,
    RS485_SCREEN_DASHBOARD,
    RS485_SCREEN_LOGS,
    RS485_SCREEN_SETTINGS,
    RS485_SCREEN_COUNT,
} rs485_screen_id_t;

typedef enum {
    RS485_PROTOCOL_MODE_RAW = 0,
    RS485_PROTOCOL_MODE_MODBUS_RTU = 1,
} rs485_protocol_mode_t;

typedef enum {
    RS485_TERMINAL_FORMAT_ASCII = 0,
    RS485_TERMINAL_FORMAT_HEX = 1,
} rs485_terminal_format_t;

typedef enum {
    RS485_PARITY_NONE = 0,
    RS485_PARITY_EVEN = 1,
    RS485_PARITY_ODD = 2,
} rs485_parity_t;

typedef enum {
    RS485_LOG_DIRECTION_EVENT = 0,
    RS485_LOG_DIRECTION_TX = 1,
    RS485_LOG_DIRECTION_RX = 2,
} rs485_log_direction_t;

typedef enum {
    RS485_LOG_KIND_RAW = 0,
    RS485_LOG_KIND_MODBUS = 1,
    RS485_LOG_KIND_ERROR = 2,
    RS485_LOG_KIND_STATUS = 3,
} rs485_log_kind_t;

typedef struct {
    uint32_t baud_rate;
    uint8_t parity;
    uint8_t stop_bits;
    uint8_t data_bits;
    int8_t uart_port;
    int16_t tx_pin;
    int16_t rx_pin;
    int16_t en_pin;
    uint16_t inter_frame_timeout_ms;
    uint16_t request_timeout_ms;
    uint16_t tx_pre_delay_us;
    uint16_t tx_post_delay_us;
    uint8_t retry_count;
} rs485_serial_settings_t;

typedef struct {
    char name[RS485_MAX_PROFILE_NAME];
    uint8_t slave_address;
    uint8_t protocol_mode;
    rs485_serial_settings_t serial;
    char notes[RS485_MAX_PROFILE_NOTES];
    uint16_t favorite_registers[RS485_MAX_FAVORITE_REGISTERS];
    uint8_t favorite_count;
    uint16_t polling_interval_ms;
    float scale;
    uint8_t decimals;
    int32_t alarm_low_milli;
    int32_t alarm_high_milli;
} rs485_device_profile_t;

typedef struct {
    char name[RS485_MAX_TEMPLATE_NAME];
    uint8_t payload[RS485_MAX_TEMPLATE_BYTES];
    uint8_t payload_len;
    uint8_t format;
    bool append_cr;
    bool append_lf;
} rs485_terminal_template_t;

typedef struct {
    uint8_t slave_address;
    uint8_t supported_functions_mask;
    uint8_t protocol_mode;
    uint8_t last_exception_code;
    uint32_t response_time_ms;
    char user_name[RS485_MAX_PROFILE_NAME];
    char summary[64];
} rs485_discovered_device_t;

typedef struct {
    rs485_serial_settings_t serial;
    uint8_t selected_screen;
    uint8_t selected_profile_index;
    uint8_t terminal_format;
    bool terminal_append_cr;
    bool terminal_append_lf;
    uint32_t terminal_auto_repeat_ms;
    uint16_t scan_start_address;
    uint16_t scan_end_address;
    uint8_t scan_probe_mask;
    bool persist_logs_to_file;
    bool log_raw_frames;
    bool log_decoded_values;
    uint8_t manual_slave_address;
    uint16_t manual_start_address;
    uint16_t manual_quantity;
} rs485_app_settings_t;

void rs485_app_state_set_defaults(rs485_app_settings_t *settings);
bool rs485_app_state_load_settings(rs485_app_settings_t *settings);
bool rs485_app_state_save_settings(const rs485_app_settings_t *settings);

const char *rs485_app_state_parity_to_string(uint8_t parity);
const char *rs485_app_state_protocol_to_string(uint8_t protocol_mode);
const char *rs485_app_state_terminal_format_to_string(uint8_t format);
uint32_t rs485_app_state_now_ms(void);

#ifdef __cplusplus
}
#endif