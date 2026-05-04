#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t timestamp_ms;
    uint8_t direction;
    uint8_t kind;
    uint8_t slave_address;
    uint8_t function_code;
    int32_t error_code;
    char summary[96];
    char payload[192];
} rs485_log_entry_t;

typedef struct {
    rs485_log_entry_t *entries;
    size_t capacity;
    size_t count;
    size_t head;
    void *lock;
} rs485_log_store_t;

bool rs485_log_store_init(rs485_log_store_t *store, size_t capacity);
void rs485_log_store_deinit(rs485_log_store_t *store);
void rs485_log_store_clear(rs485_log_store_t *store);
bool rs485_log_store_append(rs485_log_store_t *store,
                            uint8_t direction,
                            uint8_t kind,
                            uint8_t slave_address,
                            uint8_t function_code,
                            int32_t error_code,
                            const char *summary,
                            const char *payload);
size_t rs485_log_store_count(const rs485_log_store_t *store);
bool rs485_log_store_get_entry(const rs485_log_store_t *store, size_t index_from_oldest, rs485_log_entry_t *entry);
bool rs485_log_store_export_file(const rs485_log_store_t *store, const char *path);

#ifdef __cplusplus
}
#endif