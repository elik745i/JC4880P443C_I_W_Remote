#include "battery_history_service.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "adc_battery_estimation.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {

static const char *TAG = "BatteryHistory";
static constexpr uint32_t kBatterySamplerTaskStack = 4096;
static constexpr TickType_t kBatteryPollPeriod = pdMS_TO_TICKS(5000);
static constexpr int64_t kBatteryHistorySamplePeriodSec = 60;
static constexpr uint8_t kSampleFlagCharging = 0x01;

static adc_battery_estimation_handle_t s_batteryEstimationHandle = nullptr;
static battery_history_service::HistorySample *s_historySamples = nullptr;
static SemaphoreHandle_t s_historyMutex = nullptr;
static std::atomic<bool> s_initialized{false};
static std::size_t s_historyCount = 0;
static std::size_t s_historyHead = 0;
static int64_t s_nextHistorySampleSec = 0;
static battery_history_service::Status s_latestStatus = {
    false,
    false,
    0,
    0,
    -1,
    false,
    0,
    0,
};

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

static bool initialize_battery_estimation_handle(void)
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

static std::size_t history_oldest_index_locked(void)
{
    if (s_historyCount == 0) {
        return 0;
    }

    return (s_historyHead + battery_history_service::kMaxHistorySamples - s_historyCount) % battery_history_service::kMaxHistorySamples;
}

static void append_history_sample_locked(int64_t timestamp_sec, int16_t capacity_tenths, bool charging)
{
    if (s_historySamples == nullptr) {
        return;
    }

    s_historySamples[s_historyHead].timestamp_sec = timestamp_sec;
    s_historySamples[s_historyHead].capacity_tenths = capacity_tenths;
    s_historySamples[s_historyHead].flags = charging ? kSampleFlagCharging : 0;

    s_historyHead = (s_historyHead + 1) % battery_history_service::kMaxHistorySamples;
    s_historyCount = std::min<std::size_t>(s_historyCount + 1, battery_history_service::kMaxHistorySamples);
}

static int32_t calculate_eta_minutes_locked(bool charging, int current_capacity_tenths)
{
    if (s_historyCount < 2) {
        return -1;
    }

    battery_history_service::HistorySample newest = {};
    battery_history_service::HistorySample oldest = {};
    bool have_newest = false;
    bool have_oldest = false;
    std::size_t matching_count = 0;

    for (std::size_t offset = 0; offset < s_historyCount && matching_count < 12; ++offset) {
        const std::size_t index = (s_historyHead + battery_history_service::kMaxHistorySamples - 1 - offset) % battery_history_service::kMaxHistorySamples;
        const auto &sample = s_historySamples[index];
        const bool sample_charging = (sample.flags & kSampleFlagCharging) != 0;
        if (sample_charging != charging) {
            if (matching_count > 0) {
                break;
            }
            continue;
        }

        if (!have_newest) {
            newest = sample;
            have_newest = true;
        }
        oldest = sample;
        have_oldest = true;
        ++matching_count;
    }

    if (!have_newest || !have_oldest || (matching_count < 2)) {
        return -1;
    }

    const int64_t delta_sec = newest.timestamp_sec - oldest.timestamp_sec;
    const int delta_tenths = static_cast<int>(newest.capacity_tenths - oldest.capacity_tenths);
    if (delta_sec <= 0) {
        return -1;
    }

    if (charging) {
        if (delta_tenths <= 0) {
            return -1;
        }
    } else {
        if (delta_tenths >= 0) {
            return -1;
        }
    }

    const int rate_tenths_per_hour = static_cast<int>((std::abs(delta_tenths) * 3600LL) / delta_sec);
    if (rate_tenths_per_hour < 5) {
        return -1;
    }

    const int remaining_tenths = charging ? std::max(0, 1000 - current_capacity_tenths) : std::max(0, current_capacity_tenths);
    if (remaining_tenths <= 0) {
        return 0;
    }

    return std::max<int32_t>(0, static_cast<int32_t>((remaining_tenths * 60LL) / rate_tenths_per_hour));
}

static void sample_battery_state(bool store_history)
{
    if ((s_batteryEstimationHandle == nullptr) || (s_historyMutex == nullptr)) {
        return;
    }

    float capacity = 0.0f;
    bool charging = false;
    if ((adc_battery_estimation_get_capacity(s_batteryEstimationHandle, &capacity) != ESP_OK) ||
        (adc_battery_estimation_get_charging_state(s_batteryEstimationHandle, &charging) != ESP_OK)) {
        return;
    }

    const int64_t now_sec = esp_timer_get_time() / 1000000LL;
    const int capacity_tenths = std::max(0, std::min(1000, static_cast<int>((capacity * 10.0f) + 0.5f)));

    if (xSemaphoreTake(s_historyMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    if (store_history) {
        append_history_sample_locked(now_sec, static_cast<int16_t>(capacity_tenths), charging);
    }

    s_latestStatus.available = true;
    s_latestStatus.charging = charging;
    s_latestStatus.capacity_tenths = capacity_tenths;
    s_latestStatus.capacity_percent = (capacity_tenths + 5) / 10;
    s_latestStatus.eta_to_full = charging;
    s_latestStatus.eta_minutes = calculate_eta_minutes_locked(charging, capacity_tenths);
    s_latestStatus.sample_count = s_historyCount;
    s_latestStatus.timestamp_sec = now_sec;

    xSemaphoreGive(s_historyMutex);
}

static void battery_sampler_task(void *arg)
{
    (void)arg;

    while (true) {
        const int64_t now_sec = esp_timer_get_time() / 1000000LL;
        const bool should_store = (s_nextHistorySampleSec == 0) || (now_sec >= s_nextHistorySampleSec);
        sample_battery_state(should_store);
        if (should_store) {
            s_nextHistorySampleSec = now_sec + kBatteryHistorySamplePeriodSec;
        }
        vTaskDelay(kBatteryPollPeriod);
    }
}

} // namespace

namespace battery_history_service {

bool initialize()
{
#if !CONFIG_JC4880_FEATURE_BATTERY
    return false;
#else
    if (s_initialized.load()) {
        return true;
    }

    if (!initialize_battery_estimation_handle()) {
        return false;
    }

    if (s_historyMutex == nullptr) {
        s_historyMutex = xSemaphoreCreateMutex();
        if (s_historyMutex == nullptr) {
            ESP_LOGE(TAG, "Failed to create battery history mutex");
            return false;
        }
    }

    if (s_historySamples == nullptr) {
        s_historySamples = static_cast<HistorySample *>(heap_caps_malloc(sizeof(HistorySample) * kMaxHistorySamples,
                                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (s_historySamples == nullptr) {
            s_historySamples = static_cast<HistorySample *>(heap_caps_malloc(sizeof(HistorySample) * kMaxHistorySamples,
                                                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }
        if (s_historySamples == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate battery history buffer");
            return false;
        }
        std::memset(s_historySamples, 0, sizeof(HistorySample) * kMaxHistorySamples);
    }

    sample_battery_state(true);

    if (create_background_task_prefer_psram(battery_sampler_task,
                                            "battery_sampler",
                                            kBatterySamplerTaskStack,
                                            nullptr,
                                            1,
                                            1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start battery sampler task");
        return false;
    }

    s_initialized.store(true);
    return true;
#endif
}

bool get_status(Status &status)
{
#if !CONFIG_JC4880_FEATURE_BATTERY
    status = {false, false, 0, 0, -1, false, 0, 0};
    return false;
#else
    if (!s_initialized.load() && !initialize()) {
        status = {false, false, 0, 0, -1, false, 0, 0};
        return false;
    }

    if ((s_historyMutex == nullptr) || (xSemaphoreTake(s_historyMutex, pdMS_TO_TICKS(20)) != pdTRUE)) {
        return false;
    }

    status = s_latestStatus;
    xSemaphoreGive(s_historyMutex);
    return status.available;
#endif
}

std::size_t copy_samples(HistorySample *destination, std::size_t max_samples)
{
#if !CONFIG_JC4880_FEATURE_BATTERY
    (void)destination;
    (void)max_samples;
    return 0;
#else
    if ((destination == nullptr) || (max_samples == 0)) {
        return 0;
    }

    if (!s_initialized.load() && !initialize()) {
        return 0;
    }

    if ((s_historyMutex == nullptr) || (xSemaphoreTake(s_historyMutex, pdMS_TO_TICKS(50)) != pdTRUE)) {
        return 0;
    }

    const std::size_t copy_count = std::min(max_samples, s_historyCount);
    const std::size_t oldest_index = history_oldest_index_locked();
    for (std::size_t index = 0; index < copy_count; ++index) {
        const std::size_t source_index = (oldest_index + index) % kMaxHistorySamples;
        destination[index] = s_historySamples[source_index];
    }

    xSemaphoreGive(s_historyMutex);
    return copy_count;
#endif
}

} // namespace battery_history_service