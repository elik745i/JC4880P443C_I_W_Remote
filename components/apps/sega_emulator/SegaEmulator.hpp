#pragma once

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include "esp_heap_caps.h"
#include "driver/ppa.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "bsp/display.h"
#include "esp_brookesia.hpp"
#include "storage_access.h"

template <typename T>
class SegaPsramAllocator {
public:
    using value_type = T;

    SegaPsramAllocator() noexcept = default;

    template <typename U>
    SegaPsramAllocator(const SegaPsramAllocator<U> &) noexcept
    {
    }

    T *allocate(std::size_t count)
    {
        if (count > (static_cast<std::size_t>(-1) / sizeof(T))) {
            std::abort();
        }

        void *ptr = heap_caps_malloc(count * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ptr == nullptr) {
            ptr = heap_caps_malloc(count * sizeof(T), MALLOC_CAP_8BIT);
        }
        if (ptr == nullptr) {
            std::abort();
        }

        return static_cast<T *>(ptr);
    }

    void deallocate(T *ptr, std::size_t) noexcept
    {
        heap_caps_free(ptr);
    }

    template <typename U>
    bool operator==(const SegaPsramAllocator<U> &) const noexcept
    {
        return true;
    }

    template <typename U>
    bool operator!=(const SegaPsramAllocator<U> &) const noexcept
    {
        return false;
    }
};

using SegaString = std::basic_string<char, std::char_traits<char>, SegaPsramAllocator<char>>;

template <typename T>
using SegaVector = std::vector<T, SegaPsramAllocator<T>>;

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
    bool cleanResource() override;

private:
    enum class EmulatorCore {
        SmsPlus,
        Gwenesis,
    };

    struct RomEntry {
        SegaString name;
        SegaString path;
        uint64_t lastPlayedAt = 0;
        lv_obj_t *button = nullptr;
    };

    struct ControlBinding {
        SegaEmulator *app = nullptr;
        uint32_t mask = 0;
    };

    struct ControlRegion {
        uint32_t mask = 0;
        lv_area_t area{};
    };

    struct IndexTaskContext {
        SegaEmulator *app = nullptr;
        bool forceReindex = false;
    };

    struct SaveSlotEntry {
        SegaString slotId;
        SegaString statePath;
        SegaString thumbPath;
        uint64_t sortKey = 0;
        lv_color_t *previewBuffer = nullptr;
    };

    struct SaveSlotButtonContext {
        SegaEmulator *app = nullptr;
        size_t index = 0;
    };

    static constexpr int kCanvasWidth = BSP_LCD_H_RES;
    static constexpr int kCanvasHeight = BSP_LCD_V_RES;
    static constexpr int kSmsAudioSampleRate = 44100;

    static void onRomSelected(lv_event_t *event);
    static void onRefreshClicked(lv_event_t *event);
    static void onRotationChanged(lv_event_t *event);
    static void onSearchChanged(lv_event_t *event);
    static void onSearchFieldEvent(lv_event_t *event);
    static void onSearchKeyboardEvent(lv_event_t *event);
    static void onInitialIndexPromptEvent(lv_event_t *event);
    static void onPrevPageClicked(lv_event_t *event);
    static void onNextPageClicked(lv_event_t *event);
    static void onControlButtonEvent(lv_event_t *event);
    static void onPlayerActionEvent(lv_event_t *event);
    static void onLoadSlotSelected(lv_event_t *event);
    static void onLoadPickerCancel(lv_event_t *event);
    static void onBrowserScreenDeleted(lv_event_t *event);
    static void onBrowserScreenLoaded(lv_event_t *event);
    static void onPlayerScreenDeleted(lv_event_t *event);
    static void emulatorTaskEntry(void *context);
    static void indexingTaskEntry(void *context);
    static void presentFrameAsync(void *context);
    static void finishEmulationAsync(void *context);
    static void indexingProgressAsync(void *context);
    static void indexingFinishedAsync(void *context);

    bool ensureRuntimeResources();
    void releaseRuntimeResources();
    bool ensureUiReady();
    void createBrowserScreen();
    void createPlayerScreen();
    void recreatePlayerScreen();
    void resetBrowserUiPointers();
    void resetPlayerUiPointers();
    void releaseUiState();
    void refreshRomList();
    void rebuildRomList();
    void promptInitialIndex();
    void startIndexing(bool forceReindex);
    void indexingTask(bool forceReindex);
    void updateIndexingProgressOnUiThread();
    void finishIndexingOnUiThread();
    void startRom(const SegaString &path, const SegaString &name);
    void stopEmulation();
    void emulatorTask();
    void setBrowserStatus(const char *text);
    void setPlayerStatus(const char *text);
    void setPlayerFps(uint32_t emulatedFpsTimes10, uint32_t drawnFpsTimes10);
    void setPlayerLoadButtonEnabled(bool enabled);
    void showLoadStatePicker();
    void closeLoadStatePicker();
    void setPlayerFullscreen(bool fullscreen);
    void setBrowserChrome();
    void restoreHomeChrome();
    void setIndexingOverlayVisible(bool visible);
    bool queueFramePresentation();
    void resetPerformanceStats();
    void renderCurrentFrame();
    bool rotateFrameWithPpa(const lv_color_t *sourceBuffer,
                            int sourceWidth,
                            int sourceHeight,
                            lv_color_t *targetBuffer,
                            int dstOffsetX,
                            int dstOffsetY,
                            int dstWidth,
                            int dstHeight);
    void presentFrameOnUiThread();
    void finishEmulationOnUiThread();
    void updateSmsInputState();
    void cachePlayerControlRegions();
    bool readTouchInputMask(uint32_t &mask) const;
    bool setupAudio(int sampleRate);
    void teardownAudio();
    bool loadBatterySave();
    void saveBatterySave();
    bool loadResumeState(const SegaString &path);
    bool saveResumeState();
    bool saveResumeState(const SegaString &path);
    bool hasManualSaveState() const;
    bool collectSavedStates(SegaVector<SaveSlotEntry> &entries, bool limitResults) const;
    bool saveStateThumbnail(const SegaString &path);
    void releaseSaveSlotEntries(SegaVector<SaveSlotEntry> &entries);
    bool loadIndexFile();
    bool saveIndexFile(const SegaVector<RomEntry> &entries) const;
    void sortRomEntries();
    void markRomAsPlayed(const SegaString &path);
    void setSearchKeyboardVisible(bool visible);
    uint16_t getPlayerRotationDegrees() const;
    bool matchesRomFilter(const RomEntry &rom) const;
    SegaString buildBatterySavePath() const;
    SegaString buildSavedGamesRootPath() const;
    SegaString buildGameSaveDirectory() const;
    SegaString buildSaveSlotBaseName() const;
    static bool hasSupportedExtension(const SegaString &path);
    static EmulatorCore getCoreForPath(const SegaString &path);

    lv_obj_t *_browserScreen = nullptr;
    lv_obj_t *_playerScreen = nullptr;
    lv_obj_t *_romList = nullptr;
    lv_obj_t *_browserStatus = nullptr;
    lv_obj_t *_searchField = nullptr;
    lv_obj_t *_searchKeyboard = nullptr;
    lv_obj_t *_rotationDropdown = nullptr;
    lv_obj_t *_refreshButton = nullptr;
    lv_obj_t *_clearButton = nullptr;
    lv_obj_t *_prevPageButton = nullptr;
    lv_obj_t *_nextPageButton = nullptr;
    lv_obj_t *_pageLabel = nullptr;
    lv_obj_t *_indexOverlay = nullptr;
    lv_obj_t *_indexProgressLabel = nullptr;
    lv_obj_t *_indexPromptMessageBox = nullptr;
    lv_obj_t *_playerStatus = nullptr;
    lv_obj_t *_playerTitle = nullptr;
    lv_obj_t *_playerFps = nullptr;
    lv_obj_t *_canvas = nullptr;
    lv_obj_t *_loadStateModal = nullptr;

    lv_obj_t *_upButton = nullptr;
    lv_obj_t *_downButton = nullptr;
    lv_obj_t *_leftButton = nullptr;
    lv_obj_t *_rightButton = nullptr;
    lv_obj_t *_buttonA = nullptr;
    lv_obj_t *_buttonB = nullptr;
    lv_obj_t *_buttonC = nullptr;
    lv_obj_t *_startButton = nullptr;
    lv_obj_t *_loadButton = nullptr;
    lv_obj_t *_saveButton = nullptr;
    lv_obj_t *_exitButton = nullptr;

    SegaVector<RomEntry> _romEntries;
    SegaVector<ControlBinding> _controlBindings;
    SegaVector<SaveSlotEntry> _saveSlotEntries;
    std::vector<ControlRegion> _controlRegions;
    std::vector<SaveSlotButtonContext> _saveSlotButtonContexts;

    std::atomic<bool> _running{false};
    std::atomic<bool> _stopRequested{false};
    std::atomic<uint32_t> _inputMask{0};
    TaskHandle_t _emulatorTask = nullptr;
    TaskHandle_t _indexTask = nullptr;
    std::atomic<bool> _indexing{false};
    std::atomic<bool> _closingApp{false};
    std::atomic<bool> _saveRequested{false};
    std::atomic<bool> _loadRequested{false};
    std::atomic<bool> _loadMenuActive{false};
    std::atomic<bool> _showFrameStats{false};

    SegaString _activeRomPath;
    SegaString _activeRomName;
    SegaString _romFilter;
    EmulatorCore _currentCore = EmulatorCore::SmsPlus;
    uint16_t _playerRotationDegrees = 270;
    bool _indexLoaded = false;
    size_t _currentPage = 0;

    lv_color_t *_canvasFrontBuffer = nullptr;
    lv_color_t *_canvasBackBuffer = nullptr;
    lv_color_t *_ppaSourceBuffer = nullptr;
    uint8_t *_emulatorBuffer = nullptr;
    ppa_client_handle_t _ppaHandle = nullptr;
    size_t _cacheLineSize = 0;
    std::atomic<bool> _framePresentationQueued{false};
    std::atomic<uint64_t> _presentTotalUs{0};
    std::atomic<uint32_t> _presentCount{0};
    std::atomic<uint32_t> _presentMaxUs{0};
    std::atomic<uint32_t> _presentQueueBusyCount{0};
    std::mutex _frameBufferMutex;
    std::mutex _indexStateMutex;
    std::mutex _saveStateMutex;
    SegaString _pendingBrowserStatus;
    SegaString _pendingIndexProgress;
    SegaString _pendingIndexStatus;
    SegaString _pendingLoadStatePath;
    SegaVector<RomEntry> _pendingIndexedEntries;
    std::atomic<bool> _finishUiQueued{false};
};