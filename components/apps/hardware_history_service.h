#pragma once

#include <cstddef>
#include <cstdint>

namespace hardware_history_service {

enum class Metric : uint8_t {
    CpuLoad = 0,
    SramUsage,
    PsramUsage,
    CpuTemperature,
    WifiSignal,
};

static constexpr std::size_t kFastHistorySamples = 3600;
static constexpr std::size_t kMediumHistorySamples = 360;
static constexpr std::size_t kSlowHistorySamples = 60;

struct Snapshot {
    bool available;
    bool cpu_load_available;
    int cpu_load_percent;
    int cpu_clock_mhz;
    uint64_t uptime_sec;
    int sram_percent;
    uint64_t sram_used_bytes;
    uint64_t sram_total_bytes;
    int psram_percent;
    uint64_t psram_used_bytes;
    uint64_t psram_total_bytes;
    bool cpu_temperature_available;
    int cpu_temperature_tenths;
    bool wifi_connected;
    int wifi_rssi;
    int wifi_percent;
    char wifi_ssid[33];
};

bool initialize();
bool get_snapshot(Snapshot &snapshot);
std::size_t copy_samples(Metric metric, uint8_t *destination, std::size_t max_samples);

} // namespace hardware_history_service