#pragma once

#include <atomic>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "esp_heap_caps.h"
#include "lvgl.h"
#include "audio_player.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_brookesia.hpp"

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

    template <typename U>
    bool operator==(const PsramAllocator<U> &) const noexcept
    {
        return true;
    }

    template <typename U>
    bool operator!=(const PsramAllocator<U> &) const noexcept
    {
        return false;
    }
};

class InternetRadio: public ESP_Brookesia_PhoneApp {
public:
    enum QuickAccessAction {
        QUICK_ACCESS_ACTION_PREVIOUS = 1,
        QUICK_ACCESS_ACTION_NEXT,
    };

    InternetRadio();
    ~InternetRadio() = default;

    bool init(void) override;
    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool pause(void) override;
    bool resume(void) override;

    bool debugPlayStation(const std::string &country, const std::string &station_name);
    bool debugOpenVisible(void);
    bool debugOpenStationVisible(const std::string &country, const std::string &station_name, bool auto_play);
    bool debugStopPlayback(void);
    std::string debugDescribeState(void) const;
    void resetStreamBufferMetrics(void);
    void updateStreamBufferMetrics(uint32_t capacity, uint32_t buffered, uint32_t read_offset,
                                   uint32_t total_http, uint32_t total_audio, bool eof_reached);
    void refreshPlayerDialogBufferInfo(void);
    std::vector<ESP_Brookesia_PhoneQuickAccessActionData_t> getQuickAccessActions(void) const override;
    ESP_Brookesia_PhoneQuickAccessDetailData_t getQuickAccessDetail(void) const override;
    bool handleQuickAccessAction(int action_id) override;

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

    using PsramString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

    struct ListEntry {
                ListEntry() = default;

                ListEntry(std::string_view title_in,
                                    std::string_view subtitle_in,
                                    std::string_view value_in,
                                    std::string_view detail_in,
                                    std::string_view leading_symbol_in,
                                    std::string_view codec_in,
                                    bool can_preview_in)
                        : title(title_in),
                            subtitle(subtitle_in),
                            value(value_in),
                            detail(detail_in),
                            leadingSymbol(leading_symbol_in),
                            codec(codec_in),
                            canPreview(can_preview_in)
                {
                }

        PsramString title;
        PsramString subtitle;
        PsramString value;
        PsramString detail;
        PsramString leadingSymbol;
        PsramString codec;
        bool canPreview;
    };

    using EntryList = std::vector<ListEntry, PsramAllocator<ListEntry>>;

    static void onListButtonClicked(lv_event_t *event);
    static void onPlayerDialogButtonClicked(lv_event_t *event);
    static void onPlayerDialogBufferTimer(lv_timer_t *timer);
    static void onScreenDeleted(lv_event_t *event);
    static esp_err_t httpEventHandler(esp_http_client_event_t *event);
    static void applyAsyncStatus(void *context);
    static void previewPlaybackTask(void *context);
    static void radioAudioCallback(audio_player_cb_ctx_t *ctx);

    bool buildUi(void);
    bool ensureUiReady(void);
    bool hasInternetPrerequisites(void) const;
    bool hasLiveScreen(void) const;
    void applyStatusText(const char *text);
    void setStatus(const std::string &text);
    void setStatusFromTask(const char *text);
    void showRootMenu(void);
    void loadCountries(void);
    void loadLanguages(void);
    void loadTags(void);
    void loadPopularStations(void);
    void loadStationsForFilter(StationSource source, std::string_view display_name, std::string_view value);
    void renderEntries(void);
    void showEntryDetails(size_t index);
    void showPlayerDialog(size_t index);
    void closePlayerDialog(void);
    void playStationPreview(size_t index);
    bool ensurePreviewWorkerReady(void);
    void destroyPreviewWorker(void);
    bool queuePreviewPlayback(const ListEntry &entry, bool from_task);
    bool startPreviewPlayback(const ListEntry &entry);
    bool hasCountryStationQuickAccessTargets(void) const;
    bool resolveQuickAccessStationIndex(size_t *out_index) const;
    bool findAdjacentQuickAccessStationIndex(int direction, size_t *out_index) const;
    bool playQuickAccessStation(size_t index);
    void handleEntrySelection(size_t index);
    const char *entrySymbol(void) const;
    const char *entrySymbol(const ListEntry &entry) const;
    void stopPreviewPlayback(void);
    bool stopPreviewPlaybackInternal(bool from_task);
    void setStatusForContext(const std::string &text, bool from_task);
    void resetUiPointers(void);

    static std::string percentEncode(const std::string &value);
    int findEntryIndexByTitle(const std::string &title) const;
    bool fetchEntries(const std::string &url, ViewMode next_mode, StationSource next_source, const std::string &status_text,
                      EntryList &out_entries);
    bool fetchJsonArray(const std::string &url, EntryList &out_entries, ViewMode mode);

    lv_obj_t *_screen;
    lv_obj_t *_titleLabel;
    lv_obj_t *_subtitleLabel;
    lv_obj_t *_statusLabel;
    lv_obj_t *_list;
    EntryList _entries;
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