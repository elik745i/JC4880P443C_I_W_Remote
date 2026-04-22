#include "SegaEmulator.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "SegaGwenesisBridge.h"
#include "psram_alloc.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_lcd_touch.h"
#include "freertos/idf_additions.h"

#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"

extern "C" {
#include "sega_emulator/smsplus/shared.h"
#include "sega_emulator/smsplus/render.h"
#include "sega_emulator/smsplus/state.h"
}

LV_IMG_DECLARE(img_app_sega);

namespace {
constexpr const char *kTag = "SegaEmulator";
constexpr const char *kSdRomRoot = BSP_SD_MOUNT_POINT;
constexpr const char *kSpiffsRomRoot = BSP_SPIFFS_MOUNT_POINT;
constexpr const char *kIndexFilePath = BSP_SD_MOUNT_POINT "/.jc4880_sega_index_v1.txt";
constexpr const char *kIndexTempFilePath = BSP_SD_MOUNT_POINT "/.jc4880_sega_index_v1.tmp";
constexpr const char *kLegacyIndexFilePath = BSP_SPIFFS_MOUNT_POINT "/.jc4880_sega_index_v1.txt";
constexpr const char *kLegacyIndexTempFilePath = BSP_SPIFFS_MOUNT_POINT "/.jc4880_sega_index_v1.tmp";

constexpr uint32_t kInputUp = 1u << 0;
constexpr uint32_t kInputDown = 1u << 1;
constexpr uint32_t kInputLeft = 1u << 2;
constexpr uint32_t kInputRight = 1u << 3;
constexpr uint32_t kInputButton1 = 1u << 4;
constexpr uint32_t kInputButton2 = 1u << 5;
constexpr uint32_t kInputButton3 = 1u << 6;
constexpr uint32_t kInputStart = 1u << 7;

constexpr lv_coord_t kButtonWidth = 82;
constexpr lv_coord_t kButtonHeight = 56;
constexpr lv_coord_t kBrowserListHeight = 504;
constexpr lv_coord_t kBrowserListHeightWithKeyboard = 248;
constexpr lv_coord_t kSearchRowY = 112;
constexpr lv_coord_t kSearchFieldWidth = 352;
constexpr lv_coord_t kSearchButtonWidth = 88;
constexpr lv_coord_t kSearchControlHeight = 48;
constexpr lv_coord_t kPageButtonWidth = 88;
constexpr lv_coord_t kPageButtonHeight = 42;
constexpr lv_coord_t kPageNavBottomOffset = -8;
constexpr size_t kRomPageSize = 18;
constexpr int kGenesisOutputSampleRate = 44100;

constexpr size_t kSmsFrameBufferSize = SMS_WIDTH * SMS_HEIGHT;
constexpr size_t kGenesisFrameBufferSize = SEGA_GWENESIS_FRAME_OFFSET + (SEGA_GWENESIS_FRAME_STRIDE * SEGA_GWENESIS_FRAME_HEIGHT);

template <typename StringT>
StringT to_lower(StringT value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

void style_action_button(lv_obj_t *button)
{
    lv_obj_set_size(button, kButtonWidth, kButtonHeight);
    lv_obj_set_style_radius(button, 16, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1c1f2e), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x343a57), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, lv_color_hex(0x5b6388), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_text_color(button, lv_color_white(), 0);
}

void set_button_label(lv_obj_t *button, const char *label)
{
    lv_obj_t *text = lv_label_create(button);
    lv_label_set_text(text, label);
    lv_obj_center(text);
}

template <typename StringT>
bool has_extension(const StringT &path, const char *extension)
{
    const StringT lower = to_lower(path);
    const size_t extensionLength = strlen(extension);
    return (lower.size() >= extensionLength) && (lower.rfind(extension) == lower.size() - extensionLength);
}

template <typename StringT>
bool is_directory_path(const StringT &path)
{
    struct stat path_stat = {};
    if (stat(path.c_str(), &path_stat) != 0) {
        return false;
    }

    return S_ISDIR(path_stat.st_mode);
}

bool is_directory_path(const char *path)
{
    struct stat path_stat = {};
    if ((path == nullptr) || (stat(path, &path_stat) != 0)) {
        return false;
    }

    return S_ISDIR(path_stat.st_mode);
}

bool point_in_area(uint16_t x, uint16_t y, const lv_area_t &area)
{
    return (x >= area.x1) && (x <= area.x2) && (y >= area.y1) && (y <= area.y2);
}

bool file_exists(const char *path)
{
    if (path == nullptr) {
        return false;
    }

    struct stat path_stat = {};
    return stat(path, &path_stat) == 0;
}

bool ensure_sd_rom_root_available(bool allow_mount)
{
    if (allow_mount && !app_storage_ensure_sdcard_available()) {
        return false;
    }

    return app_storage_is_sdcard_mounted() && is_directory_path(kSdRomRoot);
}

BaseType_t create_emulator_task_prefer_psram(TaskFunction_t task,
                                             const char *name,
                                             const uint32_t stack_depth,
                                             void *arg,
                                             const UBaseType_t priority,
                                             TaskHandle_t *task_handle,
                                             const BaseType_t core_id)
{
    if (xTaskCreatePinnedToCoreWithCaps(task,
                                        name,
                                        stack_depth,
                                        arg,
                                        priority,
                                        task_handle,
                                        core_id,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        ESP_LOGI(kTag, "Started %s with a PSRAM-backed stack", name);
        return pdPASS;
    }

    ESP_LOGW(kTag,
             "Falling back to internal RAM stack for %s. Internal free=%u largest=%u PSRAM free=%u",
             name,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    return xTaskCreatePinnedToCore(task, name, stack_depth, arg, priority, task_handle, core_id);
}

void delay_for_frame(TickType_t &lastWakeTime, TickType_t frameInterval)
{
    const TickType_t now = xTaskGetTickCount();
    const TickType_t scheduledWake = lastWakeTime + frameInterval;
    if (scheduledWake > now) {
        vTaskDelayUntil(&lastWakeTime, frameInterval);
        return;
    }

    lastWakeTime = now;
    vTaskDelay(1);
}

size_t resample_stereo_frames(const int16_t *source,
                             size_t sourceFrames,
                             int sourceRate,
                             int targetRate,
                             int16_t *destination,
                             size_t destinationCapacityFrames)
{
    if ((source == nullptr) || (destination == nullptr) || (sourceFrames == 0) ||
        (sourceRate <= 0) || (targetRate <= 0) || (destinationCapacityFrames == 0)) {
        return 0;
    }

    size_t targetFrames = (sourceFrames * static_cast<size_t>(targetRate)) / static_cast<size_t>(sourceRate);
    if (targetFrames == 0) {
        targetFrames = 1;
    }
    if (targetFrames > destinationCapacityFrames) {
        targetFrames = destinationCapacityFrames;
    }

    for (size_t frame = 0; frame < targetFrames; ++frame) {
        const size_t sourceIndex = std::min((frame * static_cast<size_t>(sourceRate)) / static_cast<size_t>(targetRate),
                                            sourceFrames - 1);
        destination[frame * 2] = source[sourceIndex * 2];
        destination[frame * 2 + 1] = source[sourceIndex * 2 + 1];
    }

    return targetFrames;
}
}

SegaEmulator::SegaEmulator()
    : ESP_Brookesia_PhoneApp("SEGA Emulator", &img_app_sega, true)
{
}

SegaEmulator::~SegaEmulator()
{
    stopEmulation();
    _controlBindings.clear();

    if (_canvasFrontBuffer != nullptr) {
        heap_caps_free(_canvasFrontBuffer);
        _canvasFrontBuffer = nullptr;
    }
    if (_canvasBackBuffer != nullptr) {
        heap_caps_free(_canvasBackBuffer);
        _canvasBackBuffer = nullptr;
    }
    if (_emulatorBuffer != nullptr) {
        heap_caps_free(_emulatorBuffer);
        _emulatorBuffer = nullptr;
    }
}

bool SegaEmulator::init()
{
    _canvasFrontBuffer = static_cast<lv_color_t *>(
        heap_caps_malloc(sizeof(lv_color_t) * kCanvasWidth * kCanvasHeight, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    _canvasBackBuffer = static_cast<lv_color_t *>(
        heap_caps_malloc(sizeof(lv_color_t) * kCanvasWidth * kCanvasHeight, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    _emulatorBuffer = static_cast<uint8_t *>(
        heap_caps_malloc(std::max(kSmsFrameBufferSize, kGenesisFrameBufferSize), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if ((_canvasFrontBuffer == nullptr) || (_canvasBackBuffer == nullptr) || (_emulatorBuffer == nullptr)) {
        ESP_LOGE(kTag, "Failed to allocate emulator frame buffers");
        return false;
    }

    memset(_canvasFrontBuffer, 0, sizeof(lv_color_t) * kCanvasWidth * kCanvasHeight);
    memset(_canvasBackBuffer, 0, sizeof(lv_color_t) * kCanvasWidth * kCanvasHeight);
    memset(_emulatorBuffer, 0, std::max(kSmsFrameBufferSize, kGenesisFrameBufferSize));

    return true;
}

bool SegaEmulator::run()
{
    _closingApp.store(false);
    if (!ensureUiReady()) {
        return false;
    }

    refreshRomList();
    lv_scr_load(_browserScreen);
    return true;
}

bool SegaEmulator::pause()
{
    stopEmulation();
    return true;
}

bool SegaEmulator::resume()
{
    _closingApp.store(false);
    if (!ensureUiReady()) {
        return false;
    }

    if (_running.load()) {
        if ((_playerScreen == nullptr) && (_canvasFrontBuffer != nullptr) && (_canvasBackBuffer != nullptr) && (_emulatorBuffer != nullptr)) {
            createPlayerScreen();
        }
        lv_scr_load(_playerScreen);
    } else {
        lv_scr_load(_browserScreen);
    }
    return true;
}

bool SegaEmulator::ensureUiReady()
{
    if (_browserScreen != nullptr) {
        return true;
    }

    if ((_canvasFrontBuffer == nullptr) || (_canvasBackBuffer == nullptr) || (_emulatorBuffer == nullptr)) {
        ESP_LOGE(kTag, "SEGA buffers are not initialized");
        return false;
    }

    if (_browserScreen == nullptr) {
        createBrowserScreen();
    }

    return _browserScreen != nullptr;
}

bool SegaEmulator::back()
{
    if (_indexing.load()) {
        return true;
    }

    if (_running.load()) {
        _closingApp.store(false);
        stopEmulation();
        lv_scr_load(_browserScreen);
        return true;
    }
    return notifyCoreClosed();
}

bool SegaEmulator::close()
{
    if (_indexing.load()) {
        return true;
    }

    _closingApp.store(true);
    stopEmulation();
    return true;
}

void SegaEmulator::createBrowserScreen()
{
    _browserScreen = lv_obj_create(nullptr);
    lv_obj_add_event_cb(_browserScreen, onBrowserScreenDeleted, LV_EVENT_DELETE, this);
    lv_obj_set_style_bg_color(_browserScreen, lv_color_hex(0x0d1020), 0);
    lv_obj_set_style_bg_grad_color(_browserScreen, lv_color_hex(0x151a33), 0);
    lv_obj_set_style_bg_grad_dir(_browserScreen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(_browserScreen, 0, 0);
    lv_obj_set_style_pad_all(_browserScreen, 16, 0);

    lv_obj_t *title = lv_label_create(_browserScreen);
    lv_label_set_text(title, "SEGA Emulator");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 4);

    lv_obj_t *subtitle = lv_label_create(_browserScreen);
    lv_label_set_text_fmt(subtitle, "Index roots: %s and %s", kSdRomRoot, kSpiffsRomRoot);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xaeb6d8), 0);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    _refreshButton = lv_btn_create(_browserScreen);
    style_action_button(_refreshButton);
    lv_obj_set_size(_refreshButton, 108, 46);
    lv_obj_align(_refreshButton, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_add_event_cb(_refreshButton, onRefreshClicked, LV_EVENT_CLICKED, this);
    set_button_label(_refreshButton, "Refresh");

    _browserStatus = lv_label_create(_browserScreen);
    lv_obj_set_width(_browserStatus, 440);
    lv_label_set_long_mode(_browserStatus, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(_browserStatus, lv_color_hex(0xe0b96c), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_align(_browserStatus, LV_ALIGN_TOP_LEFT, 8, 74);

    _searchField = lv_textarea_create(_browserScreen);
    lv_obj_set_size(_searchField, kSearchFieldWidth, kSearchControlHeight);
    lv_obj_align(_searchField, LV_ALIGN_TOP_LEFT, 8, kSearchRowY);
    lv_textarea_set_one_line(_searchField, true);
    lv_textarea_set_max_length(_searchField, 64);
    lv_textarea_set_placeholder_text(_searchField, "Search ROMs");
    lv_obj_set_style_radius(_searchField, 14, 0);
    lv_obj_set_style_pad_left(_searchField, 14, 0);
    lv_obj_set_style_pad_right(_searchField, 14, 0);
    lv_obj_set_style_bg_color(_searchField, lv_color_hex(0x10162a), 0);
    lv_obj_set_style_border_color(_searchField, lv_color_hex(0x2f3554), 0);
    lv_obj_set_style_border_width(_searchField, 1, 0);
    lv_obj_set_style_text_color(_searchField, lv_color_white(), 0);
    lv_obj_add_event_cb(_searchField, onSearchChanged, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(_searchField, onSearchFieldEvent, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(_searchField, onSearchFieldEvent, LV_EVENT_CLICKED, this);

    _clearButton = lv_btn_create(_browserScreen);
    style_action_button(_clearButton);
    lv_obj_set_size(_clearButton, kSearchButtonWidth, kSearchControlHeight);
    lv_obj_set_style_radius(_clearButton, 14, 0);
    lv_obj_align(_clearButton, LV_ALIGN_TOP_RIGHT, -8, kSearchRowY);
    lv_obj_add_event_cb(_clearButton, [](lv_event_t *event) {
        auto *app = static_cast<SegaEmulator *>(lv_event_get_user_data(event));
        if ((app == nullptr) || (app->_searchField == nullptr) || app->_indexing.load()) {
            return;
        }

        lv_textarea_set_text(app->_searchField, "");
        app->_romFilter.clear();
        app->_currentPage = 0;
        app->rebuildRomList();
    }, LV_EVENT_CLICKED, this);
    set_button_label(_clearButton, "Clear");

    _romList = lv_list_create(_browserScreen);
    lv_obj_set_size(_romList, 448, kBrowserListHeight);
    lv_obj_align(_romList, LV_ALIGN_BOTTOM_MID, 0, -56);
    lv_obj_set_style_bg_color(_romList, lv_color_hex(0x10162a), 0);
    lv_obj_set_style_border_color(_romList, lv_color_hex(0x2f3554), 0);
    lv_obj_set_style_border_width(_romList, 1, 0);
    lv_obj_set_style_pad_row(_romList, 8, 0);

    _prevPageButton = lv_btn_create(_browserScreen);
    style_action_button(_prevPageButton);
    lv_obj_set_size(_prevPageButton, kPageButtonWidth, kPageButtonHeight);
    lv_obj_set_style_radius(_prevPageButton, 12, 0);
    lv_obj_align(_prevPageButton, LV_ALIGN_BOTTOM_LEFT, 8, kPageNavBottomOffset);
    lv_obj_add_event_cb(_prevPageButton, onPrevPageClicked, LV_EVENT_CLICKED, this);
    set_button_label(_prevPageButton, "Prev");

    _nextPageButton = lv_btn_create(_browserScreen);
    style_action_button(_nextPageButton);
    lv_obj_set_size(_nextPageButton, kPageButtonWidth, kPageButtonHeight);
    lv_obj_set_style_radius(_nextPageButton, 12, 0);
    lv_obj_align(_nextPageButton, LV_ALIGN_BOTTOM_RIGHT, -8, kPageNavBottomOffset);
    lv_obj_add_event_cb(_nextPageButton, onNextPageClicked, LV_EVENT_CLICKED, this);
    set_button_label(_nextPageButton, "Next");

    _pageLabel = lv_label_create(_browserScreen);
    lv_obj_set_width(_pageLabel, 220);
    lv_obj_set_style_text_align(_pageLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(_pageLabel, lv_color_hex(0xaeb6d8), 0);
    lv_obj_align(_pageLabel, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_label_set_text(_pageLabel, "Page 1 / 1");

    _searchKeyboard = lv_keyboard_create(_browserScreen);
    lv_obj_set_size(_searchKeyboard, 480, 260);
    lv_obj_align(_searchKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(_searchKeyboard, _searchField);
    lv_obj_add_event_cb(_searchKeyboard, onSearchKeyboardEvent, LV_EVENT_READY, this);
    lv_obj_add_event_cb(_searchKeyboard, onSearchKeyboardEvent, LV_EVENT_CANCEL, this);
    lv_obj_add_flag(_searchKeyboard, LV_OBJ_FLAG_HIDDEN);

    _indexOverlay = lv_obj_create(_browserScreen);
    lv_obj_set_size(_indexOverlay, LV_PCT(100), LV_PCT(100));
    lv_obj_center(_indexOverlay);
    lv_obj_set_style_bg_color(_indexOverlay, lv_color_hex(0x040714), 0);
    lv_obj_set_style_bg_opa(_indexOverlay, LV_OPA_80, 0);
    lv_obj_set_style_border_width(_indexOverlay, 0, 0);
    lv_obj_clear_flag(_indexOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_indexOverlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *overlayPanel = lv_obj_create(_indexOverlay);
    lv_obj_set_size(overlayPanel, 380, 240);
    lv_obj_center(overlayPanel);
    lv_obj_set_style_radius(overlayPanel, 24, 0);
    lv_obj_set_style_bg_color(overlayPanel, lv_color_hex(0x12182d), 0);
    lv_obj_set_style_border_color(overlayPanel, lv_color_hex(0x2f3554), 0);
    lv_obj_set_style_border_width(overlayPanel, 1, 0);
    lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *overlayTitle = lv_label_create(overlayPanel);
    lv_label_set_text(overlayTitle, "Indexing Games");
    lv_obj_set_style_text_font(overlayTitle, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(overlayTitle, lv_color_white(), 0);
    lv_obj_align(overlayTitle, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *spinner = lv_spinner_create(overlayPanel, 1000, 90);
    lv_obj_set_size(spinner, 72, 72);
    lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_clear_flag(spinner, LV_OBJ_FLAG_CLICKABLE);

    _indexProgressLabel = lv_label_create(overlayPanel);
    lv_obj_set_width(_indexProgressLabel, 320);
    lv_label_set_long_mode(_indexProgressLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_indexProgressLabel, "Preparing library scan...");
    lv_obj_set_style_text_color(_indexProgressLabel, lv_color_hex(0xDCE3FF), 0);
    lv_obj_set_style_text_align(_indexProgressLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_indexProgressLabel, LV_ALIGN_BOTTOM_MID, 0, -28);
}

void SegaEmulator::createPlayerScreen()
{
    _playerScreen = lv_obj_create(nullptr);
    lv_obj_add_event_cb(_playerScreen, onPlayerScreenDeleted, LV_EVENT_DELETE, this);
    lv_obj_set_style_bg_color(_playerScreen, lv_color_hex(0x05070f), 0);
    lv_obj_set_style_border_width(_playerScreen, 0, 0);
    lv_obj_set_style_pad_all(_playerScreen, 14, 0);

    _playerTitle = lv_label_create(_playerScreen);
    lv_obj_set_width(_playerTitle, 452);
    lv_label_set_long_mode(_playerTitle, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(_playerTitle, lv_color_white(), 0);
    lv_obj_set_style_text_font(_playerTitle, &lv_font_montserrat_24, 0);
    lv_obj_align(_playerTitle, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(_playerTitle, "Loading...");

    _playerStatus = lv_label_create(_playerScreen);
    lv_obj_set_width(_playerStatus, 452);
    lv_label_set_long_mode(_playerStatus, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(_playerStatus, lv_color_hex(0x94a0d1), 0);
    lv_obj_align(_playerStatus, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_label_set_text(_playerStatus, "Tap a ROM from the browser to start.");

    lv_obj_t *frame = lv_obj_create(_playerScreen);
    lv_obj_set_size(frame, kCanvasWidth + 20, kCanvasHeight + 20);
    lv_obj_align(frame, LV_ALIGN_TOP_MID, 0, 88);
    lv_obj_set_style_bg_color(frame, lv_color_hex(0x111524), 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0x39415f), 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_pad_all(frame, 10, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    _canvas = lv_canvas_create(frame);
    lv_canvas_set_buffer(_canvas, _canvasFrontBuffer, kCanvasWidth, kCanvasHeight, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(_canvas, lv_color_black(), LV_OPA_COVER);
    lv_obj_center(_canvas);

    lv_obj_t *hint = lv_label_create(_playerScreen);
    lv_label_set_text(hint, "Back returns to the ROM list. Genesis maps A/B/C plus Start.");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8e95b8), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 418);

    lv_obj_t *dpad = lv_obj_create(_playerScreen);
    lv_obj_set_size(dpad, 208, 188);
    lv_obj_align(dpad, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(dpad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dpad, 0, 0);
    lv_obj_set_style_pad_all(dpad, 0, 0);
    lv_obj_clear_flag(dpad, LV_OBJ_FLAG_SCROLLABLE);

    _upButton = lv_btn_create(dpad);
    style_action_button(_upButton);
    lv_obj_align(_upButton, LV_ALIGN_TOP_MID, 0, 0);
    set_button_label(_upButton, "Up");

    _leftButton = lv_btn_create(dpad);
    style_action_button(_leftButton);
    lv_obj_align(_leftButton, LV_ALIGN_LEFT_MID, 0, 4);
    set_button_label(_leftButton, "Left");

    _rightButton = lv_btn_create(dpad);
    style_action_button(_rightButton);
    lv_obj_align(_rightButton, LV_ALIGN_RIGHT_MID, 0, 4);
    set_button_label(_rightButton, "Right");

    _downButton = lv_btn_create(dpad);
    style_action_button(_downButton);
    lv_obj_align(_downButton, LV_ALIGN_BOTTOM_MID, 0, 0);
    set_button_label(_downButton, "Down");

    lv_obj_t *actions = lv_obj_create(_playerScreen);
    lv_obj_set_size(actions, 224, 188);
    lv_obj_align(actions, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    _buttonA = lv_btn_create(actions);
    style_action_button(_buttonA);
    lv_obj_set_size(_buttonA, 64, 52);
    lv_obj_align(_buttonA, LV_ALIGN_TOP_LEFT, 0, 18);
    set_button_label(_buttonA, "A");

    _buttonB = lv_btn_create(actions);
    style_action_button(_buttonB);
    lv_obj_set_size(_buttonB, 64, 52);
    lv_obj_align(_buttonB, LV_ALIGN_TOP_MID, 0, 0);
    set_button_label(_buttonB, "B");

    _buttonC = lv_btn_create(actions);
    style_action_button(_buttonC);
    lv_obj_set_size(_buttonC, 64, 52);
    lv_obj_align(_buttonC, LV_ALIGN_TOP_RIGHT, 0, 18);
    set_button_label(_buttonC, "C");

    _startButton = lv_btn_create(actions);
    style_action_button(_startButton);
    lv_obj_set_size(_startButton, 116, 52);
    lv_obj_align(_startButton, LV_ALIGN_BOTTOM_MID, 0, 0);
    set_button_label(_startButton, "Start");

    const struct {
        lv_obj_t *button;
        uint32_t mask;
    } controls[] = {
        {_upButton, kInputUp},
        {_downButton, kInputDown},
        {_leftButton, kInputLeft},
        {_rightButton, kInputRight},
        {_buttonA, kInputButton1},
        {_buttonB, kInputButton2},
        {_buttonC, kInputButton3},
        {_startButton, kInputStart},
    };

    _controlBindings.clear();
    _controlBindings.reserve(sizeof(controls) / sizeof(controls[0]));
    for (const auto &control : controls) {
        _controlBindings.push_back(ControlBinding{this, control.mask});
        ControlBinding *binding = &_controlBindings.back();
        lv_obj_add_event_cb(control.button, onControlButtonEvent, LV_EVENT_PRESSED, binding);
        lv_obj_add_event_cb(control.button, onControlButtonEvent, LV_EVENT_PRESS_LOST, binding);
        lv_obj_add_event_cb(control.button, onControlButtonEvent, LV_EVENT_RELEASED, binding);
    }

    cachePlayerControlRegions();
}

void SegaEmulator::resetBrowserUiPointers()
{
    _browserScreen = nullptr;
    _romList = nullptr;
    _browserStatus = nullptr;
    _searchField = nullptr;
    _searchKeyboard = nullptr;
    _refreshButton = nullptr;
    _clearButton = nullptr;
    _prevPageButton = nullptr;
    _nextPageButton = nullptr;
    _pageLabel = nullptr;
    _indexOverlay = nullptr;
    _indexProgressLabel = nullptr;
}

void SegaEmulator::resetPlayerUiPointers()
{
    _playerScreen = nullptr;
    _playerStatus = nullptr;
    _playerTitle = nullptr;
    _canvas = nullptr;
    _upButton = nullptr;
    _downButton = nullptr;
    _leftButton = nullptr;
    _rightButton = nullptr;
    _buttonA = nullptr;
    _buttonB = nullptr;
    _buttonC = nullptr;
    _startButton = nullptr;
    _controlRegions.clear();

    _controlBindings.clear();
}

void SegaEmulator::releaseUiState()
{
    if ((_indexPromptMessageBox != nullptr) && lv_obj_is_valid(_indexPromptMessageBox)) {
        lv_msgbox_close(_indexPromptMessageBox);
    }
    _indexPromptMessageBox = nullptr;

    if ((_browserScreen != nullptr) && lv_obj_is_valid(_browserScreen)) {
        lv_obj_del(_browserScreen);
    } else {
        resetBrowserUiPointers();
    }

    if ((_playerScreen != nullptr) && lv_obj_is_valid(_playerScreen)) {
        lv_obj_del(_playerScreen);
    } else {
        resetPlayerUiPointers();
    }

    SegaVector<RomEntry>().swap(_romEntries);
    SegaVector<RomEntry>().swap(_pendingIndexedEntries);
    SegaString().swap(_romFilter);
    SegaString().swap(_pendingBrowserStatus);
    SegaString().swap(_pendingIndexProgress);
    SegaString().swap(_pendingIndexStatus);
    _indexLoaded = false;
    _currentPage = 0;
    _framePresentationQueued.store(false);
}

void SegaEmulator::refreshRomList()
{
    if (_indexing.load()) {
        return;
    }

    const bool sdReady = ensure_sd_rom_root_available(true);

    if (_indexLoaded) {
        rebuildRomList();
        return;
    }

    if (loadIndexFile()) {
        _indexLoaded = true;
        _currentPage = 0;
        rebuildRomList();
        return;
    }

    SegaVector<RomEntry>().swap(_romEntries);
    _currentPage = 0;
    if (_romList != nullptr) {
        lv_obj_clean(_romList);
    }
    setBrowserStatus(sdReady ?
        "No game index found. Scan storage to build the game list." :
        "SD card is still coming online. Scan storage after it is ready to include SD games.");
    promptInitialIndex();
}

void SegaEmulator::rebuildRomList()
{
    if (_romList == nullptr) {
        return;
    }

    lv_obj_clean(_romList);

    size_t visibleCount = 0;
    for (RomEntry &rom : _romEntries) {
        rom.button = nullptr;
        if (matchesRomFilter(rom)) {
            visibleCount++;
        }
    }

    if (visibleCount == 0) {
        if (_pageLabel != nullptr) {
            lv_label_set_text(_pageLabel, "Page 0 / 0");
        }
        if (_prevPageButton != nullptr) {
            lv_obj_add_state(_prevPageButton, LV_STATE_DISABLED);
        }
        if (_nextPageButton != nullptr) {
            lv_obj_add_state(_nextPageButton, LV_STATE_DISABLED);
        }
        if (_romEntries.empty()) {
            setBrowserStatus(_indexLoaded ?
                "Indexed library is empty. Tap Refresh to run a clean reindex." :
                "No supported ROMs found yet. Supported extensions: .sms, .gg, .sg, .md, .gen, .bin, .smd.");
        } else if (_romFilter.empty()) {
            setBrowserStatus("Indexed library has no visible games.");
        } else {
            setBrowserStatus("No ROMs match the current search.");
        }
        return;
    }

    const size_t pageCount = (visibleCount + kRomPageSize - 1) / kRomPageSize;
    if (_currentPage >= pageCount) {
        _currentPage = pageCount - 1;
    }

    const size_t pageStart = _currentPage * kRomPageSize;
    const size_t pageEnd = std::min(pageStart + kRomPageSize, visibleCount);
    size_t visibleIndex = 0;
    for (RomEntry &rom : _romEntries) {
        if (!matchesRomFilter(rom)) {
            continue;
        }

        if ((visibleIndex >= pageStart) && (visibleIndex < pageEnd)) {
            rom.button = lv_list_add_btn(_romList, LV_SYMBOL_PLAY, rom.name.c_str());
            lv_obj_add_event_cb(rom.button, onRomSelected, LV_EVENT_CLICKED, this);
        }
        visibleIndex++;
    }

    if (_pageLabel != nullptr) {
        static char pageStatus[48];
        snprintf(pageStatus, sizeof(pageStatus), "Page %u / %u",
                 static_cast<unsigned>(_currentPage + 1),
                 static_cast<unsigned>(pageCount));
        lv_label_set_text(_pageLabel, pageStatus);
    }
    if (_prevPageButton != nullptr) {
        if (_currentPage == 0) {
            lv_obj_add_state(_prevPageButton, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(_prevPageButton, LV_STATE_DISABLED);
        }
    }
    if (_nextPageButton != nullptr) {
        if ((_currentPage + 1) >= pageCount) {
            lv_obj_add_state(_nextPageButton, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(_nextPageButton, LV_STATE_DISABLED);
        }
    }

    if (_romFilter.empty()) {
        static char status[96];
        snprintf(status, sizeof(status), "Ready. Showing %u-%u of %u ROMs.",
                 static_cast<unsigned>(pageStart + 1),
                 static_cast<unsigned>(pageEnd),
                 static_cast<unsigned>(visibleCount));
        setBrowserStatus(status);
    } else {
        static char status[128];
        snprintf(status, sizeof(status), "Showing %u-%u of %u ROMs for \"%s\".",
                 static_cast<unsigned>(pageStart + 1),
                 static_cast<unsigned>(pageEnd),
                 static_cast<unsigned>(visibleCount),
                 _romFilter.c_str());
        setBrowserStatus(status);
    }
}

void SegaEmulator::promptInitialIndex()
{
    if ((_indexPromptMessageBox != nullptr) || _indexing.load()) {
        return;
    }

    static const char *buttons[] = {"Scan", "Cancel", ""};
    _indexPromptMessageBox = lv_msgbox_create(nullptr,
                                              "Build Game Index",
                                              "No game index was found. Scan SD card and SPIFFS now to build a cached game list?",
                                              buttons,
                                              false);
    lv_obj_set_width(_indexPromptMessageBox, LV_MIN(LV_HOR_RES - 24, 420));
    lv_obj_center(_indexPromptMessageBox);
    lv_obj_add_event_cb(_indexPromptMessageBox, onInitialIndexPromptEvent, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(_indexPromptMessageBox, onInitialIndexPromptEvent, LV_EVENT_DELETE, this);
}

void SegaEmulator::startIndexing(bool forceReindex)
{
    if (_indexing.exchange(true)) {
        return;
    }

    const bool sdReady = ensure_sd_rom_root_available(true);

    if (_indexPromptMessageBox != nullptr) {
        lv_msgbox_close_async(_indexPromptMessageBox);
        _indexPromptMessageBox = nullptr;
    }

    setSearchKeyboardVisible(false);
    setIndexingOverlayVisible(true);
    {
        std::lock_guard<std::mutex> guard(_indexStateMutex);
        _pendingIndexProgress = forceReindex ? "Starting clean reindex..." : "Starting initial index...";
        if (!sdReady) {
            _pendingIndexProgress += "\nSD card is not mounted yet; continuing with available storage.";
        }
        _pendingIndexStatus.clear();
        _pendingIndexedEntries.clear();
    }
    updateIndexingProgressOnUiThread();

    IndexTaskContext *context = new IndexTaskContext{this, forceReindex};
    if (create_emulator_task_prefer_psram(indexingTaskEntry, "sega_index", 8192, context, 3, &_indexTask, 1) != pdPASS) {
        delete context;
        _indexTask = nullptr;
        _indexing.store(false);
        setIndexingOverlayVisible(false);
        setBrowserStatus("Failed to start the indexing task.");
    }
}

void SegaEmulator::indexingTask(bool forceReindex)
{
    SegaVector<RomEntry> entries;
    size_t scannedFiles = 0;
    size_t matchedFiles = 0;

    if (forceReindex) {
        unlink(kIndexTempFilePath);
    }

    auto publish_progress = [this](const SegaString &message) {
        {
            std::lock_guard<std::mutex> guard(_indexStateMutex);
            _pendingIndexProgress = message;
        }
        lv_async_call(indexingProgressAsync, this);
    };

    auto scan_root = [this, &entries, &scannedFiles, &matchedFiles, &publish_progress](const char *rootLabel,
                                                                                         const char *rootPath,
                                                                                         bool allowMount) {
        if ((strcmp(rootPath, kSdRomRoot) == 0) && !ensure_sd_rom_root_available(allowMount)) {
            publish_progress(SegaString("Skipping ") + rootLabel + ": not available");
            return;
        }

        if (!is_directory_path(rootPath)) {
            publish_progress(SegaString("Skipping ") + rootLabel + ": not available");
            return;
        }

        publish_progress(SegaString("Indexing ") + rootLabel + "...\n0 files scanned, 0 games found");

        SegaVector<SegaString> directories;
        directories.push_back(rootPath);
        while (!directories.empty()) {
            const SegaString directory = directories.back();
            directories.pop_back();

            DIR *dir = opendir(directory.c_str());
            if (dir == nullptr) {
                continue;
            }

            struct dirent *entry = nullptr;
            while ((entry = readdir(dir)) != nullptr) {
                if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
                    continue;
                }

                const SegaString fullPath = directory + "/" + entry->d_name;
                if (is_directory_path(fullPath)) {
                    directories.push_back(fullPath);
                    continue;
                }

                scannedFiles++;
                if (!hasSupportedExtension(fullPath)) {
                    if ((scannedFiles % 64) == 0) {
                        char progress[160];
                        snprintf(progress, sizeof(progress), "%s\n%u files scanned, %u games found",
                                 rootLabel,
                                 static_cast<unsigned>(scannedFiles),
                                 static_cast<unsigned>(matchedFiles));
                        publish_progress(progress);
                    }
                    continue;
                }

                matchedFiles++;
                entries.push_back(RomEntry{
                    .name = entry->d_name,
                    .path = fullPath,
                    .button = nullptr,
                });

                if ((matchedFiles % 16) == 0) {
                    char progress[160];
                    snprintf(progress, sizeof(progress), "%s\n%u files scanned, %u games found",
                             rootLabel,
                             static_cast<unsigned>(scannedFiles),
                             static_cast<unsigned>(matchedFiles));
                    publish_progress(progress);
                }
            }

            closedir(dir);
        }
    };

    scan_root("SD Card", kSdRomRoot, true);
    scan_root("SPIFFS", kSpiffsRomRoot, false);

    std::sort(entries.begin(), entries.end(), [](const RomEntry &lhs, const RomEntry &rhs) {
        const SegaString left_name = to_lower(lhs.name);
        const SegaString right_name = to_lower(rhs.name);
        if (left_name == right_name) {
            return to_lower(lhs.path) < to_lower(rhs.path);
        }
        return left_name < right_name;
    });

    const bool saved = saveIndexFile(entries);
    char finalStatus[160];
    snprintf(finalStatus,
             sizeof(finalStatus),
             saved ? "Indexed %u games from SD card and SPIFFS." : "Indexed %u games, but failed to save the cache.",
             static_cast<unsigned>(entries.size()));

    {
        std::lock_guard<std::mutex> guard(_indexStateMutex);
        _pendingIndexedEntries = std::move(entries);
        _pendingIndexProgress = finalStatus;
        _pendingIndexStatus = finalStatus;
    }
    lv_async_call(indexingFinishedAsync, this);
    _indexTask = nullptr;
    vTaskDelete(nullptr);
}

void SegaEmulator::updateIndexingProgressOnUiThread()
{
    if (_indexProgressLabel == nullptr) {
        return;
    }

    std::string progress;
    {
        std::lock_guard<std::mutex> guard(_indexStateMutex);
        progress = _pendingIndexProgress;
    }
    lv_label_set_text(_indexProgressLabel, progress.c_str());
}

void SegaEmulator::finishIndexingOnUiThread()
{
    SegaVector<RomEntry> entries;
    SegaString status;
    {
        std::lock_guard<std::mutex> guard(_indexStateMutex);
        entries.swap(_pendingIndexedEntries);
        status = _pendingIndexStatus;
    }

    _romEntries = std::move(entries);
    _indexLoaded = true;
    _indexing.store(false);
    _currentPage = 0;
    setIndexingOverlayVisible(false);
    rebuildRomList();
    setBrowserStatus(status.c_str());
}

void SegaEmulator::startRom(const SegaString &path, const SegaString &name)
{
    if (_running.load()) {
        return;
    }

    _closingApp.store(false);

    if ((_playerScreen == nullptr) || (_canvas == nullptr)) {
        createPlayerScreen();
    }

    if ((_playerScreen == nullptr) || (_canvas == nullptr) || (_playerTitle == nullptr)) {
        setBrowserStatus("Failed to create the player interface.");
        return;
    }

    _currentCore = getCoreForPath(path);
    _activeRomPath = path;
    _activeRomName = name;
    _stopRequested.store(false);
    _inputMask.store(0);
    lv_label_set_text(_playerTitle, name.c_str());
    setPlayerStatus("Starting emulator...");
    lv_canvas_fill_bg(_canvas, lv_color_black(), LV_OPA_COVER);
    lv_scr_load(_playerScreen);

    if (create_emulator_task_prefer_psram(emulatorTaskEntry, "sega_emu", 8192, this, 4, &_emulatorTask, 1) != pdPASS) {
        _emulatorTask = nullptr;
        setBrowserStatus("Failed to start the emulator task.");
        lv_scr_load(_browserScreen);
        return;
    }
}

void SegaEmulator::stopEmulation()
{
    if (_emulatorTask == nullptr) {
        _running.store(false);
        _stopRequested.store(false);
        _inputMask.store(0);
        return;
    }

    _stopRequested.store(true);
    for (int i = 0; (i < 100) && (_emulatorTask != nullptr); ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    _inputMask.store(0);
}

void SegaEmulator::emulatorTaskEntry(void *context)
{
    static_cast<SegaEmulator *>(context)->emulatorTask();
}

void SegaEmulator::emulatorTask()
{
    _running.store(true);
    bool smsSystemInitialized = false;

    auto finish = [this, &smsSystemInitialized](const char *browserStatus) {
        teardownAudio();
        if (smsSystemInitialized) {
            system_poweroff();
            system_shutdown();
        }

        if (cart.rom != nullptr) {
            sega_psram_free(cart.rom);
            cart.rom = nullptr;
        }
        if (cart.sram != nullptr) {
            sega_psram_free(cart.sram);
            cart.sram = nullptr;
        }
        sega_gwenesis_shutdown();

        cart.loaded = 0;
        _running.store(false);
        _stopRequested.store(false);
        _emulatorTask = nullptr;
        _pendingBrowserStatus = browserStatus;
        if (!_finishUiQueued.exchange(true)) {
            lv_async_call(finishEmulationAsync, this);
        }
        vTaskDelete(nullptr);
    };

    memset(_emulatorBuffer, 0, std::max(kSmsFrameBufferSize, kGenesisFrameBufferSize));

    if (_currentCore == EmulatorCore::Gwenesis) {
        if (!sega_gwenesis_load_rom(_activeRomPath.c_str(), _emulatorBuffer, kGenesisFrameBufferSize)) {
            finish("Failed to load Mega Drive ROM.");
            return;
        }

        const int genesisInputSampleRate = sega_gwenesis_get_audio_sample_rate();
        if (!setupAudio(kGenesisOutputSampleRate)) {
            finish("Audio initialization failed.");
            return;
        }

        const TickType_t frameInterval = pdMS_TO_TICKS(1000 / SEGA_GWENESIS_REFRESH_RATE);
        TickType_t lastWakeTime = xTaskGetTickCount();
        SegaVector<int16_t> nativeMixbuffer(SEGA_GWENESIS_AUDIO_BUFFER_LENGTH * 2);
        SegaVector<int16_t> outputMixbuffer(((kGenesisOutputSampleRate / SEGA_GWENESIS_REFRESH_RATE) + 8) * 2);
        int skipFrames = 0;

        setPlayerStatus("Running Mega Drive emulator");

        while (!_stopRequested.load()) {
            const int64_t frameStartUs = esp_timer_get_time();
            const bool drawFrame = (skipFrames == 0);
            uint32_t touchMask = 0;
            if (readTouchInputMask(touchMask)) {
                _inputMask.store(touchMask);
            }

            sega_gwenesis_set_input_mask(_inputMask.load());
            sega_gwenesis_run_frame(drawFrame);
            if (drawFrame) {
                renderCurrentFrame();
            }

            const size_t nativeSampleCount = sega_gwenesis_mix_audio_stereo(nativeMixbuffer.data(), nativeMixbuffer.size() / 2);
            const size_t outputSampleCount = resample_stereo_frames(nativeMixbuffer.data(),
                                                                    nativeSampleCount,
                                                                    genesisInputSampleRate,
                                                                    kGenesisOutputSampleRate,
                                                                    outputMixbuffer.data(),
                                                                    outputMixbuffer.size() / 2);
            if (outputSampleCount > 0) {
                size_t written = 0;
                bsp_extra_i2s_write(outputMixbuffer.data(), outputSampleCount * 2 * sizeof(int16_t), &written, 0);
            }

            const int64_t elapsedUs = esp_timer_get_time() - frameStartUs;
            const int64_t frameBudgetUs = (1000000LL / SEGA_GWENESIS_REFRESH_RATE) + 1500LL;
            if (skipFrames == 0) {
                if (elapsedUs > frameBudgetUs) {
                    skipFrames = 1;
                }
            } else {
                skipFrames--;
            }

            delay_for_frame(lastWakeTime, frameInterval);
        }

        finish("Mega Drive emulator stopped.");
        return;
    }

    memset(&smsplus, 0, sizeof(smsplus));
    memset(&snd, 0, sizeof(snd));

    system_reset_config();
    option.sndrate = kSmsAudioSampleRate;
    option.overscan = 0;
    option.extra_gg = 0;

    const SegaString lowerPath = to_lower(_activeRomPath);
    if (lowerPath.size() >= 3 && (lowerPath.rfind(".sg") == lowerPath.size() - 3)) {
        option.console = 5;
    }

    if (!load_rom_file(_activeRomPath.c_str())) {
        finish("Failed to load ROM file.");
        return;
    }

    loadBatterySave();

    bitmap.width = SMS_WIDTH;
    bitmap.height = SMS_HEIGHT;
    bitmap.pitch = SMS_WIDTH;
    bitmap.data = _emulatorBuffer;

    system_poweron();
    smsSystemInitialized = true;

    if (!setupAudio(kSmsAudioSampleRate)) {
        finish("Audio initialization failed.");
        return;
    }

    const TickType_t frameInterval = pdMS_TO_TICKS((sms.display == DISPLAY_NTSC) ? 17 : 20);
    TickType_t lastWakeTime = xTaskGetTickCount();
    SegaVector<int16_t> mixbuffer(static_cast<size_t>(kSmsAudioSampleRate / 50) * 2);

    setPlayerStatus("Running SMS/Game Gear emulator");

    while (!_stopRequested.load()) {
        uint32_t touchMask = 0;
        if (readTouchInputMask(touchMask)) {
            _inputMask.store(touchMask);
        }

        updateSmsInputState();
        system_frame(0);
        renderCurrentFrame();

        if (snd.sample_count > 0) {
            const size_t sampleCount = static_cast<size_t>(snd.sample_count);
            if (mixbuffer.size() < sampleCount * 2) {
                mixbuffer.resize(sampleCount * 2);
            }
            for (size_t i = 0; i < sampleCount; ++i) {
                int left = snd.stream[0][i] * 3;
                int right = snd.stream[1][i] * 3;
                left = std::max(-32768, std::min(32767, left));
                right = std::max(-32768, std::min(32767, right));
                mixbuffer[i * 2] = static_cast<int16_t>(left);
                mixbuffer[i * 2 + 1] = static_cast<int16_t>(right);
            }
            size_t written = 0;
            bsp_extra_i2s_write(mixbuffer.data(), sampleCount * 2 * sizeof(int16_t), &written, 0);
        }

        delay_for_frame(lastWakeTime, frameInterval);
    }

    saveBatterySave();
    finish("Emulator stopped.");
}

void SegaEmulator::setBrowserStatus(const char *text)
{
    if (_browserStatus == nullptr) {
        return;
    }
    bsp_display_lock(0);
    lv_label_set_text(_browserStatus, text);
    bsp_display_unlock();
}

void SegaEmulator::setPlayerStatus(const char *text)
{
    if (_playerStatus == nullptr) {
        return;
    }
    bsp_display_lock(0);
    lv_label_set_text(_playerStatus, text);
    bsp_display_unlock();
}

void SegaEmulator::setIndexingOverlayVisible(bool visible)
{
    if (_indexOverlay == nullptr) {
        return;
    }

    if (visible) {
        lv_obj_clear_flag(_indexOverlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_indexOverlay);
    } else {
        lv_obj_add_flag(_indexOverlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void SegaEmulator::renderCurrentFrame()
{
    std::lock_guard<std::mutex> guard(_frameBufferMutex);
    lv_color_t *targetBuffer = _canvasBackBuffer;
    if (targetBuffer == nullptr) {
        return;
    }

    if (_currentCore == EmulatorCore::Gwenesis) {
        const int srcWidth = std::max(1, sega_gwenesis_get_screen_width());
        const int srcHeight = std::max(1, sega_gwenesis_get_screen_height());
        const uint16_t *palette = sega_gwenesis_get_palette();
        const uint8_t *src = _emulatorBuffer + SEGA_GWENESIS_FRAME_OFFSET;

        int dstWidth = kCanvasWidth;
        int dstHeight = (srcHeight * kCanvasWidth) / srcWidth;
        if (dstHeight > kCanvasHeight) {
            dstHeight = kCanvasHeight;
            dstWidth = (srcWidth * kCanvasHeight) / srcHeight;
        }
        const int dstOffsetX = (kCanvasWidth - dstWidth) / 2;
        const int dstOffsetY = (kCanvasHeight - dstHeight) / 2;

        for (int i = 0; i < (kCanvasWidth * kCanvasHeight); ++i) {
            targetBuffer[i].full = 0;
        }

        for (int y = 0; y < dstHeight; ++y) {
            const int srcY = (y * srcHeight) / dstHeight;
            for (int x = 0; x < dstWidth; ++x) {
                const int srcX = (x * srcWidth) / dstWidth;
                const uint8_t index = src[srcY * SEGA_GWENESIS_FRAME_STRIDE + srcX];
                targetBuffer[(dstOffsetY + y) * kCanvasWidth + dstOffsetX + x].full = palette[index];
            }
        }

        if (!_framePresentationQueued.exchange(true)) {
            lv_async_call(presentFrameAsync, this);
        }
        return;
    }

    uint16 palette[PALETTE_SIZE] = {0};
    render_copy_palette(palette);

    const int srcWidth = bitmap.viewport.w > 0 ? bitmap.viewport.w : SMS_WIDTH;
    const int srcHeight = bitmap.viewport.h > 0 ? bitmap.viewport.h : SMS_HEIGHT;
    const int srcOffsetX = bitmap.viewport.x;
    const int srcOffsetY = bitmap.viewport.y;
    const uint8_t *src = _emulatorBuffer + (srcOffsetY * bitmap.pitch) + srcOffsetX;

    int dstWidth = kCanvasWidth;
    int dstHeight = (srcHeight * kCanvasWidth) / srcWidth;
    if (dstHeight > kCanvasHeight) {
        dstHeight = kCanvasHeight;
        dstWidth = (srcWidth * kCanvasHeight) / srcHeight;
    }
    const int dstOffsetX = (kCanvasWidth - dstWidth) / 2;
    const int dstOffsetY = (kCanvasHeight - dstHeight) / 2;

    for (int i = 0; i < kCanvasWidth * kCanvasHeight; ++i) {
        targetBuffer[i].full = 0;
    }

    for (int y = 0; y < dstHeight; ++y) {
        const int srcY = (y * srcHeight) / dstHeight;
        for (int x = 0; x < dstWidth; ++x) {
            const int srcX = (x * srcWidth) / dstWidth;
            const uint8_t index = src[srcY * bitmap.pitch + srcX];
            targetBuffer[(dstOffsetY + y) * kCanvasWidth + dstOffsetX + x].full = palette[index & PIXEL_MASK];
        }
    }

    if (!_framePresentationQueued.exchange(true)) {
        lv_async_call(presentFrameAsync, this);
    }
}

void SegaEmulator::presentFrameAsync(void *context)
{
    auto *app = static_cast<SegaEmulator *>(context);
    if (app != nullptr) {
        app->presentFrameOnUiThread();
    }
}

void SegaEmulator::presentFrameOnUiThread()
{
    std::lock_guard<std::mutex> guard(_frameBufferMutex);
    if ((_canvas == nullptr) || (_canvasFrontBuffer == nullptr) || (_canvasBackBuffer == nullptr)) {
        _framePresentationQueued.store(false);
        return;
    }

    std::swap(_canvasFrontBuffer, _canvasBackBuffer);

    bsp_display_lock(0);
    lv_canvas_set_buffer(_canvas, _canvasFrontBuffer, kCanvasWidth, kCanvasHeight, LV_IMG_CF_TRUE_COLOR);
    lv_obj_invalidate(_canvas);
    bsp_display_unlock();

    _framePresentationQueued.store(false);
}

void SegaEmulator::finishEmulationAsync(void *context)
{
    auto *app = static_cast<SegaEmulator *>(context);
    if (app != nullptr) {
        app->finishEmulationOnUiThread();
    }
}

void SegaEmulator::finishEmulationOnUiThread()
{
    if (!_pendingBrowserStatus.empty()) {
        setBrowserStatus(_pendingBrowserStatus.c_str());
    }
    if (!_closingApp.load() && (lv_scr_act() == _playerScreen)) {
        lv_scr_load(_browserScreen);
    }
    _finishUiQueued.store(false);
}

void SegaEmulator::updateSmsInputState()
{
    const uint32_t state = _inputMask.load();
    input.pad[0] = 0;
    input.pad[1] = 0;
    input.system = 0;

    if (state & kInputUp) {
        input.pad[0] |= INPUT_UP;
    }
    if (state & kInputDown) {
        input.pad[0] |= INPUT_DOWN;
    }
    if (state & kInputLeft) {
        input.pad[0] |= INPUT_LEFT;
    }
    if (state & kInputRight) {
        input.pad[0] |= INPUT_RIGHT;
    }
    if (state & kInputButton1) {
        input.pad[0] |= INPUT_BUTTON1;
    }
    if (state & kInputButton2) {
        input.pad[0] |= INPUT_BUTTON2;
    }

    if (state & kInputStart) {
        if (IS_GG) {
            input.system |= INPUT_START;
        } else {
            input.system |= INPUT_PAUSE;
        }
    }
}

void SegaEmulator::cachePlayerControlRegions()
{
    _controlRegions.clear();

    const struct {
        lv_obj_t *button;
        uint32_t mask;
    } controls[] = {
        {_upButton, kInputUp},
        {_downButton, kInputDown},
        {_leftButton, kInputLeft},
        {_rightButton, kInputRight},
        {_buttonA, kInputButton1},
        {_buttonB, kInputButton2},
        {_buttonC, kInputButton3},
        {_startButton, kInputStart},
    };

    lv_obj_update_layout(_playerScreen);
    _controlRegions.reserve(sizeof(controls) / sizeof(controls[0]));
    for (const auto &control : controls) {
        if (control.button == nullptr) {
            continue;
        }

        lv_area_t area = {};
        lv_obj_get_coords(control.button, &area);
        _controlRegions.push_back(ControlRegion{.mask = control.mask, .area = area});
    }
}

bool SegaEmulator::readTouchInputMask(uint32_t &mask) const
{
    mask = 0;

    esp_lcd_touch_handle_t touchHandle = bsp_display_get_touch_handle();
    if ((touchHandle == nullptr) || _controlRegions.empty()) {
        return false;
    }

    esp_lcd_touch_point_data_t points[CONFIG_ESP_LCD_TOUCH_MAX_POINTS] = {};
    uint8_t pointCount = 0;
    if ((esp_lcd_touch_read_data(touchHandle) != ESP_OK) ||
        (esp_lcd_touch_get_data(touchHandle, points, &pointCount, CONFIG_ESP_LCD_TOUCH_MAX_POINTS) != ESP_OK)) {
        return false;
    }

    for (uint8_t pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
        for (const ControlRegion &region : _controlRegions) {
            if (point_in_area(points[pointIndex].x, points[pointIndex].y, region.area)) {
                mask |= region.mask;
            }
        }
    }

    return true;
}

bool SegaEmulator::setupAudio(int sampleRate)
{
    bsp_extra_codec_dev_stop();
    vTaskDelay(pdMS_TO_TICKS(50));

    if (bsp_extra_codec_init() != ESP_OK) {
        return false;
    }
    if (bsp_extra_codec_set_fs_play(sampleRate, 16, I2S_SLOT_MODE_STEREO) != ESP_OK) {
        return false;
    }

    bsp_extra_codec_volume_set(60, nullptr);
    bsp_extra_codec_mute_set(false);
    return true;
}

void SegaEmulator::teardownAudio()
{
    bsp_extra_codec_dev_stop();
}

bool SegaEmulator::loadBatterySave()
{
    const SegaString savePath = buildSavePath();
    FILE *file = fopen(savePath.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }

    const size_t bytesRead = fread(cart.sram, 1, 0x8000, file);
    fclose(file);
    if (bytesRead > 0) {
        sms.save = 1;
        ESP_LOGI(kTag, "Loaded battery save from %s", savePath.c_str());
        return true;
    }
    return false;
}

void SegaEmulator::saveBatterySave()
{
    if ((cart.sram == nullptr) || !sms.save) {
        return;
    }

    const SegaString savePath = buildSavePath();
    FILE *file = fopen(savePath.c_str(), "wb");
    if (file == nullptr) {
        ESP_LOGW(kTag, "Failed to save SRAM to %s", savePath.c_str());
        return;
    }

    fwrite(cart.sram, 1, 0x8000, file);
    fclose(file);
}

SegaString SegaEmulator::buildSavePath() const
{
    return _activeRomPath + ".sav";
}

bool SegaEmulator::loadIndexFile()
{
    if (!ensure_sd_rom_root_available(true)) {
        return false;
    }

    if (!file_exists(kIndexFilePath) && file_exists(kLegacyIndexFilePath)) {
        FILE *legacy = fopen(kLegacyIndexFilePath, "rb");
        FILE *migrated = fopen(kIndexFilePath, "wb");
        if ((legacy != nullptr) && (migrated != nullptr)) {
            char buffer[512];
            size_t bytesRead = 0;
            while ((bytesRead = fread(buffer, 1, sizeof(buffer), legacy)) > 0) {
                fwrite(buffer, 1, bytesRead, migrated);
            }
        }
        if (legacy != nullptr) {
            fclose(legacy);
        }
        if (migrated != nullptr) {
            fclose(migrated);
            unlink(kLegacyIndexFilePath);
            unlink(kLegacyIndexTempFilePath);
        }
    }

    if (!file_exists(kIndexFilePath)) {
        return false;
    }

    FILE *file = fopen(kIndexFilePath, "rb");
    if (file == nullptr) {
        return false;
    }

    SegaVector<RomEntry> loadedEntries;
    char line[1024];
    bool headerSeen = false;

    while (fgets(line, sizeof(line), file) != nullptr) {
        size_t lineLength = strlen(line);
        while ((lineLength > 0) && ((line[lineLength - 1] == '\n') || (line[lineLength - 1] == '\r'))) {
            line[--lineLength] = '\0';
        }

        if (line[0] == '\0') {
            continue;
        }

        if (!headerSeen) {
            headerSeen = strcmp(line, "JC4880_SEGA_INDEX_V1") == 0;
            continue;
        }

        char *separator = strchr(line, '\t');
        if (separator == nullptr) {
            continue;
        }

        *separator = '\0';
        loadedEntries.push_back(RomEntry{
            .name = line,
            .path = separator + 1,
            .button = nullptr,
        });
    }

    fclose(file);
    if (!headerSeen) {
        return false;
    }

    _romEntries = std::move(loadedEntries);
    setBrowserStatus(_romEntries.empty() ?
        "Loaded cached index. No games were indexed yet. Tap Refresh to reindex." :
        "Loaded cached game index.");
    return true;
}

bool SegaEmulator::saveIndexFile(const SegaVector<RomEntry> &entries) const
{
    if (!ensure_sd_rom_root_available(true)) {
        return false;
    }

    FILE *file = fopen(kIndexTempFilePath, "wb");
    if (file == nullptr) {
        return false;
    }

    fprintf(file, "JC4880_SEGA_INDEX_V1\n");
    for (const RomEntry &entry : entries) {
        fprintf(file, "%s\t%s\n", entry.name.c_str(), entry.path.c_str());
    }

    fclose(file);
    unlink(kIndexFilePath);
    const bool renamed = rename(kIndexTempFilePath, kIndexFilePath) == 0;
    if (renamed) {
        unlink(kLegacyIndexFilePath);
        unlink(kLegacyIndexTempFilePath);
    }
    return renamed;
}

void SegaEmulator::setSearchKeyboardVisible(bool visible)
{
    if ((_searchKeyboard == nullptr) || (_romList == nullptr)) {
        return;
    }

    if (visible) {
        lv_keyboard_set_textarea(_searchKeyboard, _searchField);
        lv_obj_clear_flag(_searchKeyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(_romList, kBrowserListHeightWithKeyboard);
        if (_prevPageButton != nullptr) {
            lv_obj_add_flag(_prevPageButton, LV_OBJ_FLAG_HIDDEN);
        }
        if (_nextPageButton != nullptr) {
            lv_obj_add_flag(_nextPageButton, LV_OBJ_FLAG_HIDDEN);
        }
        if (_pageLabel != nullptr) {
            lv_obj_add_flag(_pageLabel, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(_searchKeyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(_romList, kBrowserListHeight);
        if (_prevPageButton != nullptr) {
            lv_obj_clear_flag(_prevPageButton, LV_OBJ_FLAG_HIDDEN);
        }
        if (_nextPageButton != nullptr) {
            lv_obj_clear_flag(_nextPageButton, LV_OBJ_FLAG_HIDDEN);
        }
        if (_pageLabel != nullptr) {
            lv_obj_clear_flag(_pageLabel, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

bool SegaEmulator::matchesRomFilter(const RomEntry &rom) const
{
    if (_romFilter.empty()) {
        return true;
    }

    return to_lower(rom.name).find(to_lower(_romFilter)) != std::string::npos;
}

bool SegaEmulator::hasSupportedExtension(const SegaString &path)
{
    return has_extension(path, ".sms") ||
           has_extension(path, ".gg") ||
           has_extension(path, ".sg") ||
           has_extension(path, ".md") ||
           has_extension(path, ".gen") ||
           has_extension(path, ".bin") ||
           has_extension(path, ".smd");
}

SegaEmulator::EmulatorCore SegaEmulator::getCoreForPath(const SegaString &path)
{
    if (has_extension(path, ".md") ||
        has_extension(path, ".gen") ||
        has_extension(path, ".bin") ||
        has_extension(path, ".smd")) {
        return EmulatorCore::Gwenesis;
    }
    return EmulatorCore::SmsPlus;
}

void SegaEmulator::onRomSelected(lv_event_t *event)
{
    SegaEmulator *app = static_cast<SegaEmulator *>(lv_event_get_user_data(event));
    lv_obj_t *target = lv_event_get_target(event);
    for (const RomEntry &rom : app->_romEntries) {
        if (rom.button == target) {
            app->startRom(rom.path, rom.name);
            return;
        }
    }
}

void SegaEmulator::onRefreshClicked(lv_event_t *event)
{
    SegaEmulator *app = static_cast<SegaEmulator *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->startIndexing(true);
    }
}

void SegaEmulator::onSearchChanged(lv_event_t *event)
{
    SegaEmulator *app = static_cast<SegaEmulator *>(lv_event_get_user_data(event));
    if ((app == nullptr) || (app->_searchField == nullptr) || app->_indexing.load()) {
        return;
    }

    app->_romFilter = lv_textarea_get_text(app->_searchField);
    app->_currentPage = 0;
    app->rebuildRomList();
}

void SegaEmulator::onSearchFieldEvent(lv_event_t *event)
{
    SegaEmulator *app = static_cast<SegaEmulator *>(lv_event_get_user_data(event));
    if ((app == nullptr) || app->_indexing.load()) {
        return;
    }

    app->setSearchKeyboardVisible(true);
}

void SegaEmulator::onSearchKeyboardEvent(lv_event_t *event)
{
    SegaEmulator *app = static_cast<SegaEmulator *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if ((code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        app->setSearchKeyboardVisible(false);
        if (app->_searchField != nullptr) {
            lv_obj_clear_state(app->_searchField, LV_STATE_FOCUSED);
        }
    }
}

void SegaEmulator::onInitialIndexPromptEvent(lv_event_t *event)
{
    SegaEmulator *app = static_cast<SegaEmulator *>(lv_event_get_user_data(event));
    lv_obj_t *msgbox = lv_event_get_current_target(event);
    const lv_event_code_t code = lv_event_get_code(event);

    if (app == nullptr) {
        return;
    }

    if ((code == LV_EVENT_DELETE) && (app->_indexPromptMessageBox == msgbox)) {
        app->_indexPromptMessageBox = nullptr;
        return;
    }

    if ((code != LV_EVENT_VALUE_CHANGED) || (msgbox == nullptr)) {
        return;
    }

    const char *buttonText = lv_msgbox_get_active_btn_text(msgbox);
    lv_msgbox_close_async(msgbox);
    app->_indexPromptMessageBox = nullptr;

    if ((buttonText != nullptr) && (strcmp(buttonText, "Scan") == 0)) {
        app->startIndexing(false);
    } else {
        app->setBrowserStatus("Index not built. Tap Refresh to scan storage and create the game index.");
    }
}

void SegaEmulator::onPrevPageClicked(lv_event_t *event)
{
    auto *app = static_cast<SegaEmulator *>(lv_event_get_user_data(event));
    if ((app == nullptr) || app->_indexing.load() || (app->_currentPage == 0)) {
        return;
    }

    app->_currentPage--;
    app->rebuildRomList();
}

void SegaEmulator::onNextPageClicked(lv_event_t *event)
{
    auto *app = static_cast<SegaEmulator *>(lv_event_get_user_data(event));
    if ((app == nullptr) || app->_indexing.load()) {
        return;
    }

    app->_currentPage++;
    app->rebuildRomList();
}

void SegaEmulator::indexingTaskEntry(void *context)
{
    IndexTaskContext *taskContext = static_cast<IndexTaskContext *>(context);
    if ((taskContext == nullptr) || (taskContext->app == nullptr)) {
        delete taskContext;
        vTaskDelete(nullptr);
        return;
    }

    SegaEmulator *app = taskContext->app;
    const bool forceReindex = taskContext->forceReindex;
    delete taskContext;
    app->indexingTask(forceReindex);
}

void SegaEmulator::indexingProgressAsync(void *context)
{
    auto *app = static_cast<SegaEmulator *>(context);
    if (app != nullptr) {
        app->updateIndexingProgressOnUiThread();
    }
}

void SegaEmulator::indexingFinishedAsync(void *context)
{
    auto *app = static_cast<SegaEmulator *>(context);
    if (app != nullptr) {
        app->finishIndexingOnUiThread();
    }
}

void SegaEmulator::onControlButtonEvent(lv_event_t *event)
{
    ControlBinding *binding = static_cast<ControlBinding *>(lv_event_get_user_data(event));
    if ((binding == nullptr) || (binding->app == nullptr)) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        binding->app->_inputMask.fetch_or(binding->mask);
    } else if ((code == LV_EVENT_RELEASED) || (code == LV_EVENT_PRESS_LOST)) {
        binding->app->_inputMask.fetch_and(~binding->mask);
    }
}

void SegaEmulator::onBrowserScreenDeleted(lv_event_t *event)
{
    auto *app = static_cast<SegaEmulator *>(lv_event_get_user_data(event));
    if ((app == nullptr) || (lv_event_get_target(event) != app->_browserScreen)) {
        return;
    }

    app->resetBrowserUiPointers();
}

void SegaEmulator::onPlayerScreenDeleted(lv_event_t *event)
{
    auto *app = static_cast<SegaEmulator *>(lv_event_get_user_data(event));
    if ((app == nullptr) || (lv_event_get_target(event) != app->_playerScreen)) {
        return;
    }

    app->resetPlayerUiPointers();
}