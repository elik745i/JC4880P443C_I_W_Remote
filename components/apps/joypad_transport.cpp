#include "joypad_transport.h"

#include <atomic>
#include <cstring>
#include <inttypes.h>

#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_hosted_event.h"
#include "esp_hosted_misc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "joypad_runtime.h"
#include "jc4880_joypad_protocol.h"

extern "C" uint8_t is_transport_tx_ready(void);

namespace {

constexpr const char *kTag = "JoypadTransport";
constexpr uint32_t kTaskStackSize = 4096;
constexpr TickType_t kConnectedDelay = pdMS_TO_TICKS(500);
constexpr TickType_t kDisconnectedDelay = pdMS_TO_TICKS(2000);

std::atomic<bool> s_initialized{false};
std::atomic<bool> s_configDirty{true};
std::atomic<bool> s_callbackRegistered{false};
std::atomic<bool> s_initialSyncSent{false};
std::atomic<bool> s_transportUp{false};
std::atomic<uint32_t> s_lastLoggedRawMask{UINT32_MAX};
std::atomic<int> s_lastLoggedFlags{-1};
std::atomic<int> s_lastLoggedAxisX{INT32_MIN};
std::atomic<int> s_lastLoggedAxisY{INT32_MIN};
std::atomic<int> s_lastLoggedAxisRx{INT32_MIN};
std::atomic<int> s_lastLoggedAxisRy{INT32_MIN};
std::atomic<int> s_lastLoggedBrake{-1};
std::atomic<int> s_lastLoggedThrottle{-1};

bool hosted_transport_ready()
{
    return is_transport_tx_ready() != 0;
}

void on_hosted_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base != ESP_HOSTED_EVENT) {
        return;
    }

    ESP_LOGI(kTag, "Hosted event: %" PRId32, event_id);

    if (event_id == ESP_HOSTED_EVENT_TRANSPORT_UP) {
        s_transportUp.store(true);
        s_initialSyncSent.store(false);
        s_configDirty.store(true);
        ESP_LOGI(kTag, "Hosted transport is up");
    } else if ((event_id == ESP_HOSTED_EVENT_TRANSPORT_DOWN) ||
               (event_id == ESP_HOSTED_EVENT_TRANSPORT_FAILURE)) {
        s_transportUp.store(false);
        s_initialSyncSent.store(false);
        s_configDirty.store(true);
        ESP_LOGW(kTag, "Hosted transport is down or failed");
    }
}

void log_report_change(const jc4880_joypad_report_t &report)
{
    const int flags = ((report.connected != 0) ? 0x1 : 0x0) | ((report.scanning != 0) ? 0x2 : 0x0);
    const uint32_t previous_mask = s_lastLoggedRawMask.exchange(report.raw_mask);
    const int previous_flags = s_lastLoggedFlags.exchange(flags);
    const int previous_axis_x = s_lastLoggedAxisX.exchange(report.axis_x);
    const int previous_axis_y = s_lastLoggedAxisY.exchange(report.axis_y);
    const int previous_axis_rx = s_lastLoggedAxisRx.exchange(report.axis_rx);
    const int previous_axis_ry = s_lastLoggedAxisRy.exchange(report.axis_ry);
    const int previous_brake = s_lastLoggedBrake.exchange(report.brake);
    const int previous_throttle = s_lastLoggedThrottle.exchange(report.throttle);
    if ((previous_mask == report.raw_mask) &&
        (previous_flags == flags) &&
        (previous_axis_x == report.axis_x) &&
        (previous_axis_y == report.axis_y) &&
        (previous_axis_rx == report.axis_rx) &&
        (previous_axis_ry == report.axis_ry) &&
        (previous_brake == report.brake) &&
        (previous_throttle == report.throttle)) {
        return;
    }

    const char *name = (report.device_name[0] != '\0') ? report.device_name : "unnamed";
    const char *addr = (report.device_addr[0] != '\0') ? report.device_addr : "unsaved";
    ESP_LOGI(kTag,
             "BLE report: connected=%u scanning=%u raw=0x%08" PRIx32 " axes=(%d,%d,%d,%d) pedals=(%u,%u) device=%s addr=%s",
             static_cast<unsigned>(report.connected),
             static_cast<unsigned>(report.scanning),
             report.raw_mask,
             static_cast<int>(report.axis_x),
             static_cast<int>(report.axis_y),
             static_cast<int>(report.axis_rx),
             static_cast<int>(report.axis_ry),
             static_cast<unsigned>(report.brake),
             static_cast<unsigned>(report.throttle),
             name,
             addr);
}

void on_joypad_config_changed(const jc4880_joypad_config_t *config, void *context)
{
    (void)config;
    (void)context;
    ESP_LOGI(kTag, "Joypad config changed");
    s_configDirty.store(true);
}

void on_joypad_report(uint32_t msg_id, const uint8_t *data, size_t data_len)
{
    (void)msg_id;
    if ((data == nullptr) || (data_len < sizeof(jc4880_joypad_report_t))) {
        ESP_LOGW(kTag, "Ignoring short joypad report: %u bytes", static_cast<unsigned>(data_len));
        return;
    }

    jc4880_joypad_report_t report = {};
    std::memcpy(&report, data, sizeof(report));
    if (report.version != JC4880_JOYPAD_PROTOCOL_VERSION) {
        ESP_LOGW(kTag, "Ignoring joypad report version %u", static_cast<unsigned>(report.version));
        return;
    }

    report.device_name[sizeof(report.device_name) - 1] = '\0';
    report.device_addr[sizeof(report.device_addr) - 1] = '\0';
    jc4880_joypad_set_ble_report(report.connected != 0,
                                 report.scanning != 0,
                                 report.device_name,
                                 report.device_addr,
                                 report.raw_mask,
                                 report.axis_x,
                                 report.axis_y,
                                 report.axis_rx,
                                 report.axis_ry,
                                 report.brake,
                                 report.throttle);
    log_report_change(report);

    if ((report.connected != 0) && (report.device_addr[0] != '\0')) {
        jc4880_joypad_config_t config = {};
        if (jc4880_joypad_get_config(&config) &&
            ((config.ble_discovery_enabled != 0) || (config.ble_device_addr[0] == '\0')) &&
            (std::strncmp(config.ble_device_addr, report.device_addr, sizeof(config.ble_device_addr)) != 0)) {
            std::strncpy(config.ble_device_addr, report.device_addr, sizeof(config.ble_device_addr) - 1);
            config.ble_device_addr[sizeof(config.ble_device_addr) - 1] = '\0';
            jc4880_joypad_set_config(&config);
        }
    }
}

bool send_config_to_coprocessor()
{
    jc4880_joypad_config_t config = {};
    if (!jc4880_joypad_get_config(&config)) {
        return false;
    }

    jc4880_joypad_config_msg_t message = {};
    message.version = JC4880_JOYPAD_PROTOCOL_VERSION;
    message.backend = config.backend;
    message.ble_enabled = config.ble_enabled;
    message.ble_discovery_enabled = config.ble_discovery_enabled;
    strlcpy(message.ble_device_addr, config.ble_device_addr, sizeof(message.ble_device_addr));

    ESP_LOGI(kTag,
             "Sending joypad config: backend=%u ble=%u discovery=%u addr=%s",
             static_cast<unsigned>(message.backend),
             static_cast<unsigned>(message.ble_enabled),
             static_cast<unsigned>(message.ble_discovery_enabled),
             (message.ble_device_addr[0] != '\0') ? message.ble_device_addr : "unsaved");

    const esp_err_t err = esp_hosted_send_custom_data(JC4880_JOYPAD_MSG_CONFIG,
                                                      reinterpret_cast<const uint8_t *>(&message),
                                                      sizeof(message));
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to push joypad config to ESP32-C6: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

void joypad_transport_task(void *arg)
{
    (void)arg;

    ESP_LOGI(kTag, "Joypad transport task started");

    for (;;) {
        const bool transport_up = hosted_transport_ready();
        const bool previous_transport_up = s_transportUp.exchange(transport_up);
        if (transport_up != previous_transport_up) {
            ESP_LOGI(kTag, "Observed hosted transport state: %s", transport_up ? "up" : "down");
            s_initialSyncSent.store(false);
            s_configDirty.store(true);
        }

        if (!s_callbackRegistered.load()) {
            const esp_err_t err = esp_hosted_register_custom_callback(JC4880_JOYPAD_MSG_REPORT, on_joypad_report);
            if (err != ESP_OK) {
                ESP_LOGW(kTag, "Failed to register joypad report callback: %s", esp_err_to_name(err));
                vTaskDelay(kDisconnectedDelay);
                continue;
            }
            s_callbackRegistered.store(true);
            ESP_LOGI(kTag, "Registered joypad report callback");
        }

        if (transport_up && (s_configDirty.exchange(false) || !s_initialSyncSent.load())) {
            if (send_config_to_coprocessor()) {
                s_initialSyncSent.store(true);
            } else {
                s_configDirty.store(true);
                s_initialSyncSent.store(false);
            }
        }

        vTaskDelay(s_transportUp.load() ? kConnectedDelay : kDisconnectedDelay);
    }
}

} // namespace

namespace joypad_transport {

bool initialize()
{
    if (s_initialized.exchange(true)) {
        return true;
    }

    ESP_LOGI(kTag, "Initializing joypad transport");

    const esp_err_t event_err = esp_event_handler_register(ESP_HOSTED_EVENT,
                                                           ESP_EVENT_ANY_ID,
                                                           &on_hosted_event,
                                                           nullptr);
    if ((event_err != ESP_OK) && (event_err != ESP_ERR_INVALID_STATE)) {
        s_initialized.store(false);
        ESP_LOGE(kTag, "Failed to register hosted event handler: %s", esp_err_to_name(event_err));
        return false;
    }

    jc4880_joypad_register_config_changed_callback(on_joypad_config_changed, nullptr);
    if (xTaskCreatePinnedToCore(joypad_transport_task,
                                "joypad_transport",
                                kTaskStackSize,
                                nullptr,
                                2,
                                nullptr,
                                1) != pdPASS) {
        s_initialized.store(false);
        ESP_LOGE(kTag, "Failed to start joypad transport task");
        return false;
    }

    return true;
}

void notify_config_changed()
{
    s_configDirty.store(true);
}

} // namespace joypad_transport