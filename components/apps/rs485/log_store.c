#include "log_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static bool ensure_parent_directory(const char *path)
{
    if (path == NULL) {
        return false;
    }

    char directory[256] = {0};
    strncpy(directory, path, sizeof(directory) - 1);
    char *slash = strrchr(directory, '/');
    if (slash == NULL) {
        return true;
    }
    *slash = '\0';
    if (directory[0] == '\0') {
        return true;
    }

    struct stat info = {0};
    if (stat(directory, &info) == 0) {
        return S_ISDIR(info.st_mode);
    }
    return mkdir(directory, 0755) == 0;
}

static void log_store_lock(const rs485_log_store_t *store)
{
    if ((store != NULL) && (store->lock != NULL)) {
        xSemaphoreTake((SemaphoreHandle_t)store->lock, pdMS_TO_TICKS(50));
    }
}

static void log_store_unlock(const rs485_log_store_t *store)
{
    if ((store != NULL) && (store->lock != NULL)) {
        xSemaphoreGive((SemaphoreHandle_t)store->lock);
    }
}

bool rs485_log_store_init(rs485_log_store_t *store, size_t capacity)
{
    if ((store == NULL) || (capacity == 0)) {
        return false;
    }

    memset(store, 0, sizeof(*store));
    store->lock = xSemaphoreCreateMutex();
    if (store->lock == NULL) {
        return false;
    }

    store->entries = (rs485_log_entry_t *)heap_caps_calloc(capacity, sizeof(rs485_log_entry_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (store->entries == NULL) {
        store->entries = (rs485_log_entry_t *)calloc(capacity, sizeof(rs485_log_entry_t));
    }
    if (store->entries == NULL) {
        vSemaphoreDelete((SemaphoreHandle_t)store->lock);
        memset(store, 0, sizeof(*store));
        return false;
    }

    store->capacity = capacity;
    return true;
}

void rs485_log_store_deinit(rs485_log_store_t *store)
{
    if (store == NULL) {
        return;
    }

    if (store->entries != NULL) {
        heap_caps_free(store->entries);
        store->entries = NULL;
    }
    if (store->lock != NULL) {
        vSemaphoreDelete((SemaphoreHandle_t)store->lock);
        store->lock = NULL;
    }
    store->capacity = 0;
    store->count = 0;
    store->head = 0;
}

void rs485_log_store_clear(rs485_log_store_t *store)
{
    if ((store == NULL) || (store->entries == NULL)) {
        return;
    }

    log_store_lock(store);
    memset(store->entries, 0, store->capacity * sizeof(rs485_log_entry_t));
    store->count = 0;
    store->head = 0;
    log_store_unlock(store);
}

bool rs485_log_store_append(rs485_log_store_t *store,
                            uint8_t direction,
                            uint8_t kind,
                            uint8_t slave_address,
                            uint8_t function_code,
                            int32_t error_code,
                            const char *summary,
                            const char *payload)
{
    if ((store == NULL) || (store->entries == NULL) || (store->capacity == 0)) {
        return false;
    }

    log_store_lock(store);
    rs485_log_entry_t *entry = &store->entries[store->head];
    memset(entry, 0, sizeof(*entry));
    entry->timestamp_ms = rs485_app_state_now_ms();
    entry->direction = direction;
    entry->kind = kind;
    entry->slave_address = slave_address;
    entry->function_code = function_code;
    entry->error_code = error_code;
    if (summary != NULL) {
        strncpy(entry->summary, summary, sizeof(entry->summary) - 1);
    }
    if (payload != NULL) {
        strncpy(entry->payload, payload, sizeof(entry->payload) - 1);
    }

    store->head = (store->head + 1) % store->capacity;
    if (store->count < store->capacity) {
        store->count++;
    }
    log_store_unlock(store);
    return true;
}

size_t rs485_log_store_count(const rs485_log_store_t *store)
{
    return store != NULL ? store->count : 0;
}

bool rs485_log_store_get_entry(const rs485_log_store_t *store, size_t index_from_oldest, rs485_log_entry_t *entry)
{
    if ((store == NULL) || (entry == NULL) || (index_from_oldest >= store->count) || (store->entries == NULL)) {
        return false;
    }

    log_store_lock(store);
    const size_t oldest = (store->count < store->capacity) ? 0 : store->head;
    const size_t position = (oldest + index_from_oldest) % store->capacity;
    *entry = store->entries[position];
    log_store_unlock(store);
    return true;
}

bool rs485_log_store_export_file(const rs485_log_store_t *store, const char *path)
{
    if ((store == NULL) || (path == NULL) || !ensure_parent_directory(path)) {
        return false;
    }

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }

    rs485_log_entry_t entry;
    for (size_t index = 0; index < rs485_log_store_count(store); ++index) {
        if (!rs485_log_store_get_entry(store, index, &entry)) {
            continue;
        }

        fprintf(file,
                "%lu,%u,%u,%u,%u,%ld,%s,%s\n",
                (unsigned long)entry.timestamp_ms,
                (unsigned)entry.direction,
                (unsigned)entry.kind,
                (unsigned)entry.slave_address,
                (unsigned)entry.function_code,
                (long)entry.error_code,
                entry.summary,
                entry.payload);
    }

    fclose(file);
    return true;
}