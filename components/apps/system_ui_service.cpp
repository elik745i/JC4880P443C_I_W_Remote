#include "system_ui_service.h"

#include <algorithm>
#include <atomic>
#include <ctime>

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "adc_battery_estimation.h"
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
static adc_battery_estimation_handle_t s_batteryEstimationHandle = nullptr;
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
        if ((s_statusBar != nullptr) && (s_batteryEstimationHandle != nullptr)) {
            float capacity = 0.0f;
            bool charging = false;
            if ((adc_battery_estimation_get_capacity(s_batteryEstimationHandle, &capacity) == ESP_OK) &&
                (adc_battery_estimation_get_charging_state(s_batteryEstimationHandle, &charging) == ESP_OK)) {
                bsp_display_lock(0);
                s_statusBar->setBatteryPercent(charging, static_cast<int>(capacity));
                bsp_display_unlock();
            }
        }

        vTaskDelay(kBatteryRefreshPeriod);
    }
}

static bool initialize_battery_estimation(void)
{
    if (s_batteryEstimationHandle != nullptr) {
        return true;
    }

    adc_battery_estimation_t config = {
        .internal = {
            .adc_unit = ADC_UNIT_2,
            .adc_bitwidth = ADC_BITWIDTH_DEFAULT,
            .adc_atten = ADC_ATTEN_DB_12,
        },
        .adc_channel = ADC_CHANNEL_4,
        .upper_resistor = 68000,
        .lower_resistor = 100000,
        .battery_points = default_battery_points,
        .battery_points_count = DEFAULT_POINTS_COUNT,
        .charging_detect_cb = nullptr,
        .charging_detect_user_data = nullptr,
    };

    s_batteryEstimationHandle = adc_battery_estimation_create(&config);
    if (s_batteryEstimationHandle == nullptr) {
        ESP_LOGW(TAG, "Failed to initialize battery estimation handle");
        return false;
    }

    return true;
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

    initialize_battery_estimation();

    if (create_background_task_prefer_psram(status_refresh_task,
                                            "status_refresh",
                                            kStatusRefreshTaskStack,
                                            nullptr,
                                            1,
                                            1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start status refresh task");
        return false;
    }

    if (s_batteryEstimationHandle != nullptr) {
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