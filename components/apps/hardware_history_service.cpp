#include "hardware_history_service.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#if __has_include("driver/temperature_sensor.h")
#include "driver/temperature_sensor.h"
#define HARDWARE_HISTORY_HAS_TEMPERATURE_SENSOR 1
#else
#define HARDWARE_HISTORY_HAS_TEMPERATURE_SENSOR 0
#endif

namespace {

static const char *TAG = "HardwareHistory";
static constexpr uint32_t kSamplerTaskStack = 4096;
static constexpr TickType_t kSamplePeriod = pdMS_TO_TICKS(1000);
static constexpr int64_t kMediumSamplePeriodSec = 10;
static constexpr int64_t kSlowSamplePeriodSec = 60;

struct RingBuffer {
    uint8_t *values;
    std::size_t capacity;
    std::size_t count;
    std::size_t head;
};

static SemaphoreHandle_t s_mutex = nullptr;
static std::atomic<bool> s_initialized{false};
static uint8_t *s_storage = nullptr;
static RingBuffer s_cpuLoadHistory = {};
static RingBuffer s_sramHistory = {};
static RingBuffer s_psramHistory = {};
static RingBuffer s_cpuTempHistory = {};
static RingBuffer s_wifiHistory = {};
static hardware_history_service::Snapshot s_snapshot = {
    false,
    false,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    false,
    0,
    false,
    0,
    0,
    {0},
};
static int64_t s_nextMediumSampleSec = 0;
static int64_t s_nextSlowSampleSec = 0;

#if HARDWARE_HISTORY_HAS_TEMPERATURE_SENSOR
static temperature_sensor_handle_t s_temperatureSensor = nullptr;
static bool s_temperatureSensorInitialized = false;
static bool s_temperatureSensorFailed = false;

static bool ensure_temperature_sensor_ready(void)
{
    if (s_temperatureSensorInitialized) {
        return true;
    }

    if (s_temperatureSensorFailed) {
        return false;
    }

    temperature_sensor_config_t sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    esp_err_t ret = temperature_sensor_install(&sensor_config, &s_temperatureSensor);
    if ((ret != ESP_OK) || (s_temperatureSensor == nullptr)) {
        s_temperatureSensorFailed = true;
        return false;
    }

    ret = temperature_sensor_enable(s_temperatureSensor);
    if (ret != ESP_OK) {
        temperature_sensor_uninstall(s_temperatureSensor);
        s_temperatureSensor = nullptr;
        s_temperatureSensorFailed = true;
        return false;
    }

    s_temperatureSensorInitialized = true;
    return true;
}

static bool read_cpu_temperature_celsius(float &temperature_celsius)
{
    if (!ensure_temperature_sensor_ready()) {
        return false;
    }

    return temperature_sensor_get_celsius(s_temperatureSensor, &temperature_celsius) == ESP_OK;
}
#else
static bool read_cpu_temperature_celsius(float &temperature_celsius)
{
    (void)temperature_celsius;
    return false;
}
#endif

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && !CONFIG_FREERTOS_SMP
static bool s_haveCpuBaseline = false;
static int64_t s_lastCpuSampleUs = 0;
static configRUN_TIME_COUNTER_TYPE s_lastIdleRunTime[portNUM_PROCESSORS] = {};
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

static void initialize_ring(RingBuffer &ring, uint8_t *storage, std::size_t capacity)
{
    ring.values = storage;
    ring.capacity = capacity;
    ring.count = 0;
    ring.head = 0;
    if (ring.values != nullptr) {
        std::memset(ring.values, 0, capacity);
    }
}

static void append_sample_locked(RingBuffer &ring, uint8_t value)
{
    if ((ring.values == nullptr) || (ring.capacity == 0)) {
        return;
    }

    ring.values[ring.head] = value;
    ring.head = (ring.head + 1) % ring.capacity;
    ring.count = std::min(ring.count + 1, ring.capacity);
}

static std::size_t copy_ring_samples_locked(const RingBuffer &ring, uint8_t *destination, std::size_t max_samples)
{
    if ((destination == nullptr) || (max_samples == 0) || (ring.values == nullptr) || (ring.count == 0)) {
        return 0;
    }

    const std::size_t copy_count = std::min(max_samples, ring.count);
    const std::size_t oldest_index = (ring.head + ring.capacity - ring.count) % ring.capacity;
    for (std::size_t index = 0; index < copy_count; ++index) {
        destination[index] = ring.values[(oldest_index + index) % ring.capacity];
    }

    return copy_count;
}

static int clamp_percent(int value)
{
    return std::max(0, std::min(100, value));
}

static int calculate_percent(uint64_t used, uint64_t total)
{
    if (total == 0) {
        return 0;
    }

    return clamp_percent(static_cast<int>((used * 100ULL) / total));
}

static int wifi_percent_from_rssi(int rssi)
{
    return clamp_percent((rssi + 100) * 2);
}

static int sample_cpu_load_percent(bool *available)
{
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && !CONFIG_FREERTOS_SMP
    const int64_t now_us = esp_timer_get_time();
    configRUN_TIME_COUNTER_TYPE current_idle_run_time[portNUM_PROCESSORS] = {};
    for (BaseType_t core = 0; core < portNUM_PROCESSORS; ++core) {
        current_idle_run_time[core] = ulTaskGetIdleRunTimeCounterForCore(core);
    }

    if (!s_haveCpuBaseline) {
        for (BaseType_t core = 0; core < portNUM_PROCESSORS; ++core) {
            s_lastIdleRunTime[core] = current_idle_run_time[core];
        }
        s_lastCpuSampleUs = now_us;
        s_haveCpuBaseline = true;
        if (available != nullptr) {
            *available = false;
        }
        return 0;
    }

    const uint64_t delta_us = static_cast<uint64_t>(std::max<int64_t>(1, now_us - s_lastCpuSampleUs));
    uint64_t idle_delta_total = 0;
    for (BaseType_t core = 0; core < portNUM_PROCESSORS; ++core) {
        idle_delta_total += static_cast<uint64_t>(current_idle_run_time[core] - s_lastIdleRunTime[core]);
        s_lastIdleRunTime[core] = current_idle_run_time[core];
    }
    s_lastCpuSampleUs = now_us;

    const uint64_t total_runtime_budget = delta_us * static_cast<uint64_t>(portNUM_PROCESSORS);
    const int cpu_load = clamp_percent(100 - static_cast<int>((idle_delta_total * 100ULL) / std::max<uint64_t>(1, total_runtime_budget)));
    if (available != nullptr) {
        *available = true;
    }
    return cpu_load;
#else
    if (available != nullptr) {
        *available = false;
    }
    return 0;
#endif
}

static void sample_metrics(bool force_medium_sample, bool force_slow_sample)
{
    hardware_history_service::Snapshot next_snapshot = s_snapshot;
    next_snapshot.available = true;

    bool cpu_load_available = false;
    next_snapshot.cpu_load_percent = sample_cpu_load_percent(&cpu_load_available);
    next_snapshot.cpu_load_available = cpu_load_available;
#ifdef CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
    next_snapshot.cpu_clock_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
#else
    next_snapshot.cpu_clock_mhz = 0;
#endif
    next_snapshot.uptime_sec = static_cast<uint64_t>(esp_timer_get_time() / 1000000ULL);

    const uint64_t free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint64_t total_sram = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    const uint64_t used_sram = (total_sram >= free_sram) ? (total_sram - free_sram) : 0;
    next_snapshot.sram_used_bytes = used_sram;
    next_snapshot.sram_total_bytes = total_sram;
    next_snapshot.sram_percent = calculate_percent(used_sram, total_sram);

    const uint64_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const uint64_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const uint64_t used_psram = (total_psram >= free_psram) ? (total_psram - free_psram) : 0;
    next_snapshot.psram_used_bytes = used_psram;
    next_snapshot.psram_total_bytes = total_psram;
    next_snapshot.psram_percent = calculate_percent(used_psram, total_psram);

    float cpu_temp_celsius = 0.0f;
    next_snapshot.cpu_temperature_available = read_cpu_temperature_celsius(cpu_temp_celsius);
    next_snapshot.cpu_temperature_tenths = next_snapshot.cpu_temperature_available
                                             ? static_cast<int>((cpu_temp_celsius * 10.0f) + ((cpu_temp_celsius >= 0.0f) ? 0.5f : -0.5f))
                                             : 0;

    wifi_ap_record_t ap_info = {};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        next_snapshot.wifi_connected = true;
        next_snapshot.wifi_rssi = ap_info.rssi;
        next_snapshot.wifi_percent = wifi_percent_from_rssi(ap_info.rssi);
        std::snprintf(next_snapshot.wifi_ssid, sizeof(next_snapshot.wifi_ssid), "%s", reinterpret_cast<const char *>(ap_info.ssid));
    } else {
        next_snapshot.wifi_connected = false;
        next_snapshot.wifi_rssi = 0;
        next_snapshot.wifi_percent = 0;
        next_snapshot.wifi_ssid[0] = '\0';
    }

    if ((s_mutex == nullptr) || (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) != pdTRUE)) {
        return;
    }

    s_snapshot = next_snapshot;
    append_sample_locked(s_cpuLoadHistory, static_cast<uint8_t>(next_snapshot.cpu_load_percent));
    append_sample_locked(s_sramHistory, static_cast<uint8_t>(next_snapshot.sram_percent));
    append_sample_locked(s_psramHistory, static_cast<uint8_t>(next_snapshot.psram_percent));
    if (force_medium_sample) {
        append_sample_locked(s_cpuTempHistory,
                             static_cast<uint8_t>(clamp_percent((next_snapshot.cpu_temperature_tenths + 5) / 10)));
    }
    if (force_slow_sample) {
        append_sample_locked(s_wifiHistory, static_cast<uint8_t>(next_snapshot.wifi_percent));
    }

    xSemaphoreGive(s_mutex);
}

static void sampler_task(void *arg)
{
    (void)arg;

    while (true) {
        const int64_t now_sec = esp_timer_get_time() / 1000000LL;
        const bool sample_medium = (s_nextMediumSampleSec == 0) || (now_sec >= s_nextMediumSampleSec);
        const bool sample_slow = (s_nextSlowSampleSec == 0) || (now_sec >= s_nextSlowSampleSec);
        sample_metrics(sample_medium, sample_slow);
        if (sample_medium) {
            s_nextMediumSampleSec = now_sec + kMediumSamplePeriodSec;
        }
        if (sample_slow) {
            s_nextSlowSampleSec = now_sec + kSlowSamplePeriodSec;
        }
        vTaskDelay(kSamplePeriod);
    }
}

static const RingBuffer *metric_to_ring(hardware_history_service::Metric metric)
{
    switch (metric) {
    case hardware_history_service::Metric::CpuLoad:
        return &s_cpuLoadHistory;
    case hardware_history_service::Metric::SramUsage:
        return &s_sramHistory;
    case hardware_history_service::Metric::PsramUsage:
        return &s_psramHistory;
    case hardware_history_service::Metric::CpuTemperature:
        return &s_cpuTempHistory;
    case hardware_history_service::Metric::WifiSignal:
        return &s_wifiHistory;
    }

    return nullptr;
}

} // namespace

namespace hardware_history_service {

bool initialize()
{
    if (s_initialized.load()) {
        return true;
    }

    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == nullptr) {
            ESP_LOGE(TAG, "Failed to create history mutex");
            return false;
        }
    }

    if (s_storage == nullptr) {
        const std::size_t total_bytes = kFastHistorySamples + kFastHistorySamples + kFastHistorySamples +
                        kMediumHistorySamples + kSlowHistorySamples;
        s_storage = static_cast<uint8_t *>(heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (s_storage == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate PSRAM for hardware history (%u bytes)", static_cast<unsigned>(total_bytes));
            return false;
        }

        uint8_t *cursor = s_storage;
        initialize_ring(s_cpuLoadHistory, cursor, kFastHistorySamples);
        cursor += kFastHistorySamples;
        initialize_ring(s_sramHistory, cursor, kFastHistorySamples);
        cursor += kFastHistorySamples;
        initialize_ring(s_psramHistory, cursor, kFastHistorySamples);
        cursor += kFastHistorySamples;
        initialize_ring(s_cpuTempHistory, cursor, kMediumHistorySamples);
        cursor += kMediumHistorySamples;
        initialize_ring(s_wifiHistory, cursor, kSlowHistorySamples);
    }

    sample_metrics(true, true);

    if (create_background_task_prefer_psram(sampler_task,
                                            "hw_history",
                                            kSamplerTaskStack,
                                            nullptr,
                                            1,
                                            1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start hardware history task");
        return false;
    }

    s_initialized.store(true);
    return true;
}

bool get_snapshot(Snapshot &snapshot)
{
    if (!s_initialized.load() && !initialize()) {
        snapshot = {};
        return false;
    }

    if ((s_mutex == nullptr) || (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) != pdTRUE)) {
        return false;
    }

    snapshot = s_snapshot;
    xSemaphoreGive(s_mutex);
    return snapshot.available;
}

std::size_t copy_samples(Metric metric, uint8_t *destination, std::size_t max_samples)
{
    if ((destination == nullptr) || (max_samples == 0)) {
        return 0;
    }

    if (!s_initialized.load() && !initialize()) {
        return 0;
    }

    const RingBuffer *ring = metric_to_ring(metric);
    if (ring == nullptr) {
        return 0;
    }

    if ((s_mutex == nullptr) || (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) != pdTRUE)) {
        return 0;
    }

    const std::size_t copied = copy_ring_samples_locked(*ring, destination, max_samples);
    xSemaphoreGive(s_mutex);
    return copied;
}

} // namespace hardware_history_service