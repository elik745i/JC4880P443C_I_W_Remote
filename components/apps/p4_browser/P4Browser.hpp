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

    struct PageLink {
        app_network_cache::PsramString label;
        app_network_cache::PsramString url;
    };

    struct PageImage {
        app_network_cache::PsramString alt;
        app_network_cache::PsramString url;
    };

    struct PageFormField {
        app_network_cache::PsramString name;
        app_network_cache::PsramString value;
        app_network_cache::PsramString type;
    };

    struct PageForm {
        app_network_cache::PsramString action;
        app_network_cache::PsramString method;
        app_network_cache::PsramString label;
        std::vector<PageFormField, AppPsramAllocator<PageFormField>> fields;
    };

    struct BookmarkEntry {
        app_network_cache::PsramString title;
        app_network_cache::PsramString url;
    };

    struct PageDocument {
        app_network_cache::PsramString title;
        app_network_cache::PsramString url;
        app_network_cache::PsramString body;
        std::vector<PageLink, AppPsramAllocator<PageLink>> links;
        std::vector<PageImage, AppPsramAllocator<PageImage>> images;
        std::vector<PageForm, AppPsramAllocator<PageForm>> forms;
        app_network_cache::PayloadStorage storage = app_network_cache::PayloadStorage::None;

        void clear()
        {
            title.clear();
            url.clear();
            body.clear();
            links.clear();
            images.clear();
            forms.clear();
            storage = app_network_cache::PayloadStorage::None;
        }
    };

    using SearchResultList = std::vector<SearchResult, AppPsramAllocator<SearchResult>>;
    using BookmarkList = std::vector<BookmarkEntry, AppPsramAllocator<BookmarkEntry>>;

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
        PageDocument page;
        app_network_cache::CachedPayload payload;
        SearchResultList results;
    };

    enum class ListMode {
        SearchResults,
        Bookmarks,
    };

private:

    bool buildUi();
    bool ensureUiReady();
    bool hasLiveScreen() const;
    bool loadBookmarks();
    bool saveBookmarks() const;
    bool toggleCurrentBookmark();
    bool hasBookmark(const std::string &url) const;
    void updateBookmarkButton();
    void startSearch();
    void startOpenPage(const std::string &title, const std::string &url, const char *status_text);
    void startOpenResult(size_t index);
    void startOpenBookmark(size_t index);
    void startSubmitForm(size_t index);
    void renderResults();
    void renderBookmarks();
    void renderPageDocument();
    void showResultList();
    void showPage(const PageDocument &page);
    void setStatus(const std::string &status);
    void resetUiPointers();

    static void onScreenDeleted(lv_event_t *event);
    static void onSearchClicked(lv_event_t *event);
    static void onSearchFocus(lv_event_t *event);
    static void onKeyboardReady(lv_event_t *event);
    static void onResultClicked(lv_event_t *event);
    static void onBackToResultsClicked(lv_event_t *event);
    static void onBookmarkToggleClicked(lv_event_t *event);
    static void onBookmarksClicked(lv_event_t *event);
    static void onPageLinkClicked(lv_event_t *event);
    static void onFormSubmitClicked(lv_event_t *event);
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
    lv_obj_t *_detailScroll;
    lv_obj_t *_detailDocument;
    lv_obj_t *_bookmarkButton;
    lv_obj_t *_resultsPanel;
    SearchResultList _results;
    BookmarkList _bookmarks;
    PageDocument _currentPage;
    std::unordered_map<lv_obj_t *, size_t> _buttonIndexMap;
    std::unordered_map<lv_obj_t *, size_t> _pageLinkIndexMap;
    std::unordered_map<lv_obj_t *, size_t> _formIndexMap;
    std::atomic<bool> _requestInFlight;
    ListMode _listMode;
    bool _bookmarksLoaded;
    bool _homeLoaded;
};