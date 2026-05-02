#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "esp_brookesia.hpp"
#include "lvgl.h"

class EReaderApp: public ESP_Brookesia_PhoneApp {
public:
    EReaderApp();
    ~EReaderApp() override = default;

    bool init() override;
    bool run() override;
    bool pause() override;
    bool resume() override;
    bool back() override;
    bool close() override;

private:
    struct Entry {
        std::string name;
        std::string path;
        bool is_directory = false;
    };

    bool buildUi();
    bool ensureUiReady();
    bool hasLiveScreen() const;
    bool refreshEntries();
    bool navigateTo(const std::string &path);
    bool navigateUp();
    bool openEntry(size_t index);
    bool switchRoot(bool use_sdcard);
    void renderEntries();
    void showReader(const std::string &title, const std::string &content);
    void showList();
    void setStatus(const std::string &status);
    void updatePathLabel();
    void resetUiPointers();

    static void onScreenDeleted(lv_event_t *event);
    static void onEntryClicked(lv_event_t *event);
    static void onRefreshClicked(lv_event_t *event);
    static void onUpClicked(lv_event_t *event);
    static void onSdClicked(lv_event_t *event);
    static void onSpiffsClicked(lv_event_t *event);
    static void onCloseReaderClicked(lv_event_t *event);

    lv_obj_t *_screen;
    lv_obj_t *_statusLabel;
    lv_obj_t *_pathLabel;
    lv_obj_t *_entryList;
    lv_obj_t *_emptyLabel;
    lv_obj_t *_readerPanel;
    lv_obj_t *_readerTitle;
    lv_obj_t *_readerBody;
    lv_obj_t *_sdButton;
    lv_obj_t *_spiffsButton;
    std::vector<Entry> _entries;
    std::unordered_map<lv_obj_t *, size_t> _buttonIndexMap;
    std::string _rootPath;
    std::string _currentPath;
    bool _usingSdCard;
};