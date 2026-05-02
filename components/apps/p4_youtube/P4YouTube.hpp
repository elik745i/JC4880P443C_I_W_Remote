#pragma once

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include "esp_brookesia.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "network_cache.hpp"

class P4YouTube: public ESP_Brookesia_PhoneApp {
public:
    P4YouTube();
    ~P4YouTube() override = default;

    bool init() override;
    bool run() override;
    bool pause() override;
    bool resume() override;
    bool back() override;
    bool close() override;

    struct VideoResult {
        app_network_cache::PsramString title;
        app_network_cache::PsramString video_id;
        app_network_cache::PsramString author;
        app_network_cache::PsramString detail;
    };

    using VideoResultList = std::vector<VideoResult, AppPsramAllocator<VideoResult>>;

    enum class WorkerAction {
        Search,
        Detail,
    };

    struct WorkerResult {
        P4YouTube *app = nullptr;
        WorkerAction action = WorkerAction::Search;
        bool success = false;
        std::string status;
        std::string title;
        std::string video_id;
        std::string body;
        app_network_cache::CachedPayload payload;
        VideoResultList results;
    };

private:

    bool buildUi();
    bool ensureUiReady();
    bool hasLiveScreen() const;
    void startSearch();
    void startOpenVideo(size_t index);
    void renderResults();
    void showResultList();
    void showVideo(const std::string &title, const std::string &video_id, const std::string &body,
                   app_network_cache::PayloadStorage storage);
    void setStatus(const std::string &status);
    void resetUiPointers();

    static void onSearchClicked(lv_event_t *event);
    static void onSearchFocus(lv_event_t *event);
    static void onKeyboardReady(lv_event_t *event);
    static void onResultClicked(lv_event_t *event);
    static void onBackToResultsClicked(lv_event_t *event);
    static void workerTask(void *context);
    static void applyWorkerResult(void *context);

    lv_obj_t *_screen;
    lv_obj_t *_statusLabel;
    lv_obj_t *_searchArea;
    lv_obj_t *_keyboard;
    lv_obj_t *_resultsPanel;
    lv_obj_t *_resultsList;
    lv_obj_t *_detailPanel;
    lv_obj_t *_detailTitle;
    lv_obj_t *_detailMeta;
    lv_obj_t *_detailBody;
    VideoResultList _results;
    std::unordered_map<lv_obj_t *, size_t> _buttonIndexMap;
    std::atomic<bool> _requestInFlight;
};