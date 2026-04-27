#pragma once

#include <cstddef>
#include <cstdint>

namespace battery_history_service {

static constexpr std::size_t kMaxHistorySamples = 60;

struct HistorySample {
    int64_t timestamp_sec;
    int16_t capacity_tenths;
    uint8_t flags;
};

struct Status {
    bool available;
    bool charging;
    int capacity_percent;
    int capacity_tenths;
    int32_t eta_minutes;
    bool eta_to_full;
    std::size_t sample_count;
    int64_t timestamp_sec;
};

bool initialize();
bool set_adc_attached(bool attached);
bool get_status(Status &status);
std::size_t copy_samples(HistorySample *destination, std::size_t max_samples);

} // namespace battery_history_service