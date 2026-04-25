#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JC4880_JOYPAD_PROTOCOL_VERSION 2u
#define JC4880_JOYPAD_MSG_CONFIG 0x4a430101u
#define JC4880_JOYPAD_MSG_REPORT 0x4a430102u

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

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t backend;
    uint8_t ble_enabled;
    uint8_t ble_discovery_enabled;
    char ble_device_addr[18];
} jc4880_joypad_config_msg_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t connected;
    uint8_t scanning;
    uint8_t reserved;
    uint32_t raw_mask;
    int16_t axis_x;
    int16_t axis_y;
    int16_t axis_rx;
    int16_t axis_ry;
    uint16_t brake;
    uint16_t throttle;
    char device_addr[18];
    char device_name[32];
} jc4880_joypad_report_t;

#ifdef __cplusplus
}
#endif