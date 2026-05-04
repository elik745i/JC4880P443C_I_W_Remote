#pragma once

#include <array>
#include <limits>
#include <new>
#include <string>
#include <vector>
#include "esp_heap_caps.h"
#include "esp_brookesia.hpp"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
}

class RecorderApp: public ESP_Brookesia_PhoneApp
{
public:
    RecorderApp();
    ~RecorderApp() override;

    bool init() override;
    bool run() override;
    bool pause() override;
    bool resume() override;
    bool back() override;
    bool close() override;
    std::vector<ESP_Brookesia_PhoneQuickAccessActionData_t> getQuickAccessActions() const override;
    ESP_Brookesia_PhoneQuickAccessDetailData_t getQuickAccessDetail() const override;
    bool handleQuickAccessAction(int action_id) override;

    bool debugStartRecording();
    bool debugStopRecording();
    bool debugPlayLatest();
    bool debugPlayIndex(size_t index);
    std::string debugDescribeState() const;
    std::vector<std::string> debugListRecordingSummaries() const;

private:
    template <typename T>
    class PsramAllocator {
    public:
        using value_type = T;

        PsramAllocator() noexcept = default;

        template <typename U>
        PsramAllocator(const PsramAllocator<U> &) noexcept
        {
        }

        [[nodiscard]] T *allocate(std::size_t count)
        {
            if (count > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
                std::abort();
            }

            void *storage = heap_caps_malloc(count * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (storage == nullptr) {
                storage = heap_caps_malloc(count * sizeof(T), MALLOC_CAP_8BIT);
            }
            if (storage == nullptr) {
                std::abort();
            }

            return static_cast<T *>(storage);
        }

        void deallocate(T *ptr, std::size_t) noexcept
        {
            heap_caps_free(ptr);
        }

        template <typename U>
        bool operator==(const PsramAllocator<U> &) const noexcept
        {
            return true;
        }

        template <typename U>
        bool operator!=(const PsramAllocator<U> &) const noexcept
        {
            return false;
        }
    };

    struct RecordingEntry {
        std::string name;
        std::string path;
        size_t sizeBytes;
    };

    struct PlayButtonContext {
        RecorderApp *app;
        size_t index;
        lv_obj_t *button;
        lv_obj_t *label;
        lv_obj_t *metaLabel;
    };

    static constexpr int kSpectrumBins = 24;

    static void onRecordButtonEvent(lv_event_t *event);
    static void onPlayButtonEvent(lv_event_t *event);
    static void onUiTimer(lv_timer_t *timer);
    static void recordTaskEntry(void *context);
    static void playbackTaskEntry(void *context);
    static void pulseAnimSizeCallback(void *object, int32_t value);
    static void pulseAnimOpacityCallback(void *object, int32_t value);

    void buildUi();
    void refreshRecordingList();
    void refreshRecordButtonState();
    void tickUi();
    void updateSpectrumFromPcm(const int16_t *samples, size_t sampleCount);
    bool ensureRecordDirectoryAvailable(bool allowMount);
    bool startRecording();
    void stopRecording();
    void recordTask();
    bool isRecordingOrStopping();
    bool preparePlaybackPath();
    void playRecording(size_t index);
    bool playRecordingEntry(const RecordingEntry &entry);
    void stopPlayback();
    void playbackTask();
    bool requestPlaybackToggle(size_t index);
    void syncPlaybackState();
    std::string createRecordingPath() const;
    void setStatusMessage(const std::string &message);
    void waitForRecordTaskStop();
    void waitForPlaybackTaskStop();
    void releaseRuntimeResources();
    void copyUiState(std::array<int16_t, kSpectrumBins> &spectrum, uint32_t &seconds, bool &recording,
                     bool &refreshList, bool &playbackActive, size_t &playingIndex, uint32_t &playbackSeconds,
                     uint32_t &playbackDurationSeconds, std::string &status);
    std::vector<RecordingEntry> scanRecordingEntries() const;
    bool requestRecordButtonToggle();

    lv_obj_t *_titleLabel;
    lv_obj_t *_statusLabel;
    lv_obj_t *_recordButton;
    lv_obj_t *_recordPulseRing;
    lv_obj_t *_recordTimeLabel;
    lv_obj_t *_spectrumChart;
    lv_chart_series_t *_spectrumSeries;
    std::array<lv_obj_t *, kSpectrumBins> _spectrumBars;
    std::array<lv_obj_t *, kSpectrumBins> _spectrumCaps;
    lv_obj_t *_recordingsList;
    lv_obj_t *_emptyLabel;
    lv_timer_t *_uiTimer;
    lv_anim_t _pulseSizeAnim;
    lv_anim_t _pulseOpacityAnim;

    SemaphoreHandle_t _stateMutex;
    TaskHandle_t _recordTaskHandle;
    TaskHandle_t _playbackTaskHandle;
    bool _recordingActive;
    bool _stopRequested;
    TickType_t _recordButtonCooldownUntil;
    bool _refreshListPending;
    uint32_t _recordedSeconds;
    std::array<int16_t, kSpectrumBins> _spectrumValues;
    std::array<int16_t, kSpectrumBins> _spectrumDisplayValues;
    std::array<int16_t, kSpectrumBins> _spectrumPeakValues;
    bool _playbackActive;
    bool _playbackStopRequested;
    size_t _playingIndex;
    TickType_t _playbackStartedAt;
    uint32_t _playbackElapsedSeconds;
    uint32_t _playbackDurationSeconds;
    std::string _statusMessage;
    std::string _activeRecordingPath;
    std::string _activePlaybackPath;
    std::vector<RecordingEntry, PsramAllocator<RecordingEntry>> _recordings;
    std::vector<PlayButtonContext, PsramAllocator<PlayButtonContext>> _playButtonContexts;
};