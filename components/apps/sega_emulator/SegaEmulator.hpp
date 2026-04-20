#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "esp_brookesia.hpp"

class SegaEmulator : public ESP_Brookesia_PhoneApp {
public:
    SegaEmulator();
    ~SegaEmulator() override;

    bool init() override;
    bool run() override;
    bool pause() override;
    bool resume() override;
    bool back() override;
    bool close() override;

private:
    enum class EmulatorCore {
        SmsPlus,
        Gwenesis,
    };

    struct RomEntry {
        std::string name;
        std::string path;
        lv_obj_t *button = nullptr;
    };

    struct ControlBinding {
        SegaEmulator *app = nullptr;
        uint32_t mask = 0;
    };

    static constexpr int kCanvasWidth = 400;
    static constexpr int kCanvasHeight = 300;
    static constexpr int kSmsAudioSampleRate = 44100;

    static void onRomSelected(lv_event_t *event);
    static void onRefreshClicked(lv_event_t *event);
    static void onControlButtonEvent(lv_event_t *event);
    static void emulatorTaskEntry(void *context);
    static void presentFrameAsync(void *context);
    static void finishEmulationAsync(void *context);

    bool ensureUiReady();
    void createBrowserScreen();
    void createPlayerScreen();
    void refreshRomList();
    void startRom(const std::string &path, const std::string &name);
    void stopEmulation();
    void emulatorTask();
    void setBrowserStatus(const char *text);
    void setPlayerStatus(const char *text);
    void renderCurrentFrame();
    void presentFrameOnUiThread();
    void finishEmulationOnUiThread();
    void updateSmsInputState();
    bool setupAudio(int sampleRate);
    void teardownAudio();
    bool loadBatterySave();
    void saveBatterySave();
    std::string buildSavePath() const;
    static bool hasSupportedExtension(const std::string &path);
    static EmulatorCore getCoreForPath(const std::string &path);

    lv_obj_t *_browserScreen = nullptr;
    lv_obj_t *_playerScreen = nullptr;
    lv_obj_t *_romList = nullptr;
    lv_obj_t *_browserStatus = nullptr;
    lv_obj_t *_playerStatus = nullptr;
    lv_obj_t *_playerTitle = nullptr;
    lv_obj_t *_canvas = nullptr;

    lv_obj_t *_upButton = nullptr;
    lv_obj_t *_downButton = nullptr;
    lv_obj_t *_leftButton = nullptr;
    lv_obj_t *_rightButton = nullptr;
    lv_obj_t *_buttonA = nullptr;
    lv_obj_t *_buttonB = nullptr;
    lv_obj_t *_buttonC = nullptr;
    lv_obj_t *_startButton = nullptr;

    std::vector<RomEntry> _romEntries;
    std::vector<ControlBinding *> _controlBindings;

    std::atomic<bool> _running{false};
    std::atomic<bool> _stopRequested{false};
    std::atomic<uint32_t> _inputMask{0};
    TaskHandle_t _emulatorTask = nullptr;

    std::string _activeRomPath;
    std::string _activeRomName;
    EmulatorCore _currentCore = EmulatorCore::SmsPlus;

    lv_color_t *_canvasFrontBuffer = nullptr;
    lv_color_t *_canvasBackBuffer = nullptr;
    uint8_t *_emulatorBuffer = nullptr;
    std::atomic<bool> _framePresentationQueued{false};
    std::mutex _frameBufferMutex;
    std::string _pendingBrowserStatus;
    std::atomic<bool> _finishUiQueued{false};
};