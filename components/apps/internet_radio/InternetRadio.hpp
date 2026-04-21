#pragma once

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include "lvgl.h"
#include "audio_player.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_brookesia.hpp"

class InternetRadio: public ESP_Brookesia_PhoneApp {
public:
    InternetRadio();
    ~InternetRadio() = default;

    bool init(void) override;
    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool pause(void) override;
    bool resume(void) override;

    bool debugPlayStation(const std::string &country, const std::string &station_name);
    bool debugStopPlayback(void);
    std::string debugDescribeState(void) const;
    void resetStreamBufferMetrics(void);
    void updateStreamBufferMetrics(uint32_t capacity, uint32_t buffered, uint32_t read_offset,
                                   uint32_t total_http, uint32_t total_audio, bool eof_reached);
    void refreshPlayerDialogBufferInfo(void);

private:
    enum class ViewMode {
        Root,
        Countries,
        Languages,
        Tags,
        Stations,
    };

    enum class StationSource {
        None,
        Popular,
        Country,
        Language,
        Tag,
    };

    struct ListEntry {
        std::string title;
        std::string subtitle;
        std::string value;
        std::string detail;
        std::string leadingSymbol;
        std::string codec;
        bool canPreview;
    };

    static void onListButtonClicked(lv_event_t *event);
    static void onPlayerDialogButtonClicked(lv_event_t *event);
    static void onPlayerDialogBufferTimer(lv_timer_t *timer);
    static esp_err_t httpEventHandler(esp_http_client_event_t *event);
    static void applyAsyncStatus(void *context);
    static void previewPlaybackTask(void *context);
    static void radioAudioCallback(audio_player_cb_ctx_t *ctx);

    bool buildUi(void);
    void applyStatusText(const char *text);
    void setStatus(const std::string &text);
    void setStatusFromTask(const char *text);
    void showRootMenu(void);
    void loadCountries(void);
    void loadLanguages(void);
    void loadTags(void);
    void loadPopularStations(void);
    void loadStationsForFilter(StationSource source, const std::string &display_name, const std::string &value);
    void renderEntries(void);
    void showEntryDetails(size_t index);
    void showPlayerDialog(size_t index);
    void closePlayerDialog(void);
    void playStationPreview(size_t index);
    bool ensurePreviewWorkerReady(void);
    void destroyPreviewWorker(void);
    bool queuePreviewPlayback(const ListEntry &entry, bool from_task);
    bool startPreviewPlayback(const ListEntry &entry);
    void handleEntrySelection(size_t index);
    const char *entrySymbol(void) const;
    const char *entrySymbol(const ListEntry &entry) const;
    void stopPreviewPlayback(void);
    bool stopPreviewPlaybackInternal(bool from_task);
    void setStatusForContext(const std::string &text, bool from_task);

    static std::string percentEncode(const std::string &value);
    bool fetchEntries(const std::string &url, ViewMode next_mode, StationSource next_source, const std::string &status_text,
                      std::vector<ListEntry> &out_entries);
    bool fetchJsonArray(const std::string &url, std::vector<ListEntry> &out_entries, ViewMode mode);

    lv_obj_t *_screen;
    lv_obj_t *_titleLabel;
    lv_obj_t *_subtitleLabel;
    lv_obj_t *_statusLabel;
    lv_obj_t *_list;
    std::vector<ListEntry> _entries;
    std::unordered_map<lv_obj_t *, size_t> _buttonIndexMap;
    ViewMode _viewMode;
    StationSource _stationSource;
    std::string _activeFilterDisplay;
    std::string _activeFilterValue;
    TaskHandle_t _previewTaskHandle;
    QueueHandle_t _previewCommandQueue;
    bool _previewTaskUsesCapsStack;
    std::atomic<bool> _previewStartInProgress;
    lv_obj_t *_playerDialog;
    lv_obj_t *_playerDialogTitleLabel;
    lv_obj_t *_playerDialogDetailLabel;
    lv_obj_t *_playerDialogStatusLabel;
    lv_obj_t *_playerDialogBufferLabel;
    lv_obj_t *_playerDialogBufferBar;
    lv_obj_t *_playerDialogPlayButton;
    lv_obj_t *_playerDialogStopButton;
    lv_timer_t *_playerDialogBufferTimer;
    size_t _selectedStationIndex;
    std::string _activeStationTitle;
    std::atomic<uint32_t> _streamBufferCapacity;
    std::atomic<uint32_t> _streamBytesBuffered;
    std::atomic<uint32_t> _streamReadOffset;
    std::atomic<uint32_t> _streamTotalHttpBytes;
    std::atomic<uint32_t> _streamTotalAudioBytes;
    std::atomic<bool> _streamEofReached;
};