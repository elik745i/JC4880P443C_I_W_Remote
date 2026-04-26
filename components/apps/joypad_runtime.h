#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JC4880_JOYPAD_MASK_UP (1u << 0)
#define JC4880_JOYPAD_MASK_DOWN (1u << 1)
#define JC4880_JOYPAD_MASK_LEFT (1u << 2)
#define JC4880_JOYPAD_MASK_RIGHT (1u << 3)
#define JC4880_JOYPAD_MASK_BUTTON_A (1u << 4)
#define JC4880_JOYPAD_MASK_BUTTON_B (1u << 5)
#define JC4880_JOYPAD_MASK_BUTTON_C (1u << 6)
#define JC4880_JOYPAD_MASK_START (1u << 7)
#define JC4880_JOYPAD_MASK_BUTTON_Y (1u << 8)
#define JC4880_JOYPAD_MASK_SHOULDER_L (1u << 9)
#define JC4880_JOYPAD_MASK_SHOULDER_R (1u << 10)
#define JC4880_JOYPAD_MASK_TRIGGER_L (1u << 11)
#define JC4880_JOYPAD_MASK_TRIGGER_R (1u << 12)
#define JC4880_JOYPAD_MASK_SELECT (1u << 13)
#define JC4880_JOYPAD_MASK_SYSTEM (1u << 14)
#define JC4880_JOYPAD_MASK_THUMB_L (1u << 15)
#define JC4880_JOYPAD_MASK_THUMB_R (1u << 16)
#define JC4880_JOYPAD_MASK_STICK_L_UP (1u << 17)
#define JC4880_JOYPAD_MASK_STICK_L_DOWN (1u << 18)
#define JC4880_JOYPAD_MASK_STICK_L_LEFT (1u << 19)
#define JC4880_JOYPAD_MASK_STICK_L_RIGHT (1u << 20)
#define JC4880_JOYPAD_MASK_STICK_R_UP (1u << 21)
#define JC4880_JOYPAD_MASK_STICK_R_DOWN (1u << 22)
#define JC4880_JOYPAD_MASK_STICK_R_LEFT (1u << 23)
#define JC4880_JOYPAD_MASK_STICK_R_RIGHT (1u << 24)
#define JC4880_JOYPAD_MASK_CAPTURE (1u << 25)

#define JC4880_JOYPAD_ACTION_EXIT (1u << 0)
#define JC4880_JOYPAD_ACTION_SAVE (1u << 1)
#define JC4880_JOYPAD_ACTION_LOAD (1u << 2)

typedef enum {
    JC4880_JOYPAD_BACKEND_DISABLED = 0,
    JC4880_JOYPAD_BACKEND_BLE = 1,
    JC4880_JOYPAD_BACKEND_MANUAL = 2,
} jc4880_joypad_backend_t;

typedef enum {
    JC4880_JOYPAD_MANUAL_MODE_SPI = 0,
    JC4880_JOYPAD_MANUAL_MODE_RESISTIVE = 1,
    JC4880_JOYPAD_MANUAL_MODE_MCP23017 = 2,
} jc4880_joypad_manual_mode_t;

typedef enum {
    JC4880_JOYPAD_MAP_NONE = 0,
    JC4880_JOYPAD_MAP_UP,
    JC4880_JOYPAD_MAP_DOWN,
    JC4880_JOYPAD_MAP_LEFT,
    JC4880_JOYPAD_MAP_RIGHT,
    JC4880_JOYPAD_MAP_BUTTON_A,
    JC4880_JOYPAD_MAP_BUTTON_B,
    JC4880_JOYPAD_MAP_BUTTON_C,
    JC4880_JOYPAD_MAP_START,
    JC4880_JOYPAD_MAP_EXIT,
    JC4880_JOYPAD_MAP_SAVE,
    JC4880_JOYPAD_MAP_LOAD,
} jc4880_joypad_map_target_t;

typedef enum {
    JC4880_JOYPAD_BLE_CONTROL_UP = 0,
    JC4880_JOYPAD_BLE_CONTROL_DOWN,
    JC4880_JOYPAD_BLE_CONTROL_LEFT,
    JC4880_JOYPAD_BLE_CONTROL_RIGHT,
    JC4880_JOYPAD_BLE_CONTROL_BUTTON_A,
    JC4880_JOYPAD_BLE_CONTROL_BUTTON_B,
    JC4880_JOYPAD_BLE_CONTROL_BUTTON_C,
    JC4880_JOYPAD_BLE_CONTROL_START,
    JC4880_JOYPAD_BLE_CONTROL_BUTTON_Y,
    JC4880_JOYPAD_BLE_CONTROL_SHOULDER_L,
    JC4880_JOYPAD_BLE_CONTROL_SHOULDER_R,
    JC4880_JOYPAD_BLE_CONTROL_TRIGGER_L,
    JC4880_JOYPAD_BLE_CONTROL_TRIGGER_R,
    JC4880_JOYPAD_BLE_CONTROL_SELECT,
    JC4880_JOYPAD_BLE_CONTROL_SYSTEM,
    JC4880_JOYPAD_BLE_CONTROL_CAPTURE,
    JC4880_JOYPAD_BLE_CONTROL_THUMB_L,
    JC4880_JOYPAD_BLE_CONTROL_THUMB_R,
    JC4880_JOYPAD_BLE_CONTROL_STICK_L_UP,
    JC4880_JOYPAD_BLE_CONTROL_STICK_L_DOWN,
    JC4880_JOYPAD_BLE_CONTROL_STICK_L_LEFT,
    JC4880_JOYPAD_BLE_CONTROL_STICK_L_RIGHT,
    JC4880_JOYPAD_BLE_CONTROL_STICK_R_UP,
    JC4880_JOYPAD_BLE_CONTROL_STICK_R_DOWN,
    JC4880_JOYPAD_BLE_CONTROL_STICK_R_LEFT,
    JC4880_JOYPAD_BLE_CONTROL_STICK_R_RIGHT,
    JC4880_JOYPAD_BLE_CONTROL_COUNT,
} jc4880_joypad_ble_control_t;

typedef enum {
    JC4880_JOYPAD_SPI_CONTROL_UP = 0,
    JC4880_JOYPAD_SPI_CONTROL_DOWN,
    JC4880_JOYPAD_SPI_CONTROL_LEFT,
    JC4880_JOYPAD_SPI_CONTROL_RIGHT,
    JC4880_JOYPAD_SPI_CONTROL_START,
    JC4880_JOYPAD_SPI_CONTROL_EXIT,
    JC4880_JOYPAD_SPI_CONTROL_SAVE,
    JC4880_JOYPAD_SPI_CONTROL_LOAD,
    JC4880_JOYPAD_SPI_CONTROL_BUTTON_A,
    JC4880_JOYPAD_SPI_CONTROL_BUTTON_B,
    JC4880_JOYPAD_SPI_CONTROL_BUTTON_C,
    JC4880_JOYPAD_SPI_CONTROL_COUNT,
} jc4880_joypad_spi_control_t;

typedef enum {
    JC4880_JOYPAD_BUTTON_CONTROL_START = 0,
    JC4880_JOYPAD_BUTTON_CONTROL_EXIT,
    JC4880_JOYPAD_BUTTON_CONTROL_SAVE,
    JC4880_JOYPAD_BUTTON_CONTROL_LOAD,
    JC4880_JOYPAD_BUTTON_CONTROL_BUTTON_A,
    JC4880_JOYPAD_BUTTON_CONTROL_BUTTON_B,
    JC4880_JOYPAD_BUTTON_CONTROL_BUTTON_C,
    JC4880_JOYPAD_BUTTON_CONTROL_COUNT,
} jc4880_joypad_button_control_t;

typedef struct {
    uint8_t backend;
    uint8_t ble_enabled;
    uint8_t ble_discovery_enabled;
    uint8_t ble_remap[JC4880_JOYPAD_BLE_CONTROL_COUNT];
    uint8_t manual_mode;
    int8_t manual_spi_gpio[JC4880_JOYPAD_SPI_CONTROL_COUNT];
    int8_t manual_resistive_gpio[2];
    int8_t manual_resistive_button_binding[JC4880_JOYPAD_BUTTON_CONTROL_COUNT];
    int8_t manual_mcp_i2c_gpio[2];
    int8_t manual_mcp_button_pin[JC4880_JOYPAD_BUTTON_CONTROL_COUNT];
    char ble_device_addr[18];
} jc4880_joypad_config_t;

typedef struct {
    uint8_t connected;
    uint8_t scanning;
    uint8_t calibration_active;
    uint8_t calibration_available;
    uint32_t raw_mask;
    int16_t raw_axis_x;
    int16_t raw_axis_y;
    int16_t raw_axis_rx;
    int16_t raw_axis_ry;
    uint16_t raw_brake;
    uint16_t raw_throttle;
    int16_t axis_x;
    int16_t axis_y;
    int16_t axis_rx;
    int16_t axis_ry;
    uint16_t brake;
    uint16_t throttle;
    char device_addr[18];
    char device_name[32];
} jc4880_joypad_ble_report_state_t;

typedef struct {
    uint8_t valid;
    uint8_t reserved[3];
    uint32_t last_used_counter;
    char device_addr[18];
    char device_name[32];
    int16_t axis_center[4];
    int16_t axis_min[4];
    int16_t axis_max[4];
    uint16_t pedal_min[2];
    uint16_t pedal_max[2];
} jc4880_joypad_ble_calibration_slot_t;

typedef void (*jc4880_joypad_config_changed_callback_t)(const jc4880_joypad_config_t *config, void *context);

void jc4880_joypad_init(void);
bool jc4880_joypad_get_config(jc4880_joypad_config_t *out_config);
bool jc4880_joypad_set_config(const jc4880_joypad_config_t *config);
void jc4880_joypad_register_config_changed_callback(jc4880_joypad_config_changed_callback_t callback, void *context);
bool jc4880_joypad_get_ble_status(bool *connected, char *device_name, size_t device_name_size);
bool jc4880_joypad_get_ble_report_state(jc4880_joypad_ble_report_state_t *out_report);
bool jc4880_joypad_begin_ble_calibration(void);
bool jc4880_joypad_finish_ble_calibration(void);
void jc4880_joypad_cancel_ble_calibration(void);
bool jc4880_joypad_is_ble_calibration_active(void);
bool jc4880_joypad_get_ble_calibration_slot(const char *device_addr, jc4880_joypad_ble_calibration_slot_t *out_slot);
uint32_t jc4880_joypad_get_input_mask(void);
uint32_t jc4880_joypad_consume_action_mask(void);
void jc4880_joypad_set_ble_report(bool connected,
                                  bool scanning,
                                  const char *device_name,
                                  const char *device_addr,
                                  uint32_t raw_mask,
                                  int16_t axis_x,
                                  int16_t axis_y,
                                  int16_t axis_rx,
                                  int16_t axis_ry,
                                  uint16_t brake,
                                  uint16_t throttle);
uint32_t jc4880_joypad_apply_ble_remap(uint32_t raw_mask, const jc4880_joypad_config_t *config);

#ifdef __cplusplus
}
#endif