#include "system_ui_service.h"

#include <algorithm>
#include <atomic>
#include <ctime>

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
#include "esp_wifi.h"

#include "bsp/esp-bsp.h"
#include "esp_brookesia.hpp"

namespace {

static const char *TAG = "SystemUiService";
static constexpr uint32_t kStatusRefreshTaskStack = 4096;
static constexpr uint32_t kBatteryRefreshTaskStack = 4096;
static constexpr TickType_t kStatusRefreshPeriod = pdMS_TO_TICKS(2000);
static constexpr TickType_t kBatteryRefreshPeriod = pdMS_TO_TICKS(5000);

static ESP_Brookesia_StatusBar *s_statusBar = nullptr;
static std::atomic<bool> s_initialized{false};
static std::atomic<bool> s_wifiConnected{false};
static std::atomic<int> s_wifiSignalLevel{0};

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
    s_statusBar->setClock(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_hour >= 12);
    s_statusBar->setWifiIconState(wifi_level);
    bsp_display_unlock();
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

    s_initialized.store(true);
    update_status_bar_clock_and_wifi();

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

} // namespace system_ui_service