#include "RecorderInternal.hpp"

using namespace recorder;

namespace {

bool write_pcm_fully(uint8_t *buffer, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        size_t bytesWritten = 0;
        const esp_err_t err = bsp_extra_i2s_write(buffer + offset, length - offset, &bytesWritten, 200);
        if (err == ESP_ERR_TIMEOUT) {
            continue;
        }
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "PCM write failed: %s", esp_err_to_name(err));
            return false;
        }
        if (bytesWritten == 0) {
            continue;
        }
        offset += bytesWritten;
    }

    return true;
}

} // namespace

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

    bool playbackRunning = false;
    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        playbackRunning = _playbackActive || (_playbackTaskHandle != nullptr);
        xSemaphoreGive(_stateMutex);
    }

    if (playbackRunning) {
        stopPlayback();
        waitForPlaybackTaskStop();
    }

    if (!preparePlaybackPath()) {
        return false;
    }

    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        const uint64_t totalBits = static_cast<uint64_t>(entry.sizeBytes) * 8ULL;
        _playbackActive = true;
        _playbackStopRequested = false;
        _activePlaybackPath = entry.path;
        _playingIndex = static_cast<size_t>(&entry - _recordings.data());
        _playbackStartedAt = xTaskGetTickCount();
        _playbackElapsedSeconds = 0;
        _playbackDurationSeconds = (entry.sizeBytes > 0)
                                       ? static_cast<uint32_t>((totalBits + static_cast<uint64_t>(kAacBitrate - 1)) /
                                                               static_cast<uint64_t>(kAacBitrate))
                                       : 0;
        xSemaphoreGive(_stateMutex);
    }

    const BaseType_t created = create_task_prefer_psram(
        playbackTaskEntry,
        "recorder_playback",
        kPlaybackTaskStack,
        this,
        kPlaybackTaskPriority,
        &_playbackTaskHandle,
        1
    );
    if (created != pdPASS) {
        if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            _playbackActive = false;
            _playbackStopRequested = false;
            _playbackTaskHandle = nullptr;
            _playingIndex = std::numeric_limits<size_t>::max();
            _playbackStartedAt = 0;
            _playbackElapsedSeconds = 0;
            _playbackDurationSeconds = 0;
            _activePlaybackPath.clear();
            _statusMessage = "Failed to start playback";
            xSemaphoreGive(_stateMutex);
        }
        return false;
    }

    ESP_LOGI(kTag, "Playback started for %s", entry.path.c_str());
    setStatusMessage(std::string("Playing ") + entry.name);
    return true;
}

void RecorderApp::stopPlayback()
{
    audio_player_stop();

    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (_playbackActive || (_playbackTaskHandle != nullptr)) {
            _playbackStopRequested = true;
            _statusMessage = "Stopping playback...";
        }
        xSemaphoreGive(_stateMutex);
    }
}

void RecorderApp::playbackTask()
{
    FILE *file = nullptr;
    void *decoder = nullptr;
    std::string path;
    std::string status;
    uint64_t totalDecodedBytes = 0;
    uint32_t sampleRate = 0;
    uint8_t channels = 0;
    uint8_t bitsPerSample = 0;
    TickType_t nextSpectrumTick = 0;

    do {
        if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            path = _activePlaybackPath;
            xSemaphoreGive(_stateMutex);
        }
        if (path.empty()) {
            status = "Playback path is missing";
            break;
        }

        file = std::fopen(path.c_str(), "rb");
        if (file == nullptr) {
            status = "Could not open recording for playback";
            break;
        }

        if ((esp_audio_dec_register_default() != ESP_AUDIO_ERR_OK) ||
            (esp_audio_simple_dec_register_default() != ESP_AUDIO_ERR_OK)) {
            status = "Playback decoder init failed";
            break;
        }

        esp_aac_dec_cfg_t aacCfg = ESP_AAC_DEC_CONFIG_DEFAULT();
        aacCfg.aac_plus_enable = true;
        esp_audio_simple_dec_cfg_t decoderCfg = {
            .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_AAC,
            .dec_cfg = &aacCfg,
            .cfg_size = sizeof(aacCfg),
            .use_frame_dec = false,
        };
        if (esp_audio_simple_dec_open(&decoderCfg, &decoder) != ESP_AUDIO_ERR_OK) {
            status = "Failed to open AAC decoder";
            break;
        }

        HeapCapsBuffer readBuffer = allocate_audio_buffer(kPlaybackReadBufferSize);
        HeapCapsBuffer outputBuffer = allocate_audio_buffer(kPlaybackOutputBufferSize);
        if ((readBuffer == nullptr) || (outputBuffer == nullptr)) {
            status = "Not enough memory for playback buffers";
            break;
        }

        size_t bufferedOffset = 0;
        size_t bufferedLength = 0;
        bool eofReached = false;
        while (true) {
            bool stopRequested = false;
            if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                stopRequested = _playbackStopRequested;
                xSemaphoreGive(_stateMutex);
            }
            if (stopRequested) {
                break;
            }

            if ((bufferedLength == 0) && !eofReached) {
                const size_t bytesRead = std::fread(readBuffer.get(), 1, kPlaybackReadBufferSize, file);
                if ((bytesRead == 0) && std::ferror(file)) {
                    status = "Failed to read AAC data";
                    break;
                }
                bufferedOffset = 0;
                bufferedLength = bytesRead;
                eofReached = std::feof(file) || (bytesRead < kPlaybackReadBufferSize);
                if ((bytesRead == 0) && eofReached) {
                    break;
                }
            }

            esp_audio_simple_dec_raw_t raw = {
                .buffer = readBuffer.get() + bufferedOffset,
                .len = static_cast<uint32_t>(bufferedLength),
                .eos = eofReached,
                .consumed = 0,
                .frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE,
            };
            esp_audio_simple_dec_out_t out = {
                .buffer = outputBuffer.get(),
                .len = static_cast<uint32_t>(kPlaybackOutputBufferSize),
                .needed_size = 0,
                .decoded_size = 0,
            };

            const esp_audio_err_t decodeErr = esp_audio_simple_dec_process(decoder, &raw, &out);
            if (decodeErr == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                outputBuffer = allocate_audio_buffer(out.needed_size);
                if (outputBuffer == nullptr) {
                    status = "Playback output buffer allocation failed";
                    break;
                }
                continue;
            }
            if (decodeErr != ESP_AUDIO_ERR_OK) {
                status = "AAC playback decode failed";
                break;
            }
            if (raw.consumed > bufferedLength) {
                status = "AAC playback consumed invalid data";
                break;
            }

            bufferedOffset += raw.consumed;
            bufferedLength -= raw.consumed;
            if (bufferedLength == 0) {
                bufferedOffset = 0;
            }

            if (out.decoded_size == 0) {
                if ((bufferedLength == 0) && eofReached) {
                    break;
                }
                continue;
            }

            esp_audio_simple_dec_info_t info = {};
            if (esp_audio_simple_dec_get_info(decoder, &info) != ESP_AUDIO_ERR_OK) {
                status = "AAC playback info unavailable";
                break;
            }
            if (info.bits_per_sample != 16) {
                status = "Unsupported playback sample format";
                break;
            }

            if ((sampleRate != info.sample_rate) || (channels != info.channel) || (bitsPerSample != info.bits_per_sample)) {
                const esp_err_t fmtErr = bsp_extra_codec_set_fs_play(info.sample_rate,
                                                                     info.bits_per_sample,
                                                                     (info.channel == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO);
                if (fmtErr != ESP_OK) {
                    status = std::string("Playback format failed: ") + esp_err_to_name(fmtErr);
                    break;
                }
                sampleRate = info.sample_rate;
                channels = info.channel;
                bitsPerSample = info.bits_per_sample;
            }

            apply_shared_mic_gain(reinterpret_cast<int16_t *>(outputBuffer.get()), out.decoded_size / sizeof(int16_t));

            const TickType_t now = xTaskGetTickCount();
            if ((nextSpectrumTick == 0) || (now >= nextSpectrumTick)) {
                updateSpectrumFromPcm(reinterpret_cast<const int16_t *>(outputBuffer.get()),
                                     out.decoded_size / sizeof(int16_t));
                nextSpectrumTick = now + pdMS_TO_TICKS(kSpectrumUpdateIntervalMs);
            }

            if (!write_pcm_fully(outputBuffer.get(), out.decoded_size)) {
                status = "Playback output failed";
                break;
            }

            totalDecodedBytes += out.decoded_size;
            const uint32_t bytesPerFrame = (static_cast<uint32_t>(channels) * static_cast<uint32_t>(bitsPerSample)) / 8U;
            if ((bytesPerFrame > 0) && (sampleRate > 0) &&
                (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(5)) == pdTRUE)) {
                const uint64_t totalFrames = totalDecodedBytes / bytesPerFrame;
                _playbackElapsedSeconds = static_cast<uint32_t>(totalFrames / sampleRate);
                xSemaphoreGive(_stateMutex);
            }
        }
    } while (false);

    if (decoder != nullptr) {
        esp_audio_simple_dec_close(decoder);
    }
    if (file != nullptr) {
        std::fclose(file);
    }

    bool stoppedByUser = false;
    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        stoppedByUser = _playbackStopRequested;
        _playbackTaskHandle = nullptr;
        _playbackActive = false;
        _playbackStopRequested = false;
        _playingIndex = std::numeric_limits<size_t>::max();
        _playbackStartedAt = 0;
        _playbackElapsedSeconds = 0;
        _playbackDurationSeconds = 0;
        _activePlaybackPath.clear();
        _spectrumValues.fill(0);
        _statusMessage = stoppedByUser ? "Playback stopped" : (status.empty() ? "Playback finished" : status);
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
    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }

    if (_playbackActive) {
        if (_playbackTaskHandle != nullptr) {
            const TickType_t elapsedTicks = xTaskGetTickCount() - _playbackStartedAt;
            const uint32_t elapsedSeconds = static_cast<uint32_t>(pdTICKS_TO_MS(elapsedTicks) / 1000U);
            _playbackElapsedSeconds = (_playbackDurationSeconds > 0)
                                          ? std::min(elapsedSeconds, _playbackDurationSeconds)
                                          : elapsedSeconds;
        }
    }

    xSemaphoreGive(_stateMutex);
}

void RecorderApp::playbackTaskEntry(void *context)
{
    auto *app = static_cast<RecorderApp *>(context);
    if (app != nullptr) {
        app->playbackTask();
    }
    vTaskDelete(nullptr);
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