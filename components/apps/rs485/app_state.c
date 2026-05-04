#include "app_state.h"

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "rs485_state";
static const char *NAMESPACE = "rs485_hmi";
static const char *SETTINGS_KEY = "settings";
static const uint32_t SETTINGS_MAGIC = 0x52533438;
static const uint16_t SETTINGS_VERSION = 1;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    rs485_app_settings_t settings;
} rs485_settings_blob_t;

void rs485_app_state_set_defaults(rs485_app_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }

    memset(settings, 0, sizeof(*settings));
    settings->serial.baud_rate = 115200;
    settings->serial.parity = RS485_PARITY_NONE;
    settings->serial.stop_bits = 1;
    settings->serial.data_bits = 8;
    settings->serial.uart_port = 1;
    settings->serial.tx_pin = -1;
    settings->serial.rx_pin = -1;
    settings->serial.en_pin = -1;
    settings->serial.inter_frame_timeout_ms = 8;
    settings->serial.request_timeout_ms = 180;
    settings->serial.tx_pre_delay_us = 100;
    settings->serial.tx_post_delay_us = 100;
    settings->serial.retry_count = 1;
    settings->selected_screen = RS485_SCREEN_HOME;
    settings->selected_profile_index = 0;
    settings->terminal_format = RS485_TERMINAL_FORMAT_HEX;
    settings->terminal_append_cr = false;
    settings->terminal_append_lf = true;
    settings->terminal_auto_repeat_ms = 0;
    settings->scan_start_address = 1;
    settings->scan_end_address = 32;
    settings->scan_probe_mask = 0x07;
    settings->persist_logs_to_file = false;
    settings->log_raw_frames = true;
    settings->log_decoded_values = true;
    settings->manual_slave_address = 1;
    settings->manual_start_address = 0;
    settings->manual_quantity = 4;
}

bool rs485_app_state_load_settings(rs485_app_settings_t *settings)
{
    if (settings == NULL) {
        return false;
    }

    rs485_app_state_set_defaults(settings);

    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    rs485_settings_blob_t blob = {0};
    size_t size = sizeof(blob);
    const esp_err_t err = nvs_get_blob(handle, SETTINGS_KEY, &blob, &size);
    nvs_close(handle);
    if ((err != ESP_OK) || (size != sizeof(blob)) || (blob.magic != SETTINGS_MAGIC) || (blob.version != SETTINGS_VERSION)) {
        return false;
    }

    *settings = blob.settings;
    return true;
}

bool rs485_app_state_save_settings(const rs485_app_settings_t *settings)
{
    if (settings == NULL) {
        return false;
    }

    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    rs485_settings_blob_t blob = {
        .magic = SETTINGS_MAGIC,
        .version = SETTINGS_VERSION,
        .reserved = 0,
        .settings = *settings,
    };

    esp_err_t err = nvs_set_blob(handle, SETTINGS_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save settings: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

const char *rs485_app_state_parity_to_string(uint8_t parity)
{
    switch (parity) {
    case RS485_PARITY_EVEN:
        return "Even";
    case RS485_PARITY_ODD:
        return "Odd";
    case RS485_PARITY_NONE:
    default:
        return "None";
    }
}

const char *rs485_app_state_protocol_to_string(uint8_t protocol_mode)
{
    switch (protocol_mode) {
    case RS485_PROTOCOL_MODE_MODBUS_RTU:
        return "Modbus RTU";
    case RS485_PROTOCOL_MODE_RAW:
    default:
        return "Raw RS-485";
    }
}

const char *rs485_app_state_terminal_format_to_string(uint8_t format)
{
    switch (format) {
    case RS485_TERMINAL_FORMAT_ASCII:
        return "ASCII";
    case RS485_TERMINAL_FORMAT_HEX:
    default:
        return "HEX";
    }
}

uint32_t rs485_app_state_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}