/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cmath>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <ctime>
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
#include "esp_core_dump.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "esp_hosted.h"
#include "esp_hosted_misc.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "cJSON.h"
#include "nvs.h"
#if APP_SETTINGS_FEATURE_BLUETOOTH_MENU && CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#endif

#include "ui/ui.h"
#include "Setting.hpp"
#include "wifi/SettingWifiPrivate.hpp"
#include "app_sntp.h"
#include "battery_history_service.h"
#include "hardware_history_service.h"
#include "ImuDriver.hpp"
#include "ImuService.hpp"
#include "JcBoardPinManager.hpp"
#include "joypad_runtime.h"
#include "joypad_transport.h"
#include "../lora_mesh/LoRaMeshStorage.hpp"
#include "../lora_mesh/LoRaMesh.hpp"
#include "../lora_mesh/LoRaMeshTypes.hpp"
#include "LoRaPinProfile.hpp"
#include "lvgl_input_helper.h"
#include "neopixel_runtime.h"
#include "storage_access.h"
#include "system_ui_service.h"

#include "esp_brookesia_versions.h"

extern "C" bool __attribute__((weak)) jc_security_handle_app_launch_request(int app_id, const char *app_name);

#if CONFIG_JC4880_FEATURE_WIFI
#define APP_SETTINGS_FEATURE_WIFI 1
#else
#define APP_SETTINGS_FEATURE_WIFI 0
#endif

#if CONFIG_JC4880_FEATURE_LEGACY_BLE_MENU
#define APP_SETTINGS_FEATURE_BLUETOOTH_MENU 1
#else
#define APP_SETTINGS_FEATURE_BLUETOOTH_MENU 0
#endif

#if APP_SETTINGS_FEATURE_BLUETOOTH_MENU && CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
#define APP_SETTINGS_FEATURE_LEGACY_BLUETOOTH_RUNTIME 1
#else
#define APP_SETTINGS_FEATURE_LEGACY_BLUETOOTH_RUNTIME 0
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

#if CONFIG_JC4880_FEATURE_IMU
#define APP_SETTINGS_FEATURE_IMU 1
#else
#define APP_SETTINGS_FEATURE_IMU 0
#endif

#define HOME_REFRESH_TASK_STACK_SIZE    (1024 * 8)
#define HOME_REFRESH_TASK_PRIORITY      (1)
#define HOME_REFRESH_TASK_PERIOD_MS     (2000)
#define JOYPAD_BLE_LIVE_REFRESH_MS      (33)
#define IMU_LIVE_REFRESH_MS             (150)

#define FIRMWARE_UPDATE_TASK_STACK_SIZE  (1024 * 10)
#define FIRMWARE_UPDATE_TASK_PRIORITY    (4)

#define SCREEN_BRIGHTNESS_MIN           (20)
#define SCREEN_BRIGHTNESS_MAX           (BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX)
#define NEOPIXEL_BRIGHTNESS_MIN         (0)
#define NEOPIXEL_BRIGHTNESS_MAX         (100)

#define SPEAKER_VOLUME_MIN              (0)
#define SPEAKER_VOLUME_MAX              (100)

#define NVS_STORAGE_NAMESPACE           "storage"
#define FACTORY_RESET_STATUS_RESTARTING "Factory reset complete. Restarting device..."

static constexpr const char *kCrashReportLocalPath = BSP_SPIFFS_MOUNT_POINT "/last_crash_report.txt";
static constexpr const char *kCrashReportPendingPath = BSP_SPIFFS_MOUNT_POINT "/pending_crash_report.txt";
#define NVS_KEY_BLE_ENABLE              "ble_en"
#define NVS_KEY_BLE_DEVICE_NAME         "ble_name"
#define NVS_KEY_ZIGBEE_ENABLE           "zb_en"
#define NVS_KEY_ZIGBEE_CHANNEL          "zb_ch"
#define NVS_KEY_ZIGBEE_PERMIT_JOIN      "zb_join"
#define NVS_KEY_ZIGBEE_DEVICE_NAME      "zb_name"
#define NVS_KEY_AUDIO_VOLUME            "volume"
#define NVS_KEY_SYSTEM_AUDIO_VOLUME     "sys_volume"
#define NVS_KEY_AUDIO_TAP_SOUND         "tap_sound"
#define NVS_KEY_AUDIO_HAPTIC_FEEDBACK   "haptic_fb"
#define NVS_KEY_AUDIO_HAPTIC_GPIO       "haptic_gpio"
#define NVS_KEY_AUDIO_HAPTIC_LEVEL      "haptic_lvl"
#define NVS_KEY_DISPLAY_BRIGHTNESS      "brightness"
#define NVS_KEY_NEOPIXEL_POWER          "neo_en"
#define NVS_KEY_NEOPIXEL_GPIO           "neo_gpio"
#define NVS_KEY_NEOPIXEL_BRIGHTNESS     "neo_bri"
#define NVS_KEY_NEOPIXEL_PALETTE        "neo_pal"
#define NVS_KEY_NEOPIXEL_EFFECT         "neo_fx"
#define NVS_KEY_DISPLAY_ADAPTIVE        "disp_adapt"
#define NVS_KEY_DISPLAY_SCREENSAVER     "disp_saver"
#define NVS_KEY_DISPLAY_TIMEOFF_IN_GAME "disp_off_g"
#define NVS_KEY_DISPLAY_TIMEOFF         "disp_off_sec"
#define NVS_KEY_DISPLAY_SLEEP           "disp_sleep"
#define NVS_KEY_DISPLAY_ORIENTATION     "disp_rot"
#define NVS_KEY_DISPLAY_ORIENTATION_PENDING "disp_rot_pend"
#define NVS_KEY_DISPLAY_ORIENTATION_PREVIOUS "disp_rot_prev"
#define NVS_KEY_DISPLAY_ORIENTATION_STATE "disp_rot_state"
#define NVS_KEY_DISPLAY_AUTOROTATE      "disp_auto_rot"
#define NVS_KEY_DISPLAY_AUTOROTATE_IMU  "disp_auto_imu"
#define NVS_KEY_DISPLAY_AUTOROTATE_SDA  "disp_auto_sda"
#define NVS_KEY_DISPLAY_AUTOROTATE_SCL  "disp_auto_scl"
#define NVS_KEY_DISPLAY_TIMEZONE        "disp_tz_min"
#define NVS_KEY_DISPLAY_TZ_AUTO         "disp_tz_auto"
#define NVS_KEY_OTA_AUTO_UPDATE         "ota_auto"
#define NVS_KEY_OTA_RESCHEDULE_UNTIL    "ota_res_at"
#define NVS_KEY_OTA_RESCHEDULE_VERSION  "ota_res_ver"
#define NVS_KEY_OTA_PENDING_VERSION     "ota_ver"
#define NVS_KEY_OTA_PENDING_NOTES       "ota_notes"
#define NVS_KEY_OTA_PENDING_SHOW        "ota_show"

#define UI_MAIN_ITEM_LEFT_OFFSET        (20)
#define UI_WIFI_LIST_UP_PAD             (20)
#define UI_WIFI_LIST_DOWN_PAD           (20)
#define UI_WIFI_LIST_H_PERCENT          (75)
#define UI_WIFI_KEYBOARD_H_PERCENT      (30)

#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_12

extern "C" void jc_ui_tap_sound_set_enabled(bool enabled);
extern "C" void jc_ui_haptic_feedback_set_enabled(bool enabled);
extern "C" void jc_ui_haptic_feedback_set_gpio(int gpio);
extern "C" void jc_ui_haptic_feedback_set_level(int level);
extern "C" void jc_ui_haptic_feedback_test(void);

static constexpr int32_t kHapticLevelOptions[] = {0, 1, 2, 3};
 
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
static constexpr int kStatusBarOtaIconId = 0x4F5441;
static constexpr uint64_t kOtaAvailabilityInitialDelayUs = 20ULL * 1000000ULL;
static constexpr uint64_t kOtaAvailabilitySuccessIntervalUs = 6ULL * 60ULL * 60ULL * 1000000ULL;
static constexpr uint64_t kOtaAvailabilityRetryIntervalUs = 15ULL * 60ULL * 1000000ULL;
static constexpr int64_t kValidUnixTimeFloor = 1700000000LL;
static constexpr int32_t kOtaRescheduleHourOptions[] = {0, 1, 2, 4, 8, 12, 24};
static constexpr int32_t kOtaRescheduleMinuteOptions[] = {0, 15, 30, 45};
static constexpr int32_t kOtaRescheduleMinuteFutureOnlyOptions[] = {15, 30, 45};

static string formatDelayMinutes(int32_t total_minutes)
{
    const int32_t hours = total_minutes / 60;
    const int32_t minutes = total_minutes % 60;
    std::ostringstream stream;
    if (hours > 0) {
        stream << hours << "h";
        if (minutes > 0) {
            stream << " ";
        }
    }
    if ((minutes > 0) || (hours == 0)) {
        stream << minutes << "m";
    }
    return stream.str();
}

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
static constexpr int kQuickAccessActionApplyAirplaneRadioPreferences = 0x41525031;
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
constexpr const char *kLoraModuleOptionsText = "E22-400M22S (SPI)\nE22-400T22S (UART)\nE220-400T22D (UART)";
constexpr const char *kLoraGpioOptionsText = "Disabled\nGPIO 29\nGPIO 30\nGPIO 31\nGPIO 33\nGPIO 34\nGPIO 35\nGPIO 50\nGPIO 51\nGPIO 52";
constexpr int32_t kLoraGpioOptions[] = {-1, 29, 30, 31, 33, 34, 35, 50, 51, 52};

enum class LoraPinRole : uint8_t {
    SpiMiso = 0,
    SpiMosi,
    SpiSck,
    SpiNss,
    Busy,
    Dio1,
    Reset,
    TxEnable,
    RxEnable,
    UartTx,
    UartRx,
    Mode0,
    Mode1,
    Aux,
    Count,
};

constexpr size_t kLoraPinRoleCount = static_cast<size_t>(LoraPinRole::Count);

static jc4880::lora_mesh::RadioModule lora_radio_module_from_dropdown(uint16_t selected_index)
{
    switch (selected_index) {
    case 1:
        return jc4880::lora_mesh::RadioModule::E22_400T22S;
    case 2:
        return jc4880::lora_mesh::RadioModule::E220_400T22D;
    default:
        return jc4880::lora_mesh::RadioModule::E22_400M22S;
    }
}

static uint16_t lora_dropdown_index_from_radio_module(jc4880::lora_mesh::RadioModule module)
{
    switch (module) {
    case jc4880::lora_mesh::RadioModule::E22_400T22S:
        return 1;
    case jc4880::lora_mesh::RadioModule::E220_400T22D:
        return 2;
    case jc4880::lora_mesh::RadioModule::E22_400M22S:
    default:
        return 0;
    }
}

static bool lora_module_uses_role(jc4880::lora_mesh::RadioModule module, LoraPinRole role)
{
    switch (module) {
    case jc4880::lora_mesh::RadioModule::E22_400M22S:
        return static_cast<size_t>(role) <= static_cast<size_t>(LoraPinRole::RxEnable);
    case jc4880::lora_mesh::RadioModule::E22_400T22S:
    case jc4880::lora_mesh::RadioModule::E220_400T22D:
        return static_cast<size_t>(role) >= static_cast<size_t>(LoraPinRole::UartTx);
    default:
        return false;
    }
}

static const char *lora_pin_role_label(LoraPinRole role)
{
    switch (role) {
    case LoraPinRole::SpiMiso: return "SPI MISO";
    case LoraPinRole::SpiMosi: return "SPI MOSI";
    case LoraPinRole::SpiSck: return "SPI SCK";
    case LoraPinRole::SpiNss: return "SPI NSS";
    case LoraPinRole::Busy: return "BUSY";
    case LoraPinRole::Dio1: return "DIO1";
    case LoraPinRole::Reset: return "RESET";
    case LoraPinRole::TxEnable: return "TX Enable";
    case LoraPinRole::RxEnable: return "RX Enable";
    case LoraPinRole::UartTx: return "UART TX";
    case LoraPinRole::UartRx: return "UART RX";
    case LoraPinRole::Mode0: return "MODE0";
    case LoraPinRole::Mode1: return "MODE1";
    case LoraPinRole::Aux: return "AUX";
    case LoraPinRole::Count: break;
    }
    return "GPIO";
}

static int8_t lora_pin_value_for_role(const jc4880::lora_mesh::MeshSettings &settings, LoraPinRole role)
{
    switch (role) {
    case LoraPinRole::SpiMiso: return settings.spi_miso_gpio;
    case LoraPinRole::SpiMosi: return settings.spi_mosi_gpio;
    case LoraPinRole::SpiSck: return settings.spi_sck_gpio;
    case LoraPinRole::SpiNss: return settings.spi_nss_gpio;
    case LoraPinRole::Busy: return settings.busy_gpio;
    case LoraPinRole::Dio1: return settings.dio1_gpio;
    case LoraPinRole::Reset: return settings.nrst_gpio;
    case LoraPinRole::TxEnable: return settings.txen_gpio;
    case LoraPinRole::RxEnable: return settings.rxen_gpio;
    case LoraPinRole::UartTx: return settings.uart_tx_gpio;
    case LoraPinRole::UartRx: return settings.uart_rx_gpio;
    case LoraPinRole::Mode0: return settings.mode0_gpio;
    case LoraPinRole::Mode1: return settings.mode1_gpio;
    case LoraPinRole::Aux: return settings.aux_gpio;
    case LoraPinRole::Count: break;
    }
    return -1;
}

static void lora_set_pin_value_for_role(jc4880::lora_mesh::MeshSettings &settings, LoraPinRole role, int8_t value)
{
    switch (role) {
    case LoraPinRole::SpiMiso: settings.spi_miso_gpio = value; break;
    case LoraPinRole::SpiMosi: settings.spi_mosi_gpio = value; break;
    case LoraPinRole::SpiSck: settings.spi_sck_gpio = value; break;
    case LoraPinRole::SpiNss: settings.spi_nss_gpio = value; break;
    case LoraPinRole::Busy: settings.busy_gpio = value; break;
    case LoraPinRole::Dio1: settings.dio1_gpio = value; break;
    case LoraPinRole::Reset: settings.nrst_gpio = value; break;
    case LoraPinRole::TxEnable: settings.txen_gpio = value; break;
    case LoraPinRole::RxEnable: settings.rxen_gpio = value; break;
    case LoraPinRole::UartTx: settings.uart_tx_gpio = value; break;
    case LoraPinRole::UartRx: settings.uart_rx_gpio = value; break;
    case LoraPinRole::Mode0: settings.mode0_gpio = value; break;
    case LoraPinRole::Mode1: settings.mode1_gpio = value; break;
    case LoraPinRole::Aux: settings.aux_gpio = value; break;
    case LoraPinRole::Count: break;
    }
}

static uint16_t lora_gpio_choice_index(int8_t value)
{
    for (size_t index = 0; index < (sizeof(kLoraGpioOptions) / sizeof(kLoraGpioOptions[0])); ++index) {
        if (kLoraGpioOptions[index] == value) {
            return static_cast<uint16_t>(index);
        }
    }
    return 0;
}

static int8_t lora_gpio_choice_value(uint16_t index)
{
    if (index >= (sizeof(kLoraGpioOptions) / sizeof(kLoraGpioOptions[0]))) {
        return static_cast<int8_t>(kLoraGpioOptions[0]);
    }
    return static_cast<int8_t>(kLoraGpioOptions[index]);
}

static bool is_local_controller_backend_active()
{
    jc4880_joypad_config_t config = {};
    if (!jc4880_joypad_get_config(&config)) {
        return false;
    }
    return config.backend == JC4880_JOYPAD_BACKEND_MANUAL;
}

static bool is_imu_enabled()
{
    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);
    return config.enabled && (config.model != jc4880::imu::ImuModel::IMU_NONE);
}

static uint16_t imu_pin_choice_index(int32_t gpio)
{
    static constexpr int32_t kImuPinChoices[] = {-1, 28, 29, 30, 31, 32, 33, 34, 35, 49, 50, 51, 52};
    for (size_t index = 0; index < (sizeof(kImuPinChoices) / sizeof(kImuPinChoices[0])); ++index) {
        if (kImuPinChoices[index] == gpio) {
            return static_cast<uint16_t>(index);
        }
    }
    return 0;
}

static int32_t imu_pin_choice_value(uint16_t index)
{
    static constexpr int32_t kImuPinChoices[] = {-1, 28, 29, 30, 31, 32, 33, 34, 35, 49, 50, 51, 52};
    if (index >= (sizeof(kImuPinChoices) / sizeof(kImuPinChoices[0]))) {
        return kImuPinChoices[0];
    }
    return kImuPinChoices[index];
}

static int32_t sanitizeImuAssignableGpio(int32_t gpio)
{
    if (gpio < 0) {
        return -1;
    }
    if (!jc4880::board_pins::is_jp1_assignable_gpio(gpio)) {
        return -1;
    }
    return gpio;
}

static int32_t imu_model_from_dropdown(uint16_t index)
{
    size_t count = 0;
    const jc4880::imu::ImuModelInfo *catalog = jc4880::imu::get_imu_model_catalog(&count);
    if ((catalog == nullptr) || (index >= count)) {
        return static_cast<int32_t>(jc4880::imu::ImuModel::IMU_NONE);
    }
    return static_cast<int32_t>(catalog[index].model);
}

static uint16_t imu_dropdown_index_from_model(jc4880::imu::ImuModel model)
{
    size_t count = 0;
    const jc4880::imu::ImuModelInfo *catalog = jc4880::imu::get_imu_model_catalog(&count);
    if (catalog == nullptr) {
        return 0;
    }
    for (size_t index = 0; index < count; ++index) {
        if (catalog[index].model == model) {
            return static_cast<uint16_t>(index);
        }
    }
    return 0;
}

static LoRaMeshApp *find_installed_lora_mesh_app(ESP_Brookesia_Core *core)
{
    if (core == nullptr) {
        return nullptr;
    }

    for (int app_id = 0; app_id < 128; ++app_id) {
        ESP_Brookesia_CoreApp *app = core->getCoreManager().getInstalledApp(app_id);
        if ((app == nullptr) || (app->getName() == nullptr)) {
            continue;
        }
        if ((std::strcmp(app->getName(), "LoRa Mesh") == 0) ||
            (std::strcmp(app->getName(), "LoRa") == 0)) {
            return static_cast<LoRaMeshApp *>(app);
        }
    }

    return nullptr;
}

enum class BleRuntimeState : uint8_t {
    Disabled = 0,
    Starting,
    Advertising,
    Error,
};

BleRuntimeState s_bleRuntimeState = BleRuntimeState::Disabled;
bool s_bleScanInProgress = false;
std::string s_bleScanStatus = "BLE discovery is idle.";
std::vector<BleScanResult> s_bleScanResults;

#if APP_SETTINGS_FEATURE_LEGACY_BLUETOOTH_RUNTIME
constexpr int32_t kBleScanDurationMs = 8000;
constexpr size_t kBleScanResultLimit = 8;
constexpr int32_t kBleStartupTimeoutMs = 8000;
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
bool s_bleResumeAdvertisingAfterScan = false;
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

    if (enabled) {
        jc4880_joypad_config_t joypad_config = {};
        if (jc4880_joypad_get_config(&joypad_config) &&
            (joypad_config.backend == JC4880_JOYPAD_BACKEND_BLE)) {
            s_bleDesiredEnabled = false;
            bleSetStatusLocked(BleRuntimeState::Error,
                               "Legacy hosted BLE is unavailable while Joypad BLE uses the ESP32-C6 radio.");
            bleUnlock();
            return ESP_ERR_NOT_SUPPORTED;
        }
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

static bool bluetoothMenuDelegatesToJoypadBle()
{
    jc4880_joypad_config_t joypad_config = {};
    return jc4880_joypad_get_config(&joypad_config) &&
           (joypad_config.backend == JC4880_JOYPAD_BACKEND_BLE);
}

static std::string bleStatusText(bool preference_enabled)
{
    if (bluetoothMenuDelegatesToJoypadBle()) {
        return "Joypad BLE is managed from the Joypad menu through the ESP32-C6 radio. The legacy Bluetooth screen is disabled.";
    }

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

#else

static bool bluetoothMenuDelegatesToJoypadBle()
{
    jc4880_joypad_config_t joypad_config = {};
    return jc4880_joypad_get_config(&joypad_config) &&
           (joypad_config.backend == JC4880_JOYPAD_BACKEND_BLE);
}

static std::string bleStatusText(bool preference_enabled)
{
    if (bluetoothMenuDelegatesToJoypadBle()) {
        return "Joypad BLE is managed from the Joypad menu through the ESP32-C6 radio. The legacy Bluetooth screen is disabled.";
    }

    if (!preference_enabled) {
        return kBleDisabledMessage;
    }

    return std::string(kBleUnsupportedMessage) + " Root P4 Bluetooth is disabled in this firmware build.";
}

[[maybe_unused]] static void bleCheckStartupTimeout(void)
{
}

static esp_err_t bleSetEnabled(bool enabled)
{
    s_bleRuntimeState = enabled ? BleRuntimeState::Error : BleRuntimeState::Disabled;
    return enabled ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
}

static esp_err_t bleStartScan(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

static void bleCancelScan(void)
{
}

static esp_err_t bleUpdateConfiguredName(const std::string &name)
{
    (void)name;
    return ESP_OK;
}

#endif

} // namespace

static uint8_t base_mac_addr[6] = {0};
static char mac_str[18] = {0};

static int brightness;
static constexpr int32_t kNeopixelGpioOptions[] = {-1, 28, 32, 49};
static constexpr char kNeopixelGpioOptionsText[] = "Disabled\nGPIO 28\nGPIO 32\nGPIO 49";
static constexpr int32_t kNeopixelPaletteOptions[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static constexpr char kNeopixelPaletteOptionsText[] = "Ruby\nAmber\nSunflower\nLime\nMint\nCyan\nAzure\nViolet\nPink\nWhite\nTangerine\nAqua";
static constexpr int32_t kNeopixelEffectOptions[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
static constexpr char kNeopixelEffectOptionsText[] = "Solid\nBlink\nBreath\nColor Wipe\nTheater Chase\nRainbow Cycle\nScanner\nTwinkle\nTwinkle Fade\nMeteor\nPulse Wave\nDual Spin\nSparkle\nBounce\nFirefly\nComet Tail\nCandy Cane\nPolice\nSunset Drift\nAurora\nRipple";
static constexpr const char *kNeopixelPaletteLabels[] = {
    "Ruby", "Amber", "Sunflower", "Lime", "Mint", "Cyan", "Azure", "Violet", "Pink", "White", "Tangerine", "Aqua"
};
static constexpr const char *kNeopixelEffectLabels[] = {
    "Solid", "Blink", "Breath", "Color Wipe", "Theater Chase", "Rainbow Cycle", "Scanner", "Twinkle", "Twinkle Fade", "Meteor",
    "Pulse Wave", "Dual Spin", "Sparkle", "Bounce", "Firefly", "Comet Tail", "Candy Cane", "Police", "Sunset Drift", "Aurora", "Ripple"
};

static constexpr int32_t kDisplayTimeoffOptionsSec[] = {0, 15, 30, 60, 120, 300};
static constexpr char kDisplayTimeoffOptionsText[] = "Off\n15 sec\n30 sec\n1 min\n2 min\n5 min";
static constexpr int32_t kDisplaySleepOptionsSec[] = {0, 30, 60, 120, 300, 600, 1800};
static constexpr char kDisplaySleepOptionsText[] = "Off\n30 sec\n1 min\n2 min\n5 min\n10 min\n30 min";
static constexpr int32_t kDisplayOrientationOptionsDeg[] = {0, 90, 180, 270};
static constexpr char kDisplayOrientationOptionsText[] = "0\n90\n180\n270";
static constexpr int32_t kDisplayOrientationPreviewSeconds = 30;
static constexpr char kDisplayOrientationPreviewInitialText[] =
    "If screen looks right hit OK, otherwise screen will return to previous settings in";
static constexpr int32_t kDisplayAutorotateAxisOptions[] = {0, 1, 2};
static constexpr char kDisplayAutorotateAxisOptionsText[] = "X\nY\nZ";
static constexpr int32_t kImuBusOptions[] = {
    static_cast<int32_t>(jc4880::imu::ImuBusType::I2C),
    static_cast<int32_t>(jc4880::imu::ImuBusType::SPI),
    static_cast<int32_t>(jc4880::imu::ImuBusType::UART),
};
static constexpr char kImuBusOptionsText[] = "I2C\nSPI\nUART";
static constexpr int32_t kImuPinOptions[] = {-1, 28, 29, 30, 31, 32, 33, 34, 35, 49, 50, 51, 52};
static constexpr char kImuPinOptionsText[] = "Disabled\nGPIO 28\nGPIO 29\nGPIO 30\nGPIO 31\nGPIO 32\nGPIO 33\nGPIO 34\nGPIO 35\nGPIO 49\nGPIO 50\nGPIO 51\nGPIO 52";
static constexpr int32_t kImuAddressOptions[] = {0x68, 0x69};
static constexpr char kImuAddressOptionsText[] = "0x68\n0x69";
static constexpr char kImuModelOptionsText[] =
    "IMU_NONE\nBNO085\nBNO080\nBNO055\nICM20948\nMPU9250\nMPU9255\nGY91_MPU9250_BMP280\nMPU6050\nMPU6500\nMPU6886\nBMI160\nBMI270\nBHI260AP\nLSM6DS3\nLSM6DSL\nLSM6DSOX\nLSM9DS1\nLSM9DS0\nLSM6DS3TRC_LIS3MDL\nBMI270_BMM150\nHMC5883L\nQMC5883L\nLIS3MDL\nBMM150\nADXL345\nADIS16500\nADIS16505\nHW579_COMBO";
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

static int32_t sanitizeDisplayOrientationDegrees(int32_t orientation_degrees)
{
    switch (orientation_degrees) {
    case 0:
    case 90:
    case 180:
    case 270:
        return orientation_degrees;
    default:
        return 0;
    }
}

static lv_disp_rotation_t displayOrientationDegreesToLvRotation(int32_t orientation_degrees)
{
    switch (sanitizeDisplayOrientationDegrees(orientation_degrees)) {
    case 90:
        return static_cast<lv_disp_rotation_t>(LV_DISP_ROT_90);
    case 180:
        return static_cast<lv_disp_rotation_t>(LV_DISP_ROT_180);
    case 270:
        return static_cast<lv_disp_rotation_t>(LV_DISP_ROT_270);
    case 0:
    default:
        return static_cast<lv_disp_rotation_t>(LV_DISP_ROT_NONE);
    }
}

static bool applyDisplayOrientationLive(int32_t orientation_degrees)
{
    lv_display_t *display = lv_disp_get_default();
    if (display == nullptr) {
        return false;
    }

    if (!bsp_display_lock(0)) {
        return false;
    }

    bsp_display_rotate(display, displayOrientationDegreesToLvRotation(orientation_degrees));
    if (lv_obj_ready(lv_disp_get_scr_act(display))) {
        lv_obj_invalidate(lv_disp_get_scr_act(display));
    }
    if (lv_obj_ready(lv_layer_top())) {
        lv_obj_invalidate(lv_layer_top());
    }
    if (lv_obj_ready(lv_layer_sys())) {
        lv_obj_invalidate(lv_layer_sys());
    }
    lv_refr_now(display);
    lv_refr_now(display);
    bsp_display_unlock();
    return true;
}

static int32_t oppositeDisplayOrientationDegrees(int32_t orientation_degrees)
{
    switch (sanitizeDisplayOrientationDegrees(orientation_degrees)) {
    case 0:
        return 180;
    case 90:
        return 270;
    case 180:
        return 0;
    case 270:
        return 90;
    default:
        return 180;
    }
}

static int32_t sanitizeDisplayAutorotateAxis(int32_t axis)
{
    switch (axis) {
    case 0:
    case 1:
    case 2:
        return axis;
    default:
        return 0;
    }
}

static int32_t sanitizeAssignableUserGpio(int32_t gpio)
{
    if (jc4880::lora_mesh::pin_profile::is_reserved_gpio(gpio)) {
        return -1;
    }

    for (size_t index = 0; index < (sizeof(kNeopixelGpioOptions) / sizeof(kNeopixelGpioOptions[0])); ++index) {
        if (kNeopixelGpioOptions[index] == gpio) {
            return gpio;
        }
    }

    return -1;
}

static int32_t sanitizeHapticGpio(int32_t gpio)
{
    const int32_t sanitized = sanitizeAssignableUserGpio(gpio);
    return sanitized >= 0 ? sanitized : 49;
}

static int32_t sanitizeNeopixelGpio(int32_t gpio)
{
    return sanitizeAssignableUserGpio(gpio);
}

static int32_t sanitizeDisplayAutorotateGpio(int32_t gpio)
{
    return sanitizeAssignableUserGpio(gpio);
}

static int32_t loadPendingDisplayOrientationPreviewDegrees(void)
{
    nvs_handle_t nvs_handle;
    if (nvs_open(NVS_STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle) != ESP_OK) {
        return -1;
    }

    int32_t preview_state = 0;
    int32_t orientation_degrees = -1;
    if ((nvs_get_i32(nvs_handle, NVS_KEY_DISPLAY_ORIENTATION_STATE, &preview_state) == ESP_OK) && (preview_state != 0)) {
        if (nvs_get_i32(nvs_handle, NVS_KEY_DISPLAY_ORIENTATION_PREVIOUS, &orientation_degrees) == ESP_OK) {
            orientation_degrees = sanitizeDisplayOrientationDegrees(orientation_degrees);
        } else {
            orientation_degrees = -1;
        }
        if (orientation_degrees >= 0) {
            nvs_set_i32(nvs_handle, NVS_KEY_DISPLAY_ORIENTATION, orientation_degrees);
            nvs_set_i32(nvs_handle, NVS_KEY_DISPLAY_ORIENTATION_PENDING, orientation_degrees);
            nvs_set_i32(nvs_handle, NVS_KEY_DISPLAY_ORIENTATION_PREVIOUS, orientation_degrees);
        }
        nvs_set_i32(nvs_handle, NVS_KEY_DISPLAY_ORIENTATION_STATE, 0);
        nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return orientation_degrees;
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
    _wifiApPanel(nullptr),
    _wifiApSwitch(nullptr),
    _wifiApStatusLabel(nullptr),
    _wifiApSsidTextArea(nullptr),
    _wifiApPasswordTextArea(nullptr),
    _wifiApSaveButton(nullptr),
    _wifiApKeyboard(nullptr),
    _wifiApKeyboardTarget(nullptr),
    _savedWifiListExpanded(false),
    _suppressDisconnectRecovery(false),
    _aboutWifiValueLabel(nullptr),
    _displayAdaptiveBrightnessSwitch(nullptr),
    _displayNeopixelPowerSwitch(nullptr),
    _displayNeopixelGpioDropdown(nullptr),
    _displayNeopixelPaletteDropdown(nullptr),
    _displayNeopixelEffectDropdown(nullptr),
    _displayNeopixelBrightnessSlider(nullptr),
    _displayNeopixelInfoLabel(nullptr),
    _displayScreensaverSwitch(nullptr),
    _displayTimeoffInGameSwitch(nullptr),
    _displayTimeoffDropdown(nullptr),
    _displaySleepDropdown(nullptr),
    _displayOrientationDropdown(nullptr),
    _displayOrientationPreviewMsgbox(nullptr),
    _displayOrientationPreviewLabel(nullptr),
    _displayOrientationPreviewSpinner(nullptr),
    _displayOrientationPreviewCountdownLabel(nullptr),
    _displayOrientationPreviewTimer(nullptr),
    _imuLiveTimer(nullptr),
    _loraSelfCheckStatusTimer(nullptr),
    _displayOrientationPreviewPrevious(0),
    _displayOrientationPreviewPending(0),
    _displayOrientationPreviewSecondsRemaining(0),
    _displayOrientationPreviewResolving(false),
    _displayAutorotateAppliedOrientation(0),
    _displayAutorotateHasAppliedOrientation(false),
    _displayAutorotateSwitch(nullptr),
    _displayAutorotateImuDropdown(nullptr),
    _displayAutorotateInfoLabel(nullptr),
    _displayAutoTimezoneSwitch(nullptr),
    _displayTimezoneDropdown(nullptr),
    _displayTimezoneInfoLabel(nullptr),
    _audioMediaVolumeSlider(nullptr),
    _audioSystemVolumeSlider(nullptr),
    _audioTapSoundSwitch(nullptr),
    _audioHapticFeedbackSwitch(nullptr),
    _bluetoothMenuItem(nullptr),
    _joypadMenuItem(nullptr),
    _imuMenuItem(nullptr),
    _loraMenuItem(nullptr),
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
    _joypadScreen(nullptr),
    _joypadBleScreen(nullptr),
    _joypadLocalScreen(nullptr),
    _imuScreen(nullptr),
    _loraScreen(nullptr),
    _joypadBleMenuItem(nullptr),
    _joypadLocalMenuItem(nullptr),
    _joypadBleActiveSwitch(nullptr),
    _joypadManualActiveSwitch(nullptr),
    _joypadBleEnableSwitch(nullptr),
    _joypadBleDiscoverySwitch(nullptr),
    _joypadBleDeviceDropdown(nullptr),
    _joypadBleStatusLabel(nullptr),
    _joypadBleCalibrationInfoLabel(nullptr),
    _joypadBleCalibrationButton(nullptr),
    _joypadBleCalibrationButtonLabel(nullptr),
    _joypadBackendDropdown(nullptr),
    _joypadManualModeDropdown(nullptr),
    _joypadInfoLabel(nullptr),
    _joypadBleTriggerBars{},
    _joypadBleShoulderIndicators{},
    _joypadBleStickBases{},
    _joypadBleStickKnobs{},
    _joypadBleDpadIndicators{},
    _joypadBleFaceIndicators{},
    _joypadLocalTriggerBars{},
    _joypadLocalShoulderIndicators{},
    _joypadLocalStickBases{},
    _joypadLocalStickKnobs{},
    _joypadLocalDpadIndicators{},
    _joypadLocalFaceIndicators{},
    _joypadBlePreviewCenterAxes{},
    _joypadBlePreviewDeviceAddr{},
    _joypadBlePreviewCenterValid(false),
    _joypadBleRemapDropdowns{},
    _joypadManualSpiDropdowns{},
    _joypadManualResistiveDropdowns{},
    _joypadManualResistiveButtonDropdowns{},
    _joypadManualMcpDropdowns{},
    _joypadManualMcpButtonDropdowns{},
    _joypadLocalHapticGpioDropdown(nullptr),
    _joypadLocalHapticLevelDropdown(nullptr),
    _joypadLocalNeopixelPowerSwitch(nullptr),
    _joypadLocalNeopixelGpioDropdown(nullptr),
    _joypadLocalNeopixelPaletteDropdown(nullptr),
    _joypadLocalNeopixelEffectDropdown(nullptr),
    _joypadLocalNeopixelBrightnessSlider(nullptr),
    _joypadLocalNeopixelInfoLabel(nullptr),
    _joypadBleDeviceOptions(),
    _imuEnabledSwitch(nullptr),
    _imuModelDropdown(nullptr),
    _imuBusDropdown(nullptr),
    _imuPowerHintLabel(nullptr),
    _imuI2cSdaDropdown(nullptr),
    _imuI2cSclDropdown(nullptr),
    _imuI2cAddressDropdown(nullptr),
    _imuI2cAddressTextArea(nullptr),
    _imuIntDropdown(nullptr),
    _imuDrdyDropdown(nullptr),
    _imuLiveScene(nullptr),
    _imuHeadingArc(nullptr),
    _imuHeadingValueLabel(nullptr),
    _imuRollValueLabel(nullptr),
    _imuPitchValueLabel(nullptr),
    _imuYawValueLabel(nullptr),
    _imuMotionShadow(nullptr),
    _imuMotionDot(nullptr),
    _imuMotionTempLabel(nullptr),
    _imuLiveCaptionLabel(nullptr),
    _imuLiveStatusLabel(nullptr),
    _imuAccelValueLabel(nullptr),
    _imuGyroValueLabel(nullptr),
    _imuMagValueLabel(nullptr),
    _imuEnvValueLabel(nullptr),
    _imuBallPosX(21.0f),
    _imuBallPosY(21.0f),
    _imuBallPosZ(0.0f),
    _imuBallVelX(0.0f),
    _imuBallVelY(0.0f),
    _imuBallVelZ(0.0f),
    _imuBallPrevAccelX(0.0f),
    _imuBallPrevAccelY(0.0f),
    _imuBallPrevAccelZ(0.0f),
    _imuBallDynamicsInitialized(false),
    _imuSensorIndicatorDots{},
    _imuSensorIndicatorLabels{},
    _imuScanButton(nullptr),
    _imuTestButton(nullptr),
    _imuStatusLabel(nullptr),
    _imuInfoLabel(nullptr),
    _loraEnabledSwitch(nullptr),
    _loraModuleDropdown(nullptr),
    _loraDisplayNameTextArea(nullptr),
    _loraCommonChatTitleTextArea(nullptr),
    _loraFrequencyTextArea(nullptr),
    _loraSpreadingFactorTextArea(nullptr),
    _loraBandwidthTextArea(nullptr),
    _loraCodingRateTextArea(nullptr),
    _loraHopLimitTextArea(nullptr),
    _loraForwardingSwitch(nullptr),
    _loraEncryptionSwitch(nullptr),
    _loraSelfCheckButton(nullptr),
    _loraSelfCheckStatusLabel(nullptr),
    _loraInfoLabel(nullptr),
    _loraPinRows{},
    _loraPinDropdowns{},
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
    _firmwareAutoUpdateSwitch(nullptr),
    _firmwareCurrentVersionLabel(nullptr),
    _firmwareOtaSummaryLabel(nullptr),
    _firmwareOtaListContainer(nullptr),
    _firmwareOtaCheckOverlay(nullptr),
    _firmwareOtaCheckSpinner(nullptr),
    _firmwareOtaCheckStatusLabel(nullptr),
    _firmwareStatusLabel(nullptr),
    _firmwareProgressBar(nullptr),
    _firmwareProgressLabel(nullptr),
    _otaUpdateAvailableMsgbox(nullptr),
    _otaUpdateProgressOverlay(nullptr),
    _otaUpdateProgressStatusLabel(nullptr),
    _otaUpdateProgressBar(nullptr),
    _otaUpdateProgressLabel(nullptr),
    _otaUpdateProgressActionRow(nullptr),
    _otaUpdateProgressInstallButton(nullptr),
    _otaUpdateProgressRescheduleButton(nullptr),
    _otaUpdateProgressCancelButton(nullptr),
    _otaUpdateProgressCornerCloseButton(nullptr),
    _otaUpdateReschedulePanel(nullptr),
    _otaUpdateRescheduleHourDropdown(nullptr),
    _otaUpdateRescheduleMinuteDropdown(nullptr),
    _otaUpdateRescheduleApplyButton(nullptr),
    _otaUpdateProgressCloseButton(nullptr),
    _firmwareUpdateInProgress(false),
    _firmwareCancelRequested(false),
    _firmwareOtaCheckInProgress(false),
    _otaStatusIconInstalled(false),
    _otaUpdateAvailableThisBoot(false),
    _otaUpdatePromptDismissedThisBoot(false),
    _otaAutoUpdateAwaitingDecision(false),
    _otaAvailabilityCheckInProgress(false),
    _pendingOpenFirmwareScreen(false),
    _otaDeferredAutoUpdateUntilUs(0),
    _nextOtaAvailabilityCheckUs(0),
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
    stopLoRaSelfCheckStatusPolling();
    if (_hardwareFastHistoryScratch != nullptr) {
        free(_hardwareFastHistoryScratch);
        _hardwareFastHistoryScratch = nullptr;
    }
    if (_hardwareSlowHistoryScratch != nullptr) {
        free(_hardwareSlowHistoryScratch);
        _hardwareSlowHistoryScratch = nullptr;
    }
}

bool AppSettings::handleQuickAccessAction(int action_id)
{
    if (action_id != kQuickAccessActionApplyAirplaneRadioPreferences) {
        return ESP_Brookesia_PhoneApp::handleQuickAccessAction(action_id);
    }

    loadNvsParam();

#if APP_SETTINGS_FEATURE_WIFI
    if (applyWifiOperatingMode(true, "quick access airplane mode") != ESP_OK) {
        return false;
    }
#endif

#if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    const bool ble_enabled = _nvs_param_map[NVS_KEY_BLE_ENABLE] != 0;
    if (!ble_enabled) {
        bleCancelScan();
    }
    if (bleSetEnabled(ble_enabled) != ESP_OK) {
        _nvs_param_map[NVS_KEY_BLE_ENABLE] = 0;
        setNvsParam(NVS_KEY_BLE_ENABLE, 0);
    }
#endif

    updateUiByNvsParam();
    return true;
}

void AppSettings::initializeDefaultNvsParams(void)
{
    _nvs_param_map[NVS_KEY_WIFI_ENABLE] = false;
    _nvs_param_map[NVS_KEY_WIFI_AP_ENABLE] = false;
    _nvs_param_map[NVS_KEY_BLE_ENABLE] = false;
    _nvs_param_map[NVS_KEY_ZIGBEE_ENABLE] = false;
    _nvs_param_map[NVS_KEY_ZIGBEE_CHANNEL] = 13;
    _nvs_param_map[NVS_KEY_ZIGBEE_PERMIT_JOIN] = 180;
    _nvs_param_map[NVS_KEY_AUDIO_VOLUME] = bsp_extra_audio_media_volume_get();
    _nvs_param_map[NVS_KEY_AUDIO_VOLUME] = max(min((int)_nvs_param_map[NVS_KEY_AUDIO_VOLUME], SPEAKER_VOLUME_MAX), SPEAKER_VOLUME_MIN);
    _nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME] = bsp_extra_audio_system_volume_get();
    _nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME] = max(min((int)_nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME], SPEAKER_VOLUME_MAX), SPEAKER_VOLUME_MIN);
    _nvs_param_map[NVS_KEY_AUDIO_TAP_SOUND] = 1;
    _nvs_param_map[NVS_KEY_AUDIO_HAPTIC_FEEDBACK] = 1;
    _nvs_param_map[NVS_KEY_AUDIO_HAPTIC_GPIO] = 49;
    _nvs_param_map[NVS_KEY_AUDIO_HAPTIC_LEVEL] = 2;
    _nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS] = brightness;
    _nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS] = max(min((int)_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS], SCREEN_BRIGHTNESS_MAX), SCREEN_BRIGHTNESS_MIN);
    _nvs_param_map[NVS_KEY_NEOPIXEL_POWER] = 0;
    _nvs_param_map[NVS_KEY_NEOPIXEL_GPIO] = -1;
    _nvs_param_map[NVS_KEY_NEOPIXEL_BRIGHTNESS] = 60;
    _nvs_param_map[NVS_KEY_NEOPIXEL_PALETTE] = 0;
    _nvs_param_map[NVS_KEY_NEOPIXEL_EFFECT] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_ADAPTIVE] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_SCREENSAVER] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF_IN_GAME] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_SLEEP] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_IMU] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_SDA] = -1;
    _nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_SCL] = -1;
    _nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE] = 480;
    _nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO] = 0;
    _nvs_param_map[NVS_KEY_OTA_AUTO_UPDATE] = 1;
    _nvs_param_map[NVS_KEY_OTA_RESCHEDULE_UNTIL] = 0;
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

    stopLoRaSelfCheckStatusPolling();
    stopImuLivePolling();
    
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
    const int32_t sanitized_haptic_gpio = sanitizeHapticGpio(_nvs_param_map[NVS_KEY_AUDIO_HAPTIC_GPIO]);
    if (sanitized_haptic_gpio != _nvs_param_map[NVS_KEY_AUDIO_HAPTIC_GPIO]) {
        _nvs_param_map[NVS_KEY_AUDIO_HAPTIC_GPIO] = sanitized_haptic_gpio;
        setNvsParam(NVS_KEY_AUDIO_HAPTIC_GPIO, sanitized_haptic_gpio);
    }
    const int32_t sanitized_neopixel_gpio = sanitizeNeopixelGpio(_nvs_param_map[NVS_KEY_NEOPIXEL_GPIO]);
    if (sanitized_neopixel_gpio != _nvs_param_map[NVS_KEY_NEOPIXEL_GPIO]) {
        _nvs_param_map[NVS_KEY_NEOPIXEL_GPIO] = sanitized_neopixel_gpio;
        setNvsParam(NVS_KEY_NEOPIXEL_GPIO, sanitized_neopixel_gpio);
    }
    jc_ui_tap_sound_set_enabled(_nvs_param_map[NVS_KEY_AUDIO_TAP_SOUND] != 0);
    jc_ui_haptic_feedback_set_enabled(_nvs_param_map[NVS_KEY_AUDIO_HAPTIC_FEEDBACK] != 0);
    jc_ui_haptic_feedback_set_gpio(_nvs_param_map[NVS_KEY_AUDIO_HAPTIC_GPIO]);
    jc_ui_haptic_feedback_set_level(_nvs_param_map[NVS_KEY_AUDIO_HAPTIC_LEVEL]);
#if APP_SETTINGS_FEATURE_BLUETOOTH_MENU && APP_SETTINGS_FEATURE_LEGACY_BLUETOOTH_RUNTIME
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
    _nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION] = sanitizeDisplayOrientationDegrees(_nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION]);
    const int32_t preview_orientation = loadPendingDisplayOrientationPreviewDegrees();
    if (preview_orientation >= 0) {
        _nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION] = preview_orientation;
    }
    _nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE] = _nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE] != 0 ? 1 : 0;
    _nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_IMU] = sanitizeDisplayAutorotateAxis(_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_IMU]);
    const int32_t sanitized_autorotate_sda = sanitizeDisplayAutorotateGpio(_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_SDA]);
    if (sanitized_autorotate_sda != _nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_SDA]) {
        _nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_SDA] = sanitized_autorotate_sda;
        setNvsParam(NVS_KEY_DISPLAY_AUTOROTATE_SDA, sanitized_autorotate_sda);
    }
    const int32_t sanitized_autorotate_scl = sanitizeDisplayAutorotateGpio(_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_SCL]);
    if (sanitized_autorotate_scl != _nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_SCL]) {
        _nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_SCL] = sanitized_autorotate_scl;
        setNvsParam(NVS_KEY_DISPLAY_AUTOROTATE_SCL, sanitized_autorotate_scl);
    }
    ESP_ERROR_CHECK(bsp_extra_display_idle_init());
    applyDisplayIdleSettings();
    applyNeopixelConfig();
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

#if CONFIG_JC4880_FEATURE_OTA
    _nextOtaAvailabilityCheckUs = static_cast<uint64_t>(esp_timer_get_time()) + kOtaAvailabilityInitialDelayUs;
#endif

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
    openFirmwareScreenIfPending();

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

    #if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    lv_obj_add_flag(ui_PanelSettingMainContainerItem2, LV_OBJ_FLAG_HIDDEN);
    #endif
    lv_obj_add_flag(ui_PanelSettingMainContainerItem3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_PanelSettingMainContainerItem4, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_PanelSettingMainContainerItem5, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_PanelSettingMainContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui_PanelSettingMainContainer, LV_DIR_VER);
    lv_obj_clear_flag(ui_PanelSettingMainContainer, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(ui_PanelSettingMainContainer, LV_OBJ_FLAG_SCROLL_ELASTIC);

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
    _joypadMenuItem = createMainBadgeMenuItem("Joypad", "JP", lv_color_hex(0x0F766E), nullptr);
    #if APP_SETTINGS_FEATURE_IMU
    _imuMenuItem = createMainBadgeMenuItem("IMU", "IM", lv_color_hex(0x0EA5E9), nullptr);
    #endif
    #if CONFIG_JC4880_APP_LORA_MESH
    _loraMenuItem = createMainBadgeMenuItem("LoRa", "LR", lv_color_hex(0xB45309), nullptr);
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

    _wifiApPanel = lv_obj_create(ui_ScreenSettingWiFi);
    lv_obj_set_size(_wifiApPanel, lv_pct(90), 250);
    lv_obj_align_to(_wifiApPanel, ui_PanelScreenSettingWiFiSwitch, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    lv_obj_clear_flag(_wifiApPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_wifiApPanel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(_wifiApPanel, 18, 0);
    lv_obj_set_style_border_width(_wifiApPanel, 0, 0);
    lv_obj_set_style_bg_color(_wifiApPanel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(_wifiApPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(_wifiApPanel, 18, 0);
    lv_obj_set_style_pad_right(_wifiApPanel, 18, 0);
    lv_obj_set_style_pad_top(_wifiApPanel, 14, 0);
    lv_obj_set_style_pad_bottom(_wifiApPanel, 14, 0);

    lv_obj_t *wifiApTitle = lv_label_create(_wifiApPanel);
    lv_label_set_text(wifiApTitle, "AP Mode");
    lv_obj_set_style_text_font(wifiApTitle, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(wifiApTitle, lv_color_hex(0x111827), 0);
    lv_obj_align(wifiApTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    _wifiApSwitch = lv_switch_create(_wifiApPanel);
    lv_obj_align(_wifiApSwitch, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(_wifiApSwitch, onWifiApSwitchValueChangeEventCallback, LV_EVENT_VALUE_CHANGED, this);

    _wifiApStatusLabel = lv_label_create(_wifiApPanel);
    lv_obj_set_width(_wifiApStatusLabel, lv_pct(100));
    lv_label_set_long_mode(_wifiApStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_wifiApStatusLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_wifiApStatusLabel, lv_color_hex(0x475569), 0);
    lv_obj_align_to(_wifiApStatusLabel, wifiApTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);

    lv_obj_t *wifiApNameTitle = lv_label_create(_wifiApPanel);
    lv_label_set_text(wifiApNameTitle, "Name");
    lv_obj_set_style_text_font(wifiApNameTitle, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(wifiApNameTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align_to(wifiApNameTitle, _wifiApStatusLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 14);

    _wifiApSsidTextArea = lv_textarea_create(_wifiApPanel);
    lv_obj_set_size(_wifiApSsidTextArea, lv_pct(100), 52);
    lv_obj_align_to(_wifiApSsidTextArea, wifiApNameTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_textarea_set_one_line(_wifiApSsidTextArea, true);
    lv_textarea_set_max_length(_wifiApSsidTextArea, 32);
    lv_textarea_set_placeholder_text(_wifiApSsidTextArea, "JC4880P443C Remote");
    lv_obj_set_style_radius(_wifiApSsidTextArea, 16, 0);
    lv_obj_set_style_border_width(_wifiApSsidTextArea, 1, 0);
    lv_obj_set_style_border_color(_wifiApSsidTextArea, lv_color_hex(0xC6D4E1), 0);
    lv_obj_set_style_bg_color(_wifiApSsidTextArea, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_opa(_wifiApSsidTextArea, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(_wifiApSsidTextArea, 16, 0);
    lv_obj_set_style_pad_right(_wifiApSsidTextArea, 16, 0);
    lv_obj_set_style_text_font(_wifiApSsidTextArea, &lv_font_montserrat_18, 0);
    lv_obj_add_event_cb(_wifiApSsidTextArea, onWifiApFieldEventCallback, LV_EVENT_ALL, this);

    lv_obj_t *wifiApPasswordTitle = lv_label_create(_wifiApPanel);
    lv_label_set_text(wifiApPasswordTitle, "Password");
    lv_obj_set_style_text_font(wifiApPasswordTitle, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(wifiApPasswordTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align_to(wifiApPasswordTitle, _wifiApSsidTextArea, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);

    _wifiApPasswordTextArea = lv_textarea_create(_wifiApPanel);
    lv_obj_set_size(_wifiApPasswordTextArea, lv_pct(100), 52);
    lv_obj_align_to(_wifiApPasswordTextArea, wifiApPasswordTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_textarea_set_one_line(_wifiApPasswordTextArea, true);
    lv_textarea_set_max_length(_wifiApPasswordTextArea, 64);
    lv_textarea_set_password_mode(_wifiApPasswordTextArea, true);
    lv_textarea_set_placeholder_text(_wifiApPasswordTextArea, "Leave blank for open hotspot");
    lv_obj_set_style_radius(_wifiApPasswordTextArea, 16, 0);
    lv_obj_set_style_border_width(_wifiApPasswordTextArea, 1, 0);
    lv_obj_set_style_border_color(_wifiApPasswordTextArea, lv_color_hex(0xC6D4E1), 0);
    lv_obj_set_style_bg_color(_wifiApPasswordTextArea, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_opa(_wifiApPasswordTextArea, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(_wifiApPasswordTextArea, 16, 0);
    lv_obj_set_style_pad_right(_wifiApPasswordTextArea, 16, 0);
    lv_obj_set_style_text_font(_wifiApPasswordTextArea, &lv_font_montserrat_18, 0);
    lv_obj_add_event_cb(_wifiApPasswordTextArea, onWifiApFieldEventCallback, LV_EVENT_ALL, this);
    jc4880_password_textarea_install_toggle(_wifiApPasswordTextArea);
    lv_obj_add_event_cb(_wifiApPanel, onWifiKeyboardBackdropClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(ui_ScreenSettingWiFi, onWifiKeyboardBackdropClickedEventCallback, LV_EVENT_CLICKED, this);

    _wifiApSaveButton = lv_btn_create(_wifiApPanel);
    lv_obj_set_size(_wifiApSaveButton, 136, 42);
    lv_obj_align_to(_wifiApSaveButton, _wifiApPasswordTextArea, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 12);
    lv_obj_set_style_radius(_wifiApSaveButton, 14, 0);
    lv_obj_set_style_bg_color(_wifiApSaveButton, lv_color_hex(0x0F766E), 0);
    lv_obj_set_style_bg_opa(_wifiApSaveButton, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_wifiApSaveButton, 0, 0);
    lv_obj_add_event_cb(_wifiApSaveButton, onWifiApSaveClickedEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t *wifiApSaveLabel = lv_label_create(_wifiApSaveButton);
    lv_label_set_text(wifiApSaveLabel, "Save AP");
    lv_obj_set_style_text_color(wifiApSaveLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(wifiApSaveLabel, &lv_font_montserrat_16, 0);
    lv_obj_center(wifiApSaveLabel);

    _savedWifiPanel = lv_obj_create(ui_ScreenSettingWiFi);
    lv_obj_set_size(_savedWifiPanel, lv_pct(90), 120);
    lv_obj_align_to(_savedWifiPanel, _wifiApPanel, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
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

    _wifiApKeyboard = lv_keyboard_create(ui_ScreenSettingWiFi);
    lv_obj_set_size(_wifiApKeyboard, lv_pct(100), lv_pct(32));
    lv_obj_align(_wifiApKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_popovers(_wifiApKeyboard, true);
    lv_obj_add_flag(_wifiApKeyboard, LV_OBJ_FLAG_HIDDEN);
    jc4880_keyboard_install_case_behavior(_wifiApKeyboard);
    lv_obj_add_event_cb(_wifiApKeyboard, onWifiApKeyboardEventCallback, LV_EVENT_READY, this);
    lv_obj_add_event_cb(_wifiApKeyboard, onWifiApKeyboardEventCallback, LV_EVENT_CANCEL, this);

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
    jc4880_password_textarea_install_toggle(ui_TextAreaScreenSettingVerificationPassword);
    lv_obj_add_event_cb(ui_ScreenSettingVerification, onWifiKeyboardBackdropClickedEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_set_size(ui_KeyboardScreenSettingVerification, lv_pct(100), lv_pct(34));
    lv_obj_align(ui_KeyboardScreenSettingVerification, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(ui_KeyboardScreenSettingVerification, ui_TextAreaScreenSettingVerificationPassword);
    lv_keyboard_set_popovers(ui_KeyboardScreenSettingVerification, true);
    lv_obj_add_flag(ui_KeyboardScreenSettingVerification, LV_OBJ_FLAG_HIDDEN);
    jc4880_keyboard_install_case_behavior(ui_KeyboardScreenSettingVerification);
    lv_obj_add_event_cb(ui_KeyboardScreenSettingVerification, onKeyboardScreenSettingVerificationClickedEventCallback,
                        LV_EVENT_READY, this);
    lv_obj_add_event_cb(ui_KeyboardScreenSettingVerification, onKeyboardScreenSettingVerificationClickedEventCallback,
                        LV_EVENT_CANCEL, this);
    // Record the screen index and install the screen loaded event callback
    #if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    lv_obj_add_flag(ui_ButtonScreenSettingBLEReturn, LV_OBJ_FLAG_HIDDEN);
    #endif
    _screen_list[UI_WIFI_SCAN_INDEX] = ui_ScreenSettingWiFi;
    lv_obj_add_event_cb(ui_ScreenSettingWiFi, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
    _screen_list[UI_WIFI_CONNECT_INDEX] = ui_ScreenSettingVerification;
    lv_obj_add_event_cb(ui_ScreenSettingVerification, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);

    #if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
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
    jc4880_keyboard_install_case_behavior(_bluetoothKeyboard);
    lv_obj_add_event_cb(_bluetoothKeyboard, onBluetoothKeyboardEventCallback, LV_EVENT_ALL, this);

    lv_obj_add_event_cb(ui_SwitchPanelScreenSettingBLESwitch, onSwitchPanelScreenSettingBluetoothValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);
    // Record the screen index and install the screen loaded event callback
    _screen_list[UI_BLUETOOTH_SETTING_INDEX] = ui_ScreenSettingBLE;
    lv_obj_add_event_cb(ui_ScreenSettingBLE, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
    #endif

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

    static constexpr lv_coord_t kDisplayScreenTopInset = 14;
    static constexpr lv_coord_t kDisplayScreenBottomInset = 12;
    static constexpr lv_coord_t kDisplayListGap = 8;
    lv_obj_set_width(ui_PanelScreenSettingLightSwitch, lv_pct(90));
    lv_obj_align(ui_PanelScreenSettingLightSwitch, LV_ALIGN_TOP_MID, 0, kDisplayScreenTopInset);

    lv_obj_clear_flag(ui_PanelScreenSettingLightList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align_to(ui_PanelScreenSettingLightList, ui_PanelScreenSettingLightSwitch, LV_ALIGN_OUT_BOTTOM_MID, 0, kDisplayListGap);
    const lv_coord_t display_screen_height = lv_obj_get_height(ui_ScreenSettingLight);
    const lv_coord_t display_list_height = std::max<lv_coord_t>(
        120,
        display_screen_height - lv_obj_get_height(ui_PanelScreenSettingLightSwitch) -
        kDisplayScreenTopInset - kDisplayListGap - kDisplayScreenBottomInset);
    lv_obj_set_size(ui_PanelScreenSettingLightList, lv_pct(90), display_list_height);
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

    auto createDisplaySliderRow = [](lv_obj_t *parent, const char *title, lv_obj_t **out_slider, int32_t min_value, int32_t max_value) {
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

        lv_obj_t *slider = lv_slider_create(row);
        lv_slider_set_range(slider, min_value, max_value);
        lv_obj_set_size(slider, 200, 14);
        lv_obj_align(slider, LV_ALIGN_RIGHT_MID, 0, 0);
        if (out_slider != nullptr) {
            *out_slider = slider;
        }

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

    lv_obj_t *timeoffInGameRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Screen timeoff in game");
    _displayTimeoffInGameSwitch = lv_switch_create(timeoffInGameRow);
    lv_obj_align(_displayTimeoffInGameSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displayTimeoffInGameSwitch, onSwitchPanelScreenSettingTimeoffInGameValueChangeEventCallback,
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

    lv_obj_t *orientationRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Screen Orientation");
    _displayOrientationDropdown = lv_dropdown_create(orientationRow);
    lv_dropdown_set_options_static(_displayOrientationDropdown, kDisplayOrientationOptionsText);
    lv_obj_set_width(_displayOrientationDropdown, 112);
    lv_obj_align(_displayOrientationDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displayOrientationDropdown, onDropdownPanelScreenSettingOrientationValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *autorotateRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Autorotate");
    _displayAutorotateSwitch = lv_switch_create(autorotateRow);
    lv_obj_align(_displayAutorotateSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displayAutorotateSwitch, onSwitchPanelScreenSettingAutorotateValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *autorotateImuRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Rotation Axis");
    _displayAutorotateImuDropdown = lv_dropdown_create(autorotateImuRow);
    lv_dropdown_set_options_static(_displayAutorotateImuDropdown, kDisplayAutorotateAxisOptionsText);
    lv_obj_set_width(_displayAutorotateImuDropdown, 220);
    lv_obj_align(_displayAutorotateImuDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displayAutorotateImuDropdown, onDropdownPanelScreenSettingAutorotateImuValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    _displayAutorotateInfoLabel = lv_label_create(ui_PanelScreenSettingLightList);
    lv_obj_set_width(_displayAutorotateInfoLabel, lv_pct(100));
    lv_label_set_long_mode(_displayAutorotateInfoLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_displayAutorotateInfoLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_displayAutorotateInfoLabel, lv_color_hex(0x475569), 0);

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

    auto createAudioSwitchRow = [](const char *title) {
        lv_obj_t *row = lv_obj_create(ui_PanelScreenSettingVolumeList);
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

    {
        lv_obj_t *row = createAudioSwitchRow("Tap Sound");
        _audioTapSoundSwitch = lv_switch_create(row);
        lv_obj_align(_audioTapSoundSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(_audioTapSoundSwitch, onSwitchPanelScreenSettingTapSoundValueChangeEventCallback,
                            LV_EVENT_VALUE_CHANGED, this);
    }

    {
        lv_obj_t *row = createAudioSwitchRow("Haptic Feedback");
        _audioHapticFeedbackSwitch = lv_switch_create(row);
        lv_obj_align(_audioHapticFeedbackSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(_audioHapticFeedbackSwitch,
                            onSwitchPanelScreenSettingHapticFeedbackValueChangeEventCallback,
                            LV_EVENT_VALUE_CHANGED, this);
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
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
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
    lv_obj_set_style_radius(ui_PanelScreenSettingAbout, 0, 0);
    lv_obj_set_style_border_width(ui_PanelScreenSettingAbout, 0, 0);
    lv_obj_set_style_bg_opa(ui_PanelScreenSettingAbout, LV_OPA_TRANSP, 0);
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
        lv_obj_set_style_radius(card, 0, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
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
    lv_obj_set_style_radius(wifiInfoCard, 0, 0);
    lv_obj_set_style_border_width(wifiInfoCard, 0, 0);
    lv_obj_set_style_bg_opa(wifiInfoCard, LV_OPA_TRANSP, 0);
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
    jc4880_keyboard_install_case_behavior(_zigbeeKeyboard);
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

void AppSettings::ensureImuScreen(void)
{
#if !APP_SETTINGS_FEATURE_IMU
    return;
#else
    if ((_imuScreen != nullptr) && lv_obj_ready(_imuScreen)) {
        return;
    }

    _imuScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_imuScreen, lv_color_hex(0xE0F2FE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_imuScreen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_imuScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(_imuScreen);
    lv_obj_set_size(panel, lv_pct(94), lv_pct(96));
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_set_style_pad_row(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    auto createSection = [](lv_obj_t *parent, const char *section_title, const char *hint) {
        lv_obj_t *section = lv_obj_create(parent);
        lv_obj_set_width(section, lv_pct(100));
        lv_obj_set_height(section, LV_SIZE_CONTENT);
        lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(section, 18, 0);
        lv_obj_set_style_border_width(section, 0, 0);
        lv_obj_set_style_bg_color(section, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_pad_all(section, 14, 0);
        lv_obj_set_style_pad_row(section, 10, 0);
        lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t *sectionLabel = lv_label_create(section);
        lv_label_set_text(sectionLabel, section_title);
        lv_obj_set_style_text_font(sectionLabel, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(sectionLabel, lv_color_hex(0x0F172A), 0);

        lv_obj_t *hintLabel = lv_label_create(section);
        lv_obj_set_width(hintLabel, lv_pct(100));
        lv_label_set_long_mode(hintLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(hintLabel, hint);
        lv_obj_set_style_text_font(hintLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(hintLabel, lv_color_hex(0x475569), 0);
        return section;
    };

    auto createDropdownRow = [this](lv_obj_t *parent, const char *row_title, const char *options, lv_obj_t **dropdown_out) {
        lv_obj_t *row = create_settings_toggle_row(parent, row_title);
        lv_obj_t *dropdown = lv_dropdown_create(row);
        lv_dropdown_set_options_static(dropdown, options);
        lv_obj_set_width(dropdown, 210);
        lv_obj_align(dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(dropdown, onImuConfigChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);
        if (dropdown_out != nullptr) {
            *dropdown_out = dropdown;
        }
        return row;
    };

    auto createTextRow = [this](lv_obj_t *parent, const char *row_title, const char *placeholder, lv_obj_t **textarea_out) {
        lv_obj_t *row = create_settings_toggle_row(parent, row_title);
        lv_obj_t *textarea = lv_textarea_create(row);
        lv_obj_set_size(textarea, 160, 46);
        lv_obj_align(textarea, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_textarea_set_one_line(textarea, true);
        lv_textarea_set_placeholder_text(textarea, placeholder);
        lv_obj_set_style_radius(textarea, 14, 0);
        lv_obj_set_style_border_width(textarea, 1, 0);
        lv_obj_set_style_border_color(textarea, lv_color_hex(0xCBD5E1), 0);
        lv_obj_set_style_bg_color(textarea, lv_color_hex(0xF8FAFC), 0);
        lv_obj_set_style_pad_left(textarea, 12, 0);
        lv_obj_set_style_pad_right(textarea, 12, 0);
        if (textarea_out != nullptr) {
            *textarea_out = textarea;
        }
        return row;
    };

    auto createTelemetryCard = [](lv_obj_t *parent, const char *title, lv_obj_t **value_out) {
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_size(card, 104, 74);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(card, 18, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xE2E8F0), 0);
        lv_obj_set_style_pad_all(card, 10, 0);

        lv_obj_t *titleLabel = lv_label_create(card);
        lv_label_set_text(titleLabel, title);
        lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x475569), 0);
        lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *valueLabel = lv_label_create(card);
        lv_label_set_text(valueLabel, "--");
        lv_obj_set_style_text_font(valueLabel, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(valueLabel, lv_color_hex(0x0F172A), 0);
        lv_obj_align(valueLabel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        if (value_out != nullptr) {
            *value_out = valueLabel;
        }
        return card;
    };

    auto createSensorChip = [](lv_obj_t *parent, const char *text, lv_obj_t **dot_out, lv_obj_t **label_out) {
        lv_obj_t *chip = lv_obj_create(parent);
        lv_obj_set_height(chip, LV_SIZE_CONTENT);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(chip, 16, 0);
        lv_obj_set_style_border_width(chip, 0, 0);
        lv_obj_set_style_bg_color(chip, lv_color_hex(0xE2E8F0), 0);
        lv_obj_set_style_pad_left(chip, 10, 0);
        lv_obj_set_style_pad_right(chip, 10, 0);
        lv_obj_set_style_pad_top(chip, 8, 0);
        lv_obj_set_style_pad_bottom(chip, 8, 0);
        lv_obj_set_style_pad_column(chip, 8, 0);
        lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *dot = lv_obj_create(chip);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x94A3B8), 0);

        lv_obj_t *label = lv_label_create(chip);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x334155), 0);

        if (dot_out != nullptr) {
            *dot_out = dot;
        }
        if (label_out != nullptr) {
            *label_out = label;
        }
        return chip;
    };

    lv_obj_t *generalSection = createSection(panel,
                                             "IMU",
                                             "Default profile is BMI160 on JP1: SDA=GPIO31, SCL=GPIO30, INT1=GPIO50, INT2=GPIO51. Only one of IMU, LoRa, or Local Controller may be active at a time.");
    lv_obj_t *enabledRow = create_settings_toggle_row(generalSection, "Enable IMU");
    _imuEnabledSwitch = lv_switch_create(enabledRow);
    lv_obj_align(_imuEnabledSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_imuEnabledSwitch, onImuConfigChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);
    createDropdownRow(generalSection, "IMU Model", kImuModelOptionsText, &_imuModelDropdown);
    createDropdownRow(generalSection, "Bus Type", kImuBusOptionsText, &_imuBusDropdown);

    _imuPowerHintLabel = lv_label_create(generalSection);
    lv_obj_set_width(_imuPowerHintLabel, lv_pct(100));
    lv_label_set_long_mode(_imuPowerHintLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuPowerHintLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_imuPowerHintLabel, lv_color_hex(0x0369A1), 0);

    lv_obj_t *liveSection = createSection(panel,
                                          "Live Motion",
                                          "This panel streams the latest IMU sample and renders the scene from LVGL arcs, gradients, and shadows so it stays on the board's existing esp_lvgl_port display path. Extra chips light up when magnetometer, barometer, or fusion data are present.");

    _imuLiveScene = lv_obj_create(liveSection);
    lv_obj_set_size(_imuLiveScene, lv_pct(100), 430);
    lv_obj_clear_flag(_imuLiveScene, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_imuLiveScene, 22, 0);
    lv_obj_set_style_border_width(_imuLiveScene, 1, 0);
    lv_obj_set_style_border_color(_imuLiveScene, lv_color_hex(0x1E3A5F), 0);
    lv_obj_set_style_bg_color(_imuLiveScene, lv_color_hex(0x07111F), 0);
    lv_obj_set_style_bg_grad_color(_imuLiveScene, lv_color_hex(0x16314F), 0);
    lv_obj_set_style_bg_grad_dir(_imuLiveScene, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_width(_imuLiveScene, 20, 0);
    lv_obj_set_style_shadow_color(_imuLiveScene, lv_color_hex(0x020617), 0);
    lv_obj_set_style_shadow_opa(_imuLiveScene, LV_OPA_40, 0);

    lv_obj_t *sceneHeader = lv_obj_create(_imuLiveScene);
    lv_obj_set_size(sceneHeader, lv_pct(100), 42);
    lv_obj_align(sceneHeader, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_clear_flag(sceneHeader, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(sceneHeader, 14, 0);
    lv_obj_set_style_border_width(sceneHeader, 1, 0);
    lv_obj_set_style_border_color(sceneHeader, lv_color_hex(0x4ADEFF), 0);
    lv_obj_set_style_bg_color(sceneHeader, lv_color_hex(0x081623), 0);
    lv_obj_set_style_bg_opa(sceneHeader, LV_OPA_70, 0);

    lv_obj_t *sceneHeaderIcon = lv_obj_create(sceneHeader);
    lv_obj_set_size(sceneHeaderIcon, 24, 24);
    lv_obj_align(sceneHeaderIcon, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_clear_flag(sceneHeaderIcon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(sceneHeaderIcon, 8, 0);
    lv_obj_set_style_border_width(sceneHeaderIcon, 1, 0);
    lv_obj_set_style_border_color(sceneHeaderIcon, lv_color_hex(0x67E8F9), 0);
    lv_obj_set_style_bg_color(sceneHeaderIcon, lv_color_hex(0x0E2236), 0);
    lv_obj_set_style_bg_opa(sceneHeaderIcon, LV_OPA_80, 0);

    lv_obj_t *sceneHeaderIconCore = lv_obj_create(sceneHeaderIcon);
    lv_obj_set_size(sceneHeaderIconCore, 10, 10);
    lv_obj_center(sceneHeaderIconCore);
    lv_obj_clear_flag(sceneHeaderIconCore, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(sceneHeaderIconCore, 3, 0);
    lv_obj_set_style_border_width(sceneHeaderIconCore, 1, 0);
    lv_obj_set_style_border_color(sceneHeaderIconCore, lv_color_hex(0xA5F3FC), 0);
    lv_obj_set_style_bg_color(sceneHeaderIconCore, lv_color_hex(0x164E63), 0);

    _imuLiveCaptionLabel = lv_label_create(sceneHeader);
    lv_label_set_text(_imuLiveCaptionLabel, "BMI160");
    lv_obj_set_style_text_font(_imuLiveCaptionLabel, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(_imuLiveCaptionLabel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align(_imuLiveCaptionLabel, LV_ALIGN_TOP_LEFT, 42, -1);

    lv_obj_t *sceneHeaderSub = lv_label_create(sceneHeader);
    lv_label_set_text(sceneHeaderSub, "MOTION SENSOR TEST");
    lv_obj_set_style_text_font(sceneHeaderSub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sceneHeaderSub, lv_color_hex(0x67E8F9), 0);
    lv_obj_align(sceneHeaderSub, LV_ALIGN_BOTTOM_LEFT, 42, 0);

    lv_obj_t *leftPanel = lv_obj_create(_imuLiveScene);
    lv_obj_set_size(leftPanel, lv_pct(100), 104);
    lv_obj_align(leftPanel, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_clear_flag(leftPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(leftPanel, 14, 0);
    lv_obj_set_style_border_width(leftPanel, 1, 0);
    lv_obj_set_style_border_color(leftPanel, lv_color_hex(0x2A5679), 0);
    lv_obj_set_style_bg_color(leftPanel, lv_color_hex(0x091522), 0);
    lv_obj_set_style_bg_opa(leftPanel, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(leftPanel, 14, 0);
    lv_obj_set_style_pad_column(leftPanel, 14, 0);
    lv_obj_set_style_pad_row(leftPanel, 6, 0);
    lv_obj_set_flex_flow(leftPanel, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(leftPanel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *accelColumn = lv_obj_create(leftPanel);
    lv_obj_set_size(accelColumn, 112, LV_SIZE_CONTENT);
    lv_obj_clear_flag(accelColumn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(accelColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(accelColumn, 0, 0);
    lv_obj_set_style_pad_all(accelColumn, 0, 0);

    lv_obj_t *gyroColumn = lv_obj_create(leftPanel);
    lv_obj_set_size(gyroColumn, 112, LV_SIZE_CONTENT);
    lv_obj_clear_flag(gyroColumn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(gyroColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gyroColumn, 0, 0);
    lv_obj_set_style_pad_all(gyroColumn, 0, 0);

    lv_obj_t *accelTitle = lv_label_create(accelColumn);
    lv_label_set_text(accelTitle, "ACCELEROMETER");
    lv_obj_set_style_text_font(accelTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(accelTitle, lv_color_hex(0x67E8F9), 0);

    _imuAccelValueLabel = lv_label_create(accelColumn);
    lv_obj_set_width(_imuAccelValueLabel, lv_pct(100));
    lv_label_set_long_mode(_imuAccelValueLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuAccelValueLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_imuAccelValueLabel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align_to(_imuAccelValueLabel, accelTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    lv_obj_t *accelMiniGraph = lv_obj_create(accelColumn);
    lv_obj_set_size(accelMiniGraph, lv_pct(100), 34);
    lv_obj_align_to(accelMiniGraph, _imuAccelValueLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_clear_flag(accelMiniGraph, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(accelMiniGraph, 10, 0);
    lv_obj_set_style_border_width(accelMiniGraph, 1, 0);
    lv_obj_set_style_border_color(accelMiniGraph, lv_color_hex(0x17314D), 0);
    lv_obj_set_style_bg_color(accelMiniGraph, lv_color_hex(0x06111D), 0);
    lv_obj_set_style_bg_opa(accelMiniGraph, LV_OPA_50, 0);

    for (int i = 0; i < 3; ++i) {
        lv_obj_t *bar = lv_obj_create(accelMiniGraph);
        lv_obj_set_size(bar, 14, 18 - (i * 4));
        lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 12 + (i * 30), -8);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, i == 1 ? lv_color_hex(0x67E8F9) : lv_color_hex(0x7DD3FC), 0);
        lv_obj_set_style_bg_opa(bar, i == 1 ? LV_OPA_80 : LV_OPA_60, 0);

        static const char *kAxisLabels[] = {"X", "Y", "Z"};
        lv_obj_t *axis = lv_label_create(accelMiniGraph);
        lv_label_set_text(axis, kAxisLabels[i]);
        lv_obj_set_style_text_font(axis, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(axis, lv_color_hex(0x67E8F9), 0);
        lv_obj_align_to(axis, bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    }

    lv_obj_t *gyroTitle = lv_label_create(gyroColumn);
    lv_label_set_text(gyroTitle, "GYROSCOPE");
    lv_obj_set_style_text_font(gyroTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(gyroTitle, lv_color_hex(0x67E8F9), 0);

    _imuGyroValueLabel = lv_label_create(gyroColumn);
    lv_obj_set_width(_imuGyroValueLabel, lv_pct(100));
    lv_label_set_long_mode(_imuGyroValueLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuGyroValueLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_imuGyroValueLabel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align_to(_imuGyroValueLabel, gyroTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    lv_obj_t *rightPanel = lv_obj_create(_imuLiveScene);
    lv_obj_set_size(rightPanel, lv_pct(100), 108);
    lv_obj_align(rightPanel, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_clear_flag(rightPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(rightPanel, 14, 0);
    lv_obj_set_style_border_width(rightPanel, 1, 0);
    lv_obj_set_style_border_color(rightPanel, lv_color_hex(0x2A5679), 0);
    lv_obj_set_style_bg_color(rightPanel, lv_color_hex(0x091522), 0);
    lv_obj_set_style_bg_opa(rightPanel, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(rightPanel, 14, 0);
    lv_obj_set_style_pad_column(rightPanel, 12, 0);
    lv_obj_set_style_pad_row(rightPanel, 6, 0);

    lv_obj_t *orientationColumn = lv_obj_create(rightPanel);
    lv_obj_set_size(orientationColumn, 124, 76);
    lv_obj_align(orientationColumn, LV_ALIGN_LEFT_MID, 0, -2);
    lv_obj_clear_flag(orientationColumn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(orientationColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(orientationColumn, 0, 0);
    lv_obj_set_style_pad_all(orientationColumn, 0, 0);

    lv_obj_t *orientationTitle = lv_label_create(orientationColumn);
    lv_label_set_text(orientationTitle, "ORIENTATION");
    lv_obj_set_style_text_font(orientationTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(orientationTitle, lv_color_hex(0x67E8F9), 0);
    lv_obj_align(orientationTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    auto createOrientationRow = [](lv_obj_t *parent, const char *title, lv_coord_t y, lv_obj_t **value_out) {
        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x67E8F9), 0);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, y);

        lv_obj_t *value = lv_label_create(parent);
        lv_label_set_text(value, "+0.0 deg");
        lv_obj_set_style_text_font(value, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(value, lv_color_hex(0xF8FAFC), 0);
        lv_obj_align(value, LV_ALIGN_TOP_RIGHT, 0, y - 2);

        if (value_out != nullptr) {
            *value_out = value;
        }
    };

    createOrientationRow(orientationColumn, "ROLL", 20, &_imuRollValueLabel);
    createOrientationRow(orientationColumn, "PITCH", 40, &_imuPitchValueLabel);
    createOrientationRow(orientationColumn, "YAW", 60, &_imuYawValueLabel);

    _imuHeadingArc = lv_arc_create(rightPanel);
    lv_obj_set_size(_imuHeadingArc, 60, 60);
    lv_obj_align(_imuHeadingArc, LV_ALIGN_RIGHT_MID, -10, -8);
    lv_arc_set_range(_imuHeadingArc, 0, 360);
    lv_arc_set_rotation(_imuHeadingArc, 270);
    lv_arc_set_bg_angles(_imuHeadingArc, 0, 360);
    lv_arc_set_value(_imuHeadingArc, 0);
    lv_obj_remove_style(_imuHeadingArc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(_imuHeadingArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(_imuHeadingArc, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_color(_imuHeadingArc, lv_color_hex(0x1E3A5F), LV_PART_MAIN);
    lv_obj_set_style_arc_width(_imuHeadingArc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(_imuHeadingArc, lv_color_hex(0x38BDF8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_imuHeadingArc, LV_OPA_TRANSP, 0);

    _imuHeadingValueLabel = lv_label_create(rightPanel);
    lv_label_set_text(_imuHeadingValueLabel, "000 deg");
    lv_obj_set_style_text_font(_imuHeadingValueLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(_imuHeadingValueLabel, lv_color_hex(0xE0F2FE), 0);
    lv_obj_align_to(_imuHeadingValueLabel, _imuHeadingArc, LV_ALIGN_CENTER, 0, 0);

    _imuLiveStatusLabel = lv_label_create(rightPanel);
    lv_obj_set_width(_imuLiveStatusLabel, 96);
    lv_label_set_long_mode(_imuLiveStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuLiveStatusLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(_imuLiveStatusLabel, lv_color_hex(0x67E8F9), 0);
    lv_obj_align(_imuLiveStatusLabel, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *chamber = lv_obj_create(_imuLiveScene);
    lv_obj_set_size(chamber, lv_pct(100), 150);
    lv_obj_align(chamber, LV_ALIGN_CENTER, 0, 14);
    lv_obj_clear_flag(chamber, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(chamber, 18, 0);
    lv_obj_set_style_border_width(chamber, 1, 0);
    lv_obj_set_style_border_color(chamber, lv_color_hex(0x285E8E), 0);
    lv_obj_set_style_bg_color(chamber, lv_color_hex(0x08131F), 0);
    lv_obj_set_style_bg_grad_color(chamber, lv_color_hex(0x13273C), 0);
    lv_obj_set_style_bg_grad_dir(chamber, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(chamber, LV_OPA_80, 0);

    for (int i = 0; i < 5; ++i) {
        lv_obj_t *vLine = lv_obj_create(chamber);
        lv_obj_set_size(vLine, 1, 132);
        lv_obj_align(vLine, LV_ALIGN_TOP_LEFT, 22 + (i * 48), 8);
        lv_obj_clear_flag(vLine, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(vLine, 0, 0);
        lv_obj_set_style_bg_color(vLine, lv_color_hex(0x31597E), 0);
        lv_obj_set_style_bg_opa(vLine, i == 2 ? LV_OPA_40 : LV_OPA_20, 0);

        lv_obj_t *hLine = lv_obj_create(chamber);
        lv_obj_set_size(hLine, 220, 1);
        lv_obj_align(hLine, LV_ALIGN_TOP_LEFT, 8, 18 + (i * 28));
        lv_obj_clear_flag(hLine, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(hLine, 0, 0);
        lv_obj_set_style_bg_color(hLine, lv_color_hex(0x31597E), 0);
        lv_obj_set_style_bg_opa(hLine, i == 2 ? LV_OPA_40 : LV_OPA_20, 0);
    }

    lv_obj_t *ceilingGlow = lv_obj_create(chamber);
    lv_obj_set_size(ceilingGlow, 212, 3);
    lv_obj_align(ceilingGlow, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_clear_flag(ceilingGlow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ceilingGlow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ceilingGlow, 0, 0);
    lv_obj_set_style_bg_color(ceilingGlow, lv_color_hex(0x7DD3FC), 0);
    lv_obj_set_style_bg_opa(ceilingGlow, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(ceilingGlow, 18, 0);
    lv_obj_set_style_shadow_color(ceilingGlow, lv_color_hex(0x7DD3FC), 0);
    lv_obj_set_style_shadow_opa(ceilingGlow, LV_OPA_60, 0);

    lv_obj_t *floorPlane = lv_obj_create(chamber);
    lv_obj_set_size(floorPlane, 170, 28);
    lv_obj_align(floorPlane, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_clear_flag(floorPlane, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(floorPlane, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(floorPlane, 1, 0);
    lv_obj_set_style_border_color(floorPlane, lv_color_hex(0x2D597C), 0);
    lv_obj_set_style_bg_color(floorPlane, lv_color_hex(0x16324E), 0);
    lv_obj_set_style_bg_opa(floorPlane, LV_OPA_20, 0);

    lv_obj_t *leftGhost = lv_obj_create(chamber);
    lv_obj_set_size(leftGhost, 22, 22);
    lv_obj_align(leftGhost, LV_ALIGN_BOTTOM_LEFT, 48, -20);
    lv_obj_clear_flag(leftGhost, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(leftGhost, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(leftGhost, 1, 0);
    lv_obj_set_style_border_color(leftGhost, lv_color_hex(0xBFE8FF), 0);
    lv_obj_set_style_bg_color(leftGhost, lv_color_hex(0x7DD3FC), 0);
    lv_obj_set_style_bg_opa(leftGhost, LV_OPA_20, 0);

    lv_obj_t *rightGhost = lv_obj_create(chamber);
    lv_obj_set_size(rightGhost, 26, 26);
    lv_obj_align(rightGhost, LV_ALIGN_BOTTOM_RIGHT, -38, -18);
    lv_obj_clear_flag(rightGhost, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(rightGhost, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(rightGhost, 1, 0);
    lv_obj_set_style_border_color(rightGhost, lv_color_hex(0xBFE8FF), 0);
    lv_obj_set_style_bg_color(rightGhost, lv_color_hex(0x7DD3FC), 0);
    lv_obj_set_style_bg_opa(rightGhost, LV_OPA_20, 0);

    _imuMotionShadow = lv_obj_create(chamber);
    lv_obj_set_size(_imuMotionShadow, 42, 14);
    lv_obj_align(_imuMotionShadow, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_clear_flag(_imuMotionShadow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_imuMotionShadow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_imuMotionShadow, 0, 0);
    lv_obj_set_style_bg_color(_imuMotionShadow, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(_imuMotionShadow, LV_OPA_40, 0);

    _imuMotionDot = lv_obj_create(chamber);
    lv_obj_set_size(_imuMotionDot, 46, 46);
    lv_obj_clear_flag(_imuMotionDot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(_imuMotionDot, 0, 0);
    lv_obj_set_flex_flow(_imuMotionDot, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_imuMotionDot, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(_imuMotionDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_imuMotionDot, 1, 0);
    lv_obj_set_style_border_color(_imuMotionDot, lv_color_hex(0xE0F2FE), 0);
    lv_obj_set_style_bg_color(_imuMotionDot, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_bg_grad_color(_imuMotionDot, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_grad_dir(_imuMotionDot, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(_imuMotionDot, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(_imuMotionDot, 20, 0);
    lv_obj_set_style_shadow_opa(_imuMotionDot, LV_OPA_60, 0);
    lv_obj_set_style_shadow_color(_imuMotionDot, lv_color_hex(0x0EA5E9), 0);
    lv_obj_set_pos(_imuMotionDot, 92, 42);

    lv_obj_t *ballHighlight = lv_obj_create(_imuMotionDot);
    lv_obj_set_size(ballHighlight, 14, 10);
    lv_obj_align(ballHighlight, LV_ALIGN_TOP_LEFT, 8, 6);
    lv_obj_clear_flag(ballHighlight, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ballHighlight, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ballHighlight, 0, 0);
    lv_obj_set_style_bg_color(ballHighlight, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(ballHighlight, LV_OPA_70, 0);

    lv_obj_t *ballBand = lv_obj_create(_imuMotionDot);
    lv_obj_set_size(ballBand, 28, 6);
    lv_obj_align(ballBand, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_clear_flag(ballBand, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ballBand, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ballBand, 0, 0);
    lv_obj_set_style_bg_color(ballBand, lv_color_hex(0x0284C7), 0);
    lv_obj_set_style_bg_opa(ballBand, LV_OPA_60, 0);

    _imuMotionTempLabel = lv_label_create(_imuMotionDot);
    lv_label_set_text(_imuMotionTempLabel, "--.-");
    lv_obj_set_width(_imuMotionTempLabel, lv_pct(100));
    lv_obj_set_style_text_font(_imuMotionTempLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(_imuMotionTempLabel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_text_align(_imuMotionTempLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(_imuMotionTempLabel);

    lv_obj_t *sensorRow = lv_obj_create(liveSection);
    lv_obj_set_width(sensorRow, lv_pct(100));
    lv_obj_set_height(sensorRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(sensorRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(sensorRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sensorRow, 0, 0);
    lv_obj_set_style_pad_all(sensorRow, 0, 0);
    lv_obj_set_style_pad_column(sensorRow, 8, 0);
    lv_obj_set_style_pad_row(sensorRow, 8, 0);
    lv_obj_set_flex_flow(sensorRow, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(sensorRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    createSensorChip(sensorRow, "ACC", &_imuSensorIndicatorDots[0], &_imuSensorIndicatorLabels[0]);
    createSensorChip(sensorRow, "GYR", &_imuSensorIndicatorDots[1], &_imuSensorIndicatorLabels[1]);
    createSensorChip(sensorRow, "MAG", &_imuSensorIndicatorDots[2], &_imuSensorIndicatorLabels[2]);
    createSensorChip(sensorRow, "BAR", &_imuSensorIndicatorDots[3], &_imuSensorIndicatorLabels[3]);
    createSensorChip(sensorRow, "FUS", &_imuSensorIndicatorDots[4], &_imuSensorIndicatorLabels[4]);

    _imuMagValueLabel = lv_label_create(liveSection);
    lv_obj_set_width(_imuMagValueLabel, lv_pct(100));
    lv_label_set_long_mode(_imuMagValueLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuMagValueLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_imuMagValueLabel, lv_color_hex(0x0F172A), 0);

    _imuEnvValueLabel = lv_label_create(liveSection);
    lv_obj_set_width(_imuEnvValueLabel, lv_pct(100));
    lv_label_set_long_mode(_imuEnvValueLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuEnvValueLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_imuEnvValueLabel, lv_color_hex(0x0F172A), 0);

    lv_obj_t *i2cSection = createSection(panel,
                                         "I2C Wiring",
                                         "Pins can be remapped across the JP1 GPIO list. When LoRa is active, the E22-reserved pins are blocked for IMU use.");
    createDropdownRow(i2cSection, "I2C SDA", kImuPinOptionsText, &_imuI2cSdaDropdown);
    createDropdownRow(i2cSection, "I2C SCL", kImuPinOptionsText, &_imuI2cSclDropdown);
    createDropdownRow(i2cSection, "I2C Address", kImuAddressOptionsText, &_imuI2cAddressDropdown);
    createTextRow(i2cSection, "Manual Hex Address", "68", &_imuI2cAddressTextArea);
    createDropdownRow(i2cSection, "INT1 / INT", kImuPinOptionsText, &_imuIntDropdown);
    createDropdownRow(i2cSection, "INT2 / DRDY", kImuPinOptionsText, &_imuDrdyDropdown);

    lv_obj_t *diagSection = createSection(panel,
                                          "Diagnostics",
                                          "Scan validates the selected I2C wiring. Test attempts a live sample through the IMU service.");
    _imuScanButton = lv_btn_create(diagSection);
    lv_obj_set_size(_imuScanButton, lv_pct(100), 54);
    lv_obj_set_style_radius(_imuScanButton, 16, 0);
    lv_obj_set_style_border_width(_imuScanButton, 0, 0);
    lv_obj_add_event_cb(_imuScanButton, onImuScanClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *scanLabel = lv_label_create(_imuScanButton);
    lv_label_set_text(scanLabel, "Scan I2C");
    lv_obj_center(scanLabel);

    _imuTestButton = lv_btn_create(diagSection);
    lv_obj_set_size(_imuTestButton, lv_pct(100), 54);
    lv_obj_set_style_radius(_imuTestButton, 16, 0);
    lv_obj_set_style_border_width(_imuTestButton, 0, 0);
    lv_obj_add_event_cb(_imuTestButton, onImuTestClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *testLabel = lv_label_create(_imuTestButton);
    lv_label_set_text(testLabel, "Test IMU");
    lv_obj_center(testLabel);

    lv_obj_t *saveButton = lv_btn_create(diagSection);
    lv_obj_set_size(saveButton, lv_pct(100), 54);
    lv_obj_set_style_radius(saveButton, 16, 0);
    lv_obj_set_style_border_width(saveButton, 0, 0);
    lv_obj_add_event_cb(saveButton, onImuSaveClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *saveLabel = lv_label_create(saveButton);
    lv_label_set_text(saveLabel, "Save IMU Settings");
    lv_obj_center(saveLabel);

    _imuStatusLabel = lv_label_create(diagSection);
    lv_obj_set_width(_imuStatusLabel, lv_pct(100));
    lv_label_set_long_mode(_imuStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuStatusLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_imuStatusLabel, lv_color_hex(0x475569), 0);

    _imuInfoLabel = lv_label_create(diagSection);
    lv_obj_set_width(_imuInfoLabel, lv_pct(100));
    lv_label_set_long_mode(_imuInfoLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuInfoLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_imuInfoLabel, lv_color_hex(0x475569), 0);

    _screen_list[UI_IMU_SETTING_INDEX] = _imuScreen;
    lv_obj_add_event_cb(_imuScreen, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
#endif
}

void AppSettings::refreshImuUi(void)
{
#if !APP_SETTINGS_FEATURE_IMU
    return;
#else
    if ((_imuScreen == nullptr) || !lv_obj_ready(_imuScreen)) {
        return;
    }

    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);
    const bool local_controller_active = is_local_controller_backend_active();
    const bool lora_active = []() {
        jc4880::lora_mesh::StoredState state = {};
        return jc4880::lora_mesh::load_stored_state(state) && state.settings.radio_enabled;
    }();
    const jc4880::imu::ImuModelInfo *model_info = jc4880::imu::find_imu_model_info(config.model);

    if (config.enabled) {
        lv_obj_add_state(_imuEnabledSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(_imuEnabledSwitch, LV_STATE_CHECKED);
    }

    lv_dropdown_set_selected(_imuModelDropdown, imu_dropdown_index_from_model(config.model));
    lv_dropdown_set_selected(_imuBusDropdown,
                             findDropdownIndexForValue(kImuBusOptions,
                                                       sizeof(kImuBusOptions) / sizeof(kImuBusOptions[0]),
                                                       static_cast<int32_t>(config.busType)));
    lv_dropdown_set_selected(_imuI2cSdaDropdown, imu_pin_choice_index(config.i2cSda));
    lv_dropdown_set_selected(_imuI2cSclDropdown, imu_pin_choice_index(config.i2cScl));
    lv_dropdown_set_selected(_imuI2cAddressDropdown,
                             findDropdownIndexForValue(kImuAddressOptions,
                                                       sizeof(kImuAddressOptions) / sizeof(kImuAddressOptions[0]),
                                                       config.i2cAddress));
    lv_dropdown_set_selected(_imuIntDropdown, imu_pin_choice_index(config.intPin));
    lv_dropdown_set_selected(_imuDrdyDropdown, imu_pin_choice_index(config.drdyPin));

    char address_text[8] = {};
    std::snprintf(address_text, sizeof(address_text), "%02X", static_cast<unsigned>(config.i2cAddress));
    lv_textarea_set_text(_imuI2cAddressTextArea, address_text);

    if (model_info != nullptr) {
        char power_hint[160] = {};
        std::snprintf(power_hint,
                      sizeof(power_hint),
                      "Power wiring: %s. Magnetometer: %s. Fusion: %s.%s",
                      model_info->powerHint,
                      model_info->hasMagnetometer ? "yes" : "no",
                      model_info->hasFusion ? "yes" : "no",
                      model_info->placeholderOnly ? " Driver is staged as a placeholder for this model." : "");
        lv_label_set_text(_imuPowerHintLabel, power_hint);
        if (!model_info->hasMagnetometer) {
            lv_label_set_text(_imuStatusLabel, "Compass heading unavailable without magnetometer.");
        }
    }

    if (local_controller_active) {
        lv_label_set_text(_imuInfoLabel,
                          "Local Controller is active. Enabling IMU will turn Local Controller off so JP1 pins can be reassigned safely.");
    } else if (lora_active) {
        lv_label_set_text(_imuInfoLabel,
                          "LoRa is active. Enabling IMU will turn LoRa off because the default BMI160 wiring overlaps the E22 JP1 pin profile.");
    } else if (config.enabled) {
        lv_label_set_text(_imuInfoLabel,
                          "IMU is enabled. LoRa and Local Controller remain off while IMU owns its selected JP1 pins.");
    } else {
        lv_label_set_text(_imuInfoLabel,
                          "IMU is disabled. The default profile is BMI160 over I2C on GPIO31/GPIO30 with optional interrupts on GPIO50/GPIO51.");
    }

    refreshImuLiveUi();
#endif
}

void AppSettings::refreshImuLiveUi(void)
{
#if !APP_SETTINGS_FEATURE_IMU
    return;
#else
    if ((_imuScreen == nullptr) || !lv_obj_ready(_imuScreen) || (_imuLiveScene == nullptr)) {
        return;
    }

    auto setSensorIndicator = [](lv_obj_t *dot, lv_obj_t *label, const char *text, bool active, lv_color_t color) {
        if (lv_obj_ready(dot)) {
            lv_obj_set_style_bg_color(dot, active ? color : lv_color_hex(0x94A3B8), 0);
            lv_obj_set_style_bg_opa(dot, active ? LV_OPA_COVER : LV_OPA_50, 0);
        }
        if (lv_obj_ready(label)) {
            lv_label_set_text(label, text);
            lv_obj_set_style_text_color(label, active ? lv_color_hex(0x0F172A) : lv_color_hex(0x64748B), 0);
        }
    };

    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);
    jc4880::imu::ImuSample sample = {};
    std::string live_status;
    bool sample_ok = false;

    if (!config.enabled || (config.model == jc4880::imu::ImuModel::IMU_NONE)) {
        jc4880::imu::ImuService::instance().stop();
        live_status = "Live stream idle. Enable IMU to sample motion, heading, and auxiliary sensors.";
    } else {
        sample_ok = jc4880::imu::ImuService::instance().read(sample);
        if (!sample_ok) {
            if (jc4880::imu::ImuService::instance().begin(&config)) {
                sample_ok = jc4880::imu::ImuService::instance().read(sample);
            }
        }

        if (sample_ok) {
            char status[160] = {};
                std::snprintf(status,
                              sizeof(status),
                              "%s\n%.0f Hz %s\n%s",
                              jc4880::imu::imu_model_label(config.model),
                              static_cast<float>(config.sampleRateHz),
                              jc4880::imu::imu_bus_type_label(config.busType),
                              sample.hasMag ? "Compass fused" : "Yaw drift expected");
            live_status = status;
        } else {
            const jc4880::imu::ImuModelInfo *info = jc4880::imu::find_imu_model_info(config.model);
            if ((info != nullptr) && info->placeholderOnly) {
                live_status = std::string(info->label) + "\nDriver staged\nNo live stream";
            } else {
                live_status = "Live sample\nunavailable\nCheck wiring";
            }
        }
    }

    const float roll = sample_ok ? sample.roll : 0.0f;
    const float pitch = sample_ok ? sample.pitch : 0.0f;
    const float yaw = sample_ok ? sample.yaw : 0.0f;
    const int yaw_degrees = static_cast<int>(std::lround(std::fmod((yaw + 360.0f), 360.0f)));

    if (lv_obj_ready(_imuHeadingArc)) {
        lv_arc_set_value(_imuHeadingArc, yaw_degrees);
    }
    if (lv_obj_ready(_imuHeadingValueLabel)) {
        char heading[24] = {};
        std::snprintf(heading, sizeof(heading), "%03d deg", yaw_degrees);
        lv_label_set_text(_imuHeadingValueLabel, heading);
    }
    if (lv_obj_ready(_imuRollValueLabel)) {
        char text[24] = {};
        std::snprintf(text, sizeof(text), "%+.1f deg", roll);
        lv_label_set_text(_imuRollValueLabel, text);
    }
    if (lv_obj_ready(_imuPitchValueLabel)) {
        char text[24] = {};
        std::snprintf(text, sizeof(text), "%+.1f deg", pitch);
        lv_label_set_text(_imuPitchValueLabel, text);
    }
    if (lv_obj_ready(_imuYawValueLabel)) {
        char text[24] = {};
        std::snprintf(text, sizeof(text), "%+.1f deg", yaw);
        lv_label_set_text(_imuYawValueLabel, text);
    }
    if (lv_obj_ready(_imuLiveCaptionLabel)) {
        lv_label_set_text(_imuLiveCaptionLabel, jc4880::imu::imu_model_label(config.model));
    }
    if (lv_obj_ready(_imuLiveStatusLabel)) {
        lv_label_set_text(_imuLiveStatusLabel, live_status.c_str());
    }
    if (lv_obj_ready(_imuAccelValueLabel)) {
        char text[96] = {};
        if (sample_ok && sample.hasAccel) {
            std::snprintf(text, sizeof(text), "X  %+.2f g\nY  %+.2f g\nZ  %+.2f g", sample.ax, sample.ay, sample.az);
        } else {
            std::snprintf(text, sizeof(text), "X  --.-- g\nY  --.-- g\nZ  --.-- g");
        }
        lv_label_set_text(_imuAccelValueLabel, text);
    }
    if (lv_obj_ready(_imuGyroValueLabel)) {
        char text[96] = {};
        if (sample_ok && sample.hasGyro) {
            std::snprintf(text, sizeof(text), "X  %+.1f deg/s\nY  %+.1f deg/s\nZ  %+.1f deg/s", sample.gx, sample.gy, sample.gz);
        } else {
            std::snprintf(text, sizeof(text), "X  --.- deg/s\nY  --.- deg/s\nZ  --.- deg/s");
        }
        lv_label_set_text(_imuGyroValueLabel, text);
    }
    if (lv_obj_ready(_imuMagValueLabel)) {
        char text[112] = {};
        if (sample_ok && sample.hasMag) {
            std::snprintf(text, sizeof(text), "Mag: X=%+.1f  Y=%+.1f  Z=%+.1f uT", sample.mx, sample.my, sample.mz);
        } else {
            std::snprintf(text, sizeof(text), "Mag: unavailable on this reading or this IMU model");
        }
        lv_label_set_text(_imuMagValueLabel, text);
    }
    if (lv_obj_ready(_imuEnvValueLabel)) {
        char text[112] = {};
        if (sample_ok && sample.hasBarometer) {
            std::snprintf(text,
                          sizeof(text),
                          "Temp: %.1f C  Pressure: %.1f hPa  Alt: %.1f m",
                          sample.temperature,
                          sample.pressure,
                          sample.altitude);
        } else if (sample_ok) {
            std::snprintf(text, sizeof(text), "Temp: %.1f C  Barometer: unavailable", sample.temperature);
        } else {
            std::snprintf(text, sizeof(text), "Temp / pressure stream unavailable");
        }
        lv_label_set_text(_imuEnvValueLabel, text);
    }

    setSensorIndicator(_imuSensorIndicatorDots[0], _imuSensorIndicatorLabels[0], "ACC", sample_ok && sample.hasAccel, lv_color_hex(0x22C55E));
    setSensorIndicator(_imuSensorIndicatorDots[1], _imuSensorIndicatorLabels[1], "GYR", sample_ok && sample.hasGyro, lv_color_hex(0x38BDF8));
    setSensorIndicator(_imuSensorIndicatorDots[2], _imuSensorIndicatorLabels[2], "MAG", sample_ok && sample.hasMag, lv_color_hex(0xF59E0B));
    setSensorIndicator(_imuSensorIndicatorDots[3], _imuSensorIndicatorLabels[3], "BAR", sample_ok && sample.hasBarometer, lv_color_hex(0xA855F7));
    setSensorIndicator(_imuSensorIndicatorDots[4], _imuSensorIndicatorLabels[4], "FUS", sample_ok && sample.hasFusion, lv_color_hex(0xF43F5E));

    if (lv_obj_ready(_imuMotionDot)) {
        const float temperature_c = sample_ok ? sample.temperature : 24.0f;
        const float temp_mix = std::clamp((temperature_c - 18.0f) / 22.0f, 0.0f, 1.0f);
        const lv_color_t temp_core_color = lv_color_mix(lv_color_hex(0xFB7185), lv_color_hex(0x38BDF8), static_cast<uint8_t>(255.0f * (1.0f - temp_mix)));
        const lv_color_t temp_edge_color = lv_color_mix(lv_color_hex(0xFFF1F2), lv_color_hex(0xE0F2FE), static_cast<uint8_t>(255.0f * (1.0f - temp_mix)));

        if (sample_ok && sample.hasAccel) {
            const float center_x = 92.0f;
            const float center_y = 42.0f;
            if (!_imuBallDynamicsInitialized) {
                _imuBallPosX = center_x;
                _imuBallPosY = center_y;
                _imuBallPosZ = 0.0f;
                _imuBallVelX = 0.0f;
                _imuBallVelY = 0.0f;
                _imuBallVelZ = 0.0f;
                _imuBallPrevAccelX = sample.ax;
                _imuBallPrevAccelY = sample.ay;
                _imuBallPrevAccelZ = sample.az;
                _imuBallDynamicsInitialized = true;
            }

            const float accel_impulse_x = std::clamp((sample.ax - _imuBallPrevAccelX) * 28.0f, -6.5f, 6.5f);
            const float accel_impulse_y = std::clamp((_imuBallPrevAccelY - sample.ay) * 28.0f, -6.5f, 6.5f);
            const float accel_impulse_z = std::clamp((sample.az - _imuBallPrevAccelZ) * 18.0f, -4.0f, 4.0f);
            if (!_imuBallDynamicsInitialized) {
                _imuBallDynamicsInitialized = true;
            }
            _imuBallPrevAccelX = sample.ax;
            _imuBallPrevAccelY = sample.ay;
            _imuBallPrevAccelZ = sample.az;

            _imuBallVelX = (_imuBallVelX + ((center_x - _imuBallPosX) * 0.11f) + accel_impulse_x) * 0.80f;
            _imuBallVelY = (_imuBallVelY + ((center_y - _imuBallPosY) * 0.11f) + accel_impulse_y) * 0.80f;
            _imuBallVelZ = (_imuBallVelZ + ((0.0f - _imuBallPosZ) * 0.15f) + accel_impulse_z) * 0.78f;
            _imuBallPosX = std::clamp(_imuBallPosX + _imuBallVelX, 28.0f, 136.0f);
            _imuBallPosY = std::clamp(_imuBallPosY + _imuBallVelY, 10.0f, 72.0f);
            _imuBallPosZ = std::clamp(_imuBallPosZ + _imuBallVelZ, -14.0f, 14.0f);
        } else {
            _imuBallVelX *= 0.68f;
            _imuBallVelY *= 0.68f;
            _imuBallVelZ *= 0.68f;
            _imuBallPosX = std::clamp(_imuBallPosX + ((92.0f - _imuBallPosX) * 0.15f) + _imuBallVelX, 28.0f, 136.0f);
            _imuBallPosY = std::clamp(_imuBallPosY + ((42.0f - _imuBallPosY) * 0.15f) + _imuBallVelY, 10.0f, 72.0f);
            _imuBallPosZ = std::clamp(_imuBallPosZ + ((0.0f - _imuBallPosZ) * 0.18f) + _imuBallVelZ, -14.0f, 14.0f);
            _imuBallDynamicsInitialized = false;
        }

        const int32_t x = static_cast<int32_t>(std::lround(_imuBallPosX));
        const int32_t y = static_cast<int32_t>(std::lround(_imuBallPosY));
        const int32_t depth_size = static_cast<int32_t>(std::lround(std::clamp(46.0f + (_imuBallPosZ * 1.0f), 30.0f, 60.0f)));
        const int32_t shadow_width = static_cast<int32_t>(std::lround(std::clamp(40.0f - (_imuBallPosZ * 0.8f), 24.0f, 48.0f)));
        const int32_t shadow_height = static_cast<int32_t>(std::lround(std::clamp(14.0f - (_imuBallPosZ * 0.25f), 8.0f, 18.0f)));

        if (lv_obj_ready(_imuMotionShadow)) {
            lv_obj_set_size(_imuMotionShadow, shadow_width, shadow_height);
            lv_obj_set_pos(_imuMotionShadow,
                           static_cast<int32_t>(std::lround(_imuBallPosX + ((depth_size - shadow_width) * 0.5f))),
                           static_cast<int32_t>(std::lround(104.0f - (_imuBallPosZ * 0.45f))));
            lv_obj_set_style_bg_opa(_imuMotionShadow,
                                    static_cast<lv_opa_t>(std::clamp(90.0f - (_imuBallPosZ * 4.0f), 20.0f, 90.0f)),
                                    0);
        }

        lv_obj_set_size(_imuMotionDot, depth_size, depth_size);
        lv_obj_set_pos(_imuMotionDot, x, y);
        lv_obj_set_style_bg_color(_imuMotionDot, sample_ok ? temp_core_color : lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_bg_grad_color(_imuMotionDot, sample_ok ? temp_edge_color : lv_color_hex(0xCBD5E1), 0);
        lv_obj_set_style_bg_opa(_imuMotionDot, sample_ok ? LV_OPA_50 : LV_OPA_40, 0);
        lv_obj_set_style_shadow_color(_imuMotionDot, sample_ok ? temp_core_color : lv_color_hex(0x64748B), 0);
        lv_obj_set_style_shadow_width(_imuMotionDot,
                                      static_cast<int32_t>(std::lround(std::clamp(16.0f + (_imuBallPosZ * 0.6f), 10.0f, 22.0f))),
                                      0);
        lv_obj_set_style_shadow_opa(_imuMotionDot,
                                    static_cast<lv_opa_t>(std::clamp(80.0f + (temp_mix * 100.0f), 60.0f, 170.0f)),
                                    0);
        lv_obj_set_style_border_color(_imuMotionDot, sample_ok ? temp_edge_color : lv_color_hex(0xE2E8F0), 0);

        if (lv_obj_ready(_imuMotionTempLabel)) {
            char temp_text[16] = {};
            if (sample_ok) {
                std::snprintf(temp_text, sizeof(temp_text), "%.1fC", temperature_c);
            } else {
                std::snprintf(temp_text, sizeof(temp_text), "--.-C");
            }
            lv_label_set_text(_imuMotionTempLabel, temp_text);
            lv_obj_set_style_text_color(_imuMotionTempLabel,
                                        sample_ok && temp_mix > 0.6f ? lv_color_hex(0xFFF7ED) : lv_color_hex(0xEFF6FF),
                                        0);
            if (depth_size <= 34) {
                lv_obj_set_style_text_font(_imuMotionTempLabel, &lv_font_montserrat_10, 0);
            } else if (depth_size <= 42) {
                lv_obj_set_style_text_font(_imuMotionTempLabel, &lv_font_montserrat_12, 0);
            } else if (depth_size <= 50) {
                lv_obj_set_style_text_font(_imuMotionTempLabel, &lv_font_montserrat_14, 0);
            } else {
                lv_obj_set_style_text_font(_imuMotionTempLabel, &lv_font_montserrat_16, 0);
            }
            lv_obj_center(_imuMotionTempLabel);
        }
    }
#endif
}

void AppSettings::startImuLivePolling(void)
{
#if !APP_SETTINGS_FEATURE_IMU
    return;
#else
    if (_imuLiveTimer != nullptr) {
        return;
    }
    _imuLiveTimer = lv_timer_create(onImuLiveTimerCallback, IMU_LIVE_REFRESH_MS, this);
#endif
}

void AppSettings::stopImuLivePolling(void)
{
#if !APP_SETTINGS_FEATURE_IMU
    return;
#else
    if (_imuLiveTimer != nullptr) {
        lv_timer_del(_imuLiveTimer);
        _imuLiveTimer = nullptr;
    }
#endif
}

bool AppSettings::persistImuConfigFromUi(bool autosave_enabled_only)
{
#if !APP_SETTINGS_FEATURE_IMU
    (void)autosave_enabled_only;
    return false;
#else
    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);

    config.enabled = lv_obj_ready(_imuEnabledSwitch) && lv_obj_has_state(_imuEnabledSwitch, LV_STATE_CHECKED);
    if (!autosave_enabled_only) {
        config.model = static_cast<jc4880::imu::ImuModel>(imu_model_from_dropdown(lv_dropdown_get_selected(_imuModelDropdown)));
        config.busType = static_cast<jc4880::imu::ImuBusType>(getDropdownValueForIndex(kImuBusOptions,
                                                                                       sizeof(kImuBusOptions) / sizeof(kImuBusOptions[0]),
                                                                                       lv_dropdown_get_selected(_imuBusDropdown)));
        config.i2cSda = static_cast<int8_t>(sanitizeImuAssignableGpio(imu_pin_choice_value(lv_dropdown_get_selected(_imuI2cSdaDropdown))));
        config.i2cScl = static_cast<int8_t>(sanitizeImuAssignableGpio(imu_pin_choice_value(lv_dropdown_get_selected(_imuI2cSclDropdown))));
        config.intPin = static_cast<int8_t>(sanitizeImuAssignableGpio(imu_pin_choice_value(lv_dropdown_get_selected(_imuIntDropdown))));
        config.drdyPin = static_cast<int8_t>(sanitizeImuAssignableGpio(imu_pin_choice_value(lv_dropdown_get_selected(_imuDrdyDropdown))));
        config.i2cAddress = static_cast<uint8_t>(getDropdownValueForIndex(kImuAddressOptions,
                                                                          sizeof(kImuAddressOptions) / sizeof(kImuAddressOptions[0]),
                                                                          lv_dropdown_get_selected(_imuI2cAddressDropdown)));
        const char *manual_hex = lv_textarea_get_text(_imuI2cAddressTextArea);
        if ((manual_hex != nullptr) && (*manual_hex != '\0')) {
            char *end = nullptr;
            const unsigned long manual_value = std::strtoul(manual_hex, &end, 16);
            if ((end != manual_hex) && (*end == '\0') && (manual_value >= 0x08) && (manual_value <= 0x77)) {
                config.i2cAddress = static_cast<uint8_t>(manual_value);
            }
        }
    }

    if (config.enabled) {
        if (!disableLocalControllerForLoRa()) {
            if (lv_obj_ready(_imuStatusLabel)) {
                lv_label_set_text(_imuStatusLabel, "Failed to disable Local Controller before enabling IMU.");
            }
            return false;
        }

        jc4880::lora_mesh::StoredState lora_state = {};
        if (jc4880::lora_mesh::load_stored_state(lora_state) && lora_state.settings.radio_enabled) {
            lora_state.settings.radio_enabled = false;
            if (!jc4880::lora_mesh::save_stored_state(lora_state)) {
                if (lv_obj_ready(_imuStatusLabel)) {
                    lv_label_set_text(_imuStatusLabel, "Failed to disable LoRa before enabling IMU.");
                }
                return false;
            }
        }
    }

    std::string error;
    if (!jc4880::imu::ImuService::instance().validateConfig(config, error)) {
        if (lv_obj_ready(_imuStatusLabel)) {
            lv_label_set_text(_imuStatusLabel, error.c_str());
        }
        return false;
    }

    if (!jc4880::imu::ImuService::instance().saveConfig(config)) {
        if (lv_obj_ready(_imuStatusLabel)) {
            lv_label_set_text(_imuStatusLabel, "Failed to save IMU settings.");
        }
        return false;
    }

    refreshImuUi();
    refreshLoRaUi();
    refreshJoypadUi();
    if (!autosave_enabled_only && lv_obj_ready(_imuStatusLabel)) {
        lv_label_set_text(_imuStatusLabel, "IMU settings saved.");
    }
    return true;
#endif
}

void AppSettings::disableImuForLocalController(void)
{
#if !APP_SETTINGS_FEATURE_IMU
    return;
#else
    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);
    if (!config.enabled) {
        return;
    }
    config.enabled = false;
    jc4880::imu::ImuService::instance().saveConfig(config);
    refreshImuUi();
#endif
}

bool AppSettings::disableImuForLoRa(void)
{
#if !APP_SETTINGS_FEATURE_IMU
    return true;
#else
    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);
    if (!config.enabled) {
        return true;
    }
    config.enabled = false;
    const bool saved = jc4880::imu::ImuService::instance().saveConfig(config);
    if (saved) {
        refreshImuUi();
    }
    return saved;
#endif
}

void AppSettings::ensureLoRaScreen(void)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    return;
#else
    if ((_loraScreen != nullptr) && lv_obj_ready(_loraScreen)) {
        return;
    }

    _loraScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_loraScreen, lv_color_hex(0xE5F3FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_loraScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_loraScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(_loraScreen);
    lv_obj_set_size(panel, lv_pct(94), lv_pct(96));
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_set_style_pad_row(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    auto createSection = [](lv_obj_t *parent, const char *section_title, const char *hint) {
        lv_obj_t *section = lv_obj_create(parent);
        lv_obj_set_width(section, lv_pct(100));
        lv_obj_set_height(section, LV_SIZE_CONTENT);
        lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(section, 18, 0);
        lv_obj_set_style_border_width(section, 0, 0);
        lv_obj_set_style_bg_color(section, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_pad_all(section, 14, 0);
        lv_obj_set_style_pad_row(section, 10, 0);
        lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t *sectionLabel = lv_label_create(section);
        lv_label_set_text(sectionLabel, section_title);
        lv_obj_set_style_text_font(sectionLabel, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(sectionLabel, lv_color_hex(0x0F172A), 0);

        lv_obj_t *hintLabel = lv_label_create(section);
        lv_obj_set_width(hintLabel, lv_pct(100));
        lv_label_set_long_mode(hintLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(hintLabel, hint);
        lv_obj_set_style_text_font(hintLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(hintLabel, lv_color_hex(0x475569), 0);
        return section;
    };

    auto createDropdownRow = [this](lv_obj_t *parent, const char *row_title, const char *options, lv_obj_t **dropdown_out) {
        lv_obj_t *row = create_settings_toggle_row(parent, row_title);
        lv_obj_t *dropdown = lv_dropdown_create(row);
        lv_dropdown_set_options_static(dropdown, options);
        lv_obj_set_width(dropdown, 210);
        lv_obj_align(dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(dropdown, onLoRaConfigChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);
        if (dropdown_out != nullptr) {
            *dropdown_out = dropdown;
        }
        return row;
    };

    auto createTextRow = [](lv_obj_t *parent, const char *row_title, const char *placeholder, lv_obj_t **textarea_out) {
        lv_obj_t *row = create_settings_toggle_row(parent, row_title);
        lv_obj_t *textarea = lv_textarea_create(row);
        lv_obj_set_size(textarea, 220, 46);
        lv_obj_align(textarea, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_textarea_set_one_line(textarea, true);
        lv_textarea_set_placeholder_text(textarea, placeholder);
        lv_obj_set_style_radius(textarea, 14, 0);
        lv_obj_set_style_border_width(textarea, 1, 0);
        lv_obj_set_style_border_color(textarea, lv_color_hex(0xCBD5E1), 0);
        lv_obj_set_style_bg_color(textarea, lv_color_hex(0xF8FAFC), 0);
        lv_obj_set_style_pad_left(textarea, 12, 0);
        lv_obj_set_style_pad_right(textarea, 12, 0);
        if (textarea_out != nullptr) {
            *textarea_out = textarea;
        }
        return row;
    };

    lv_obj_t *generalSection = createSection(panel,
                                             "Radio",
                                             "These settings are shared with the LoRa Mesh app. Disable the radio here when Local Controller needs shared GPIOs for haptics or Neopixel.");
    lv_obj_t *enabledRow = create_settings_toggle_row(generalSection, "Enable LoRa Radio");
    _loraEnabledSwitch = lv_switch_create(enabledRow);
    lv_obj_align(_loraEnabledSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_loraEnabledSwitch, onLoRaConfigChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *identitySection = createSection(panel,
                                              "Identity",
                                              "Display Name and Common Chat Title are shared with the LoRa Mesh app.");
    createTextRow(identitySection, "Display Name", "Visible chat name", &_loraDisplayNameTextArea);
    createTextRow(identitySection, "Common Chat Title", "Common Mesh Chat", &_loraCommonChatTitleTextArea);

    createDropdownRow(generalSection, "Radio Module", kLoraModuleOptionsText, &_loraModuleDropdown);
    createTextRow(generalSection, "Frequency (Hz)", "433125000", &_loraFrequencyTextArea);
    createTextRow(generalSection, "Spreading Factor", "9", &_loraSpreadingFactorTextArea);
    createTextRow(generalSection, "Bandwidth Index", "4", &_loraBandwidthTextArea);
    createTextRow(generalSection, "Coding Rate", "1", &_loraCodingRateTextArea);
    createTextRow(generalSection, "Hop Limit", "4", &_loraHopLimitTextArea);

    lv_obj_t *meshSection = createSection(panel,
                                          "Mesh Behavior",
                                          "Forwarding relays traffic for nearby peers. Public chat encryption uses the shared mesh group key.");
    lv_obj_t *forwardRow = create_settings_toggle_row(meshSection, "Enable Forwarding");
    _loraForwardingSwitch = lv_switch_create(forwardRow);
    lv_obj_align(_loraForwardingSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *encryptRow = create_settings_toggle_row(meshSection, "Encrypt Public Chat");
    _loraEncryptionSwitch = lv_switch_create(encryptRow);
    lv_obj_align(_loraEncryptionSwitch, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *pinsSection = createSection(panel,
                                          "Pin Mapping",
                                          "The fixed E22 wiring defaults are prefilled. Keep each active signal on a unique GPIO.");
    for (size_t role_index = 0; role_index < kLoraPinRoleCount; ++role_index) {
        _loraPinRows[role_index] = createDropdownRow(pinsSection,
                                                     lora_pin_role_label(static_cast<LoraPinRole>(role_index)),
                                                     kLoraGpioOptionsText,
                                                     &_loraPinDropdowns[role_index]);
    }

    lv_obj_t *applySection = createSection(panel,
                                           "Diagnostics",
                                           "Run Self Check to open LoRa Mesh and start the built-in radio self-test. Save writes directly to shared LoRa storage.");
    _loraSelfCheckButton = lv_btn_create(applySection);
    lv_obj_set_size(_loraSelfCheckButton, lv_pct(100), 54);
    lv_obj_set_style_radius(_loraSelfCheckButton, 16, 0);
    lv_obj_set_style_border_width(_loraSelfCheckButton, 0, 0);
    lv_obj_add_event_cb(_loraSelfCheckButton, onLoRaSelfCheckClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *selfCheckLabel = lv_label_create(_loraSelfCheckButton);
    lv_label_set_text(selfCheckLabel, "Run LoRa Self Check");
    lv_obj_center(selfCheckLabel);

    _loraSelfCheckStatusLabel = lv_label_create(applySection);
    lv_obj_set_width(_loraSelfCheckStatusLabel, lv_pct(100));
    lv_label_set_long_mode(_loraSelfCheckStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_loraSelfCheckStatusLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_loraSelfCheckStatusLabel, lv_color_hex(0x64748B), 0);

    lv_obj_t *saveButton = lv_btn_create(applySection);
    lv_obj_set_size(saveButton, lv_pct(100), 54);
    lv_obj_set_style_radius(saveButton, 16, 0);
    lv_obj_set_style_border_width(saveButton, 0, 0);
    lv_obj_add_event_cb(saveButton, onLoRaSaveClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *saveLabel = lv_label_create(saveButton);
    lv_label_set_text(saveLabel, "Save LoRa Settings");
    lv_obj_center(saveLabel);

    _loraInfoLabel = lv_label_create(applySection);
    lv_obj_set_width(_loraInfoLabel, lv_pct(100));
    lv_label_set_long_mode(_loraInfoLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_loraInfoLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_loraInfoLabel, lv_color_hex(0x475569), 0);

    _screen_list[UI_LORA_SETTING_INDEX] = _loraScreen;
    lv_obj_add_event_cb(_loraScreen, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
#endif
}

void AppSettings::refreshLoRaUi(void)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    return;
#else
    if ((_loraScreen == nullptr) || !lv_obj_ready(_loraScreen)) {
        return;
    }

    jc4880::lora_mesh::StoredState state = {};
    jc4880::lora_mesh::load_stored_state(state);
    const bool local_controller_active = is_local_controller_backend_active();
    const bool radio_enabled = state.settings.radio_enabled;
    const auto module = state.settings.radio_module;

    auto set_disabled = [](lv_obj_t *object, bool disabled) {
        if ((object == nullptr) || !lv_obj_ready(object)) {
            return;
        }
        if (disabled) {
            lv_obj_add_state(object, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(object, LV_STATE_DISABLED);
        }
    };

    if (radio_enabled) {
        lv_obj_add_state(_loraEnabledSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(_loraEnabledSwitch, LV_STATE_CHECKED);
    }
    set_disabled(_loraEnabledSwitch, false);

    lv_textarea_set_text(_loraDisplayNameTextArea, state.identity.display_name.c_str());
    lv_textarea_set_text(_loraCommonChatTitleTextArea, state.settings.common_chat_name.c_str());
    lv_dropdown_set_selected(_loraModuleDropdown, lora_dropdown_index_from_radio_module(module));
    lv_textarea_set_text(_loraFrequencyTextArea, std::to_string(state.settings.frequency_hz).c_str());
    lv_textarea_set_text(_loraSpreadingFactorTextArea, std::to_string(state.settings.spreading_factor).c_str());
    lv_textarea_set_text(_loraBandwidthTextArea, std::to_string(state.settings.bandwidth).c_str());
    lv_textarea_set_text(_loraCodingRateTextArea, std::to_string(state.settings.coding_rate).c_str());
    lv_textarea_set_text(_loraHopLimitTextArea, std::to_string(state.settings.hop_limit).c_str());

    if (state.settings.forwarding_enabled) {
        lv_obj_add_state(_loraForwardingSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(_loraForwardingSwitch, LV_STATE_CHECKED);
    }
    if (state.settings.public_chat_encryption) {
        lv_obj_add_state(_loraEncryptionSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(_loraEncryptionSwitch, LV_STATE_CHECKED);
    }

    for (size_t role_index = 0; role_index < kLoraPinRoleCount; ++role_index) {
        const auto role = static_cast<LoraPinRole>(role_index);
        const bool visible = lora_module_uses_role(module, role);
        if ((_loraPinRows[role_index] != nullptr) && lv_obj_ready(_loraPinRows[role_index])) {
            if (visible) {
                lv_obj_clear_flag(_loraPinRows[role_index], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(_loraPinRows[role_index], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if ((_loraPinDropdowns[role_index] != nullptr) && lv_obj_ready(_loraPinDropdowns[role_index])) {
            lv_dropdown_set_selected(_loraPinDropdowns[role_index],
                                     lora_gpio_choice_index(lora_pin_value_for_role(state.settings, role)));
            set_disabled(_loraPinDropdowns[role_index], local_controller_active);
        }
    }

    set_disabled(_loraModuleDropdown, false);
    set_disabled(_loraDisplayNameTextArea, false);
    set_disabled(_loraCommonChatTitleTextArea, false);
    set_disabled(_loraFrequencyTextArea, false);
    set_disabled(_loraSpreadingFactorTextArea, false);
    set_disabled(_loraBandwidthTextArea, false);
    set_disabled(_loraCodingRateTextArea, false);
    set_disabled(_loraHopLimitTextArea, false);
    set_disabled(_loraForwardingSwitch, false);
    set_disabled(_loraEncryptionSwitch, false);
    set_disabled(_loraSelfCheckButton, !radio_enabled);
    refreshLoRaSelfCheckStatus();

    if (local_controller_active) {
        lv_label_set_text(_loraInfoLabel,
                          radio_enabled
                              ? "LoRa radio is enabled. Local Controller has been turned off to avoid conflicts with haptics and Neopixel on shared GPIOs."
                              : "Local Controller is active. Turning LoRa on will disable Local Controller automatically to avoid GPIO conflicts.");
    } else if (is_imu_enabled()) {
        lv_label_set_text(_loraInfoLabel,
                          "IMU is active. Turning LoRa on will disable IMU because the default BMI160 wiring overlaps the E22 JP1 pin profile.");
    } else if (!state.settings.radio_enabled) {
        lv_label_set_text(_loraInfoLabel,
                          "LoRa radio is disabled. Mesh settings stay saved so you can re-enable it later.");
    } else {
        lv_label_set_text(_loraInfoLabel,
                          "LoRa radio is enabled. Reopen the LoRa Mesh app after changing pins or modulation so it reinitializes the radio.");
    }
#endif
}

bool AppSettings::persistLoRaConfigFromUi(void)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    return false;
#else
    jc4880::lora_mesh::StoredState state = {};
    if (!jc4880::lora_mesh::load_stored_state(state)) {
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel, "Failed to load LoRa settings.");
        }
        return false;
    }

    auto parse_uint = [](lv_obj_t *textarea, uint32_t min_value, uint32_t max_value, uint32_t *value_out) {
        if ((textarea == nullptr) || (value_out == nullptr)) {
            return false;
        }
        const char *text = lv_textarea_get_text(textarea);
        if ((text == nullptr) || (*text == '\0')) {
            return false;
        }
        char *end = nullptr;
        const unsigned long parsed = strtoul(text, &end, 10);
        if ((end == text) || (*end != '\0') || (parsed < min_value) || (parsed > max_value)) {
            return false;
        }
        *value_out = static_cast<uint32_t>(parsed);
        return true;
    };

    uint32_t frequency_hz = 0;
    uint32_t spreading_factor = 0;
    uint32_t bandwidth = 0;
    uint32_t coding_rate = 0;
    uint32_t hop_limit = 0;
    if (!parse_uint(_loraFrequencyTextArea, 400000000U, 1000000000U, &frequency_hz) ||
        !parse_uint(_loraSpreadingFactorTextArea, 5U, 12U, &spreading_factor) ||
        !parse_uint(_loraBandwidthTextArea, 0U, 9U, &bandwidth) ||
        !parse_uint(_loraCodingRateTextArea, 1U, 4U, &coding_rate) ||
        !parse_uint(_loraHopLimitTextArea, 1U, 16U, &hop_limit)) {
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel, "LoRa settings contain invalid numeric values.");
        }
        return false;
    }

    bool local_controller_active = is_local_controller_backend_active();
    const auto module = lora_radio_module_from_dropdown(lv_dropdown_get_selected(_loraModuleDropdown));
    if (lv_obj_has_state(_loraEnabledSwitch, LV_STATE_CHECKED) && local_controller_active) {
        if (!disableLocalControllerForLoRa()) {
            if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
                lv_label_set_text(_loraInfoLabel, "Failed to disable Local Controller before enabling LoRa.");
            }
            return false;
        }
        local_controller_active = false;
    }
    state.identity.display_name = lv_textarea_get_text(_loraDisplayNameTextArea);
    state.settings.common_chat_name = lv_textarea_get_text(_loraCommonChatTitleTextArea);
    if (state.identity.display_name.empty()) {
        state.identity.display_name = std::string("P4-") + state.identity.device_id.substr(0, 4);
    }
    if (state.settings.common_chat_name.empty()) {
        state.settings.common_chat_name = "Common Mesh Chat";
    }
    state.settings.radio_enabled = !local_controller_active && lv_obj_has_state(_loraEnabledSwitch, LV_STATE_CHECKED);
    state.settings.radio_module = module;
    state.settings.frequency_hz = frequency_hz;
    state.settings.spreading_factor = static_cast<uint8_t>(spreading_factor);
    state.settings.bandwidth = static_cast<uint8_t>(bandwidth);
    state.settings.coding_rate = static_cast<uint8_t>(coding_rate);
    state.settings.hop_limit = static_cast<uint8_t>(hop_limit);
    state.settings.forwarding_enabled = lv_obj_has_state(_loraForwardingSwitch, LV_STATE_CHECKED);
    state.settings.public_chat_encryption = lv_obj_has_state(_loraEncryptionSwitch, LV_STATE_CHECKED);

    std::vector<int8_t> active_pins;
    active_pins.reserve(kLoraPinRoleCount);
    for (size_t role_index = 0; role_index < kLoraPinRoleCount; ++role_index) {
        const auto role = static_cast<LoraPinRole>(role_index);
        const int8_t value = lora_gpio_choice_value(lv_dropdown_get_selected(_loraPinDropdowns[role_index]));
        lora_set_pin_value_for_role(state.settings, role, value);
        if (!lora_module_uses_role(module, role)) {
            continue;
        }
        if (value < 0) {
            if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
                lv_label_set_text(_loraInfoLabel, "Each active LoRa signal must have a GPIO assigned.");
            }
            return false;
        }
        if (std::find(active_pins.begin(), active_pins.end(), value) != active_pins.end()) {
            if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
                lv_label_set_text(_loraInfoLabel, "Each visible LoRa signal must use a unique GPIO.");
            }
            return false;
        }
        active_pins.push_back(value);
    }

    if (!jc4880::lora_mesh::save_stored_state(state)) {
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel, "Failed to save LoRa settings.");
        }
        return false;
    }

    refreshLoRaUi();
    if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
        lv_label_set_text(_loraInfoLabel,
                          local_controller_active
                              ? "Saved. Local Controller is active, so the LoRa radio remains forced off."
                              : "Saved. Reopen the LoRa Mesh app if it is already open so the radio picks up these settings.");
    }
    return true;
#endif
}

bool AppSettings::persistLoRaRadioEnabledFromUi(void)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    return false;
#else
    jc4880::lora_mesh::StoredState state = {};
    if (!jc4880::lora_mesh::load_stored_state(state)) {
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel, "Failed to load LoRa settings.");
        }
        return false;
    }

    bool local_controller_active = is_local_controller_backend_active();
    const bool radio_enabled = lv_obj_has_state(_loraEnabledSwitch, LV_STATE_CHECKED);
    if (radio_enabled && local_controller_active) {
        if (!disableLocalControllerForLoRa()) {
            if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
                lv_label_set_text(_loraInfoLabel, "Failed to disable Local Controller before enabling LoRa.");
            }
            return false;
        }
        local_controller_active = false;
    }
    if (radio_enabled && is_imu_enabled() && !disableImuForLoRa()) {
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel, "Failed to disable IMU before enabling LoRa.");
        }
        return false;
    }

    state.settings.radio_enabled = radio_enabled && !local_controller_active;
    if (!jc4880::lora_mesh::save_stored_state(state)) {
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel, "Failed to save LoRa settings.");
        }
        return false;
    }

    if ((_loraSelfCheckButton != nullptr) && lv_obj_ready(_loraSelfCheckButton)) {
        if (state.settings.radio_enabled) {
            lv_obj_clear_state(_loraSelfCheckButton, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(_loraSelfCheckButton, LV_STATE_DISABLED);
        }
    }
    refreshLoRaSelfCheckStatus();

    if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
        if (state.settings.radio_enabled) {
            lv_label_set_text(_loraInfoLabel,
                              "LoRa radio enabled. IMU and Local Controller are forced off while LoRa owns its JP1 pins. Other LoRa settings still require Save if you change them.");
        } else {
            lv_label_set_text(_loraInfoLabel,
                              "LoRa radio disabled. Other LoRa settings stay saved until you change them and press Save.");
        }
    }

    return true;
#endif
}

void AppSettings::disableLoRaRadioForLocalController(void)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    return;
#else
    jc4880::lora_mesh::StoredState state = {};
    if (!jc4880::lora_mesh::load_stored_state(state)) {
        return;
    }
    if (state.settings.radio_enabled) {
        state.settings.radio_enabled = false;
        jc4880::lora_mesh::save_stored_state(state);
    }
    refreshLoRaUi();
#endif
}

bool AppSettings::disableLocalControllerForLoRa(void)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    return false;
#else
    jc4880_joypad_config_t config = {};
    if (!jc4880_joypad_get_config(&config)) {
        return false;
    }
    if (config.backend != JC4880_JOYPAD_BACKEND_MANUAL) {
        return true;
    }

    config.backend = JC4880_JOYPAD_BACKEND_DISABLED;
    if (!jc4880_joypad_set_config(&config)) {
        return false;
    }

    if ((_joypadManualActiveSwitch != nullptr) && lv_obj_ready(_joypadManualActiveSwitch)) {
        lv_obj_clear_state(_joypadManualActiveSwitch, LV_STATE_CHECKED);
    }
    applyNeopixelConfig();
    refreshJoypadUi();
    return true;
#endif
}

void AppSettings::refreshLoRaSelfCheckStatus(void)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    return;
#else
    if ((_loraSelfCheckStatusLabel == nullptr) || !lv_obj_ready(_loraSelfCheckStatusLabel)) {
        return;
    }

    jc4880::lora_mesh::StoredState state = {};
    jc4880::lora_mesh::load_stored_state(state);
    if (!state.settings.radio_enabled) {
        lv_label_set_text(_loraSelfCheckStatusLabel, "Self check unavailable while the LoRa radio is disabled.");
        stopLoRaSelfCheckStatusPolling();
        return;
    }

    LoRaMeshApp *lora_app = find_installed_lora_mesh_app(getCore());
    if (lora_app == nullptr) {
        lv_label_set_text(_loraSelfCheckStatusLabel, "Self check unavailable because the LoRa app is not installed.");
        stopLoRaSelfCheckStatusPolling();
        return;
    }

    bool self_test_running = false;
    bool self_test_ran = false;
    const std::string self_test_status = lora_app->getSelfTestStatus(&self_test_running, &self_test_ran);
    if (self_test_running) {
        lv_label_set_text(_loraSelfCheckStatusLabel, self_test_status.c_str());
        startLoRaSelfCheckStatusPolling();
        return;
    }
    if (self_test_ran && !self_test_status.empty() && (self_test_status != "Mode: idle")) {
        lv_label_set_text(_loraSelfCheckStatusLabel, self_test_status.c_str());
        stopLoRaSelfCheckStatusPolling();
        return;
    }

    lv_label_set_text(_loraSelfCheckStatusLabel, "Self check ready. Tap Run LoRa Self Check to run diagnostics here.");
    stopLoRaSelfCheckStatusPolling();
#endif
}

void AppSettings::startLoRaSelfCheckStatusPolling(void)
{
    if (_loraSelfCheckStatusTimer != nullptr) {
        return;
    }

    _loraSelfCheckStatusTimer = lv_timer_create(onLoRaSelfCheckStatusTimerCallback, 250, this);
}

void AppSettings::stopLoRaSelfCheckStatusPolling(void)
{
    if (_loraSelfCheckStatusTimer != nullptr) {
        lv_timer_del(_loraSelfCheckStatusTimer);
        _loraSelfCheckStatusTimer = nullptr;
    }
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

    lv_obj_t *otaAutoUpdateRow = lv_obj_create(currentSection);
    lv_obj_set_width(otaAutoUpdateRow, lv_pct(100));
    lv_obj_set_height(otaAutoUpdateRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(otaAutoUpdateRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(otaAutoUpdateRow, 16, 0);
    lv_obj_set_style_border_width(otaAutoUpdateRow, 0, 0);
    lv_obj_set_style_bg_color(otaAutoUpdateRow, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_opa(otaAutoUpdateRow, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(otaAutoUpdateRow, 14, 0);
    lv_obj_set_style_pad_right(otaAutoUpdateRow, 14, 0);
    lv_obj_set_style_pad_top(otaAutoUpdateRow, 12, 0);
    lv_obj_set_style_pad_bottom(otaAutoUpdateRow, 12, 0);

    lv_obj_t *otaAutoUpdateTitle = lv_label_create(otaAutoUpdateRow);
    lv_label_set_text(otaAutoUpdateTitle, "Auto Update");
    lv_obj_set_style_text_font(otaAutoUpdateTitle, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(otaAutoUpdateTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(otaAutoUpdateTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *otaAutoUpdateDetail = lv_label_create(otaAutoUpdateRow);
    lv_obj_set_width(otaAutoUpdateDetail, 240);
    lv_label_set_long_mode(otaAutoUpdateDetail, LV_LABEL_LONG_WRAP);
    lv_label_set_text(otaAutoUpdateDetail, "Automatically start the preferred OTA release when a new update is detected.");
    lv_obj_set_style_text_font(otaAutoUpdateDetail, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(otaAutoUpdateDetail, lv_color_hex(0x475569), 0);
    lv_obj_align_to(otaAutoUpdateDetail, otaAutoUpdateTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    _firmwareAutoUpdateSwitch = lv_switch_create(otaAutoUpdateRow);
    lv_obj_align(_firmwareAutoUpdateSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_firmwareAutoUpdateSwitch, onFirmwareAutoUpdateSwitchValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);
    if (_nvs_param_map[NVS_KEY_OTA_AUTO_UPDATE] != 0) {
        lv_obj_add_state(_firmwareAutoUpdateSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(_firmwareAutoUpdateSwitch, LV_STATE_CHECKED);
    }

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

    refreshDeferredOtaScheduleState();

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
    auto ignore_wifi_reset_error = [](esp_err_t err) {
        return (err == ESP_OK) || (err == ESP_ERR_WIFI_NOT_INIT) || (err == ESP_ERR_WIFI_NOT_STARTED) ||
               (err == ESP_ERR_WIFI_STATE) || (err == ESP_ERR_WIFI_CONN);
    };

    stopWifiScan();
    ok &= ignore_wifi_reset_error(esp_wifi_disconnect());
    ok &= ignore_wifi_reset_error(esp_wifi_stop());
    ok &= ignore_wifi_reset_error(esp_wifi_restore());

    if (remove(kCrashReportLocalPath) != 0 && errno != ENOENT) {
        ESP_LOGW(TAG, "Failed to remove %s", kCrashReportLocalPath);
        ok = false;
    }
    if (remove(kCrashReportPendingPath) != 0 && errno != ENOENT) {
        ESP_LOGW(TAG, "Failed to remove %s", kCrashReportPendingPath);
        ok = false;
    }

    if (esp_core_dump_image_erase() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to erase coredump image during factory reset");
        ok = false;
    }

    if (esp_spiffs_format(CONFIG_BSP_SPIFFS_PARTITION_LABEL) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to format SPIFFS partition during factory reset");
        ok = false;
    }

    nvs_flash_deinit();
    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) erasing NVS partition during factory reset", esp_err_to_name(err));
        return false;
    }

    err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) reinitializing NVS after factory reset", esp_err_to_name(err));
        return false;
    }

    initializeDefaultNvsParams();
    _hasAutoDetectedTimezone = false;
    _autoTimezoneRefreshPending = false;
    loadNvsParam();
    _autoDetectedTimezoneOffsetMinutes = _nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE];
    _autoTimezoneStatus.clear();

    jc_ui_tap_sound_set_enabled(_nvs_param_map[NVS_KEY_AUDIO_TAP_SOUND] != 0);
    jc_ui_haptic_feedback_set_enabled(_nvs_param_map[NVS_KEY_AUDIO_HAPTIC_FEEDBACK] != 0);
    jc_ui_haptic_feedback_set_gpio(_nvs_param_map[NVS_KEY_AUDIO_HAPTIC_GPIO]);
    jc_ui_haptic_feedback_set_level(_nvs_param_map[NVS_KEY_AUDIO_HAPTIC_LEVEL]);
    applyManualTimezonePreference();
    bsp_extra_audio_media_volume_set(_nvs_param_map[NVS_KEY_AUDIO_VOLUME]);
    bsp_extra_audio_system_volume_set(_nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME]);
    bsp_display_brightness_set(_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS]);
    applyDisplayIdleSettings();
    applyNeopixelConfig();
    updateUiByNvsParam();
    setFirmwareStatus(FACTORY_RESET_STATUS_RESTARTING, !ok);

    esp_restart();
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

    if (lv_obj_ready(_displayTimeoffInGameSwitch)) {
        if (_nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF_IN_GAME]) {
            lv_obj_add_state(_displayTimeoffInGameSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_displayTimeoffInGameSwitch, LV_STATE_CHECKED);
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

    if (lv_obj_ready(_displayOrientationDropdown)) {
        lv_dropdown_set_selected(_displayOrientationDropdown,
                                 findDropdownIndexForValue(kDisplayOrientationOptionsDeg,
                                                           sizeof(kDisplayOrientationOptionsDeg) / sizeof(kDisplayOrientationOptionsDeg[0]),
                                                           sanitizeDisplayOrientationDegrees(_nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION])));
    }

    refreshDisplayAutorotateUi();

    #if CONFIG_JC4880_FEATURE_TIME_SYNC
    refreshTimezoneUi();
    #endif
}

void AppSettings::refreshDisplayAutorotateUi(void)
{
#if !APP_SETTINGS_FEATURE_DISPLAY_MENU
    return;
#endif
    if (!isUiActive()) {
        return;
    }

    const bool enabled = _nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE] != 0;
    const int32_t rotation_axis = sanitizeDisplayAutorotateAxis(_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_IMU]);

    if (lv_obj_ready(_displayAutorotateSwitch)) {
        if (enabled) {
            lv_obj_add_state(_displayAutorotateSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_displayAutorotateSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_displayAutorotateImuDropdown)) {
        lv_dropdown_set_selected(_displayAutorotateImuDropdown,
                                 findDropdownIndexForValue(kDisplayAutorotateAxisOptions,
                                                           sizeof(kDisplayAutorotateAxisOptions) / sizeof(kDisplayAutorotateAxisOptions[0]),
                                                           rotation_axis));
        if (enabled) {
            lv_obj_clear_state(_displayAutorotateImuDropdown, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(_displayAutorotateImuDropdown, LV_STATE_DISABLED);
        }
    }

    if (lv_obj_ready(_displayAutorotateInfoLabel)) {
        const char *axis_name = "X";
        std::string info_text;
        switch (rotation_axis) {
        case 1:
            axis_name = "Y";
            break;
        case 2:
            axis_name = "Z";
            break;
        default:
            break;
        }
        if (enabled) {
            info_text = std::string("Autorotate is live. The display flips from the selected ") + axis_name +
                        " axis between the saved orientation and its 180 degree opposite.";
        } else {
            info_text = std::string("Autorotate is off. When enabled, the ") + axis_name +
                        " axis drives a 180 degree UI flip.";
        }
        lv_label_set_text(_displayAutorotateInfoLabel, info_text.c_str());
    }
}

void AppSettings::updateDisplayAutorotateFromSample(const jc4880::imu::ImuSample *sample, bool sample_ok)
{
#if !APP_SETTINGS_FEATURE_DISPLAY_MENU
    (void)sample;
    (void)sample_ok;
    return;
#else
    const int32_t base_orientation = sanitizeDisplayOrientationDegrees(_nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION]);
    if (_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE] == 0) {
        if (!_displayAutorotateHasAppliedOrientation || (_displayAutorotateAppliedOrientation != base_orientation)) {
            if (applyDisplayOrientationLive(base_orientation)) {
                _displayAutorotateAppliedOrientation = base_orientation;
                _displayAutorotateHasAppliedOrientation = true;
            }
        }
        return;
    }

    if ((sample == nullptr) || !sample_ok || !sample->hasAccel) {
        return;
    }

    const int32_t rotation_axis = sanitizeDisplayAutorotateAxis(_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_IMU]);
    float selected_angle = sample->roll;
    switch (rotation_axis) {
    case 1:
        selected_angle = sample->pitch;
        break;
    case 2:
        selected_angle = sample->yaw;
        break;
    default:
        selected_angle = sample->roll;
        break;
    }
    const int32_t inverted_orientation = oppositeDisplayOrientationDegrees(base_orientation);
    const bool currently_inverted = _displayAutorotateHasAppliedOrientation && (_displayAutorotateAppliedOrientation == inverted_orientation);
    int32_t target_orientation = currently_inverted ? inverted_orientation : base_orientation;

    if (currently_inverted) {
        if (std::fabs(selected_angle) <= 70.0f) {
            target_orientation = base_orientation;
        }
    } else if (std::fabs(selected_angle) >= 110.0f) {
        target_orientation = inverted_orientation;
    }

    if (!_displayAutorotateHasAppliedOrientation || (target_orientation != _displayAutorotateAppliedOrientation)) {
        if (applyDisplayOrientationLive(target_orientation)) {
            _displayAutorotateAppliedOrientation = target_orientation;
            _displayAutorotateHasAppliedOrientation = true;
        }
    }
#endif
}

void AppSettings::requestDisplayOrientationPreview(int32_t orientation_degrees)
{
#if !APP_SETTINGS_FEATURE_DISPLAY_MENU
    (void)orientation_degrees;
    return;
#else
    const int32_t sanitized_orientation = sanitizeDisplayOrientationDegrees(orientation_degrees);
    const int32_t previous_orientation = sanitizeDisplayOrientationDegrees(_nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION]);
    if (sanitized_orientation == previous_orientation) {
        return;
    }

    if (_displayOrientationPreviewMsgbox != nullptr) {
        finishDisplayOrientationPreview(false);
    }

    if ((sanitized_orientation != orientation_degrees) || !applyDisplayOrientationLive(sanitized_orientation)) {
        ESP_LOGW(TAG, "Failed to apply live display orientation %ld", static_cast<long>(orientation_degrees));
        return;
    }

    _displayOrientationPreviewPrevious = previous_orientation;
    _displayOrientationPreviewPending = sanitized_orientation;
    _displayOrientationPreviewSecondsRemaining = kDisplayOrientationPreviewSeconds;
    _displayOrientationPreviewResolving = false;
    _nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION] = sanitized_orientation;

    setNvsParam(NVS_KEY_DISPLAY_ORIENTATION_PENDING, sanitized_orientation);
    setNvsParam(NVS_KEY_DISPLAY_ORIENTATION_PREVIOUS, previous_orientation);
    setNvsParam(NVS_KEY_DISPLAY_ORIENTATION_STATE, 1);

    static const char *buttons[] = {"Cancel", "OK", ""};
    _displayOrientationPreviewMsgbox = lv_msgbox_create(lv_layer_top(), "Display Orientation",
                                                        kDisplayOrientationPreviewInitialText, buttons, false);
    if ((_displayOrientationPreviewMsgbox == nullptr) || !lv_obj_is_valid(_displayOrientationPreviewMsgbox)) {
        _displayOrientationPreviewMsgbox = nullptr;
        finishDisplayOrientationPreview(false);
        return;
    }

    _displayOrientationPreviewLabel = lv_msgbox_get_text(_displayOrientationPreviewMsgbox);
    const lv_coord_t display_width = lv_disp_get_hor_res(nullptr);
    const lv_coord_t preview_msgbox_width = std::min<lv_coord_t>(340, std::max<lv_coord_t>(260, display_width - 96));
    lv_obj_set_width(_displayOrientationPreviewMsgbox, preview_msgbox_width);
    lv_obj_set_style_pad_all(_displayOrientationPreviewMsgbox, 16, 0);
    lv_obj_set_style_pad_row(_displayOrientationPreviewMsgbox, 12, 0);
    if (lv_obj_ready(_displayOrientationPreviewLabel)) {
        lv_obj_set_width(_displayOrientationPreviewLabel, lv_pct(100));
        lv_label_set_long_mode(_displayOrientationPreviewLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(_displayOrientationPreviewLabel, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_t *preview_content = lv_msgbox_get_content(_displayOrientationPreviewMsgbox);
    if (lv_obj_ready(preview_content)) {
        lv_obj_set_flex_flow(preview_content, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(preview_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(preview_content, 12, 0);

        _displayOrientationPreviewSpinner = lv_spinner_create(preview_content, 1000, 80);
        if (lv_obj_ready(_displayOrientationPreviewSpinner)) {
            lv_obj_set_size(_displayOrientationPreviewSpinner, 86, 86);
            lv_obj_clear_flag(_displayOrientationPreviewSpinner, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_arc_width(_displayOrientationPreviewSpinner, 8, LV_PART_MAIN);
            lv_obj_set_style_arc_width(_displayOrientationPreviewSpinner, 8, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(_displayOrientationPreviewSpinner, lv_color_hex(0xD7DCE6), LV_PART_MAIN);
            lv_obj_set_style_arc_color(_displayOrientationPreviewSpinner, lv_color_hex(0x2563EB), LV_PART_INDICATOR);

            _displayOrientationPreviewCountdownLabel = lv_label_create(_displayOrientationPreviewSpinner);
            if (lv_obj_ready(_displayOrientationPreviewCountdownLabel)) {
                lv_obj_set_style_text_align(_displayOrientationPreviewCountdownLabel, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_set_style_text_font(_displayOrientationPreviewCountdownLabel, &lv_font_montserrat_28, 0);
                lv_obj_center(_displayOrientationPreviewCountdownLabel);
            }
        }
    }
    lv_obj_t *preview_buttons = lv_msgbox_get_btns(_displayOrientationPreviewMsgbox);
    if (lv_obj_ready(preview_buttons)) {
        lv_obj_set_size(preview_buttons, lv_pct(100), 54);
        lv_btnmatrix_set_btn_width(preview_buttons, 0, 2);
        lv_btnmatrix_set_btn_width(preview_buttons, 1, 3);
        lv_obj_set_style_pad_all(preview_buttons, 4, 0);
        lv_obj_set_style_pad_column(preview_buttons, 10, 0);
        lv_obj_set_style_radius(preview_buttons, 6, LV_PART_ITEMS);
    }
    lv_obj_center(_displayOrientationPreviewMsgbox);
    lv_obj_add_event_cb(_displayOrientationPreviewMsgbox, onDisplayOrientationPreviewPopupEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(_displayOrientationPreviewMsgbox, onDisplayOrientationPreviewPopupEventCallback,
                        LV_EVENT_DELETE, this);
    updateDisplayOrientationPreviewPopup();

    _displayOrientationPreviewTimer = lv_timer_create(onDisplayOrientationPreviewTimerCallback, 1000, this);
    if (_displayOrientationPreviewTimer == nullptr) {
        finishDisplayOrientationPreview(false);
    }
#endif
}

void AppSettings::updateDisplayOrientationPreviewPopup(void)
{
#if APP_SETTINGS_FEATURE_DISPLAY_MENU
    if (lv_obj_ready(_displayOrientationPreviewLabel)) {
        lv_label_set_text(_displayOrientationPreviewLabel, kDisplayOrientationPreviewInitialText);
    }

    if (lv_obj_ready(_displayOrientationPreviewCountdownLabel)) {
        char countdown[8] = {0};
        snprintf(countdown, sizeof(countdown), "%ld", static_cast<long>(_displayOrientationPreviewSecondsRemaining));
        lv_label_set_text(_displayOrientationPreviewCountdownLabel, countdown);
        lv_obj_center(_displayOrientationPreviewCountdownLabel);
    }
#endif
}

void AppSettings::finishDisplayOrientationPreview(bool keep_orientation)
{
#if APP_SETTINGS_FEATURE_DISPLAY_MENU
    if (_displayOrientationPreviewTimer != nullptr) {
        lv_timer_del(_displayOrientationPreviewTimer);
        _displayOrientationPreviewTimer = nullptr;
    }

    const int32_t final_orientation = keep_orientation
                                          ? sanitizeDisplayOrientationDegrees(_displayOrientationPreviewPending)
                                          : sanitizeDisplayOrientationDegrees(_displayOrientationPreviewPrevious);
    applyDisplayOrientationLive(final_orientation);
    _nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION] = final_orientation;
    setNvsParam(NVS_KEY_DISPLAY_ORIENTATION, final_orientation);
    setNvsParam(NVS_KEY_DISPLAY_ORIENTATION_PENDING, final_orientation);
    setNvsParam(NVS_KEY_DISPLAY_ORIENTATION_PREVIOUS, final_orientation);
    setNvsParam(NVS_KEY_DISPLAY_ORIENTATION_STATE, 0);

    if (lv_obj_ready(_displayOrientationDropdown)) {
        lv_dropdown_set_selected(_displayOrientationDropdown,
                                 findDropdownIndexForValue(kDisplayOrientationOptionsDeg,
                                                           sizeof(kDisplayOrientationOptionsDeg) / sizeof(kDisplayOrientationOptionsDeg[0]),
                                                           final_orientation));
    }

    _displayOrientationPreviewResolving = true;
    if (lv_obj_ready(_displayOrientationPreviewMsgbox)) {
        lv_msgbox_close_async(_displayOrientationPreviewMsgbox);
    } else {
        _displayOrientationPreviewMsgbox = nullptr;
        _displayOrientationPreviewLabel = nullptr;
        _displayOrientationPreviewSpinner = nullptr;
        _displayOrientationPreviewCountdownLabel = nullptr;
        _displayOrientationPreviewSecondsRemaining = 0;
        _displayOrientationPreviewResolving = false;
    }
#else
    (void)keep_orientation;
#endif
}

void AppSettings::applyNeopixelConfig(void)
{
#if !APP_SETTINGS_FEATURE_DISPLAY_MENU
    return;
#else
    jc4880_joypad_config_t joypad_config = {};
    const bool local_controller_active = jc4880_joypad_get_config(&joypad_config) &&
                                         (joypad_config.backend == JC4880_JOYPAD_BACKEND_MANUAL);
    const int32_t neopixel_gpio = sanitizeNeopixelGpio(_nvs_param_map[NVS_KEY_NEOPIXEL_GPIO]);
    jc4880_neopixel_apply_config((_nvs_param_map[NVS_KEY_NEOPIXEL_POWER] != 0) && local_controller_active,
                                 neopixel_gpio,
                                 std::clamp(static_cast<int32_t>(_nvs_param_map[NVS_KEY_NEOPIXEL_BRIGHTNESS]), static_cast<int32_t>(NEOPIXEL_BRIGHTNESS_MIN), static_cast<int32_t>(NEOPIXEL_BRIGHTNESS_MAX)),
                                 std::clamp(static_cast<int32_t>(_nvs_param_map[NVS_KEY_NEOPIXEL_PALETTE]), static_cast<int32_t>(0), static_cast<int32_t>(11)),
                                 std::clamp(static_cast<int32_t>(_nvs_param_map[NVS_KEY_NEOPIXEL_EFFECT]), static_cast<int32_t>(0), static_cast<int32_t>(20)));
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
                    label += " С‚РђРІ " + _autoTimezoneStatus;
                }
            } else if (!_autoTimezoneStatus.empty()) {
                label = _autoTimezoneStatus + " С‚РђРІ fallback " + manual_option.label;
            } else {
                label = std::string("Auto timezone enabled С‚РђРІ fallback ") + manual_option.label;
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
    const bool delegated_to_joypad = bluetoothMenuDelegatesToJoypadBle();

    if (lv_obj_ready(ui_SwitchPanelScreenSettingBLESwitch)) {
        if (delegated_to_joypad) {
            lv_obj_clear_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_CHECKED);
            lv_obj_add_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_DISABLED);
        } else if (bluetooth_enabled) {
            lv_obj_add_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_CHECKED);
            lv_obj_clear_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_CHECKED);
            lv_obj_clear_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_DISABLED);
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

        if (delegated_to_joypad) {
            lv_obj_add_state(_bluetoothNameTextArea, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(_bluetoothNameTextArea, LV_STATE_DISABLED);
        }
    }

    if (lv_obj_ready(_bluetoothNameSaveButton)) {
        if (delegated_to_joypad) {
            lv_obj_add_state(_bluetoothNameSaveButton, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(_bluetoothNameSaveButton, LV_STATE_DISABLED);
        }
    }

    if (lv_obj_ready(_bluetoothScanButtonLabel)) {
        lv_label_set_text(_bluetoothScanButtonLabel,
                          delegated_to_joypad ? "Use Joypad Menu" : (s_bleScanInProgress ? "Stop Scan" : "Scan Nearby"));
    }

    if (lv_obj_ready(_bluetoothScanButton)) {
        if (delegated_to_joypad) {
            lv_obj_add_state(_bluetoothScanButton, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(_bluetoothScanButton, LV_STATE_DISABLED);
        }
    }

    if (lv_obj_ready(_bluetoothScanStatusLabel)) {
        if (delegated_to_joypad) {
            lv_label_set_text(_bluetoothScanStatusLabel,
                              "Open Settings > Joypad to enable Bluepad32, pair a controller, or manage discovery on the ESP32-C6.");
        } else if (!bluetooth_enabled) {
            lv_label_set_text(_bluetoothScanStatusLabel, "Enable BLE first to scan nearby devices.");
        } else {
            lv_label_set_text(_bluetoothScanStatusLabel, s_bleScanStatus.c_str());
        }
    }

    if (lv_obj_ready(_bluetoothScanResultsLabel)) {
        std::string results;
        if (delegated_to_joypad) {
            results = "The Joypad screen owns controller pairing and stored-controller selection when Joypad BLE is active.";
        } else if (s_bleScanResults.empty()) {
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

#if CONFIG_JC4880_FEATURE_OTA
    if (!_otaStatusIconInstalled) {
        ESP_Brookesia_StatusBarIconData_t ota_icon = {
            .size = {
                .width = 18,
                .height = 18,
            },
            .icon = {
                .image_num = 1,
                .images = {
                    ESP_BROOKESIA_STYLE_IMAGE_RECOLOR_WHITE(&ui_img_ota_status_png),
                },
            },
        };

        _otaStatusIconInstalled = mutable_status_bar->addIcon(
            ota_icon, ESP_BROOKESIA_STATUS_BAR_DATA_AREA_NUM_MAX - 1, kStatusBarOtaIconId
        );
    }

    if (_otaStatusIconInstalled) {
        mutable_status_bar->setIconState(kStatusBarOtaIconId, _otaUpdateAvailableThisBoot ? 0 : -1);
    }
#endif
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
    if (lv_obj_ready(_otaUpdateProgressStatusLabel)) {
        lv_label_set_text(_otaUpdateProgressStatusLabel, status.c_str());
        lv_obj_set_style_text_color(_otaUpdateProgressStatusLabel, is_error ? lv_color_hex(0xB91C1C) : lv_color_hex(0x334155), 0);
    }

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

void AppSettings::ensureOtaUpdateProgressOverlay(void)
{
    if ((_otaUpdateProgressOverlay != nullptr) && lv_obj_ready(_otaUpdateProgressOverlay)) {
        return;
    }

    _otaUpdateProgressOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_otaUpdateProgressOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(_otaUpdateProgressOverlay, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(_otaUpdateProgressOverlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_otaUpdateProgressOverlay, 0, 0);
    lv_obj_set_style_pad_all(_otaUpdateProgressOverlay, 0, 0);
    lv_obj_add_flag(_otaUpdateProgressOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_otaUpdateProgressOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_otaUpdateProgressOverlay, onOtaUpdateProgressCloseEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_center(_otaUpdateProgressOverlay);
    lv_obj_add_flag(_otaUpdateProgressOverlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *card = lv_obj_create(_otaUpdateProgressOverlay);
    lv_obj_set_size(card, 340, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_style_pad_row(card, 14, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _otaUpdateProgressCornerCloseButton = lv_btn_create(card);
    lv_obj_set_size(_otaUpdateProgressCornerCloseButton, 36, 36);
    lv_obj_align(_otaUpdateProgressCornerCloseButton, LV_ALIGN_TOP_RIGHT, 8, -8);
    lv_obj_set_style_radius(_otaUpdateProgressCornerCloseButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_otaUpdateProgressCornerCloseButton, 0, 0);
    lv_obj_set_style_bg_color(_otaUpdateProgressCornerCloseButton, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_bg_opa(_otaUpdateProgressCornerCloseButton, LV_OPA_COVER, 0);
    lv_obj_add_flag(_otaUpdateProgressCornerCloseButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_otaUpdateProgressCornerCloseButton, onOtaUpdateProgressCloseEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t *cornerCloseLabel = lv_label_create(_otaUpdateProgressCornerCloseButton);
    lv_label_set_text(cornerCloseLabel, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(cornerCloseLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(cornerCloseLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_center(cornerCloseLabel);

    lv_obj_t *titleLabel = lv_label_create(card);
    lv_label_set_text(titleLabel, "OTA update");
    lv_obj_set_width(titleLabel, lv_pct(100));
    lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x0F172A), 0);

    _otaUpdateProgressStatusLabel = lv_label_create(card);
    lv_obj_set_width(_otaUpdateProgressStatusLabel, lv_pct(100));
    lv_label_set_long_mode(_otaUpdateProgressStatusLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_otaUpdateProgressStatusLabel, "Preparing OTA update...");
    lv_obj_set_style_text_align(_otaUpdateProgressStatusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(_otaUpdateProgressStatusLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(_otaUpdateProgressStatusLabel, lv_color_hex(0x334155), 0);

    _otaUpdateProgressBar = lv_bar_create(card);
    lv_obj_set_width(_otaUpdateProgressBar, lv_pct(100));
    lv_obj_set_height(_otaUpdateProgressBar, 18);
    lv_bar_set_range(_otaUpdateProgressBar, 0, 100);
    lv_bar_set_value(_otaUpdateProgressBar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(_otaUpdateProgressBar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_otaUpdateProgressBar, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_otaUpdateProgressBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_otaUpdateProgressBar, lv_color_hex(0x2563EB), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_otaUpdateProgressBar, LV_OPA_COVER, LV_PART_INDICATOR);

    _otaUpdateProgressLabel = lv_label_create(card);
    lv_obj_set_width(_otaUpdateProgressLabel, lv_pct(100));
    lv_label_set_long_mode(_otaUpdateProgressLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_otaUpdateProgressLabel, "Preparing OTA update...");
    lv_obj_set_style_text_align(_otaUpdateProgressLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(_otaUpdateProgressLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_otaUpdateProgressLabel, lv_color_hex(0x64748B), 0);

    _otaUpdateProgressActionRow = lv_obj_create(card);
    lv_obj_set_width(_otaUpdateProgressActionRow, lv_pct(100));
    lv_obj_set_height(_otaUpdateProgressActionRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(_otaUpdateProgressActionRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(_otaUpdateProgressActionRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_otaUpdateProgressActionRow, 0, 0);
    lv_obj_set_style_pad_all(_otaUpdateProgressActionRow, 0, 0);
    lv_obj_set_style_pad_column(_otaUpdateProgressActionRow, 8, 0);
    lv_obj_set_style_pad_row(_otaUpdateProgressActionRow, 8, 0);
    lv_obj_set_flex_flow(_otaUpdateProgressActionRow, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_add_flag(_otaUpdateProgressActionRow, LV_OBJ_FLAG_HIDDEN);

    _otaUpdateProgressInstallButton = lv_btn_create(_otaUpdateProgressActionRow);
    lv_obj_set_size(_otaUpdateProgressInstallButton, 100, 46);
    lv_obj_set_style_radius(_otaUpdateProgressInstallButton, 16, 0);
    lv_obj_set_style_border_width(_otaUpdateProgressInstallButton, 0, 0);
    lv_obj_set_style_bg_color(_otaUpdateProgressInstallButton, lv_color_hex(0x2563EB), 0);
    lv_obj_add_event_cb(_otaUpdateProgressInstallButton, onOtaUpdateInstallNowEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *installLabel = lv_label_create(_otaUpdateProgressInstallButton);
    lv_label_set_text(installLabel, "Install Now");
    lv_obj_set_style_text_font(installLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(installLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(installLabel);

    _otaUpdateProgressRescheduleButton = lv_btn_create(_otaUpdateProgressActionRow);
    lv_obj_set_size(_otaUpdateProgressRescheduleButton, 108, 46);
    lv_obj_set_style_radius(_otaUpdateProgressRescheduleButton, 16, 0);
    lv_obj_set_style_border_width(_otaUpdateProgressRescheduleButton, 0, 0);
    lv_obj_set_style_bg_color(_otaUpdateProgressRescheduleButton, lv_color_hex(0xCBD5E1), 0);
    lv_obj_add_event_cb(_otaUpdateProgressRescheduleButton, onOtaUpdateRescheduleEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *rescheduleLabel = lv_label_create(_otaUpdateProgressRescheduleButton);
    lv_label_set_text(rescheduleLabel, "Reschedule");
    lv_obj_set_style_text_font(rescheduleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(rescheduleLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_center(rescheduleLabel);

    _otaUpdateProgressCancelButton = lv_btn_create(_otaUpdateProgressActionRow);
    lv_obj_set_size(_otaUpdateProgressCancelButton, 92, 46);
    lv_obj_set_style_radius(_otaUpdateProgressCancelButton, 16, 0);
    lv_obj_set_style_border_width(_otaUpdateProgressCancelButton, 0, 0);
    lv_obj_set_style_bg_color(_otaUpdateProgressCancelButton, lv_color_hex(0xE2E8F0), 0);
    lv_obj_add_event_cb(_otaUpdateProgressCancelButton, onOtaUpdateCancelEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *cancelLabel = lv_label_create(_otaUpdateProgressCancelButton);
    lv_label_set_text(cancelLabel, "Cancel");
    lv_obj_set_style_text_font(cancelLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(cancelLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_center(cancelLabel);

    _otaUpdateReschedulePanel = lv_obj_create(card);
    lv_obj_set_width(_otaUpdateReschedulePanel, lv_pct(100));
    lv_obj_set_height(_otaUpdateReschedulePanel, LV_SIZE_CONTENT);
    lv_obj_clear_flag(_otaUpdateReschedulePanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_otaUpdateReschedulePanel, 16, 0);
    lv_obj_set_style_bg_color(_otaUpdateReschedulePanel, lv_color_hex(0xEFF6FF), 0);
    lv_obj_set_style_bg_opa(_otaUpdateReschedulePanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_otaUpdateReschedulePanel, 0, 0);
    lv_obj_set_style_pad_all(_otaUpdateReschedulePanel, 12, 0);
    lv_obj_set_style_pad_row(_otaUpdateReschedulePanel, 10, 0);
    lv_obj_set_flex_flow(_otaUpdateReschedulePanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(_otaUpdateReschedulePanel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *rescheduleHint = lv_label_create(_otaUpdateReschedulePanel);
    lv_obj_set_width(rescheduleHint, lv_pct(100));
    lv_label_set_long_mode(rescheduleHint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(rescheduleHint, "Delay automatic installation until a later time.");
    lv_obj_set_style_text_font(rescheduleHint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rescheduleHint, lv_color_hex(0x334155), 0);

    lv_obj_t *reschedulePickerRow = lv_obj_create(_otaUpdateReschedulePanel);
    lv_obj_set_width(reschedulePickerRow, lv_pct(100));
    lv_obj_set_height(reschedulePickerRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(reschedulePickerRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(reschedulePickerRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(reschedulePickerRow, 0, 0);
    lv_obj_set_style_pad_all(reschedulePickerRow, 0, 0);
    lv_obj_set_style_pad_column(reschedulePickerRow, 8, 0);
    lv_obj_set_flex_flow(reschedulePickerRow, LV_FLEX_FLOW_ROW);

    _otaUpdateRescheduleHourDropdown = lv_dropdown_create(reschedulePickerRow);
    lv_dropdown_set_options(_otaUpdateRescheduleHourDropdown, "0 hours\n1 hour\n2 hours\n4 hours\n8 hours\n12 hours\n24 hours");
    lv_dropdown_set_selected(_otaUpdateRescheduleHourDropdown, 1);
    lv_obj_set_width(_otaUpdateRescheduleHourDropdown, 150);
    lv_obj_add_event_cb(_otaUpdateRescheduleHourDropdown, onOtaUpdateRescheduleHourChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

    _otaUpdateRescheduleMinuteDropdown = lv_dropdown_create(reschedulePickerRow);
    lv_dropdown_set_options(_otaUpdateRescheduleMinuteDropdown, "0 minutes\n15 minutes\n30 minutes\n45 minutes");
    lv_dropdown_set_selected(_otaUpdateRescheduleMinuteDropdown, 0);
    lv_obj_set_width(_otaUpdateRescheduleMinuteDropdown, 150);

    _otaUpdateRescheduleApplyButton = lv_btn_create(_otaUpdateReschedulePanel);
    lv_obj_set_width(_otaUpdateRescheduleApplyButton, lv_pct(100));
    lv_obj_set_height(_otaUpdateRescheduleApplyButton, 46);
    lv_obj_set_style_radius(_otaUpdateRescheduleApplyButton, 16, 0);
    lv_obj_set_style_border_width(_otaUpdateRescheduleApplyButton, 0, 0);
    lv_obj_set_style_bg_color(_otaUpdateRescheduleApplyButton, lv_color_hex(0x1D4ED8), 0);
    lv_obj_add_event_cb(_otaUpdateRescheduleApplyButton, onOtaUpdateRescheduleApplyEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *applyLabel = lv_label_create(_otaUpdateRescheduleApplyButton);
    lv_label_set_text(applyLabel, "Apply Delay");
    lv_obj_set_style_text_font(applyLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(applyLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(applyLabel);

    _otaUpdateProgressCloseButton = lv_btn_create(card);
    lv_obj_set_width(_otaUpdateProgressCloseButton, lv_pct(100));
    lv_obj_set_height(_otaUpdateProgressCloseButton, 48);
    lv_obj_set_style_radius(_otaUpdateProgressCloseButton, 16, 0);
    lv_obj_set_style_bg_color(_otaUpdateProgressCloseButton, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_bg_opa(_otaUpdateProgressCloseButton, LV_OPA_COVER, 0);
    lv_obj_add_flag(_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_otaUpdateProgressCloseButton, onOtaUpdateProgressCloseEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t *closeLabel = lv_label_create(_otaUpdateProgressCloseButton);
    lv_label_set_text(closeLabel, "Close");
    lv_obj_set_style_text_font(closeLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(closeLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_center(closeLabel);
}

void AppSettings::updateOtaUpdateOverlayActions(bool waiting_for_decision, bool show_reschedule_picker)
{
    _otaAutoUpdateAwaitingDecision = waiting_for_decision;
    const bool show_action_row = waiting_for_decision || _firmwareUpdateInProgress;
    const bool allow_manual_dismiss = waiting_for_decision && !_firmwareUpdateInProgress;

    if (lv_obj_ready(_otaUpdateProgressActionRow)) {
        if (show_action_row) {
            lv_obj_clear_flag(_otaUpdateProgressActionRow, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_otaUpdateProgressActionRow, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (lv_obj_ready(_otaUpdateProgressInstallButton)) {
        if (show_action_row) {
            lv_obj_clear_flag(_otaUpdateProgressInstallButton, LV_OBJ_FLAG_HIDDEN);
            if (_firmwareUpdateInProgress) {
                lv_obj_add_state(_otaUpdateProgressInstallButton, LV_STATE_DISABLED);
            } else {
                lv_obj_clear_state(_otaUpdateProgressInstallButton, LV_STATE_DISABLED);
            }
        } else {
            lv_obj_add_flag(_otaUpdateProgressInstallButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_state(_otaUpdateProgressInstallButton, LV_STATE_DISABLED);
        }
    }

    if (lv_obj_ready(_otaUpdateProgressRescheduleButton)) {
        if (show_action_row) {
            lv_obj_clear_flag(_otaUpdateProgressRescheduleButton, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_otaUpdateProgressRescheduleButton, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (lv_obj_ready(_otaUpdateProgressCancelButton)) {
        if (show_action_row) {
            lv_obj_clear_flag(_otaUpdateProgressCancelButton, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_otaUpdateProgressCancelButton, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (lv_obj_ready(_otaUpdateReschedulePanel)) {
        if (show_action_row && show_reschedule_picker) {
            lv_obj_clear_flag(_otaUpdateReschedulePanel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_otaUpdateReschedulePanel, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (lv_obj_ready(_otaUpdateProgressCornerCloseButton)) {
        if (allow_manual_dismiss) {
            lv_obj_clear_flag(_otaUpdateProgressCornerCloseButton, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_otaUpdateProgressCornerCloseButton, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (lv_obj_ready(_otaUpdateProgressCloseButton)) {
        if (allow_manual_dismiss) {
            lv_obj_clear_flag(_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
        } else if (show_action_row) {
            lv_obj_add_flag(_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void AppSettings::setOtaUpdateProgressOverlayVisible(bool visible)
{
    if (visible) {
        ensureOtaUpdateProgressOverlay();
    }

    if ((_otaUpdateProgressOverlay == nullptr) || !lv_obj_ready(_otaUpdateProgressOverlay)) {
        return;
    }

    if (visible) {
        lv_obj_move_foreground(_otaUpdateProgressOverlay);
        lv_obj_clear_flag(_otaUpdateProgressOverlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_otaUpdateProgressOverlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void AppSettings::setFirmwareProgress(int32_t percent, const std::string &phase, bool is_error)
{
    const int32_t clamped = std::max<int32_t>(0, std::min<int32_t>(100, percent));

    if (lv_obj_ready(_otaUpdateProgressBar)) {
        lv_bar_set_value(_otaUpdateProgressBar, clamped, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_otaUpdateProgressBar,
                                  is_error ? lv_color_hex(0xFECACA) : lv_color_hex(0xCBD5E1),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_color(_otaUpdateProgressBar,
                                  is_error ? lv_color_hex(0xDC2626) : lv_color_hex(0x2563EB),
                                  LV_PART_INDICATOR);
    }

    if (lv_obj_ready(_otaUpdateProgressLabel)) {
        lv_label_set_text(_otaUpdateProgressLabel, phase.c_str());
        lv_obj_set_style_text_color(_otaUpdateProgressLabel,
                                    is_error ? lv_color_hex(0xB91C1C) : lv_color_hex(0x64748B),
                                    0);
    }

    if (!isUiActive()) {
        return;
    }

    if (lv_obj_ready(_firmwareProgressBar)) {
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

void AppSettings::refreshDeferredOtaScheduleState(void)
{
    char version[32] = {};
    _otaDeferredAutoUpdateUntilUs = 0;
    _otaDeferredAutoUpdateVersion.clear();

    if (loadNvsStringParam(NVS_KEY_OTA_RESCHEDULE_VERSION, version, sizeof(version)) && (version[0] != '\0')) {
        _otaDeferredAutoUpdateVersion = trim_copy(version);
    }

    const int32_t deferred_until_epoch = _nvs_param_map[NVS_KEY_OTA_RESCHEDULE_UNTIL];
    const int64_t current_epoch = static_cast<int64_t>(time(nullptr));
    if (_otaDeferredAutoUpdateVersion.empty() || (deferred_until_epoch <= 0)) {
        return;
    }

    if ((current_epoch >= kValidUnixTimeFloor) && (deferred_until_epoch <= current_epoch)) {
        clearDeferredOtaSchedule(true);
        return;
    }

    if ((current_epoch >= kValidUnixTimeFloor) && (deferred_until_epoch > current_epoch)) {
        const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
        _otaDeferredAutoUpdateUntilUs = now_us + (static_cast<uint64_t>(deferred_until_epoch - current_epoch) * 1000000ULL);
        if ((_nextOtaAvailabilityCheckUs == 0) || (_otaDeferredAutoUpdateUntilUs < _nextOtaAvailabilityCheckUs)) {
            _nextOtaAvailabilityCheckUs = _otaDeferredAutoUpdateUntilUs;
        }
    }
}

void AppSettings::clearDeferredOtaSchedule(bool persist)
{
    _otaDeferredAutoUpdateUntilUs = 0;
    _otaDeferredAutoUpdateVersion.clear();
    if (!persist) {
        return;
    }

    _nvs_param_map[NVS_KEY_OTA_RESCHEDULE_UNTIL] = 0;
    setNvsParam(NVS_KEY_OTA_RESCHEDULE_UNTIL, 0);
    setNvsStringParam(NVS_KEY_OTA_RESCHEDULE_VERSION, "");
}

void AppSettings::deferOtaUpdateForEntry(const FirmwareEntry_t &entry, uint32_t delay_seconds)
{
    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    _otaDeferredAutoUpdateUntilUs = now_us + (static_cast<uint64_t>(delay_seconds) * 1000000ULL);
    _otaDeferredAutoUpdateVersion = entry.version;
    _nvs_param_map[NVS_KEY_OTA_RESCHEDULE_UNTIL] = 0;

    const int64_t current_epoch = static_cast<int64_t>(time(nullptr));
    if (current_epoch >= kValidUnixTimeFloor) {
        const int64_t deferred_epoch = current_epoch + static_cast<int64_t>(delay_seconds);
        if (deferred_epoch <= std::numeric_limits<int32_t>::max()) {
            _nvs_param_map[NVS_KEY_OTA_RESCHEDULE_UNTIL] = static_cast<int32_t>(deferred_epoch);
        }
    }

    setNvsParam(NVS_KEY_OTA_RESCHEDULE_UNTIL, _nvs_param_map[NVS_KEY_OTA_RESCHEDULE_UNTIL]);
    setNvsStringParam(NVS_KEY_OTA_RESCHEDULE_VERSION, _otaDeferredAutoUpdateVersion.c_str());
    _nextOtaAvailabilityCheckUs = (_nextOtaAvailabilityCheckUs == 0)
                                    ? _otaDeferredAutoUpdateUntilUs
                                    : std::min(_nextOtaAvailabilityCheckUs, _otaDeferredAutoUpdateUntilUs);
}

bool AppSettings::shouldSuppressDeferredOtaUpdate(const FirmwareEntry_t &entry, uint64_t now_us)
{
    if (_otaDeferredAutoUpdateVersion.empty() || (_otaDeferredAutoUpdateVersion != entry.version)) {
        return false;
    }

    if ((_otaDeferredAutoUpdateUntilUs != 0) && (now_us < _otaDeferredAutoUpdateUntilUs)) {
        _nextOtaAvailabilityCheckUs = (_nextOtaAvailabilityCheckUs == 0)
                                        ? _otaDeferredAutoUpdateUntilUs
                                        : std::min(_nextOtaAvailabilityCheckUs, _otaDeferredAutoUpdateUntilUs);
        return true;
    }

    const int32_t deferred_until_epoch = _nvs_param_map[NVS_KEY_OTA_RESCHEDULE_UNTIL];
    const int64_t current_epoch = static_cast<int64_t>(time(nullptr));
    if ((deferred_until_epoch > 0) && (current_epoch >= kValidUnixTimeFloor) && (deferred_until_epoch > current_epoch)) {
        _otaDeferredAutoUpdateUntilUs = now_us + (static_cast<uint64_t>(deferred_until_epoch - current_epoch) * 1000000ULL);
        _nextOtaAvailabilityCheckUs = (_nextOtaAvailabilityCheckUs == 0)
                                        ? _otaDeferredAutoUpdateUntilUs
                                        : std::min(_nextOtaAvailabilityCheckUs, _otaDeferredAutoUpdateUntilUs);
        return true;
    }

    return false;
}

bool AppSettings::shouldResumeDeferredOtaUpdate(const FirmwareEntry_t &entry, uint64_t now_us)
{
    if (_otaDeferredAutoUpdateVersion.empty() || (_otaDeferredAutoUpdateVersion != entry.version)) {
        return false;
    }

    if ((_otaDeferredAutoUpdateUntilUs != 0) && (now_us >= _otaDeferredAutoUpdateUntilUs)) {
        return true;
    }

    const int32_t deferred_until_epoch = _nvs_param_map[NVS_KEY_OTA_RESCHEDULE_UNTIL];
    const int64_t current_epoch = static_cast<int64_t>(time(nullptr));
    return (deferred_until_epoch > 0) && (current_epoch >= kValidUnixTimeFloor) && (deferred_until_epoch <= current_epoch);
}

void AppSettings::updateOtaRescheduleMinuteOptions()
{
    if (!lv_obj_ready(_otaUpdateRescheduleHourDropdown) || !lv_obj_ready(_otaUpdateRescheduleMinuteDropdown)) {
        return;
    }

    const uint16_t hour_index = lv_dropdown_get_selected(_otaUpdateRescheduleHourDropdown);
    const size_t hour_slot = std::min<size_t>(hour_index, std::size(kOtaRescheduleHourOptions) - 1);
    if (kOtaRescheduleHourOptions[hour_slot] == 0) {
        lv_dropdown_set_options(_otaUpdateRescheduleMinuteDropdown, "15 minutes\n30 minutes\n45 minutes");
        if (lv_dropdown_get_selected(_otaUpdateRescheduleMinuteDropdown) > 2) {
            lv_dropdown_set_selected(_otaUpdateRescheduleMinuteDropdown, 0);
        }
        return;
    }

    const uint16_t previous_index = lv_dropdown_get_selected(_otaUpdateRescheduleMinuteDropdown);
    lv_dropdown_set_options(_otaUpdateRescheduleMinuteDropdown, "0 minutes\n15 minutes\n30 minutes\n45 minutes");
    lv_dropdown_set_selected(_otaUpdateRescheduleMinuteDropdown, std::min<uint16_t>(previous_index, 3));
}

uint32_t AppSettings::getSelectedOtaRescheduleDelaySeconds(void) const
{
    const uint16_t hour_index = lv_obj_ready(_otaUpdateRescheduleHourDropdown) ? lv_dropdown_get_selected(_otaUpdateRescheduleHourDropdown) : 0;
    const uint16_t minute_index = lv_obj_ready(_otaUpdateRescheduleMinuteDropdown) ? lv_dropdown_get_selected(_otaUpdateRescheduleMinuteDropdown) : 0;
    const size_t hour_slot = std::min<size_t>(hour_index, std::size(kOtaRescheduleHourOptions) - 1);
    const bool zero_hours = kOtaRescheduleHourOptions[hour_slot] == 0;
    if (zero_hours) {
        const size_t minute_slot = std::min<size_t>(minute_index, std::size(kOtaRescheduleMinuteFutureOnlyOptions) - 1);
        return static_cast<uint32_t>((kOtaRescheduleMinuteFutureOnlyOptions[minute_slot]) * 60);
    }

    const size_t minute_slot = std::min<size_t>(minute_index, std::size(kOtaRescheduleMinuteOptions) - 1);
    return static_cast<uint32_t>((kOtaRescheduleHourOptions[hour_slot] * 60 + kOtaRescheduleMinuteOptions[minute_slot]) * 60);
}

void AppSettings::showAutoUpdateDecisionOverlay(const FirmwareEntry_t &entry)
{
    setOtaUpdateProgressOverlayVisible(true);
    updateOtaUpdateOverlayActions(true, false);
    setFirmwareStatus(std::string("Auto update is ready for ") + entry.version + ".", false);
    setFirmwareProgress(0, "Install now, reschedule, or cancel this automatic update.", false);
    if (lv_obj_ready(_otaUpdateRescheduleHourDropdown)) {
        lv_dropdown_set_selected(_otaUpdateRescheduleHourDropdown, 1);
    }
    if (lv_obj_ready(_otaUpdateRescheduleMinuteDropdown)) {
        lv_dropdown_set_selected(_otaUpdateRescheduleMinuteDropdown, 0);
    }
    updateOtaRescheduleMinuteOptions();
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
    std::vector<FirmwareEntry_t> entries;
    if (!fetchGithubFirmwareEntriesForVersion(getCurrentFirmwareVersion(), entries)) {
        _otaFirmwareEntries.clear();
        _otaUpdateAvailableThisBoot = false;
        refreshRadioStatusBar();
        return false;
    }

    _otaFirmwareEntries = std::move(entries);
    _otaUpdateAvailableThisBoot = std::any_of(_otaFirmwareEntries.begin(), _otaFirmwareEntries.end(), [](const FirmwareEntry_t &entry) {
        return entry.is_valid && entry.is_newer;
    });
    refreshRadioStatusBar();
    return !_otaFirmwareEntries.empty();
}

bool AppSettings::fetchGithubFirmwareEntriesForVersion(const std::string &current_version, std::vector<FirmwareEntry_t> &entries)
{
    entries.clear();
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
                    entries.push_back(entry);
                }
            }
        }
        cJSON_Delete(root);
        ok = !entries.empty();
    }

    esp_http_client_cleanup(client);

    std::sort(entries.begin(), entries.end(), [](const FirmwareEntry_t &lhs, const FirmwareEntry_t &rhs) {
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

bool AppSettings::isWifiConnectedForOtaCheck(void) const
{
#if !CONFIG_JC4880_FEATURE_WIFI
    return false;
#else
    wifi_ap_record_t ap_info = {};
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
#endif
}

int AppSettings::findPreferredOtaEntryIndex(bool prefer_newer) const
{
    if (prefer_newer) {
        for (size_t index = 0; index < _otaFirmwareEntries.size(); ++index) {
            if (_otaFirmwareEntries[index].is_valid && _otaFirmwareEntries[index].is_newer) {
                return static_cast<int>(index);
            }
        }
    }

    for (size_t index = 0; index < _otaFirmwareEntries.size(); ++index) {
        if (_otaFirmwareEntries[index].is_valid) {
            return static_cast<int>(index);
        }
    }

    return -1;
}

void AppSettings::requestFirmwareScreenOpen(bool prefer_newer)
{
#if !CONFIG_JC4880_FEATURE_OTA
    (void)prefer_newer;
    return;
#else
    const int preferred_index = findPreferredOtaEntryIndex(prefer_newer);
    if (preferred_index >= 0) {
        setSelectedOtaFirmwareIndex(preferred_index);
    }

    _pendingOpenFirmwareScreen = true;
    if (isUiActive()) {
        openFirmwareScreenIfPending();
        return;
    }

    if ((jc_security_handle_app_launch_request != nullptr) &&
        jc_security_handle_app_launch_request(getId(), getName())) {
        return;
    }

    ESP_Brookesia_CoreAppEventData_t app_event_data = {
        .id = getId(),
        .type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START,
        .data = nullptr,
    };
    getPhone()->sendAppEvent(&app_event_data);
#endif
}

void AppSettings::openFirmwareScreenIfPending(void)
{
    if (!_pendingOpenFirmwareScreen) {
        return;
    }

    _pendingOpenFirmwareScreen = false;
    ensureFirmwareScreen();
    refreshFirmwareUi();
    if ((_firmwareScreen != nullptr) && (lv_scr_act() != _firmwareScreen)) {
        lv_scr_load_anim(_firmwareScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
    }
}

void AppSettings::applyAsyncOtaAvailabilityResult(void *arg)
{
    std::unique_ptr<AsyncOtaAvailabilityContext> context(static_cast<AsyncOtaAvailabilityContext *>(arg));
    if ((context == nullptr) || (context->app == nullptr)) {
        return;
    }

    AppSettings *app = context->app;
    app->_otaAvailabilityCheckInProgress = false;

    if (!context->success) {
        return;
    }

    app->_otaFirmwareEntries = std::move(context->entries);
    app->_otaUpdateAvailableThisBoot = std::any_of(app->_otaFirmwareEntries.begin(), app->_otaFirmwareEntries.end(),
                                                   [](const FirmwareEntry_t &entry) {
                                                       return entry.is_valid && entry.is_newer;
                                                   });

    if (app->getSelectedOtaFirmwareIndex() < 0) {
        app->setSelectedOtaFirmwareIndex(app->findPreferredOtaEntryIndex(true));
    }

    if (!app->_otaUpdateAvailableThisBoot && (app->_otaUpdateAvailableMsgbox != nullptr) && lv_obj_is_valid(app->_otaUpdateAvailableMsgbox)) {
        lv_msgbox_close_async(app->_otaUpdateAvailableMsgbox);
    }

    app->refreshRadioStatusBar();
    if (app->isUiActive()) {
        app->refreshFirmwareUi();
    }

    if (!app->_otaUpdateAvailableThisBoot || app->_otaUpdatePromptDismissedThisBoot ||
        ((app->_otaUpdateAvailableMsgbox != nullptr) && lv_obj_is_valid(app->_otaUpdateAvailableMsgbox))) {
        return;
    }

    const int preferred_index = app->findPreferredOtaEntryIndex(true);
    if (preferred_index < 0) {
        return;
    }

    const FirmwareEntry_t &entry = app->_otaFirmwareEntries[preferred_index];
    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());

    if (app->_nvs_param_map[NVS_KEY_OTA_AUTO_UPDATE] != 0) {
        if (app->shouldSuppressDeferredOtaUpdate(entry, now_us)) {
            return;
        }

        if (app->shouldResumeDeferredOtaUpdate(entry, now_us)) {
            app->clearDeferredOtaSchedule(true);
            app->_otaUpdatePromptDismissedThisBoot = true;
            app->startPreferredOtaUpdate();
            return;
        }

        app->setSelectedOtaFirmwareIndex(preferred_index);
        app->_otaUpdatePromptDismissedThisBoot = true;
        app->startPreferredOtaUpdate();
        return;
    }

    const std::string message = std::string("Current firmware: ") + app->getCurrentFirmwareVersion() +
                                "\nNew firmware: " + entry.version +
                                "\n\nStart the OTA update now?";
    static const char *buttons[] = {"Update", "Cancel", ""};
    app->_otaUpdateAvailableMsgbox = lv_msgbox_create(lv_layer_top(), "Update Available", message.c_str(), buttons, false);
    if ((app->_otaUpdateAvailableMsgbox == nullptr) || !lv_obj_is_valid(app->_otaUpdateAvailableMsgbox)) {
        app->_otaUpdateAvailableMsgbox = nullptr;
        return;
    }

    lv_obj_set_width(app->_otaUpdateAvailableMsgbox, LV_MIN(LV_HOR_RES - 24, 460));
    lv_obj_center(app->_otaUpdateAvailableMsgbox);
    lv_obj_add_event_cb(app->_otaUpdateAvailableMsgbox, onOtaUpdateAvailablePopupEventCallback, LV_EVENT_VALUE_CHANGED, app);
    lv_obj_add_event_cb(app->_otaUpdateAvailableMsgbox, onOtaUpdateAvailablePopupEventCallback, LV_EVENT_DELETE, app);
}

void AppSettings::maybeRunOtaAvailabilityCheck(void)
{
#if !CONFIG_JC4880_FEATURE_OTA
    return;
#else
    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    if ((_otaDeferredAutoUpdateUntilUs != 0) && ((_nextOtaAvailabilityCheckUs == 0) || (_otaDeferredAutoUpdateUntilUs < _nextOtaAvailabilityCheckUs))) {
        _nextOtaAvailabilityCheckUs = _otaDeferredAutoUpdateUntilUs;
    }

    if (_otaAvailabilityCheckInProgress || _firmwareUpdateInProgress || _firmwareOtaCheckInProgress || (now_us < _nextOtaAvailabilityCheckUs)) {
        return;
    }

    if (!hasOtaFlashSupport() || !isWifiConnectedForOtaCheck()) {
        _nextOtaAvailabilityCheckUs = now_us + kOtaAvailabilityRetryIntervalUs;
        return;
    }

    _otaAvailabilityCheckInProgress = true;
    _nextOtaAvailabilityCheckUs = now_us + kOtaAvailabilitySuccessIntervalUs;

    auto *context = new AsyncOtaAvailabilityContext{};
    if (context == nullptr) {
        _otaAvailabilityCheckInProgress = false;
        _nextOtaAvailabilityCheckUs = now_us + kOtaAvailabilityRetryIntervalUs;
        return;
    }

    context->app = this;
    context->success = fetchGithubFirmwareEntriesForVersion(getCurrentFirmwareVersion(), context->entries);
    if (!context->success) {
        _nextOtaAvailabilityCheckUs = now_us + kOtaAvailabilityRetryIntervalUs;
    }

    bsp_display_lock(0);
    if (lv_async_call(applyAsyncOtaAvailabilityResult, context) != LV_RES_OK) {
        bsp_display_unlock();
        delete context;
        _otaAvailabilityCheckInProgress = false;
        _nextOtaAvailabilityCheckUs = now_us + kOtaAvailabilityRetryIntervalUs;
        return;
    }
    bsp_display_unlock();
#endif
}

void AppSettings::refreshFirmwareUi(void)
{
#if !CONFIG_JC4880_FEATURE_OTA
    return;
#endif
    if (lv_obj_ready(_firmwareAutoUpdateSwitch)) {
        if (_nvs_param_map[NVS_KEY_OTA_AUTO_UPDATE] != 0) {
            lv_obj_add_state(_firmwareAutoUpdateSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_firmwareAutoUpdateSwitch, LV_STATE_CHECKED);
        }
    }

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

    const bool canceled = !context->busy && !context->is_error &&
                          (strcmp(context->status, "Firmware update canceled.") == 0);

    context->app->_firmwareUpdateInProgress = context->busy;
    context->app->updateOtaUpdateOverlayActions(canceled, false);
    context->app->setOtaUpdateProgressOverlayVisible(context->busy || context->is_error || canceled);
    if (lv_obj_ready(context->app->_otaUpdateProgressCloseButton)) {
        if (context->busy || (!context->is_error && !canceled)) {
            lv_obj_add_flag(context->app->_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(context->app->_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (context->is_error) {
        context->app->refreshFirmwareUi();
        context->app->setFirmwareStatus(context->status, true);
        context->app->setFirmwareProgress(context->percent, context->status, true);
        delete context;
        return;
    }

    if (canceled) {
        context->app->refreshFirmwareUi();
        context->app->setFirmwareStatus(context->status, false);
        context->app->setFirmwareProgress(0, "Install now, reschedule, or close this automatic update.", false);
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
        if (_firmwareCancelRequested) {
            error_message = "Firmware update canceled.";
            ESP_LOGI(TAG, "SD firmware flash canceled by user");
            goto cleanup;
        }

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

    if (_firmwareCancelRequested) {
        error_message = "Firmware update canceled.";
        ESP_LOGI(TAG, "SD firmware flash canceled before finalization");
        goto cleanup;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        error_message = std::string("esp_ota_end failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        goto cleanup_no_abort;
    }

    if (_firmwareCancelRequested) {
        error_message = "Firmware update canceled.";
        ESP_LOGI(TAG, "SD firmware flash canceled before boot partition switch");
        return false;
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
        if (_firmwareCancelRequested) {
            error_message = "Firmware update canceled.";
            ESP_LOGI(TAG, "OTA firmware flash canceled by user");
            goto ota_cleanup;
        }

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

    if (_firmwareCancelRequested) {
        error_message = "Firmware update canceled.";
        ESP_LOGI(TAG, "OTA firmware flash canceled before finalization");
        goto ota_cleanup;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        error_message = std::string("esp_ota_end failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        goto ota_cleanup_no_abort;
    }

    if (_firmwareCancelRequested) {
        error_message = "Firmware update canceled.";
        ESP_LOGI(TAG, "OTA firmware flash canceled before boot partition switch");
        return false;
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

    _firmwareCancelRequested = false;
    _firmwareUpdateInProgress = true;
    updateOtaUpdateOverlayActions(false, false);
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
        _firmwareCancelRequested = false;
        updateOtaUpdateOverlayActions(false, false);
        refreshFirmwareUi();
        setFirmwareStatus("Failed to start firmware update task.", true);
        ESP_LOGE(TAG, "Failed to start firmware update background task");
        return false;
    }

    return true;
}

bool AppSettings::startPreferredOtaUpdate(void)
{
    if (_firmwareUpdateInProgress) {
        setOtaUpdateProgressOverlayVisible(true);
        return true;
    }

    if (!hasOtaFlashSupport()) {
        setOtaUpdateProgressOverlayVisible(true);
        setFirmwareStatus("OTA flashing is blocked because the current partition table has no OTA slot.", true);
        setFirmwareProgress(0, "OTA update unavailable.", true);
        if (lv_obj_ready(_otaUpdateProgressCloseButton)) {
            lv_obj_clear_flag(_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
        }
        return false;
    }

    const int selected = findPreferredOtaEntryIndex(true);
    if ((selected < 0) || (static_cast<size_t>(selected) >= _otaFirmwareEntries.size()) || !_otaFirmwareEntries[selected].is_valid) {
        setOtaUpdateProgressOverlayVisible(true);
        setFirmwareStatus("No valid OTA release asset is available for update.", true);
        setFirmwareProgress(0, "OTA update unavailable.", true);
        if (lv_obj_ready(_otaUpdateProgressCloseButton)) {
            lv_obj_clear_flag(_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
        }
        return false;
    }

    setSelectedOtaFirmwareIndex(selected);
    if (!_otaDeferredAutoUpdateVersion.empty() && (_otaDeferredAutoUpdateVersion == _otaFirmwareEntries[selected].version)) {
        clearDeferredOtaSchedule(true);
    }
    setOtaUpdateProgressOverlayVisible(true);
    updateOtaUpdateOverlayActions(false, false);
    setFirmwareStatus("Starting OTA update...");
    setFirmwareProgress(0, "Preparing OTA update...");
    if (lv_obj_ready(_otaUpdateProgressCloseButton)) {
        lv_obj_add_flag(_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
    }

    return flashFirmwareEntry(_otaFirmwareEntries[selected], FIRMWARE_UPDATE_SOURCE_OTA);
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
        const bool canceled = error_message == "Firmware update canceled.";
        ESP_LOGE(TAG,
                 "Firmware update failed: source=%s label='%s' version='%s' reason='%s'",
                 source == FIRMWARE_UPDATE_SOURCE_OTA ? "ota" : "sd",
                 entry.label.c_str(),
                 entry.version.c_str(),
                 error_message.empty() ? "Firmware update failed." : error_message.c_str());
        app->_firmwareCancelRequested = false;
        app->queueFirmwareUiUpdate(error_message.empty() ? "Firmware update failed." : error_message.c_str(),
                                   0,
                                   false,
                                   canceled ? false : true);
        vTaskDelete(nullptr);
        return;
    }

    app->_firmwareCancelRequested = false;
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
    loadNvsStringParam(NVS_KEY_WIFI_AP_SSID, st_wifi_ap_ssid, sizeof(st_wifi_ap_ssid));
    loadNvsStringParam(NVS_KEY_WIFI_AP_PASSWORD, st_wifi_ap_password, sizeof(st_wifi_ap_password));
#endif

#if APP_SETTINGS_FEATURE_WIFI
    if (_nvs_param_map[NVS_KEY_WIFI_ENABLE]) {
        lv_obj_add_state(ui_SwitchPanelScreenSettingWiFiSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(ui_SwitchPanelScreenSettingWiFiSwitch, LV_STATE_CHECKED);
    }
    refreshWifiApUi();
#endif

#if APP_SETTINGS_FEATURE_DISPLAY_MENU
    lv_slider_set_value(ui_SliderPanelScreenSettingLightSwitch1, _nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS], LV_ANIM_OFF);
#endif

#if CONFIG_JC4880_FEATURE_AUDIO
    lv_slider_set_value(ui_SliderPanelScreenSettingVolumeSwitch, _nvs_param_map[NVS_KEY_AUDIO_VOLUME], LV_ANIM_OFF);
    if (_audioSystemVolumeSlider != nullptr) {
        lv_slider_set_value(_audioSystemVolumeSlider, _nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME], LV_ANIM_OFF);
    }
    if (lv_obj_ready(_audioTapSoundSwitch)) {
        if (_nvs_param_map[NVS_KEY_AUDIO_TAP_SOUND] != 0) {
            lv_obj_add_state(_audioTapSoundSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_audioTapSoundSwitch, LV_STATE_CHECKED);
        }
    }
    if (lv_obj_ready(_audioHapticFeedbackSwitch)) {
        if (_nvs_param_map[NVS_KEY_AUDIO_HAPTIC_FEEDBACK] != 0) {
            lv_obj_add_state(_audioHapticFeedbackSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_audioHapticFeedbackSwitch, LV_STATE_CHECKED);
        }
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

    if (lv_obj_ready(_firmwareAutoUpdateSwitch)) {
        if (_nvs_param_map[NVS_KEY_OTA_AUTO_UPDATE] != 0) {
            lv_obj_add_state(_firmwareAutoUpdateSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_firmwareAutoUpdateSwitch, LV_STATE_CHECKED);
        }
    }
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
                detail += battery_status.charging ? " в”¬в•– full in " : " в”¬в•– ";
                if (!battery_status.charging) {
                    detail += formatDurationMinutes(battery_status.eta_minutes) + " left";
                } else {
                    detail += formatDurationMinutes(battery_status.eta_minutes);
                }
            } else {
                detail += battery_status.charging ? " в”¬в•– estimating time to full" : " в”¬в•– estimating battery life";
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
    const bool sd_mounted = app_storage_is_sdcard_mounted();
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
        uint32_t refresh_period_ms = HOME_REFRESH_TASK_PERIOD_MS;
#if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
        bleCheckStartupTimeout();
#endif
        app->maybeRunOtaAvailabilityCheck();
        if (app->isUiActive()) {
            bsp_display_lock(0);
            app->refreshRadioStatusBar();
#if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
            if (app->_screen_index == UI_BLUETOOTH_SETTING_INDEX) {
                app->refreshBluetoothUi();
            }
#endif
            if (lv_scr_act() == app->_joypadBleScreen) {
                jc4880_joypad_ble_report_state_t report = {};
                if (jc4880_joypad_get_ble_report_state(&report)) {
                    app->refreshJoypadCalibrationUi(report);
                }
                refresh_period_ms = JOYPAD_BLE_LIVE_REFRESH_MS;
            } else if ((lv_scr_act() == app->_joypadScreen) ||
                       (lv_scr_act() == app->_joypadLocalScreen)) {
                app->refreshJoypadUi();
                if (lv_scr_act() == app->_joypadLocalScreen) {
                    refresh_period_ms = JOYPAD_BLE_LIVE_REFRESH_MS;
                }
            }
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

        vTaskDelay(pdMS_TO_TICKS(refresh_period_ms));
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
        jc4880_password_textarea_set_visibility(ui_TextAreaScreenSettingVerificationPassword, false);
    }
    if (app->_screen_index != UI_WIFI_SCAN_INDEX) {
        app->setWifiApKeyboardVisible(false);
        jc4880_password_textarea_set_visibility(app->_wifiApPasswordTextArea, false);
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

    #if APP_SETTINGS_FEATURE_IMU
    if ((app->_screen_index != UI_IMU_SETTING_INDEX) && (app->_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE] == 0)) {
        app->stopImuLivePolling();
    }
    #endif

    #if APP_SETTINGS_FEATURE_WIFI
    if (app->_screen_index == UI_WIFI_SCAN_INDEX) {
        app->stopWifiScan();
        app->refreshWifiApUi();
        app->refreshSavedWifiUi();
    }
    #endif

    #if APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    if (app->_screen_index == UI_BLUETOOTH_SETTING_INDEX) {
        app->refreshBluetoothUi();
    }
    #endif

    if (app->_screen_index == UI_JOYPAD_SETTING_INDEX) {
        app->refreshJoypadUi();
    }

    #if APP_SETTINGS_FEATURE_IMU
    if ((app->_screen_index == UI_IMU_SETTING_INDEX) || (app->_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE] != 0)) {
        if (app->_screen_index == UI_IMU_SETTING_INDEX) {
            app->refreshImuUi();
        }
        app->startImuLivePolling();
    }
    #endif

    #if CONFIG_JC4880_APP_LORA_MESH
    if (app->_screen_index == UI_LORA_SETTING_INDEX) {
        app->refreshLoRaUi();
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

    #if APP_SETTINGS_FEATURE_DISPLAY_MENU
    if (app->_screen_index == UI_BRIGHTNESS_SETTING_INDEX) {
        app->refreshDisplayIdleUi();
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
    if (target == app->_joypadMenuItem) {
        app->ensureJoypadScreen();
        lv_scr_load_anim(app->_joypadScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
    } else
    #if APP_SETTINGS_FEATURE_IMU
    if (target == app->_imuMenuItem) {
        app->ensureImuScreen();
        app->refreshImuUi();
        lv_scr_load_anim(app->_imuScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
    } else
    #endif
    #if CONFIG_JC4880_APP_LORA_MESH
    if (target == app->_loraMenuItem) {
        app->ensureLoRaScreen();
        app->refreshLoRaUi();
        lv_scr_load_anim(app->_loraScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
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

void AppSettings::onImuConfigChangedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

#if APP_SETTINGS_FEATURE_IMU
    if (lv_event_get_target(e) == app->_imuEnabledSwitch) {
        if (!app->persistImuConfigFromUi(true)) {
            app->refreshImuUi();
        }
        return;
    }

    app->refreshImuUi();
#else
    (void)e;
#endif
}

void AppSettings::onImuSaveClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");
    app->persistImuConfigFromUi(false);
end:
    return;
}

void AppSettings::onImuScanClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

#if APP_SETTINGS_FEATURE_IMU
    if (!app->persistImuConfigFromUi(false)) {
        return;
    }

    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);
    std::string status;
    jc4880::imu::ImuModel detected_model = jc4880::imu::ImuModel::IMU_NONE;
    uint8_t detected_address = 0;
    if (jc4880::imu::ImuService::instance().detectI2cModel(config, detected_model, detected_address, status)) {
        if (lv_obj_ready(app->_imuModelDropdown)) {
            lv_dropdown_set_selected(app->_imuModelDropdown, imu_dropdown_index_from_model(detected_model));
        }
        if (lv_obj_ready(app->_imuI2cAddressDropdown)) {
            lv_dropdown_set_selected(app->_imuI2cAddressDropdown,
                                     findDropdownIndexForValue(kImuAddressOptions,
                                                               sizeof(kImuAddressOptions) / sizeof(kImuAddressOptions[0]),
                                                               detected_address));
        }
        if (lv_obj_ready(app->_imuI2cAddressTextArea)) {
            char address_text[8] = {};
            std::snprintf(address_text, sizeof(address_text), "%02X", static_cast<unsigned>(detected_address));
            lv_textarea_set_text(app->_imuI2cAddressTextArea, address_text);
        }
        (void)app->persistImuConfigFromUi(false);
        app->refreshImuUi();
    }
    if (lv_obj_ready(app->_imuStatusLabel)) {
        lv_label_set_text(app->_imuStatusLabel, status.c_str());
    }
#else
    (void)e;
#endif
}

void AppSettings::onImuTestClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

#if APP_SETTINGS_FEATURE_IMU
    if (!app->persistImuConfigFromUi(false)) {
        return;
    }

    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);
    jc4880::imu::ImuSample sample = {};
    std::string status;
    jc4880::imu::ImuService::instance().test(config, sample, status);
    if (lv_obj_ready(app->_imuStatusLabel)) {
        lv_label_set_text(app->_imuStatusLabel, status.c_str());
    }
#else
    (void)e;
#endif
}

void AppSettings::onImuLiveTimerCallback(lv_timer_t *timer)
{
    if (timer == nullptr) {
        return;
    }

    AppSettings *app = static_cast<AppSettings *>(timer->user_data);
    if (app == nullptr) {
        return;
    }

    if (app->_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE] != 0) {
        jc4880::imu::ImuSample sample = {};
        bool sample_ok = false;

        if (app->isUiActive() && (app->_screen_index == UI_IMU_SETTING_INDEX)) {
            app->refreshImuLiveUi();
            sample_ok = jc4880::imu::ImuService::instance().getLastSample(sample);
        } else {
            jc4880::imu::ImuConfig config = {};
            if (jc4880::imu::ImuService::instance().loadConfig(config) && config.enabled &&
                (config.model != jc4880::imu::ImuModel::IMU_NONE)) {
                sample_ok = jc4880::imu::ImuService::instance().read(sample);
                if (!sample_ok && jc4880::imu::ImuService::instance().begin(&config)) {
                    sample_ok = jc4880::imu::ImuService::instance().read(sample);
                }
            }
        }

        app->updateDisplayAutorotateFromSample(sample_ok ? &sample : nullptr, sample_ok);
        if (!app->isUiActive() || (app->_screen_index != UI_IMU_SETTING_INDEX)) {
            return;
        }
        return;
    }

    if (!app->isUiActive() || (app->_screen_index != UI_IMU_SETTING_INDEX)) {
        return;
    }

    app->refreshImuLiveUi();
}

void AppSettings::onLoRaConfigChangedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

#if CONFIG_JC4880_APP_LORA_MESH
    if (lv_event_get_target(e) == app->_loraEnabledSwitch) {
        if (!app->persistLoRaRadioEnabledFromUi()) {
            app->refreshLoRaUi();
        }
        return;
    }

    const auto module = lora_radio_module_from_dropdown(lv_dropdown_get_selected(app->_loraModuleDropdown));
    for (size_t role_index = 0; role_index < kLoraPinRoleCount; ++role_index) {
        if ((app->_loraPinRows[role_index] == nullptr) || !lv_obj_ready(app->_loraPinRows[role_index])) {
            continue;
        }
        if (lora_module_uses_role(module, static_cast<LoraPinRole>(role_index))) {
            lv_obj_clear_flag(app->_loraPinRows[role_index], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(app->_loraPinRows[role_index], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if ((app->_loraInfoLabel != nullptr) && lv_obj_ready(app->_loraInfoLabel) && is_local_controller_backend_active()) {
        lv_label_set_text(app->_loraInfoLabel,
                          "Local Controller is active. LoRa radio is forced off to avoid conflicts with haptics and Neopixel on shared GPIOs.");
    }
#endif
}

void AppSettings::onLoRaSaveClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");
    app->persistLoRaConfigFromUi();
end:
    return;
}

void AppSettings::onLoRaSelfCheckClickedEventCallback(lv_event_t *e)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_BROOKESIA_LOGE("Invalid app pointer");
        return;
    }

    if ((app->_loraEnabledSwitch != nullptr) && lv_obj_ready(app->_loraEnabledSwitch) &&
        lv_obj_has_state(app->_loraEnabledSwitch, LV_STATE_CHECKED)) {
        if (!app->persistLoRaRadioEnabledFromUi()) {
            if ((app->_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(app->_loraSelfCheckStatusLabel)) {
                lv_label_set_text(app->_loraSelfCheckStatusLabel,
                                  "Self check unavailable because the LoRa radio switch could not be saved.");
            }
            return;
        }
    }

    jc4880::lora_mesh::StoredState state = {};
    if (!jc4880::lora_mesh::load_stored_state(state)) {
        if ((app->_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(app->_loraSelfCheckStatusLabel)) {
            lv_label_set_text(app->_loraSelfCheckStatusLabel,
                              "Self check unavailable because LoRa settings could not be loaded.");
        }
        if ((app->_loraInfoLabel != nullptr) && lv_obj_ready(app->_loraInfoLabel)) {
            lv_label_set_text(app->_loraInfoLabel, "Failed to load LoRa settings before starting self check.");
        }
        return;
    }
    if (!state.settings.radio_enabled) {
        if ((app->_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(app->_loraSelfCheckStatusLabel)) {
            lv_label_set_text(app->_loraSelfCheckStatusLabel,
                              "Self check unavailable while the LoRa radio is disabled.");
        }
        if ((app->_loraInfoLabel != nullptr) && lv_obj_ready(app->_loraInfoLabel)) {
            lv_label_set_text(app->_loraInfoLabel, "Device disabled. Please enable radio in Device Settings.");
        }
        return;
    }

    LoRaMeshApp *lora_app = find_installed_lora_mesh_app(app->getCore());
    if (lora_app == nullptr) {
        if ((app->_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(app->_loraSelfCheckStatusLabel)) {
            lv_label_set_text(app->_loraSelfCheckStatusLabel, "Self check unavailable because the LoRa app is not installed.");
        }
        if ((app->_loraInfoLabel != nullptr) && lv_obj_ready(app->_loraInfoLabel)) {
            lv_label_set_text(app->_loraInfoLabel, "LoRa Mesh app is not installed.");
        }
        return;
    }

    if (!lora_app->startSelfTestFromSettings()) {
        if ((app->_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(app->_loraSelfCheckStatusLabel)) {
            lv_label_set_text(app->_loraSelfCheckStatusLabel, "Failed to start LoRa self check.");
        }
        if ((app->_loraInfoLabel != nullptr) && lv_obj_ready(app->_loraInfoLabel)) {
            lv_label_set_text(app->_loraInfoLabel, "Failed to start LoRa self check.");
        }
        return;
    }

    if ((app->_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(app->_loraSelfCheckStatusLabel)) {
        lv_label_set_text(app->_loraSelfCheckStatusLabel, "LoRa self check starting...");
    }
    app->startLoRaSelfCheckStatusPolling();
#endif
}

void AppSettings::onLoRaSelfCheckStatusTimerCallback(lv_timer_t *timer)
{
    AppSettings *app = (timer != nullptr) ? static_cast<AppSettings *>(timer->user_data) : nullptr;
    if (app == nullptr) {
        return;
    }

    app->refreshLoRaSelfCheckStatus();
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
    lv_obj_t *msgbox = lv_event_get_current_target(e);
    const char *button_text = nullptr;
    const lv_event_code_t code = lv_event_get_code(e);

    if ((app == nullptr) || (msgbox == nullptr) || (code != LV_EVENT_VALUE_CHANGED)) {
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

void AppSettings::onOtaUpdateAvailablePopupEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *msgbox = lv_event_get_current_target(e);
    const lv_event_code_t code = lv_event_get_code(e);

    if (app == nullptr) {
        return;
    }

    if ((code == LV_EVENT_DELETE) && (app->_otaUpdateAvailableMsgbox == msgbox)) {
        app->_otaUpdateAvailableMsgbox = nullptr;
        return;
    }

    if ((code != LV_EVENT_VALUE_CHANGED) || (msgbox == nullptr)) {
        return;
    }

    const char *button_text = lv_msgbox_get_active_btn_text(msgbox);
    app->_otaUpdatePromptDismissedThisBoot = true;
    lv_msgbox_close_async(msgbox);

    if ((button_text != nullptr) && (strcmp(button_text, "Update") == 0)) {
        app->startPreferredOtaUpdate();
    }
}

void AppSettings::onFirmwareAutoUpdateSwitchValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *target = nullptr;
    bool enabled = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid OTA auto update switch");

    enabled = (lv_obj_get_state(target) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_OTA_AUTO_UPDATE] = enabled ? 1 : 0;
    app->setNvsParam(NVS_KEY_OTA_AUTO_UPDATE, enabled ? 1 : 0);
    if (!enabled) {
        app->clearDeferredOtaSchedule(true);
        app->updateOtaUpdateOverlayActions(false, false);
    }

end:
    return;
}

void AppSettings::onOtaUpdateInstallNowEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    app->updateOtaUpdateOverlayActions(false, false);
    app->_otaUpdatePromptDismissedThisBoot = true;
    app->startPreferredOtaUpdate();
}

void AppSettings::onOtaUpdateCancelEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    app->_otaUpdatePromptDismissedThisBoot = true;
    if (app->_firmwareUpdateInProgress) {
        app->_firmwareCancelRequested = true;
        app->setFirmwareStatus("Canceling firmware update...", false);
        app->setFirmwareProgress(0, "Canceling firmware update...", false);
        return;
    }

    app->updateOtaUpdateOverlayActions(false, false);
    app->setOtaUpdateProgressOverlayVisible(false);
    app->setFirmwareStatus("Automatic OTA start canceled.", false);
    app->setFirmwareProgress(0, "Automatic update canceled for now.", false);
}

void AppSettings::onOtaUpdateRescheduleEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    app->updateOtaUpdateOverlayActions(true, true);
    app->updateOtaRescheduleMinuteOptions();
    app->setFirmwareProgress(0, "Choose how long to delay automatic installation.", false);
}

void AppSettings::onOtaUpdateRescheduleHourChangedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    app->updateOtaRescheduleMinuteOptions();
}

void AppSettings::onOtaUpdateRescheduleApplyEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    const uint32_t delay_seconds = app->getSelectedOtaRescheduleDelaySeconds();
    if (delay_seconds == 0) {
        app->setFirmwareStatus("Select a delay longer than zero to reschedule.", true);
        app->setFirmwareProgress(0, "Reschedule requires a future time.", true);
        return;
    }

    const int selected = app->findPreferredOtaEntryIndex(true);
    if ((selected < 0) || (static_cast<size_t>(selected) >= app->_otaFirmwareEntries.size())) {
        app->setFirmwareStatus("No OTA release is available to reschedule.", true);
        app->setFirmwareProgress(0, "Reschedule unavailable.", true);
        return;
    }

    const FirmwareEntry_t &entry = app->_otaFirmwareEntries[selected];
    app->deferOtaUpdateForEntry(entry, delay_seconds);
    if (app->_firmwareUpdateInProgress) {
        app->_otaUpdatePromptDismissedThisBoot = true;
        app->_firmwareCancelRequested = true;
        app->updateOtaUpdateOverlayActions(false, false);
        app->setFirmwareStatus(std::string("Automatic OTA update deferred for ") +
                                   formatDelayMinutes(static_cast<int32_t>(delay_seconds / 60)) + ".",
                               false);
        app->setFirmwareProgress(0, "Canceling current update and applying the new schedule...", false);
        return;
    }

    app->updateOtaUpdateOverlayActions(false, false);
    app->setOtaUpdateProgressOverlayVisible(false);
    app->setFirmwareStatus(std::string("Automatic OTA update deferred for ") + formatDelayMinutes(static_cast<int32_t>(delay_seconds / 60)) + ".",
                           false);
    app->setFirmwareProgress(0, "Automatic update rescheduled.", false);
}

void AppSettings::onOtaUpdateProgressCloseEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    if (app->_firmwareUpdateInProgress) {
        return;
    }

    if (!app->_otaAutoUpdateAwaitingDecision) {
        return;
    }

    lv_obj_t *current_target = lv_event_get_current_target(e);
    if (current_target == app->_otaUpdateProgressOverlay) {
        lv_obj_t *target = lv_event_get_target(e);
        if (target != app->_otaUpdateProgressOverlay) {
            return;
        }
    }

    app->setOtaUpdateProgressOverlayVisible(false);
}


void AppSettings::onSwitchPanelScreenSettingBluetoothValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (bluetoothMenuDelegatesToJoypadBle()) {
        lv_obj_clear_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_CHECKED);
        app->refreshBluetoothUi();
        goto end;
    }

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

    if (bluetoothMenuDelegatesToJoypadBle()) {
        app->refreshBluetoothUi();
        goto end;
    }

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

    if ((code == LV_EVENT_FOCUSED) || (code == LV_EVENT_CLICKED)) {
        app->setZigbeeKeyboardVisible(true);
    } else if ((code == LV_EVENT_DEFOCUSED) || (code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        app->setZigbeeKeyboardVisible(false);
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

void AppSettings::onSwitchPanelScreenSettingTapSoundValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");
    ESP_BROOKESIA_CHECK_NULL_GOTO(app->_audioTapSoundSwitch, end, "Invalid tap sound switch");

    enabled = (lv_obj_get_state(app->_audioTapSoundSwitch) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_AUDIO_TAP_SOUND] = enabled ? 1 : 0;
    app->setNvsParam(NVS_KEY_AUDIO_TAP_SOUND, enabled ? 1 : 0);
    jc_ui_tap_sound_set_enabled(enabled);

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingHapticFeedbackValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;
    lv_obj_t *target = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");
    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid haptic feedback switch");

    enabled = (lv_obj_get_state(target) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_AUDIO_HAPTIC_FEEDBACK] = enabled ? 1 : 0;
    app->setNvsParam(NVS_KEY_AUDIO_HAPTIC_FEEDBACK, enabled ? 1 : 0);
    jc_ui_haptic_feedback_set_enabled(enabled);

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

void AppSettings::onSwitchPanelScreenSettingNeopixelPowerValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    bool enabled = false;
    lv_obj_t *target = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid Neopixel power switch");
    enabled = (lv_obj_get_state(target) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_NEOPIXEL_POWER] = enabled ? 1 : 0;
    app->setNvsParam(NVS_KEY_NEOPIXEL_POWER, enabled ? 1 : 0);
    app->applyNeopixelConfig();
    app->refreshDisplayIdleUi();
    app->refreshJoypadUi();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingNeopixelGpioValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    uint16_t selected = 0;
    int32_t value = -1;
    lv_obj_t *target = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid Neopixel GPIO dropdown");
    selected = lv_dropdown_get_selected(target);
    value = getDropdownValueForIndex(kNeopixelGpioOptions,
                                     sizeof(kNeopixelGpioOptions) / sizeof(kNeopixelGpioOptions[0]),
                                     selected);
    value = sanitizeNeopixelGpio(value);
    app->_nvs_param_map[NVS_KEY_NEOPIXEL_GPIO] = value;
    app->setNvsParam(NVS_KEY_NEOPIXEL_GPIO, value);
    app->applyNeopixelConfig();
    app->refreshDisplayIdleUi();
    app->refreshJoypadUi();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingNeopixelPaletteValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    uint16_t selected = 0;
    int32_t value = 0;
    lv_obj_t *target = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid Neopixel palette dropdown");
    selected = lv_dropdown_get_selected(target);
    value = getDropdownValueForIndex(kNeopixelPaletteOptions,
                                     sizeof(kNeopixelPaletteOptions) / sizeof(kNeopixelPaletteOptions[0]),
                                     selected);
    app->_nvs_param_map[NVS_KEY_NEOPIXEL_PALETTE] = value;
    app->setNvsParam(NVS_KEY_NEOPIXEL_PALETTE, value);
    app->applyNeopixelConfig();
    app->refreshDisplayIdleUi();
    app->refreshJoypadUi();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingNeopixelEffectValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    uint16_t selected = 0;
    int32_t value = 0;
    lv_obj_t *target = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid Neopixel effect dropdown");
    selected = lv_dropdown_get_selected(target);
    value = getDropdownValueForIndex(kNeopixelEffectOptions,
                                     sizeof(kNeopixelEffectOptions) / sizeof(kNeopixelEffectOptions[0]),
                                     selected);
    app->_nvs_param_map[NVS_KEY_NEOPIXEL_EFFECT] = value;
    app->setNvsParam(NVS_KEY_NEOPIXEL_EFFECT, value);
    app->applyNeopixelConfig();
    app->refreshDisplayIdleUi();
    app->refreshJoypadUi();

end:
    return;
}

void AppSettings::onSliderPanelScreenSettingNeopixelBrightnessValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    int32_t brightness_value = 0;
    lv_obj_t *target = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid Neopixel brightness slider");
    brightness_value = lv_slider_get_value(target);
    app->_nvs_param_map[NVS_KEY_NEOPIXEL_BRIGHTNESS] = brightness_value;
    app->setNvsParam(NVS_KEY_NEOPIXEL_BRIGHTNESS, brightness_value);
    app->applyNeopixelConfig();
    app->refreshDisplayIdleUi();
    app->refreshJoypadUi();

end:
    return;
}

void AppSettings::onDropdownJoypadLocalHapticGpioValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *target = nullptr;
    uint16_t selected = 0;
    int32_t value = -1;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid haptic GPIO dropdown");
    selected = lv_dropdown_get_selected(target);
    value = getDropdownValueForIndex(kNeopixelGpioOptions,
                                     sizeof(kNeopixelGpioOptions) / sizeof(kNeopixelGpioOptions[0]),
                                     selected);
    value = sanitizeHapticGpio(value);
    app->_nvs_param_map[NVS_KEY_AUDIO_HAPTIC_GPIO] = value;
    app->setNvsParam(NVS_KEY_AUDIO_HAPTIC_GPIO, value);
    jc_ui_haptic_feedback_set_gpio(value);
    jc_ui_haptic_feedback_test();
    app->refreshJoypadUi();

end:
    return;
}

void AppSettings::onDropdownJoypadLocalHapticLevelValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *target = nullptr;
    uint16_t selected = 0;
    int32_t value = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid haptic level dropdown");
    selected = lv_dropdown_get_selected(target);
    value = getDropdownValueForIndex(kHapticLevelOptions,
                                     sizeof(kHapticLevelOptions) / sizeof(kHapticLevelOptions[0]),
                                     selected);
    app->_nvs_param_map[NVS_KEY_AUDIO_HAPTIC_LEVEL] = value;
    app->setNvsParam(NVS_KEY_AUDIO_HAPTIC_LEVEL, value);
    jc_ui_haptic_feedback_set_level(value);
    jc_ui_haptic_feedback_test();
    app->refreshJoypadUi();

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

void AppSettings::onSwitchPanelScreenSettingTimeoffInGameValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *target = nullptr;
    bool enabled = false;
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    target = lv_event_get_target(e);
    if (target == nullptr) {
        ESP_LOGE(TAG, "Invalid screen timeoff in game switch");
        return;
    }

    enabled = (lv_obj_get_state(target) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF_IN_GAME] = enabled;
    app->setNvsParam(NVS_KEY_DISPLAY_TIMEOFF_IN_GAME, enabled ? 1 : 0);
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

void AppSettings::onDisplayOrientationPreviewPopupEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *msgbox = lv_event_get_current_target(e);
    const lv_event_code_t code = lv_event_get_code(e);
    if (app == nullptr) {
        return;
    }

    if (code == LV_EVENT_DELETE) {
        if (app->_displayOrientationPreviewResolving) {
            app->_displayOrientationPreviewMsgbox = nullptr;
            app->_displayOrientationPreviewLabel = nullptr;
            app->_displayOrientationPreviewSpinner = nullptr;
            app->_displayOrientationPreviewCountdownLabel = nullptr;
            app->_displayOrientationPreviewSecondsRemaining = 0;
            app->_displayOrientationPreviewResolving = false;
            return;
        }

        if (msgbox == app->_displayOrientationPreviewMsgbox) {
            app->_displayOrientationPreviewMsgbox = nullptr;
            app->_displayOrientationPreviewLabel = nullptr;
            app->_displayOrientationPreviewSpinner = nullptr;
            app->_displayOrientationPreviewCountdownLabel = nullptr;
            app->finishDisplayOrientationPreview(false);
        }
        return;
    }

    if ((code != LV_EVENT_VALUE_CHANGED) || (msgbox == nullptr)) {
        return;
    }

    const char *button_text = lv_msgbox_get_active_btn_text(msgbox);
    if (button_text == nullptr) {
        return;
    }

    if (strcmp(button_text, "OK") == 0) {
        app->finishDisplayOrientationPreview(true);
    } else if (strcmp(button_text, "Cancel") == 0) {
        app->finishDisplayOrientationPreview(false);
    }
}

void AppSettings::onDisplayOrientationPreviewTimerCallback(lv_timer_t *timer)
{
    AppSettings *app = (timer != nullptr) ? static_cast<AppSettings *>(timer->user_data) : nullptr;
    if (app == nullptr) {
        return;
    }

    if (app->_displayOrientationPreviewSecondsRemaining > 0) {
        --app->_displayOrientationPreviewSecondsRemaining;
    }

    if (app->_displayOrientationPreviewSecondsRemaining <= 0) {
        app->finishDisplayOrientationPreview(false);
        return;
    }

    app->updateDisplayOrientationPreviewPopup();
}

void AppSettings::onDropdownPanelScreenSettingOrientationValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    uint16_t selected_index = 0;
    int32_t selected_value = 0;
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    const int32_t previous_value = sanitizeDisplayOrientationDegrees(app->_nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION]);

    selected_index = lv_dropdown_get_selected(app->_displayOrientationDropdown);
    selected_value = getDropdownValueForIndex(kDisplayOrientationOptionsDeg,
                                              sizeof(kDisplayOrientationOptionsDeg) / sizeof(kDisplayOrientationOptionsDeg[0]),
                                              selected_index);
    selected_value = sanitizeDisplayOrientationDegrees(selected_value);
    if (selected_value == previous_value) {
        return;
    }

    app->requestDisplayOrientationPreview(selected_value);
    ESP_LOGI(TAG, "Display orientation updated live to %ld degrees", static_cast<long>(selected_value));
}

void AppSettings::onSwitchPanelScreenSettingAutorotateValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *target = nullptr;
    bool enabled = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid autorotate switch");
    enabled = (lv_obj_get_state(target) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE] = enabled ? 1 : 0;
    app->setNvsParam(NVS_KEY_DISPLAY_AUTOROTATE, enabled ? 1 : 0);
    app->_displayAutorotateHasAppliedOrientation = false;
    if (enabled) {
        app->startImuLivePolling();
    } else {
        app->updateDisplayAutorotateFromSample(nullptr, false);
        if (app->_screen_index != UI_IMU_SETTING_INDEX) {
            app->stopImuLivePolling();
        }
    }
    app->refreshDisplayAutorotateUi();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingAutorotateImuValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *target = nullptr;
    uint16_t selected = 0;
    int32_t value = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid rotation axis dropdown");
    selected = lv_dropdown_get_selected(target);
    value = getDropdownValueForIndex(kDisplayAutorotateAxisOptions,
                                     sizeof(kDisplayAutorotateAxisOptions) / sizeof(kDisplayAutorotateAxisOptions[0]),
                                     selected);
    value = sanitizeDisplayAutorotateAxis(value);
    app->_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_IMU] = value;
    app->setNvsParam(NVS_KEY_DISPLAY_AUTOROTATE_IMU, value);
    app->refreshDisplayAutorotateUi();

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
 