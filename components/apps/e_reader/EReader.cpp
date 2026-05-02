#include "EReader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string_view>
#include <sys/stat.h>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "storage_access.h"

LV_IMG_DECLARE(img_app_ereader);

namespace {

static constexpr const char *TAG = "EReaderApp";
static constexpr const char *kSdRoot = "/sdcard";
static constexpr size_t kMaxBookBytes = 128 * 1024;

bool is_directory_path(const std::string &path)
{
    struct stat info = {};
    return (stat(path.c_str(), &info) == 0) && S_ISDIR(info.st_mode);
}

std::string to_lower_copy(const std::string &value)
{
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lower;
}

std::string extension_of(const std::string &path)
{
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return {};
    }
    return to_lower_copy(path.substr(dot));
}

bool is_supported_book_extension(const std::string &path)
{
    static const char *kExtensions[] = {
        ".txt", ".md",  ".log",  ".ini", ".cfg",  ".csv",  ".json", ".yaml", ".yml",
        ".xml", ".html", ".htm", ".fb2", ".rtf",  ".c",    ".cpp",  ".h",    ".hpp",
    };
    const std::string extension = extension_of(path);
    for (const char *candidate: kExtensions) {
        if (extension == candidate) {
            return true;
        }
    }
    return false;
}

std::string trim_copy(const std::string &value)
{
    size_t start = 0;
    while ((start < value.size()) && (std::isspace(static_cast<unsigned char>(value[start])) != 0)) {
        ++start;
    }
    size_t end = value.size();
    while ((end > start) && (std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string collapse_whitespace(const std::string &value)
{
    std::string output;
    output.reserve(value.size());
    bool previous_space = false;
    for (char ch: value) {
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            if (!output.empty() && (output.back() != '\n')) {
                output.push_back('\n');
            }
            previous_space = false;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            if (!previous_space) {
                output.push_back(' ');
                previous_space = true;
            }
            continue;
        }
        previous_space = false;
        output.push_back(ch);
    }
    return trim_copy(output);
}

std::string strip_markup_tags(const std::string &input)
{
    std::string output;
    output.reserve(input.size());
    bool in_tag = false;
    for (size_t index = 0; index < input.size(); ++index) {
        const char ch = input[index];
        if (ch == '<') {
            in_tag = true;
            continue;
        }
        if (ch == '>') {
            in_tag = false;
            output.push_back('\n');
            continue;
        }
        if (!in_tag) {
            output.push_back(ch);
        }
    }
    return collapse_whitespace(output);
}

std::string strip_rtf_controls(const std::string &input)
{
    std::string output;
    output.reserve(input.size());
    bool control = false;
    for (size_t index = 0; index < input.size(); ++index) {
        const char ch = input[index];
        if (ch == '\\') {
            control = true;
            continue;
        }
        if (control) {
            if ((ch == ' ') || (ch == '\n') || (ch == '\r')) {
                control = false;
            }
            continue;
        }
        if ((ch == '{') || (ch == '}')) {
            continue;
        }
        output.push_back(ch);
    }
    return collapse_whitespace(output);
}

std::string normalize_book_text(const std::string &path, const std::string &text)
{
    const std::string extension = extension_of(path);
    if ((extension == ".html") || (extension == ".htm") || (extension == ".xml") || (extension == ".fb2")) {
        return strip_markup_tags(text);
    }
    if (extension == ".rtf") {
        return strip_rtf_controls(text);
    }
    return text;
}

bool load_text_file(const std::string &path, std::string &text, bool *truncated)
{
    text.clear();
    if (truncated != nullptr) {
        *truncated = false;
    }

    FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }

    char buffer[2048] = {};
    while (text.size() < kMaxBookBytes) {
        const size_t to_read = std::min(sizeof(buffer), kMaxBookBytes - text.size());
        const size_t bytes_read = std::fread(buffer, 1, to_read, file);
        if (bytes_read == 0) {
            break;
        }
        text.append(buffer, bytes_read);
    }

    if (!std::feof(file) && (truncated != nullptr)) {
        *truncated = true;
    }
    std::fclose(file);
    text = normalize_book_text(path, text);
    return true;
}

} // namespace

EReaderApp::EReaderApp():
    ESP_Brookesia_PhoneApp("E-Reader", &img_app_ereader, true),
    _screen(nullptr),
    _statusLabel(nullptr),
    _pathLabel(nullptr),
    _entryList(nullptr),
    _emptyLabel(nullptr),
    _readerPanel(nullptr),
    _readerTitle(nullptr),
    _readerBody(nullptr),
    _sdButton(nullptr),
    _spiffsButton(nullptr),
    _rootPath(BSP_SPIFFS_MOUNT_POINT),
    _currentPath(BSP_SPIFFS_MOUNT_POINT),
    _usingSdCard(false)
{
}

bool EReaderApp::init()
{
    _usingSdCard = false;
    _rootPath = BSP_SPIFFS_MOUNT_POINT;
    _currentPath = _rootPath;
    return true;
}

bool EReaderApp::run()
{
    if (!ensureUiReady()) {
        return false;
    }
    showList();
    refreshEntries();
    return true;
}

bool EReaderApp::pause()
{
    return true;
}

bool EReaderApp::resume()
{
    if (!ensureUiReady()) {
        return false;
    }
    refreshEntries();
    return true;
}

bool EReaderApp::back()
{
    if ((_readerPanel != nullptr) && !lv_obj_has_flag(_readerPanel, LV_OBJ_FLAG_HIDDEN)) {
        showList();
        return true;
    }
    if (_currentPath != _rootPath) {
        return navigateUp();
    }
    return notifyCoreClosed();
}

bool EReaderApp::close()
{
    _entries.clear();
    _buttonIndexMap.clear();
    return true;
}

bool EReaderApp::buildUi()
{
    _screen = lv_scr_act();
    if (_screen == nullptr) {
        return false;
    }

    lv_obj_set_style_bg_color(_screen, lv_color_hex(0xFFFDF5), 0);
    lv_obj_set_style_bg_grad_color(_screen, lv_color_hex(0xF3E8D1), 0);
    lv_obj_set_style_bg_grad_dir(_screen, LV_GRAD_DIR_VER, 0);

    lv_obj_t *title = lv_label_create(_screen);
    lv_label_set_text(title, "E-Reader");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x3F2D1D), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 18, 16);

    _statusLabel = lv_label_create(_screen);
    lv_obj_set_width(_statusLabel, 444);
    lv_label_set_long_mode(_statusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_statusLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_statusLabel, lv_color_hex(0x6B4F35), 0);
    lv_obj_align(_statusLabel, LV_ALIGN_TOP_LEFT, 18, 56);

    _pathLabel = lv_label_create(_screen);
    lv_obj_set_width(_pathLabel, 444);
    lv_label_set_long_mode(_pathLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_pathLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_pathLabel, lv_color_hex(0x7C6A58), 0);
    lv_obj_align(_pathLabel, LV_ALIGN_TOP_LEFT, 18, 90);

    _sdButton = lv_btn_create(_screen);
    lv_obj_set_size(_sdButton, 96, 42);
    lv_obj_align(_sdButton, LV_ALIGN_TOP_LEFT, 18, 118);
    lv_obj_add_event_cb(_sdButton, onSdClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *sdLabel = lv_label_create(_sdButton);
    lv_label_set_text(sdLabel, "SD Card");
    lv_obj_center(sdLabel);

    _spiffsButton = lv_btn_create(_screen);
    lv_obj_set_size(_spiffsButton, 96, 42);
    lv_obj_align(_spiffsButton, LV_ALIGN_TOP_LEFT, 122, 118);
    lv_obj_add_event_cb(_spiffsButton, onSpiffsClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *spiffsLabel = lv_label_create(_spiffsButton);
    lv_label_set_text(spiffsLabel, "SPIFFS");
    lv_obj_center(spiffsLabel);

    lv_obj_t *upButton = lv_btn_create(_screen);
    lv_obj_set_size(upButton, 72, 42);
    lv_obj_align(upButton, LV_ALIGN_TOP_RIGHT, -98, 118);
    lv_obj_add_event_cb(upButton, onUpClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *upLabel = lv_label_create(upButton);
    lv_label_set_text(upLabel, LV_SYMBOL_UP " Up");
    lv_obj_center(upLabel);

    lv_obj_t *refreshButton = lv_btn_create(_screen);
    lv_obj_set_size(refreshButton, 72, 42);
    lv_obj_align(refreshButton, LV_ALIGN_TOP_RIGHT, -18, 118);
    lv_obj_add_event_cb(refreshButton, onRefreshClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *refreshLabel = lv_label_create(refreshButton);
    lv_label_set_text(refreshLabel, LV_SYMBOL_REFRESH);
    lv_obj_center(refreshLabel);

    _entryList = lv_list_create(_screen);
    lv_obj_set_size(_entryList, 444, 368);
    lv_obj_align(_entryList, LV_ALIGN_TOP_LEFT, 18, 170);

    _emptyLabel = lv_label_create(_entryList);
    lv_obj_set_width(_emptyLabel, 400);
    lv_obj_set_style_text_align(_emptyLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_emptyLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_emptyLabel, "No readable files found yet.");
    lv_obj_center(_emptyLabel);

    _readerPanel = lv_obj_create(_screen);
    lv_obj_set_size(_readerPanel, 444, 508);
    lv_obj_align(_readerPanel, LV_ALIGN_TOP_LEFT, 18, 30);
    lv_obj_set_style_radius(_readerPanel, 20, 0);
    lv_obj_set_style_border_width(_readerPanel, 0, 0);
    lv_obj_set_style_pad_all(_readerPanel, 12, 0);
    lv_obj_add_flag(_readerPanel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *closeReaderButton = lv_btn_create(_readerPanel);
    lv_obj_set_size(closeReaderButton, 112, 40);
    lv_obj_align(closeReaderButton, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_event_cb(closeReaderButton, onCloseReaderClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *closeReaderLabel = lv_label_create(closeReaderButton);
    lv_label_set_text(closeReaderLabel, LV_SYMBOL_LEFT " Library");
    lv_obj_center(closeReaderLabel);

    _readerTitle = lv_label_create(_readerPanel);
    lv_obj_set_width(_readerTitle, 300);
    lv_label_set_long_mode(_readerTitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_readerTitle, &lv_font_montserrat_20, 0);
    lv_obj_align(_readerTitle, LV_ALIGN_TOP_LEFT, 126, 0);

    _readerBody = lv_textarea_create(_readerPanel);
    lv_obj_set_size(_readerBody, 420, 438);
    lv_obj_align(_readerBody, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_textarea_set_one_line(_readerBody, false);
    lv_obj_clear_flag(_readerBody, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_text_font(_readerBody, &lv_font_montserrat_16, 0);

    lv_obj_add_event_cb(_screen, onScreenDeleted, LV_EVENT_DELETE, this);
    return true;
}

bool EReaderApp::ensureUiReady()
{
    if (hasLiveScreen()) {
        return true;
    }
    resetUiPointers();
    return buildUi();
}

bool EReaderApp::hasLiveScreen() const
{
    return (_screen != nullptr) && lv_obj_is_valid(_screen) && (_entryList != nullptr) && lv_obj_is_valid(_entryList);
}

bool EReaderApp::refreshEntries()
{
    _entries.clear();
    _buttonIndexMap.clear();

    DIR *directory = opendir(_currentPath.c_str());
    if (directory == nullptr) {
        setStatus(std::string("Cannot open ") + _currentPath);
        renderEntries();
        return false;
    }

    struct dirent *entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        const std::string name = entry->d_name;
        if ((name == ".") || (name == "..")) {
            continue;
        }
        const std::string path = _currentPath + "/" + name;
        const bool is_dir = is_directory_path(path);
        if (!is_dir && !is_supported_book_extension(path)) {
            continue;
        }
        _entries.push_back({name, path, is_dir});
    }
    closedir(directory);

    std::sort(_entries.begin(), _entries.end(), [](const Entry &left, const Entry &right) {
        if (left.is_directory != right.is_directory) {
            return left.is_directory > right.is_directory;
        }
        return to_lower_copy(left.name) < to_lower_copy(right.name);
    });

    updatePathLabel();
    renderEntries();
    if (_entries.empty()) {
        setStatus("No readable books here. Supports TXT, MD, HTML, XML, FB2, RTF, JSON, CSV and similar text formats.");
    } else {
        setStatus(std::string("Found ") + std::to_string(_entries.size()) + " folders/books");
    }
    return true;
}

bool EReaderApp::navigateTo(const std::string &path)
{
    _currentPath = path;
    return refreshEntries();
}

bool EReaderApp::navigateUp()
{
    if (_currentPath == _rootPath) {
        return true;
    }
    const size_t slash = _currentPath.find_last_of('/');
    if ((slash == std::string::npos) || (slash < _rootPath.size())) {
        _currentPath = _rootPath;
    } else {
        _currentPath = _currentPath.substr(0, slash);
    }
    return refreshEntries();
}

bool EReaderApp::openEntry(size_t index)
{
    if (index >= _entries.size()) {
        return false;
    }
    const Entry &entry = _entries[index];
    if (entry.is_directory) {
        return navigateTo(entry.path);
    }

    std::string content;
    bool truncated = false;
    if (!load_text_file(entry.path, content, &truncated)) {
        setStatus(std::string("Failed to read ") + entry.name);
        return false;
    }
    if (content.empty()) {
        content = "This file is empty or could not be normalized into readable text.";
    }
    if (truncated) {
        content += "\n\n[Preview truncated at 128 KB for device memory safety]";
    }
    showReader(entry.name, content);
    setStatus(std::string("Reading ") + entry.name);
    return true;
}

bool EReaderApp::switchRoot(bool use_sdcard)
{
    if (use_sdcard) {
        if (!app_storage_ensure_sdcard_available()) {
            setStatus("Insert or mount the SD card first.");
            return false;
        }
        _usingSdCard = true;
        _rootPath = kSdRoot;
    } else {
        _usingSdCard = false;
        _rootPath = BSP_SPIFFS_MOUNT_POINT;
    }
    _currentPath = _rootPath;
    return refreshEntries();
}

void EReaderApp::renderEntries()
{
    if ((_entryList == nullptr) || !lv_obj_is_valid(_entryList)) {
        return;
    }

    lv_obj_clean(_entryList);
    _buttonIndexMap.clear();
    if (_entries.empty()) {
        _emptyLabel = lv_label_create(_entryList);
        lv_obj_set_width(_emptyLabel, 400);
        lv_obj_set_style_text_align(_emptyLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(_emptyLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(_emptyLabel,
                          _usingSdCard ? "Add readable book files under /sdcard or its folders."
                                       : "Add readable book files under /spiffs.");
        lv_obj_center(_emptyLabel);
        return;
    }

    for (size_t index = 0; index < _entries.size(); ++index) {
        const Entry &entry = _entries[index];
        const char *symbol = entry.is_directory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE;
        lv_obj_t *button = lv_list_add_btn(_entryList, symbol, entry.name.c_str());
        lv_obj_add_event_cb(button, onEntryClicked, LV_EVENT_CLICKED, this);
        _buttonIndexMap[button] = index;
    }
}

void EReaderApp::showReader(const std::string &title, const std::string &content)
{
    lv_label_set_text(_readerTitle, title.c_str());
    lv_textarea_set_text(_readerBody, content.c_str());
    lv_obj_add_flag(_entryList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_readerPanel, LV_OBJ_FLAG_HIDDEN);
}

void EReaderApp::showList()
{
    if (_entryList != nullptr) {
        lv_obj_clear_flag(_entryList, LV_OBJ_FLAG_HIDDEN);
    }
    if (_readerPanel != nullptr) {
        lv_obj_add_flag(_readerPanel, LV_OBJ_FLAG_HIDDEN);
    }
}

void EReaderApp::setStatus(const std::string &status)
{
    if ((_statusLabel != nullptr) && lv_obj_is_valid(_statusLabel)) {
        lv_label_set_text(_statusLabel, status.c_str());
    }
}

void EReaderApp::updatePathLabel()
{
    if ((_pathLabel != nullptr) && lv_obj_is_valid(_pathLabel)) {
        lv_label_set_text(_pathLabel, _currentPath.c_str());
    }

    if (_sdButton != nullptr) {
        if (_usingSdCard) {
            lv_obj_add_state(_sdButton, LV_STATE_CHECKED);
            lv_obj_clear_state(_spiffsButton, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(_spiffsButton, LV_STATE_CHECKED);
            lv_obj_clear_state(_sdButton, LV_STATE_CHECKED);
        }
    }
}

void EReaderApp::resetUiPointers()
{
    _screen = nullptr;
    _statusLabel = nullptr;
    _pathLabel = nullptr;
    _entryList = nullptr;
    _emptyLabel = nullptr;
    _readerPanel = nullptr;
    _readerTitle = nullptr;
    _readerBody = nullptr;
    _sdButton = nullptr;
    _spiffsButton = nullptr;
}

void EReaderApp::onScreenDeleted(lv_event_t *event)
{
    auto *app = static_cast<EReaderApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->resetUiPointers();
    }
}

void EReaderApp::onEntryClicked(lv_event_t *event)
{
    auto *app = static_cast<EReaderApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    const auto found = app->_buttonIndexMap.find(lv_event_get_target(event));
    if (found != app->_buttonIndexMap.end()) {
        app->openEntry(found->second);
    }
}

void EReaderApp::onRefreshClicked(lv_event_t *event)
{
    auto *app = static_cast<EReaderApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->refreshEntries();
    }
}

void EReaderApp::onUpClicked(lv_event_t *event)
{
    auto *app = static_cast<EReaderApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->navigateUp();
    }
}

void EReaderApp::onSdClicked(lv_event_t *event)
{
    auto *app = static_cast<EReaderApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->switchRoot(true);
    }
}

void EReaderApp::onSpiffsClicked(lv_event_t *event)
{
    auto *app = static_cast<EReaderApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->switchRoot(false);
    }
}

void EReaderApp::onCloseReaderClicked(lv_event_t *event)
{
    auto *app = static_cast<EReaderApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->showList();
    }
}