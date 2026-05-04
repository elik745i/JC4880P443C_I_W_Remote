#include "RecorderInternal.hpp"

using namespace recorder;

void RecorderApp::updateSpectrumFromPcm(const int16_t *samples, size_t sampleCount)
{
    if ((samples == nullptr) || (sampleCount < 64U)) {
        return;
    }

    constexpr size_t kWindow = 384;
    const size_t windowSize = std::min(sampleCount, kWindow);
    std::array<int16_t, kSpectrumBins> nextValues = {};

    for (int bin = 0; bin < kSpectrumBins; ++bin) {
        const size_t start = (windowSize * static_cast<size_t>(bin)) / kSpectrumBins;
        const size_t end = (windowSize * static_cast<size_t>(bin + 1)) / kSpectrumBins;
        if (end <= start) {
            continue;
        }

        uint32_t energy = 0;
        int16_t peak = 0;
        for (size_t sampleIndex = start; sampleIndex < end; ++sampleIndex) {
            const int32_t value = samples[sampleIndex];
            const int32_t absValue = (value < 0) ? -value : value;
            energy += static_cast<uint32_t>(absValue);
            if (absValue > peak) {
                peak = static_cast<int16_t>(absValue);
            }
        }

        const uint32_t count = static_cast<uint32_t>(end - start);
        const uint32_t average = (count > 0) ? (energy / count) : 0;
        const uint32_t blended = (average * 3U + static_cast<uint32_t>(peak)) / 4U;
        const int scaled = std::min(100, static_cast<int>(blended / 90U));
        nextValues[bin] = static_cast<int16_t>(scaled);
    }

    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        _spectrumValues = nextValues;
        xSemaphoreGive(_stateMutex);
    }
}

bool RecorderApp::ensureRecordDirectoryAvailable(bool allowMount)
{
    if (allowMount && !app_storage_ensure_sdcard_available()) {
        return false;
    }

    if (!app_storage_is_sdcard_mounted()) {
        return false;
    }

    return ensure_directory(kRecordDirectory);
}

bool RecorderApp::startRecording()
{
    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        const bool busy = _recordingActive || (_recordTaskHandle != nullptr) || _stopRequested;
        xSemaphoreGive(_stateMutex);
        if (busy) {
            return false;
        }
    } else {
        return false;
    }

    if (!ensureRecordDirectoryAvailable(true)) {
        setStatusMessage("SD card is not ready for recording");
        return false;
    }

    _activeRecordingPath = createRecordingPath();
    if (_activeRecordingPath.empty()) {
        setStatusMessage("Could not create a timestamped recording filename");
        return false;
    }

    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        _recordingActive = true;
        _stopRequested = false;
        _recordedSeconds = 0;
        _spectrumValues.fill(0);
        _statusMessage = "Recording to SD card...";
        xSemaphoreGive(_stateMutex);
    }

    const BaseType_t created = create_task_prefer_psram(
        recordTaskEntry,
        "recorder_task",
        kRecordTaskStack,
        this,
        kRecordTaskPriority,
        &_recordTaskHandle,
        1
    );

    if (created != pdPASS) {
        if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            _recordingActive = false;
            _stopRequested = false;
            _statusMessage = "Failed to start recorder task";
            xSemaphoreGive(_stateMutex);
        }
        _recordTaskHandle = nullptr;
        return false;
    }

    return true;
}

void RecorderApp::stopRecording()
{
    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (_recordingActive || (_recordTaskHandle != nullptr) || _stopRequested) {
            _stopRequested = true;
            _recordButtonCooldownUntil = xTaskGetTickCount() + kRecordButtonCooldown;
            _statusMessage = "Finishing recording...";
        }
        xSemaphoreGive(_stateMutex);
    }
}

bool RecorderApp::isRecordingOrStopping()
{
    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        const bool active = _recordingActive || (_recordTaskHandle != nullptr) || _stopRequested;
        xSemaphoreGive(_stateMutex);
        return active;
    }

    return true;
}

void RecorderApp::recordTask()
{
    FILE *output = nullptr;
    void *encoder = nullptr;
    bool success = false;
    std::string status;
    std::string finalPath;
    uint64_t totalPcmBytes = 0;
    uint64_t totalEncodedBytes = 0;
    size_t finalFileSize = 0;

    do {
        audio_player_stop();

        esp_err_t err = bsp_extra_codec_dev_stop();
        if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
            status = std::string("Codec stop failed: ") + esp_err_to_name(err);
            break;
        }

        err = bsp_extra_codec_init();
        if (err != ESP_OK) {
            status = std::string("Codec init failed: ") + esp_err_to_name(err);
            break;
        }

        err = bsp_extra_codec_set_fs(kSampleRate, kBitsPerSample, I2S_SLOT_MODE_MONO);
        if (err != ESP_OK) {
            status = std::string("Mic sample format failed: ") + esp_err_to_name(err);
            break;
        }

        err = bsp_extra_audio_mic_gain_set_level(bsp_extra_audio_mic_gain_get_level());
        if (err != ESP_OK) {
            status = std::string("Mic gain failed: ") + esp_err_to_name(err);
            break;
        }

        finalPath = _activeRecordingPath;
        output = std::fopen(finalPath.c_str(), "wb");
        if (output == nullptr) {
            status = "Unable to create AAC file on SD card";
            break;
        }

        esp_aac_enc_config_t config = ESP_AAC_ENC_CONFIG_DEFAULT();
        config.sample_rate = static_cast<int>(kSampleRate);
        config.channel = 1;
        config.bits_per_sample = static_cast<int>(kBitsPerSample);
        config.bitrate = kAacBitrate;
        config.adts_used = true;

        if (esp_aac_enc_open(&config, sizeof(config), &encoder) != ESP_AUDIO_ERR_OK) {
            status = "Failed to open AAC encoder";
            break;
        }

        int inFrameSize = 0;
        int outFrameSize = 0;
        if (esp_aac_enc_get_frame_size(encoder, &inFrameSize, &outFrameSize) != ESP_AUDIO_ERR_OK ||
            (inFrameSize <= 0) || (outFrameSize <= 0)) {
            status = "Failed to query AAC frame size";
            break;
        }

        HeapCapsBuffer pcmBuffer = allocate_audio_buffer(static_cast<size_t>(inFrameSize));
        HeapCapsBuffer encodedBuffer = allocate_audio_buffer(static_cast<size_t>(outFrameSize));
        if ((pcmBuffer == nullptr) || (encodedBuffer == nullptr)) {
            status = "Not enough memory for recorder buffers";
            break;
        }

        TickType_t nextSpectrumTick = 0;
        TickType_t nextListRefreshTick = 0;
        while (true) {
            bool stopNow = false;
            if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                stopNow = _stopRequested;
                xSemaphoreGive(_stateMutex);
            }
            if (stopNow) {
                break;
            }

            size_t filled = 0;
            while (filled < static_cast<size_t>(inFrameSize)) {
                if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    stopNow = _stopRequested;
                    xSemaphoreGive(_stateMutex);
                }
                if (stopNow) {
                    break;
                }

                size_t bytesRead = 0;
                err = bsp_extra_i2s_read(pcmBuffer.get() + filled,
                                         static_cast<size_t>(inFrameSize) - filled,
                                         &bytesRead,
                                         200);
                if (err != ESP_OK) {
                    if (err == ESP_ERR_TIMEOUT) {
                        continue;
                    }
                    status = std::string("Mic read failed: ") + esp_err_to_name(err);
                    break;
                }
                if (bytesRead == 0) {
                    continue;
                }
                filled += bytesRead;
            }

            if (!status.empty() || stopNow) {
                break;
            }

            apply_shared_mic_gain(reinterpret_cast<int16_t *>(pcmBuffer.get()),
                                  static_cast<size_t>(inFrameSize) / sizeof(int16_t));

            const TickType_t now = xTaskGetTickCount();
            if ((nextSpectrumTick == 0) || (now >= nextSpectrumTick)) {
                updateSpectrumFromPcm(reinterpret_cast<const int16_t *>(pcmBuffer.get()),
                                     static_cast<size_t>(inFrameSize) / sizeof(int16_t));
                nextSpectrumTick = now + pdMS_TO_TICKS(kSpectrumUpdateIntervalMs);
            }

            esp_audio_enc_in_frame_t inFrame = {
                .buffer = pcmBuffer.get(),
                .len = static_cast<uint32_t>(inFrameSize),
            };
            esp_audio_enc_out_frame_t outFrame = {
                .buffer = encodedBuffer.get(),
                .len = static_cast<uint32_t>(outFrameSize),
                .encoded_bytes = 0,
                .pts = 0,
            };

            const esp_audio_err_t encErr = esp_aac_enc_process(encoder, &inFrame, &outFrame);
            if (encErr != ESP_AUDIO_ERR_OK) {
                status = "AAC encode failed";
                break;
            }

            if ((outFrame.encoded_bytes > 0) &&
                (std::fwrite(encodedBuffer.get(), 1, outFrame.encoded_bytes, output) != outFrame.encoded_bytes)) {
                status = "Failed to write AAC data to SD card";
                break;
            }

            if (outFrame.encoded_bytes > 0) {
                totalEncodedBytes += outFrame.encoded_bytes;

                const TickType_t now = xTaskGetTickCount();
                if ((nextListRefreshTick == 0) || (now >= nextListRefreshTick)) {
                    if (std::fflush(output) != 0) {
                        status = "Failed to flush AAC data to SD card";
                        break;
                    }

                    const int fileDescriptor = fileno(output);
                    if ((fileDescriptor >= 0) && (fsync(fileDescriptor) != 0)) {
                        status = "Failed to sync AAC data to SD card";
                        break;
                    }

                    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                        _refreshListPending = true;
                        xSemaphoreGive(_stateMutex);
                    }
                    nextListRefreshTick = now + pdMS_TO_TICKS(1000);
                }
            }

            totalPcmBytes += static_cast<uint64_t>(inFrameSize);
            const uint32_t totalSeconds = static_cast<uint32_t>(totalPcmBytes / ((kBitsPerSample / 8U) * kSampleRate));
            if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                _recordedSeconds = totalSeconds;
                xSemaphoreGive(_stateMutex);
            }

            vTaskDelay(1);
        }

        success = status.empty() && !finalPath.empty() && (totalEncodedBytes > 0);
    } while (false);

    if (encoder != nullptr) {
        esp_aac_enc_close(encoder);
    }
    if (output != nullptr) {
        if (status.empty()) {
            if (std::fflush(output) != 0) {
                status = "Failed to flush final AAC data to SD card";
            } else {
                const int fileDescriptor = fileno(output);
                if ((fileDescriptor >= 0) && (fsync(fileDescriptor) != 0)) {
                    status = "Failed to sync final AAC data to SD card";
                }
            }
        }
        std::fclose(output);
    }

    if (!finalPath.empty()) {
        struct stat fileInfo = {};
        if (stat(finalPath.c_str(), &fileInfo) == 0) {
            finalFileSize = static_cast<size_t>(fileInfo.st_size);
        }
    }

    success = !finalPath.empty() && (finalFileSize > 0);

    if (!success && !finalPath.empty()) {
        unlink(finalPath.c_str());
    }

    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        _recordTaskHandle = nullptr;
        _recordingActive = false;
        _stopRequested = false;
        _refreshListPending = true;
        _spectrumValues.fill(0);
        _recordButtonCooldownUntil = xTaskGetTickCount() + kRecordButtonCooldown;

        if (success) {
            const char *fileName = strrchr(finalPath.c_str(), '/');
            _statusMessage = std::string("Saved ") + ((fileName != nullptr) ? (fileName + 1) : finalPath.c_str());
        } else if (!status.empty()) {
            _statusMessage = status;
        } else if (totalPcmBytes > 0) {
            _statusMessage = "Recording was too short to save";
        } else {
            _statusMessage = "Recording canceled";
        }
        xSemaphoreGive(_stateMutex);
    }
}

bool RecorderApp::requestRecordButtonToggle()
{
    enum class Action {
        None,
        Start,
        Stop,
    };

    Action action = Action::None;
    const TickType_t now = xTaskGetTickCount();

    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return false;
    }

    const bool isBusy = _recordingActive || (_recordTaskHandle != nullptr) || _stopRequested;
    if (isBusy) {
        _stopRequested = true;
        _recordButtonCooldownUntil = now + kRecordButtonCooldown;
        _statusMessage = "Finishing recording...";
        action = Action::Stop;
    } else if (now < _recordButtonCooldownUntil) {
        xSemaphoreGive(_stateMutex);
        return false;
    } else {
        action = Action::Start;
    }

    xSemaphoreGive(_stateMutex);

    if (action == Action::Stop) {
        return true;
    }

    if (action == Action::Start) {
        return startRecording();
    }

    return false;
}

std::string RecorderApp::createRecordingPath() const
{
    std::time_t now = std::time(nullptr);
    std::tm timeInfo = {};
    if (localtime_r(&now, &timeInfo) == nullptr) {
        return {};
    }

    char path[96];
    std::snprintf(path,
                  sizeof(path),
                  "%s/%04d%02d%02d_%02d%02d%02d.aac",
                  kRecordDirectory,
                  timeInfo.tm_year + 1900,
                  timeInfo.tm_mon + 1,
                  timeInfo.tm_mday,
                  timeInfo.tm_hour,
                  timeInfo.tm_min,
                  timeInfo.tm_sec);
    return path;
}

void RecorderApp::recordTaskEntry(void *context)
{
    auto *app = static_cast<RecorderApp *>(context);
    if (app != nullptr) {
        app->recordTask();
    }
    vTaskDelete(nullptr);
}