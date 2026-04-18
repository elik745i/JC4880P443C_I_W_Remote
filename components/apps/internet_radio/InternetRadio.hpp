#pragma once

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include "lvgl.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
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
    static esp_err_t httpEventHandler(esp_http_client_event_t *event);
    static void applyAsyncStatus(void *context);
    static void previewPlaybackTask(void *context);

    bool buildUi(void);
    void setStatus(const std::string &text);
    void setStatusFromTask(const std::string &text);
    void showRootMenu(void);
    void loadCountries(void);
    void loadLanguages(void);
    void loadTags(void);
    void loadPopularStations(void);
    void loadStationsForFilter(StationSource source, const std::string &display_name, const std::string &value);
    void renderEntries(void);
    void showEntryDetails(size_t index);
    void playStationPreview(size_t index);
    bool startPreviewPlayback(const ListEntry &entry);
    void handleEntrySelection(size_t index);
    const char *entrySymbol(void) const;
    const char *entrySymbol(const ListEntry &entry) const;

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
    std::atomic<bool> _previewStartInProgress;
};