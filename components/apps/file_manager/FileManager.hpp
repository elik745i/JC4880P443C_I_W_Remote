#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lvgl.h"
#include "esp_brookesia.hpp"

class FileManager: public ESP_Brookesia_PhoneApp {
public:
    FileManager();
    ~FileManager() override = default;

    bool init() override;
    bool run() override;
    bool pause() override;
    bool resume() override;
    bool back() override;
    bool close() override;

private:
    struct EntryInfo {
        std::string name;
        std::string path;
        bool isDirectory;
        uint64_t size;
        lv_obj_t *button;
    };

    enum class StorageRoot {
        SdCard,
        Spiffs,
    };

    enum class InputMode {
        None,
        CreateFolder,
        RenameEntry,
    };

    bool buildUi();
    bool ensureUiReady();
    bool ensureStorageReady(StorageRoot root, bool allowMount);
    bool refreshEntries();
    bool switchStorage(StorageRoot root);
    bool navigateTo(const std::string &path);
    bool navigateUp();
    bool openSelectedEntry();
    bool createFolder(const std::string &name);
    bool renameSelectedEntry(const std::string &newName);
    bool deleteSelectedEntry();
    bool showSelectedEntryInfo();

    bool rootAvailable(StorageRoot root) const;
    std::string rootPath(StorageRoot root) const;
    std::string rootLabel(StorageRoot root) const;
    std::string joinPath(const std::string &base, const std::string &name) const;
    std::string formatSize(uint64_t size) const;
    std::string formatEntrySummary() const;
    void setStatus(const std::string &message, bool isError = false);
    void updatePathLabel();
    void updateOverviewCard();
    void updateStorageButtons();
    void updateActionButtons();
    void selectEntry(EntryInfo *entry);
    void resetUiPointers();
    void showInputDialog(InputMode mode, const char *title, const std::string &initialValue);
    void hideInputDialog();
    void showDeleteConfirm();
    static bool removePathRecursively(const std::string &path);

    static void onEntryClicked(lv_event_t *event);
    static void onUpClicked(lv_event_t *event);
    static void onRefreshClicked(lv_event_t *event);
    static void onStorageClicked(lv_event_t *event);
    static void onOpenClicked(lv_event_t *event);
    static void onNewFolderClicked(lv_event_t *event);
    static void onRenameClicked(lv_event_t *event);
    static void onDeleteClicked(lv_event_t *event);
    static void onInfoClicked(lv_event_t *event);
    static void onInputConfirmClicked(lv_event_t *event);
    static void onInputCancelClicked(lv_event_t *event);
    static void onKeyboardDone(lv_event_t *event);
    static void onInfoMessageBoxEvent(lv_event_t *event);
    static void onDeleteMessageBoxEvent(lv_event_t *event);
    static void onScreenDeleted(lv_event_t *event);

    lv_obj_t *_screen;
    lv_obj_t *_titleLabel;
    lv_obj_t *_subtitleLabel;
    lv_obj_t *_pathLabel;
    lv_obj_t *_statusLabel;
    lv_obj_t *_entryList;
    lv_obj_t *_sdButton;
    lv_obj_t *_spiffsButton;
    lv_obj_t *_storageNameLabel;
    lv_obj_t *_storageMetaLabel;
    lv_obj_t *_folderMetaLabel;
    lv_obj_t *_upButton;
    lv_obj_t *_refreshButton;
    lv_obj_t *_openButton;
    lv_obj_t *_newFolderButton;
    lv_obj_t *_renameButton;
    lv_obj_t *_deleteButton;
    lv_obj_t *_infoButton;
    lv_obj_t *_emptyLabel;
    lv_obj_t *_inputPanel;
    lv_obj_t *_inputTitleLabel;
    lv_obj_t *_inputTextArea;
    lv_obj_t *_keyboard;
    lv_obj_t *_dialogMessageBox;

    StorageRoot _activeRoot;
    std::string _currentPath;
    std::vector<EntryInfo> _entries;
    EntryInfo *_selectedEntry;
    InputMode _inputMode;
};
