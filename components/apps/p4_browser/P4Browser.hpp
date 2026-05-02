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

class P4Browser: public ESP_Brookesia_PhoneApp {
public:
    P4Browser();
    ~P4Browser() override = default;

    bool init() override;
    bool run() override;
    bool pause() override;
    bool resume() override;
    bool back() override;
    bool close() override;

    struct SearchResult {
        app_network_cache::PsramString title;
        app_network_cache::PsramString url;
        app_network_cache::PsramString snippet;
    };

    using SearchResultList = std::vector<SearchResult, AppPsramAllocator<SearchResult>>;

    enum class WorkerAction {
        Search,
        Open,
    };

    struct WorkerResult {
        P4Browser *app = nullptr;
        WorkerAction action = WorkerAction::Search;
        bool success = false;
        std::string status;
        std::string title;
        std::string url;
        std::string body;
        app_network_cache::CachedPayload payload;
        SearchResultList results;
    };

private:

    bool buildUi();
    bool ensureUiReady();
    bool hasLiveScreen() const;
    void startSearch();
    void startOpenResult(size_t index);
    void renderResults();
    void showResultList();
    void showPage(const std::string &title, const std::string &url, const std::string &body,
                  app_network_cache::PayloadStorage storage);
    void setStatus(const std::string &status);
    void resetUiPointers();

    static void onScreenDeleted(lv_event_t *event);
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
    lv_obj_t *_resultsList;
    lv_obj_t *_detailPanel;
    lv_obj_t *_detailTitle;
    lv_obj_t *_detailMeta;
    lv_obj_t *_detailBody;
    lv_obj_t *_resultsPanel;
    SearchResultList _results;
    std::unordered_map<lv_obj_t *, size_t> _buttonIndexMap;
    std::atomic<bool> _requestInFlight;
};