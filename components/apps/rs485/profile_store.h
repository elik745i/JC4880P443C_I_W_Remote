#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *lock;
    rs485_device_profile_t profiles[RS485_MAX_PROFILES];
    uint8_t profile_count;
    rs485_terminal_template_t templates[RS485_MAX_TEMPLATES];
    uint8_t template_count;
} rs485_profile_store_t;

bool rs485_profile_store_init(rs485_profile_store_t *store);
void rs485_profile_store_deinit(rs485_profile_store_t *store);
bool rs485_profile_store_save(rs485_profile_store_t *store);

size_t rs485_profile_store_profile_count(const rs485_profile_store_t *store);
bool rs485_profile_store_get_profile(const rs485_profile_store_t *store, size_t index, rs485_device_profile_t *profile);
bool rs485_profile_store_upsert_profile(rs485_profile_store_t *store, const rs485_device_profile_t *profile);

size_t rs485_profile_store_template_count(const rs485_profile_store_t *store);
bool rs485_profile_store_get_template(const rs485_profile_store_t *store, size_t index, rs485_terminal_template_t *templ);
bool rs485_profile_store_upsert_template(rs485_profile_store_t *store, const rs485_terminal_template_t *templ);

#ifdef __cplusplus
}
#endif