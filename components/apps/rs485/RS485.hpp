#pragma once

#include <array>
#include <limits>
#include <string>
#include <vector>

#include "esp_brookesia.hpp"
#include "esp_heap_caps.h"

extern "C" {
#include "app_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
}

class RS485App: public ESP_Brookesia_PhoneApp
{
public:
    RS485App();
    ~RS485App() override;

    bool init() override;
    bool run() override;
    bool pause() override;
    bool resume() override;
    bool back() override;
    bool close() override;

    bool debugStartScan();
    bool debugStopScan();
    bool debugSendAscii(const std::string &text);
    bool debugSendHex(const std::string &hexText);
    bool debugExportLog();
    bool debugCloseApp();
    std::string debugDescribeState() const;
    void handleScanProgress(uint8_t current, uint8_t total);
    void handleScanDevice(const rs485_discovered_device_t *device);
    void handleScanLog(const char *message);

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
    };

    static void onUiTimer(lv_timer_t *timer);
    static void onNavButtonEvent(lv_event_t *event);
    static void onActionButtonEvent(lv_event_t *event);
    static void scanTaskEntry(void *context);

    bool buildUi();
    void tickUi();
    void releaseRuntimeResources();
    void switchScreen(int screenId);
    void refreshStatusText();
    void refreshScanView();
    void refreshTerminalView();
    void refreshBrowserView();
    void refreshProfilesView();
    void refreshDashboardView();
    void refreshLogsView();
    void refreshSettingsView();
    void refreshHomeView();
    bool ensureTransportReady();
    bool startScan();
    void stopScan();
    void scanTask();
    bool exportLogsToFile();
    bool saveSelectedDeviceProfile();
    bool performManualRead(uint8_t functionCode);
    bool sendTerminalPayload(bool hexMode);
    void appendLogStatus(const char *summary, const char *payload = nullptr);
    void setStatusMessage(const std::string &message);

    struct ButtonBinding {
        lv_obj_t *button;
        int id;
    };

    lv_obj_t *_root;
    lv_obj_t *_statusLabel;
    lv_obj_t *_titleLabel;
    std::array<lv_obj_t *, 8> _screenPanels;
    std::array<lv_obj_t *, 8> _navButtons;
    std::array<ButtonBinding, 32> _actionBindings;
    size_t _actionBindingCount;

    lv_obj_t *_homeSummaryLabel;

    lv_obj_t *_scanProgressBar;
    lv_obj_t *_scanResultsLabel;
    lv_obj_t *_scanDetailLabel;

    lv_obj_t *_terminalInput;
    lv_obj_t *_terminalTemplatesLabel;
    lv_obj_t *_terminalLogLabel;

    lv_obj_t *_browserResultLabel;

    lv_obj_t *_profilesLabel;

    lv_obj_t *_dashboardPrimaryLabel;
    lv_obj_t *_dashboardSecondaryLabel;
    lv_obj_t *_dashboardChart;
    lv_chart_series_t *_dashboardSeries;

    lv_obj_t *_logsLabel;
    lv_obj_t *_settingsLabel;

    lv_timer_t *_uiTimer;
    SemaphoreHandle_t _stateMutex;
    TaskHandle_t _scanTaskHandle;
    bool _scanCancelRequested;
    bool _scanRunning;
    uint8_t _scanProgressCurrent;
    uint8_t _scanProgressTotal;
    uint8_t _activeScreen;
    uint8_t _lastRenderedLogCount;
    uint8_t _lastPolledSlaveAddress;
    std::array<int16_t, 16> _dashboardTrend;
    uint16_t _lastPrimaryValue;
    uint16_t _lastStartAddress;
    uint32_t _lastPollTimestampMs;
    bool _lastPollHealthy;

    std::string _statusMessage;
    std::string _scanSummaryText;
    std::string _browserSummaryText;

    std::vector<rs485_discovered_device_t, PsramAllocator<rs485_discovered_device_t>> _discoveredDevices;
};