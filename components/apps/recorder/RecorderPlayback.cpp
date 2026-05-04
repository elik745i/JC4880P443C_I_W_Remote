#include "RecorderInternal.hpp"

using namespace recorder;

bool RecorderApp::preparePlaybackPath()
{
    if (!ensureRecordDirectoryAvailable(false)) {
        setStatusMessage("SD card is not ready for playback");
        return false;
    }

    if (!ensure_playback_audio_ready()) {
        setStatusMessage("Audio output is not available right now");
        return false;
    }

    return true;
}

void RecorderApp::playRecording(size_t index)
{
    if (index >= _recordings.size()) {
        return;
    }

    playRecordingEntry(_recordings[index]);
}

bool RecorderApp::playRecordingEntry(const RecordingEntry &entry)
{
    if (isRecordingOrStopping()) {
        stopRecording();
        setStatusMessage("Stop the recording first, then play files");
        return false;
    }

    if (!preparePlaybackPath()) {
        return false;
    }

    FILE *file = std::fopen(entry.path.c_str(), "rb");
    if (file == nullptr) {
        setStatusMessage("Could not open recording for playback");
        return false;
    }

    const esp_err_t err = audio_player_play_file(file, entry.path.c_str());
    if (err != ESP_OK) {
        std::fclose(file);
        setStatusMessage(std::string("Playback failed: ") + esp_err_to_name(err));
        ESP_LOGW(kTag, "Playback failed for %s: %s", entry.path.c_str(), esp_err_to_name(err));
        return false;
    }

    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        const uint64_t totalBits = static_cast<uint64_t>(entry.sizeBytes) * 8ULL;
        _playbackActive = true;
        _playbackStopRequested = false;
        _playingIndex = static_cast<size_t>(&entry - _recordings.data());
        _playbackStartedAt = xTaskGetTickCount();
        _playbackElapsedSeconds = 0;
        _playbackDurationSeconds = (entry.sizeBytes > 0)
                                       ? static_cast<uint32_t>((totalBits + static_cast<uint64_t>(kAacBitrate - 1)) /
                                                               static_cast<uint64_t>(kAacBitrate))
                                       : 0;
        xSemaphoreGive(_stateMutex);
    }

    ESP_LOGI(kTag, "Playback started for %s", entry.path.c_str());
    setStatusMessage(std::string("Playing ") + entry.name);
    return true;
}

void RecorderApp::stopPlayback()
{
    const esp_err_t err = audio_player_stop();
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        ESP_LOGW(kTag, "Failed to stop playback: %s", esp_err_to_name(err));
    }

    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        _playbackActive = false;
        _playbackStopRequested = true;
        _playingIndex = std::numeric_limits<size_t>::max();
        _playbackStartedAt = 0;
        _playbackElapsedSeconds = 0;
        _playbackDurationSeconds = 0;
        _statusMessage = "Playback stopped";
        xSemaphoreGive(_stateMutex);
    }
}

bool RecorderApp::requestPlaybackToggle(size_t index)
{
    bool isActive = false;
    size_t activeIndex = std::numeric_limits<size_t>::max();

    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        isActive = _playbackActive;
        activeIndex = _playingIndex;
        xSemaphoreGive(_stateMutex);
    }

    if (isActive && (activeIndex == index)) {
        stopPlayback();
        return true;
    }

    return (index < _recordings.size()) ? playRecordingEntry(_recordings[index]) : false;
}

void RecorderApp::syncPlaybackState()
{
    const audio_player_state_t state = audio_player_get_state();
    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }

    if (_playbackActive) {
        if ((state == AUDIO_PLAYER_STATE_PLAYING) || (state == AUDIO_PLAYER_STATE_PAUSE)) {
            const TickType_t elapsedTicks = xTaskGetTickCount() - _playbackStartedAt;
            const uint32_t elapsedSeconds = static_cast<uint32_t>(pdTICKS_TO_MS(elapsedTicks) / 1000U);
            _playbackElapsedSeconds = (_playbackDurationSeconds > 0)
                                          ? std::min(elapsedSeconds, _playbackDurationSeconds)
                                          : elapsedSeconds;
        } else {
            const bool stoppedByUser = _playbackStopRequested;
            _playbackActive = false;
            _playbackStopRequested = false;
            _playingIndex = std::numeric_limits<size_t>::max();
            _playbackStartedAt = 0;
            _playbackElapsedSeconds = 0;
            _playbackDurationSeconds = 0;
            _statusMessage = stoppedByUser ? "Playback stopped" : "Playback finished";
        }
    }

    xSemaphoreGive(_stateMutex);
}

std::vector<RecorderApp::RecordingEntry> RecorderApp::scanRecordingEntries() const
{
    std::vector<RecordingEntry> entries;

    DIR *directory = opendir(kRecordDirectory);
    if (directory == nullptr) {
        return entries;
    }

    dirent *entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        if ((entry->d_name[0] == '.') || !has_aac_extension(entry->d_name)) {
            continue;
        }

        RecordingEntry item;
        item.name = entry->d_name;
        item.path = std::string(kRecordDirectory) + "/" + entry->d_name;
        struct stat info = {};
        item.sizeBytes = (stat(item.path.c_str(), &info) == 0) ? static_cast<size_t>(info.st_size) : 0U;
        entries.push_back(std::move(item));
    }
    closedir(directory);

    std::sort(entries.begin(), entries.end(), [](const RecordingEntry &left, const RecordingEntry &right) {
        return left.name > right.name;
    });
    return entries;
}