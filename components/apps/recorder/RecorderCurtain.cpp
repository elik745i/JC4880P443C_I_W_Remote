#include "RecorderInternal.hpp"

using namespace recorder;

std::vector<ESP_Brookesia_PhoneQuickAccessActionData_t> RecorderApp::getQuickAccessActions() const
{
    if (!checkInitialized()) {
        return {};
    }

    bool recording = false;
    bool stopping = false;
    if ((_stateMutex != nullptr) && (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE)) {
        recording = _recordingActive;
        stopping = _stopRequested || (_recordTaskHandle != nullptr && !_recordingActive);
        xSemaphoreGive(_stateMutex);
    }

    return {
        {QUICK_ACCESS_ACTION_RECORD, "REC", !recording && !stopping},
        {QUICK_ACCESS_ACTION_STOP, LV_SYMBOL_STOP, recording || stopping},
    };
}

ESP_Brookesia_PhoneQuickAccessDetailData_t RecorderApp::getQuickAccessDetail() const
{
    if (!checkInitialized()) {
        return {};
    }

    bool recording = false;
    bool stopping = false;
    uint32_t seconds = 0;
    std::string status;

    if ((_stateMutex != nullptr) && (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE)) {
        recording = _recordingActive;
        stopping = _stopRequested || (_recordTaskHandle != nullptr && !_recordingActive);
        seconds = _recordedSeconds;
        status = _statusMessage;
        xSemaphoreGive(_stateMutex);
    }

    if (recording || stopping) {
        const uint32_t clamped = std::min(seconds, kQuickAccessRecordingMaxSeconds);
        const int progress_percent = static_cast<int>((clamped * 100U) / kQuickAccessRecordingMaxSeconds);
        return {
            .text = std::string(recording ? "Recording " : "Stopping ") + format_duration(seconds),
            .scroll_text = false,
            .progress_percent = progress_percent,
        };
    }

    const std::string detailText = status.empty() ? "Ready to record" : status;
    return {
        .text = detailText,
        .scroll_text = detailText.size() > 22,
        .progress_percent = -1,
    };
}

bool RecorderApp::handleQuickAccessAction(int action_id)
{
    switch (action_id) {
    case QUICK_ACCESS_ACTION_RECORD:
        return startRecording();
    case QUICK_ACCESS_ACTION_STOP:
        if (isRecordingOrStopping()) {
            stopRecording();
            return true;
        }
        return false;
    default:
        return false;
    }
}