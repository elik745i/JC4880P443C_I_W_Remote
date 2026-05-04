#include "RecorderInternal.hpp"

using namespace recorder;

RecorderApp::RecorderApp()
    : ESP_Brookesia_PhoneApp("Recorder", &record_png, true),
      _titleLabel(nullptr),
      _statusLabel(nullptr),
      _recordButton(nullptr),
      _recordPulseRing(nullptr),
      _recordTimeLabel(nullptr),
      _spectrumChart(nullptr),
      _spectrumSeries(nullptr),
    _spectrumBars{},
    _spectrumCaps{},
      _recordingsList(nullptr),
      _emptyLabel(nullptr),
      _uiTimer(nullptr),
      _stateMutex(nullptr),
      _recordTaskHandle(nullptr),
      _recordingActive(false),
      _stopRequested(false),
    _recordButtonCooldownUntil(0),
      _refreshListPending(true),
      _recordedSeconds(0),
      _spectrumValues{},
    _spectrumDisplayValues{},
    _spectrumPeakValues{},
    _playbackActive(false),
    _playbackStopRequested(false),
    _playingIndex(std::numeric_limits<size_t>::max()),
    _playbackStartedAt(0),
    _playbackElapsedSeconds(0),
    _playbackDurationSeconds(0),
      _statusMessage("Ready to record to SD card")
{
}

RecorderApp::~RecorderApp()
{
    if (_stateMutex != nullptr) {
        vSemaphoreDelete(_stateMutex);
        _stateMutex = nullptr;
    }
}

bool RecorderApp::init()
{
    if (_stateMutex == nullptr) {
        _stateMutex = xSemaphoreCreateMutex();
        if (_stateMutex == nullptr) {
            return false;
        }
    }

    _spectrumValues.fill(0);
    _spectrumDisplayValues.fill(0);
    _spectrumPeakValues.fill(0);
    return true;
}

bool RecorderApp::run()
{
    buildUi();
    refreshRecordingList();
    refreshRecordButtonState();
    tickUi();

    if (_uiTimer != nullptr) {
        lv_timer_del(_uiTimer);
    }
    _uiTimer = lv_timer_create(onUiTimer, kUiTickMs, this);

    return _uiTimer != nullptr;
}

bool RecorderApp::pause()
{
    if (_uiTimer != nullptr) {
        lv_timer_pause(_uiTimer);
    }

    if (isRecordingOrStopping()) {
        ESP_LOGI(kTag, "Keeping recorder active while the app is minimized");
    }

    return true;
}

bool RecorderApp::resume()
{
    if (_uiTimer != nullptr) {
        lv_timer_resume(_uiTimer);
    }

    tickUi();
    return true;
}

bool RecorderApp::back()
{
    if (_recordingActive) {
        stopRecording();
        return true;
    }

    return notifyCoreClosed();
}

bool RecorderApp::close()
{
    stopRecording();
    waitForRecordTaskStop();
    audio_player_stop();
    bsp_extra_codec_dev_stop();

    if (_uiTimer != nullptr) {
        lv_timer_del(_uiTimer);
        _uiTimer = nullptr;
    }

    _titleLabel = nullptr;
    _statusLabel = nullptr;
    _recordButton = nullptr;
    _recordPulseRing = nullptr;
    _recordTimeLabel = nullptr;
    _spectrumChart = nullptr;
    _spectrumSeries = nullptr;
    _spectrumBars.fill(nullptr);
    _spectrumCaps.fill(nullptr);
    _recordingsList = nullptr;
    _emptyLabel = nullptr;
    releaseRuntimeResources();

    return true;
}

