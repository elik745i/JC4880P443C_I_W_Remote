#pragma once

#include "Recorder.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <iomanip>
#include <memory>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "audio_player.h"
#include "bsp_board_extra.h"
#include "driver/i2s_std.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_aac_enc.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "impl/esp_aac_dec.h"
}

#include "storage_access.h"

LV_IMG_DECLARE(record_png);

namespace recorder {

inline constexpr char kTag[] = "RecorderApp";
inline constexpr char kRecordDirectory[] = "/sdcard/record";
inline constexpr uint32_t kSampleRate = 44100;
inline constexpr uint32_t kBitsPerSample = 16;
inline constexpr int kInputGain = 24;
inline constexpr int kAacBitrate = 64000;
inline constexpr uint32_t kUiTickMs = 100;
inline constexpr uint32_t kSpectrumUpdateIntervalMs = 120;
inline constexpr TickType_t kRecordButtonCooldown = pdMS_TO_TICKS(250);
inline constexpr uint32_t kRecordTaskStack = 12288;
inline constexpr UBaseType_t kRecordTaskPriority = 5;
inline constexpr uint32_t kPlaybackTaskStack = 16384;
inline constexpr UBaseType_t kPlaybackTaskPriority = 5;
inline constexpr size_t kPlaybackReadBufferSize = 1024;
inline constexpr size_t kPlaybackOutputBufferSize = 4096;
inline constexpr lv_coord_t kHeaderHeight = 126;
inline constexpr lv_coord_t kRecordButtonSize = 92;
inline constexpr lv_coord_t kRecordRingBaseSize = 106;
inline constexpr lv_coord_t kRecordButtonOffsetX = -14;
inline constexpr lv_coord_t kRecordButtonOffsetY = 12;
inline constexpr lv_coord_t kHeaderTopOffset = 4;
inline constexpr lv_coord_t kSectionGap = 10;
inline constexpr lv_coord_t kChartCardHeight = 206;
inline constexpr lv_coord_t kBottomMargin = 14;
inline constexpr lv_coord_t kMinListCardHeight = 120;
inline constexpr uint32_t kQuickAccessRecordingMaxSeconds = 3600;
inline constexpr TickType_t kPlaybackStopTimeout = pdMS_TO_TICKS(500);
inline constexpr TickType_t kPlaybackCodecResetDelay = pdMS_TO_TICKS(50);
inline constexpr TickType_t kPlaybackCodecResumeDelay = pdMS_TO_TICKS(100);

enum QuickAccessAction {
    QUICK_ACCESS_ACTION_RECORD = 0x52454331,
    QUICK_ACCESS_ACTION_STOP = 0x53544F50,
};

struct HeapCapsDeleter {
    void operator()(uint8_t *ptr) const
    {
        if (ptr != nullptr) {
            heap_caps_free(ptr);
        }
    }
};

using HeapCapsBuffer = std::unique_ptr<uint8_t, HeapCapsDeleter>;

inline int16_t clip_pcm_sample(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return static_cast<int16_t>(value);
}

inline void apply_shared_audio_gain(int16_t *samples, size_t sampleCount, int gainLevel)
{
    if ((samples == nullptr) || (sampleCount == 0)) {
        return;
    }

    const int level = std::max(1, gainLevel);
    if (level <= 1) {
        return;
    }

    for (size_t index = 0; index < sampleCount; ++index) {
        samples[index] = clip_pcm_sample(static_cast<int32_t>(samples[index]) * level);
    }
}

inline void apply_shared_mic_gain(int16_t *samples, size_t sampleCount)
{
    apply_shared_audio_gain(samples, sampleCount, bsp_extra_audio_mic_gain_get_level());
}

inline bool has_aac_extension(const char *name)
{
    if (name == nullptr) {
        return false;
    }

    const char *dot = strrchr(name, '.');
    if (dot == nullptr) {
        return false;
    }

    return strcasecmp(dot, ".aac") == 0;
}

inline bool ensure_directory(const char *path)
{
    struct stat info = {};
    if (stat(path, &info) == 0) {
        return S_ISDIR(info.st_mode);
    }

    if (mkdir(path, 0755) == 0) {
        return true;
    }

    return errno == EEXIST;
}

inline std::string format_duration(uint32_t totalSeconds)
{
    char buffer[16];
    const uint32_t minutes = totalSeconds / 60U;
    const uint32_t seconds = totalSeconds % 60U;
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%02lu:%02lu",
                  static_cast<unsigned long>(minutes),
                  static_cast<unsigned long>(seconds));
    return buffer;
}

inline std::string format_size(size_t sizeBytes)
{
    std::ostringstream stream;
    if (sizeBytes >= (1024U * 1024U)) {
        stream.setf(std::ios::fixed);
        stream.precision(1);
        stream << (static_cast<double>(sizeBytes) / (1024.0 * 1024.0)) << " MB";
    } else {
        stream << ((sizeBytes + 1023U) / 1024U) << " KB";
    }
    return stream.str();
}

inline bool ensure_playback_audio_ready()
{
    TickType_t start = xTaskGetTickCount();
    while (audio_player_get_state() != AUDIO_PLAYER_STATE_IDLE) {
        const esp_err_t stopResult = audio_player_stop();
        if ((stopResult != ESP_OK) && (stopResult != ESP_ERR_INVALID_STATE)) {
            ESP_LOGW(kTag, "Failed to stop audio player before playback restart: %s", esp_err_to_name(stopResult));
            return false;
        }

        if ((xTaskGetTickCount() - start) >= kPlaybackStopTimeout) {
            ESP_LOGW(kTag, "Audio player failed to reach idle before playback restart");
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    esp_err_t ret = bsp_extra_codec_dev_stop();
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE)) {
        ESP_LOGW(kTag, "Failed to stop codec path before playback: %s", esp_err_to_name(ret));
        return false;
    }

    vTaskDelay(kPlaybackCodecResetDelay);

    ret = bsp_extra_codec_init();
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "Failed to init codec for playback: %s", esp_err_to_name(ret));
        return false;
    }

    ret = bsp_extra_player_init();
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "Failed to init player: %s", esp_err_to_name(ret));
        return false;
    }

    ret = bsp_extra_codec_dev_resume();
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "Failed to resume codec path: %s", esp_err_to_name(ret));
        return false;
    }

    vTaskDelay(kPlaybackCodecResumeDelay);

    const int currentVolume = bsp_extra_codec_volume_get();
    const int restoreVolume = currentVolume >= 0 ? currentVolume : 60;
    ret = bsp_extra_codec_volume_set(restoreVolume, nullptr);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "Failed to set playback volume: %s", esp_err_to_name(ret));
        return false;
    }

    ret = bsp_extra_codec_mute_set(false);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "Failed to unmute playback: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

inline HeapCapsBuffer allocate_audio_buffer(size_t size)
{
    uint8_t *buffer = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer != nullptr) {
        return HeapCapsBuffer(buffer);
    }

    return HeapCapsBuffer(static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_8BIT)));
}

inline BaseType_t create_task_prefer_psram(TaskFunction_t task,
                                           const char *name,
                                           uint32_t stackDepth,
                                           void *arg,
                                           UBaseType_t priority,
                                           TaskHandle_t *taskHandle,
                                           BaseType_t coreId)
{
    if (xTaskCreatePinnedToCoreWithCaps(task,
                                        name,
                                        stackDepth,
                                        arg,
                                        priority,
                                        taskHandle,
                                        coreId,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        return pdPASS;
    }

    ESP_LOGW(kTag,
             "Falling back to internal RAM stack for %s. Internal free=%u PSRAM free=%u",
             name,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    return xTaskCreatePinnedToCore(task, name, stackDepth, arg, priority, taskHandle, coreId);
}

} // namespace recorder