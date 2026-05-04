#include "profile_store.h"

#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "rs485_profiles";
static const char *NAMESPACE = "rs485_hmi";
static const char *PROFILES_KEY = "profiles";
static const char *TEMPLATES_KEY = "templates";
static const uint32_t STORE_MAGIC = 0x52535046;
static const uint16_t STORE_VERSION = 1;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint8_t count;
    rs485_device_profile_t items[RS485_MAX_PROFILES];
} profile_blob_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint8_t count;
    rs485_terminal_template_t items[RS485_MAX_TEMPLATES];
} template_blob_t;

static void *alloc_profile_buffer(size_t size)
{
    void *buffer = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        buffer = calloc(1, size);
    }
    return buffer;
}

static void profile_store_lock(const rs485_profile_store_t *store)
{
    if ((store != NULL) && (store->lock != NULL)) {
        xSemaphoreTake((SemaphoreHandle_t)store->lock, pdMS_TO_TICKS(50));
    }
}

static void profile_store_unlock(const rs485_profile_store_t *store)
{
    if ((store != NULL) && (store->lock != NULL)) {
        xSemaphoreGive((SemaphoreHandle_t)store->lock);
    }
}

static void install_default_templates(rs485_profile_store_t *store)
{
    if ((store == NULL) || (store->template_count != 0)) {
        return;
    }

    rs485_terminal_template_t ascii = {0};
    strncpy(ascii.name, "ASCII Ping", sizeof(ascii.name) - 1);
    ascii.format = RS485_TERMINAL_FORMAT_ASCII;
    ascii.append_lf = true;
    ascii.payload_len = 4;
    memcpy(ascii.payload, "PING", 4);
    store->templates[store->template_count++] = ascii;

    rs485_terminal_template_t modbus = {0};
    strncpy(modbus.name, "Read HR 0", sizeof(modbus.name) - 1);
    modbus.format = RS485_TERMINAL_FORMAT_HEX;
    modbus.payload_len = 8;
    modbus.payload[0] = 0x01;
    modbus.payload[1] = 0x03;
    modbus.payload[2] = 0x00;
    modbus.payload[3] = 0x00;
    modbus.payload[4] = 0x00;
    modbus.payload[5] = 0x01;
    modbus.payload[6] = 0x84;
    modbus.payload[7] = 0x0A;
    store->templates[store->template_count++] = modbus;
}

bool rs485_profile_store_init(rs485_profile_store_t *store)
{
    if (store == NULL) {
        return false;
    }

    memset(store, 0, sizeof(*store));
    store->lock = xSemaphoreCreateMutex();
    if (store->lock == NULL) {
        return false;
    }

    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        install_default_templates(store);
        return true;
    }

    profile_blob_t *profiles = (profile_blob_t *)alloc_profile_buffer(sizeof(profile_blob_t));
    template_blob_t *templates = (template_blob_t *)alloc_profile_buffer(sizeof(template_blob_t));
    if ((profiles == NULL) || (templates == NULL)) {
        free(profiles);
        free(templates);
        nvs_close(handle);
        install_default_templates(store);
        return true;
    }

    size_t profile_size = sizeof(*profiles);
    if ((nvs_get_blob(handle, PROFILES_KEY, profiles, &profile_size) == ESP_OK) &&
        (profile_size == sizeof(*profiles)) && (profiles->magic == STORE_MAGIC) && (profiles->version == STORE_VERSION)) {
        store->profile_count = profiles->count > RS485_MAX_PROFILES ? RS485_MAX_PROFILES : profiles->count;
        memcpy(store->profiles, profiles->items, sizeof(store->profiles));
    }

    size_t template_size = sizeof(*templates);
    if ((nvs_get_blob(handle, TEMPLATES_KEY, templates, &template_size) == ESP_OK) &&
        (template_size == sizeof(*templates)) && (templates->magic == STORE_MAGIC) && (templates->version == STORE_VERSION)) {
        store->template_count = templates->count > RS485_MAX_TEMPLATES ? RS485_MAX_TEMPLATES : templates->count;
        memcpy(store->templates, templates->items, sizeof(store->templates));
    }

    free(profiles);
    free(templates);
    nvs_close(handle);
    install_default_templates(store);
    return true;
}

void rs485_profile_store_deinit(rs485_profile_store_t *store)
{
    if ((store != NULL) && (store->lock != NULL)) {
        vSemaphoreDelete((SemaphoreHandle_t)store->lock);
        store->lock = NULL;
    }
}

bool rs485_profile_store_save(rs485_profile_store_t *store)
{
    if (store == NULL) {
        return false;
    }

    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    profile_blob_t *profiles = (profile_blob_t *)alloc_profile_buffer(sizeof(profile_blob_t));
    template_blob_t *templates = (template_blob_t *)alloc_profile_buffer(sizeof(template_blob_t));
    if ((profiles == NULL) || (templates == NULL)) {
        free(profiles);
        free(templates);
        nvs_close(handle);
        return false;
    }

    profile_store_lock(store);
    profiles->magic = STORE_MAGIC;
    profiles->version = STORE_VERSION;
    profiles->reserved = 0;
    profiles->count = store->profile_count;
    memcpy(profiles->items, store->profiles, sizeof(store->profiles));

    templates->magic = STORE_MAGIC;
    templates->version = STORE_VERSION;
    templates->reserved = 0;
    templates->count = store->template_count;
    memcpy(templates->items, store->templates, sizeof(store->templates));
    profile_store_unlock(store);

    esp_err_t err = nvs_set_blob(handle, PROFILES_KEY, profiles, sizeof(*profiles));
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, TEMPLATES_KEY, templates, sizeof(*templates));
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    free(profiles);
    free(templates);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist profiles/templates: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

size_t rs485_profile_store_profile_count(const rs485_profile_store_t *store)
{
    return store != NULL ? store->profile_count : 0;
}

bool rs485_profile_store_get_profile(const rs485_profile_store_t *store, size_t index, rs485_device_profile_t *profile)
{
    if ((store == NULL) || (profile == NULL) || (index >= store->profile_count)) {
        return false;
    }

    profile_store_lock(store);
    *profile = store->profiles[index];
    profile_store_unlock(store);
    return true;
}

bool rs485_profile_store_upsert_profile(rs485_profile_store_t *store, const rs485_device_profile_t *profile)
{
    if ((store == NULL) || (profile == NULL)) {
        return false;
    }

    profile_store_lock(store);
    for (size_t index = 0; index < store->profile_count; ++index) {
        if ((store->profiles[index].slave_address == profile->slave_address) &&
            (strncmp(store->profiles[index].name, profile->name, sizeof(store->profiles[index].name)) == 0)) {
            store->profiles[index] = *profile;
            profile_store_unlock(store);
            return true;
        }
    }

    if (store->profile_count >= RS485_MAX_PROFILES) {
        store->profiles[RS485_MAX_PROFILES - 1] = *profile;
        profile_store_unlock(store);
        return true;
    }

    store->profiles[store->profile_count++] = *profile;
    profile_store_unlock(store);
    return true;
}

size_t rs485_profile_store_template_count(const rs485_profile_store_t *store)
{
    return store != NULL ? store->template_count : 0;
}

bool rs485_profile_store_get_template(const rs485_profile_store_t *store, size_t index, rs485_terminal_template_t *templ)
{
    if ((store == NULL) || (templ == NULL) || (index >= store->template_count)) {
        return false;
    }

    profile_store_lock(store);
    *templ = store->templates[index];
    profile_store_unlock(store);
    return true;
}

bool rs485_profile_store_upsert_template(rs485_profile_store_t *store, const rs485_terminal_template_t *templ)
{
    if ((store == NULL) || (templ == NULL)) {
        return false;
    }

    profile_store_lock(store);
    for (size_t index = 0; index < store->template_count; ++index) {
        if (strncmp(store->templates[index].name, templ->name, sizeof(store->templates[index].name)) == 0) {
            store->templates[index] = *templ;
            profile_store_unlock(store);
            return true;
        }
    }

    if (store->template_count >= RS485_MAX_TEMPLATES) {
        store->templates[RS485_MAX_TEMPLATES - 1] = *templ;
        profile_store_unlock(store);
        return true;
    }

    store->templates[store->template_count++] = *templ;
    profile_store_unlock(store);
    return true;
}