#include "c6_joypad_service.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_stdio_esp32.h>
#include <uni.h>

#include "bt/uni_bt.h"
#include "jc4880_joypad_protocol.h"
#include "controller/uni_controller.h"
#include "controller/uni_gamepad.h"
#include "esp_hosted_peer_data.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform/uni_platform.h"
#include "sdkconfig.h"

typedef struct {
    jc4880_joypad_config_msg_t config;
    bool config_dirty;
    bool bluepad_ready;
    bool report_connected;
    bool report_scanning;
    int active_device_idx;
    uint32_t last_raw_mask;
    int16_t axis_x;
    int16_t axis_y;
    int16_t axis_rx;
    int16_t axis_ry;
    uint16_t brake;
    uint16_t throttle;
    char active_device_addr[18];
    char active_device_name[32];
} joypad_state_t;

static const char *TAG = "C6Joypad";
static SemaphoreHandle_t s_state_mutex;
static joypad_state_t s_state = {
    .config = {
        .version = JC4880_JOYPAD_PROTOCOL_VERSION,
    },
    .config_dirty = true,
    .bluepad_ready = false,
    .report_connected = false,
    .report_scanning = false,
    .active_device_idx = -1,
    .last_raw_mask = 0,
};

static void copy_addr_string_from_bdaddr(char *out_addr, size_t out_size, const bd_addr_t addr)
{
    if ((out_addr == NULL) || (out_size == 0)) {
        return;
    }

    const char *addr_text = bd_addr_to_str(addr);
    if (addr_text == NULL) {
        out_addr[0] = '\0';
        return;
    }

    strlcpy(out_addr, addr_text, out_size);
}

static bool joypad_backend_enabled_locked(void)
{
    return (s_state.config.version == JC4880_JOYPAD_PROTOCOL_VERSION) &&
           (s_state.config.backend == 1) &&
           (s_state.config.ble_enabled != 0);
}

static bool discovery_enabled_locked(void)
{
    return joypad_backend_enabled_locked() && (s_state.config.ble_discovery_enabled != 0);
}

static bool address_matches_config_locked(const char *addr)
{
    if ((addr == NULL) || (addr[0] == '\0')) {
        return false;
    }
    if (s_state.config.ble_device_addr[0] == '\0') {
        return true;
    }
    return strcasecmp(s_state.config.ble_device_addr, addr) == 0;
}

static void send_report_locked(void)
{
    jc4880_joypad_report_t report = {
        .version = JC4880_JOYPAD_PROTOCOL_VERSION,
        .connected = s_state.report_connected ? 1 : 0,
        .scanning = s_state.report_scanning ? 1 : 0,
        .reserved = 0,
        .raw_mask = s_state.last_raw_mask,
        .axis_x = s_state.axis_x,
        .axis_y = s_state.axis_y,
        .axis_rx = s_state.axis_rx,
        .axis_ry = s_state.axis_ry,
        .brake = s_state.brake,
        .throttle = s_state.throttle,
    };

    strlcpy(report.device_addr, s_state.active_device_addr, sizeof(report.device_addr));
    strlcpy(report.device_name, s_state.active_device_name, sizeof(report.device_name));

    const esp_err_t err = esp_hosted_send_custom_data(JC4880_JOYPAD_MSG_REPORT,
                                                      (const uint8_t *)&report,
                                                      sizeof(report));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send joypad report to host: %s", esp_err_to_name(err));
    }
}

static void hosted_config_callback(uint32_t msg_id, const uint8_t *data, size_t data_len)
{
    (void)msg_id;
    if ((data == NULL) || (data_len < sizeof(jc4880_joypad_config_msg_t))) {
        ESP_LOGW(TAG, "Ignoring short joypad config message: %u bytes", (unsigned)data_len);
        return;
    }

    jc4880_joypad_config_msg_t config = {0};
    memcpy(&config, data, sizeof(config));
    if (config.version != JC4880_JOYPAD_PROTOCOL_VERSION) {
        ESP_LOGW(TAG, "Ignoring joypad config version %u", (unsigned)config.version);
        return;
    }

    config.ble_device_addr[sizeof(config.ble_device_addr) - 1] = '\0';

    ESP_LOGI(TAG,
             "Received joypad config: backend=%u ble=%u discovery=%u addr=%s",
             (unsigned)config.backend,
             (unsigned)config.ble_enabled,
             (unsigned)config.ble_discovery_enabled,
             config.ble_device_addr[0] != '\0' ? config.ble_device_addr : "unsaved");

    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
        s_state.config = config;
        s_state.config_dirty = true;
        xSemaphoreGive(s_state_mutex);
    }
}

static uint32_t gamepad_to_raw_mask(const uni_gamepad_t *gp)
{
    uint32_t raw_mask = 0;
    if (gp == NULL) {
        return 0;
    }

    if (gp->dpad & DPAD_UP) {
        raw_mask |= (1u << 0);
    }
    if (gp->dpad & DPAD_DOWN) {
        raw_mask |= (1u << 1);
    }
    if (gp->dpad & DPAD_LEFT) {
        raw_mask |= (1u << 2);
    }
    if (gp->dpad & DPAD_RIGHT) {
        raw_mask |= (1u << 3);
    }
    if (gp->buttons & BUTTON_A) {
        raw_mask |= (1u << 4);
    }
    if (gp->buttons & BUTTON_B) {
        raw_mask |= (1u << 5);
    }
    if (gp->buttons & BUTTON_X) {
        raw_mask |= (1u << 6);
    }
    if (gp->misc_buttons & MISC_BUTTON_START) {
        raw_mask |= (1u << 7);
    }
    if (gp->buttons & BUTTON_Y) {
        raw_mask |= JC4880_JOYPAD_MASK_BUTTON_Y;
    }
    if (gp->buttons & BUTTON_SHOULDER_L) {
        raw_mask |= JC4880_JOYPAD_MASK_SHOULDER_L;
    }
    if (gp->buttons & BUTTON_SHOULDER_R) {
        raw_mask |= JC4880_JOYPAD_MASK_SHOULDER_R;
    }
    if ((gp->buttons & BUTTON_TRIGGER_L) || (gp->brake > AXIS_THRESHOLD)) {
        raw_mask |= JC4880_JOYPAD_MASK_TRIGGER_L;
    }
    if ((gp->buttons & BUTTON_TRIGGER_R) || (gp->throttle > AXIS_THRESHOLD)) {
        raw_mask |= JC4880_JOYPAD_MASK_TRIGGER_R;
    }
    if (gp->misc_buttons & MISC_BUTTON_SELECT) {
        raw_mask |= JC4880_JOYPAD_MASK_SELECT;
    }
    if (gp->misc_buttons & MISC_BUTTON_SYSTEM) {
        raw_mask |= JC4880_JOYPAD_MASK_SYSTEM;
    }
    if (gp->misc_buttons & MISC_BUTTON_CAPTURE) {
        raw_mask |= JC4880_JOYPAD_MASK_CAPTURE;
    }
    if (gp->buttons & BUTTON_THUMB_L) {
        raw_mask |= JC4880_JOYPAD_MASK_THUMB_L;
    }
    if (gp->buttons & BUTTON_THUMB_R) {
        raw_mask |= JC4880_JOYPAD_MASK_THUMB_R;
    }
    if (gp->axis_y < -AXIS_THRESHOLD) {
        raw_mask |= JC4880_JOYPAD_MASK_STICK_L_UP;
    }
    if (gp->axis_y > AXIS_THRESHOLD) {
        raw_mask |= JC4880_JOYPAD_MASK_STICK_L_DOWN;
    }
    if (gp->axis_x < -AXIS_THRESHOLD) {
        raw_mask |= JC4880_JOYPAD_MASK_STICK_L_LEFT;
    }
    if (gp->axis_x > AXIS_THRESHOLD) {
        raw_mask |= JC4880_JOYPAD_MASK_STICK_L_RIGHT;
    }
    if (gp->axis_ry < -AXIS_THRESHOLD) {
        raw_mask |= JC4880_JOYPAD_MASK_STICK_R_UP;
    }
    if (gp->axis_ry > AXIS_THRESHOLD) {
        raw_mask |= JC4880_JOYPAD_MASK_STICK_R_DOWN;
    }
    if (gp->axis_rx < -AXIS_THRESHOLD) {
        raw_mask |= JC4880_JOYPAD_MASK_STICK_R_LEFT;
    }
    if (gp->axis_rx > AXIS_THRESHOLD) {
        raw_mask |= JC4880_JOYPAD_MASK_STICK_R_RIGHT;
    }

    return raw_mask;
}

static void bluepad_platform_init(int argc, const char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
}

static void bluepad_platform_on_init_complete(void)
{
    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
        s_state.bluepad_ready = true;
        s_state.config_dirty = true;
        xSemaphoreGive(s_state_mutex);
    }
}

static uni_error_t bluepad_platform_on_device_discovered(bd_addr_t addr, const char *name, uint16_t cod, uint8_t rssi)
{
    ARG_UNUSED(name);
    ARG_UNUSED(cod);
    ARG_UNUSED(rssi);

    char addr_text[18] = {};
    copy_addr_string_from_bdaddr(addr_text, sizeof(addr_text), addr);

    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return UNI_ERROR_IGNORE_DEVICE;
    }

    const bool enabled = joypad_backend_enabled_locked();
    const bool discovery_enabled = discovery_enabled_locked();
    const bool accepted = enabled && (discovery_enabled || address_matches_config_locked(addr_text));
    xSemaphoreGive(s_state_mutex);

    return accepted ? UNI_ERROR_SUCCESS : UNI_ERROR_IGNORE_DEVICE;
}

static void bluepad_platform_on_device_connected(uni_hid_device_t *d)
{
    if (d == NULL) {
        return;
    }

    char addr_text[18] = {};
    copy_addr_string_from_bdaddr(addr_text, sizeof(addr_text), d->conn.btaddr);
    ESP_LOGI(TAG, "Controller link established: %s (%s)", d->name[0] != '\0' ? d->name : "unnamed", addr_text);
}

static void bluepad_platform_on_device_disconnected(uni_hid_device_t *d)
{
    const int device_idx = uni_hid_device_get_idx_for_instance(d);

    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
        if (device_idx == s_state.active_device_idx) {
            ESP_LOGI(TAG,
                     "Controller disconnected: %s (%s)",
                     s_state.active_device_name[0] != '\0' ? s_state.active_device_name : "unnamed",
                     s_state.active_device_addr[0] != '\0' ? s_state.active_device_addr : "unsaved");
            s_state.active_device_idx = -1;
            s_state.report_connected = false;
            s_state.last_raw_mask = 0;
            s_state.axis_x = 0;
            s_state.axis_y = 0;
            s_state.axis_rx = 0;
            s_state.axis_ry = 0;
            s_state.brake = 0;
            s_state.throttle = 0;
            s_state.active_device_addr[0] = '\0';
            s_state.active_device_name[0] = '\0';
            send_report_locked();
        }
        xSemaphoreGive(s_state_mutex);
    }
}

static uni_error_t bluepad_platform_on_device_ready(uni_hid_device_t *d)
{
    char addr_text[18] = {};
    copy_addr_string_from_bdaddr(addr_text, sizeof(addr_text), d->conn.btaddr);

    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return UNI_ERROR_INVALID_DEVICE;
    }

    if (!joypad_backend_enabled_locked() ||
        (!address_matches_config_locked(addr_text) && !discovery_enabled_locked())) {
        xSemaphoreGive(s_state_mutex);
        return UNI_ERROR_INVALID_DEVICE;
    }

    s_state.active_device_idx = uni_hid_device_get_idx_for_instance(d);
    s_state.report_connected = true;
    s_state.last_raw_mask = 0;
    s_state.axis_x = 0;
    s_state.axis_y = 0;
    s_state.axis_rx = 0;
    s_state.axis_ry = 0;
    s_state.brake = 0;
    s_state.throttle = 0;
    strlcpy(s_state.active_device_addr, addr_text, sizeof(s_state.active_device_addr));
    strlcpy(s_state.active_device_name, d->name, sizeof(s_state.active_device_name));
    ESP_LOGI(TAG,
             "Controller ready: %s (%s)",
             s_state.active_device_name[0] != '\0' ? s_state.active_device_name : "unnamed",
             s_state.active_device_addr);
    send_report_locked();
    xSemaphoreGive(s_state_mutex);

    return UNI_ERROR_SUCCESS;
}

static void bluepad_platform_on_controller_data(uni_hid_device_t *d, uni_controller_t *ctl)
{
    if ((ctl == NULL) || (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD)) {
        return;
    }

    const int device_idx = uni_hid_device_get_idx_for_instance(d);
    const uint32_t raw_mask = gamepad_to_raw_mask(&ctl->gamepad);

    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
        if (device_idx == s_state.active_device_idx) {
            const bool changed = (s_state.last_raw_mask != raw_mask) ||
                                 (s_state.axis_x != ctl->gamepad.axis_x) ||
                                 (s_state.axis_y != ctl->gamepad.axis_y) ||
                                 (s_state.axis_rx != ctl->gamepad.axis_rx) ||
                                 (s_state.axis_ry != ctl->gamepad.axis_ry) ||
                                 (s_state.brake != ctl->gamepad.brake) ||
                                 (s_state.throttle != ctl->gamepad.throttle);
            s_state.last_raw_mask = raw_mask;
            s_state.axis_x = (int16_t)ctl->gamepad.axis_x;
            s_state.axis_y = (int16_t)ctl->gamepad.axis_y;
            s_state.axis_rx = (int16_t)ctl->gamepad.axis_rx;
            s_state.axis_ry = (int16_t)ctl->gamepad.axis_ry;
            s_state.brake = (uint16_t)ctl->gamepad.brake;
            s_state.throttle = (uint16_t)ctl->gamepad.throttle;
            strlcpy(s_state.active_device_name, d->name, sizeof(s_state.active_device_name));
            if (changed) {
                ESP_LOGI(TAG,
                         "Controller data: raw=0x%08" PRIx32 " device=%s",
                         raw_mask,
                         s_state.active_device_name[0] != '\0' ? s_state.active_device_name : "unnamed");
                ESP_LOGI(TAG,
                         "Controller parsed: dpad=0x%02x buttons=0x%04x misc=0x%02x axis=(%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 ") pedals=(%" PRId32 ",%" PRId32 ") device=%s",
                         ctl->gamepad.dpad,
                         ctl->gamepad.buttons,
                         ctl->gamepad.misc_buttons,
                         ctl->gamepad.axis_x,
                         ctl->gamepad.axis_y,
                         ctl->gamepad.axis_rx,
                         ctl->gamepad.axis_ry,
                         ctl->gamepad.brake,
                         ctl->gamepad.throttle,
                         s_state.active_device_name[0] != '\0' ? s_state.active_device_name : "unnamed");
            }
            send_report_locked();
        }
        xSemaphoreGive(s_state_mutex);
    }
}

static const uni_property_t *bluepad_platform_get_property(uni_property_idx_t idx)
{
    ARG_UNUSED(idx);
    return NULL;
}

static void bluepad_platform_on_oob_event(uni_platform_oob_event_t event, void *data)
{
    if (event != UNI_PLATFORM_OOB_BLUETOOTH_ENABLED) {
        return;
    }

    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
        s_state.report_scanning = data != NULL;
        ESP_LOGI(TAG, "Bluetooth scan state: %s", s_state.report_scanning ? "running" : "idle");
        send_report_locked();
        xSemaphoreGive(s_state_mutex);
    }
}

static struct uni_platform *get_bluepad_platform(void)
{
    static struct uni_platform platform = {
        .name = "jc4880-c6",
        .init = bluepad_platform_init,
        .on_init_complete = bluepad_platform_on_init_complete,
        .on_device_discovered = bluepad_platform_on_device_discovered,
        .on_device_connected = bluepad_platform_on_device_connected,
        .on_device_disconnected = bluepad_platform_on_device_disconnected,
        .on_device_ready = bluepad_platform_on_device_ready,
        .on_gamepad_data = NULL,
        .on_controller_data = bluepad_platform_on_controller_data,
        .get_property = bluepad_platform_get_property,
        .on_oob_event = bluepad_platform_on_oob_event,
        .device_dump = NULL,
        .register_console_cmds = NULL,
    };
    return &platform;
}

static void bluepad32_task(void *arg)
{
    ARG_UNUSED(arg);

#ifdef CONFIG_ESP_CONSOLE_UART
#ifndef CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
    btstack_stdio_init();
#endif
#endif

    btstack_init();
    uni_platform_set_custom(get_bluepad_platform());
    uni_init(0, NULL);
    btstack_run_loop_execute();
    vTaskDelete(NULL);
}

static void joypad_management_task(void *arg)
{
    ARG_UNUSED(arg);

    for (;;) {
        bool ready = false;
        bool config_dirty = false;
        bool enabled = false;
        bool discovery_enabled = false;
        int active_device_idx = -1;

        if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
            ready = s_state.bluepad_ready;
            config_dirty = s_state.config_dirty;
            enabled = joypad_backend_enabled_locked();
            discovery_enabled = discovery_enabled_locked();
            active_device_idx = s_state.active_device_idx;
            if (ready && config_dirty) {
                s_state.config_dirty = false;
            }
            xSemaphoreGive(s_state_mutex);
        }

        if (!ready || !config_dirty) {
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }

        uni_bt_allow_incoming_connections(enabled);
        if (enabled && discovery_enabled) {
            ESP_LOGI(TAG, "Applying joypad config: starting BLE scan");
            uni_bt_start_scanning_and_autoconnect_safe();
        } else {
            ESP_LOGI(TAG,
                     "Applying joypad config: stopping BLE scan (enabled=%u discovery=%u saved_addr=%s)",
                     enabled ? 1u : 0u,
                     discovery_enabled ? 1u : 0u,
                     s_state.config.ble_device_addr[0] != '\0' ? s_state.config.ble_device_addr : "unsaved");
            uni_bt_stop_scanning_safe();
        }

        if (!enabled && (active_device_idx >= 0)) {
            uni_bt_disconnect_device_safe(active_device_idx);
        }

        if (!enabled && xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
            s_state.report_connected = false;
            s_state.report_scanning = false;
            s_state.last_raw_mask = 0;
            s_state.axis_x = 0;
            s_state.axis_y = 0;
            s_state.axis_rx = 0;
            s_state.axis_ry = 0;
            s_state.brake = 0;
            s_state.throttle = 0;
            s_state.active_device_idx = -1;
            s_state.active_device_addr[0] = '\0';
            s_state.active_device_name[0] = '\0';
            send_report_locked();
            xSemaphoreGive(s_state_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t c6_joypad_service_init(void)
{
    if (s_state_mutex != NULL) {
        return ESP_OK;
    }

    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t callback_err = esp_hosted_register_custom_callback(JC4880_JOYPAD_MSG_CONFIG, hosted_config_callback);
    if (callback_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register hosted joypad config callback: %s", esp_err_to_name(callback_err));
        return callback_err;
    }

    if (xTaskCreatePinnedToCore(joypad_management_task, "joypad_mgmt", 4096, NULL, 4, NULL, 0) != pdPASS) {
        return ESP_FAIL;
    }

    if (xTaskCreatePinnedToCore(bluepad32_task, "bluepad32", 8192, NULL, 5, NULL, 0) != pdPASS) {
        return ESP_FAIL;
    }

    return ESP_OK;
}