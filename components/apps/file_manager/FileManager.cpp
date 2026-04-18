#include "FileManager.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "FileManager";
static constexpr const char *kSdRoot = "/sdcard";
static constexpr const char *kSpiffsRoot = BSP_SPIFFS_MOUNT_POINT;
static constexpr intptr_t kDialogModeInfo = 0;
static constexpr intptr_t kDialogModeDelete = 1;

LV_IMG_DECLARE(img_app_file_manager);

namespace {

bool is_path_directory(const std::string &path)
{
    struct stat st = {};
    return (stat(path.c_str(), &st) == 0) && S_ISDIR(st.st_mode);
}

void set_button_enabled(lv_obj_t *button, bool enabled)
{
    if (button == nullptr) {
        return;
    }

    if (enabled) {
        lv_obj_clear_state(button, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }
}

lv_obj_t *create_action_button(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y, lv_coord_t width = 136)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, 54);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0xD6DEEA), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    return button;
}

lv_obj_t *create_card(lv_obj_t *parent, lv_coord_t width, lv_coord_t height, lv_coord_t x, lv_coord_t y, lv_color_t color)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, width, height);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, color, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 24, 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x22304A), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        return card;
}

} // namespace

FileManager::FileManager()
    : ESP_Brookesia_PhoneApp("Files", &img_app_file_manager, true),
      _screen(nullptr),
      _titleLabel(nullptr),
            _subtitleLabel(nullptr),
      _pathLabel(nullptr),
            _statusLabel(nullptr),
      _entryList(nullptr),
      _sdButton(nullptr),
      _spiffsButton(nullptr),
            _storageNameLabel(nullptr),
            _storageMetaLabel(nullptr),
            _folderMetaLabel(nullptr),
            _upButton(nullptr),
      _refreshButton(nullptr),
      _openButton(nullptr),
      _newFolderButton(nullptr),
      _renameButton(nullptr),
            _deleteButton(nullptr),
      _infoButton(nullptr),
      _emptyLabel(nullptr),
      _inputPanel(nullptr),
      _inputTitleLabel(nullptr),
      _inputTextArea(nullptr),
      _keyboard(nullptr),
            _dialogMessageBox(nullptr),
      _activeRoot(StorageRoot::Spiffs),
      _currentPath(rootPath(StorageRoot::Spiffs)),
      _selectedEntry(nullptr),
      _inputMode(InputMode::None)
{
}

bool FileManager::init()
{
    if (!buildUi()) {
        return false;
    }

    if (rootAvailable(StorageRoot::SdCard)) {
        return switchStorage(StorageRoot::SdCard);
    }

    return switchStorage(StorageRoot::Spiffs);
}

bool FileManager::run()
{
    if (_screen == nullptr) {
        return false;
    }

    lv_scr_load(_screen);
    return refreshEntries();
}

bool FileManager::pause()
{
    return true;
}

bool FileManager::resume()
{
    return refreshEntries();
}

bool FileManager::back()
{
    if ((_dialogMessageBox != nullptr) && lv_obj_is_valid(_dialogMessageBox)) {
        lv_msgbox_close(_dialogMessageBox);
        _dialogMessageBox = nullptr;
        return true;
    }

    if ((_inputPanel != nullptr) && !lv_obj_has_flag(_inputPanel, LV_OBJ_FLAG_HIDDEN)) {
        hideInputDialog();
        return true;
    }

    if (_selectedEntry != nullptr) {
        selectEntry(nullptr);
        return true;
    }

    if (_currentPath != rootPath(_activeRoot)) {
        return navigateUp();
    }

    return notifyCoreClosed();
}

bool FileManager::close()
{
    hideInputDialog();
    if ((_dialogMessageBox != nullptr) && lv_obj_is_valid(_dialogMessageBox)) {
        lv_msgbox_close(_dialogMessageBox);
        _dialogMessageBox = nullptr;
    }
    selectEntry(nullptr);
    return true;
}

bool FileManager::buildUi()
{
    _screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_screen, lv_color_hex(0xEEF3F9), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *heroCard = create_card(_screen, 440, 130, 20, 18, lv_color_hex(0x102A43));
    lv_obj_set_style_bg_grad_color(heroCard, lv_color_hex(0x1D4ED8), 0);
    lv_obj_set_style_bg_grad_dir(heroCard, LV_GRAD_DIR_HOR, 0);

    _titleLabel = lv_label_create(heroCard);
    lv_label_set_text(_titleLabel, "Files");
    lv_obj_set_style_text_font(_titleLabel, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(_titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_titleLabel, LV_ALIGN_TOP_LEFT, 18, 16);

    _subtitleLabel = lv_label_create(heroCard);
    lv_label_set_text(_subtitleLabel, "Android-style browser for SD card and SPIFFS");
    lv_obj_set_style_text_font(_subtitleLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_subtitleLabel, lv_color_hex(0xC7D8FF), 0);
    lv_obj_align(_subtitleLabel, LV_ALIGN_TOP_LEFT, 18, 52);

    _pathLabel = lv_label_create(heroCard);
    lv_label_set_long_mode(_pathLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(_pathLabel, 404);
    lv_obj_set_style_text_font(_pathLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_pathLabel, lv_color_hex(0xEAF2FF), 0);
    lv_obj_align(_pathLabel, LV_ALIGN_BOTTOM_LEFT, 18, -18);

    _sdButton = create_action_button(_screen, LV_SYMBOL_DRIVE "  SD Card", 20, 162, 212);
    _spiffsButton = create_action_button(_screen, LV_SYMBOL_HOME "  SPIFFS", 248, 162, 212);

    lv_obj_t *overviewCard = create_card(_screen, 440, 112, 20, 228, lv_color_hex(0xFFFFFF));
    lv_obj_t *overviewIcon = lv_label_create(overviewCard);
    lv_label_set_text(overviewIcon, LV_SYMBOL_DIRECTORY);
    lv_obj_set_style_text_font(overviewIcon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(overviewIcon, lv_color_hex(0x2563EB), 0);
    lv_obj_align(overviewIcon, LV_ALIGN_TOP_LEFT, 18, 18);

    _storageNameLabel = lv_label_create(overviewCard);
    lv_obj_set_style_text_font(_storageNameLabel, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(_storageNameLabel, lv_color_hex(0x102A43), 0);
    lv_obj_align(_storageNameLabel, LV_ALIGN_TOP_LEFT, 66, 16);

    _storageMetaLabel = lv_label_create(overviewCard);
    lv_obj_set_style_text_font(_storageMetaLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_storageMetaLabel, lv_color_hex(0x52606D), 0);
    lv_obj_align(_storageMetaLabel, LV_ALIGN_TOP_LEFT, 66, 48);

    _folderMetaLabel = lv_label_create(overviewCard);
    lv_obj_set_style_text_font(_folderMetaLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_folderMetaLabel, lv_color_hex(0x1F2937), 0);
    lv_obj_align(_folderMetaLabel, LV_ALIGN_BOTTOM_LEFT, 18, -18);

    _upButton = create_action_button(_screen, LV_SYMBOL_LEFT " Up", 20, 356, 102);
    _refreshButton = create_action_button(_screen, LV_SYMBOL_REFRESH " Sync", 132, 356, 102);
    _openButton = create_action_button(_screen, LV_SYMBOL_RIGHT " Open", 244, 356, 102);
    _newFolderButton = create_action_button(_screen, LV_SYMBOL_PLUS " New", 356, 356, 104);
    _renameButton = create_action_button(_screen, LV_SYMBOL_EDIT " Rename", 20, 420, 138);
    _deleteButton = create_action_button(_screen, LV_SYMBOL_TRASH " Delete", 170, 420, 138);
    _infoButton = create_action_button(_screen, LV_SYMBOL_LIST " Details", 320, 420, 140);

    lv_obj_t *listCard = create_card(_screen, 440, 246, 20, 486, lv_color_hex(0xFFFFFF));
    lv_obj_t *listHeader = lv_label_create(listCard);
    lv_label_set_text(listHeader, "This folder");
    lv_obj_set_style_text_font(listHeader, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(listHeader, lv_color_hex(0x102A43), 0);
    lv_obj_align(listHeader, LV_ALIGN_TOP_LEFT, 16, 14);

    _entryList = lv_obj_create(listCard);
    lv_obj_set_size(_entryList, 408, 190);
    lv_obj_set_pos(_entryList, 16, 42);
    lv_obj_set_style_bg_color(_entryList, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_opa(_entryList, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_entryList, 20, 0);
    lv_obj_set_style_border_width(_entryList, 0, 0);
    lv_obj_set_style_pad_all(_entryList, 8, 0);
    lv_obj_set_style_pad_row(_entryList, 8, 0);
    lv_obj_set_layout(_entryList, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_entryList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(_entryList, LV_SCROLLBAR_MODE_OFF);

    lv_obj_add_event_cb(_sdButton, onStorageClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_spiffsButton, onStorageClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_upButton, onUpClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_refreshButton, onRefreshClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_openButton, onOpenClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_newFolderButton, onNewFolderClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_renameButton, onRenameClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_deleteButton, onDeleteClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_infoButton, onInfoClicked, LV_EVENT_CLICKED, this);

    _emptyLabel = lv_label_create(_entryList);
    lv_label_set_text(_emptyLabel, "No files in this folder yet");
    lv_obj_set_style_text_font(_emptyLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_emptyLabel, lv_color_hex(0x7D8996), 0);
    lv_obj_set_width(_emptyLabel, 360);
    lv_obj_set_style_text_align(_emptyLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(_emptyLabel, LV_OBJ_FLAG_HIDDEN);

    _inputPanel = lv_obj_create(_screen);
    lv_obj_set_size(_inputPanel, 430, 240);
    lv_obj_align(_inputPanel, LV_ALIGN_CENTER, 0, -60);
    lv_obj_set_style_radius(_inputPanel, 24, 0);
    lv_obj_set_style_bg_color(_inputPanel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(_inputPanel, 0, 0);
    lv_obj_set_style_shadow_width(_inputPanel, 24, 0);
    lv_obj_set_style_shadow_opa(_inputPanel, LV_OPA_20, 0);
    lv_obj_add_flag(_inputPanel, LV_OBJ_FLAG_HIDDEN);

    _inputTitleLabel = lv_label_create(_inputPanel);
    lv_obj_set_style_text_font(_inputTitleLabel, &lv_font_montserrat_22, 0);
    lv_obj_align(_inputTitleLabel, LV_ALIGN_TOP_LEFT, 16, 14);

    _inputTextArea = lv_textarea_create(_inputPanel);
    lv_obj_set_size(_inputTextArea, 394, 58);
    lv_obj_align(_inputTextArea, LV_ALIGN_TOP_LEFT, 16, 52);
    lv_textarea_set_one_line(_inputTextArea, true);
    lv_textarea_set_placeholder_text(_inputTextArea, "Enter name");

    lv_obj_t *cancelButton = create_action_button(_inputPanel, "Cancel", 16, 140, 130);
    lv_obj_t *confirmButton = create_action_button(_inputPanel, LV_SYMBOL_OK " Confirm", 284, 140, 130);
    lv_obj_add_event_cb(cancelButton, onInputCancelClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(confirmButton, onInputConfirmClicked, LV_EVENT_CLICKED, this);

    _keyboard = lv_keyboard_create(_screen);
    lv_obj_set_size(_keyboard, 480, 260);
    lv_obj_align(_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(_keyboard, _inputTextArea);
    lv_obj_add_event_cb(_keyboard, onKeyboardDone, LV_EVENT_READY, this);
    lv_obj_add_event_cb(_keyboard, onKeyboardDone, LV_EVENT_CANCEL, this);
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);

    _statusLabel = lv_label_create(_screen);
    lv_obj_set_width(_statusLabel, 440);
    lv_label_set_long_mode(_statusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_statusLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_statusLabel, lv_color_hex(0x2A7D46), 0);
    lv_obj_align(_statusLabel, LV_ALIGN_BOTTOM_LEFT, 20, -14);

    updateActionButtons();
    updateOverviewCard();
    setStatus("Ready");
    return true;
}

bool FileManager::refreshEntries()
{
    if (_entryList == nullptr) {
        return false;
    }

    _entries.clear();
    _selectedEntry = nullptr;
    lv_obj_clean(_entryList);
    _emptyLabel = nullptr;

    DIR *dir = opendir(_currentPath.c_str());
    if (dir == nullptr) {
        setStatus(std::string("Cannot open ") + _currentPath + ": " + strerror(errno), true);
        _emptyLabel = lv_label_create(_entryList);
        lv_label_set_text(_emptyLabel, "Unable to read this folder");
        lv_obj_set_width(_emptyLabel, 360);
        lv_obj_set_style_text_align(_emptyLabel, LV_TEXT_ALIGN_CENTER, 0);
        updatePathLabel();
        updateOverviewCard();
        updateActionButtons();
        return false;
    }

    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }

        EntryInfo info = {};
        info.name = entry->d_name;
        info.path = joinPath(_currentPath, info.name);

        struct stat st = {};
        if (stat(info.path.c_str(), &st) == 0) {
            info.isDirectory = S_ISDIR(st.st_mode);
            info.size = static_cast<uint64_t>(st.st_size);
        }

        info.button = nullptr;
        _entries.push_back(info);
    }
    closedir(dir);

    std::sort(_entries.begin(), _entries.end(), [](const EntryInfo &left, const EntryInfo &right) {
        if (left.isDirectory != right.isDirectory) {
            return left.isDirectory > right.isDirectory;
        }
        return left.name < right.name;
    });

    if (_entries.empty()) {
        _emptyLabel = lv_label_create(_entryList);
        lv_label_set_text(_emptyLabel, "No files in this folder yet");
        lv_obj_set_width(_emptyLabel, 360);
        lv_obj_set_style_text_font(_emptyLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_emptyLabel, lv_color_hex(0x7D8996), 0);
        lv_obj_set_style_text_align(_emptyLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(_emptyLabel, 56, 0);
    } else {
        for (size_t index = 0; index < _entries.size(); ++index) {
            EntryInfo &info = _entries[index];
            info.button = lv_btn_create(_entryList);
            lv_obj_set_width(info.button, lv_pct(100));
            lv_obj_set_height(info.button, 72);
            lv_obj_set_style_radius(info.button, 18, 0);
            lv_obj_set_style_bg_color(info.button, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(info.button, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(info.button, 1, 0);
            lv_obj_set_style_border_color(info.button, lv_color_hex(0xE2E8F0), 0);
            lv_obj_set_style_shadow_width(info.button, 0, 0);
            lv_obj_clear_flag(info.button, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *iconLabel = lv_label_create(info.button);
            lv_label_set_text(iconLabel, info.isDirectory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
            lv_obj_set_style_text_font(iconLabel, &lv_font_montserrat_22, 0);
            lv_obj_set_style_text_color(iconLabel, info.isDirectory ? lv_color_hex(0x2563EB) : lv_color_hex(0x64748B), 0);
            lv_obj_align(iconLabel, LV_ALIGN_LEFT_MID, 12, -10);

            lv_obj_t *nameLabel = lv_label_create(info.button);
            lv_label_set_text(nameLabel, info.name.c_str());
            lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_DOT);
            lv_obj_set_width(nameLabel, 250);
            lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(nameLabel, lv_color_hex(0x102A43), 0);
            lv_obj_align(nameLabel, LV_ALIGN_LEFT_MID, 52, -14);

            lv_obj_t *metaLabel = lv_label_create(info.button);
            const std::string meta = info.isDirectory ? std::string("Folder") : formatSize(info.size);
            lv_label_set_text(metaLabel, meta.c_str());
            lv_obj_set_style_text_font(metaLabel, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(metaLabel, lv_color_hex(0x52606D), 0);
            lv_obj_align(metaLabel, LV_ALIGN_LEFT_MID, 52, 12);

            lv_obj_t *chevronLabel = lv_label_create(info.button);
            lv_label_set_text(chevronLabel, LV_SYMBOL_RIGHT);
            lv_obj_set_style_text_font(chevronLabel, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(chevronLabel, lv_color_hex(0x94A3B8), 0);
            lv_obj_align(chevronLabel, LV_ALIGN_RIGHT_MID, -14, 0);

            lv_obj_add_event_cb(info.button, onEntryClicked, LV_EVENT_CLICKED, this);
        }
    }

    updatePathLabel();
    updateOverviewCard();
    updateActionButtons();
    setStatus(std::string("Loaded ") + std::to_string(_entries.size()) + " entries");
    return true;
}

bool FileManager::switchStorage(StorageRoot root)
{
    _activeRoot = root;
    _currentPath = rootPath(root);
    updateStorageButtons();

    if (!rootAvailable(root)) {
        lv_obj_clean(_entryList);
        _emptyLabel = nullptr;
        _entries.clear();
        _selectedEntry = nullptr;
        updateActionButtons();
        updatePathLabel();
        updateOverviewCard();
        setStatus(rootLabel(root) + " is not available", true);
        _emptyLabel = lv_label_create(_entryList);
        lv_label_set_text_fmt(_emptyLabel, "%s is not mounted", rootLabel(root).c_str());
        lv_obj_set_width(_emptyLabel, 360);
        lv_obj_set_style_text_align(_emptyLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(_emptyLabel, 56, 0);
        return false;
    }

    return refreshEntries();
}

bool FileManager::navigateTo(const std::string &path)
{
    if (!is_path_directory(path)) {
        setStatus("Selected path is not a folder", true);
        return false;
    }

    _currentPath = path;
    return refreshEntries();
}

bool FileManager::navigateUp()
{
    const std::string root = rootPath(_activeRoot);
    if (_currentPath == root) {
        return true;
    }

    const size_t split = _currentPath.find_last_of('/');
    if ((split == std::string::npos) || (split <= root.size())) {
        _currentPath = root;
    } else {
        _currentPath = _currentPath.substr(0, split);
    }

    return refreshEntries();
}

bool FileManager::openSelectedEntry()
{
    if (_selectedEntry == nullptr) {
        setStatus("Select an entry first", true);
        return false;
    }

    if (_selectedEntry->isDirectory) {
        return navigateTo(_selectedEntry->path);
    }

    return showSelectedEntryInfo();
}

bool FileManager::createFolder(const std::string &name)
{
    if (name.empty()) {
        setStatus("Folder name cannot be empty", true);
        return false;
    }
    if ((name.find('/') != std::string::npos) || (name == ".") || (name == "..")) {
        setStatus("Invalid folder name", true);
        return false;
    }

    const std::string path = joinPath(_currentPath, name);
    if (mkdir(path.c_str(), 0775) != 0) {
        setStatus(std::string("Create folder failed: ") + strerror(errno), true);
        return false;
    }

    setStatus(std::string("Created folder ") + name);
    return true;
}

bool FileManager::renameSelectedEntry(const std::string &newName)
{
    if (_selectedEntry == nullptr) {
        setStatus("Select an entry first", true);
        return false;
    }
    if (newName.empty()) {
        setStatus("Name cannot be empty", true);
        return false;
    }
    if ((newName.find('/') != std::string::npos) || (newName == ".") || (newName == "..")) {
        setStatus("Invalid name", true);
        return false;
    }

    const std::string newPath = joinPath(_currentPath, newName);
    if (rename(_selectedEntry->path.c_str(), newPath.c_str()) != 0) {
        setStatus(std::string("Rename failed: ") + strerror(errno), true);
        return false;
    }

    setStatus(std::string("Renamed to ") + newName);
    return true;
}

bool FileManager::deleteSelectedEntry()
{
    if (_selectedEntry == nullptr) {
        setStatus("Select an entry first", true);
        return false;
    }

    const std::string name = _selectedEntry->name;
    if (!removePathRecursively(_selectedEntry->path)) {
        setStatus(std::string("Delete failed: ") + strerror(errno), true);
        return false;
    }

    setStatus(std::string("Deleted ") + name);
    return true;
}

bool FileManager::showSelectedEntryInfo()
{
    if (_selectedEntry == nullptr) {
        setStatus("Select an entry first", true);
        return false;
    }

    std::string message = std::string("Name: ") + _selectedEntry->name + "\n";
    message += "Type: ";
    message += _selectedEntry->isDirectory ? "Folder\n" : "File\n";
    message += std::string("Path: ") + _selectedEntry->path + "\n";
    if (!_selectedEntry->isDirectory) {
        message += std::string("Size: ") + formatSize(_selectedEntry->size);
    }

    static const char *buttons[] = {"Close", ""};
    _dialogMessageBox = lv_msgbox_create(NULL, "Entry Info", message.c_str(), buttons, false);
    lv_obj_center(_dialogMessageBox);
    lv_obj_add_event_cb(_dialogMessageBox, onInfoMessageBoxEvent, LV_EVENT_VALUE_CHANGED, this);
    return true;
}

bool FileManager::rootAvailable(StorageRoot root) const
{
    return is_path_directory(rootPath(root));
}

std::string FileManager::rootPath(StorageRoot root) const
{
    return (root == StorageRoot::SdCard) ? kSdRoot : kSpiffsRoot;
}

std::string FileManager::rootLabel(StorageRoot root) const
{
    return (root == StorageRoot::SdCard) ? "SD Card" : "SPIFFS";
}

std::string FileManager::joinPath(const std::string &base, const std::string &name) const
{
    if (base.empty() || (base == "/")) {
        return "/" + name;
    }
    if (base.back() == '/') {
        return base + name;
    }
    return base + "/" + name;
}

std::string FileManager::formatSize(uint64_t size) const
{
    char buffer[32] = {};
    if (size >= (1024ULL * 1024ULL)) {
        snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(size) / (1024.0 * 1024.0));
    } else if (size >= 1024ULL) {
        snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(size) / 1024.0);
    } else {
        snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(size));
    }
    return buffer;
}

std::string FileManager::formatEntrySummary() const
{
    size_t directoryCount = 0;
    size_t fileCount = 0;

    for (const EntryInfo &entry : _entries) {
        if (entry.isDirectory) {
            ++directoryCount;
        } else {
            ++fileCount;
        }
    }

    return std::to_string(directoryCount) + " folders  •  " + std::to_string(fileCount) + " files";
}

void FileManager::setStatus(const std::string &message, bool isError)
{
    if (_statusLabel == nullptr) {
        return;
    }

    lv_label_set_text(_statusLabel, message.c_str());
    lv_obj_set_style_text_color(_statusLabel, isError ? lv_color_hex(0xB42318) : lv_color_hex(0x2A7D46), 0);
}

void FileManager::updatePathLabel()
{
    if (_pathLabel != nullptr) {
        lv_label_set_text_fmt(_pathLabel, "%s  •  %s", rootLabel(_activeRoot).c_str(), _currentPath.c_str());
    }
}

void FileManager::updateOverviewCard()
{
    if ((_storageNameLabel == nullptr) || (_storageMetaLabel == nullptr) || (_folderMetaLabel == nullptr)) {
        return;
    }

    const bool available = rootAvailable(_activeRoot);
    lv_label_set_text(_storageNameLabel, rootLabel(_activeRoot).c_str());
    lv_label_set_text_fmt(_storageMetaLabel, "%s root: %s", available ? "Mounted" : "Offline", rootPath(_activeRoot).c_str());
    lv_label_set_text(_folderMetaLabel, formatEntrySummary().c_str());
}

void FileManager::updateStorageButtons()
{
    const bool sdActive = (_activeRoot == StorageRoot::SdCard);
    lv_obj_set_style_bg_color(_sdButton, sdActive ? lv_color_hex(0x2563EB) : lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(_spiffsButton, sdActive ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_border_color(_sdButton, sdActive ? lv_color_hex(0x2563EB) : lv_color_hex(0xD6DEEA), 0);
    lv_obj_set_style_border_color(_spiffsButton, sdActive ? lv_color_hex(0xD6DEEA) : lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_text_color(_sdButton, sdActive ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x122033), 0);
    lv_obj_set_style_text_color(_spiffsButton, sdActive ? lv_color_hex(0x122033) : lv_color_hex(0xFFFFFF), 0);
}

void FileManager::updateActionButtons()
{
    const bool hasSelection = (_selectedEntry != nullptr);
    set_button_enabled(_openButton, hasSelection);
    set_button_enabled(_renameButton, hasSelection);
    set_button_enabled(_deleteButton, hasSelection);
    set_button_enabled(_infoButton, hasSelection);
    set_button_enabled(_upButton, _currentPath != rootPath(_activeRoot));
}

void FileManager::selectEntry(EntryInfo *entry)
{
    if ((_selectedEntry != nullptr) && (_selectedEntry->button != nullptr)) {
        lv_obj_set_style_bg_color(_selectedEntry->button, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(_selectedEntry->button, lv_color_hex(0xE2E8F0), 0);
    }

    const bool selectedSameEntry = (_selectedEntry == entry) && (entry != nullptr);
    _selectedEntry = entry;

    if ((_selectedEntry != nullptr) && (_selectedEntry->button != nullptr)) {
        lv_obj_set_style_bg_color(_selectedEntry->button, lv_color_hex(0xDBEAFE), 0);
        lv_obj_set_style_border_color(_selectedEntry->button, lv_color_hex(0x60A5FA), 0);
        setStatus(std::string("Selected ") + _selectedEntry->name);
    } else if (!selectedSameEntry) {
        setStatus("Ready");
    }

    updateActionButtons();
}

void FileManager::showInputDialog(InputMode mode, const char *title, const std::string &initialValue)
{
    _inputMode = mode;
    lv_label_set_text(_inputTitleLabel, title);
    lv_textarea_set_text(_inputTextArea, initialValue.c_str());
    lv_obj_clear_flag(_inputPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(_keyboard, _inputTextArea);
    lv_obj_move_foreground(_inputPanel);
    lv_obj_move_foreground(_keyboard);
    lv_obj_add_state(_inputTextArea, LV_STATE_FOCUSED);
}

void FileManager::hideInputDialog()
{
    _inputMode = InputMode::None;
    if (_inputPanel != nullptr) {
        lv_obj_add_flag(_inputPanel, LV_OBJ_FLAG_HIDDEN);
    }
    if (_keyboard != nullptr) {
        lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

void FileManager::showDeleteConfirm()
{
    if (_selectedEntry == nullptr) {
        setStatus("Select an entry first", true);
        return;
    }

    static const char *buttons[] = {"Delete", "Cancel", ""};
    std::string message = std::string("Delete ") + _selectedEntry->name + "?";
    _dialogMessageBox = lv_msgbox_create(NULL, "Confirm Delete", message.c_str(), buttons, false);
    lv_obj_center(_dialogMessageBox);
    lv_obj_add_event_cb(_dialogMessageBox, onDeleteMessageBoxEvent, LV_EVENT_VALUE_CHANGED, this);
}

bool FileManager::removePathRecursively(const std::string &path)
{
    struct stat st = {};
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path.c_str());
        if (dir == nullptr) {
            return false;
        }

        struct dirent *entry = nullptr;
        while ((entry = readdir(dir)) != nullptr) {
            if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
                continue;
            }

            if (!removePathRecursively(path + "/" + entry->d_name)) {
                closedir(dir);
                return false;
            }
        }
        closedir(dir);
        return rmdir(path.c_str()) == 0;
    }

    return remove(path.c_str()) == 0;
}

void FileManager::onEntryClicked(lv_event_t *event)
{
    auto *app = static_cast<FileManager *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(event);
    for (EntryInfo &entry : app->_entries) {
        if (entry.button == target) {
            if (app->_selectedEntry == &entry) {
                app->openSelectedEntry();
                return;
            }
            app->selectEntry(&entry);
            return;
        }
    }
}

void FileManager::onUpClicked(lv_event_t *event)
{
    auto *app = static_cast<FileManager *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->navigateUp();
    }
}

void FileManager::onRefreshClicked(lv_event_t *event)
{
    auto *app = static_cast<FileManager *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->refreshEntries();
    }
}

void FileManager::onStorageClicked(lv_event_t *event)
{
    auto *app = static_cast<FileManager *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }

    if (lv_event_get_target(event) == app->_sdButton) {
        app->switchStorage(StorageRoot::SdCard);
    } else if (lv_event_get_target(event) == app->_spiffsButton) {
        app->switchStorage(StorageRoot::Spiffs);
    }
}

void FileManager::onOpenClicked(lv_event_t *event)
{
    auto *app = static_cast<FileManager *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->openSelectedEntry();
    }
}

void FileManager::onNewFolderClicked(lv_event_t *event)
{
    auto *app = static_cast<FileManager *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->showInputDialog(InputMode::CreateFolder, "Create Folder", "");
    }
}

void FileManager::onRenameClicked(lv_event_t *event)
{
    auto *app = static_cast<FileManager *>(lv_event_get_user_data(event));
    if ((app != nullptr) && (app->_selectedEntry != nullptr)) {
        app->showInputDialog(InputMode::RenameEntry, "Rename Entry", app->_selectedEntry->name);
    }
}

void FileManager::onDeleteClicked(lv_event_t *event)
{
    auto *app = static_cast<FileManager *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->showDeleteConfirm();
    }
}

void FileManager::onInfoClicked(lv_event_t *event)
{
    auto *app = static_cast<FileManager *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->showSelectedEntryInfo();
    }
}

void FileManager::onInputConfirmClicked(lv_event_t *event)
{
    auto *app = static_cast<FileManager *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }

    const std::string value = lv_textarea_get_text(app->_inputTextArea);
    bool success = false;
    if (app->_inputMode == InputMode::CreateFolder) {
        success = app->createFolder(value);
    } else if (app->_inputMode == InputMode::RenameEntry) {
        success = app->renameSelectedEntry(value);
    }

    if (success) {
        app->hideInputDialog();
        app->refreshEntries();
    }
}

void FileManager::onInputCancelClicked(lv_event_t *event)
{
    auto *app = static_cast<FileManager *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->hideInputDialog();
    }
}

void FileManager::onKeyboardDone(lv_event_t *event)
{
    auto *app = static_cast<FileManager *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->hideInputDialog();
    }
}

void FileManager::onInfoMessageBoxEvent(lv_event_t *event)
{
    auto *app = static_cast<FileManager *>(lv_event_get_user_data(event));
    lv_obj_t *msgbox = lv_event_get_target(event);
    if ((app == nullptr) || (msgbox == nullptr)) {
        return;
    }

    lv_msgbox_close(msgbox);
    app->_dialogMessageBox = nullptr;
}

void FileManager::onDeleteMessageBoxEvent(lv_event_t *event)
{
    auto *app = static_cast<FileManager *>(lv_event_get_user_data(event));
    lv_obj_t *msgbox = lv_event_get_target(event);
    if ((app == nullptr) || (msgbox == nullptr)) {
        return;
    }

    const char *buttonText = lv_msgbox_get_active_btn_text(msgbox);
    if ((buttonText != nullptr) && (strcmp(buttonText, "Delete") == 0)) {
        if (app->deleteSelectedEntry()) {
            app->refreshEntries();
        }
    }

    lv_msgbox_close(msgbox);
    app->_dialogMessageBox = nullptr;
}
