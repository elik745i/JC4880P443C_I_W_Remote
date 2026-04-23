/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cmath>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <limits>
#include <cstdio>
#include <cstdint>
#include <dirent.h>
#include <sstream>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_memory_utils.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "esp_hosted.h"
#include "esp_hosted_misc.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "cJSON.h"
#include "nvs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "ui/ui.h"
#include "Setting.hpp"
#include "wifi/SettingWifiPrivate.hpp"
#include "app_sntp.h"
#include "battery_history_service.h"
#include "hardware_history_service.h"
#include "storage_access.h"
#include "system_ui_service.h"

#include "esp_brookesia_versions.h"

#if CONFIG_JC4880_FEATURE_WIFI
#define APP_SETTINGS_FEATURE_WIFI 1
#else
#define APP_SETTINGS_FEATURE_WIFI 0
#endif

#if CONFIG_JC4880_FEATURE_BLUETOOTH || CONFIG_JC4880_FEATURE_BLE
#define APP_SETTINGS_FEATURE_BLUETOOTH_MENU 1
#else
#define APP_SETTINGS_FEATURE_BLUETOOTH_MENU 0
#endif

#if CONFIG_JC4880_FEATURE_DISPLAY || CONFIG_JC4880_FEATURE_TIME_SYNC
#define APP_SETTINGS_FEATURE_DISPLAY_MENU 1
#else
#define APP_SETTINGS_FEATURE_DISPLAY_MENU 0
#endif

#if CONFIG_JC4880_FEATURE_HARDWARE_INFO || CONFIG_JC4880_FEATURE_BATTERY
#define APP_SETTINGS_FEATURE_HARDWARE_MENU 1
#else
#define APP_SETTINGS_FEATURE_HARDWARE_MENU 0
#endif

#define HOME_REFRESH_TASK_STACK_SIZE    (1024 * 4)
#define HOME_REFRESH_TASK_PRIORITY      (1)
#define HOME_REFRESH_TASK_PERIOD_MS     (2000)

#define FIRMWARE_UPDATE_TASK_STACK_SIZE  (1024 * 10)
#define FIRMWARE_UPDATE_TASK_PRIORITY    (4)

#define SCREEN_BRIGHTNESS_MIN           (20)
#define SCREEN_BRIGHTNESS_MAX           (BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX)

#define SPEAKER_VOLUME_MIN              (0)
#define SPEAKER_VOLUME_MAX              (100)

#define NVS_STORAGE_NAMESPACE           "storage"
#define NVS_KEY_BLE_ENABLE              "ble_en"
#define NVS_KEY_BLE_DEVICE_NAME         "ble_name"
#define NVS_KEY_ZIGBEE_ENABLE           "zb_en"
#define NVS_KEY_ZIGBEE_CHANNEL          "zb_ch"
#define NVS_KEY_ZIGBEE_PERMIT_JOIN      "zb_join"
#define NVS_KEY_ZIGBEE_DEVICE_NAME      "zb_name"
#define NVS_KEY_AUDIO_VOLUME            "volume"
#define NVS_KEY_SYSTEM_AUDIO_VOLUME     "sys_volume"
#define NVS_KEY_DISPLAY_BRIGHTNESS      "brightness"
#define NVS_KEY_DISPLAY_ADAPTIVE        "disp_adapt"
#define NVS_KEY_DISPLAY_SCREENSAVER     "disp_saver"
#define NVS_KEY_DISPLAY_TIMEOFF         "disp_off_sec"
#define NVS_KEY_DISPLAY_SLEEP           "disp_sleep"
#define NVS_KEY_DISPLAY_TIMEZONE        "disp_tz_min"
#define NVS_KEY_DISPLAY_TZ_AUTO         "disp_tz_auto"
#define NVS_KEY_OTA_PENDING_VERSION     "ota_ver"
#define NVS_KEY_OTA_PENDING_NOTES       "ota_notes"
#define NVS_KEY_OTA_PENDING_SHOW        "ota_show"

#define UI_MAIN_ITEM_LEFT_OFFSET        (20)
#define UI_WIFI_LIST_UP_PAD             (20)
#define UI_WIFI_LIST_DOWN_PAD           (20)
#define UI_WIFI_LIST_H_PERCENT          (75)
#define UI_WIFI_KEYBOARD_H_PERCENT      (30)

#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_12
 
#define EXAMPLE_ADC2_CHAN0          ADC_CHANNEL_4
static int adc_raw[2][10];
static int voltage[2][10];

using namespace std;

static const char TAG[] = "EUI_Setting";
static constexpr lv_coord_t kBatteryCardCollapsedHeight = 146;
static constexpr lv_coord_t kBatteryCardExpandedHeight = 514;
static constexpr uint32_t kBatteryCardExpandAnimMs = 240;
static constexpr lv_coord_t kHardwareTrendCardCollapsedHeight = 146;
static constexpr lv_coord_t kHardwareTrendCardExpandedHeight = 514;
static constexpr uint32_t kHardwareTrendCardExpandAnimMs = 240;

static void *allocate_psram_preferred_buffer(size_t size)
{
    void *buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer != nullptr) {
        return buffer;
    }

    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void animateObjectHeight(void *target, int32_t value)
{
    if (target == nullptr) {
        return;
    }

    lv_obj_set_height(static_cast<lv_obj_t *>(target), value);
}

static void enableEventBubbleRecursively(lv_obj_t *object)
{
    if (!lv_obj_ready(object)) {
        return;
    }

    lv_obj_add_flag(object, LV_OBJ_FLAG_EVENT_BUBBLE);

    const uint32_t child_count = lv_obj_get_child_cnt(object);
    for (uint32_t child_index = 0; child_index < child_count; ++child_index) {
        lv_obj_t *child = lv_obj_get_child(object, static_cast<int32_t>(child_index));
        if (child != nullptr) {
            enableEventBubbleRecursively(child);
        }
    }
}

static lv_obj_t *create_monitor_card(lv_obj_t *parent, const char *title, const char *subtitle, lv_obj_t **value_label,
                                     lv_obj_t **detail_label, lv_obj_t **bar)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(card, 16, 0);
    lv_obj_set_style_pad_right(card, 16, 0);
    lv_obj_set_style_pad_top(card, 14, 0);
    lv_obj_set_style_pad_bottom(card, 14, 0);

    lv_obj_t *titleLabel = lv_label_create(card);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *valueLabel = lv_label_create(card);
    lv_label_set_text(valueLabel, "--");
    lv_obj_set_style_text_font(valueLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(valueLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_align(valueLabel, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *subtitleLabel = lv_label_create(card);
    lv_obj_set_width(subtitleLabel, lv_pct(100));
    lv_label_set_long_mode(subtitleLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(subtitleLabel, subtitle);
    lv_obj_set_style_text_font(subtitleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(subtitleLabel, lv_color_hex(0x64748B), 0);
    lv_obj_align_to(subtitleLabel, titleLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    lv_obj_t *barObj = lv_bar_create(card);
    lv_obj_set_size(barObj, lv_pct(100), 16);
    lv_bar_set_range(barObj, 0, 100);
    lv_bar_set_value(barObj, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(barObj, 8, 0);
    lv_obj_set_style_bg_color(barObj, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(barObj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(barObj, lv_color_hex(0x2563EB), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(barObj, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_align_to(barObj, subtitleLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);

    lv_obj_t *detailLabel = lv_label_create(card);
    lv_obj_set_width(detailLabel, lv_pct(100));
    lv_label_set_long_mode(detailLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(detailLabel, "Waiting for telemetry...");
    lv_obj_set_style_text_font(detailLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(detailLabel, lv_color_hex(0x475569), 0);
    lv_obj_align_to(detailLabel, barObj, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    if (value_label != nullptr) {
        *value_label = valueLabel;
    }
    if (detail_label != nullptr) {
        *detail_label = detailLabel;
    }
    if (bar != nullptr) {
        *bar = barObj;
    }

    return card;
}

static lv_obj_t *create_settings_toggle_row(lv_obj_t *parent, const char *title)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 72);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(row, 18, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(row, 18, 0);
    lv_obj_set_style_pad_right(row, 18, 0);
    lv_obj_set_style_pad_top(row, 10, 0);
    lv_obj_set_style_pad_bottom(row, 10, 0);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x111827), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

    return row;
}

static constexpr uint32_t kSettingScreenAnimTimeMs = 220;
static constexpr int kStatusBarBluetoothIconId = 0x424C45;
static constexpr int kStatusBarZigbeeIconId = 0x5A425A;

TaskHandle_t wifi_scan_handle_task;
static SemaphoreHandle_t s_ble_mutex;
static constexpr const char *kBleDisabledMessage = "BLE is off. Enable it to advertise from the ESP32-C6 radio.";
static constexpr const char *kBleUnsupportedMessage = "BLE is unavailable with the current ESP32-C6 firmware or hosted setup.";

namespace {

struct BleScanResult {
    std::string address;
    std::string name;
    int rssi;
};

constexpr const char *kBleDefaultDeviceName = "JC4880P443C Remote";
constexpr const char *kZigbeeDefaultDeviceName = "JC4880P443C ZigBee";
constexpr int32_t kBleScanDurationMs = 8000;
constexpr size_t kBleScanResultLimit = 8;
constexpr int32_t kBleStartupTimeoutMs = 8000;

enum class BleRuntimeState : uint8_t {
    Disabled = 0,
    Starting,
    Advertising,
    Error,
};

BleRuntimeState s_bleRuntimeState = BleRuntimeState::Disabled;
bool s_bleDesiredEnabled = false;
bool s_bleTransportReady = false;
bool s_bleControllerReady = false;
bool s_bleHostReady = false;
bool s_bleSynced = false;
bool s_bleAdvertising = false;
bool s_bleStopInProgress = false;
uint8_t s_bleOwnAddrType = BLE_OWN_ADDR_PUBLIC;
std::string s_bleStatusMessage = "BLE is off. Enable it to advertise from the ESP32-C6 radio.";
std::string s_bleConfiguredName = kBleDefaultDeviceName;
bool s_bleScanInProgress = false;
bool s_bleResumeAdvertisingAfterScan = false;
std::string s_bleScanStatus = "BLE discovery is idle.";
std::vector<BleScanResult> s_bleScanResults;
SemaphoreHandle_t s_bleHostStoppedSem = nullptr;
int64_t s_bleStartTimestampUs = 0;

extern "C" void ble_store_config_init(void);

static std::string bleFormatAddress(const ble_addr_t &addr)
{
    char buffer[18] = {0};
    snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
             addr.val[5], addr.val[4], addr.val[3], addr.val[2], addr.val[1], addr.val[0]);
    return std::string(buffer);
}

static std::string bleExtractNameFromPayload(const uint8_t *data, uint8_t length)
{
    if ((data == nullptr) || (length == 0)) {
        return std::string();
    }

    uint8_t offset = 0;
    while (offset < length) {
        const uint8_t field_length = data[offset];
        if (field_length == 0) {
            break;
        }

        if ((offset + field_length) >= length) {
            break;
        }

        const uint8_t field_type = data[offset + 1];
        if (((field_type == 0x08) || (field_type == 0x09)) && (field_length > 1)) {
            return std::string(reinterpret_cast<const char *>(&data[offset + 2]), field_length - 1);
        }

        offset += field_length + 1;
    }

    return std::string();
}

static void bleUpdateScanResultLocked(const ble_gap_disc_desc &desc)
{
    BleScanResult result = {
        .address = bleFormatAddress(desc.addr),
        .name = bleExtractNameFromPayload(desc.data, desc.length_data),
        .rssi = desc.rssi,
    };

    auto existing = std::find_if(s_bleScanResults.begin(), s_bleScanResults.end(),
                                 [&result](const BleScanResult &entry) {
                                     return entry.address == result.address;
                                 });
    if (existing != s_bleScanResults.end()) {
        if (!result.name.empty()) {
            existing->name = result.name;
        }
        existing->rssi = std::max(existing->rssi, result.rssi);
    } else {
        s_bleScanResults.push_back(result);
    }

    std::sort(s_bleScanResults.begin(), s_bleScanResults.end(),
              [](const BleScanResult &lhs, const BleScanResult &rhs) {
                  return lhs.rssi > rhs.rssi;
              });
    if (s_bleScanResults.size() > kBleScanResultLimit) {
        s_bleScanResults.resize(kBleScanResultLimit);
    }

    s_bleScanStatus = std::string("Scanning nearby devices: ") + std::to_string(s_bleScanResults.size()) + " found.";
}

static bool bleIsExpectedInitResult(esp_err_t err)
{
    return (err == ESP_OK) || (err == ESP_ERR_INVALID_STATE);
}

static bool bleLock(TickType_t timeout = pdMS_TO_TICKS(1000))
{
    if (s_ble_mutex == nullptr) {
        s_ble_mutex = xSemaphoreCreateMutex();
    }

    return (s_ble_mutex != nullptr) && (xSemaphoreTake(s_ble_mutex, timeout) == pdTRUE);
}

static void bleUnlock(void)
{
    if (s_ble_mutex != nullptr) {
        xSemaphoreGive(s_ble_mutex);
    }
}

static void bleSetStatusLocked(BleRuntimeState state, const std::string &message)
{
    s_bleRuntimeState = state;
    s_bleStatusMessage = message;
}

static void bleTeardownLocked(bool set_disabled_status)
{
    if (s_bleScanInProgress) {
        ble_gap_disc_cancel();
        s_bleScanInProgress = false;
    }

    s_bleResumeAdvertisingAfterScan = false;
    s_bleScanResults.clear();
    s_bleScanStatus = "BLE discovery is idle.";

    if (s_bleAdvertising) {
        ble_gap_adv_stop();
        s_bleAdvertising = false;
    }

    if (s_bleHostReady) {
        if (s_bleHostStoppedSem == nullptr) {
            s_bleHostStoppedSem = xSemaphoreCreateBinary();
        }
        if (s_bleHostStoppedSem != nullptr) {
            xSemaphoreTake(s_bleHostStoppedSem, 0);
        }

        s_bleStopInProgress = true;

        const int stop_rc = nimble_port_stop();
        if ((stop_rc != 0) && (stop_rc != BLE_HS_EALREADY)) {
            ESP_LOGW(TAG, "nimble_port_stop during teardown failed: %d", stop_rc);
        }

        if ((stop_rc == 0) || (stop_rc == BLE_HS_EALREADY)) {
            if ((s_bleHostStoppedSem != nullptr) &&
                (stop_rc == 0) &&
                (xSemaphoreTake(s_bleHostStoppedSem, pdMS_TO_TICKS(2000)) != pdTRUE)) {
                ESP_LOGW(TAG, "Timed out waiting for NimBLE host task shutdown");
            }

            nimble_port_freertos_deinit();
        }

        esp_err_t err = nimble_port_deinit();
        if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
            ESP_LOGW(TAG, "nimble_port_deinit during teardown failed: %s", esp_err_to_name(err));
        }

        s_bleHostReady = false;
        s_bleAdvertising = false;
        s_bleSynced = false;
        s_bleStopInProgress = false;
    }

    s_bleStartTimestampUs = 0;

    if (s_bleControllerReady) {
        esp_err_t err = esp_hosted_bt_controller_disable();
        if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
            ESP_LOGW(TAG, "Hosted BT controller disable during teardown failed: %s", esp_err_to_name(err));
        }

        err = esp_hosted_bt_controller_deinit(false);
        if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
            ESP_LOGW(TAG, "Hosted BT controller deinit during teardown failed: %s", esp_err_to_name(err));
        }

        s_bleControllerReady = false;
    }

    s_bleTransportReady = false;

    if (set_disabled_status) {
        bleSetStatusLocked(BleRuntimeState::Disabled, kBleDisabledMessage);
    }
}

static void bleCheckStartupTimeout(void)
{
    if (!bleLock(pdMS_TO_TICKS(10))) {
        return;
    }

    const bool timed_out = s_bleDesiredEnabled &&
                           (s_bleRuntimeState == BleRuntimeState::Starting) &&
                           !s_bleSynced &&
                           (s_bleStartTimestampUs != 0) &&
                           ((esp_timer_get_time() - s_bleStartTimestampUs) >= (static_cast<int64_t>(kBleStartupTimeoutMs) * 1000));

    if (timed_out) {
        bleSetStatusLocked(BleRuntimeState::Error,
                           "BLE startup timed out. Toggle BLE again or reset the device if the ESP32-C6 radio is stuck.");
        bleTeardownLocked(false);
        s_bleDesiredEnabled = false;
    }

    bleUnlock();
}

static const char *bleCurrentDeviceNameLocked(void)
{
    return s_bleConfiguredName.empty() ? kBleDefaultDeviceName : s_bleConfiguredName.c_str();
}

static esp_err_t bleHandleHostedUnsupportedLocked(const char *context)
{
    s_bleDesiredEnabled = false;
    s_bleAdvertising = false;
    s_bleSynced = false;
    bleSetStatusLocked(BleRuntimeState::Error,
                       std::string(context != nullptr ? context : "BLE is unavailable") +
                           ": " + kBleUnsupportedMessage +
                           " The ESP32-C6 likely does not expose hosted BLE in its current firmware.");
    return ESP_ERR_NOT_SUPPORTED;
}

static int bleGapEvent(struct ble_gap_event *event, void *arg);

static esp_err_t bleAdvertiseLocked(void)
{
    if (!s_bleDesiredEnabled) {
        bleSetStatusLocked(BleRuntimeState::Disabled, kBleDisabledMessage);
        return ESP_OK;
    }

    if (!s_bleSynced) {
        bleSetStatusLocked(BleRuntimeState::Starting, "Connecting to the ESP32-C6 and waiting for the BLE host stack to sync.");
        return ESP_OK;
    }

    if (s_bleAdvertising) {
        bleSetStatusLocked(BleRuntimeState::Advertising, std::string("BLE is advertising as '") + bleCurrentDeviceNameLocked() + "'.");
        return ESP_OK;
    }

    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char *name = ble_svc_gap_device_name();
    fields.name = reinterpret_cast<uint8_t *>(const_cast<char *>(name));
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        bleSetStatusLocked(BleRuntimeState::Error,
                           std::string("Failed to publish BLE advertisement data (rc=") + std::to_string(rc) + ").");
        return ESP_FAIL;
    }

    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_bleOwnAddrType, nullptr, BLE_HS_FOREVER, &adv_params, bleGapEvent, nullptr);
    if (rc != 0) {
        bleSetStatusLocked(BleRuntimeState::Error,
                           std::string("Failed to start BLE advertising (rc=") + std::to_string(rc) + ").");
        return ESP_FAIL;
    }

    s_bleAdvertising = true;
    bleSetStatusLocked(BleRuntimeState::Advertising, std::string("BLE is advertising as '") + bleCurrentDeviceNameLocked() + "'.");
    return ESP_OK;
}

static void bleStopAdvertisingLocked(void)
{
    bleTeardownLocked(true);
}

static void bleOnReset(int reason)
{
    if (bleLock()) {
        s_bleSynced = false;
        s_bleAdvertising = false;
        bleSetStatusLocked(BleRuntimeState::Error,
                           std::string("BLE host reset (reason=") + std::to_string(reason) + "). Toggle BLE to retry.");
        bleUnlock();
    }
}

static void bleOnSync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        if (bleLock()) {
            bleSetStatusLocked(BleRuntimeState::Error,
                               std::string("BLE address setup failed (rc=") + std::to_string(rc) + ").");
            bleUnlock();
        }
        return;
    }

    rc = ble_hs_id_infer_auto(0, &s_bleOwnAddrType);
    if (rc != 0) {
        if (bleLock()) {
            bleSetStatusLocked(BleRuntimeState::Error,
                               std::string("BLE address selection failed (rc=") + std::to_string(rc) + ").");
            bleUnlock();
        }
        return;
    }

    if (bleLock()) {
        s_bleSynced = true;
        s_bleStartTimestampUs = 0;
        if (s_bleDesiredEnabled) {
            bleAdvertiseLocked();
        } else {
            bleSetStatusLocked(BleRuntimeState::Disabled, "BLE is off. Enable it to advertise from the ESP32-C6 radio.");
        }
        bleUnlock();
    }
}

static void bleHostTask(void *param)
{
    (void)param;
    nimble_port_run();

    if (s_bleHostStoppedSem != nullptr) {
        xSemaphoreGive(s_bleHostStoppedSem);
    }

    if (s_bleStopInProgress) {
        vTaskSuspend(nullptr);
    }

    if (bleLock()) {
        s_bleHostReady = false;
        s_bleSynced = false;
        s_bleAdvertising = false;
        if (s_bleDesiredEnabled) {
            bleSetStatusLocked(BleRuntimeState::Error, "BLE host stopped unexpectedly. Toggle BLE to restart it.");
        }
        bleUnlock();
    }

    vTaskDelete(nullptr);
}

static int bleGapEvent(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    if (!bleLock()) {
        return 0;
    }

    switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            if (s_bleScanInProgress) {
                bleUpdateScanResultLocked(event->disc);
            }
            break;

        case BLE_GAP_EVENT_DISC_COMPLETE:
            s_bleScanInProgress = false;
            if (s_bleScanResults.empty()) {
                s_bleScanStatus = "No nearby BLE devices found.";
            } else {
                s_bleScanStatus = std::string("Scan complete: ") + std::to_string(s_bleScanResults.size()) + " found.";
            }
            if (s_bleResumeAdvertisingAfterScan && s_bleDesiredEnabled) {
                s_bleResumeAdvertisingAfterScan = false;
                bleAdvertiseLocked();
            } else {
                s_bleResumeAdvertisingAfterScan = false;
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            s_bleAdvertising = false;
            if (s_bleDesiredEnabled) {
                bleAdvertiseLocked();
            } else {
                bleSetStatusLocked(BleRuntimeState::Disabled, kBleDisabledMessage);
            }
            break;

        default:
            break;
    }

    bleUnlock();
    return 0;
}

static esp_err_t bleSetEnabled(bool enabled)
{
    if (!bleLock()) {
        return ESP_ERR_TIMEOUT;
    }

    s_bleDesiredEnabled = enabled;

    if (!enabled) {
        bleStopAdvertisingLocked();
        bleUnlock();
        return ESP_OK;
    }

    bleSetStatusLocked(BleRuntimeState::Starting, "Connecting to the ESP32-C6 and starting BLE.");
    s_bleStartTimestampUs = esp_timer_get_time();

    int ret = esp_hosted_connect_to_slave();
    if (ret != ESP_OK) {
        bleSetStatusLocked(BleRuntimeState::Error,
                           std::string("Failed to reach the ESP32-C6 radio (err=") + std::to_string(ret) + ").");
        bleUnlock();
        return ESP_FAIL;
    }
    s_bleTransportReady = true;

    if (!s_bleControllerReady) {
        esp_err_t err = esp_hosted_bt_controller_init();
        if (err == ESP_ERR_NOT_SUPPORTED) {
            err = bleHandleHostedUnsupportedLocked("Hosted BLE controller init unavailable");
            bleUnlock();
            return err;
        }
        if (!bleIsExpectedInitResult(err)) {
            bleSetStatusLocked(BleRuntimeState::Error,
                               std::string("Hosted BT controller init failed: ") + esp_err_to_name(err));
            bleTeardownLocked(false);
            s_bleDesiredEnabled = false;
            bleUnlock();
            return err;
        }

        err = esp_hosted_bt_controller_enable();
        if (err == ESP_ERR_NOT_SUPPORTED) {
            err = bleHandleHostedUnsupportedLocked("Hosted BLE controller enable unavailable");
            bleUnlock();
            return err;
        }
        if (!bleIsExpectedInitResult(err)) {
            bleSetStatusLocked(BleRuntimeState::Error,
                               std::string("Hosted BT controller enable failed: ") + esp_err_to_name(err));
            bleTeardownLocked(false);
            s_bleDesiredEnabled = false;
            bleUnlock();
            return err;
        }

        s_bleControllerReady = true;
    }

    if (!s_bleHostReady) {
        esp_err_t err = nimble_port_init();
        if (!bleIsExpectedInitResult(err)) {
            bleSetStatusLocked(BleRuntimeState::Error,
                               std::string("NimBLE host init failed: ") + esp_err_to_name(err));
            bleTeardownLocked(false);
            s_bleDesiredEnabled = false;
            bleUnlock();
            return err;
        }

        ble_hs_cfg.reset_cb = bleOnReset;
        ble_hs_cfg.sync_cb = bleOnSync;

        ble_svc_gap_init();
        ble_svc_gatt_init();
        ble_store_config_init();

        int rc = ble_svc_gap_device_name_set(bleCurrentDeviceNameLocked());
        if (rc != 0) {
            bleSetStatusLocked(BleRuntimeState::Error,
                               std::string("Failed to set BLE device name (rc=") + std::to_string(rc) + ").");
            bleTeardownLocked(false);
            s_bleDesiredEnabled = false;
            bleUnlock();
            return ESP_FAIL;
        }

        nimble_port_freertos_init(bleHostTask);
        s_bleHostReady = true;
    }

    esp_err_t err = bleAdvertiseLocked();
    bleUnlock();
    return err;
}

static std::string bleStatusText(bool preference_enabled)
{
    if (!preference_enabled && (s_bleRuntimeState == BleRuntimeState::Disabled)) {
        return kBleDisabledMessage;
    }

    return s_bleStatusMessage;
}

static esp_err_t bleStartScanLocked(void)
{
    if (!s_bleDesiredEnabled) {
        s_bleScanStatus = "Enable BLE first to scan nearby devices.";
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_bleHostReady || !s_bleSynced) {
        s_bleScanStatus = "BLE is still starting. Wait for advertising, then scan again.";
        return ESP_ERR_INVALID_STATE;
    }

    if (s_bleScanInProgress) {
        return ESP_OK;
    }

    if (s_bleAdvertising) {
        ble_gap_adv_stop();
        s_bleAdvertising = false;
        s_bleResumeAdvertisingAfterScan = true;
    } else {
        s_bleResumeAdvertisingAfterScan = false;
    }

    s_bleScanResults.clear();
    s_bleScanInProgress = true;
    s_bleScanStatus = "Scanning nearby BLE devices for 8 seconds...";

    struct ble_gap_disc_params params = {};
    params.passive = 0;
    params.itvl = 0;
    params.window = 0;
    params.filter_duplicates = 1;
    params.limited = 0;

    const int rc = ble_gap_disc(s_bleOwnAddrType, kBleScanDurationMs, &params, bleGapEvent, nullptr);
    if (rc != 0) {
        s_bleScanInProgress = false;
        s_bleScanStatus = std::string("BLE scan failed to start (rc=") + std::to_string(rc) + ").";
        if (s_bleResumeAdvertisingAfterScan && s_bleDesiredEnabled) {
            s_bleResumeAdvertisingAfterScan = false;
            bleAdvertiseLocked();
        }
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t bleStartScan(void)
{
    if (!bleLock()) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = bleStartScanLocked();
    bleUnlock();
    return err;
}

static void bleCancelScan(void)
{
    if (!bleLock()) {
        return;
    }

    if (s_bleScanInProgress) {
        ble_gap_disc_cancel();
        s_bleScanInProgress = false;
        s_bleScanStatus = "BLE scan stopped.";
    }

    if (s_bleResumeAdvertisingAfterScan && s_bleDesiredEnabled) {
        s_bleResumeAdvertisingAfterScan = false;
        bleAdvertiseLocked();
    }

    bleUnlock();
}

static esp_err_t bleUpdateConfiguredName(const std::string &name)
{
    if (!bleLock()) {
        return ESP_ERR_TIMEOUT;
    }

    s_bleConfiguredName = name.empty() ? std::string(kBleDefaultDeviceName) : name;

    if (s_bleHostReady) {
        const int rc = ble_svc_gap_device_name_set(bleCurrentDeviceNameLocked());
        if (rc != 0) {
            bleUnlock();
            return ESP_FAIL;
        }
    }

    if (s_bleAdvertising) {
        ble_gap_adv_stop();
        s_bleAdvertising = false;
        bleAdvertiseLocked();
    }

    bleUnlock();
    return ESP_OK;
}

} // namespace

static uint8_t base_mac_addr[6] = {0};
static char mac_str[18] = {0};

static int brightness;

static constexpr int32_t kDisplayTimeoffOptionsSec[] = {0, 15, 30, 60, 120, 300};
static constexpr char kDisplayTimeoffOptionsText[] = "Off\n15 sec\n30 sec\n1 min\n2 min\n5 min";
static constexpr int32_t kDisplaySleepOptionsSec[] = {0, 30, 60, 120, 300, 600, 1800};
static constexpr char kDisplaySleepOptionsText[] = "Off\n30 sec\n1 min\n2 min\n5 min\n10 min\n30 min";
static constexpr int32_t kZigbeeChannelOptions[] = {0, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26};
static constexpr char kZigbeeChannelOptionsText[] = "Auto\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26";
static constexpr int32_t kZigbeePermitJoinOptionsSec[] = {0, 60, 180, 255};
static constexpr char kZigbeePermitJoinOptionsText[] = "Disabled\n60 sec\n180 sec\nAlways";
static constexpr const char *kFirmwareGithubReleasesUrl = "https://api.github.com/repos/elik745i/JC4880P443C_I_W_Remote/releases";
static constexpr const char *kFirmwareSdDirectory = "/sdcard/firmware";
static constexpr const char *kFirmwareUnknownVersion = "unknown";
static constexpr const char *kSdCardMountPoint = "/sdcard";

static bool settings_ui_is_ready(void)
{
    if ((ui_ScreenSettingMain == nullptr) || !lv_obj_is_valid(ui_ScreenSettingMain)) {
        return false;
    }
#if APP_SETTINGS_FEATURE_WIFI
    if ((ui_ScreenSettingWiFi == nullptr) || !lv_obj_is_valid(ui_ScreenSettingWiFi) ||
        (ui_ScreenSettingVerification == nullptr) || !lv_obj_is_valid(ui_ScreenSettingVerification)) {
        return false;
    }
#endif
#if CONFIG_JC4880_FEATURE_AUDIO
    if ((ui_ScreenSettingVolume == nullptr) || !lv_obj_is_valid(ui_ScreenSettingVolume)) {
        return false;
    }
#endif
#if APP_SETTINGS_FEATURE_DISPLAY_MENU
    if ((ui_ScreenSettingLight == nullptr) || !lv_obj_is_valid(ui_ScreenSettingLight)) {
        return false;
    }
#endif
#if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    if ((ui_ScreenSettingBLE == nullptr) || !lv_obj_is_valid(ui_ScreenSettingBLE)) {
        return false;
    }
#endif
#if CONFIG_JC4880_FEATURE_ABOUT_DEVICE
    if ((ui_ScreenSettingAbout == nullptr) || !lv_obj_is_valid(ui_ScreenSettingAbout)) {
        return false;
    }
#endif

    return true;
}

bool lv_obj_ready(lv_obj_t *obj)
{
    return (obj != nullptr) && lv_obj_is_valid(obj);
}

static std::string zigbeeChannelPreferenceLabel(int32_t channel)
{
    if (channel <= 0) {
        return "Auto / firmware default";
    }

    return std::string("Channel ") + std::to_string(channel);
}

static std::string zigbeePermitJoinLabel(int32_t permit_join_seconds)
{
    if (permit_join_seconds <= 0) {
        return "Disabled";
    }

    if (permit_join_seconds == 255) {
        return "Always open";
    }

    return std::to_string(permit_join_seconds) + " sec";
}

BaseType_t create_background_task_prefer_psram(TaskFunction_t task,
                                               const char *name,
                                               const uint32_t stack_depth,
                                               void *arg,
                                               const UBaseType_t priority,
                                               TaskHandle_t *task_handle,
                                               const BaseType_t core_id)
{
    if (xTaskCreatePinnedToCoreWithCaps(task,
                                        name,
                                        stack_depth,
                                        arg,
                                        priority,
                                        task_handle,
                                        core_id,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        return pdPASS;
    }

    ESP_LOGW(TAG,
             "Falling back to internal RAM stack for %s. Internal free=%u largest=%u PSRAM free=%u",
             name,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    return xTaskCreatePinnedToCore(task, name, stack_depth, arg, priority, task_handle, core_id);
}

static string formatStorageAmount(uint64_t bytes)
{
    const uint64_t kib = bytes / 1024ULL;
    const uint64_t mib = bytes / (1024ULL * 1024ULL);
    const uint64_t gib = bytes / (1024ULL * 1024ULL * 1024ULL);

    if (gib >= 1ULL) {
        const uint64_t unit = 1024ULL * 1024ULL * 1024ULL;
        const uint64_t whole = bytes / unit;
        const uint64_t fractional = ((bytes % unit) * 100ULL) / unit;
        string text = std::to_string(static_cast<unsigned long long>(whole));
        text += ".";
        if (fractional < 10ULL) {
            text += "0";
        }
        text += std::to_string(static_cast<unsigned long long>(fractional));
        text += " GB";
        return text;
    } else if (mib >= 1ULL) {
        const uint64_t unit = 1024ULL * 1024ULL;
        const uint64_t whole = bytes / unit;
        const uint64_t fractional = ((bytes % unit) * 10ULL) / unit;
        return std::to_string(static_cast<unsigned long long>(whole)) + "." +
               std::to_string(static_cast<unsigned long long>(fractional)) + " MB";
    }

    return std::to_string(static_cast<unsigned long long>(kib)) + " KB";
}

static int32_t calculatePercent(uint64_t used, uint64_t total)
{
    if (total == 0) {
        return 0;
    }

    return static_cast<int32_t>(std::min<uint64_t>(100, (used * 100) / total));
}

static lv_color_t getMonitorBarColor(int32_t percent)
{
    if (percent >= 85) {
        return lv_color_hex(0xDC2626);
    }

    if (percent >= 65) {
        return lv_color_hex(0xF59E0B);
    }

    return lv_color_hex(0x2563EB);
}

static string formatUptime(uint64_t uptime_seconds)
{
    const uint64_t days = uptime_seconds / 86400;
    const uint64_t hours = (uptime_seconds % 86400) / 3600;
    const uint64_t minutes = (uptime_seconds % 3600) / 60;

    if (days > 0) {
        string text = std::to_string(static_cast<unsigned long long>(days)) + "d ";
        if (hours < 10ULL) {
            text += "0";
        }
        text += std::to_string(static_cast<unsigned long long>(hours)) + "h ";
        if (minutes < 10ULL) {
            text += "0";
        }
        text += std::to_string(static_cast<unsigned long long>(minutes)) + "m";
        return text;
    }

    string text;
    if (hours < 10ULL) {
        text += "0";
    }
    text += std::to_string(static_cast<unsigned long long>(hours)) + "h ";
    if (minutes < 10ULL) {
        text += "0";
    }
    text += std::to_string(static_cast<unsigned long long>(minutes)) + "m";
    return text;
}

static string formatPercentUsed(int32_t percent)
{
    return std::to_string(static_cast<int>(percent)) + "% used";
}

static string formatDurationMinutes(int32_t total_minutes)
{
    if (total_minutes <= 0) {
        return "under 1 min";
    }

    const int32_t days = total_minutes / (24 * 60);
    const int32_t hours = (total_minutes % (24 * 60)) / 60;
    const int32_t minutes = total_minutes % 60;
    string text;
    if (days > 0) {
        text += std::to_string(days) + "d ";
    }
    if ((hours > 0) || (days > 0)) {
        text += std::to_string(hours) + "h ";
    }
    text += std::to_string(minutes) + "m";
    return text;
}

static string formatLookbackMinutes(int32_t total_minutes)
{
    if (total_minutes <= 0) {
        return "Now";
    }

    return formatDurationMinutes(total_minutes) + " ago";
}

static lv_color_t getBatteryBarColor(int32_t percent, bool charging)
{
    if (charging) {
        return lv_color_hex(0x16A34A);
    }

    if (percent <= 20) {
        return lv_color_hex(0xDC2626);
    }

    if (percent <= 45) {
        return lv_color_hex(0xF59E0B);
    }

    return lv_color_hex(0x2563EB);
}

static string formatSignedWithUnit(int32_t value, const char *unit)
{
    string text = std::to_string(static_cast<int>(value));
    if ((unit != nullptr) && (unit[0] != '\0')) {
        text += " ";
        text += unit;
    }
    return text;
}

static string formatTemperatureCelsius(float temperature_celsius)
{
    const int32_t temp_tenths = static_cast<int32_t>((temperature_celsius * 10.0f) + ((temperature_celsius >= 0.0f) ? 0.5f : -0.5f));
    const int32_t whole = temp_tenths / 10;
    const int32_t fractional = std::abs(temp_tenths % 10);
    return std::to_string(static_cast<int>(whole)) + "." + std::to_string(static_cast<int>(fractional)) + " C";
}

struct TimezoneOption {
    int32_t offset_minutes;
    const char *label;
    const char *tz;
};

static constexpr TimezoneOption kTimezoneOptions[] = {
    {-720, "GMT-12:00", "UTC+12"},
    {-660, "GMT-11:00", "UTC+11"},
    {-600, "GMT-10:00", "UTC+10"},
    {-570, "GMT-09:30", "UTC+9:30"},
    {-540, "GMT-09:00", "UTC+9"},
    {-480, "GMT-08:00", "UTC+8"},
    {-420, "GMT-07:00", "UTC+7"},
    {-360, "GMT-06:00", "UTC+6"},
    {-300, "GMT-05:00", "UTC+5"},
    {-240, "GMT-04:00", "UTC+4"},
    {-210, "GMT-03:30", "UTC+3:30"},
    {-180, "GMT-03:00", "UTC+3"},
    {-120, "GMT-02:00", "UTC+2"},
    {-60, "GMT-01:00", "UTC+1"},
    {0, "GMT+00:00", "UTC0"},
    {60, "GMT+01:00", "UTC-1"},
    {120, "GMT+02:00", "UTC-2"},
    {180, "GMT+03:00", "UTC-3"},
    {210, "GMT+03:30", "UTC-3:30"},
    {240, "GMT+04:00", "UTC-4"},
    {270, "GMT+04:30", "UTC-4:30"},
    {300, "GMT+05:00", "UTC-5"},
    {330, "GMT+05:30", "UTC-5:30"},
    {345, "GMT+05:45", "UTC-5:45"},
    {360, "GMT+06:00", "UTC-6"},
    {390, "GMT+06:30", "UTC-6:30"},
    {420, "GMT+07:00", "UTC-7"},
    {480, "GMT+08:00", "UTC-8"},
    {525, "GMT+08:45", "UTC-8:45"},
    {540, "GMT+09:00", "UTC-9"},
    {570, "GMT+09:30", "UTC-9:30"},
    {600, "GMT+10:00", "UTC-10"},
    {630, "GMT+10:30", "UTC-10:30"},
    {660, "GMT+11:00", "UTC-11"},
    {720, "GMT+12:00", "UTC-12"},
    {765, "GMT+12:45", "UTC-12:45"},
    {780, "GMT+13:00", "UTC-13"},
    {840, "GMT+14:00", "UTC-14"},
};

static const char *kTimezoneOptionsText =
    "GMT-12:00\nGMT-11:00\nGMT-10:00\nGMT-09:30\nGMT-09:00\nGMT-08:00\nGMT-07:00\nGMT-06:00\nGMT-05:00\nGMT-04:00\nGMT-03:30\nGMT-03:00\nGMT-02:00\nGMT-01:00\nGMT+00:00\nGMT+01:00\nGMT+02:00\nGMT+03:00\nGMT+03:30\nGMT+04:00\nGMT+04:30\nGMT+05:00\nGMT+05:30\nGMT+05:45\nGMT+06:00\nGMT+06:30\nGMT+07:00\nGMT+08:00\nGMT+08:45\nGMT+09:00\nGMT+09:30\nGMT+10:00\nGMT+10:30\nGMT+11:00\nGMT+12:00\nGMT+12:45\nGMT+13:00\nGMT+14:00";

static constexpr const char *kTimezoneLookupUrl = "https://ipapi.co/json/";

static size_t kTimezoneOptionCount = sizeof(kTimezoneOptions) / sizeof(kTimezoneOptions[0]);

static uint16_t findDropdownIndexForValue(const int32_t *values, size_t value_count, int32_t value)
{
    for (size_t index = 0; index < value_count; ++index) {
        if (values[index] == value) {
            return static_cast<uint16_t>(index);
        }
    }

    return 0;
}

static int32_t getDropdownValueForIndex(const int32_t *values, size_t value_count, uint16_t index)
{
    if (index >= value_count) {
        return values[0];
    }

    return values[index];
}

static uint16_t findTimezoneDropdownIndexForOffset(int32_t offset_minutes)
{
    for (size_t index = 0; index < kTimezoneOptionCount; ++index) {
        if (kTimezoneOptions[index].offset_minutes == offset_minutes) {
            return static_cast<uint16_t>(index);
        }
    }

    return 0;
}

static const TimezoneOption &getTimezoneOptionForIndex(uint16_t index)
{
    if (index >= kTimezoneOptionCount) {
        return kTimezoneOptions[findTimezoneDropdownIndexForOffset(480)];
    }

    return kTimezoneOptions[index];
}

static const TimezoneOption &getTimezoneOptionForOffset(int32_t offset_minutes)
{
    return getTimezoneOptionForIndex(findTimezoneDropdownIndexForOffset(offset_minutes));
}

static bool parseUtcOffsetMinutes(const std::string &offset_text, int32_t &minutes)
{
    if (offset_text.size() < 3) {
        return false;
    }

    const char sign = offset_text[0];
    if ((sign != '+') && (sign != '-')) {
        return false;
    }

    std::string digits;
    for (size_t index = 1; index < offset_text.size(); ++index) {
        const char ch = offset_text[index];
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            digits.push_back(ch);
        }
    }

    if ((digits.size() != 2) && (digits.size() != 4)) {
        return false;
    }

    const int hours = std::stoi(digits.substr(0, 2));
    const int mins = (digits.size() == 4) ? std::stoi(digits.substr(2, 2)) : 0;
    minutes = (hours * 60) + mins;
    if (sign == '-') {
        minutes = -minutes;
    }

    return true;
}

std::string trim_copy(const std::string &text)
{
    size_t start = 0;
    while ((start < text.size()) && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }

    size_t end = text.size();
    while ((end > start) && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(start, end - start);
}

static std::string lowercase_copy(const std::string &value)
{
    std::string copy = value;
    std::transform(copy.begin(), copy.end(), copy.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return copy;
}

static bool ends_with_bin(const std::string &path)
{
    const std::string lower = lowercase_copy(path);
    return (lower.size() >= 4) && (lower.substr(lower.size() - 4) == ".bin");
}

static std::string basename_from_path(const std::string &path)
{
    const size_t separator = path.find_last_of("/\\");
    return (separator == std::string::npos) ? path : path.substr(separator + 1);
}

static std::string safe_json_string(cJSON *object, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || (item->valuestring == nullptr)) {
        return {};
    }

    return item->valuestring;
}

LV_IMG_DECLARE(img_wifi_connect_success);
LV_IMG_DECLARE(img_wifi_connect_fail);

LV_IMG_DECLARE(img_app_setting);
extern lv_obj_t *ui_Min;
extern lv_obj_t *ui_Hour;
extern lv_obj_t *ui_Sec;
extern lv_obj_t *ui_Date;
extern lv_obj_t *ui_Clock_Number;

AppSettings::AppSettings():
    ESP_Brookesia_PhoneApp("Settings", &img_app_setting, false),                  // auto_resize_visual_area
    _is_ui_resumed(false),
    _is_ui_del(true),
    _screen_index(UI_MAIN_SETTING_INDEX),
    _wifi_signal_strength_level(WIFI_SIGNAL_STRENGTH_NONE),
    _savedWifiPanel(nullptr),
    _wifiScanButton(nullptr),
    _wifiScanButtonLabel(nullptr),
    _savedWifiTitleLabel(nullptr),
    _savedWifiExpandButton(nullptr),
    _savedWifiExpandLabel(nullptr),
    _savedWifiListContainer(nullptr),
    _savedWifiListExpanded(false),
    _suppressDisconnectRecovery(false),
    _aboutWifiValueLabel(nullptr),
    _wifiPasswordToggleButton(nullptr),
    _wifiPasswordToggleLabel(nullptr),
    _displayAdaptiveBrightnessSwitch(nullptr),
    _displayScreensaverSwitch(nullptr),
    _displayTimeoffDropdown(nullptr),
    _displaySleepDropdown(nullptr),
    _displayAutoTimezoneSwitch(nullptr),
    _displayTimezoneDropdown(nullptr),
    _displayTimezoneInfoLabel(nullptr),
    _audioMediaVolumeSlider(nullptr),
    _audioSystemVolumeSlider(nullptr),
    _bluetoothMenuItem(nullptr),
    _zigbeeMenuItem(nullptr),
    _wifiMenuItem(nullptr),
    _audioMenuItem(nullptr),
    _displayMenuItem(nullptr),
    _hardwareMenuItem(nullptr),
    _securityMenuItem(nullptr),
    _aboutMenuItem(nullptr),
    _bluetoothInfoLabel(nullptr),
    _bluetoothNameTextArea(nullptr),
    _bluetoothNameSaveButton(nullptr),
    _bluetoothScanButton(nullptr),
    _bluetoothScanButtonLabel(nullptr),
    _bluetoothScanStatusLabel(nullptr),
    _bluetoothScanResultsLabel(nullptr),
    _bluetoothKeyboard(nullptr),
    _zigbeeEnableSwitch(nullptr),
    _zigbeeNameTextArea(nullptr),
    _zigbeeNameSaveButton(nullptr),
    _zigbeeChannelDropdown(nullptr),
    _zigbeePermitJoinDropdown(nullptr),
    _zigbeeKeyboard(nullptr),
    _zigbeeInfoLabel(nullptr),
    _zigbeeRoleValueLabel(nullptr),
    _zigbeeConfigSummaryLabel(nullptr),
    _securityDeviceLockSwitch(nullptr),
    _securitySettingsLockSwitch(nullptr),
    _securityInfoLabel(nullptr),
    _firmwareMenuItem(nullptr),
    _firmwareScreen(nullptr),
    _hardwareScreen(nullptr),
    _securityScreen(nullptr),
    _zigbeeScreen(nullptr),
    _hardwareCpuSpeedValueLabel(nullptr),
    _hardwareCpuSpeedDetailLabel(nullptr),
    _hardwareCpuSpeedBar(nullptr),
    _hardwareBatteryCard(nullptr),
    _hardwareBatteryValueLabel(nullptr),
    _hardwareBatteryDetailLabel(nullptr),
    _hardwareBatteryBar(nullptr),
    _hardwareBatteryExpandedArea(nullptr),
    _hardwareBatteryExpandLabel(nullptr),
    _hardwareBatteryHistoryTitleLabel(nullptr),
    _hardwareBatteryHistorySummaryLabel(nullptr),
    _hardwareBatteryHistoryChart(nullptr),
    _hardwareBatteryHistorySeries(nullptr),
    _hardwareBatteryHistoryLeftLabel(nullptr),
    _hardwareBatteryHistoryRightLabel(nullptr),
    _hardwareBatteryHistoryFooterLabel(nullptr),
    _hardwareBatteryExpanded(false),
    _hardwareTrendUi{},
    _hardwareFastHistoryScratch(nullptr),
    _hardwareSlowHistoryScratch(nullptr),
    _hardwareCpuTempValueLabel(nullptr),
    _hardwareCpuTempDetailLabel(nullptr),
    _hardwareCpuTempBar(nullptr),
    _hardwareSramValueLabel(nullptr),
    _hardwareSramDetailLabel(nullptr),
    _hardwareSramBar(nullptr),
    _hardwarePsramValueLabel(nullptr),
    _hardwarePsramDetailLabel(nullptr),
    _hardwarePsramBar(nullptr),
    _hardwareSdValueLabel(nullptr),
    _hardwareSdDetailLabel(nullptr),
    _hardwareSdBar(nullptr),
    _hardwareWifiValueLabel(nullptr),
    _hardwareWifiDetailLabel(nullptr),
    _hardwareWifiBar(nullptr),
    _firmwareSdDropdown(nullptr),
    _firmwareSdFlashButton(nullptr),
    _firmwareOtaCheckButton(nullptr),
    _firmwareOtaFlashButton(nullptr),
    _firmwareCurrentVersionLabel(nullptr),
    _firmwareOtaSummaryLabel(nullptr),
    _firmwareOtaListContainer(nullptr),
    _firmwareOtaCheckOverlay(nullptr),
    _firmwareOtaCheckSpinner(nullptr),
    _firmwareOtaCheckStatusLabel(nullptr),
    _firmwareStatusLabel(nullptr),
    _firmwareProgressBar(nullptr),
    _firmwareProgressLabel(nullptr),
    _firmwareUpdateInProgress(false),
    _firmwareOtaCheckInProgress(false),
    _isWifiPasswordVisible(false),
    _wifiKeyboardCapsLockEnabled(false),
    _wifiKeyboardSingleShiftPending(false),
    _wifiKeyboardPressedAction(WIFI_KEYBOARD_PRESSED_NONE),
    _bluetoothStatusIconInstalled(false),
    _zigbeeStatusIconInstalled(false),
    _deviceLockToggleContext{this, device_security::LockType::Device},
    _settingsLockToggleContext{this, device_security::LockType::Settings},
    _screen_list({nullptr}),
    _selectedOtaFirmwareIndex(-1),
    _autoTimezoneRefreshPending(false),
    _hasAutoDetectedTimezone(false),
    _autoDetectedTimezoneOffsetMinutes(480),
    _autoTimezoneStatus()
{
}

AppSettings::~AppSettings()
{
    if (_hardwareFastHistoryScratch != nullptr) {
        free(_hardwareFastHistoryScratch);
        _hardwareFastHistoryScratch = nullptr;
    }
    if (_hardwareSlowHistoryScratch != nullptr) {
        free(_hardwareSlowHistoryScratch);
        _hardwareSlowHistoryScratch = nullptr;
    }
}

void AppSettings::initializeDefaultNvsParams(void)
{
    _nvs_param_map[NVS_KEY_WIFI_ENABLE] = false;
    _nvs_param_map[NVS_KEY_BLE_ENABLE] = false;
    _nvs_param_map[NVS_KEY_ZIGBEE_ENABLE] = false;
    _nvs_param_map[NVS_KEY_ZIGBEE_CHANNEL] = 13;
    _nvs_param_map[NVS_KEY_ZIGBEE_PERMIT_JOIN] = 180;
    _nvs_param_map[NVS_KEY_AUDIO_VOLUME] = bsp_extra_audio_media_volume_get();
    _nvs_param_map[NVS_KEY_AUDIO_VOLUME] = max(min((int)_nvs_param_map[NVS_KEY_AUDIO_VOLUME], SPEAKER_VOLUME_MAX), SPEAKER_VOLUME_MIN);
    _nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME] = bsp_extra_audio_system_volume_get();
    _nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME] = max(min((int)_nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME], SPEAKER_VOLUME_MAX), SPEAKER_VOLUME_MIN);
    _nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS] = brightness;
    _nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS] = max(min((int)_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS], SCREEN_BRIGHTNESS_MAX), SCREEN_BRIGHTNESS_MIN);
    _nvs_param_map[NVS_KEY_DISPLAY_ADAPTIVE] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_SCREENSAVER] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_SLEEP] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE] = 480;
    _nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO] = 0;
}

bool AppSettings::run(void)
{
    _is_ui_del = false;

    const bool rebuild_ui = !settings_ui_is_ready();
    if (rebuild_ui) {
        ui_setting_init();
    } else {
        lv_disp_load_scr(ui_ScreenSettingMain);
    }

    esp_read_mac(base_mac_addr, ESP_MAC_EFUSE_FACTORY);
    snprintf(mac_str, sizeof(mac_str), "%02X-%02X-%02X-%02X-%02X-%02X",
             base_mac_addr[0], base_mac_addr[1], base_mac_addr[2],
             base_mac_addr[3], base_mac_addr[4], base_mac_addr[5]);

    if (rebuild_ui) {
        extraUiInit();
    }
    refreshRadioStatusBar();
    updateUiByNvsParam();

    xEventGroupSetBits(s_wifi_event_group, WIFI_EVENT_UI_INIT_DONE);

    return true;
}

bool AppSettings::back(void)
{
    _is_ui_resumed = false;

#if APP_SETTINGS_FEATURE_WIFI
    if (_screen_index == UI_WIFI_CONNECT_INDEX) {
        lv_scr_load_anim(ui_ScreenSettingWiFi, LV_SCR_LOAD_ANIM_MOVE_RIGHT, kSettingScreenAnimTimeMs, 0, false);
    } else if (_screen_index != UI_MAIN_SETTING_INDEX) {
        lv_scr_load_anim(ui_ScreenSettingMain, LV_SCR_LOAD_ANIM_MOVE_RIGHT, kSettingScreenAnimTimeMs, 0, false);
    } else {
        while(xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_SCANING) {
            ESP_LOGI(TAG, "WiFi is scanning, please wait");
            vTaskDelay(pdMS_TO_TICKS(100));
            stopWifiScan();
        }
        notifyCoreClosed();
    }
#else
    if (_screen_index != UI_MAIN_SETTING_INDEX) {
        lv_scr_load_anim(ui_ScreenSettingMain, LV_SCR_LOAD_ANIM_MOVE_RIGHT, kSettingScreenAnimTimeMs, 0, false);
    } else {
        notifyCoreClosed();
    }
#endif

    return true;
}

bool AppSettings::close(void)
{
#if APP_SETTINGS_FEATURE_WIFI
    while(xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_SCANING) {
        ESP_LOGI(TAG, "WiFi is scanning, please wait");
        vTaskDelay(pdMS_TO_TICKS(100));
        stopWifiScan();
    } 
#endif
    
    _is_ui_del = true;
    
    return true;
}

bool AppSettings::init(void)
{
    ESP_Brookesia_Phone *phone = getPhone();
    ESP_Brookesia_PhoneHome& home = phone->getHome();
    status_bar = home.getStatusBar();
    backstage = home.getRecentsScreen();

    // Initialize NVS parameters
    initializeDefaultNvsParams();
    // Load NVS parameters if exist
    loadNvsParam();
#if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    {
        char ble_name[32] = {0};
        if (!loadNvsStringParam(NVS_KEY_BLE_DEVICE_NAME, ble_name, sizeof(ble_name)) || (ble_name[0] == '\0')) {
            strlcpy(ble_name, kBleDefaultDeviceName, sizeof(ble_name));
        }
        s_bleConfiguredName = ble_name;
    }
    if (_nvs_param_map[NVS_KEY_BLE_ENABLE]) {
        esp_err_t ble_err = bleSetEnabled(true);
        if (ble_err != ESP_OK) {
            ESP_LOGW(TAG, "BLE startup failed: %s", esp_err_to_name(ble_err));
            _nvs_param_map[NVS_KEY_BLE_ENABLE] = false;
            setNvsParam(NVS_KEY_BLE_ENABLE, 0);
        }
    }
#else
    if (_nvs_param_map[NVS_KEY_BLE_ENABLE]) {
        _nvs_param_map[NVS_KEY_BLE_ENABLE] = false;
        setNvsParam(NVS_KEY_BLE_ENABLE, 0);
    }
#endif

#if APP_SETTINGS_FEATURE_DISPLAY_MENU
    applyManualTimezonePreference();
#endif
    // Update System parameters

#if CONFIG_JC4880_FEATURE_AUDIO
    bsp_extra_audio_media_volume_set(_nvs_param_map[NVS_KEY_AUDIO_VOLUME]);
    bsp_extra_audio_system_volume_set(_nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME]);
#endif

#if APP_SETTINGS_FEATURE_DISPLAY_MENU
    bsp_display_brightness_set(_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS]);
    ESP_ERROR_CHECK(bsp_extra_display_idle_init());
    applyDisplayIdleSettings();
#endif

    if (create_background_task_prefer_psram(euiRefresTask, "Home Refresh", HOME_REFRESH_TASK_STACK_SIZE,
                                            this, HOME_REFRESH_TASK_PRIORITY, nullptr, 1) != pdPASS) {
        ESP_LOGW(TAG, "Failed to start Settings refresh task");
    }

#if APP_SETTINGS_FEATURE_WIFI
    if (create_background_task_prefer_psram(wifiScanTask, "WiFi Scan", WIFI_SCAN_TASK_STACK_SIZE,
                                            this, WIFI_SCAN_TASK_PRIORITY, nullptr, 1) != pdPASS) {
        ESP_LOGW(TAG, "Failed to start WiFi scan background task");
    }
#endif

    refreshRadioStatusBar();

    return true;
}

bool AppSettings::isUiActive(void) const
{
    return !_is_ui_del && settings_ui_is_ready();
}

bool AppSettings::pause(void)
{
    _is_ui_resumed = true;

    return true;
}

bool AppSettings::resume(void)
{
    _is_ui_resumed = false;
    refreshRadioStatusBar();

    return true;
}

void AppSettings::extraUiInit(void)
{
    auto createMainMenuItem = [this](const char *title, const void *icon_src, const char *icon_symbol, lv_obj_t **label_out) {
        lv_obj_t *item = lv_obj_create(ui_PanelSettingMainContainer);
        lv_obj_set_size(item, lv_pct(100), 70);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(item, 0, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xCBCBCB), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, 255, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_color(item, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(item, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *icon = nullptr;
        if (icon_src != nullptr) {
            icon = lv_img_create(item);
            lv_img_set_src(icon, icon_src);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, 24, 0);
        } else {
            icon = lv_label_create(item);
            lv_label_set_text(icon, icon_symbol != nullptr ? icon_symbol : LV_SYMBOL_SETTINGS);
            lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
            lv_obj_set_style_text_color(icon, lv_color_hex(0x0F172A), 0);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, 22, 0);
        }

        lv_obj_t *label = lv_label_create(item);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_30, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x0F172A), 0);
        lv_obj_align_to(label, icon, LV_ALIGN_OUT_RIGHT_MID, UI_MAIN_ITEM_LEFT_OFFSET, 0);

        lv_obj_t *arrow = lv_img_create(item);
        lv_img_set_src(arrow, &ui_img_arrow_png);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -24, 0);

        if (label_out != nullptr) {
            *label_out = label;
        }

        lv_obj_add_event_cb(item, onMainMenuItemClickedEventCallback, LV_EVENT_CLICKED, this);
        return item;
    };

    auto createMainBadgeMenuItem = [this](const char *title, const char *badge_text, lv_color_t badge_color,
                                          lv_obj_t **label_out) {
        lv_obj_t *item = lv_obj_create(ui_PanelSettingMainContainer);
        lv_obj_set_size(item, lv_pct(100), 70);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(item, 0, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xCBCBCB), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, 255, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_color(item, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(item, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *badge = lv_obj_create(item);
        lv_obj_set_size(badge, 42, 42);
        lv_obj_align(badge, LV_ALIGN_LEFT_MID, 18, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_bg_color(badge, badge_color, 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(badge, 0, 0);

        lv_obj_t *badgeLabel = lv_label_create(badge);
        lv_label_set_text(badgeLabel, badge_text);
        lv_obj_set_style_text_font(badgeLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(badgeLabel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(badgeLabel);

        lv_obj_t *label = lv_label_create(item);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_30, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x0F172A), 0);
        lv_obj_align_to(label, badge, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

        lv_obj_t *arrow = lv_img_create(item);
        lv_img_set_src(arrow, &ui_img_arrow_png);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -24, 0);

        if (label_out != nullptr) {
            *label_out = label;
        }

        lv_obj_add_event_cb(item, onMainMenuItemClickedEventCallback, LV_EVENT_CLICKED, this);
        return item;
    };

    auto createMonitorCard = [](lv_obj_t *parent, const char *title, const char *subtitle, lv_obj_t **value_label,
                                lv_obj_t **detail_label, lv_obj_t **bar) {
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(card, 18, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(card, 16, 0);
        lv_obj_set_style_pad_right(card, 16, 0);
        lv_obj_set_style_pad_top(card, 14, 0);
        lv_obj_set_style_pad_bottom(card, 14, 0);

        lv_obj_t *titleLabel = lv_label_create(card);
        lv_label_set_text(titleLabel, title);
        lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x0F172A), 0);
        lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *valueLabel = lv_label_create(card);
        lv_label_set_text(valueLabel, "--");
        lv_obj_set_style_text_font(valueLabel, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(valueLabel, lv_color_hex(0x0F172A), 0);
        lv_obj_align(valueLabel, LV_ALIGN_TOP_RIGHT, 0, 0);

        lv_obj_t *subtitleLabel = lv_label_create(card);
        lv_obj_set_width(subtitleLabel, lv_pct(100));
        lv_label_set_long_mode(subtitleLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(subtitleLabel, subtitle);
        lv_obj_set_style_text_font(subtitleLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(subtitleLabel, lv_color_hex(0x64748B), 0);
        lv_obj_align_to(subtitleLabel, titleLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

        lv_obj_t *barObj = lv_bar_create(card);
        lv_obj_set_size(barObj, lv_pct(100), 16);
        lv_bar_set_range(barObj, 0, 100);
        lv_bar_set_value(barObj, 0, LV_ANIM_OFF);
        lv_obj_set_style_radius(barObj, 8, 0);
        lv_obj_set_style_bg_color(barObj, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(barObj, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(barObj, lv_color_hex(0x2563EB), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(barObj, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_align_to(barObj, subtitleLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);

        lv_obj_t *detailLabel = lv_label_create(card);
        lv_obj_set_width(detailLabel, lv_pct(100));
        lv_label_set_long_mode(detailLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(detailLabel, "Waiting for telemetry...");
        lv_obj_set_style_text_font(detailLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(detailLabel, lv_color_hex(0x475569), 0);
        lv_obj_align_to(detailLabel, barObj, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

        if (value_label != nullptr) {
            *value_label = valueLabel;
        }
        if (detail_label != nullptr) {
            *detail_label = detailLabel;
        }
        if (bar != nullptr) {
            *bar = barObj;
        }
    };

    lv_obj_add_flag(ui_PanelSettingMainContainerItem1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_PanelSettingMainContainerItem2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_PanelSettingMainContainerItem3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_PanelSettingMainContainerItem4, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_PanelSettingMainContainerItem5, LV_OBJ_FLAG_HIDDEN);

    #if APP_SETTINGS_FEATURE_WIFI
    _wifiMenuItem = createMainMenuItem("Wi-Fi", &ui_img_wifi_png, nullptr, nullptr);
    #endif
    #if CONFIG_JC4880_FEATURE_AUDIO
    _audioMenuItem = createMainMenuItem("Audio", &ui_img_sound_png, nullptr, nullptr);
    #endif
    #if APP_SETTINGS_FEATURE_DISPLAY_MENU
    _displayMenuItem = createMainMenuItem("Display", &ui_img_light_png, nullptr, nullptr);
    #endif
    #if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    _bluetoothMenuItem = createMainMenuItem("Bluetooth", &ui_img_bluetooth_png, nullptr, nullptr);
    #endif
    #if CONFIG_JC4880_FEATURE_ZIGBEE
    _zigbeeMenuItem = createMainBadgeMenuItem("ZigBee", "ZB", lv_color_hex(0xD97706), nullptr);
    #endif
    #if APP_SETTINGS_FEATURE_HARDWARE_MENU
    _hardwareMenuItem = createMainMenuItem("Hardware", nullptr, LV_SYMBOL_SETTINGS, nullptr);
    #endif
    #if CONFIG_JC4880_FEATURE_SECURITY
    _securityMenuItem = createMainMenuItem("Security", nullptr, LV_SYMBOL_WARNING, nullptr);
    #endif
    #if CONFIG_JC4880_FEATURE_OTA
    _firmwareMenuItem = createMainMenuItem("Firmware OTA", nullptr, LV_SYMBOL_DOWNLOAD, nullptr);
    #endif
    #if CONFIG_JC4880_FEATURE_ABOUT_DEVICE
    _aboutMenuItem = createMainMenuItem("About Device", &ui_img_about_png, nullptr, nullptr);
    #endif

    // Record the screen index and install the screen loaded event callback
    _screen_list[UI_MAIN_SETTING_INDEX] = ui_ScreenSettingMain;
    lv_obj_add_event_cb(ui_ScreenSettingMain, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);

    /* WiFi */
    // Switch
    lv_obj_add_event_cb(ui_SwitchPanelScreenSettingWiFiSwitch, onSwitchPanelScreenSettingWiFiSwitchValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);
    _savedWifiPanel = lv_obj_create(ui_ScreenSettingWiFi);
    lv_obj_set_size(_savedWifiPanel, lv_pct(90), 120);
    lv_obj_align_to(_savedWifiPanel, ui_PanelScreenSettingWiFiSwitch, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    lv_obj_clear_flag(_savedWifiPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_savedWifiPanel, 18, 0);
    lv_obj_set_style_border_width(_savedWifiPanel, 0, 0);
    lv_obj_set_style_bg_color(_savedWifiPanel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(_savedWifiPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(_savedWifiPanel, 18, 0);
    lv_obj_set_style_pad_right(_savedWifiPanel, 18, 0);
    lv_obj_set_style_pad_top(_savedWifiPanel, 10, 0);
    lv_obj_set_style_pad_bottom(_savedWifiPanel, 10, 0);

    _wifiScanButton = lv_btn_create(ui_ScreenSettingWiFi);
    lv_obj_set_size(_wifiScanButton, lv_pct(100), 40);
    lv_obj_set_width(_wifiScanButton, lv_pct(90));
    lv_obj_align_to(_wifiScanButton, _savedWifiPanel, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_set_style_radius(_wifiScanButton, 12, 0);
    lv_obj_set_style_bg_color(_wifiScanButton, lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_bg_opa(_wifiScanButton, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_wifiScanButton, 0, 0);
    lv_obj_add_event_cb(_wifiScanButton, onWifiScanClickedEventCallback, LV_EVENT_CLICKED, this);

    _wifiScanButtonLabel = lv_label_create(_wifiScanButton);
    lv_label_set_text(_wifiScanButtonLabel, "Scan");
    lv_obj_set_style_text_color(_wifiScanButtonLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(_wifiScanButtonLabel);

    _savedWifiTitleLabel = lv_label_create(_savedWifiPanel);
    lv_label_set_text(_savedWifiTitleLabel, "Saved Network");
    lv_obj_set_style_text_font(_savedWifiTitleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_savedWifiTitleLabel, lv_color_hex(0x4A5568), 0);
    lv_obj_align(_savedWifiTitleLabel, LV_ALIGN_TOP_LEFT, 0, 0);

    _savedWifiExpandButton = lv_btn_create(_savedWifiPanel);
    lv_obj_set_size(_savedWifiExpandButton, 36, 36);
    lv_obj_align(_savedWifiExpandButton, LV_ALIGN_TOP_RIGHT, 0, -4);
    lv_obj_set_style_radius(_savedWifiExpandButton, 12, 0);
    lv_obj_set_style_bg_color(_savedWifiExpandButton, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_bg_opa(_savedWifiExpandButton, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_savedWifiExpandButton, 0, 0);
    lv_obj_add_event_cb(_savedWifiExpandButton, onSavedWifiDropdownClickedEventCallback, LV_EVENT_CLICKED, this);

    _savedWifiExpandLabel = lv_label_create(_savedWifiExpandButton);
    lv_label_set_text(_savedWifiExpandLabel, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(_savedWifiExpandLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_center(_savedWifiExpandLabel);

    _savedWifiListContainer = lv_obj_create(_savedWifiPanel);
    lv_obj_set_size(_savedWifiListContainer, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_align_to(_savedWifiListContainer, _savedWifiTitleLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_obj_clear_flag(_savedWifiListContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(_savedWifiListContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_savedWifiListContainer, 0, 0);
    lv_obj_set_style_pad_all(_savedWifiListContainer, 0, 0);
    lv_obj_set_style_pad_row(_savedWifiListContainer, 8, 0);
    lv_obj_set_flex_flow(_savedWifiListContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_savedWifiListContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    // List
    // lv_obj_clear_flag(ui_PanelScreenSettingWiFiList, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui_PanelScreenSettingWiFiList, LV_DIR_VER);
    lv_obj_set_height(ui_PanelScreenSettingWiFiList, lv_pct(UI_WIFI_LIST_H_PERCENT));
    lv_obj_align_to(ui_PanelScreenSettingWiFiList, _wifiScanButton, LV_ALIGN_OUT_BOTTOM_MID, 0,
                    UI_WIFI_LIST_UP_OFFSET);
    lv_obj_set_style_pad_all(ui_PanelScreenSettingWiFiList, 0, 0);
    lv_obj_set_style_pad_top(ui_PanelScreenSettingWiFiList, UI_WIFI_LIST_UP_PAD, 0);
    lv_obj_set_style_pad_bottom(ui_PanelScreenSettingWiFiList, UI_WIFI_LIST_DOWN_PAD, 0);
    for(int i = 0; i < SCAN_LIST_SIZE; i++) {
        panel_wifi_btn[i] = lv_obj_create(ui_PanelScreenSettingWiFiList);
        lv_obj_set_size(panel_wifi_btn[i], lv_pct(100), UI_WIFI_LIST_ITEM_H);
        lv_obj_set_style_radius(panel_wifi_btn[i], 0, 0);
        lv_obj_set_style_border_width(panel_wifi_btn[i], 0, 0);
        lv_obj_set_style_text_font(panel_wifi_btn[i], UI_WIFI_LIST_ITEM_FONT, 0);
        lv_obj_add_flag(panel_wifi_btn[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag( panel_wifi_btn[i], LV_OBJ_FLAG_SCROLLABLE );
        lv_obj_set_style_bg_color(panel_wifi_btn[i], lv_color_hex(0xCBCBCB), LV_PART_MAIN | LV_STATE_PRESSED );
        lv_obj_set_style_bg_opa(panel_wifi_btn[i], 255, LV_PART_MAIN| LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(panel_wifi_btn[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
        lv_obj_set_style_border_opa(panel_wifi_btn[i], 255, LV_PART_MAIN| LV_STATE_DEFAULT);
        lv_obj_add_flag(panel_wifi_btn[i], LV_OBJ_FLAG_HIDDEN);

        label_wifi_ssid[i] = lv_label_create(panel_wifi_btn[i]);
        lv_obj_set_align(label_wifi_ssid[i], LV_ALIGN_LEFT_MID);
        lv_label_set_text(label_wifi_ssid[i], "");

        img_img_wifi_lock[i] = lv_img_create(panel_wifi_btn[i]);
        lv_obj_align(img_img_wifi_lock[i], LV_ALIGN_RIGHT_MID, UI_WIFI_ICON_LOCK_RIGHT_OFFSET, 0);
        lv_obj_add_flag(img_img_wifi_lock[i], LV_OBJ_FLAG_HIDDEN);

        wifi_image[i] = lv_img_create(panel_wifi_btn[i]);
        lv_obj_align(wifi_image[i], LV_ALIGN_RIGHT_MID, UI_WIFI_ICON_SIGNAL_RIGHT_OFFSET, 0);

        wifi_connect[i] = lv_label_create(panel_wifi_btn[i]);
        lv_label_set_text(wifi_connect[i], LV_SYMBOL_OK);
        lv_obj_align(wifi_connect[i], LV_ALIGN_RIGHT_MID, UI_WIFI_ICON_CONNECT_RIGHT_OFFSET, 0);
        lv_obj_add_flag(wifi_connect[i], LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_event_cb(panel_wifi_btn[i], onButtonWifiListClickedEventCallback, LV_EVENT_CLICKED, this);
        if(!(xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_SCANING)) {
            lv_obj_add_flag(ui_PanelScreenSettingWiFiList, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_SpinnerScreenSettingWiFi, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_add_flag(ui_ButtonScreenSettingWiFiReturn, LV_OBJ_FLAG_HIDDEN);
    // Connect
    lv_obj_add_flag(ui_SpinnerScreenSettingVerification, LV_OBJ_FLAG_HIDDEN);
    _panel_wifi_connect = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_panel_wifi_connect, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(_panel_wifi_connect, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_panel_wifi_connect, LV_OPA_70, 0);
    lv_obj_add_flag(_panel_wifi_connect, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_panel_wifi_connect, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(_panel_wifi_connect);
    _img_wifi_connect = lv_img_create(_panel_wifi_connect);
    lv_obj_center(_img_wifi_connect);
    _spinner_wifi_connect = lv_spinner_create(_panel_wifi_connect, 1000, 600);
    lv_obj_set_size(_spinner_wifi_connect, lv_pct(20), lv_pct(20));
    lv_obj_center(_spinner_wifi_connect);
    processWifiConnect(WIFI_CONNECT_HIDE);

    lv_label_set_text(ui_LabelScreenSettingVerification, "Connect to Wi-Fi");
    lv_obj_set_width(ui_LabelScreenSettingVerification, lv_pct(84));
    lv_obj_align(ui_LabelScreenSettingVerification, LV_ALIGN_TOP_LEFT, 36, 56);
    lv_label_set_long_mode(ui_LabelScreenSettingVerification, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(ui_LabelScreenSettingVerification, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(ui_LabelScreenSettingVerification, lv_color_hex(0x0F172A), 0);

    lv_obj_set_width(ui_LabelScreenSettingVerificationSSID, lv_pct(84));
    lv_obj_align_to(ui_LabelScreenSettingVerificationSSID, ui_LabelScreenSettingVerification, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 14);
    lv_label_set_long_mode(ui_LabelScreenSettingVerificationSSID, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(ui_LabelScreenSettingVerificationSSID, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(ui_LabelScreenSettingVerificationSSID, lv_color_hex(0x475569), 0);
    lv_label_set_text(ui_LabelScreenSettingVerificationSSID, "Choose a network to continue");

    lv_obj_t *wifiPasswordTitle = lv_label_create(ui_ScreenSettingVerification);
    lv_label_set_text(wifiPasswordTitle, "Password");
    lv_obj_set_style_text_font(wifiPasswordTitle, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(wifiPasswordTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align_to(wifiPasswordTitle, ui_LabelScreenSettingVerificationSSID, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 34);

    // Keyboard
    lv_obj_set_height(ui_TextAreaScreenSettingVerificationPassword, 64);
    lv_obj_set_width(ui_TextAreaScreenSettingVerificationPassword, 328);
    lv_obj_align_to(ui_TextAreaScreenSettingVerificationPassword, wifiPasswordTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);
    lv_textarea_set_one_line(ui_TextAreaScreenSettingVerificationPassword, true);
    lv_textarea_set_password_mode(ui_TextAreaScreenSettingVerificationPassword, true);
    lv_textarea_set_placeholder_text(ui_TextAreaScreenSettingVerificationPassword, "Enter Wi-Fi password");
    lv_obj_set_style_radius(ui_TextAreaScreenSettingVerificationPassword, 18, 0);
    lv_obj_set_style_border_width(ui_TextAreaScreenSettingVerificationPassword, 1, 0);
    lv_obj_set_style_border_color(ui_TextAreaScreenSettingVerificationPassword, lv_color_hex(0xC6D4E1), 0);
    lv_obj_set_style_bg_color(ui_TextAreaScreenSettingVerificationPassword, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(ui_TextAreaScreenSettingVerificationPassword, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(ui_TextAreaScreenSettingVerificationPassword, 18, 0);
    lv_obj_set_style_text_font(ui_TextAreaScreenSettingVerificationPassword, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ui_TextAreaScreenSettingVerificationPassword, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_text_color(ui_TextAreaScreenSettingVerificationPassword, lv_color_hex(0x94A3B8), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_add_event_cb(ui_TextAreaScreenSettingVerificationPassword, onWifiPasswordFieldEventCallback, LV_EVENT_ALL, this);

    _wifiPasswordToggleButton = lv_btn_create(ui_ScreenSettingVerification);
    lv_obj_set_size(_wifiPasswordToggleButton, 56, 56);
    lv_obj_align_to(_wifiPasswordToggleButton, ui_TextAreaScreenSettingVerificationPassword, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    lv_obj_set_style_radius(_wifiPasswordToggleButton, 18, 0);
    lv_obj_set_style_bg_color(_wifiPasswordToggleButton, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(_wifiPasswordToggleButton, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_wifiPasswordToggleButton, lv_color_hex(0xC6D4E1), 0);
    lv_obj_set_style_border_width(_wifiPasswordToggleButton, 1, 0);
    lv_obj_set_style_shadow_width(_wifiPasswordToggleButton, 0, 0);
    lv_obj_add_event_cb(_wifiPasswordToggleButton, onWifiPasswordToggleClickedEventCallback, LV_EVENT_CLICKED, this);

    _wifiPasswordToggleLabel = lv_label_create(_wifiPasswordToggleButton);
    lv_obj_set_style_text_font(_wifiPasswordToggleLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_wifiPasswordToggleLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_center(_wifiPasswordToggleLabel);
    updateWifiPasswordVisibility(false);

    lv_obj_set_size(ui_KeyboardScreenSettingVerification, lv_pct(100), lv_pct(34));
    lv_obj_align(ui_KeyboardScreenSettingVerification, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(ui_KeyboardScreenSettingVerification, ui_TextAreaScreenSettingVerificationPassword);
    lv_keyboard_set_popovers(ui_KeyboardScreenSettingVerification, true);
    lv_obj_add_flag(ui_KeyboardScreenSettingVerification, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ui_KeyboardScreenSettingVerification, onKeyboardScreenSettingVerificationClickedEventCallback,
                        LV_EVENT_ALL, this);
    // Record the screen index and install the screen loaded event callback
    lv_obj_add_flag(ui_ButtonScreenSettingBLEReturn, LV_OBJ_FLAG_HIDDEN);
    _screen_list[UI_WIFI_SCAN_INDEX] = ui_ScreenSettingWiFi;
    lv_obj_add_event_cb(ui_ScreenSettingWiFi, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
    _screen_list[UI_WIFI_CONNECT_INDEX] = ui_ScreenSettingVerification;
    lv_obj_add_event_cb(ui_ScreenSettingVerification, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);

    /* Bluetooth */
    lv_label_set_text(ui_LabelPanelScreenSettingBLESwitch, "BLE");
    lv_obj_clear_flag(ui_ImagePanelScreenSettingBLESwitch, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(ui_ImagePanelScreenSettingBLESwitch, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_set_style_img_recolor_opa(ui_ImagePanelScreenSettingBLESwitch, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(ui_LabelPanelScreenSettingBLESwitch, &lv_font_montserrat_24, 0);
    lv_obj_align_to(ui_LabelPanelScreenSettingBLESwitch, ui_ImagePanelScreenSettingBLESwitch, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    lv_obj_align(ui_SwitchPanelScreenSettingBLESwitch, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_clear_flag(ui_PanelScreenSettingBLEList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_SpinnerScreenSettingBLE, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align_to(ui_PanelScreenSettingBLEList, ui_PanelScreenSettingBLESwitch, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    lv_obj_set_size(ui_PanelScreenSettingBLEList, lv_pct(90), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(ui_PanelScreenSettingBLEList, 16, 0);
    lv_obj_set_style_pad_row(ui_PanelScreenSettingBLEList, 12, 0);
    lv_obj_set_style_bg_color(ui_PanelScreenSettingBLEList, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(ui_PanelScreenSettingBLEList, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_PanelScreenSettingBLEList, 0, 0);
    lv_obj_set_scroll_dir(ui_PanelScreenSettingBLEList, LV_DIR_VER);

    auto createSettingsToggleRow = [](lv_obj_t *parent, const char *title) {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_set_size(row, lv_pct(100), 72);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(row, 18, 0);
        lv_obj_set_style_pad_right(row, 18, 0);
        lv_obj_set_style_pad_top(row, 10, 0);
        lv_obj_set_style_pad_bottom(row, 10, 0);

        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x111827), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        return row;
    };

    lv_obj_t *bluetoothStatusRow = lv_obj_create(ui_PanelScreenSettingBLEList);
    lv_obj_set_width(bluetoothStatusRow, lv_pct(100));
    lv_obj_set_height(bluetoothStatusRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(bluetoothStatusRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(bluetoothStatusRow, 18, 0);
    lv_obj_set_style_border_width(bluetoothStatusRow, 0, 0);
    lv_obj_set_style_bg_color(bluetoothStatusRow, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_opa(bluetoothStatusRow, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(bluetoothStatusRow, 16, 0);

    lv_obj_t *bluetoothStatusTitle = lv_label_create(bluetoothStatusRow);
    lv_label_set_text(bluetoothStatusTitle, "Status");
    lv_obj_set_style_text_font(bluetoothStatusTitle, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(bluetoothStatusTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(bluetoothStatusTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    _bluetoothInfoLabel = lv_label_create(bluetoothStatusRow);
    lv_obj_set_width(_bluetoothInfoLabel, lv_pct(100));
    lv_label_set_long_mode(_bluetoothInfoLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_bluetoothInfoLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_bluetoothInfoLabel, lv_color_hex(0x475569), 0);
    lv_obj_align_to(_bluetoothInfoLabel, bluetoothStatusTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    lv_obj_t *bluetoothNameRow = lv_obj_create(ui_PanelScreenSettingBLEList);
    lv_obj_set_width(bluetoothNameRow, lv_pct(100));
    lv_obj_set_height(bluetoothNameRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(bluetoothNameRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(bluetoothNameRow, 18, 0);
    lv_obj_set_style_border_width(bluetoothNameRow, 0, 0);
    lv_obj_set_style_bg_color(bluetoothNameRow, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_opa(bluetoothNameRow, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(bluetoothNameRow, 16, 0);

    lv_obj_t *bluetoothNameTitle = lv_label_create(bluetoothNameRow);
    lv_label_set_text(bluetoothNameTitle, "Device name");
    lv_obj_set_style_text_font(bluetoothNameTitle, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(bluetoothNameTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(bluetoothNameTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    _bluetoothNameTextArea = lv_textarea_create(bluetoothNameRow);
    lv_obj_set_width(_bluetoothNameTextArea, lv_pct(100));
    lv_textarea_set_one_line(_bluetoothNameTextArea, true);
    lv_textarea_set_max_length(_bluetoothNameTextArea, 31);
    lv_textarea_set_placeholder_text(_bluetoothNameTextArea, kBleDefaultDeviceName);
    lv_obj_set_style_radius(_bluetoothNameTextArea, 14, 0);
    lv_obj_set_style_bg_color(_bluetoothNameTextArea, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(_bluetoothNameTextArea, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_border_width(_bluetoothNameTextArea, 1, 0);
    lv_obj_set_style_pad_left(_bluetoothNameTextArea, 12, 0);
    lv_obj_set_style_pad_right(_bluetoothNameTextArea, 12, 0);
    lv_obj_align_to(_bluetoothNameTextArea, bluetoothNameTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_obj_add_event_cb(_bluetoothNameTextArea, onBluetoothNameTextAreaEventCallback, LV_EVENT_ALL, this);

    _bluetoothNameSaveButton = lv_btn_create(bluetoothNameRow);
    lv_obj_set_size(_bluetoothNameSaveButton, 120, 42);
    lv_obj_set_style_radius(_bluetoothNameSaveButton, 14, 0);
    lv_obj_set_style_bg_color(_bluetoothNameSaveButton, lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_bg_opa(_bluetoothNameSaveButton, LV_OPA_COVER, 0);
    lv_obj_align_to(_bluetoothNameSaveButton, _bluetoothNameTextArea, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 10);
    lv_obj_add_event_cb(_bluetoothNameSaveButton, onBluetoothNameSaveClickedEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t *bluetoothNameSaveLabel = lv_label_create(_bluetoothNameSaveButton);
    lv_label_set_text(bluetoothNameSaveLabel, "Save Name");
    lv_obj_set_style_text_color(bluetoothNameSaveLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(bluetoothNameSaveLabel, &lv_font_montserrat_16, 0);
    lv_obj_center(bluetoothNameSaveLabel);

    lv_obj_t *bluetoothDiscoveryRow = lv_obj_create(ui_PanelScreenSettingBLEList);
    lv_obj_set_width(bluetoothDiscoveryRow, lv_pct(100));
    lv_obj_set_height(bluetoothDiscoveryRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(bluetoothDiscoveryRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(bluetoothDiscoveryRow, 18, 0);
    lv_obj_set_style_border_width(bluetoothDiscoveryRow, 0, 0);
    lv_obj_set_style_bg_color(bluetoothDiscoveryRow, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_opa(bluetoothDiscoveryRow, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(bluetoothDiscoveryRow, 16, 0);

    lv_obj_t *bluetoothDiscoveryTitle = lv_label_create(bluetoothDiscoveryRow);
    lv_label_set_text(bluetoothDiscoveryTitle, "Nearby BLE devices");
    lv_obj_set_style_text_font(bluetoothDiscoveryTitle, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(bluetoothDiscoveryTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(bluetoothDiscoveryTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    _bluetoothScanButton = lv_btn_create(bluetoothDiscoveryRow);
    lv_obj_set_size(_bluetoothScanButton, 170, 42);
    lv_obj_set_style_radius(_bluetoothScanButton, 14, 0);
    lv_obj_set_style_bg_color(_bluetoothScanButton, lv_color_hex(0x0F766E), 0);
    lv_obj_set_style_bg_opa(_bluetoothScanButton, LV_OPA_COVER, 0);
    lv_obj_align(bluetoothDiscoveryTitle, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_align(_bluetoothScanButton, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(_bluetoothScanButton, onBluetoothScanClickedEventCallback, LV_EVENT_CLICKED, this);

    _bluetoothScanButtonLabel = lv_label_create(_bluetoothScanButton);
    lv_label_set_text(_bluetoothScanButtonLabel, "Scan Nearby");
    lv_obj_set_style_text_color(_bluetoothScanButtonLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(_bluetoothScanButtonLabel, &lv_font_montserrat_16, 0);
    lv_obj_center(_bluetoothScanButtonLabel);

    _bluetoothScanStatusLabel = lv_label_create(bluetoothDiscoveryRow);
    lv_obj_set_width(_bluetoothScanStatusLabel, lv_pct(100));
    lv_label_set_long_mode(_bluetoothScanStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_bluetoothScanStatusLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_bluetoothScanStatusLabel, lv_color_hex(0x475569), 0);
    lv_obj_align_to(_bluetoothScanStatusLabel, bluetoothDiscoveryTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 14);

    _bluetoothScanResultsLabel = lv_label_create(bluetoothDiscoveryRow);
    lv_obj_set_width(_bluetoothScanResultsLabel, lv_pct(100));
    lv_label_set_long_mode(_bluetoothScanResultsLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_bluetoothScanResultsLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_bluetoothScanResultsLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_align_to(_bluetoothScanResultsLabel, _bluetoothScanStatusLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    _bluetoothKeyboard = lv_keyboard_create(ui_ScreenSettingBLE);
    lv_obj_set_size(_bluetoothKeyboard, lv_pct(100), lv_pct(32));
    lv_obj_align(_bluetoothKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_popovers(_bluetoothKeyboard, true);
    lv_obj_add_flag(_bluetoothKeyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_bluetoothKeyboard, onBluetoothKeyboardEventCallback, LV_EVENT_ALL, this);

    lv_obj_add_event_cb(ui_SwitchPanelScreenSettingBLESwitch, onSwitchPanelScreenSettingBluetoothValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);
    // Record the screen index and install the screen loaded event callback
    _screen_list[UI_BLUETOOTH_SETTING_INDEX] = ui_ScreenSettingBLE;
    lv_obj_add_event_cb(ui_ScreenSettingBLE, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);

    /* Display */
    lv_slider_set_range(ui_SliderPanelScreenSettingLightSwitch1, SCREEN_BRIGHTNESS_MIN, SCREEN_BRIGHTNESS_MAX);
    lv_obj_add_event_cb(ui_SliderPanelScreenSettingLightSwitch1, onSliderPanelLightSwitchValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);
    lv_obj_set_height(ui_PanelScreenSettingLightSwitch, 84);
    lv_obj_set_width(ui_PanelScreenSettingLightSwitch, lv_pct(90));
    lv_obj_set_x(ui_PanelScreenSettingLightSwitch, 0);
    lv_obj_align(ui_PanelScreenSettingLightSwitch, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_x(ui_ImagePanelScreenSettingLightSwitch, 16);
    lv_obj_set_width(ui_LabelPanelScreenSettingLightSwitch, 126);
    lv_obj_set_style_text_font(ui_LabelPanelScreenSettingLightSwitch, &lv_font_montserrat_20, 0);
    lv_obj_align_to(ui_LabelPanelScreenSettingLightSwitch, ui_ImagePanelScreenSettingLightSwitch, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    lv_obj_set_width(ui_SliderPanelScreenSettingLightSwitch1, 168);
    lv_obj_align(ui_SliderPanelScreenSettingLightSwitch1, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_clear_flag(ui_PanelScreenSettingLightList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align_to(ui_PanelScreenSettingLightList, ui_PanelScreenSettingLightSwitch, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    lv_obj_set_size(ui_PanelScreenSettingLightList, lv_pct(90), 430);
    lv_obj_set_style_pad_all(ui_PanelScreenSettingLightList, 0, 0);
    lv_obj_set_style_pad_row(ui_PanelScreenSettingLightList, 12, 0);
    lv_obj_set_style_bg_opa(ui_PanelScreenSettingLightList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui_PanelScreenSettingLightList, 0, 0);
    lv_obj_set_scroll_dir(ui_PanelScreenSettingLightList, LV_DIR_VER);

    auto createDisplaySettingRow = [](lv_obj_t *parent, const char *title) {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_set_size(row, lv_pct(100), 72);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(row, 18, 0);
        lv_obj_set_style_pad_right(row, 18, 0);
        lv_obj_set_style_pad_top(row, 10, 0);
        lv_obj_set_style_pad_bottom(row, 10, 0);

        lv_obj_t *label = lv_label_create(row);
    lv_obj_set_width(label, 180);
        lv_label_set_text(label, title);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x111827), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        return row;
    };

    lv_obj_t *adaptiveRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Adaptive Brightness");
    _displayAdaptiveBrightnessSwitch = lv_switch_create(adaptiveRow);
    lv_obj_align(_displayAdaptiveBrightnessSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displayAdaptiveBrightnessSwitch, onSwitchPanelScreenSettingAdaptiveBrightnessValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *screensaverRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Screensaver");
    _displayScreensaverSwitch = lv_switch_create(screensaverRow);
    lv_obj_align(_displayScreensaverSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displayScreensaverSwitch, onSwitchPanelScreenSettingScreensaverValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *timezoneRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Timezone");
    _displayTimezoneDropdown = lv_dropdown_create(timezoneRow);
    lv_dropdown_set_options_static(_displayTimezoneDropdown, kTimezoneOptionsText);
    lv_obj_set_width(_displayTimezoneDropdown, 156);
    lv_obj_align(_displayTimezoneDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displayTimezoneDropdown, onDropdownPanelScreenSettingTimezoneValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *autoTimezoneRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Auto Timezone");
    _displayAutoTimezoneSwitch = lv_switch_create(autoTimezoneRow);
    lv_obj_align(_displayAutoTimezoneSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displayAutoTimezoneSwitch, onSwitchPanelScreenSettingAutoTimezoneValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *timeoffRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Timeoff Interval");
    _displayTimeoffDropdown = lv_dropdown_create(timeoffRow);
    lv_dropdown_set_options_static(_displayTimeoffDropdown, kDisplayTimeoffOptionsText);
    lv_obj_set_width(_displayTimeoffDropdown, 132);
    lv_obj_align(_displayTimeoffDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displayTimeoffDropdown, onDropdownPanelScreenSettingTimeoffIntervalValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *sleepRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Sleep Interval");
    _displaySleepDropdown = lv_dropdown_create(sleepRow);
    lv_dropdown_set_options_static(_displaySleepDropdown, kDisplaySleepOptionsText);
    lv_obj_set_width(_displaySleepDropdown, 132);
    lv_obj_align(_displaySleepDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displaySleepDropdown, onDropdownPanelScreenSettingSleepIntervalValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    _displayTimezoneInfoLabel = lv_label_create(ui_PanelScreenSettingLightList);
    lv_obj_set_width(_displayTimezoneInfoLabel, lv_pct(100));
    lv_label_set_long_mode(_displayTimezoneInfoLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_displayTimezoneInfoLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_displayTimezoneInfoLabel, lv_color_hex(0x475569), 0);
    lv_obj_add_flag(ui_ButtonScreenSettingLightReturn, LV_OBJ_FLAG_HIDDEN);
    // Record the screen index and install the screen loaded event callback
    _screen_list[UI_BRIGHTNESS_SETTING_INDEX] = ui_ScreenSettingLight;
    lv_obj_add_event_cb(ui_ScreenSettingLight, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);

    /* Audio */
    _audioMediaVolumeSlider = ui_SliderPanelScreenSettingVolumeSwitch;
    lv_obj_clear_flag(ui_PanelScreenSettingVolumeList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_left(ui_PanelScreenSettingVolumeList, 0, 0);
    lv_obj_set_style_pad_right(ui_PanelScreenSettingVolumeList, 0, 0);
    lv_obj_set_style_pad_top(ui_PanelScreenSettingVolumeList, 0, 0);
    lv_obj_set_style_pad_bottom(ui_PanelScreenSettingVolumeList, 0, 0);
    lv_obj_set_style_pad_row(ui_PanelScreenSettingVolumeList, 12, 0);
    lv_obj_set_style_bg_opa(ui_PanelScreenSettingVolumeList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui_PanelScreenSettingVolumeList, 0, 0);
    lv_obj_set_scroll_dir(ui_PanelScreenSettingVolumeList, LV_DIR_VER);

    auto styleAudioSliderRow = [](lv_obj_t *row, lv_obj_t *label, lv_obj_t *slider, lv_obj_t *icon) {
        lv_obj_set_parent(row, ui_PanelScreenSettingVolumeList);
        lv_obj_set_size(row, lv_pct(100), 72);
        lv_obj_set_x(row, 0);
        lv_obj_set_y(row, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(row, 18, 0);
        lv_obj_set_style_pad_right(row, 18, 0);
        lv_obj_set_style_pad_top(row, 10, 0);
        lv_obj_set_style_pad_bottom(row, 10, 0);

        if (icon != nullptr) {
            lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
        }

        if (label != nullptr) {
            lv_obj_set_width(label, 110);
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(0x111827), 0);
            if (icon != nullptr) {
                lv_obj_align_to(label, icon, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
            } else {
                lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
            }
        }

        if (slider != nullptr) {
            lv_obj_set_size(slider, 220, 14);
            lv_obj_align(slider, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    };

    lv_label_set_text(ui_LabelPanelScreenSettingVolumeSwitch, "Media");
    lv_slider_set_range(ui_SliderPanelScreenSettingVolumeSwitch, SPEAKER_VOLUME_MIN, SPEAKER_VOLUME_MAX);
    lv_obj_add_event_cb(ui_SliderPanelScreenSettingVolumeSwitch, onSliderPanelVolumeSwitchValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);
    styleAudioSliderRow(ui_PanelScreenSettingVolumeSwitch, ui_LabelPanelScreenSettingVolumeSwitch,
                        ui_SliderPanelScreenSettingVolumeSwitch, ui_ImagePanelScreenSettingVolumeSwitch);

    {
        lv_obj_t *row = lv_obj_create(ui_PanelScreenSettingVolumeList);
        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text(label, "System Sounds");

        _audioSystemVolumeSlider = lv_slider_create(row);
        lv_slider_set_range(_audioSystemVolumeSlider, SPEAKER_VOLUME_MIN, SPEAKER_VOLUME_MAX);
        lv_obj_add_event_cb(_audioSystemVolumeSlider, onSliderPanelSystemVolumeValueChangeEventCallback,
                            LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(_audioSystemVolumeSlider, onSliderPanelSystemVolumeValueChangeEventCallback,
                            LV_EVENT_RELEASED, this);

        styleAudioSliderRow(row, label, _audioSystemVolumeSlider, nullptr);
    }
    lv_obj_add_flag(ui_ButtonScreenSettingVolumeReturn, LV_OBJ_FLAG_HIDDEN);
    // Record the screen index and install the screen loaded event callback
    _screen_list[UI_VOLUME_SETTING_INDEX] = ui_ScreenSettingVolume;
    lv_obj_add_event_cb(ui_ScreenSettingVolume, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);

    /* About */
    lv_label_set_text(ui_LabelPanelPanelScreenSettingAbout4, "ESP_Brookesia");
    lv_obj_add_flag(ui_ButtonScreenSettingAboutReturn, LV_OBJ_FLAG_HIDDEN);

    auto styleAboutRow = [](lv_obj_t *row, lv_obj_t *title_label, lv_obj_t *value_label) {
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(row, 72, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(row, 16, 0);
        lv_obj_set_style_pad_right(row, 16, 0);
        lv_obj_set_style_pad_top(row, 12, 0);
        lv_obj_set_style_pad_bottom(row, 12, 0);

        if (title_label != nullptr) {
            lv_obj_set_width(title_label, 150);
            lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(title_label, lv_color_hex(0x0F172A), 0);
        }

        if (value_label != nullptr) {
            lv_obj_set_width(value_label, 220);
            lv_label_set_long_mode(value_label, LV_LABEL_LONG_WRAP);
            lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_style_text_font(value_label, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(value_label, lv_color_hex(0x334155), 0);
        }
    };

    lv_obj_add_flag(ui_PanelScreenSettingAbout, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui_PanelScreenSettingAbout, LV_DIR_VER);
    lv_obj_set_size(ui_PanelScreenSettingAbout, lv_pct(92), 650);
    lv_obj_align(ui_PanelScreenSettingAbout, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_style_pad_all(ui_PanelScreenSettingAbout, 0, 0);
    lv_obj_set_style_pad_row(ui_PanelScreenSettingAbout, 12, 0);
    styleAboutRow(ui_PanelPanelScreenSettingAbout, ui_LabelPanelPanelScreenSettingAboutDevice, ui_LabelPanelPanelScreenSettingAbout2);
    styleAboutRow(ui_PanelPanelScreenSettingAbout1, ui_LabelPanelPanelScreenSettingAboutManufacturer, ui_LabelPanelPanelScreenSettingAbout1);
    styleAboutRow(ui_PanelPanelScreenSettingAbout2, ui_LabelPanelPanelScreenSettingAboutMAC, ui_LabelPanelPanelScreenSettingAbout3);
    styleAboutRow(ui_PanelPanelScreenSettingAbout3, ui_LabelPanelPanelScreenSettingAboutUIFramework, ui_LabelPanelPanelScreenSettingAbout4);
    styleAboutRow(ui_PanelPanelScreenSettingAbout4, ui_LabelPanelPanelScreenSettingAboutSoftwareVersion, ui_LabelPanelPanelScreenSettingAbout5);
    styleAboutRow(ui_PanelPanelScreenSettingAbout5, ui_LabelPanelPanelScreenSettingAboutUIFrameworkVersion, ui_LabelPanelPanelScreenSettingAbout6);

    auto createAboutInfoCard = [](lv_obj_t *parent, const char *title, const char *body, lv_color_t body_color) {
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(card, 18, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(card, 16, 0);
        lv_obj_set_style_pad_right(card, 16, 0);
        lv_obj_set_style_pad_top(card, 14, 0);
        lv_obj_set_style_pad_bottom(card, 14, 0);

        lv_obj_t *title_label = lv_label_create(card);
        lv_label_set_text(title_label, title);
        lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(title_label, lv_color_hex(0x0F172A), 0);
        lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *body_label = lv_label_create(card);
        lv_obj_set_width(body_label, lv_pct(100));
        lv_label_set_long_mode(body_label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(body_label, body);
        lv_obj_set_style_text_font(body_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(body_label, body_color, 0);
        lv_obj_align_to(body_label, title_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

        return card;
    };

    createAboutInfoCard(
        ui_PanelScreenSettingAbout,
        "Project",
        "Custom ESP32-P4 firmware for the JC4880P443C_I_W profile, built on ESP-Brookesia and ESP-IDF 5.4.",
        lv_color_hex(0x334155)
    );
    createAboutInfoCard(
        ui_PanelScreenSettingAbout,
        "Hardware",
        "ESP32-P4 Function EV Board\n1024x600 display\nMIPI camera\nUSB-C and SD card support",
        lv_color_hex(0x334155)
    );
    createAboutInfoCard(
        ui_PanelScreenSettingAbout,
        "GitHub",
        "https://github.com/elik745i/JC4880P443C_I_W_Remote",
        lv_color_hex(0x2563EB)
    );

    lv_obj_t *wifiInfoCard = lv_obj_create(ui_PanelScreenSettingAbout);
    lv_obj_set_width(wifiInfoCard, lv_pct(100));
    lv_obj_set_height(wifiInfoCard, LV_SIZE_CONTENT);
    lv_obj_clear_flag(wifiInfoCard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(wifiInfoCard, 18, 0);
    lv_obj_set_style_border_width(wifiInfoCard, 0, 0);
    lv_obj_set_style_bg_color(wifiInfoCard, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(wifiInfoCard, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(wifiInfoCard, 16, 0);
    lv_obj_set_style_pad_right(wifiInfoCard, 16, 0);
    lv_obj_set_style_pad_top(wifiInfoCard, 14, 0);
    lv_obj_set_style_pad_bottom(wifiInfoCard, 14, 0);

    lv_obj_t *wifiInfoTitle = lv_label_create(wifiInfoCard);
    lv_label_set_text(wifiInfoTitle, "Current Wi-Fi");
    lv_obj_set_style_text_font(wifiInfoTitle, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(wifiInfoTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(wifiInfoTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    _aboutWifiValueLabel = lv_label_create(wifiInfoCard);
    lv_obj_set_width(_aboutWifiValueLabel, lv_pct(100));
    lv_label_set_long_mode(_aboutWifiValueLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_aboutWifiValueLabel, "Disconnected");
    lv_obj_set_style_text_font(_aboutWifiValueLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_aboutWifiValueLabel, lv_color_hex(0x334155), 0);
    lv_obj_align_to(_aboutWifiValueLabel, wifiInfoTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    // Record the screen index and install the screen loaded event callback
    _screen_list[UI_ABOUT_SETTING_INDEX] = ui_ScreenSettingAbout;
    lv_obj_add_event_cb(ui_ScreenSettingAbout, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);

    const esp_app_desc_t *app_desc = esp_app_get_description();
    lv_label_set_text(ui_LabelPanelPanelScreenSettingAbout3, mac_str);
    lv_label_set_text(ui_LabelPanelPanelScreenSettingAbout5,
                      ((app_desc != nullptr) && (app_desc->version[0] != '\0')) ? app_desc->version : "unknown");
    lv_label_set_text(ui_LabelPanelPanelScreenSettingAbout2, "JC4880P443C_I_W Remote\nESP32-P4 Function EV Board");

    char char_ui_version[20];
    snprintf(char_ui_version, sizeof(char_ui_version), "v%d.%d.%d", ESP_BROOKESIA_CONF_VER_MAJOR, ESP_BROOKESIA_CONF_VER_MINOR, ESP_BROOKESIA_CONF_VER_PATCH);
    lv_label_set_text(ui_LabelPanelPanelScreenSettingAbout6, char_ui_version);
    #if CONFIG_JC4880_FEATURE_ABOUT_DEVICE
    refreshAboutWifiUi();
    #endif
    #if APP_SETTINGS_FEATURE_WIFI
    refreshSavedWifiUi();
    #endif
    #if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    refreshBluetoothUi();
    #endif
    #if CONFIG_JC4880_FEATURE_SECURITY
    refreshSecurityUi();
    #endif
}

void AppSettings::processWifiConnect(WifiConnectState_t state)
{
    switch (state) {
    case WIFI_CONNECT_HIDE:
        lv_obj_add_flag(_panel_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_img_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_spinner_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        break;
    case WIFI_CONNECT_RUNNING:
        lv_obj_move_foreground(_panel_wifi_connect);
        lv_obj_clear_flag(_panel_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_img_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_spinner_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        break;
    case WIFI_CONNECT_SUCCESS:
        lv_obj_move_foreground(_panel_wifi_connect);
        lv_obj_clear_flag(_panel_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_img_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_img_set_src(_img_wifi_connect, &img_wifi_connect_success);
        lv_obj_add_flag(_spinner_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        break;
    case WIFI_CONNECT_FAIL:
        lv_obj_move_foreground(_panel_wifi_connect);
        lv_obj_clear_flag(_panel_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_img_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_img_set_src(_img_wifi_connect, &img_wifi_connect_fail);
        lv_obj_add_flag(_spinner_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        break;
    default:
        break;
    }
}

void AppSettings::ensureHardwareScreen(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    if ((_hardwareScreen != nullptr) && lv_obj_ready(_hardwareScreen)) {
        return;
    }

    _hardwareScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_hardwareScreen, lv_color_hex(0xE5F3FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_hardwareScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_hardwareScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hardwareTitle = lv_label_create(_hardwareScreen);
    lv_label_set_text(hardwareTitle, "Hardware Monitor");
    lv_obj_set_style_text_font(hardwareTitle, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(hardwareTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(hardwareTitle, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *hardwarePanel = lv_obj_create(_hardwareScreen);
    lv_obj_set_size(hardwarePanel, lv_pct(92), 650);
    lv_obj_align(hardwarePanel, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_style_radius(hardwarePanel, 20, 0);
    lv_obj_set_style_border_width(hardwarePanel, 0, 0);
    lv_obj_set_style_bg_color(hardwarePanel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_all(hardwarePanel, 14, 0);
    lv_obj_set_style_pad_row(hardwarePanel, 12, 0);
    lv_obj_set_flex_flow(hardwarePanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hardwarePanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(hardwarePanel, LV_DIR_VER);

    auto setupTrendCard = [&](HardwareTrendCardIndex index, lv_obj_t *card, lv_obj_t *detail_label) {
        if (!lv_obj_ready(card)) {
            return;
        }

        HardwareTrendUi &trend_ui = _hardwareTrendUi[index];
        trend_ui.card = card;
        trend_ui.expandLabel = nullptr;
        trend_ui.expandedArea = nullptr;
        trend_ui.historyTitleLabel = nullptr;
        trend_ui.historySummaryLabel = nullptr;
        trend_ui.historyChart = nullptr;
        trend_ui.historySeries = nullptr;
        trend_ui.historyLeftLabel = nullptr;
        trend_ui.historyRightLabel = nullptr;
        trend_ui.historyFooterLabel = nullptr;
        trend_ui.expanded = false;

        lv_obj_set_height(card, kHardwareTrendCardCollapsedHeight);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, onHardwareTrendCardClickedEventCallback, LV_EVENT_CLICKED, this);

        trend_ui.expandLabel = lv_label_create(card);
        lv_label_set_text(trend_ui.expandLabel, LV_SYMBOL_DOWN);
        lv_obj_set_style_text_font(trend_ui.expandLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(trend_ui.expandLabel, lv_color_hex(0x64748B), 0);
        lv_obj_align(trend_ui.expandLabel, LV_ALIGN_TOP_RIGHT, 0, 30);

        trend_ui.expandedArea = lv_obj_create(card);
        lv_obj_set_size(trend_ui.expandedArea, lv_pct(100), 338);
        lv_obj_align_to(trend_ui.expandedArea, detail_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 14);
        lv_obj_clear_flag(trend_ui.expandedArea, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(trend_ui.expandedArea, 16, 0);
        lv_obj_set_style_border_width(trend_ui.expandedArea, 0, 0);
        lv_obj_set_style_bg_color(trend_ui.expandedArea, lv_color_hex(0xEFF6FF), 0);
        lv_obj_set_style_bg_opa(trend_ui.expandedArea, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(trend_ui.expandedArea, 14, 0);
        lv_obj_set_style_pad_row(trend_ui.expandedArea, 10, 0);
        lv_obj_set_flex_flow(trend_ui.expandedArea, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(trend_ui.expandedArea, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        trend_ui.historyTitleLabel = lv_label_create(trend_ui.expandedArea);
        lv_obj_set_width(trend_ui.historyTitleLabel, lv_pct(100));
        lv_obj_set_style_text_font(trend_ui.historyTitleLabel, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(trend_ui.historyTitleLabel, lv_color_hex(0x0F172A), 0);

        trend_ui.historySummaryLabel = lv_label_create(trend_ui.expandedArea);
        lv_obj_set_width(trend_ui.historySummaryLabel, lv_pct(100));
        lv_label_set_long_mode(trend_ui.historySummaryLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(trend_ui.historySummaryLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(trend_ui.historySummaryLabel, lv_color_hex(0x475569), 0);

        trend_ui.historyChart = lv_chart_create(trend_ui.expandedArea);
        lv_obj_set_size(trend_ui.historyChart, lv_pct(100), 190);
        lv_obj_set_style_radius(trend_ui.historyChart, 14, 0);
        lv_obj_set_style_border_width(trend_ui.historyChart, 0, 0);
        lv_obj_set_style_bg_color(trend_ui.historyChart, lv_color_hex(0xDBEAFE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(trend_ui.historyChart, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_line_width(trend_ui.historyChart, 2, LV_PART_ITEMS);
        lv_obj_set_style_size(trend_ui.historyChart, 0, LV_PART_INDICATOR);
        lv_chart_set_type(trend_ui.historyChart, LV_CHART_TYPE_LINE);
        lv_chart_set_range(trend_ui.historyChart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        lv_chart_set_div_line_count(trend_ui.historyChart, 4, 6);
        lv_chart_set_point_count(trend_ui.historyChart, 2);
        trend_ui.historySeries = lv_chart_add_series(trend_ui.historyChart, lv_color_hex(0x2563EB), LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_all_value(trend_ui.historyChart, trend_ui.historySeries, 0);

        lv_obj_t *history_axis_row = lv_obj_create(trend_ui.expandedArea);
        lv_obj_set_width(history_axis_row, lv_pct(100));
        lv_obj_set_height(history_axis_row, LV_SIZE_CONTENT);
        lv_obj_clear_flag(history_axis_row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(history_axis_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(history_axis_row, 0, 0);
        lv_obj_set_style_pad_all(history_axis_row, 0, 0);
        lv_obj_set_flex_flow(history_axis_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(history_axis_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        trend_ui.historyLeftLabel = lv_label_create(history_axis_row);
        lv_obj_set_style_text_font(trend_ui.historyLeftLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(trend_ui.historyLeftLabel, lv_color_hex(0x64748B), 0);

        trend_ui.historyRightLabel = lv_label_create(history_axis_row);
        lv_obj_set_style_text_font(trend_ui.historyRightLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(trend_ui.historyRightLabel, lv_color_hex(0x64748B), 0);

        trend_ui.historyFooterLabel = lv_label_create(trend_ui.expandedArea);
        lv_obj_set_width(trend_ui.historyFooterLabel, lv_pct(100));
        lv_label_set_long_mode(trend_ui.historyFooterLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(trend_ui.historyFooterLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(trend_ui.historyFooterLabel, lv_color_hex(0x475569), 0);

        enableEventBubbleRecursively(card);
    };

    setupTrendCard(HARDWARE_TREND_CPU_LOAD,
                   create_monitor_card(hardwarePanel, "CPU Load", "Processor activity over the last hour", &_hardwareCpuSpeedValueLabel,
                                       &_hardwareCpuSpeedDetailLabel, &_hardwareCpuSpeedBar),
                   _hardwareCpuSpeedDetailLabel);
#if CONFIG_JC4880_FEATURE_BATTERY
    _hardwareBatteryCard = create_monitor_card(hardwarePanel,
                                               "Battery",
                                               "Charge level, history, and ETA",
                                               &_hardwareBatteryValueLabel,
                                               &_hardwareBatteryDetailLabel,
                                               &_hardwareBatteryBar);
    if (lv_obj_ready(_hardwareBatteryCard)) {
        lv_obj_set_height(_hardwareBatteryCard, kBatteryCardCollapsedHeight);
        lv_obj_add_flag(_hardwareBatteryCard, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(_hardwareBatteryCard, onHardwareBatteryCardClickedEventCallback, LV_EVENT_CLICKED, this);

        _hardwareBatteryExpandLabel = lv_label_create(_hardwareBatteryCard);
        lv_label_set_text(_hardwareBatteryExpandLabel, LV_SYMBOL_DOWN);
        lv_obj_set_style_text_font(_hardwareBatteryExpandLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_hardwareBatteryExpandLabel, lv_color_hex(0x64748B), 0);
        lv_obj_align(_hardwareBatteryExpandLabel, LV_ALIGN_TOP_RIGHT, 0, 30);

        _hardwareBatteryExpandedArea = lv_obj_create(_hardwareBatteryCard);
        lv_obj_set_size(_hardwareBatteryExpandedArea, lv_pct(100), 338);
        lv_obj_align_to(_hardwareBatteryExpandedArea, _hardwareBatteryDetailLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 14);
        lv_obj_clear_flag(_hardwareBatteryExpandedArea, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(_hardwareBatteryExpandedArea, 16, 0);
        lv_obj_set_style_border_width(_hardwareBatteryExpandedArea, 0, 0);
        lv_obj_set_style_bg_color(_hardwareBatteryExpandedArea, lv_color_hex(0xEFF6FF), 0);
        lv_obj_set_style_bg_opa(_hardwareBatteryExpandedArea, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(_hardwareBatteryExpandedArea, 14, 0);
        lv_obj_set_style_pad_row(_hardwareBatteryExpandedArea, 10, 0);
        lv_obj_set_flex_flow(_hardwareBatteryExpandedArea, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(_hardwareBatteryExpandedArea, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        _hardwareBatteryHistoryTitleLabel = lv_label_create(_hardwareBatteryExpandedArea);
        lv_obj_set_width(_hardwareBatteryHistoryTitleLabel, lv_pct(100));
        lv_obj_set_style_text_font(_hardwareBatteryHistoryTitleLabel, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(_hardwareBatteryHistoryTitleLabel, lv_color_hex(0x0F172A), 0);

        _hardwareBatteryHistorySummaryLabel = lv_label_create(_hardwareBatteryExpandedArea);
        lv_obj_set_width(_hardwareBatteryHistorySummaryLabel, lv_pct(100));
        lv_label_set_long_mode(_hardwareBatteryHistorySummaryLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(_hardwareBatteryHistorySummaryLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_hardwareBatteryHistorySummaryLabel, lv_color_hex(0x475569), 0);

        _hardwareBatteryHistoryChart = lv_chart_create(_hardwareBatteryExpandedArea);
        lv_obj_set_size(_hardwareBatteryHistoryChart, lv_pct(100), 190);
        lv_obj_set_style_radius(_hardwareBatteryHistoryChart, 14, 0);
        lv_obj_set_style_border_width(_hardwareBatteryHistoryChart, 0, 0);
        lv_obj_set_style_bg_color(_hardwareBatteryHistoryChart, lv_color_hex(0xDBEAFE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_hardwareBatteryHistoryChart, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_line_width(_hardwareBatteryHistoryChart, 2, LV_PART_ITEMS);
        lv_obj_set_style_size(_hardwareBatteryHistoryChart, 0, LV_PART_INDICATOR);
        lv_chart_set_type(_hardwareBatteryHistoryChart, LV_CHART_TYPE_LINE);
        lv_chart_set_range(_hardwareBatteryHistoryChart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        lv_chart_set_div_line_count(_hardwareBatteryHistoryChart, 4, 6);
        lv_chart_set_point_count(_hardwareBatteryHistoryChart, 2);
        _hardwareBatteryHistorySeries = lv_chart_add_series(_hardwareBatteryHistoryChart,
                                                            lv_color_hex(0xF59E0B),
                                                            LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_all_value(_hardwareBatteryHistoryChart, _hardwareBatteryHistorySeries, 0);

        lv_obj_t *historyAxisRow = lv_obj_create(_hardwareBatteryExpandedArea);
        lv_obj_set_width(historyAxisRow, lv_pct(100));
        lv_obj_set_height(historyAxisRow, LV_SIZE_CONTENT);
        lv_obj_clear_flag(historyAxisRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(historyAxisRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(historyAxisRow, 0, 0);
        lv_obj_set_style_pad_all(historyAxisRow, 0, 0);
        lv_obj_set_flex_flow(historyAxisRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(historyAxisRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        _hardwareBatteryHistoryLeftLabel = lv_label_create(historyAxisRow);
        lv_obj_set_style_text_font(_hardwareBatteryHistoryLeftLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_hardwareBatteryHistoryLeftLabel, lv_color_hex(0x64748B), 0);

        _hardwareBatteryHistoryRightLabel = lv_label_create(historyAxisRow);
        lv_obj_set_style_text_font(_hardwareBatteryHistoryRightLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_hardwareBatteryHistoryRightLabel, lv_color_hex(0x64748B), 0);

        _hardwareBatteryHistoryFooterLabel = lv_label_create(_hardwareBatteryExpandedArea);
        lv_obj_set_width(_hardwareBatteryHistoryFooterLabel, lv_pct(100));
        lv_label_set_long_mode(_hardwareBatteryHistoryFooterLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(_hardwareBatteryHistoryFooterLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_hardwareBatteryHistoryFooterLabel, lv_color_hex(0x475569), 0);

        enableEventBubbleRecursively(_hardwareBatteryCard);
    }
#endif
    setupTrendCard(HARDWARE_TREND_SRAM,
                   create_monitor_card(hardwarePanel, "SRAM", "Occupied versus total internal memory", &_hardwareSramValueLabel,
                                       &_hardwareSramDetailLabel, &_hardwareSramBar),
                   _hardwareSramDetailLabel);
    setupTrendCard(HARDWARE_TREND_PSRAM,
                   create_monitor_card(hardwarePanel, "PSRAM", "Occupied versus total external memory", &_hardwarePsramValueLabel,
                                       &_hardwarePsramDetailLabel, &_hardwarePsramBar),
                   _hardwarePsramDetailLabel);
    setupTrendCard(HARDWARE_TREND_CPU_TEMP,
                   create_monitor_card(hardwarePanel, "CPU Temperature", "On-die sensor reading over the last hour", &_hardwareCpuTempValueLabel,
                                       &_hardwareCpuTempDetailLabel, &_hardwareCpuTempBar),
                   _hardwareCpuTempDetailLabel);
    create_monitor_card(hardwarePanel, "SD Card Storage", "Used versus total mounted capacity", &_hardwareSdValueLabel,
                        &_hardwareSdDetailLabel, &_hardwareSdBar);
    setupTrendCard(HARDWARE_TREND_WIFI,
                   create_monitor_card(hardwarePanel, "Wi-Fi Signal", "Current station RSSI and last-hour history", &_hardwareWifiValueLabel,
                                       &_hardwareWifiDetailLabel, &_hardwareWifiBar),
                   _hardwareWifiDetailLabel);

    if (_hardwareCpuSpeedDetailLabel != nullptr) {
        lv_label_set_text(_hardwareCpuSpeedDetailLabel, "Tap to expand history.");
        lv_obj_set_style_text_font(_hardwareCpuSpeedDetailLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_hardwareCpuSpeedDetailLabel, lv_color_hex(0x475569), 0);
    }
    if (_hardwareCpuSpeedBar != nullptr) {
        lv_bar_set_value(_hardwareCpuSpeedBar, 100, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_hardwareCpuSpeedBar, lv_color_hex(0x2563EB), LV_PART_INDICATOR);
    }

    _screen_list[UI_HARDWARE_SETTING_INDEX] = _hardwareScreen;
    lv_obj_add_event_cb(_hardwareScreen, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
#endif
}

void AppSettings::ensureZigbeeScreen(void)
{
#if !CONFIG_JC4880_FEATURE_ZIGBEE
    return;
#else
    if ((_zigbeeScreen != nullptr) && lv_obj_ready(_zigbeeScreen)) {
        return;
    }

    _zigbeeScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_zigbeeScreen, lv_color_hex(0xE5F3FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_zigbeeScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_zigbeeScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *zigbeeBackButton = lv_btn_create(_zigbeeScreen);
    lv_obj_set_size(zigbeeBackButton, 60, 60);
    lv_obj_align(zigbeeBackButton, LV_ALIGN_TOP_LEFT, 18, 18);
    lv_obj_set_style_bg_color(zigbeeBackButton, lv_color_hex(0xE5F3FF), 0);
    lv_obj_set_style_border_width(zigbeeBackButton, 0, 0);
    lv_obj_add_event_cb(zigbeeBackButton, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            lv_scr_load_anim(ui_ScreenSettingMain, LV_SCR_LOAD_ANIM_MOVE_RIGHT, kSettingScreenAnimTimeMs, 0, false);
        }
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *zigbeeBackImage = lv_img_create(zigbeeBackButton);
    lv_img_set_src(zigbeeBackImage, &ui_img_return_png);
    lv_obj_center(zigbeeBackImage);
    lv_obj_set_style_img_recolor(zigbeeBackImage, lv_color_hex(0x000000), 0);
    lv_obj_set_style_img_recolor_opa(zigbeeBackImage, 255, 0);

    lv_obj_t *zigbeeTitle = lv_label_create(_zigbeeScreen);
    lv_label_set_text(zigbeeTitle, "ZigBee");
    lv_obj_set_style_text_font(zigbeeTitle, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(zigbeeTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(zigbeeTitle, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *zigbeeTitleBadge = lv_obj_create(_zigbeeScreen);
    lv_obj_set_size(zigbeeTitleBadge, 38, 38);
    lv_obj_align_to(zigbeeTitleBadge, zigbeeTitle, LV_ALIGN_OUT_LEFT_MID, -16, 0);
    lv_obj_clear_flag(zigbeeTitleBadge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(zigbeeTitleBadge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(zigbeeTitleBadge, 0, 0);
    lv_obj_set_style_bg_color(zigbeeTitleBadge, lv_color_hex(0xD97706), 0);
    lv_obj_set_style_bg_opa(zigbeeTitleBadge, LV_OPA_COVER, 0);

    lv_obj_t *zigbeeTitleBadgeLabel = lv_label_create(zigbeeTitleBadge);
    lv_label_set_text(zigbeeTitleBadgeLabel, "ZB");
    lv_obj_set_style_text_font(zigbeeTitleBadgeLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(zigbeeTitleBadgeLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(zigbeeTitleBadgeLabel);

    lv_obj_t *zigbeePanel = lv_obj_create(_zigbeeScreen);
    lv_obj_set_size(zigbeePanel, lv_pct(92), 650);
    lv_obj_align(zigbeePanel, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_style_radius(zigbeePanel, 20, 0);
    lv_obj_set_style_border_width(zigbeePanel, 0, 0);
    lv_obj_set_style_bg_color(zigbeePanel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_all(zigbeePanel, 14, 0);
    lv_obj_set_style_pad_row(zigbeePanel, 12, 0);
    lv_obj_set_flex_flow(zigbeePanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(zigbeePanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(zigbeePanel, LV_DIR_VER);

    lv_obj_t *zigbeeEnableRow = create_settings_toggle_row(zigbeePanel, "Enable ZigBee");
    _zigbeeEnableSwitch = lv_switch_create(zigbeeEnableRow);
    lv_obj_align(_zigbeeEnableSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_zigbeeEnableSwitch, onZigbeeEnableSwitchValueChangeEventCallback, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *zigbeeRoleCard = lv_obj_create(zigbeePanel);
    lv_obj_set_width(zigbeeRoleCard, lv_pct(100));
    lv_obj_set_height(zigbeeRoleCard, LV_SIZE_CONTENT);
    lv_obj_clear_flag(zigbeeRoleCard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(zigbeeRoleCard, 18, 0);
    lv_obj_set_style_border_width(zigbeeRoleCard, 0, 0);
    lv_obj_set_style_bg_color(zigbeeRoleCard, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(zigbeeRoleCard, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(zigbeeRoleCard, 16, 0);

    lv_obj_t *zigbeeRoleTitle = lv_label_create(zigbeeRoleCard);
    lv_label_set_text(zigbeeRoleTitle, "Coordinator Role");
    lv_obj_set_style_text_font(zigbeeRoleTitle, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(zigbeeRoleTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(zigbeeRoleTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    _zigbeeRoleValueLabel = lv_label_create(zigbeeRoleCard);
    lv_obj_set_width(_zigbeeRoleValueLabel, lv_pct(100));
    lv_label_set_long_mode(_zigbeeRoleValueLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_zigbeeRoleValueLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_zigbeeRoleValueLabel, lv_color_hex(0x475569), 0);
    lv_obj_align_to(_zigbeeRoleValueLabel, zigbeeRoleTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    lv_obj_t *zigbeeNameCard = lv_obj_create(zigbeePanel);
    lv_obj_set_width(zigbeeNameCard, lv_pct(100));
    lv_obj_set_height(zigbeeNameCard, LV_SIZE_CONTENT);
    lv_obj_clear_flag(zigbeeNameCard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(zigbeeNameCard, 18, 0);
    lv_obj_set_style_border_width(zigbeeNameCard, 0, 0);
    lv_obj_set_style_bg_color(zigbeeNameCard, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(zigbeeNameCard, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(zigbeeNameCard, 16, 0);

    lv_obj_t *zigbeeNameTitle = lv_label_create(zigbeeNameCard);
    lv_label_set_text(zigbeeNameTitle, "Device Name");
    lv_obj_set_style_text_font(zigbeeNameTitle, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(zigbeeNameTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(zigbeeNameTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    _zigbeeNameTextArea = lv_textarea_create(zigbeeNameCard);
    lv_obj_set_size(_zigbeeNameTextArea, lv_pct(100), 58);
    lv_obj_align_to(_zigbeeNameTextArea, zigbeeNameTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_textarea_set_one_line(_zigbeeNameTextArea, true);
    lv_textarea_set_max_length(_zigbeeNameTextArea, 31);
    lv_textarea_set_placeholder_text(_zigbeeNameTextArea, "Enter ZigBee device name");
    lv_obj_set_style_radius(_zigbeeNameTextArea, 16, 0);
    lv_obj_set_style_border_width(_zigbeeNameTextArea, 1, 0);
    lv_obj_set_style_border_color(_zigbeeNameTextArea, lv_color_hex(0xC6D4E1), 0);
    lv_obj_set_style_bg_color(_zigbeeNameTextArea, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_opa(_zigbeeNameTextArea, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(_zigbeeNameTextArea, 16, 0);
    lv_obj_set_style_text_font(_zigbeeNameTextArea, &lv_font_montserrat_20, 0);
    lv_obj_add_event_cb(_zigbeeNameTextArea, onZigbeeNameTextAreaEventCallback, LV_EVENT_ALL, this);

    _zigbeeNameSaveButton = lv_btn_create(zigbeeNameCard);
    lv_obj_set_size(_zigbeeNameSaveButton, 120, 44);
    lv_obj_align(_zigbeeNameSaveButton, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_radius(_zigbeeNameSaveButton, 14, 0);
    lv_obj_set_style_border_width(_zigbeeNameSaveButton, 0, 0);
    lv_obj_set_style_bg_color(_zigbeeNameSaveButton, lv_color_hex(0xD97706), 0);
    lv_obj_add_event_cb(_zigbeeNameSaveButton, onZigbeeNameSaveClickedEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t *zigbeeNameSaveLabel = lv_label_create(_zigbeeNameSaveButton);
    lv_label_set_text(zigbeeNameSaveLabel, "Save Name");
    lv_obj_set_style_text_font(zigbeeNameSaveLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(zigbeeNameSaveLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(zigbeeNameSaveLabel);

    lv_obj_t *zigbeeChannelRow = create_settings_toggle_row(zigbeePanel, "Preferred Channel");
    _zigbeeChannelDropdown = lv_dropdown_create(zigbeeChannelRow);
    lv_dropdown_set_options_static(_zigbeeChannelDropdown, kZigbeeChannelOptionsText);
    lv_obj_set_width(_zigbeeChannelDropdown, 150);
    lv_obj_align(_zigbeeChannelDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_zigbeeChannelDropdown, onZigbeeChannelChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *zigbeePermitJoinRow = create_settings_toggle_row(zigbeePanel, "Permit Joining");
    _zigbeePermitJoinDropdown = lv_dropdown_create(zigbeePermitJoinRow);
    lv_dropdown_set_options_static(_zigbeePermitJoinDropdown, kZigbeePermitJoinOptionsText);
    lv_obj_set_width(_zigbeePermitJoinDropdown, 150);
    lv_obj_align(_zigbeePermitJoinDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_zigbeePermitJoinDropdown, onZigbeePermitJoinChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

    _zigbeeConfigSummaryLabel = lv_label_create(zigbeePanel);
    lv_obj_set_width(_zigbeeConfigSummaryLabel, lv_pct(100));
    lv_label_set_long_mode(_zigbeeConfigSummaryLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_zigbeeConfigSummaryLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_zigbeeConfigSummaryLabel, lv_color_hex(0x334155), 0);

    _zigbeeInfoLabel = lv_label_create(zigbeePanel);
    lv_obj_set_width(_zigbeeInfoLabel, lv_pct(100));
    lv_label_set_long_mode(_zigbeeInfoLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_zigbeeInfoLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_zigbeeInfoLabel, lv_color_hex(0x475569), 0);

    _zigbeeKeyboard = lv_keyboard_create(_zigbeeScreen);
    lv_obj_set_size(_zigbeeKeyboard, lv_pct(100), lv_pct(34));
    lv_obj_align(_zigbeeKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(_zigbeeKeyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_zigbeeKeyboard, onZigbeeKeyboardEventCallback, LV_EVENT_ALL, this);

    _screen_list[UI_ZIGBEE_SETTING_INDEX] = _zigbeeScreen;
    lv_obj_add_event_cb(_zigbeeScreen, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
#endif
}

void AppSettings::ensureSecurityScreen(void)
{
#if !CONFIG_JC4880_FEATURE_SECURITY
    return;
#else
    if ((_securityScreen != nullptr) && lv_obj_ready(_securityScreen)) {
        return;
    }

    _securityScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_securityScreen, lv_color_hex(0xE5F3FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_securityScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_securityScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *securityBackButton = lv_btn_create(_securityScreen);
    lv_obj_set_size(securityBackButton, 60, 60);
    lv_obj_align(securityBackButton, LV_ALIGN_TOP_LEFT, 18, 18);
    lv_obj_set_style_bg_color(securityBackButton, lv_color_hex(0xE5F3FF), 0);
    lv_obj_set_style_border_width(securityBackButton, 0, 0);
    lv_obj_add_event_cb(securityBackButton, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            lv_scr_load_anim(ui_ScreenSettingMain, LV_SCR_LOAD_ANIM_MOVE_RIGHT, kSettingScreenAnimTimeMs, 0, false);
        }
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *securityBackImage = lv_img_create(securityBackButton);
    lv_img_set_src(securityBackImage, &ui_img_return_png);
    lv_obj_center(securityBackImage);
    lv_obj_set_style_img_recolor(securityBackImage, lv_color_hex(0x000000), 0);
    lv_obj_set_style_img_recolor_opa(securityBackImage, 255, 0);

    lv_obj_t *securityTitle = lv_label_create(_securityScreen);
    lv_label_set_text(securityTitle, "Security");
    lv_obj_set_style_text_font(securityTitle, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(securityTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(securityTitle, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *securityPanel = lv_obj_create(_securityScreen);
    lv_obj_set_size(securityPanel, lv_pct(92), 650);
    lv_obj_align(securityPanel, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_style_radius(securityPanel, 20, 0);
    lv_obj_set_style_border_width(securityPanel, 0, 0);
    lv_obj_set_style_bg_color(securityPanel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_all(securityPanel, 14, 0);
    lv_obj_set_style_pad_row(securityPanel, 12, 0);
    lv_obj_set_flex_flow(securityPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(securityPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(securityPanel, LV_DIR_VER);

    lv_obj_t *deviceLockRow = create_settings_toggle_row(securityPanel, "Device Lock");
    _securityDeviceLockSwitch = lv_switch_create(deviceLockRow);
    lv_obj_align(_securityDeviceLockSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_securityDeviceLockSwitch, onSwitchPanelScreenSettingBLESwitchValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *settingsLockRow = create_settings_toggle_row(securityPanel, "Settings Lock");
    _securitySettingsLockSwitch = lv_switch_create(settingsLockRow);
    lv_obj_align(_securitySettingsLockSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_securitySettingsLockSwitch, onSwitchPanelScreenSettingSettingsLockValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    _securityInfoLabel = lv_label_create(securityPanel);
    lv_obj_set_width(_securityInfoLabel, lv_pct(100));
    lv_label_set_long_mode(_securityInfoLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_securityInfoLabel,
                      "Enabling a lock asks for a new 4-digit PIN. Disabling it asks for the existing PIN.");
    lv_obj_set_style_text_font(_securityInfoLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_securityInfoLabel, lv_color_hex(0x475569), 0);

    _screen_list[UI_SECURITY_SETTING_INDEX] = _securityScreen;
    lv_obj_add_event_cb(_securityScreen, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
#endif
}

void AppSettings::ensureFirmwareScreen(void)
{
#if !CONFIG_JC4880_FEATURE_OTA
    return;
#else
    if ((_firmwareScreen != nullptr) && lv_obj_ready(_firmwareScreen)) {
        return;
    }

    _firmwareScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_firmwareScreen, lv_color_hex(0xE5F3FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_firmwareScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_firmwareScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *firmwareBackButton = lv_btn_create(_firmwareScreen);
    lv_obj_set_size(firmwareBackButton, 60, 60);
    lv_obj_align(firmwareBackButton, LV_ALIGN_TOP_LEFT, 18, 18);
    lv_obj_set_style_bg_color(firmwareBackButton, lv_color_hex(0xE5F3FF), 0);
    lv_obj_set_style_border_width(firmwareBackButton, 0, 0);
    lv_obj_add_event_cb(firmwareBackButton, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            lv_scr_load_anim(ui_ScreenSettingMain, LV_SCR_LOAD_ANIM_MOVE_RIGHT, kSettingScreenAnimTimeMs, 0, false);
        }
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *firmwareBackImage = lv_img_create(firmwareBackButton);
    lv_img_set_src(firmwareBackImage, &ui_img_return_png);
    lv_obj_center(firmwareBackImage);
    lv_obj_set_style_img_recolor(firmwareBackImage, lv_color_hex(0x000000), 0);
    lv_obj_set_style_img_recolor_opa(firmwareBackImage, 255, 0);

    lv_obj_t *firmwareTitle = lv_label_create(_firmwareScreen);
    lv_label_set_text(firmwareTitle, "Firmware");
    lv_obj_set_style_text_font(firmwareTitle, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(firmwareTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(firmwareTitle, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *firmwarePanel = lv_obj_create(_firmwareScreen);
    lv_obj_set_size(firmwarePanel, lv_pct(92), 650);
    lv_obj_align(firmwarePanel, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_style_radius(firmwarePanel, 20, 0);
    lv_obj_set_style_border_width(firmwarePanel, 0, 0);
    lv_obj_set_style_bg_color(firmwarePanel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_all(firmwarePanel, 14, 0);
    lv_obj_set_style_pad_row(firmwarePanel, 12, 0);
    lv_obj_set_flex_flow(firmwarePanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(firmwarePanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(firmwarePanel, LV_DIR_VER);

    auto createFirmwareSection = [](lv_obj_t *parent, const char *title) {
        lv_obj_t *section = lv_obj_create(parent);
        lv_obj_set_width(section, lv_pct(100));
        lv_obj_set_height(section, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(section, 18, 0);
        lv_obj_set_style_border_width(section, 0, 0);
        lv_obj_set_style_bg_color(section, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_pad_all(section, 14, 0);
        lv_obj_set_style_pad_row(section, 10, 0);
        lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t *sectionTitle = lv_label_create(section);
        lv_label_set_text(sectionTitle, title);
        lv_obj_set_style_text_font(sectionTitle, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(sectionTitle, lv_color_hex(0x0F172A), 0);
        return section;
    };

    auto createFirmwareControlsRow = [](lv_obj_t *parent) {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_row(row, 8, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        return row;
    };

    lv_obj_t *currentSection = createFirmwareSection(firmwarePanel, "Installed Firmware");
    _firmwareCurrentVersionLabel = lv_label_create(currentSection);
    lv_obj_set_width(_firmwareCurrentVersionLabel, lv_pct(100));
    lv_label_set_long_mode(_firmwareCurrentVersionLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_firmwareCurrentVersionLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(_firmwareCurrentVersionLabel, lv_color_hex(0x0F172A), 0);

    lv_obj_t *sdSection = createFirmwareSection(firmwarePanel, "Flash from SD Card");
    lv_obj_t *sdHint = lv_label_create(sdSection);
    lv_obj_set_width(sdHint, lv_pct(100));
    lv_label_set_long_mode(sdHint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(sdHint, "Select a validated .bin firmware image from /sdcard or /sdcard/firmware.");
    lv_obj_set_style_text_font(sdHint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sdHint, lv_color_hex(0x475569), 0);

    lv_obj_t *sdControlsRow = createFirmwareControlsRow(sdSection);

    _firmwareSdDropdown = lv_dropdown_create(sdControlsRow);
    lv_obj_set_size(_firmwareSdDropdown, 220, 48);
    lv_obj_set_flex_grow(_firmwareSdDropdown, 1);
    lv_obj_add_event_cb(_firmwareSdDropdown, onFirmwareSelectionChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *sdRefreshButton = lv_btn_create(sdControlsRow);
    lv_obj_set_size(sdRefreshButton, 92, 48);
    lv_obj_set_style_radius(sdRefreshButton, 16, 0);
    lv_obj_set_style_border_width(sdRefreshButton, 0, 0);
    lv_obj_set_style_bg_color(sdRefreshButton, lv_color_hex(0xCBD5E1), 0);
    lv_obj_add_event_cb(sdRefreshButton, onFirmwareSdRefreshClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *sdRefreshLabel = lv_label_create(sdRefreshButton);
    lv_label_set_text(sdRefreshLabel, "Scan");
    lv_obj_center(sdRefreshLabel);

    _firmwareSdFlashButton = lv_btn_create(sdControlsRow);
    lv_obj_set_size(_firmwareSdFlashButton, 92, 48);
    lv_obj_set_style_radius(_firmwareSdFlashButton, 16, 0);
    lv_obj_set_style_border_width(_firmwareSdFlashButton, 0, 0);
    lv_obj_add_event_cb(_firmwareSdFlashButton, onFirmwareSdFlashClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *sdFlashLabel = lv_label_create(_firmwareSdFlashButton);
    lv_label_set_text(sdFlashLabel, "Flash");
    lv_obj_center(sdFlashLabel);

    lv_obj_t *otaSection = createFirmwareSection(firmwarePanel, "Check GitHub Releases");
    lv_obj_t *otaHint = lv_label_create(otaSection);
    lv_obj_set_width(otaHint, lv_pct(100));
    lv_label_set_long_mode(otaHint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(otaHint, "Check GitHub releases, review installed and latest versions below, then tick one firmware to flash.");
    lv_obj_set_style_text_font(otaHint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(otaHint, lv_color_hex(0x475569), 0);

    lv_obj_t *otaControlsRow = createFirmwareControlsRow(otaSection);

    _firmwareOtaCheckButton = lv_btn_create(otaControlsRow);
    lv_obj_set_size(_firmwareOtaCheckButton, 96, 48);
    lv_obj_set_style_radius(_firmwareOtaCheckButton, 16, 0);
    lv_obj_set_style_border_width(_firmwareOtaCheckButton, 0, 0);
    lv_obj_add_event_cb(_firmwareOtaCheckButton, onFirmwareOtaCheckClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *otaCheckLabel = lv_label_create(_firmwareOtaCheckButton);
    lv_label_set_text(otaCheckLabel, "Check");
    lv_obj_center(otaCheckLabel);

    _firmwareOtaFlashButton = lv_btn_create(otaControlsRow);
    lv_obj_set_size(_firmwareOtaFlashButton, 92, 48);
    lv_obj_set_style_radius(_firmwareOtaFlashButton, 16, 0);
    lv_obj_set_style_border_width(_firmwareOtaFlashButton, 0, 0);
    lv_obj_add_event_cb(_firmwareOtaFlashButton, onFirmwareOtaFlashClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *otaFlashLabel = lv_label_create(_firmwareOtaFlashButton);
    lv_label_set_text(otaFlashLabel, "Flash");
    lv_obj_center(otaFlashLabel);

    _firmwareOtaSummaryLabel = lv_label_create(otaSection);
    lv_obj_set_width(_firmwareOtaSummaryLabel, lv_pct(100));
    lv_label_set_long_mode(_firmwareOtaSummaryLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_firmwareOtaSummaryLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_firmwareOtaSummaryLabel, lv_color_hex(0x334155), 0);

    _firmwareOtaListContainer = lv_obj_create(otaSection);
    lv_obj_set_width(_firmwareOtaListContainer, lv_pct(100));
    lv_obj_set_height(_firmwareOtaListContainer, 210);
    lv_obj_set_style_radius(_firmwareOtaListContainer, 16, 0);
    lv_obj_set_style_bg_color(_firmwareOtaListContainer, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_border_color(_firmwareOtaListContainer, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_border_width(_firmwareOtaListContainer, 1, 0);
    lv_obj_set_style_pad_all(_firmwareOtaListContainer, 12, 0);
    lv_obj_set_style_pad_row(_firmwareOtaListContainer, 8, 0);
    lv_obj_set_scroll_dir(_firmwareOtaListContainer, LV_DIR_VER);
    lv_obj_set_flex_flow(_firmwareOtaListContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_firmwareOtaListContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    _firmwareProgressBar = lv_bar_create(firmwarePanel);
    lv_obj_set_width(_firmwareProgressBar, lv_pct(100));
    lv_obj_set_height(_firmwareProgressBar, 18);
    lv_bar_set_range(_firmwareProgressBar, 0, 100);
    lv_bar_set_value(_firmwareProgressBar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(_firmwareProgressBar, 9, 0);
    lv_obj_set_style_bg_color(_firmwareProgressBar, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_firmwareProgressBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_firmwareProgressBar, lv_color_hex(0x2563EB), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_firmwareProgressBar, LV_OPA_COVER, LV_PART_INDICATOR);

    _firmwareProgressLabel = lv_label_create(firmwarePanel);
    lv_obj_set_width(_firmwareProgressLabel, lv_pct(100));
    lv_label_set_long_mode(_firmwareProgressLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_firmwareProgressLabel, "Idle");
    lv_obj_set_style_text_font(_firmwareProgressLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_firmwareProgressLabel, lv_color_hex(0x64748B), 0);

    _firmwareStatusLabel = lv_label_create(firmwarePanel);
    lv_obj_set_width(_firmwareStatusLabel, lv_pct(100));
    lv_label_set_long_mode(_firmwareStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_firmwareStatusLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_firmwareStatusLabel, lv_color_hex(0x334155), 0);

    lv_obj_t *dangerSection = createFirmwareSection(firmwarePanel, "Danger Zone");
    lv_obj_t *dangerHint = lv_label_create(dangerSection);
    lv_obj_set_width(dangerHint, lv_pct(100));
    lv_label_set_long_mode(dangerHint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(dangerHint, "Factory reset clears saved Settings preferences including Wi-Fi credentials, display, audio, and timezone options.");
    lv_obj_set_style_text_font(dangerHint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(dangerHint, lv_color_hex(0x7F1D1D), 0);

    lv_obj_t *dangerControlsRow = createFirmwareControlsRow(dangerSection);
    lv_obj_t *factoryResetButton = lv_btn_create(dangerControlsRow);
    lv_obj_set_size(factoryResetButton, 170, 50);
    lv_obj_set_style_radius(factoryResetButton, 16, 0);
    lv_obj_set_style_border_width(factoryResetButton, 0, 0);
    lv_obj_set_style_bg_color(factoryResetButton, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_bg_opa(factoryResetButton, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(factoryResetButton, lv_color_hex(0xB91C1C), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(factoryResetButton, onFirmwareFactoryResetClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *factoryResetLabel = lv_label_create(factoryResetButton);
    lv_label_set_text(factoryResetLabel, "Factory Reset");
    lv_obj_set_style_text_color(factoryResetLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(factoryResetLabel);

    _screen_list[UI_FIRMWARE_SETTING_INDEX] = _firmwareScreen;
    lv_obj_add_event_cb(_firmwareScreen, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
#endif
}

bool AppSettings::loadNvsParam(void)
{
    esp_err_t err = ESP_OK;
    nvs_handle_t nvs_handle;
    err = nvs_open(NVS_STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return false;
    }

    for (auto& key_value : _nvs_param_map) {
        err = nvs_get_i32(nvs_handle, key_value.first.c_str(), &key_value.second);
        switch (err) {
        case ESP_OK:
            ESP_LOGI(TAG, "Load %s: %d", key_value.first.c_str(), key_value.second);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            err = nvs_set_i32(nvs_handle, key_value.first.c_str(), key_value.second);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error (%s) setting %s", esp_err_to_name(err), key_value.first.c_str());
            }
            ESP_LOGW(TAG, "The value of %s is not initialized yet, set it to default value: %d", key_value.first.c_str(),
                     key_value.second);
            break;
        default:
            break;
        }
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) committing NVS changes", esp_err_to_name(err));
        return false;
    }
    nvs_close(nvs_handle);

    return true;
}

bool AppSettings::setNvsParam(std::string key, int value)
{
    esp_err_t err = ESP_OK;
    nvs_handle_t nvs_handle;
    err = nvs_open(NVS_STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_i32(nvs_handle, key.c_str(), value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) setting %s", esp_err_to_name(err), key.c_str());
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) committing NVS changes", esp_err_to_name(err));
        return false;
    }
    nvs_close(nvs_handle);

    return true;
}

bool AppSettings::loadNvsStringParam(const char *key, char *buffer, size_t buffer_size)
{
    if ((key == nullptr) || (buffer == nullptr) || (buffer_size == 0)) {
        return false;
    }

    buffer[0] = '\0';

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle for %s", esp_err_to_name(err), key);
        return false;
    }

    size_t required_size = buffer_size;
    err = nvs_get_str(nvs_handle, key, buffer, &required_size);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        return true;
    }

    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error (%s) reading %s", esp_err_to_name(err), key);
    }
    buffer[0] = '\0';
    return false;
}

bool AppSettings::setNvsStringParam(const char *key, const char *value)
{
    if ((key == nullptr) || (value == nullptr)) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle for %s", esp_err_to_name(err), key);
        return false;
    }

    err = nvs_set_str(nvs_handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) saving %s", esp_err_to_name(err), key);
        return false;
    }

    return true;
}


bool AppSettings::factoryResetPreferences(void)
{
    bool ok = true;
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle for factory reset", esp_err_to_name(err));
        return false;
    }

    err = nvs_erase_all(nvs_handle);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) wiping preferences", esp_err_to_name(err));
        return false;
    }

    initializeDefaultNvsParams();
    _hasAutoDetectedTimezone = false;
    _autoTimezoneRefreshPending = false;
    _autoDetectedTimezoneOffsetMinutes = _nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE];
    _autoTimezoneStatus.clear();

    ok &= clearSavedWifiCredentials();
    ok &= loadNvsParam();
    applyManualTimezonePreference();
    bsp_extra_audio_media_volume_set(_nvs_param_map[NVS_KEY_AUDIO_VOLUME]);
    bsp_extra_audio_system_volume_set(_nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME]);
    bsp_display_brightness_set(_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS]);
    applyDisplayIdleSettings();
    updateUiByNvsParam();
    setFirmwareStatus("Factory reset complete. Saved preferences were cleared.");

    return ok;
}


void AppSettings::refreshDisplayIdleUi(void)
{
#if !APP_SETTINGS_FEATURE_DISPLAY_MENU
    return;
#endif
    if (!isUiActive()) {
        return;
    }

    if (lv_obj_ready(_displayAdaptiveBrightnessSwitch)) {
        if (_nvs_param_map[NVS_KEY_DISPLAY_ADAPTIVE]) {
            lv_obj_add_state(_displayAdaptiveBrightnessSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_displayAdaptiveBrightnessSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_displayScreensaverSwitch)) {
        if (_nvs_param_map[NVS_KEY_DISPLAY_SCREENSAVER]) {
            lv_obj_add_state(_displayScreensaverSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_displayScreensaverSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_displayTimeoffDropdown)) {
        lv_dropdown_set_selected(_displayTimeoffDropdown,
                                 findDropdownIndexForValue(kDisplayTimeoffOptionsSec,
                                                           sizeof(kDisplayTimeoffOptionsSec) / sizeof(kDisplayTimeoffOptionsSec[0]),
                                                           _nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF]));
    }

    if (lv_obj_ready(_displaySleepDropdown)) {
        lv_dropdown_set_selected(_displaySleepDropdown,
                                 findDropdownIndexForValue(kDisplaySleepOptionsSec,
                                                           sizeof(kDisplaySleepOptionsSec) / sizeof(kDisplaySleepOptionsSec[0]),
                                                           _nvs_param_map[NVS_KEY_DISPLAY_SLEEP]));
    }

    #if CONFIG_JC4880_FEATURE_TIME_SYNC
    refreshTimezoneUi();
    #endif
}

void AppSettings::refreshTimezoneUi(void)
{
#if !CONFIG_JC4880_FEATURE_TIME_SYNC
    return;
#endif
    if (!isUiActive()) {
        return;
    }

    if (lv_obj_ready(_displayTimezoneDropdown)) {
        lv_dropdown_set_selected(_displayTimezoneDropdown,
                                 findTimezoneDropdownIndexForOffset(_nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE]));
    }

    if (lv_obj_ready(_displayAutoTimezoneSwitch)) {
        if (_nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO]) {
            lv_obj_add_state(_displayAutoTimezoneSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_displayAutoTimezoneSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_displayTimezoneInfoLabel)) {
        const bool auto_enabled = _nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO] != 0;
        const TimezoneOption &manual_option = getTimezoneOptionForOffset(_nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE]);

        std::string label = std::string("Manual: ") + manual_option.label;
        if (auto_enabled) {
            if (_hasAutoDetectedTimezone) {
                const TimezoneOption &detected_option = getTimezoneOptionForOffset(_autoDetectedTimezoneOffsetMinutes);
                label = std::string("Auto: ") + detected_option.label;
                if (!_autoTimezoneStatus.empty()) {
                    label += " • " + _autoTimezoneStatus;
                }
            } else if (!_autoTimezoneStatus.empty()) {
                label = _autoTimezoneStatus + " • fallback " + manual_option.label;
            } else {
                label = std::string("Auto timezone enabled • fallback ") + manual_option.label;
            }
        }

        lv_label_set_text(_displayTimezoneInfoLabel, label.c_str());
    }
}

void AppSettings::refreshBluetoothUi(void)
{
#if !APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    return;
#endif
    if (!isUiActive()) {
        return;
    }

    const bool bluetooth_enabled = _nvs_param_map[NVS_KEY_BLE_ENABLE] != 0;

    if (lv_obj_ready(ui_SwitchPanelScreenSettingBLESwitch)) {
        if (bluetooth_enabled) {
            lv_obj_add_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(ui_SpinnerScreenSettingBLE)) {
        if (s_bleRuntimeState == BleRuntimeState::Starting) {
            lv_obj_clear_flag(ui_SpinnerScreenSettingBLE, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui_SpinnerScreenSettingBLE, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (lv_obj_ready(_bluetoothInfoLabel)) {
        const std::string status = bleStatusText(bluetooth_enabled);
        lv_label_set_text(_bluetoothInfoLabel, status.c_str());
    }

    if (lv_obj_ready(_bluetoothNameTextArea)) {
        char ble_name[32] = {0};
        if (!loadNvsStringParam(NVS_KEY_BLE_DEVICE_NAME, ble_name, sizeof(ble_name)) || (ble_name[0] == '\0')) {
            strlcpy(ble_name, kBleDefaultDeviceName, sizeof(ble_name));
        }

        if (!lv_obj_has_state(_bluetoothNameTextArea, LV_STATE_FOCUSED)) {
            lv_textarea_set_text(_bluetoothNameTextArea, ble_name);
        }
    }

    if (lv_obj_ready(_bluetoothScanButtonLabel)) {
        lv_label_set_text(_bluetoothScanButtonLabel, s_bleScanInProgress ? "Stop Scan" : "Scan Nearby");
    }

    if (lv_obj_ready(_bluetoothScanStatusLabel)) {
        if (!bluetooth_enabled) {
            lv_label_set_text(_bluetoothScanStatusLabel, "Enable BLE first to scan nearby devices.");
        } else {
            lv_label_set_text(_bluetoothScanStatusLabel, s_bleScanStatus.c_str());
        }
    }

    if (lv_obj_ready(_bluetoothScanResultsLabel)) {
        std::string results;
        if (s_bleScanResults.empty()) {
            results = bluetooth_enabled ? "No discovery results yet." : "Discovery is unavailable while BLE is off.";
        } else {
            for (size_t index = 0; index < s_bleScanResults.size(); ++index) {
                const auto &entry = s_bleScanResults[index];
                results += std::to_string(index + 1) + ". ";
                results += entry.name.empty() ? std::string("Unnamed device") : entry.name;
                results += "\n";
                results += entry.address + "  RSSI " + std::to_string(entry.rssi) + " dBm";
                if ((index + 1) < s_bleScanResults.size()) {
                    results += "\n\n";
                }
            }
        }
        lv_label_set_text(_bluetoothScanResultsLabel, results.c_str());
    }
}

void AppSettings::refreshRadioStatusBar(void)
{
    if (status_bar == nullptr) {
        return;
    }

    auto *mutable_status_bar = const_cast<ESP_Brookesia_StatusBar *>(status_bar);
#if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    if (!_bluetoothStatusIconInstalled) {
        ESP_Brookesia_StatusBarIconData_t bluetooth_icon = {
            .size = {
                .width = 18,
                .height = 18,
            },
            .icon = {
                .image_num = 1,
                .images = {
                    ESP_BROOKESIA_STYLE_IMAGE_RECOLOR_WHITE(&ui_img_bluetooth_status_png),
                },
            },
        };

        _bluetoothStatusIconInstalled = mutable_status_bar->addIcon(
            bluetooth_icon, ESP_BROOKESIA_STATUS_BAR_DATA_AREA_NUM_MAX - 1, kStatusBarBluetoothIconId
        );
    }
#endif

#if CONFIG_JC4880_FEATURE_ZIGBEE
    if (!_zigbeeStatusIconInstalled) {
        ESP_Brookesia_StatusBarIconData_t zigbee_icon = {
            .size = {
                .width = 18,
                .height = 18,
            },
            .icon = {
                .image_num = 1,
                .images = {
                    ESP_BROOKESIA_STYLE_IMAGE_RECOLOR_WHITE(&ui_img_zigbee_status_png),
                },
            },
        };

        _zigbeeStatusIconInstalled = mutable_status_bar->addIcon(
            zigbee_icon, ESP_BROOKESIA_STATUS_BAR_DATA_AREA_NUM_MAX - 1, kStatusBarZigbeeIconId
        );
    }
#endif

#if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    const bool bluetooth_active = (_nvs_param_map[NVS_KEY_BLE_ENABLE] != 0) &&
                                  (s_bleRuntimeState == BleRuntimeState::Advertising || s_bleRuntimeState == BleRuntimeState::Starting);
#else
    const bool bluetooth_active = false;
#endif

#if CONFIG_JC4880_FEATURE_ZIGBEE
    const bool zigbee_active = _nvs_param_map[NVS_KEY_ZIGBEE_ENABLE] != 0;
#else
    const bool zigbee_active = false;
#endif

    if (_bluetoothStatusIconInstalled) {
        mutable_status_bar->setIconState(kStatusBarBluetoothIconId, bluetooth_active ? 0 : -1);
    }
    if (_zigbeeStatusIconInstalled) {
        mutable_status_bar->setIconState(kStatusBarZigbeeIconId, zigbee_active ? 0 : -1);
    }
}

void AppSettings::refreshZigbeeUi(void)
{
#if !CONFIG_JC4880_FEATURE_ZIGBEE
    return;
#endif
    if (!isUiActive()) {
        return;
    }

    const bool zigbee_enabled = _nvs_param_map[NVS_KEY_ZIGBEE_ENABLE] != 0;
    const int32_t preferred_channel = _nvs_param_map[NVS_KEY_ZIGBEE_CHANNEL];
    const int32_t permit_join_seconds = _nvs_param_map[NVS_KEY_ZIGBEE_PERMIT_JOIN];

    if (lv_obj_ready(_zigbeeEnableSwitch)) {
        if (zigbee_enabled) {
            lv_obj_add_state(_zigbeeEnableSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_zigbeeEnableSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_zigbeeChannelDropdown)) {
        lv_dropdown_set_selected(_zigbeeChannelDropdown,
                                 findDropdownIndexForValue(kZigbeeChannelOptions,
                                                           sizeof(kZigbeeChannelOptions) / sizeof(kZigbeeChannelOptions[0]),
                                                           preferred_channel));
    }

    if (lv_obj_ready(_zigbeePermitJoinDropdown)) {
        lv_dropdown_set_selected(_zigbeePermitJoinDropdown,
                                 findDropdownIndexForValue(kZigbeePermitJoinOptionsSec,
                                                           sizeof(kZigbeePermitJoinOptionsSec) / sizeof(kZigbeePermitJoinOptionsSec[0]),
                                                           permit_join_seconds));
    }

    if (lv_obj_ready(_zigbeeNameTextArea)) {
        char zigbee_name[32] = {0};
        if (!loadNvsStringParam(NVS_KEY_ZIGBEE_DEVICE_NAME, zigbee_name, sizeof(zigbee_name)) || (zigbee_name[0] == '\0')) {
            strlcpy(zigbee_name, kZigbeeDefaultDeviceName, sizeof(zigbee_name));
        }

        if (lv_obj_has_state(_zigbeeNameTextArea, LV_STATE_FOCUSED) == false) {
            lv_textarea_set_text(_zigbeeNameTextArea, zigbee_name);
        }
    }

    if (lv_obj_ready(_zigbeeRoleValueLabel)) {
        lv_label_set_text(_zigbeeRoleValueLabel,
                          "Coordinator on the ESP32-C6 coprocessor. The current firmware starts ZigBee natively on boot and keeps Wi-Fi/BLE coexistence enabled there.");
    }

    if (lv_obj_ready(_zigbeeConfigSummaryLabel)) {
        char zigbee_name[32] = {0};
        if (!loadNvsStringParam(NVS_KEY_ZIGBEE_DEVICE_NAME, zigbee_name, sizeof(zigbee_name)) || (zigbee_name[0] == '\0')) {
            strlcpy(zigbee_name, kZigbeeDefaultDeviceName, sizeof(zigbee_name));
        }

        std::string summary = std::string("Device name: ") + zigbee_name +
                              "\nPreferred channel: " + zigbeeChannelPreferenceLabel(preferred_channel) +
                              "\nPermit joining: " + zigbeePermitJoinLabel(permit_join_seconds);
        lv_label_set_text(_zigbeeConfigSummaryLabel, summary.c_str());
    }

    if (lv_obj_ready(_zigbeeInfoLabel)) {
        const std::string status = zigbee_enabled ?
            "ZigBee preferences are enabled on the P4. Current controls are host-side preferences only: the existing ESP32-C6 firmware does not expose live ZigBee RPC control, joined-device lists, PAN ID, or permit-join commands back to the P4 yet." :
            "ZigBee preference is disabled on the P4 UI. Note that the current ESP32-C6 release still starts ZigBee natively at boot when compiled with CONFIG_ZB_ENABLED, so this setting currently acts as a host-side preference gate for future integration.";
        lv_label_set_text(_zigbeeInfoLabel, status.c_str());
    }
}

void AppSettings::refreshSecurityUi(void)
{
#if !CONFIG_JC4880_FEATURE_SECURITY
    return;
#endif
    if (!isUiActive()) {
        return;
    }

    const bool device_lock_enabled = device_security::isLockEnabled(device_security::LockType::Device);
    const bool settings_lock_enabled = device_security::isLockEnabled(device_security::LockType::Settings);

    if (lv_obj_ready(_securityDeviceLockSwitch)) {
        if (device_lock_enabled) {
            lv_obj_add_state(_securityDeviceLockSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_securityDeviceLockSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_securitySettingsLockSwitch)) {
        if (settings_lock_enabled) {
            lv_obj_add_state(_securitySettingsLockSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_securitySettingsLockSwitch, LV_STATE_CHECKED);
        }
    }
}

void AppSettings::setFirmwareStatus(const std::string &status, bool is_error)
{
    if (!isUiActive() || !lv_obj_ready(_firmwareStatusLabel)) {
        return;
    }

    lv_label_set_text(_firmwareStatusLabel, status.c_str());
    lv_obj_set_style_text_color(_firmwareStatusLabel, is_error ? lv_color_hex(0xB91C1C) : lv_color_hex(0x334155), 0);
}

void AppSettings::ensureFirmwareOtaCheckOverlay(void)
{
    if ((_firmwareOtaCheckOverlay != nullptr) && lv_obj_ready(_firmwareOtaCheckOverlay)) {
        return;
    }

    _firmwareOtaCheckOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_firmwareOtaCheckOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(_firmwareOtaCheckOverlay, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(_firmwareOtaCheckOverlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_firmwareOtaCheckOverlay, 0, 0);
    lv_obj_set_style_pad_all(_firmwareOtaCheckOverlay, 0, 0);
    lv_obj_add_flag(_firmwareOtaCheckOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_firmwareOtaCheckOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(_firmwareOtaCheckOverlay);
    lv_obj_add_flag(_firmwareOtaCheckOverlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *firmwareOtaCheckCard = lv_obj_create(_firmwareOtaCheckOverlay);
    lv_obj_set_size(firmwareOtaCheckCard, 320, 210);
    lv_obj_center(firmwareOtaCheckCard);
    lv_obj_set_style_radius(firmwareOtaCheckCard, 24, 0);
    lv_obj_set_style_bg_color(firmwareOtaCheckCard, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_border_width(firmwareOtaCheckCard, 0, 0);
    lv_obj_set_style_pad_all(firmwareOtaCheckCard, 20, 0);
    lv_obj_set_style_pad_row(firmwareOtaCheckCard, 16, 0);
    lv_obj_clear_flag(firmwareOtaCheckCard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(firmwareOtaCheckCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(firmwareOtaCheckCard, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _firmwareOtaCheckSpinner = lv_spinner_create(firmwareOtaCheckCard, 1000, 90);
    lv_obj_set_size(_firmwareOtaCheckSpinner, 72, 72);

    _firmwareOtaCheckStatusLabel = lv_label_create(firmwareOtaCheckCard);
    lv_obj_set_width(_firmwareOtaCheckStatusLabel, lv_pct(100));
    lv_label_set_long_mode(_firmwareOtaCheckStatusLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_firmwareOtaCheckStatusLabel, "Checking GitHub for firmware releases...");
    lv_obj_set_style_text_align(_firmwareOtaCheckStatusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(_firmwareOtaCheckStatusLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(_firmwareOtaCheckStatusLabel, lv_color_hex(0x334155), 0);
}

void AppSettings::setFirmwareOtaCheckOverlayVisible(bool visible, const std::string &status)
{
    if (visible) {
        ensureFirmwareOtaCheckOverlay();
    }

    if ((_firmwareOtaCheckOverlay == nullptr) || !lv_obj_ready(_firmwareOtaCheckOverlay)) {
        return;
    }

    if ((_firmwareOtaCheckStatusLabel != nullptr) && lv_obj_ready(_firmwareOtaCheckStatusLabel) && !status.empty()) {
        lv_label_set_text(_firmwareOtaCheckStatusLabel, status.c_str());
    }

    if (visible) {
        lv_obj_move_foreground(_firmwareOtaCheckOverlay);
        lv_obj_clear_flag(_firmwareOtaCheckOverlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_firmwareOtaCheckOverlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void AppSettings::setFirmwareProgress(int32_t percent, const std::string &phase, bool is_error)
{
    if (!isUiActive()) {
        return;
    }

    if (lv_obj_ready(_firmwareProgressBar)) {
        const int32_t clamped = std::max<int32_t>(0, std::min<int32_t>(100, percent));
        lv_bar_set_value(_firmwareProgressBar, clamped, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_firmwareProgressBar,
                                  is_error ? lv_color_hex(0xFECACA) : lv_color_hex(0xCBD5E1),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_color(_firmwareProgressBar,
                                  is_error ? lv_color_hex(0xDC2626) : lv_color_hex(0x2563EB),
                                  LV_PART_INDICATOR);
    }

    if (lv_obj_ready(_firmwareProgressLabel)) {
        lv_label_set_text(_firmwareProgressLabel, phase.c_str());
        lv_obj_set_style_text_color(_firmwareProgressLabel,
                                    is_error ? lv_color_hex(0xB91C1C) : lv_color_hex(0x64748B),
                                    0);
    }
}

void AppSettings::queueFirmwareUiUpdate(const char *status, int32_t percent, bool busy, bool is_error)
{
    auto *context = new AsyncFirmwareUiContext{};
    if (context == nullptr) {
        ESP_LOGW(TAG, "Failed to allocate firmware UI context");
        return;
    }

    context->app = this;
    context->percent = percent;
    context->busy = busy;
    context->is_error = is_error;
    snprintf(context->status, sizeof(context->status), "%s", (status != nullptr) ? status : "");

    bsp_display_lock(0);
    if (lv_async_call(applyAsyncFirmwareUiUpdate, context) != LV_RES_OK) {
        bsp_display_unlock();
        delete context;
        ESP_LOGW(TAG, "Failed to queue firmware UI update");
        return;
    }
    bsp_display_unlock();
}

std::string AppSettings::getCurrentFirmwareVersion(void) const
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if ((app_desc == nullptr) || (app_desc->version[0] == '\0')) {
        return kFirmwareUnknownVersion;
    }

    return trim_copy(app_desc->version);
}

int AppSettings::compareVersionStrings(const std::string &lhs, const std::string &rhs)
{
    auto parse_values = [](const std::string &input) {
        std::vector<int> values;
        int current_value = -1;

        for (char ch : input) {
            if (std::isdigit(static_cast<unsigned char>(ch))) {
                if (current_value < 0) {
                    current_value = 0;
                }
                current_value = (current_value * 10) + (ch - '0');
            } else if (current_value >= 0) {
                values.push_back(current_value);
                current_value = -1;
            }
        }

        if (current_value >= 0) {
            values.push_back(current_value);
        }

        return values;
    };

    const std::vector<int> lhs_values = parse_values(lhs);
    const std::vector<int> rhs_values = parse_values(rhs);
    const size_t count = std::max(lhs_values.size(), rhs_values.size());

    for (size_t index = 0; index < count; ++index) {
        const int lhs_value = (index < lhs_values.size()) ? lhs_values[index] : 0;
        const int rhs_value = (index < rhs_values.size()) ? rhs_values[index] : 0;
        if (lhs_value != rhs_value) {
            return (lhs_value < rhs_value) ? -1 : 1;
        }
    }

    return 0;
}

std::string AppSettings::formatFirmwareLabel(const FirmwareEntry_t &entry) const
{
    std::string label = entry.label;
    if (!entry.version.empty() && (entry.version != kFirmwareUnknownVersion)) {
        label += " (" + entry.version + ")";
    }
    if (entry.is_current) {
        label += " [current]";
    } else if (entry.is_newer) {
        label += " [new]";
    }
    if (!entry.is_valid) {
        label += " [invalid]";
    }
    return label;
}

void AppSettings::populateFirmwareDropdown(lv_obj_t *dropdown, const std::vector<FirmwareEntry_t> &entries, const char *empty_label)
{
    if (dropdown == nullptr) {
        return;
    }

    if (entries.empty()) {
        lv_dropdown_set_options(dropdown, empty_label);
        lv_dropdown_set_selected(dropdown, 0);
        return;
    }

    std::string options;
    for (size_t index = 0; index < entries.size(); ++index) {
        if (index > 0) {
            options += '\n';
        }
        options += formatFirmwareLabel(entries[index]);
    }
    lv_dropdown_set_options(dropdown, options.c_str());
    lv_dropdown_set_selected(dropdown, 0);
}

int AppSettings::getSelectedOtaFirmwareIndex(void) const
{
    if ((_selectedOtaFirmwareIndex < 0) || (static_cast<size_t>(_selectedOtaFirmwareIndex) >= _otaFirmwareEntries.size())) {
        return -1;
    }

    return _selectedOtaFirmwareIndex;
}

void AppSettings::setSelectedOtaFirmwareIndex(int index)
{
    _selectedOtaFirmwareIndex = ((index >= 0) && (static_cast<size_t>(index) < _otaFirmwareEntries.size())) ? index : -1;

    for (size_t entry_index = 0; entry_index < _firmwareOtaCheckboxes.size(); ++entry_index) {
        lv_obj_t *checkbox = _firmwareOtaCheckboxes[entry_index];
        if ((checkbox == nullptr) || !lv_obj_ready(checkbox)) {
            continue;
        }

        if (static_cast<int>(entry_index) == _selectedOtaFirmwareIndex) {
            lv_obj_add_state(checkbox, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(checkbox, LV_STATE_CHECKED);
        }
    }
}

void AppSettings::rebuildFirmwareOtaList(void)
{
    if ((_firmwareOtaListContainer == nullptr) || !lv_obj_ready(_firmwareOtaListContainer)) {
        return;
    }

    lv_obj_clean(_firmwareOtaListContainer);
    _firmwareOtaCheckboxes.clear();

    const std::string current_version = getCurrentFirmwareVersion();
    std::string latest_version = "No GitHub firmware checked yet";
    for (const FirmwareEntry_t &entry : _otaFirmwareEntries) {
        if (entry.is_valid) {
            latest_version = entry.version.empty() ? entry.label : entry.version;
            break;
        }
    }

    if ((_selectedOtaFirmwareIndex >= 0) && (static_cast<size_t>(_selectedOtaFirmwareIndex) >= _otaFirmwareEntries.size())) {
        _selectedOtaFirmwareIndex = -1;
    }

    if (_firmwareOtaSummaryLabel != nullptr) {
        char summary[256] = {};
        snprintf(summary,
                 sizeof(summary),
                 "Installed: %s\nLatest available: %s\nAvailable releases: %u",
                 current_version.c_str(),
                 latest_version.c_str(),
                 static_cast<unsigned>(_otaFirmwareEntries.size()));
        lv_label_set_text(_firmwareOtaSummaryLabel, summary);
    }

    if (_otaFirmwareEntries.empty()) {
        lv_obj_t *emptyLabel = lv_label_create(_firmwareOtaListContainer);
        lv_obj_set_width(emptyLabel, lv_pct(100));
        lv_label_set_long_mode(emptyLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(emptyLabel, "Press Check to load OTA-ready firmware releases from GitHub.");
        lv_obj_set_style_text_color(emptyLabel, lv_color_hex(0x64748B), 0);
        return;
    }

    _firmwareOtaCheckboxes.reserve(_otaFirmwareEntries.size());
    for (size_t index = 0; index < _otaFirmwareEntries.size(); ++index) {
        const FirmwareEntry_t &entry = _otaFirmwareEntries[index];

        lv_obj_t *checkbox = lv_checkbox_create(_firmwareOtaListContainer);
        lv_obj_set_width(checkbox, lv_pct(100));
        lv_checkbox_set_text(checkbox, formatFirmwareLabel(entry).c_str());
        lv_obj_set_style_pad_ver(checkbox, 8, 0);
        lv_obj_set_style_text_font(checkbox, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(checkbox, lv_color_hex(0x0F172A), 0);
        lv_obj_add_event_cb(checkbox, onFirmwareOtaEntryCheckedEventCallback, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_set_user_data(checkbox, reinterpret_cast<void *>(index + 1));
        if (!entry.is_valid) {
            lv_obj_add_state(checkbox, LV_STATE_DISABLED);
        }
        _firmwareOtaCheckboxes.push_back(checkbox);
    }

    if ((_selectedOtaFirmwareIndex < 0) || (static_cast<size_t>(_selectedOtaFirmwareIndex) >= _otaFirmwareEntries.size()) ||
        !_otaFirmwareEntries[_selectedOtaFirmwareIndex].is_valid) {
        int preferred_index = -1;
        for (size_t index = 0; index < _otaFirmwareEntries.size(); ++index) {
            if (_otaFirmwareEntries[index].is_valid && _otaFirmwareEntries[index].is_newer) {
                preferred_index = static_cast<int>(index);
                break;
            }
        }
        if ((preferred_index < 0) && !_otaFirmwareEntries.empty() && _otaFirmwareEntries.front().is_valid) {
            preferred_index = 0;
        }
        _selectedOtaFirmwareIndex = preferred_index;
    }

    setSelectedOtaFirmwareIndex(_selectedOtaFirmwareIndex);
}

void AppSettings::releaseFirmwareOtaResources(void)
{
    _firmwareOtaCheckInProgress = false;
    _selectedOtaFirmwareIndex = -1;

    if ((_firmwareOtaListContainer != nullptr) && lv_obj_ready(_firmwareOtaListContainer)) {
        lv_obj_clean(_firmwareOtaListContainer);
    }

    _firmwareOtaCheckboxes.clear();
    std::vector<lv_obj_t *>().swap(_firmwareOtaCheckboxes);
    _otaFirmwareEntries.clear();
    std::vector<FirmwareEntry_t>().swap(_otaFirmwareEntries);

    if ((_firmwareOtaSummaryLabel != nullptr) && lv_obj_ready(_firmwareOtaSummaryLabel)) {
        lv_label_set_text(_firmwareOtaSummaryLabel, "");
    }

    if ((_firmwareOtaCheckOverlay != nullptr) && lv_obj_ready(_firmwareOtaCheckOverlay)) {
        lv_obj_del(_firmwareOtaCheckOverlay);
    }
    _firmwareOtaCheckOverlay = nullptr;
    _firmwareOtaCheckSpinner = nullptr;
    _firmwareOtaCheckStatusLabel = nullptr;
}

bool AppSettings::hasOtaFlashSupport(void) const
{
    return esp_ota_get_next_update_partition(nullptr) != nullptr;
}

bool AppSettings::probeFirmwareFile(const std::string &path, FirmwareEntry_t &entry)
{
    entry = {};
    entry.path_or_url = path;
    entry.label = basename_from_path(path);
    entry.version = kFirmwareUnknownVersion;

    FILE *file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        entry.notes = "Unable to open file";
        return false;
    }

    struct stat file_info = {};
    if (stat(path.c_str(), &file_info) == 0) {
        entry.size_bytes = static_cast<size_t>(file_info.st_size);
    }

    esp_image_header_t image_header = {};
    esp_image_segment_header_t segment_header = {};
    esp_app_desc_t app_desc = {};
    bool ok = false;

    if ((fread(&image_header, 1, sizeof(image_header), file) == sizeof(image_header)) &&
        (image_header.magic == ESP_IMAGE_HEADER_MAGIC) &&
        (fread(&segment_header, 1, sizeof(segment_header), file) == sizeof(segment_header)) &&
        (fread(&app_desc, 1, sizeof(app_desc), file) == sizeof(app_desc))) {
        entry.project_name = trim_copy(app_desc.project_name);
        entry.version = trim_copy(app_desc.version);
        if (entry.version.empty()) {
            entry.version = kFirmwareUnknownVersion;
        }

        const esp_app_desc_t *current_app = esp_app_get_description();
        const std::string current_project = (current_app != nullptr) ? trim_copy(current_app->project_name) : std::string();
        if (!current_project.empty() && !entry.project_name.empty() && (entry.project_name != current_project)) {
            entry.notes = "Project mismatch: " + entry.project_name;
        } else {
            const std::string current_version = getCurrentFirmwareVersion();
            entry.is_valid = true;
            entry.is_current = compareVersionStrings(entry.version, current_version) == 0;
            entry.is_newer = compareVersionStrings(entry.version, current_version) > 0;

            std::ostringstream stream;
            stream << "Project: " << (entry.project_name.empty() ? "unknown" : entry.project_name)
                   << ", size: " << (entry.size_bytes / 1024U) << " KB";
            entry.notes = stream.str();
            ok = true;
        }
    } else {
        entry.notes = "Not a valid ESP firmware image";
    }

    fclose(file);
    return ok;
}

bool AppSettings::scanSdFirmwareEntries(void)
{
    _sdFirmwareEntries.clear();
    const char *directories[] = {kFirmwareSdDirectory, "/sdcard"};

    for (const char *directory_path : directories) {
        DIR *dir = opendir(directory_path);
        if (dir == nullptr) {
            continue;
        }

        struct dirent *item = nullptr;
        while ((item = readdir(dir)) != nullptr) {
            if ((strcmp(item->d_name, ".") == 0) || (strcmp(item->d_name, "..") == 0)) {
                continue;
            }

            const std::string candidate_path = std::string(directory_path) + "/" + item->d_name;
            if (!ends_with_bin(candidate_path)) {
                continue;
            }

            FirmwareEntry_t entry = {};
            probeFirmwareFile(candidate_path, entry);
            _sdFirmwareEntries.push_back(entry);
        }

        closedir(dir);
    }

    std::sort(_sdFirmwareEntries.begin(), _sdFirmwareEntries.end(), [](const FirmwareEntry_t &lhs, const FirmwareEntry_t &rhs) {
        if (lhs.is_valid != rhs.is_valid) {
            return lhs.is_valid > rhs.is_valid;
        }
        if (lhs.is_newer != rhs.is_newer) {
            return lhs.is_newer > rhs.is_newer;
        }
        return lhs.label < rhs.label;
    });

    return !_sdFirmwareEntries.empty();
}

bool AppSettings::fetchGithubFirmwareEntries(void)
{
    _otaFirmwareEntries.clear();
    std::string response;

    esp_http_client_config_t config = {};
    config.url = kFirmwareGithubReleasesUrl;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = [](esp_http_client_event_t *event) {
        if ((event == nullptr) || (event->user_data == nullptr) || (event->event_id != HTTP_EVENT_ON_DATA) ||
            (event->data == nullptr) || (event->data_len <= 0)) {
            return ESP_OK;
        }

        auto *body = static_cast<std::string *>(event->user_data);
        body->append(static_cast<const char *>(event->data), static_cast<size_t>(event->data_len));
        return ESP_OK;
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }

    bool ok = false;
    if ((esp_http_client_set_header(client, "Accept", "application/vnd.github+json") == ESP_OK) &&
        (esp_http_client_set_header(client, "User-Agent", "JC4880P443C-IW-Remote") == ESP_OK) &&
        (esp_http_client_perform(client) == ESP_OK) &&
        (esp_http_client_get_status_code(client) == 200)) {
        cJSON *root = cJSON_Parse(response.c_str());
        if (cJSON_IsArray(root)) {
            const std::string current_version = getCurrentFirmwareVersion();
            cJSON *release = nullptr;
            cJSON_ArrayForEach(release, root) {
                if (!cJSON_IsObject(release) || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(release, "draft"))) {
                    continue;
                }

                const std::string release_name = trim_copy(safe_json_string(release, "name"));
                const std::string tag_name = trim_copy(safe_json_string(release, "tag_name"));
                const std::string release_body = trim_copy(safe_json_string(release, "body"));
                cJSON *assets = cJSON_GetObjectItemCaseSensitive(release, "assets");
                if (!cJSON_IsArray(assets)) {
                    continue;
                }

                cJSON *asset = nullptr;
                cJSON_ArrayForEach(asset, assets) {
                    if (!cJSON_IsObject(asset)) {
                        continue;
                    }

                    const std::string asset_name = trim_copy(safe_json_string(asset, "name"));
                    const std::string download_url = trim_copy(safe_json_string(asset, "browser_download_url"));
                    if (asset_name.empty() || download_url.empty() || !ends_with_bin(asset_name)) {
                        continue;
                    }

                    FirmwareEntry_t entry = {};
                    entry.label = asset_name;
                    entry.version = tag_name.empty() ? release_name : tag_name;
                    if (entry.version.empty()) {
                        entry.version = kFirmwareUnknownVersion;
                    }
                    entry.path_or_url = download_url;
                    entry.project_name = "JC4880P443C_I_W_Remote";
                    entry.size_bytes = static_cast<size_t>(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(asset, "size")) ?
                                                              cJSON_GetObjectItemCaseSensitive(asset, "size")->valuedouble : 0);
                    entry.notes = release_name.empty() ? std::string("GitHub release") : release_name;
                    entry.release_notes = release_body;
                    entry.is_valid = true;
                    entry.is_current = compareVersionStrings(entry.version, current_version) == 0;
                    entry.is_newer = compareVersionStrings(entry.version, current_version) > 0;
                    _otaFirmwareEntries.push_back(entry);
                }
            }
        }
        cJSON_Delete(root);
        ok = !_otaFirmwareEntries.empty();
    }

    esp_http_client_cleanup(client);

    std::sort(_otaFirmwareEntries.begin(), _otaFirmwareEntries.end(), [](const FirmwareEntry_t &lhs, const FirmwareEntry_t &rhs) {
        if (lhs.is_current != rhs.is_current) {
            return lhs.is_current > rhs.is_current;
        }
        if (lhs.is_newer != rhs.is_newer) {
            return lhs.is_newer > rhs.is_newer;
        }
        return AppSettings::compareVersionStrings(lhs.version, rhs.version) > 0;
    });

    return ok;
}

void AppSettings::refreshFirmwareUi(void)
{
#if !CONFIG_JC4880_FEATURE_OTA
    return;
#endif
    populateFirmwareDropdown(_firmwareSdDropdown, _sdFirmwareEntries, "No SD firmware found");
    rebuildFirmwareOtaList();

    if (_firmwareCurrentVersionLabel != nullptr) {
        const std::string current_version = getCurrentFirmwareVersion();
        const esp_app_desc_t *app_desc = esp_app_get_description();
        const std::string project_name = ((app_desc != nullptr) && (app_desc->project_name[0] != '\0'))
                                             ? trim_copy(app_desc->project_name)
                                             : std::string("unknown project");
        std::string installed_text = "Current firmware: " + current_version;
        if (!project_name.empty()) {
            installed_text += "\nProject: " + project_name;
        }
        lv_label_set_text(_firmwareCurrentVersionLabel, installed_text.c_str());
    }

    const bool ota_supported = hasOtaFlashSupport();
    const uint16_t sd_index = (_firmwareSdDropdown != nullptr) ? lv_dropdown_get_selected(_firmwareSdDropdown) : 0;
    const int ota_index = getSelectedOtaFirmwareIndex();
    const bool controls_busy = _firmwareUpdateInProgress || _firmwareOtaCheckInProgress;
    const bool sd_ready = ota_supported && !controls_busy && (sd_index < _sdFirmwareEntries.size()) && _sdFirmwareEntries[sd_index].is_valid;
    const bool ota_ready = ota_supported && !controls_busy && (ota_index >= 0) &&
                           (static_cast<size_t>(ota_index) < _otaFirmwareEntries.size()) && _otaFirmwareEntries[ota_index].is_valid;

    auto update_button = [](lv_obj_t *button, bool enabled) {
        if (button == nullptr) {
            return;
        }

        if (enabled) {
            lv_obj_clear_state(button, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(button, lv_color_hex(0x2563EB), 0);
        } else {
            lv_obj_add_state(button, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(button, lv_color_hex(0x94A3B8), 0);
        }
    };

    update_button(_firmwareSdFlashButton, sd_ready);
    update_button(_firmwareOtaCheckButton, !_firmwareUpdateInProgress && !_firmwareOtaCheckInProgress);
    update_button(_firmwareOtaFlashButton, ota_ready);

    if (_firmwareUpdateInProgress) {
        return;
    }

    if (_firmwareOtaCheckInProgress) {
        setFirmwareStatus("Checking GitHub for firmware releases...");
        setFirmwareProgress(0, "Fetching release list from server...");
        return;
    }

    if (!ota_supported) {
        setFirmwareStatus("Flash buttons are disabled because this build has only a factory app partition. Safe in-app updates require OTA partitions.");
    } else if ((ota_index >= 0) && (static_cast<size_t>(ota_index) < _otaFirmwareEntries.size())) {
        const FirmwareEntry_t &entry = _otaFirmwareEntries[ota_index];
        setFirmwareStatus(entry.release_notes.empty() ? (entry.notes.empty() ? std::string("No release notes available.") : entry.notes)
                                                     : entry.release_notes);
        setFirmwareProgress(0, "Ready to download and flash the selected GitHub firmware.");
    } else if ((sd_index < _sdFirmwareEntries.size()) && !_sdFirmwareEntries.empty()) {
        setFirmwareStatus(_sdFirmwareEntries[sd_index].notes);
        setFirmwareProgress(0, "Ready to flash selected SD firmware image.");
    } else {
        setFirmwareStatus("Firmware screen ready. Scan SD or check GitHub releases for OTA .bin assets.");
        setFirmwareProgress(0, "Idle");
    }
}

void AppSettings::applyAsyncFirmwareUiUpdate(void *arg)
{
    auto *context = static_cast<AsyncFirmwareUiContext *>(arg);
    if ((context == nullptr) || (context->app == nullptr)) {
        delete context;
        return;
    }

    context->app->_firmwareUpdateInProgress = context->busy;
    if (context->is_error) {
        context->app->refreshFirmwareUi();
        context->app->setFirmwareStatus(context->status, true);
        context->app->setFirmwareProgress(context->percent, context->status, true);
        delete context;
        return;
    }

    if (!context->busy) {
        context->app->setFirmwareStatus(context->status, false);
    }
    context->app->setFirmwareProgress(context->percent, context->status, context->is_error);
    context->app->refreshFirmwareUi();
    delete context;
}

bool AppSettings::validateFirmwareImageHeader(const uint8_t *data, size_t data_len, const std::string &source_label,
                                             std::string &error_message, bool &header_checked)
{
    if (header_checked) {
        return true;
    }

    const size_t required_bytes = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
    if (data_len < required_bytes) {
        return true;
    }

    auto *image_header = reinterpret_cast<const esp_image_header_t *>(data);
    if (image_header->magic != ESP_IMAGE_HEADER_MAGIC) {
        error_message = source_label + " is not a valid ESP firmware image.";
        return false;
    }

    auto *app_desc = reinterpret_cast<const esp_app_desc_t *>(data + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    const esp_app_desc_t *current_app = esp_app_get_description();
    const std::string current_project = (current_app != nullptr) ? trim_copy(current_app->project_name) : std::string();
    const std::string incoming_project = trim_copy(app_desc->project_name);
    if (!current_project.empty() && !incoming_project.empty() && (incoming_project != current_project)) {
        error_message = "Firmware project mismatch: " + incoming_project;
        return false;
    }

    header_checked = true;
    return true;
}

void AppSettings::persistPendingReleaseNotes(const FirmwareEntry_t &entry)
{
    setNvsStringParam(NVS_KEY_OTA_PENDING_VERSION, entry.version.empty() ? kFirmwareUnknownVersion : entry.version.c_str());
    setNvsStringParam(NVS_KEY_OTA_PENDING_NOTES,
                      entry.release_notes.empty() ? entry.notes.c_str() : entry.release_notes.c_str());
    setNvsParam(NVS_KEY_OTA_PENDING_SHOW, 1);
}

bool AppSettings::flashFirmwareFromFile(const FirmwareEntry_t &entry, std::string &error_message)
{
    ESP_LOGI(TAG, "Starting SD firmware flash: label='%s' path='%s'", entry.label.c_str(), entry.path_or_url.c_str());

    FILE *file = fopen(entry.path_or_url.c_str(), "rb");
    if (file == nullptr) {
        error_message = "Unable to open selected firmware file.";
        ESP_LOGE(TAG, "%s", error_message.c_str());
        return false;
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    if (partition == nullptr) {
        fclose(file);
        error_message = "No OTA partition is available.";
        ESP_LOGE(TAG, "%s", error_message.c_str());
        return false;
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        fclose(file);
        error_message = std::string("esp_ota_begin failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        return false;
    }

    uint8_t *buffer = static_cast<uint8_t *>(allocate_psram_preferred_buffer(4096));
    uint8_t *header_buffer = static_cast<uint8_t *>(allocate_psram_preferred_buffer(512));
    size_t header_size = 0;
    bool header_checked = false;
    size_t written_total = 0;
    int last_percent = -1;
    bool success = false;

    if ((buffer == nullptr) || (header_buffer == nullptr)) {
        error_message = "Unable to allocate firmware flashing buffers.";
        ESP_LOGE(TAG, "%s", error_message.c_str());
        goto cleanup_alloc;
    }

    while (true) {
        const size_t read_bytes = fread(buffer, 1, 4096, file);
        if (read_bytes == 0) {
            if (feof(file)) {
                break;
            }
            error_message = "Reading firmware file failed.";
            ESP_LOGE(TAG, "%s", error_message.c_str());
            goto cleanup;
        }

        if (!header_checked && (header_size < 512)) {
            const size_t copy_bytes = std::min<size_t>(512 - header_size, read_bytes);
            memcpy(header_buffer + header_size, buffer, copy_bytes);
            header_size += copy_bytes;
            if (!validateFirmwareImageHeader(header_buffer, header_size, entry.label, error_message, header_checked)) {
                ESP_LOGE(TAG, "%s", error_message.c_str());
                goto cleanup;
            }
        }

        err = esp_ota_write(ota_handle, buffer, read_bytes);
        if (err != ESP_OK) {
            error_message = std::string("esp_ota_write failed: ") + esp_err_to_name(err);
            ESP_LOGE(TAG, "%s", error_message.c_str());
            goto cleanup;
        }

        written_total += read_bytes;
        if (entry.size_bytes > 0) {
            const int percent = static_cast<int>((written_total * 100U) / entry.size_bytes);
            if (percent != last_percent) {
                last_percent = percent;
                char phase[128] = {};
                snprintf(phase, sizeof(phase), "Flashing from SD... %d%%", percent);
                queueFirmwareUiUpdate(phase, percent, true, false);
            }
        }
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        error_message = std::string("esp_ota_end failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        goto cleanup_no_abort;
    }

    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        error_message = std::string("esp_ota_set_boot_partition failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        goto cleanup_no_abort;
    }

    success = true;
    ESP_LOGI(TAG, "SD firmware flash staged successfully: version='%s'", entry.version.c_str());

cleanup_no_abort:
    heap_caps_free(header_buffer);
    heap_caps_free(buffer);
    fclose(file);
    if (!success) {
        return false;
    }
    return true;

cleanup_alloc:
    heap_caps_free(header_buffer);
    heap_caps_free(buffer);
    fclose(file);
    return false;

cleanup:
    esp_ota_abort(ota_handle);
    heap_caps_free(header_buffer);
    heap_caps_free(buffer);
    fclose(file);
    return false;
}

bool AppSettings::flashFirmwareFromUrl(const FirmwareEntry_t &entry, std::string &error_message)
{
    ESP_LOGI(TAG, "Starting OTA firmware flash: label='%s' version='%s' url='%s'",
             entry.label.c_str(), entry.version.c_str(), entry.path_or_url.c_str());

    constexpr int kMaxHttpRedirects = 5;
    constexpr int kHttpClientBufferSize = 4096;
    struct RedirectCapture {
        std::string location;
    } redirect_capture;

    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    if (partition == nullptr) {
        error_message = "No OTA partition is available.";
        ESP_LOGE(TAG, "%s", error_message.c_str());
        return false;
    }

    esp_http_client_config_t config = {};
    config.url = entry.path_or_url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.buffer_size = kHttpClientBufferSize;
    config.buffer_size_tx = kHttpClientBufferSize;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.disable_auto_redirect = true;
    config.max_redirection_count = kMaxHttpRedirects;
    config.user_data = &redirect_capture;
    config.event_handler = [](esp_http_client_event_t *event) {
        if ((event == nullptr) || (event->user_data == nullptr)) {
            return ESP_OK;
        }

        auto *capture = static_cast<RedirectCapture *>(event->user_data);
        if ((event->event_id == HTTP_EVENT_ON_HEADER) && (event->header_key != nullptr) && (event->header_value != nullptr) &&
            (strcasecmp(event->header_key, "Location") == 0)) {
            capture->location = event->header_value;
        }
        return ESP_OK;
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        error_message = "Failed to create HTTP client.";
        ESP_LOGE(TAG, "%s", error_message.c_str());
        return false;
    }

    esp_http_client_set_header(client, "Accept", "application/octet-stream");
    esp_http_client_set_header(client, "User-Agent", "JC4880P443C-IW-Remote");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        error_message = std::string("Failed to open release asset: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        esp_http_client_cleanup(client);
        return false;
    }

    int header_status = esp_http_client_fetch_headers(client);
    int http_status = esp_http_client_get_status_code(client);
    int redirect_count = 0;
    while ((http_status >= 300) && (http_status < 400) && (redirect_count < kMaxHttpRedirects)) {
        if (redirect_capture.location.empty()) {
            error_message = "GitHub redirect response did not include a valid Location header.";
            ESP_LOGE(TAG, "%s HTTP status=%d", error_message.c_str(), http_status);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }

        ESP_LOGI(TAG,
                 "Following OTA redirect %d/%d: HTTP status=%d location='%s'",
                 redirect_count + 1,
                 kMaxHttpRedirects,
                 http_status,
                 redirect_capture.location.c_str());

        esp_http_client_close(client);
        err = esp_http_client_set_url(client, redirect_capture.location.c_str());
        if (err != ESP_OK) {
            error_message = std::string("Failed to set redirected asset URL: ") + esp_err_to_name(err);
            ESP_LOGE(TAG, "%s", error_message.c_str());
            esp_http_client_cleanup(client);
            return false;
        }

        redirect_capture.location.clear();
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            error_message = std::string("Failed to open redirected asset URL: ") + esp_err_to_name(err);
            ESP_LOGE(TAG, "%s", error_message.c_str());
            esp_http_client_cleanup(client);
            return false;
        }

        header_status = esp_http_client_fetch_headers(client);
        http_status = esp_http_client_get_status_code(client);
        ++redirect_count;
    }

    if ((header_status < 0) || ((http_status / 100) != 2)) {
        if ((http_status >= 300) && (http_status < 400)) {
            error_message = "GitHub asset download redirect limit reached.";
        } else {
            error_message = "GitHub asset download request failed.";
        }
        ESP_LOGE(TAG,
                 "%s HTTP status=%d header_status=%d redirects=%d",
                 error_message.c_str(),
                 http_status,
                 header_status,
                 redirect_count);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    const int64_t content_length = esp_http_client_get_content_length(client);
    esp_ota_handle_t ota_handle = 0;
    err = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        error_message = std::string("esp_ota_begin failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    uint8_t *buffer = static_cast<uint8_t *>(allocate_psram_preferred_buffer(4096));
    uint8_t *header_buffer = static_cast<uint8_t *>(allocate_psram_preferred_buffer(512));
    size_t header_size = 0;
    bool header_checked = false;
    int last_percent = -1;
    size_t written_total = 0;
    bool success = false;

    if ((buffer == nullptr) || (header_buffer == nullptr)) {
        error_message = "Unable to allocate OTA download buffers.";
        ESP_LOGE(TAG, "%s", error_message.c_str());
        goto ota_cleanup_alloc;
    }

    while (true) {
        const int read_bytes = esp_http_client_read(client, reinterpret_cast<char *>(buffer), 4096);
        if (read_bytes < 0) {
            error_message = "Release asset download failed.";
            ESP_LOGE(TAG, "%s", error_message.c_str());
            goto ota_cleanup;
        }
        if (read_bytes == 0) {
            break;
        }

        if (!header_checked && (header_size < 512)) {
            const size_t copy_bytes = std::min<size_t>(512 - header_size, static_cast<size_t>(read_bytes));
            memcpy(header_buffer + header_size, buffer, copy_bytes);
            header_size += copy_bytes;
            if (!validateFirmwareImageHeader(header_buffer, header_size, entry.label, error_message, header_checked)) {
                ESP_LOGE(TAG, "%s", error_message.c_str());
                goto ota_cleanup;
            }
        }

        err = esp_ota_write(ota_handle, buffer, static_cast<size_t>(read_bytes));
        if (err != ESP_OK) {
            error_message = std::string("esp_ota_write failed: ") + esp_err_to_name(err);
            ESP_LOGE(TAG, "%s", error_message.c_str());
            goto ota_cleanup;
        }

        written_total += static_cast<size_t>(read_bytes);
        if (content_length > 0) {
            const int percent = static_cast<int>((written_total * 100ULL) / static_cast<uint64_t>(content_length));
            if (percent != last_percent) {
                last_percent = percent;
                char phase[128] = {};
                snprintf(phase, sizeof(phase), "Downloading and flashing... %d%%", percent);
                queueFirmwareUiUpdate(phase, percent, true, false);
            }
        }
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        error_message = std::string("esp_ota_end failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        goto ota_cleanup_no_abort;
    }

    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        error_message = std::string("esp_ota_set_boot_partition failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        goto ota_cleanup_no_abort;
    }

    success = true;
    ESP_LOGI(TAG, "OTA firmware flash staged successfully: version='%s'", entry.version.c_str());

ota_cleanup_no_abort:
    heap_caps_free(header_buffer);
    heap_caps_free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (!success) {
        return false;
    }
    return true;

ota_cleanup_alloc:
    heap_caps_free(header_buffer);
    heap_caps_free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;

ota_cleanup:
    esp_ota_abort(ota_handle);
    heap_caps_free(header_buffer);
    heap_caps_free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
}

bool AppSettings::flashFirmwareEntry(const FirmwareEntry_t &entry, FirmwareUpdateSource_t source)
{
    auto *context = new FirmwareUpdateTaskContext{this, entry, source};
    if (context == nullptr) {
        setFirmwareStatus("Failed to allocate firmware update task.", true);
        ESP_LOGE(TAG, "Failed to allocate firmware update task context");
        return false;
    }

    _firmwareUpdateInProgress = true;
    refreshFirmwareUi();
    setFirmwareProgress(0, source == FIRMWARE_UPDATE_SOURCE_OTA ? "Preparing OTA update..." : "Preparing SD flash...");
    setFirmwareStatus(source == FIRMWARE_UPDATE_SOURCE_OTA ? "Starting OTA update..." : "Starting SD flash...");
    ESP_LOGI(TAG,
             "Queueing firmware update task: source=%s label='%s' version='%s' target='%s'",
             source == FIRMWARE_UPDATE_SOURCE_OTA ? "ota" : "sd",
             entry.label.c_str(),
             entry.version.c_str(),
             entry.path_or_url.c_str());

    // OTA finalization disables flash cache, so this worker must keep its stack in internal RAM.
    if (xTaskCreatePinnedToCore(firmwareUpdateTask,
                                "firmware_update",
                                FIRMWARE_UPDATE_TASK_STACK_SIZE,
                                context,
                                FIRMWARE_UPDATE_TASK_PRIORITY,
                                nullptr,
                                1) != pdPASS) {
        delete context;
        _firmwareUpdateInProgress = false;
        refreshFirmwareUi();
        setFirmwareStatus("Failed to start firmware update task.", true);
        ESP_LOGE(TAG, "Failed to start firmware update background task");
        return false;
    }

    return true;
}

void AppSettings::firmwareUpdateTask(void *arg)
{
    auto *context = static_cast<FirmwareUpdateTaskContext *>(arg);
    if ((context == nullptr) || (context->app == nullptr)) {
        delete context;
        vTaskDelete(nullptr);
        return;
    }

    AppSettings *app = context->app;
    const FirmwareEntry_t entry = context->entry;
    const FirmwareUpdateSource_t source = context->source;
    delete context;

    std::string error_message;
    const bool ok = (source == FIRMWARE_UPDATE_SOURCE_OTA)
                        ? app->flashFirmwareFromUrl(entry, error_message)
                        : app->flashFirmwareFromFile(entry, error_message);

    if (!ok) {
        ESP_LOGE(TAG,
                 "Firmware update failed: source=%s label='%s' version='%s' reason='%s'",
                 source == FIRMWARE_UPDATE_SOURCE_OTA ? "ota" : "sd",
                 entry.label.c_str(),
                 entry.version.c_str(),
                 error_message.empty() ? "Firmware update failed." : error_message.c_str());
        app->queueFirmwareUiUpdate(error_message.empty() ? "Firmware update failed." : error_message.c_str(), 0, false, true);
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG,
             "Firmware update complete, rebooting: source=%s label='%s' version='%s'",
             source == FIRMWARE_UPDATE_SOURCE_OTA ? "ota" : "sd",
             entry.label.c_str(),
             entry.version.c_str());
    app->persistPendingReleaseNotes(entry);
    app->queueFirmwareUiUpdate("Firmware update complete. Rebooting...", 100, false, false);
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

void AppSettings::applyDisplayIdleSettings(void)
{
    bsp_extra_display_idle_set_base_brightness(_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS]);
    bsp_extra_display_idle_configure(_nvs_param_map[NVS_KEY_DISPLAY_ADAPTIVE],
                                     _nvs_param_map[NVS_KEY_DISPLAY_SCREENSAVER],
                                     _nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF],
                                     _nvs_param_map[NVS_KEY_DISPLAY_SLEEP]);
}

void AppSettings::applyManualTimezonePreference(void)
{
    const TimezoneOption &option = getTimezoneOptionForOffset(_nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE]);
    app_sntp_set_timezone(option.tz);
}

bool AppSettings::syncAutoTimezoneFromInternet(void)
{
    std::string response;

    esp_http_client_config_t config = {};
    config.url = kTimezoneLookupUrl;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 12000;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = [](esp_http_client_event_t *event) {
        if ((event == nullptr) || (event->user_data == nullptr) || (event->event_id != HTTP_EVENT_ON_DATA) ||
            (event->data == nullptr) || (event->data_len <= 0)) {
            return ESP_OK;
        }

        auto *body = static_cast<std::string *>(event->user_data);
        body->append(static_cast<const char *>(event->data), static_cast<size_t>(event->data_len));
        return ESP_OK;
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        _autoTimezoneStatus = "Auto timezone lookup unavailable";
        refreshTimezoneUi();
        return false;
    }

    bool ok = false;
    if ((esp_http_client_set_header(client, "Accept", "application/json") == ESP_OK) &&
        (esp_http_client_set_header(client, "User-Agent", "JC4880P443C-IW-Remote") == ESP_OK) &&
        (esp_http_client_perform(client) == ESP_OK) &&
        (esp_http_client_get_status_code(client) == 200)) {
        cJSON *root = cJSON_Parse(response.c_str());
        if (cJSON_IsObject(root)) {
            const std::string offset_text = trim_copy(safe_json_string(root, "utc_offset"));
            const std::string city = trim_copy(safe_json_string(root, "city"));
            const std::string region = trim_copy(safe_json_string(root, "region"));
            const std::string country = trim_copy(safe_json_string(root, "country_name"));
            int32_t detected_minutes = 0;

            if (parseUtcOffsetMinutes(offset_text, detected_minutes)) {
                _autoDetectedTimezoneOffsetMinutes = detected_minutes;
                _hasAutoDetectedTimezone = true;

                const TimezoneOption &option = getTimezoneOptionForOffset(detected_minutes);
                app_sntp_set_timezone(option.tz);

                std::string location = city;
                if (!region.empty()) {
                    location = location.empty() ? region : (location + ", " + region);
                }
                if (!country.empty()) {
                    location = location.empty() ? country : (location + ", " + country);
                }

                _autoTimezoneStatus = location.empty() ? std::string("Detected from internet") : location;
                ok = true;
            }
        }
        cJSON_Delete(root);
    }

    esp_http_client_cleanup(client);

    if (!ok) {
        _hasAutoDetectedTimezone = false;
        _autoTimezoneStatus = "Auto timezone lookup failed";
        applyManualTimezonePreference();
    }

    refreshTimezoneUi();
    return ok;
}

void AppSettings::updateUiByNvsParam(void)
{
    if (!isUiActive()) {
        return;
    }

#if APP_SETTINGS_FEATURE_WIFI
    loadNvsStringParam(NVS_KEY_WIFI_SSID, st_wifi_ssid, sizeof(st_wifi_ssid));
    loadNvsStringParam(NVS_KEY_WIFI_PASSWORD, st_wifi_password, sizeof(st_wifi_password));
#endif

#if APP_SETTINGS_FEATURE_WIFI
    if (_nvs_param_map[NVS_KEY_WIFI_ENABLE]) {
        lv_obj_add_state(ui_SwitchPanelScreenSettingWiFiSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(ui_SwitchPanelScreenSettingWiFiSwitch, LV_STATE_CHECKED);
    }
#endif

#if APP_SETTINGS_FEATURE_DISPLAY_MENU
    lv_slider_set_value(ui_SliderPanelScreenSettingLightSwitch1, _nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS], LV_ANIM_OFF);
#endif

#if CONFIG_JC4880_FEATURE_AUDIO
    lv_slider_set_value(ui_SliderPanelScreenSettingVolumeSwitch, _nvs_param_map[NVS_KEY_AUDIO_VOLUME], LV_ANIM_OFF);
    if (_audioSystemVolumeSlider != nullptr) {
        lv_slider_set_value(_audioSystemVolumeSlider, _nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME], LV_ANIM_OFF);
    }
#endif

#if APP_SETTINGS_FEATURE_WIFI
    refreshSavedWifiUi();
#endif

#if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    refreshBluetoothUi();
#endif
    refreshRadioStatusBar();

#if CONFIG_JC4880_FEATURE_ZIGBEE
    refreshZigbeeUi();
#endif

#if CONFIG_JC4880_FEATURE_SECURITY
    refreshSecurityUi();
#endif

#if APP_SETTINGS_FEATURE_DISPLAY_MENU
    refreshDisplayIdleUi();
#endif
}

void AppSettings::setZigbeeKeyboardVisible(bool visible)
{
    if (!isUiActive() || !lv_obj_ready(_zigbeeKeyboard) || !lv_obj_ready(_zigbeeNameTextArea)) {
        return;
    }

    if (visible) {
        lv_keyboard_set_textarea(_zigbeeKeyboard, _zigbeeNameTextArea);
        lv_obj_clear_flag(_zigbeeKeyboard, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_keyboard_set_textarea(_zigbeeKeyboard, nullptr);
        lv_obj_add_flag(_zigbeeKeyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

bool AppSettings::persistZigbeeNameFromUi(void)
{
    if (_zigbeeNameTextArea == nullptr) {
        return false;
    }

    std::string name = trim_copy(lv_textarea_get_text(_zigbeeNameTextArea));
    if (name.empty()) {
        name = kZigbeeDefaultDeviceName;
    }

    if (name.size() > 31) {
        name.resize(31);
    }

    lv_textarea_set_text(_zigbeeNameTextArea, name.c_str());
    return setNvsStringParam(NVS_KEY_ZIGBEE_DEVICE_NAME, name.c_str());
}

void AppSettings::setBluetoothKeyboardVisible(bool visible)
{
    if (!isUiActive() || !lv_obj_ready(_bluetoothKeyboard) || !lv_obj_ready(_bluetoothNameTextArea)) {
        return;
    }

    if (visible) {
        lv_keyboard_set_textarea(_bluetoothKeyboard, _bluetoothNameTextArea);
        lv_obj_clear_flag(_bluetoothKeyboard, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_keyboard_set_textarea(_bluetoothKeyboard, nullptr);
        lv_obj_add_flag(_bluetoothKeyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

bool AppSettings::persistBluetoothNameFromUi(void)
{
    if (_bluetoothNameTextArea == nullptr) {
        return false;
    }

    std::string name = trim_copy(lv_textarea_get_text(_bluetoothNameTextArea));
    if (name.empty()) {
        name = kBleDefaultDeviceName;
    }

    if (name.size() > 31) {
        name.resize(31);
    }

    lv_textarea_set_text(_bluetoothNameTextArea, name.c_str());
    if (!setNvsStringParam(NVS_KEY_BLE_DEVICE_NAME, name.c_str())) {
        return false;
    }

    return bleUpdateConfiguredName(name) == ESP_OK;
}


void AppSettings::refreshHardwareMonitorUi(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#endif
    if (!isUiActive()) {
        return;
    }

    auto setMonitorBar = [](lv_obj_t *bar, int32_t percent, lv_color_t color) {
        if (!lv_obj_ready(bar)) {
            return;
        }

        lv_bar_set_value(bar, std::max<int32_t>(0, std::min<int32_t>(100, percent)), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    };

    hardware_history_service::Snapshot hardware_snapshot = {};
    const bool hardware_snapshot_ready = hardware_history_service::get_snapshot(hardware_snapshot);
    if (_hardwareFastHistoryScratch == nullptr) {
        _hardwareFastHistoryScratch = static_cast<uint8_t *>(allocate_psram_preferred_buffer(hardware_history_service::kFastHistorySamples));
        if (_hardwareFastHistoryScratch != nullptr) {
            std::memset(_hardwareFastHistoryScratch, 0, hardware_history_service::kFastHistorySamples);
        }
    }
    if (_hardwareSlowHistoryScratch == nullptr) {
        _hardwareSlowHistoryScratch = static_cast<uint8_t *>(allocate_psram_preferred_buffer(hardware_history_service::kSlowHistorySamples));
        if (_hardwareSlowHistoryScratch != nullptr) {
            std::memset(_hardwareSlowHistoryScratch, 0, hardware_history_service::kSlowHistorySamples);
        }
    }

    auto updateTrendChart = [&](HardwareTrendCardIndex index,
                                hardware_history_service::Metric metric,
                                const char *title,
                                const string &summary,
                                const char *footer,
                                lv_color_t line_color,
                                lv_color_t background_color) {
        HardwareTrendUi &trend_ui = _hardwareTrendUi[index];

        if (lv_obj_ready(trend_ui.expandLabel)) {
            lv_label_set_text(trend_ui.expandLabel, trend_ui.expanded ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
        }
        if (lv_obj_ready(trend_ui.historyTitleLabel)) {
            lv_label_set_text(trend_ui.historyTitleLabel, title);
        }
        if (lv_obj_ready(trend_ui.historySummaryLabel)) {
            lv_label_set_text(trend_ui.historySummaryLabel, summary.c_str());
        }

        const bool slow_metric = (metric == hardware_history_service::Metric::WifiSignal);
        uint8_t *history_buffer = slow_metric ? _hardwareSlowHistoryScratch : _hardwareFastHistoryScratch;
        const std::size_t history_capacity = slow_metric ? hardware_history_service::kSlowHistorySamples
                                 : hardware_history_service::kFastHistorySamples;
        const std::size_t sample_count = (history_buffer != nullptr)
                             ? hardware_history_service::copy_samples(metric, history_buffer, history_capacity)
                             : 0;

        if (lv_obj_ready(trend_ui.historyChart) && (trend_ui.historySeries != nullptr)) {
            lv_chart_set_point_count(trend_ui.historyChart, std::max<std::size_t>(sample_count, 2));
            lv_chart_set_all_value(trend_ui.historyChart, trend_ui.historySeries, sample_count > 0 ? history_buffer[0] : 0);
            for (std::size_t sample_index = 0; sample_index < sample_count; ++sample_index) {
                lv_chart_set_next_value(trend_ui.historyChart, trend_ui.historySeries, history_buffer[sample_index]);
            }
            lv_obj_set_style_bg_color(trend_ui.historyChart, background_color, LV_PART_MAIN);
            lv_obj_set_style_bg_color(trend_ui.historyChart, line_color, LV_PART_ITEMS);
            lv_chart_refresh(trend_ui.historyChart);
        }
        if (lv_obj_ready(trend_ui.historyLeftLabel)) {
            lv_label_set_text(trend_ui.historyLeftLabel,
                              sample_count > 1 ? (slow_metric ? "59m ago" : "60m ago") : "Collecting history");
        }
        if (lv_obj_ready(trend_ui.historyRightLabel)) {
            lv_label_set_text(trend_ui.historyRightLabel, "Now");
        }
        if (lv_obj_ready(trend_ui.historyFooterLabel)) {
            lv_label_set_text(trend_ui.historyFooterLabel, footer);
        }
    };

#if CONFIG_JC4880_FEATURE_BATTERY
    battery_history_service::Status battery_status = {};
    battery_history_service::HistorySample battery_samples[battery_history_service::kMaxHistorySamples] = {};
    const std::size_t battery_sample_count = battery_history_service::copy_samples(battery_samples, battery_history_service::kMaxHistorySamples);
    if (battery_history_service::get_status(battery_status)) {
        if (lv_obj_ready(_hardwareBatteryValueLabel)) {
            const string text = std::to_string(battery_status.capacity_percent) + "%";
            lv_label_set_text(_hardwareBatteryValueLabel, text.c_str());
        }
        if (lv_obj_ready(_hardwareBatteryExpandLabel)) {
            lv_label_set_text(_hardwareBatteryExpandLabel, _hardwareBatteryExpanded ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
        }
        if (lv_obj_ready(_hardwareBatteryDetailLabel)) {
            string detail = battery_status.charging ? "Charging" : "On battery";
            if (battery_status.eta_minutes >= 0) {
                detail += battery_status.charging ? " · full in " : " · ";
                if (!battery_status.charging) {
                    detail += formatDurationMinutes(battery_status.eta_minutes) + " left";
                } else {
                    detail += formatDurationMinutes(battery_status.eta_minutes);
                }
            } else {
                detail += battery_status.charging ? " · estimating time to full" : " · estimating battery life";
            }
            detail += _hardwareBatteryExpanded ? "\nTap to collapse history" : "\nTap to expand history";
            lv_label_set_text(_hardwareBatteryDetailLabel, detail.c_str());
        }
        setMonitorBar(_hardwareBatteryBar,
                      battery_status.capacity_percent,
                      getBatteryBarColor(battery_status.capacity_percent, battery_status.charging));

        if (lv_obj_ready(_hardwareBatteryHistoryTitleLabel)) {
            lv_label_set_text(_hardwareBatteryHistoryTitleLabel,
                              battery_status.charging ? "Charging history" : "Battery drain history");
        }
        if (lv_obj_ready(_hardwareBatteryHistorySummaryLabel)) {
            string summary = std::string(battery_status.charging ? "Currently charging at " : "Currently on battery at ") +
                             std::to_string(battery_status.capacity_percent) + "%";
            if (battery_status.eta_minutes >= 0) {
                summary += battery_status.charging ? (". Full in " + formatDurationMinutes(battery_status.eta_minutes))
                                                   : (". Estimated life: " + formatDurationMinutes(battery_status.eta_minutes));
            }
            lv_label_set_text(_hardwareBatteryHistorySummaryLabel, summary.c_str());
        }
        if (lv_obj_ready(_hardwareBatteryHistoryChart) && (_hardwareBatteryHistorySeries != nullptr)) {
            lv_chart_set_point_count(_hardwareBatteryHistoryChart, std::max<std::size_t>(battery_sample_count, 2));
            lv_chart_set_all_value(_hardwareBatteryHistoryChart,
                                   _hardwareBatteryHistorySeries,
                                   battery_sample_count > 0 ? ((battery_samples[0].capacity_tenths + 5) / 10) : 0);
            for (std::size_t index = 0; index < battery_sample_count; ++index) {
                lv_chart_set_next_value(_hardwareBatteryHistoryChart,
                                        _hardwareBatteryHistorySeries,
                                        (battery_samples[index].capacity_tenths + 5) / 10);
            }
            lv_obj_set_style_bg_color(_hardwareBatteryHistoryChart,
                                      battery_status.charging ? lv_color_hex(0xDCFCE7) : lv_color_hex(0xFEF3C7),
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_color(_hardwareBatteryHistoryChart,
                                      battery_status.charging ? lv_color_hex(0x16A34A) : lv_color_hex(0xF59E0B),
                                      LV_PART_ITEMS);
            lv_chart_refresh(_hardwareBatteryHistoryChart);
        }
        if (lv_obj_ready(_hardwareBatteryHistoryLeftLabel)) {
            if (battery_sample_count > 1) {
                const int64_t oldest_age_sec = std::max<int64_t>(0, battery_status.timestamp_sec - battery_samples[0].timestamp_sec);
                const string label = formatLookbackMinutes(static_cast<int32_t>(oldest_age_sec / 60));
                lv_label_set_text(_hardwareBatteryHistoryLeftLabel, label.c_str());
            } else {
                lv_label_set_text(_hardwareBatteryHistoryLeftLabel, "Waiting for history");
            }
        }
        if (lv_obj_ready(_hardwareBatteryHistoryRightLabel)) {
            lv_label_set_text(_hardwareBatteryHistoryRightLabel, "Now");
        }
        if (lv_obj_ready(_hardwareBatteryHistoryFooterLabel)) {
            string footer = "Sampling every 1 minute, keeping the latest 60 points (~1 hour) in PSRAM.";
            if (battery_sample_count < 2) {
                footer += " More time is needed before trend and ETA stabilize.";
            }
            lv_label_set_text(_hardwareBatteryHistoryFooterLabel, footer.c_str());
        }
    } else {
        if (lv_obj_ready(_hardwareBatteryValueLabel)) {
            lv_label_set_text(_hardwareBatteryValueLabel, "Unavailable");
        }
        if (lv_obj_ready(_hardwareBatteryExpandLabel)) {
            lv_label_set_text(_hardwareBatteryExpandLabel, _hardwareBatteryExpanded ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
        }
        if (lv_obj_ready(_hardwareBatteryDetailLabel)) {
            lv_label_set_text(_hardwareBatteryDetailLabel, "Battery monitoring is unavailable on this build.\nTap to retry after startup settles.");
        }
        setMonitorBar(_hardwareBatteryBar, 0, lv_color_hex(0x94A3B8));
        if (lv_obj_ready(_hardwareBatteryHistoryTitleLabel)) {
            lv_label_set_text(_hardwareBatteryHistoryTitleLabel, "Battery history");
        }
        if (lv_obj_ready(_hardwareBatteryHistorySummaryLabel)) {
            lv_label_set_text(_hardwareBatteryHistorySummaryLabel, "Battery status is still initializing.");
        }
        if (lv_obj_ready(_hardwareBatteryHistoryChart) && (_hardwareBatteryHistorySeries != nullptr)) {
            lv_chart_set_point_count(_hardwareBatteryHistoryChart, 2);
            lv_chart_set_all_value(_hardwareBatteryHistoryChart, _hardwareBatteryHistorySeries, 0);
            lv_obj_set_style_bg_color(_hardwareBatteryHistoryChart, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
            lv_obj_set_style_bg_color(_hardwareBatteryHistoryChart, lv_color_hex(0x94A3B8), LV_PART_ITEMS);
            lv_chart_refresh(_hardwareBatteryHistoryChart);
        }
        if (lv_obj_ready(_hardwareBatteryHistoryLeftLabel)) {
            lv_label_set_text(_hardwareBatteryHistoryLeftLabel, "Waiting for history");
        }
        if (lv_obj_ready(_hardwareBatteryHistoryRightLabel)) {
            lv_label_set_text(_hardwareBatteryHistoryRightLabel, "Now");
        }
        if (lv_obj_ready(_hardwareBatteryHistoryFooterLabel)) {
            lv_label_set_text(_hardwareBatteryHistoryFooterLabel,
                              "Battery history becomes available after the sampler collects enough points.");
        }
    }
#endif

    const uint64_t total_sram = hardware_snapshot_ready ? hardware_snapshot.sram_total_bytes : 0;
    const uint64_t used_sram = hardware_snapshot_ready ? hardware_snapshot.sram_used_bytes : 0;
    const int32_t sram_percent = hardware_snapshot_ready ? hardware_snapshot.sram_percent : 0;
    const char *sram_hint = _hardwareTrendUi[HARDWARE_TREND_SRAM].expanded ? "\nTap to collapse history" : "\nTap to expand history";

    if (lv_obj_ready(_hardwareSramValueLabel)) {
        const string text = formatPercentUsed(sram_percent);
        lv_label_set_text(_hardwareSramValueLabel, text.c_str());
    }
    if (lv_obj_ready(_hardwareSramDetailLabel)) {
        const string detail = formatStorageAmount(used_sram) + " / " + formatStorageAmount(total_sram) + " occupied" + sram_hint;
        lv_label_set_text(_hardwareSramDetailLabel, detail.c_str());
    }
    setMonitorBar(_hardwareSramBar, sram_percent, getMonitorBarColor(sram_percent));
    updateTrendChart(HARDWARE_TREND_SRAM,
                     hardware_history_service::Metric::SramUsage,
                     "SRAM usage history",
                     formatStorageAmount(used_sram) + " used out of " + formatStorageAmount(total_sram) + ".",
                     "Stored in PSRAM every 1 second, keeping the latest 3600 points (1 hour).",
                     getMonitorBarColor(sram_percent),
                     lv_color_hex(0xDBEAFE));

    const uint64_t total_psram = hardware_snapshot_ready ? hardware_snapshot.psram_total_bytes : 0;
    const uint64_t used_psram = hardware_snapshot_ready ? hardware_snapshot.psram_used_bytes : 0;
    const int32_t psram_percent = hardware_snapshot_ready ? hardware_snapshot.psram_percent : 0;
    const char *psram_hint = _hardwareTrendUi[HARDWARE_TREND_PSRAM].expanded ? "\nTap to collapse history" : "\nTap to expand history";

    if (lv_obj_ready(_hardwarePsramValueLabel)) {
        const string text = formatPercentUsed(psram_percent);
        lv_label_set_text(_hardwarePsramValueLabel, text.c_str());
    }
    if (lv_obj_ready(_hardwarePsramDetailLabel)) {
        const string detail = formatStorageAmount(used_psram) + " / " + formatStorageAmount(total_psram) + " occupied" + psram_hint;
        lv_label_set_text(_hardwarePsramDetailLabel, detail.c_str());
    }
    setMonitorBar(_hardwarePsramBar, psram_percent, getMonitorBarColor(psram_percent));
    updateTrendChart(HARDWARE_TREND_PSRAM,
                     hardware_history_service::Metric::PsramUsage,
                     "PSRAM usage history",
                     formatStorageAmount(used_psram) + " used out of " + formatStorageAmount(total_psram) + ".",
                     "Stored in PSRAM every 1 second, keeping the latest 3600 points (1 hour).",
                     getMonitorBarColor(psram_percent),
                     lv_color_hex(0xDBEAFE));

    uint64_t sd_total = 0;
    uint64_t sd_used = 0;
    const bool sd_mounted = app_storage_ensure_sdcard_available();
    bool sd_capacity_ready = false;
    uint64_t sd_total_bytes = 0;
    uint64_t sd_free_bytes = 0;
    if (sd_mounted &&
        (esp_vfs_fat_info(kSdCardMountPoint, &sd_total_bytes, &sd_free_bytes) == ESP_OK)) {
        sd_total = sd_total_bytes;
        sd_used = (sd_total >= sd_free_bytes) ? (sd_total - sd_free_bytes) : 0;
        sd_capacity_ready = (sd_total > 0);
    }

    if (lv_obj_ready(_hardwareSdValueLabel)) {
        if (sd_capacity_ready) {
            const string text = formatPercentUsed(calculatePercent(sd_used, sd_total));
            lv_label_set_text(_hardwareSdValueLabel, text.c_str());
        } else if (sd_mounted) {
            lv_label_set_text(_hardwareSdValueLabel, "Mounted");
        } else {
            lv_label_set_text(_hardwareSdValueLabel, "Not mounted");
        }
    }
    if (lv_obj_ready(_hardwareSdDetailLabel)) {
        if (sd_capacity_ready) {
            const string detail = formatStorageAmount(sd_used) + " / " + formatStorageAmount(sd_total) + " occupied";
            lv_label_set_text(_hardwareSdDetailLabel, detail.c_str());
        } else if (sd_mounted) {
            lv_label_set_text(_hardwareSdDetailLabel, "SD card is mounted, but capacity information is temporarily unavailable.");
        } else {
            lv_label_set_text(_hardwareSdDetailLabel, "Insert or remount the SD card to monitor storage usage.");
        }
    }
    setMonitorBar(_hardwareSdBar, sd_capacity_ready ? calculatePercent(sd_used, sd_total) : 0,
                  sd_capacity_ready ? getMonitorBarColor(calculatePercent(sd_used, sd_total)) : lv_color_hex(0x94A3B8));

    const bool wifi_connected = hardware_snapshot_ready && hardware_snapshot.wifi_connected;
    const char *wifi_hint = _hardwareTrendUi[HARDWARE_TREND_WIFI].expanded ? "\nTap to collapse history" : "\nTap to expand history";
    if (lv_obj_ready(_hardwareWifiValueLabel)) {
        if (wifi_connected) {
            const string text = formatSignedWithUnit(static_cast<int32_t>(hardware_snapshot.wifi_rssi), "dBm");
            lv_label_set_text(_hardwareWifiValueLabel, text.c_str());
        } else {
            lv_label_set_text(_hardwareWifiValueLabel, "Disconnected");
        }
    }
    if (lv_obj_ready(_hardwareWifiDetailLabel)) {
        if (wifi_connected) {
            string detail = "Connected to ";
            detail += hardware_snapshot.wifi_ssid;
            detail += wifi_hint;
            lv_label_set_text(_hardwareWifiDetailLabel, detail.c_str());
        } else {
            const string detail = string("Join a network to view live signal strength.") + wifi_hint;
            lv_label_set_text(_hardwareWifiDetailLabel, detail.c_str());
        }
    }
    const int32_t wifi_percent = wifi_connected ? hardware_snapshot.wifi_percent : 0;
    setMonitorBar(_hardwareWifiBar, wifi_percent, wifi_connected ? getMonitorBarColor(100 - wifi_percent) : lv_color_hex(0x94A3B8));
    updateTrendChart(HARDWARE_TREND_WIFI,
                     hardware_history_service::Metric::WifiSignal,
                     "Wi-Fi signal history",
                     wifi_connected ? (string("Connected to ") + hardware_snapshot.wifi_ssid + " at " + formatSignedWithUnit(hardware_snapshot.wifi_rssi, "dBm"))
                                    : string("No active Wi-Fi link."),
                     "Stored in PSRAM every 1 minute, keeping the latest 60 points (1 hour).",
                     wifi_connected ? getMonitorBarColor(100 - wifi_percent) : lv_color_hex(0x94A3B8),
                     lv_color_hex(0xDBEAFE));

    if (lv_obj_ready(_hardwareCpuSpeedValueLabel)) {
        const string text = hardware_snapshot_ready && hardware_snapshot.cpu_load_available
                                ? (std::to_string(hardware_snapshot.cpu_load_percent) + "%")
                                : string("Measuring");
        lv_label_set_text(_hardwareCpuSpeedValueLabel, text.c_str());
    }

    if (lv_obj_ready(_hardwareCpuSpeedDetailLabel)) {
        const char *cpu_hint = _hardwareTrendUi[HARDWARE_TREND_CPU_LOAD].expanded ? "\nTap to collapse history" : "\nTap to expand history";
        string uptime_text = hardware_snapshot_ready && (hardware_snapshot.cpu_clock_mhz > 0)
                                 ? (formatSignedWithUnit(hardware_snapshot.cpu_clock_mhz, "MHz") + string(" configured"))
                                 : string("CPU clock unavailable");
        uptime_text += "\nUptime: ";
        uptime_text += formatUptime(hardware_snapshot_ready ? hardware_snapshot.uptime_sec : 0);
        uptime_text += cpu_hint;
        lv_label_set_text(_hardwareCpuSpeedDetailLabel, uptime_text.c_str());
    }
    setMonitorBar(_hardwareCpuSpeedBar,
                  hardware_snapshot_ready && hardware_snapshot.cpu_load_available ? hardware_snapshot.cpu_load_percent : 0,
                  hardware_snapshot_ready && hardware_snapshot.cpu_load_available ? getMonitorBarColor(hardware_snapshot.cpu_load_percent)
                                                                               : lv_color_hex(0x94A3B8));
    updateTrendChart(HARDWARE_TREND_CPU_LOAD,
                     hardware_history_service::Metric::CpuLoad,
                     "CPU load history",
                     hardware_snapshot_ready && hardware_snapshot.cpu_load_available
                         ? (string("Current load: ") + std::to_string(hardware_snapshot.cpu_load_percent) + "% at " +
                            formatSignedWithUnit(hardware_snapshot.cpu_clock_mhz, "MHz"))
                         : string("Collecting CPU runtime statistics."),
                     "Stored in PSRAM every 1 second, keeping the latest 3600 points (1 hour).",
                     hardware_snapshot_ready && hardware_snapshot.cpu_load_available ? getMonitorBarColor(hardware_snapshot.cpu_load_percent)
                                                                                   : lv_color_hex(0x94A3B8),
                     lv_color_hex(0xDBEAFE));

    const bool has_cpu_temp = hardware_snapshot_ready && hardware_snapshot.cpu_temperature_available;
    const int32_t cpu_temp_tenths = hardware_snapshot_ready ? hardware_snapshot.cpu_temperature_tenths : 0;
    const float cpu_temp_celsius = static_cast<float>(cpu_temp_tenths) / 10.0f;
    if (lv_obj_ready(_hardwareCpuTempValueLabel)) {
        if (has_cpu_temp) {
            const string text = formatTemperatureCelsius(cpu_temp_celsius);
            lv_label_set_text(_hardwareCpuTempValueLabel, text.c_str());
        } else {
            lv_label_set_text(_hardwareCpuTempValueLabel, "Unavailable");
        }
    }
    if (lv_obj_ready(_hardwareCpuTempDetailLabel)) {
        const char *temp_hint = _hardwareTrendUi[HARDWARE_TREND_CPU_TEMP].expanded ? "\nTap to collapse history" : "\nTap to expand history";
        const string detail = has_cpu_temp ? (string("Background sampling every 10 seconds.") + temp_hint)
                                           : string("Temperature sensor is not available on this build.");
        lv_label_set_text(_hardwareCpuTempDetailLabel, detail.c_str());
    }
    const int32_t temp_percent = has_cpu_temp ? std::max<int32_t>(0, std::min<int32_t>(100, static_cast<int32_t>(cpu_temp_celsius))) : 0;
    setMonitorBar(_hardwareCpuTempBar, temp_percent, has_cpu_temp ? getMonitorBarColor(temp_percent) : lv_color_hex(0x94A3B8));
    updateTrendChart(HARDWARE_TREND_CPU_TEMP,
                     hardware_history_service::Metric::CpuTemperature,
                     "CPU temperature history",
                     has_cpu_temp ? (string("Current temperature: ") + formatTemperatureCelsius(cpu_temp_celsius))
                                  : string("Temperature sensor is unavailable."),
                     "Stored in PSRAM every 10 seconds, keeping the latest 360 points (1 hour).",
                     has_cpu_temp ? getMonitorBarColor(temp_percent) : lv_color_hex(0x94A3B8),
                     lv_color_hex(0xDBEAFE));
}

void AppSettings::setBatteryHistoryExpanded(bool expanded, bool animate)
{
#if !CONFIG_JC4880_FEATURE_BATTERY
    (void)expanded;
    (void)animate;
    return;
#else
    if (!lv_obj_ready(_hardwareBatteryCard)) {
        return;
    }
    _hardwareBatteryExpanded = expanded;
    if (lv_obj_ready(_hardwareBatteryExpandLabel)) {
        lv_label_set_text(_hardwareBatteryExpandLabel, _hardwareBatteryExpanded ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
    }

    const lv_coord_t target_height = expanded ? kBatteryCardExpandedHeight : kBatteryCardCollapsedHeight;
    lv_anim_del(_hardwareBatteryCard, animateObjectHeight);
    if (!animate) {
        lv_obj_set_height(_hardwareBatteryCard, target_height);
    } else {
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, _hardwareBatteryCard);
        lv_anim_set_exec_cb(&animation, animateObjectHeight);
        lv_anim_set_time(&animation, kBatteryCardExpandAnimMs);
        lv_anim_set_values(&animation, lv_obj_get_height(_hardwareBatteryCard), target_height);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
        lv_anim_start(&animation);
    }

    if (expanded) {
        lv_obj_scroll_to_view(_hardwareBatteryCard, LV_ANIM_ON);
    }
#endif
}

void AppSettings::setHardwareTrendExpanded(HardwareTrendCardIndex index, bool expanded, bool animate)
{
    if ((index < HARDWARE_TREND_CPU_LOAD) || (index >= HARDWARE_TREND_CARD_COUNT)) {
        return;
    }

    HardwareTrendUi &trend_ui = _hardwareTrendUi[index];
    if (!lv_obj_ready(trend_ui.card)) {
        return;
    }

    trend_ui.expanded = expanded;
    if (lv_obj_ready(trend_ui.expandLabel)) {
        lv_label_set_text(trend_ui.expandLabel, expanded ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
    }

    const lv_coord_t target_height = expanded ? kHardwareTrendCardExpandedHeight : kHardwareTrendCardCollapsedHeight;
    lv_anim_del(trend_ui.card, animateObjectHeight);
    if (!animate) {
        lv_obj_set_height(trend_ui.card, target_height);
    } else {
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, trend_ui.card);
        lv_anim_set_exec_cb(&animation, animateObjectHeight);
        lv_anim_set_time(&animation, kHardwareTrendCardExpandAnimMs);
        lv_anim_set_values(&animation, lv_obj_get_height(trend_ui.card), target_height);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
        lv_anim_start(&animation);
    }

    if (expanded) {
        lv_obj_scroll_to_view(trend_ui.card, LV_ANIM_ON);
    }
}


void AppSettings::euiRefresTask(void *arg)
{
    AppSettings *app = (AppSettings *)arg;
    uint16_t free_sram_size_kb = 0;
    uint16_t total_sram_size_kb = 0;
    uint16_t free_psram_size_kb = 0;
    uint16_t total_psram_size_kb = 0;

    if (app == NULL) {
        ESP_LOGE(TAG, "App instance is NULL");
        goto err;
    }

    while (1) {
#if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
        bleCheckStartupTimeout();
#endif
        if (app->isUiActive()) {
            bsp_display_lock(0);
            app->refreshRadioStatusBar();
#if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
            if (app->_screen_index == UI_BLUETOOTH_SETTING_INDEX) {
                app->refreshBluetoothUi();
            }
#endif
            bsp_display_unlock();
        }

        /* Updte Smart Gadget app */
        // app->updateGadgetTime(timeinfo);

        // Update memory in backstage
        if(app->backstage->checkVisible()) {
            free_sram_size_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
            total_sram_size_kb = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;
            free_psram_size_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
            total_psram_size_kb = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
            ESP_LOGI(TAG, "Free sram size: %d KB, total sram size: %d KB, "
                        "free psram size: %d KB, total psram size: %d KB",
                        free_sram_size_kb, total_sram_size_kb, free_psram_size_kb, total_psram_size_kb);

            bsp_display_lock(0);
            if(!app->backstage->setMemoryLabel(free_sram_size_kb, total_sram_size_kb, free_psram_size_kb, total_psram_size_kb)) {
                ESP_LOGE(TAG, "Update memory usage failed");
            }
            bsp_display_unlock();
        }

        if (app->isUiActive() && (app->_screen_index == UI_HARDWARE_SETTING_INDEX)) {
            bsp_display_lock(0);
            app->refreshHardwareMonitorUi();
            bsp_display_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(HOME_REFRESH_TASK_PERIOD_MS));
    }

err:
    vTaskDelete(NULL);
}



void AppSettings::onScreenLoadEventCallback( lv_event_t * e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    SettingScreenIndex_t last_scr_index = app->_screen_index;

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    for (int i = 0; i < UI_MAX_INDEX; i++) {
        if (app->_screen_list[i] == lv_event_get_target(e)) {
            app->_screen_index = (SettingScreenIndex_t)i;
            break;
        }
    }

    #if APP_SETTINGS_FEATURE_WIFI
    if (last_scr_index == UI_WIFI_SCAN_INDEX) {
        app->stopWifiScan();
    }

    if (app->_screen_index != UI_WIFI_CONNECT_INDEX) {
        app->setWifiKeyboardVisible(false);
        app->updateWifiPasswordVisibility(false);
    }
    #endif

    #if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    if (app->_screen_index != UI_BLUETOOTH_SETTING_INDEX) {
        app->setBluetoothKeyboardVisible(false);
    }
    #endif

    #if CONFIG_JC4880_FEATURE_ZIGBEE
    if (app->_screen_index != UI_ZIGBEE_SETTING_INDEX) {
        app->setZigbeeKeyboardVisible(false);
    }
    #endif

    #if CONFIG_JC4880_FEATURE_OTA
    if ((last_scr_index == UI_FIRMWARE_SETTING_INDEX) && (app->_screen_index != UI_FIRMWARE_SETTING_INDEX)) {
        app->releaseFirmwareOtaResources();
    }
    #endif

    #if APP_SETTINGS_FEATURE_WIFI
    if (app->_screen_index == UI_WIFI_SCAN_INDEX) {
        app->stopWifiScan();
        app->refreshSavedWifiUi();
    }
    #endif

    #if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    if (app->_screen_index == UI_BLUETOOTH_SETTING_INDEX) {
        app->refreshBluetoothUi();
    }
    #endif

    #if CONFIG_JC4880_FEATURE_ZIGBEE
    if (app->_screen_index == UI_ZIGBEE_SETTING_INDEX) {
        app->refreshZigbeeUi();
    }
    #endif

    #if CONFIG_JC4880_FEATURE_SECURITY
    if (app->_screen_index == UI_SECURITY_SETTING_INDEX) {
        app->refreshSecurityUi();
    }
    #endif

    #if CONFIG_JC4880_FEATURE_ABOUT_DEVICE
    if (app->_screen_index == UI_ABOUT_SETTING_INDEX) {
        app->refreshAboutWifiUi();
    }
    #endif

    #if APP_SETTINGS_FEATURE_HARDWARE_MENU
    if (app->_screen_index == UI_HARDWARE_SETTING_INDEX) {
        app->refreshHardwareMonitorUi();
    }
    #endif

    #if CONFIG_JC4880_FEATURE_OTA
    if (app->_screen_index == UI_FIRMWARE_SETTING_INDEX) {
        app->scanSdFirmwareEntries();
        app->refreshFirmwareUi();
    }
    #endif

end:
    return;
}

void AppSettings::onHardwareBatteryCardClickedEventCallback(lv_event_t *e)
{
#if !CONFIG_JC4880_FEATURE_BATTERY
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");
    app->setBatteryHistoryExpanded(!app->_hardwareBatteryExpanded, true);
end:
    return;
#endif
}

void AppSettings::onHardwareTrendCardClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    lv_obj_t *current_target = lv_event_get_current_target(e);
    for (int index = HARDWARE_TREND_CPU_LOAD; index < HARDWARE_TREND_CARD_COUNT; ++index) {
        if (app->_hardwareTrendUi[index].card == current_target) {
            app->setHardwareTrendExpanded(static_cast<HardwareTrendCardIndex>(index),
                                          !app->_hardwareTrendUi[index].expanded,
                                          true);
            break;
        }
    }
}

void AppSettings::onMainMenuItemClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *target = lv_event_get_target(e);

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    #if APP_SETTINGS_FEATURE_WIFI
    if (target == app->_wifiMenuItem) {
        lv_scr_load_anim(ui_ScreenSettingWiFi, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
    } else
    #endif
    #if CONFIG_JC4880_FEATURE_AUDIO
    if (target == app->_audioMenuItem) {
        lv_scr_load_anim(ui_ScreenSettingVolume, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
    } else
    #endif
    #if APP_SETTINGS_FEATURE_DISPLAY_MENU
    if (target == app->_displayMenuItem) {
        lv_scr_load_anim(ui_ScreenSettingLight, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
    } else
    #endif
    #if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    if (target == app->_bluetoothMenuItem) {
        lv_scr_load_anim(ui_ScreenSettingBLE, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
    } else
    #endif
    #if CONFIG_JC4880_FEATURE_ZIGBEE
    if (target == app->_zigbeeMenuItem) {
        app->ensureZigbeeScreen();
        lv_scr_load_anim(app->_zigbeeScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
    } else
    #endif
    #if APP_SETTINGS_FEATURE_HARDWARE_MENU
    if (target == app->_hardwareMenuItem) {
        app->ensureHardwareScreen();
        lv_scr_load_anim(app->_hardwareScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
    } else
    #endif
    #if CONFIG_JC4880_FEATURE_SECURITY
    if (target == app->_securityMenuItem) {
        app->ensureSecurityScreen();
        lv_scr_load_anim(app->_securityScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
    } else
    #endif
    #if CONFIG_JC4880_FEATURE_OTA
    if (target == app->_firmwareMenuItem) {
        app->ensureFirmwareScreen();
        lv_scr_load_anim(app->_firmwareScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
    } else
    #endif
    #if CONFIG_JC4880_FEATURE_ABOUT_DEVICE
    if (target == app->_aboutMenuItem) {
        lv_scr_load_anim(ui_ScreenSettingAbout, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
    }
    #endif

end:
    return;
}

void AppSettings::onFirmwareMenuClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    app->ensureFirmwareScreen();
    lv_scr_load_anim(app->_firmwareScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);

end:
    return;
}

void AppSettings::onFirmwareSdRefreshClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    app->scanSdFirmwareEntries();
    app->refreshFirmwareUi();
    if (app->_sdFirmwareEntries.empty()) {
        app->setFirmwareStatus("No valid .bin firmware images found on /sdcard or /sdcard/firmware.", true);
    }

end:
    return;
}

void AppSettings::onFirmwareOtaCheckClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (app->_firmwareUpdateInProgress) {
        app->setFirmwareStatus("A firmware update is already running.", true);
        goto end;
    }

    if (app->_firmwareOtaCheckInProgress) {
        goto end;
    }

    app->_firmwareOtaCheckInProgress = true;
    app->refreshFirmwareUi();
    app->setFirmwareOtaCheckOverlayVisible(true, "Checking GitHub for firmware releases...\nPlease wait.");
    lv_refr_now(nullptr);
    app->setFirmwareStatus("Checking GitHub releases...");
    app->setFirmwareProgress(0, "Querying GitHub releases...");
    if (!app->fetchGithubFirmwareEntries()) {
        app->setSelectedOtaFirmwareIndex(-1);
        app->_firmwareOtaCheckInProgress = false;
        app->setFirmwareOtaCheckOverlayVisible(false);
        app->refreshFirmwareUi();
        app->setFirmwareStatus("No OTA .bin assets were found in GitHub releases, or the request failed.", true);
        goto end;
    }

    app->setSelectedOtaFirmwareIndex(-1);
    app->_firmwareOtaCheckInProgress = false;
    app->setFirmwareOtaCheckOverlayVisible(false);
    app->refreshFirmwareUi();

end:
    return;
}

void AppSettings::onFirmwareSdFlashClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    if (app->_firmwareUpdateInProgress) {
        app->setFirmwareStatus("A firmware update is already running.", true);
        return;
    }

    if (!app->hasOtaFlashSupport()) {
        app->setFirmwareStatus("In-app flashing is blocked because the current partition table has no OTA slot.", true);
        return;
    }

    const uint16_t selected = (app->_firmwareSdDropdown != nullptr) ? lv_dropdown_get_selected(app->_firmwareSdDropdown) : 0;
    if ((selected >= app->_sdFirmwareEntries.size()) || !app->_sdFirmwareEntries[selected].is_valid) {
        app->setFirmwareStatus("Select a valid SD firmware image first.", true);
        return;
    }

    app->flashFirmwareEntry(app->_sdFirmwareEntries[selected], FIRMWARE_UPDATE_SOURCE_SD);
}

void AppSettings::onFirmwareOtaFlashClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    if (app->_firmwareUpdateInProgress) {
        app->setFirmwareStatus("A firmware update is already running.", true);
        return;
    }

    if (!app->hasOtaFlashSupport()) {
        app->setFirmwareStatus("OTA flashing is blocked because the current partition table has no OTA slot.", true);
        return;
    }

    const int selected = app->getSelectedOtaFirmwareIndex();
    if ((selected < 0) || (static_cast<size_t>(selected) >= app->_otaFirmwareEntries.size()) || !app->_otaFirmwareEntries[selected].is_valid) {
        app->setFirmwareStatus("Select a valid GitHub release asset first.", true);
        return;
    }

    app->flashFirmwareEntry(app->_otaFirmwareEntries[selected], FIRMWARE_UPDATE_SOURCE_OTA);
}

void AppSettings::onFirmwareOtaEntryCheckedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    if (target == nullptr) {
        return;
    }

    const uintptr_t index_value = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target));
    if (index_value == 0) {
        return;
    }

    const int index = static_cast<int>(index_value - 1);
    if (lv_obj_has_state(target, LV_STATE_CHECKED)) {
        app->setSelectedOtaFirmwareIndex(index);
    } else if (app->getSelectedOtaFirmwareIndex() == index) {
        app->setSelectedOtaFirmwareIndex(-1);
    }
    app->refreshFirmwareUi();
}

void AppSettings::onFirmwareSelectionChangedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    app->refreshFirmwareUi();

end:
    return;
}

void AppSettings::onFirmwareFactoryResetClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *msgbox = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    static const char *buttons[] = {"Reset", "Cancel", ""};
    msgbox = lv_msgbox_create(NULL,
                              "Factory Reset",
                              "Erase saved preferences and restore default settings? This clears Wi-Fi, display, audio, and timezone preferences.",
                              buttons,
                              false);
    lv_obj_center(msgbox);
    lv_obj_set_width(msgbox, 360);
    lv_obj_add_event_cb(msgbox, onFirmwareFactoryResetConfirmEventCallback, LV_EVENT_VALUE_CHANGED, app);

end:
    return;
}

void AppSettings::onFirmwareFactoryResetConfirmEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *msgbox = lv_event_get_target(e);
    const char *button_text = nullptr;

    if ((app == nullptr) || (msgbox == nullptr)) {
        return;
    }

    button_text = lv_msgbox_get_active_btn_text(msgbox);
    if ((button_text != nullptr) && (strcmp(button_text, "Reset") == 0)) {
        if (!app->factoryResetPreferences()) {
            app->setFirmwareStatus("Factory reset failed. Preferences were not fully cleared.", true);
        }
    }

    lv_msgbox_close(msgbox);
}


void AppSettings::onSwitchPanelScreenSettingBluetoothValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    enabled = (lv_obj_get_state(ui_SwitchPanelScreenSettingBLESwitch) & LV_STATE_CHECKED) != 0;
    if (!enabled) {
        bleCancelScan();
    }
    if (bleSetEnabled(enabled) != ESP_OK) {
        enabled = false;
        lv_obj_clear_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_CHECKED);
    }

    app->_nvs_param_map[NVS_KEY_BLE_ENABLE] = enabled;
    app->setNvsParam(NVS_KEY_BLE_ENABLE, enabled ? 1 : 0);
    app->refreshBluetoothUi();
    app->refreshRadioStatusBar();

end:
    return;
}

void AppSettings::onBluetoothNameTextAreaEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (code == LV_EVENT_FOCUSED) {
        app->setBluetoothKeyboardVisible(true);
    } else if ((code == LV_EVENT_DEFOCUSED) || (code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        app->setBluetoothKeyboardVisible(false);
    }

end:
    return;
}

void AppSettings::onBluetoothNameSaveClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    app->persistBluetoothNameFromUi();
    app->setBluetoothKeyboardVisible(false);
    app->refreshBluetoothUi();

end:
    return;
}

void AppSettings::onBluetoothKeyboardEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if ((code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        app->setBluetoothKeyboardVisible(false);
        if (code == LV_EVENT_READY) {
            app->persistBluetoothNameFromUi();
            app->refreshBluetoothUi();
        }
    }

end:
    return;
}

void AppSettings::onBluetoothScanClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (s_bleScanInProgress) {
        bleCancelScan();
    } else {
        bleStartScan();
    }

    app->refreshBluetoothUi();

end:
    return;
}

void AppSettings::onZigbeeEnableSwitchValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    enabled = (lv_obj_get_state(app->_zigbeeEnableSwitch) & LV_STATE_CHECKED) != 0;

    app->_nvs_param_map[NVS_KEY_ZIGBEE_ENABLE] = enabled ? 1 : 0;
    app->setNvsParam(NVS_KEY_ZIGBEE_ENABLE, enabled ? 1 : 0);
    app->refreshZigbeeUi();
    app->refreshRadioStatusBar();

end:
    return;
}

void AppSettings::onZigbeeChannelChangedEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    uint16_t selected = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    selected = lv_dropdown_get_selected(app->_zigbeeChannelDropdown);
    if (selected < (sizeof(kZigbeeChannelOptions) / sizeof(kZigbeeChannelOptions[0]))) {
        app->_nvs_param_map[NVS_KEY_ZIGBEE_CHANNEL] = kZigbeeChannelOptions[selected];
        app->setNvsParam(NVS_KEY_ZIGBEE_CHANNEL, kZigbeeChannelOptions[selected]);
        app->refreshZigbeeUi();
    }

end:
    return;
}

void AppSettings::onZigbeePermitJoinChangedEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    uint16_t selected = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    selected = lv_dropdown_get_selected(app->_zigbeePermitJoinDropdown);
    if (selected < (sizeof(kZigbeePermitJoinOptionsSec) / sizeof(kZigbeePermitJoinOptionsSec[0]))) {
        app->_nvs_param_map[NVS_KEY_ZIGBEE_PERMIT_JOIN] = kZigbeePermitJoinOptionsSec[selected];
        app->setNvsParam(NVS_KEY_ZIGBEE_PERMIT_JOIN, kZigbeePermitJoinOptionsSec[selected]);
        app->refreshZigbeeUi();
    }

end:
    return;
}

void AppSettings::onZigbeeNameTextAreaEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (code == LV_EVENT_FOCUSED) {
        app->setZigbeeKeyboardVisible(true);
    }

end:
    return;
}

void AppSettings::onZigbeeNameSaveClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    app->persistZigbeeNameFromUi();
    app->setZigbeeKeyboardVisible(false);
    app->refreshZigbeeUi();

end:
    return;
}

void AppSettings::onZigbeeKeyboardEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (code == LV_EVENT_READY) {
        app->persistZigbeeNameFromUi();
        app->setZigbeeKeyboardVisible(false);
        if (app->_zigbeeNameTextArea != nullptr) {
            lv_obj_clear_state(app->_zigbeeNameTextArea, LV_STATE_FOCUSED);
        }
        app->refreshZigbeeUi();
    } else if (code == LV_EVENT_CANCEL) {
        app->setZigbeeKeyboardVisible(false);
        if (app->_zigbeeNameTextArea != nullptr) {
            lv_obj_clear_state(app->_zigbeeNameTextArea, LV_STATE_FOCUSED);
        }
        app->refreshZigbeeUi();
    }

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingBLESwitchValueChangeEventCallback( lv_event_t * e) {
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool requested_state = false;
    bool current_state = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    requested_state = (lv_obj_get_state(app->_securityDeviceLockSwitch) & LV_STATE_CHECKED) != 0;
    current_state = device_security::isLockEnabled(device_security::LockType::Device);
    if (requested_state == current_state) {
        return;
    }

    if (requested_state) {
        device_security::requestEnable(device_security::LockType::Device,
                                       onSecurityToggleRequestFinished,
                                       &app->_deviceLockToggleContext);
    } else {
        device_security::requestDisable(device_security::LockType::Device,
                                        onSecurityToggleRequestFinished,
                                        &app->_deviceLockToggleContext);
    }

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingSettingsLockValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool requested_state = false;
    bool current_state = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    requested_state = (lv_obj_get_state(app->_securitySettingsLockSwitch) & LV_STATE_CHECKED) != 0;
    current_state = device_security::isLockEnabled(device_security::LockType::Settings);
    if (requested_state == current_state) {
        return;
    }

    if (requested_state) {
        device_security::requestEnable(device_security::LockType::Settings,
                                       onSecurityToggleRequestFinished,
                                       &app->_settingsLockToggleContext);
    } else {
        device_security::requestDisable(device_security::LockType::Settings,
                                        onSecurityToggleRequestFinished,
                                        &app->_settingsLockToggleContext);
    }

end:
    return;
}

void AppSettings::onSecurityToggleRequestFinished(bool success, void *user_data)
{
    SecurityToggleContext *context = static_cast<SecurityToggleContext *>(user_data);
    if ((context == nullptr) || (context->app == nullptr)) {
        return;
    }

    context->app->handleSecurityToggleResult(context->type, success);
}

void AppSettings::handleSecurityToggleResult(device_security::LockType type, bool success)
{
    (void)type;
    (void)success;
    refreshSecurityUi();
}

void AppSettings::onSliderPanelVolumeSwitchValueChangeEventCallback( lv_event_t * e) {
    int volume = lv_slider_get_value(ui_SliderPanelScreenSettingVolumeSwitch);

    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (volume != app->_nvs_param_map[NVS_KEY_AUDIO_VOLUME]) {
        if ((bsp_extra_audio_media_volume_set(volume) != ESP_OK) && (bsp_extra_audio_media_volume_get() != volume)) {
            ESP_LOGE(TAG, "Set volume failed");
            lv_slider_set_value(ui_SliderPanelScreenSettingVolumeSwitch, app->_nvs_param_map[NVS_KEY_AUDIO_VOLUME], LV_ANIM_OFF);
            return;
        }
        app->_nvs_param_map[NVS_KEY_AUDIO_VOLUME] = volume;
        app->setNvsParam(NVS_KEY_AUDIO_VOLUME, volume);
    }

end:
    return;
}

void AppSettings::onSliderPanelSystemVolumeValueChangeEventCallback( lv_event_t * e) {
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    int volume = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");
    ESP_BROOKESIA_CHECK_NULL_GOTO(app->_audioSystemVolumeSlider, end, "Invalid system volume slider");

    volume = lv_slider_get_value(app->_audioSystemVolumeSlider);
    if (volume != app->_nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME]) {
        if ((bsp_extra_audio_system_volume_set(volume) != ESP_OK) &&
            (bsp_extra_audio_system_volume_get() != volume)) {
            ESP_LOGE(TAG, "Set system sound volume failed");
            lv_slider_set_value(app->_audioSystemVolumeSlider, app->_nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME], LV_ANIM_OFF);
            goto end;
        }

        app->_nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME] = volume;
        app->setNvsParam(NVS_KEY_SYSTEM_AUDIO_VOLUME, volume);
    }

    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        bsp_extra_audio_play_system_notification();
    }

end:
    return;
}

void AppSettings::onSliderPanelLightSwitchValueChangeEventCallback( lv_event_t * e) {
    brightness = lv_slider_get_value(ui_SliderPanelScreenSettingLightSwitch1);

    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (brightness != app->_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS]) {
        // if ((bsp_display_brightness_set(brightness) != ESP_OK) && (bsp_display_brightness_get() != brightness)) {
        if (bsp_display_brightness_set(brightness) != ESP_OK) {
            ESP_LOGE(TAG, "Set brightness failed");
            lv_slider_set_value(ui_SliderPanelScreenSettingLightSwitch1, app->_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS], LV_ANIM_OFF);
            return;
        }
        app->_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS] = brightness;
        app->setNvsParam(NVS_KEY_DISPLAY_BRIGHTNESS, brightness);
        app->applyDisplayIdleSettings();
    }

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingAdaptiveBrightnessValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    enabled = (lv_obj_get_state(app->_displayAdaptiveBrightnessSwitch) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_DISPLAY_ADAPTIVE] = enabled;
    app->setNvsParam(NVS_KEY_DISPLAY_ADAPTIVE, enabled ? 1 : 0);
    app->applyDisplayIdleSettings();

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingScreensaverValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    enabled = (lv_obj_get_state(app->_displayScreensaverSwitch) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_DISPLAY_SCREENSAVER] = enabled;
    app->setNvsParam(NVS_KEY_DISPLAY_SCREENSAVER, enabled ? 1 : 0);
    app->applyDisplayIdleSettings();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingTimeoffIntervalValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    uint16_t selected_index = 0;
    int32_t selected_value = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    selected_index = lv_dropdown_get_selected(app->_displayTimeoffDropdown);
    selected_value = getDropdownValueForIndex(kDisplayTimeoffOptionsSec,
                                              sizeof(kDisplayTimeoffOptionsSec) / sizeof(kDisplayTimeoffOptionsSec[0]),
                                              selected_index);
    app->_nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF] = selected_value;
    app->setNvsParam(NVS_KEY_DISPLAY_TIMEOFF, selected_value);
    app->applyDisplayIdleSettings();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingSleepIntervalValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    uint16_t selected_index = 0;
    int32_t selected_value = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    selected_index = lv_dropdown_get_selected(app->_displaySleepDropdown);
    selected_value = getDropdownValueForIndex(kDisplaySleepOptionsSec,
                                              sizeof(kDisplaySleepOptionsSec) / sizeof(kDisplaySleepOptionsSec[0]),
                                              selected_index);
    app->_nvs_param_map[NVS_KEY_DISPLAY_SLEEP] = selected_value;
    app->setNvsParam(NVS_KEY_DISPLAY_SLEEP, selected_value);
    app->applyDisplayIdleSettings();

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingAutoTimezoneValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    enabled = (lv_obj_get_state(app->_displayAutoTimezoneSwitch) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO] = enabled;
    app->setNvsParam(NVS_KEY_DISPLAY_TZ_AUTO, enabled ? 1 : 0);

    if (enabled) {
        app->_hasAutoDetectedTimezone = false;
        app->_autoTimezoneStatus = "Auto timezone enabled";
        app->_autoTimezoneRefreshPending = true;
        if (xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_CONNECTED) {
            app->syncAutoTimezoneFromInternet();
            app->_autoTimezoneRefreshPending = false;
            app_sntp_init();
        }
    } else {
        app->_autoTimezoneRefreshPending = false;
        app->_hasAutoDetectedTimezone = false;
        app->_autoTimezoneStatus.clear();
        app->applyManualTimezonePreference();
        app_sntp_init();
    }

    app->refreshTimezoneUi();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingTimezoneValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    uint16_t selected_index = 0;
    int32_t selected_offset_minutes = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    selected_index = lv_dropdown_get_selected(app->_displayTimezoneDropdown);
    selected_offset_minutes = getTimezoneOptionForIndex(selected_index).offset_minutes;
    app->_nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE] = selected_offset_minutes;
    app->setNvsParam(NVS_KEY_DISPLAY_TIMEZONE, selected_offset_minutes);

    if (!app->_nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO]) {
        app->applyManualTimezonePreference();
        app_sntp_init();
    }

    app->refreshTimezoneUi();

end:
    return;
}
 
