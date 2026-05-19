#include "system_ui_service.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <ctime>

#if CONFIG_JC4880_FEATURE_IMU
#include "ImuService.hpp"
#endif
#include "battery_history_service.h"
#include "hardware_history_service.h"
#include "joypad_runtime.h"
#include "joypad_transport.h"
#include "setting/wifi/SettingWifiPrivate.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"

#include "bsp/esp-bsp.h"
#include "esp_brookesia.hpp"

namespace {

static const char *TAG = "SystemUiService";
static constexpr uint32_t kStatusRefreshTaskStack = 4096;
static constexpr uint32_t kBatteryRefreshTaskStack = 4096;
#if CONFIG_JC4880_FEATURE_IMU
static constexpr uint32_t kImuAutorotateTaskStack = 4096;
static constexpr TickType_t kStatusRefreshPeriod = pdMS_TO_TICKS(1000);
static constexpr TickType_t kBatteryRefreshPeriod = pdMS_TO_TICKS(5000);
static constexpr TickType_t kImuAutorotateActivePeriod = pdMS_TO_TICKS(150);
static constexpr TickType_t kImuAutorotateIdlePeriod = pdMS_TO_TICKS(1000);
static constexpr int64_t kImuAutorotateRetryDelayUs = 3LL * 1000LL * 1000LL;
#else
static constexpr TickType_t kStatusRefreshPeriod = pdMS_TO_TICKS(1000);
static constexpr TickType_t kBatteryRefreshPeriod = pdMS_TO_TICKS(5000);
#endif
static constexpr const char *kNvsStorageNamespace = "storage";
static constexpr const char *kNvsKeyDisplayOrientation = "disp_rot";
static constexpr const char *kNvsKeyDisplayAutorotate = "disp_auto_rot";
#if CONFIG_JC4880_FEATURE_IMU
static constexpr const char *kNvsKeyDisplayAutorotateImu = "disp_auto_imu";
#endif

static ESP_Brookesia_StatusBar *s_statusBar = nullptr;
static lv_obj_t *s_statusBarRightIndicators = nullptr;
static lv_obj_t *s_loraUnreadLabel = nullptr;
static std::atomic<bool> s_initialized{false};
static std::atomic<bool> s_wifiConnected{false};
static std::atomic<int> s_wifiSignalLevel{0};
static std::atomic<bool> s_loraUnread{false};
static nvs_handle_t s_settingsNvsHandle = 0;
#if CONFIG_JC4880_FEATURE_IMU
static int32_t s_imuAutorotateAppliedOrientation = 0;
static bool s_imuAutorotateHasAppliedOrientation = false;
static int64_t s_imuAutorotateRetryAfterUs = 0;
#endif

static BaseType_t create_background_task_prefer_psram(TaskFunction_t task,
                                                      const char *name,
                                                      const uint32_t stack_depth,
                                                      void *arg,
                                                      const UBaseType_t priority,
                                                      const BaseType_t core_id)
{
    if (xTaskCreatePinnedToCoreWithCaps(task,
                                        name,
                                        stack_depth,
                                        arg,
                                        priority,
                                        nullptr,
                                        core_id,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        return pdPASS;
    }

    ESP_LOGW(TAG, "Falling back to internal RAM stack for %s", name);
    return xTaskCreatePinnedToCore(task, name, stack_depth, arg, priority, nullptr, core_id);
}

static int wifi_signal_strength_from_rssi(int rssi)
{
    if (rssi > -50) {
        return 3;
    }
    if (rssi > -60) {
        return 2;
    }
    if (rssi > -75) {
        return 1;
    }
    return 0;
}

static int get_wifi_level_from_driver(bool *connected)
{
    wifi_ap_record_t ap_info = {};

    if (!s_wifi_runtime_ready) {
        if (connected != nullptr) {
            *connected = false;
        }
        return 0;
    }

    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        if (connected != nullptr) {
            *connected = true;
        }

        const int wifi_level = std::max(1, wifi_signal_strength_from_rssi(ap_info.rssi));
        s_wifiSignalLevel.store(wifi_level);
        s_wifiConnected.store(true);
        return wifi_level;
    }

    if (connected != nullptr) {
        *connected = false;
    }

    s_wifiSignalLevel.store(0);
    s_wifiConnected.store(false);
    return 0;
}

static int32_t read_setting_i32(const char *key, int32_t default_value)
{
    if (s_settingsNvsHandle == 0) {
        return default_value;
    }

    int32_t value = default_value;
    if (nvs_get_i32(s_settingsNvsHandle, key, &value) != ESP_OK) {
        return default_value;
    }
    return value;
}

static int32_t sanitize_display_orientation_degrees(int32_t orientation_degrees)
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

static lv_disp_rotation_t display_orientation_degrees_to_lv_rotation(int32_t orientation_degrees)
{
    switch (sanitize_display_orientation_degrees(orientation_degrees)) {
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

static bool apply_display_orientation_live(int32_t orientation_degrees)
{
    lv_display_t *display = lv_disp_get_default();
    if (display == nullptr) {
        return false;
    }

    if (!bsp_display_lock(0)) {
        return false;
    }

    bsp_display_rotate(display, display_orientation_degrees_to_lv_rotation(orientation_degrees));
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

static int32_t opposite_display_orientation_degrees(int32_t orientation_degrees)
{
    switch (sanitize_display_orientation_degrees(orientation_degrees)) {
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

static int32_t sanitize_display_autorotate_axis(int32_t axis)
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

#if CONFIG_JC4880_FEATURE_IMU
static void update_display_autorotate_from_sample(const jc4880::imu::ImuSample *sample,
                                                  bool sample_ok,
                                                  bool enabled,
                                                  int32_t base_orientation,
                                                  int32_t rotation_axis)
{
    if (!enabled) {
        if (!s_imuAutorotateHasAppliedOrientation || (s_imuAutorotateAppliedOrientation != base_orientation)) {
            if (apply_display_orientation_live(base_orientation)) {
                s_imuAutorotateAppliedOrientation = base_orientation;
                s_imuAutorotateHasAppliedOrientation = true;
            }
        }
        return;
    }

    if ((sample == nullptr) || !sample_ok || !sample->hasAccel) {
        return;
    }

    float selected_angle = sample->roll;
    switch (sanitize_display_autorotate_axis(rotation_axis)) {
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

    const int32_t inverted_orientation = opposite_display_orientation_degrees(base_orientation);
    const bool currently_inverted = s_imuAutorotateHasAppliedOrientation &&
                                    (s_imuAutorotateAppliedOrientation == inverted_orientation);
    int32_t target_orientation = currently_inverted ? inverted_orientation : base_orientation;

    if (currently_inverted) {
        if (std::fabs(selected_angle) <= 70.0f) {
            target_orientation = base_orientation;
        }
    } else if (std::fabs(selected_angle) >= 110.0f) {
        target_orientation = inverted_orientation;
    }

    if (!s_imuAutorotateHasAppliedOrientation || (target_orientation != s_imuAutorotateAppliedOrientation)) {
        if (apply_display_orientation_live(target_orientation)) {
            s_imuAutorotateAppliedOrientation = target_orientation;
            s_imuAutorotateHasAppliedOrientation = true;
        }
    }
}
#endif

static void update_status_bar_clock_and_wifi(void)
{
    if (s_statusBar == nullptr) {
        return;
    }

    time_t now = 0;
    struct tm timeinfo = {};
    time(&now);
    localtime_r(&now, &timeinfo);

    bool wifi_connected = false;
    int wifi_level = get_wifi_level_from_driver(&wifi_connected);

    if (!wifi_connected && s_wifiConnected.load()) {
        wifi_level = std::max(1, s_wifiSignalLevel.load());
    }

    bsp_display_lock(0);
    s_statusBar->setClock(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_hour >= 12);
    s_statusBar->setWifiIconState(wifi_level);
    bsp_display_unlock();
}

static void update_lora_unread_indicator(void)
{
    if (s_loraUnreadLabel == nullptr) {
        return;
    }

    bsp_display_lock(0);
    if (s_loraUnread.load()) {
        lv_obj_clear_flag(s_loraUnreadLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_loraUnreadLabel, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}

static lv_obj_t *get_status_bar_right_area(lv_obj_t *status_bar_main)
{
    if (status_bar_main == nullptr) {
        return nullptr;
    }

    const uint32_t child_count = lv_obj_get_child_cnt(status_bar_main);
    if (child_count == 0U) {
        return nullptr;
    }

    return lv_obj_get_child(status_bar_main, static_cast<int32_t>(child_count - 1U));
}

static void init_status_bar_right_indicators(lv_obj_t *status_bar_main)
{
    lv_obj_t *right_area = get_status_bar_right_area(status_bar_main);
    if (right_area == nullptr) {
        return;
    }

    const uint32_t existing_child_count = lv_obj_get_child_cnt(right_area);
    lv_obj_t *wifi_icon = nullptr;
    if (existing_child_count > 0U) {
        wifi_icon = lv_obj_get_child(right_area, static_cast<int32_t>(existing_child_count - 1U));
    }

    s_statusBarRightIndicators = lv_obj_create(right_area);
    if (s_statusBarRightIndicators == nullptr) {
        return;
    }

    lv_obj_remove_style_all(s_statusBarRightIndicators);
    lv_obj_set_size(s_statusBarRightIndicators, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_statusBarRightIndicators, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_statusBarRightIndicators, 4, 0);
    lv_obj_set_style_pad_all(s_statusBarRightIndicators, 0, 0);
    lv_obj_clear_flag(s_statusBarRightIndicators, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(s_statusBarRightIndicators, 0);
    if (wifi_icon != nullptr) {
        lv_obj_move_to_index(wifi_icon, 1);
    }

    s_loraUnreadLabel = lv_label_create(s_statusBarRightIndicators);
    if (s_loraUnreadLabel == nullptr) {
        return;
    }

    lv_label_set_text(s_loraUnreadLabel, LV_SYMBOL_ENVELOPE);
    lv_obj_set_style_text_color(s_loraUnreadLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_loraUnreadLabel, &lv_font_montserrat_16, 0);
    lv_obj_add_flag(s_loraUnreadLabel, LV_OBJ_FLAG_HIDDEN);
}

static void status_refresh_task(void *arg)
{
    (void)arg;

    while (true) {
        update_status_bar_clock_and_wifi();
        vTaskDelay(kStatusRefreshPeriod);
    }
}

static void battery_refresh_task(void *arg)
{
    (void)arg;

    while (true) {
        if (s_statusBar != nullptr) {
            battery_history_service::Status status = {};
            if (battery_history_service::get_status(status)) {
                bsp_display_lock(0);
                s_statusBar->setBatteryPercent(status.charging, status.capacity_percent);
                bsp_display_unlock();
            }
        }

        vTaskDelay(kBatteryRefreshPeriod);
    }
}

#if CONFIG_JC4880_FEATURE_IMU
static void imu_autorotate_task(void *arg)
{
    (void)arg;

    while (true) {
        const int32_t base_orientation = sanitize_display_orientation_degrees(
            read_setting_i32(kNvsKeyDisplayOrientation, 0));
        const bool enabled = read_setting_i32(kNvsKeyDisplayAutorotate, 0) != 0;
        const int32_t rotation_axis = sanitize_display_autorotate_axis(
            read_setting_i32(kNvsKeyDisplayAutorotateImu, 0));

        if (!enabled) {
            update_display_autorotate_from_sample(nullptr, false, false, base_orientation, rotation_axis);
            s_imuAutorotateRetryAfterUs = 0;
            vTaskDelay(kImuAutorotateIdlePeriod);
            continue;
        }

        jc4880::imu::ImuConfig config = {};
        if (!jc4880::imu::ImuService::instance().loadConfig(config) || !config.enabled ||
            (config.model == jc4880::imu::ImuModel::IMU_NONE)) {
            update_display_autorotate_from_sample(nullptr, false, false, base_orientation, rotation_axis);
            s_imuAutorotateRetryAfterUs = 0;
            vTaskDelay(kImuAutorotateIdlePeriod);
            continue;
        }

        jc4880::imu::ImuSample sample = {};
        bool sample_ok = jc4880::imu::ImuService::instance().read(sample);
        const int64_t now_us = esp_timer_get_time();
        if (!sample_ok && (now_us >= s_imuAutorotateRetryAfterUs)) {
            if (jc4880::imu::ImuService::instance().begin(&config)) {
                sample_ok = jc4880::imu::ImuService::instance().read(sample);
                s_imuAutorotateRetryAfterUs = sample_ok ? 0 : (now_us + kImuAutorotateRetryDelayUs);
            } else {
                s_imuAutorotateRetryAfterUs = now_us + kImuAutorotateRetryDelayUs;
            }
        }

        update_display_autorotate_from_sample(sample_ok ? &sample : nullptr,
                                              sample_ok,
                                              true,
                                              base_orientation,
                                              rotation_axis);
        vTaskDelay(sample_ok ? kImuAutorotateActivePeriod : kImuAutorotateIdlePeriod);
    }
}
#endif

} // namespace

namespace system_ui_service {

bool initialize(ESP_Brookesia_Phone &phone)
{
    if (s_initialized.load()) {
        return true;
    }

    s_statusBar = phone.getHome().getStatusBar();
    if (s_statusBar == nullptr) {
        ESP_LOGW(TAG, "Status bar is unavailable during system UI service init");
        return false;
    }
    s_statusBar->setClockFormat(ESP_Brookesia_StatusBar::ClockFormat::FORMAT_24H);
    s_statusBar->hideBatteryPercent();

    if (lv_obj_t *status_bar_main = s_statusBar->getMainObject(); status_bar_main != nullptr) {
        init_status_bar_right_indicators(status_bar_main);
    }

#if CONFIG_JC4880_FEATURE_BATTERY
    jc4880_joypad_config_t joypadConfig = {};
    const bool local_controller_active = jc4880_joypad_get_config(&joypadConfig) &&
                                         (joypadConfig.backend == JC4880_JOYPAD_BACKEND_MANUAL);
    battery_history_service::set_adc_attached(!local_controller_active);
    battery_history_service::initialize();
#endif
    hardware_history_service::initialize();
    if (!joypad_transport::initialize()) {
        ESP_LOGW(TAG, "Joypad transport initialization failed");
    }

    if (nvs_open(kNvsStorageNamespace, NVS_READWRITE, &s_settingsNvsHandle) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open settings storage for IMU autorotate state");
        s_settingsNvsHandle = 0;
    }

    if (create_background_task_prefer_psram(status_refresh_task,
                                            "status_refresh",
                                            kStatusRefreshTaskStack,
                                            nullptr,
                                            1,
                                            1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start status refresh task");
        return false;
    }

    if (battery_history_service::initialize()) {
        if (create_background_task_prefer_psram(battery_refresh_task,
                                                "battery_refresh",
                                                kBatteryRefreshTaskStack,
                                                nullptr,
                                                1,
                                                1) != pdPASS) {
            ESP_LOGW(TAG, "Failed to start battery refresh task");
        }
    }

#if CONFIG_JC4880_FEATURE_IMU
    if (create_background_task_prefer_psram(imu_autorotate_task,
                                            "imu_autorotate",
                                            kImuAutorotateTaskStack,
                                            nullptr,
                                            1,
                                            1) != pdPASS) {
        ESP_LOGW(TAG, "Failed to start IMU autorotate task");
    }
#endif

    s_initialized.store(true);
    update_status_bar_clock_and_wifi();
    update_lora_unread_indicator();

    return true;
}

void set_wifi_connected(bool connected)
{
    s_wifiConnected.store(connected);
    if (!connected) {
        s_wifiSignalLevel.store(0);
    }
    update_status_bar_clock_and_wifi();
}

void refresh_wifi_from_driver(void)
{
    get_wifi_level_from_driver(nullptr);
    update_status_bar_clock_and_wifi();
}

void set_lora_unread(bool unread)
{
    s_loraUnread.store(unread);
    update_lora_unread_indicator();
}

} // namespace system_ui_service