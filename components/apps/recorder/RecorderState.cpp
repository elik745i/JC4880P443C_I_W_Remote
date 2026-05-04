#include "RecorderInternal.hpp"

using namespace recorder;

void RecorderApp::setStatusMessage(const std::string &message)
{
    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        _statusMessage = message;
        xSemaphoreGive(_stateMutex);
    }
}

void RecorderApp::waitForRecordTaskStop()
{
    while (true) {
        TaskHandle_t taskHandle = nullptr;
        if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            taskHandle = _recordTaskHandle;
            xSemaphoreGive(_stateMutex);
        }

        if (taskHandle == nullptr) {
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void RecorderApp::releaseRuntimeResources()
{
    _playButtonContexts.clear();
    _playButtonContexts.shrink_to_fit();
    _recordings.clear();
    _recordings.shrink_to_fit();
    _activeRecordingPath.clear();

    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        _refreshListPending = false;
        _recordedSeconds = 0;
        _spectrumValues.fill(0);
        _recordButtonCooldownUntil = 0;
        _playbackActive = false;
        _playbackStopRequested = false;
        _playingIndex = std::numeric_limits<size_t>::max();
        _playbackStartedAt = 0;
        _playbackElapsedSeconds = 0;
        _playbackDurationSeconds = 0;
        xSemaphoreGive(_stateMutex);
    }
}

void RecorderApp::copyUiState(std::array<int16_t, kSpectrumBins> &spectrum,
                              uint32_t &seconds,
                              bool &recording,
                              bool &refreshList,
                              bool &playbackActive,
                              size_t &playingIndex,
                              uint32_t &playbackSeconds,
                              uint32_t &playbackDurationSeconds,
                              std::string &status)
{
    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        spectrum = _spectrumValues;
        seconds = _recordedSeconds;
        recording = _recordingActive;
        refreshList = _refreshListPending;
        playbackActive = _playbackActive;
        playingIndex = _playingIndex;
        playbackSeconds = _playbackElapsedSeconds;
        playbackDurationSeconds = _playbackDurationSeconds;
        status = _statusMessage;
        xSemaphoreGive(_stateMutex);
        return;
    }

    spectrum.fill(0);
    seconds = 0;
    recording = false;
    refreshList = false;
    playbackActive = false;
    playingIndex = std::numeric_limits<size_t>::max();
    playbackSeconds = 0;
    playbackDurationSeconds = 0;
    status = "Recorder state is busy";
}

bool RecorderApp::debugStartRecording()
{
    ESP_LOGI(kTag, "Serial debug start requested. %s", debugDescribeState().c_str());
    const bool started = startRecording();
    if (!started) {
        ESP_LOGW(kTag, "Serial debug start rejected. %s", debugDescribeState().c_str());
    }
    return started;
}

bool RecorderApp::debugStopRecording()
{
    const bool busy = isRecordingOrStopping();
    ESP_LOGI(kTag, "Serial debug stop requested. %s", debugDescribeState().c_str());
    if (busy) {
        stopRecording();
    }
    return busy;
}

bool RecorderApp::debugPlayLatest()
{
    const std::vector<RecordingEntry> entries = scanRecordingEntries();
    if (entries.empty()) {
        setStatusMessage("No recordings available for playback");
        ESP_LOGW(kTag, "Serial debug play latest rejected: no recordings");
        return false;
    }

    ESP_LOGI(kTag, "Serial debug play latest requested: %s", entries.front().name.c_str());
    return playRecordingEntry(entries.front());
}

bool RecorderApp::debugPlayIndex(size_t index)
{
    const std::vector<RecordingEntry> entries = scanRecordingEntries();
    if (index >= entries.size()) {
        setStatusMessage("Recording index is out of range");
        ESP_LOGW(kTag, "Serial debug play rejected index=%u count=%u",
                 static_cast<unsigned>(index),
                 static_cast<unsigned>(entries.size()));
        return false;
    }

    ESP_LOGI(kTag, "Serial debug play requested index=%u name=%s",
             static_cast<unsigned>(index),
             entries[index].name.c_str());
    return playRecordingEntry(entries[index]);
}

std::string RecorderApp::debugDescribeState() const
{
    bool recording = false;
    bool stopRequested = false;
    bool refreshPending = false;
    uint32_t recordedSeconds = 0;
    std::string status = "<unavailable>";
    std::string activePath;
    bool taskRunning = false;

    if ((_stateMutex != nullptr) && (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE)) {
        recording = _recordingActive;
        stopRequested = _stopRequested;
        refreshPending = _refreshListPending;
        recordedSeconds = _recordedSeconds;
        status = _statusMessage;
        activePath = _activeRecordingPath;
        taskRunning = (_recordTaskHandle != nullptr);
        xSemaphoreGive(_stateMutex);
    }

    const std::vector<RecordingEntry> entries = app_storage_is_sdcard_mounted() ? scanRecordingEntries() : std::vector<RecordingEntry>();

    std::ostringstream stream;
    stream << "recording=" << (recording ? "yes" : "no")
           << " stop_requested=" << (stopRequested ? "yes" : "no")
           << " task=" << (taskRunning ? "set" : "null")
           << " seconds=" << recordedSeconds
           << " refresh_list=" << (refreshPending ? "yes" : "no")
           << " sdcard=" << (app_storage_is_sdcard_mounted() ? "mounted" : "missing")
           << " files=" << entries.size()
           << " status=\"" << status << "\"";

    if (!activePath.empty()) {
        stream << " active_path=\"" << activePath << "\"";
    }
    if (!entries.empty()) {
        stream << " latest=\"" << entries.front().name << "\"";
    }

    return stream.str();
}

std::vector<std::string> RecorderApp::debugListRecordingSummaries() const
{
    std::vector<std::string> lines;
    const std::vector<RecordingEntry> entries = scanRecordingEntries();
    lines.reserve(entries.size());
    for (size_t index = 0; index < entries.size(); ++index) {
        std::ostringstream stream;
        stream << index << " " << entries[index].name << " (" << format_size(entries[index].sizeBytes) << ")";
        lines.push_back(stream.str());
    }
    return lines;
}