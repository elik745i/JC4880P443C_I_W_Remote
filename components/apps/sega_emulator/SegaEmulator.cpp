#include "SegaEmulator.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
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
constexpr const char *kSavedGamesRootPath = BSP_SD_MOUNT_POINT "/saved_games";

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
constexpr lv_coord_t kFpsPanelY = 170;
constexpr lv_coord_t kBrowserListY = 228;
constexpr lv_coord_t kSearchFieldWidth = 352;
constexpr lv_coord_t kSearchButtonWidth = 88;
constexpr lv_coord_t kSearchControlHeight = 48;
constexpr lv_coord_t kPageButtonWidth = 88;
constexpr lv_coord_t kPageButtonHeight = 42;
constexpr lv_coord_t kPageNavBottomOffset = -8;
constexpr size_t kRomPageSize = 18;
constexpr int kGenesisOutputSampleRate = CODEC_DEFAULT_SAMPLE_RATE;

constexpr lv_coord_t kOverlayButtonCornerRadius = 18;
constexpr lv_opa_t kOverlayButtonOpacity = LV_OPA_50;
constexpr lv_coord_t kSideButtonWidth = 70;
constexpr lv_coord_t kSideButtonHeight = 122;
constexpr lv_coord_t kSmallOverlayButtonWidth = 64;
constexpr lv_coord_t kSmallOverlayButtonHeight = 104;
constexpr size_t kSaveSlotPreviewWidth = 96;
constexpr size_t kSaveSlotPreviewHeight = 54;
constexpr size_t kMaxVisibleSaveSlots = 5;

constexpr size_t kSmsFrameBufferSize = SMS_WIDTH * SMS_HEIGHT;
constexpr size_t kGenesisFrameBufferSize = SEGA_GWENESIS_FRAME_OFFSET +
                                           (SEGA_GWENESIS_FRAME_STRIDE * SEGA_GWENESIS_FRAME_HEIGHT *
                                            SEGA_GWENESIS_FRAME_BYTES_PER_PIXEL);
constexpr size_t kMaxPpaSourcePixels = SEGA_GWENESIS_FRAME_STRIDE * SEGA_GWENESIS_FRAME_HEIGHT;

#define ALIGN_UP_BY(num, align) (((num) + ((align) - 1)) & ~((align) - 1))

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

void style_overlay_button(lv_obj_t *button, lv_coord_t width, lv_coord_t height)
{
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, kOverlayButtonCornerRadius, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x0b1020), 0);
    lv_obj_set_style_bg_opa(button, kOverlayButtonOpacity, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2a3352), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, lv_color_hex(0xb5c1ff), 0);
    lv_obj_set_style_border_opa(button, LV_OPA_50, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_text_color(button, lv_color_white(), 0);
    lv_obj_set_style_pad_all(button, 0, 0);
}

void set_button_label(lv_obj_t *button, const char *label)
{
    lv_obj_t *text = lv_label_create(button);
    lv_label_set_text(text, label);
    lv_obj_center(text);
}

void set_rotated_button_label(lv_obj_t *button, const char *label)
{
    lv_obj_t *text = lv_label_create(button);
    lv_label_set_text(text, label);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_transform_angle(text, 900, 0);
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

uint32_t hash_path_component(const SegaString &value)
{
    uint32_t hash = 2166136261u;
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= 16777619u;
    }
    return hash;
}

SegaString sanitize_path_component(const SegaString &value)
{
    SegaString sanitized;
    sanitized.reserve(value.size());

    bool previousUnderscore = false;
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            sanitized.push_back(static_cast<char>(ch));
            previousUnderscore = false;
        } else if (!previousUnderscore) {
            sanitized.push_back('_');
            previousUnderscore = true;
        }
    }

    while (!sanitized.empty() && (sanitized.front() == '_')) {
        sanitized.erase(sanitized.begin());
    }
    while (!sanitized.empty() && (sanitized.back() == '_')) {
        sanitized.pop_back();
    }

    if (sanitized.empty()) {
        sanitized = "game";
    }

    return sanitized;
}

bool ensure_directory_exists(const SegaString &path)
{
    if (path.empty()) {
        return false;
    }

    if (is_directory_path(path)) {
        return true;
    }

    if (mkdir(path.c_str(), 0775) == 0) {
        return true;
    }

    return errno == EEXIST;
}

uint64_t parse_save_slot_key(const char *filename)
{
    if (filename == nullptr) {
        return 0;
    }

    const char *digits = strrchr(filename, '_');
    if (digits == nullptr) {
        digits = filename;
    } else {
        ++digits;
    }

    return strtoull(digits, nullptr, 10);
}

BaseType_t create_emulator_task_prefer_internal(TaskFunction_t task,
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
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) == pdPASS) {
        ESP_LOGI(kTag, "Started %s with an internal RAM stack", name);
        return pdPASS;
    }

    ESP_LOGW(kTag,
             "Falling back to a PSRAM-backed stack for %s. Internal free=%u largest=%u PSRAM free=%u",
             name,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    return xTaskCreatePinnedToCoreWithCaps(task,
                                           name,
                                           stack_depth,
                                           arg,
                                           priority,
                                           task_handle,
                                           core_id,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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

void *aligned_alloc_prefer_internal(size_t alignment, size_t size)
{
    void *ptr = heap_caps_aligned_alloc(alignment, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ptr != nullptr) {
        return ptr;
    }

    return heap_caps_aligned_alloc(alignment, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void *alloc_prefer_internal(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ptr != nullptr) {
        return ptr;
    }

    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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
        const uint64_t sourcePosition = static_cast<uint64_t>(frame) * static_cast<uint64_t>(sourceRate);
        const size_t sourceIndex = std::min(static_cast<size_t>(sourcePosition / static_cast<uint64_t>(targetRate)),
                                            sourceFrames - 1);
        const size_t nextIndex = std::min(sourceIndex + 1, sourceFrames - 1);
        const uint32_t fraction = static_cast<uint32_t>(sourcePosition % static_cast<uint64_t>(targetRate));
        const uint32_t inverseFraction = static_cast<uint32_t>(targetRate) - fraction;

        const int left0 = source[sourceIndex * 2];
        const int right0 = source[sourceIndex * 2 + 1];
        const int left1 = source[nextIndex * 2];
        const int right1 = source[nextIndex * 2 + 1];

        destination[frame * 2] = static_cast<int16_t>(((left0 * static_cast<int>(inverseFraction)) +
                                                       (left1 * static_cast<int>(fraction))) /
                                                      targetRate);
        destination[frame * 2 + 1] = static_cast<int16_t>(((right0 * static_cast<int>(inverseFraction)) +
                                                           (right1 * static_cast<int>(fraction))) /
                                                          targetRate);
    }

    return targetFrames;
}

size_t aligned_buffer_size(size_t size, size_t alignment)
{
    return (alignment > 0) ? ALIGN_UP_BY(size, alignment) : size;
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
    if (_ppaSourceBuffer != nullptr) {
        heap_caps_free(_ppaSourceBuffer);
        _ppaSourceBuffer = nullptr;
    }
    if (_emulatorBuffer != nullptr) {
        heap_caps_free(_emulatorBuffer);
        _emulatorBuffer = nullptr;
    }
    if (_ppaHandle != nullptr) {
        ppa_unregister_client(_ppaHandle);
        _ppaHandle = nullptr;
    }
}

bool SegaEmulator::init()
{
    _cacheLineSize = CONFIG_CACHE_L2_CACHE_LINE_SIZE;

    const size_t canvasBufferBytes = sizeof(lv_color_t) * kCanvasWidth * kCanvasHeight;
    const size_t ppaSourceBytes = sizeof(lv_color_t) * kMaxPpaSourcePixels;

    _canvasFrontBuffer = static_cast<lv_color_t *>(
        heap_caps_aligned_alloc(_cacheLineSize, canvasBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    _canvasBackBuffer = static_cast<lv_color_t *>(
        heap_caps_aligned_alloc(_cacheLineSize, canvasBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    _ppaSourceBuffer = static_cast<lv_color_t *>(
        aligned_alloc_prefer_internal(_cacheLineSize, ppaSourceBytes));
    _emulatorBuffer = static_cast<uint8_t *>(
        alloc_prefer_internal(std::max(kSmsFrameBufferSize, kGenesisFrameBufferSize)));

    if ((_canvasFrontBuffer == nullptr) || (_canvasBackBuffer == nullptr) || (_ppaSourceBuffer == nullptr) || (_emulatorBuffer == nullptr)) {
        ESP_LOGE(kTag, "Failed to allocate emulator frame buffers");
        return false;
    }

    ppa_client_config_t ppaClientConfig = {
        .oper_type = PPA_OPERATION_SRM,
    };
    if (ppa_register_client(&ppaClientConfig, &_ppaHandle) != ESP_OK) {
        ESP_LOGW(kTag, "Failed to register PPA SRM client, using software rendering fallback");
        _ppaHandle = nullptr;
    }

    memset(_canvasFrontBuffer, 0, canvasBufferBytes);
    memset(_canvasBackBuffer, 0, canvasBufferBytes);
    memset(_ppaSourceBuffer, 0, ppaSourceBytes);
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
        setPlayerFullscreen(true);
        lv_scr_load(_playerScreen);
    } else {
        setPlayerFullscreen(false);
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
        setPlayerFullscreen(false);
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

    lv_obj_t *fpsPanel = lv_obj_create(_browserScreen);
    lv_obj_set_size(fpsPanel, 448, 46);
    lv_obj_align(fpsPanel, LV_ALIGN_TOP_LEFT, 8, kFpsPanelY);
    lv_obj_set_style_radius(fpsPanel, 14, 0);
    lv_obj_set_style_bg_color(fpsPanel, lv_color_hex(0x10162a), 0);
    lv_obj_set_style_border_color(fpsPanel, lv_color_hex(0x2f3554), 0);
    lv_obj_set_style_border_width(fpsPanel, 1, 0);
    lv_obj_clear_flag(fpsPanel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *fpsLabel = lv_label_create(fpsPanel);
    lv_label_set_text(fpsLabel, "Show FPS Overlay");
    lv_obj_set_style_text_color(fpsLabel, lv_color_white(), 0);
    lv_obj_align(fpsLabel, LV_ALIGN_LEFT_MID, 12, 0);

    lv_obj_t *fpsToggle = lv_switch_create(fpsPanel);
    lv_obj_align(fpsToggle, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_add_event_cb(fpsToggle, [](lv_event_t *event) {
        auto *app = static_cast<SegaEmulator *>(lv_event_get_user_data(event));
        lv_obj_t *toggle = lv_event_get_target(event);
        if ((app == nullptr) || (toggle == nullptr)) {
            return;
        }

        const bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
        app->_showFrameStats.store(enabled);
        if (app->_playerFps != nullptr) {
            bsp_display_lock(0);
            if (enabled) {
                lv_obj_clear_flag(app->_playerFps, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(app->_playerFps, "EMU 0.0\nDRW 0.0");
            } else {
                lv_obj_add_flag(app->_playerFps, LV_OBJ_FLAG_HIDDEN);
            }
            bsp_display_unlock();
        }
    }, LV_EVENT_VALUE_CHANGED, this);

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
    lv_obj_align(_romList, LV_ALIGN_TOP_MID, 0, kBrowserListY);
    lv_obj_set_style_bg_color(_romList, lv_color_hex(0x10162a), 0);
    lv_obj_set_style_border_color(_romList, lv_color_hex(0x2f3554), 0);
    lv_obj_set_style_border_width(_romList, 1, 0);
    lv_obj_set_style_pad_row(_romList, 8, 0);
    lv_obj_move_foreground(fpsPanel);

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
    lv_obj_set_style_bg_color(_playerScreen, lv_color_black(), 0);
    lv_obj_set_style_border_width(_playerScreen, 0, 0);
    lv_obj_set_style_pad_all(_playerScreen, 0, 0);

    _canvas = lv_canvas_create(_playerScreen);
    lv_canvas_set_buffer(_canvas, _canvasFrontBuffer, kCanvasWidth, kCanvasHeight, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(_canvas, lv_color_black(), LV_OPA_COVER);
    lv_obj_set_size(_canvas, kCanvasWidth, kCanvasHeight);
    lv_obj_center(_canvas);

    _playerFps = lv_label_create(_playerScreen);
    lv_obj_set_style_text_color(_playerFps, lv_color_white(), 0);
    lv_obj_set_style_text_font(_playerFps, &lv_font_montserrat_20, 0);
    lv_obj_set_style_bg_color(_playerFps, lv_color_hex(0x05070f), 0);
    lv_obj_set_style_bg_opa(_playerFps, LV_OPA_50, 0);
    lv_obj_set_style_pad_left(_playerFps, 8, 0);
    lv_obj_set_style_pad_right(_playerFps, 8, 0);
    lv_obj_set_style_pad_top(_playerFps, 4, 0);
    lv_obj_set_style_pad_bottom(_playerFps, 4, 0);
    lv_obj_set_style_radius(_playerFps, 10, 0);
    lv_obj_set_style_transform_angle(_playerFps, 900, 0);
    lv_label_set_text(_playerFps, "EMU 0.0\nDRW 0.0");
    lv_obj_align(_playerFps, LV_ALIGN_RIGHT_MID, -10, 0);
    if (!_showFrameStats.load()) {
        lv_obj_add_flag(_playerFps, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *dpad = lv_obj_create(_playerScreen);
    lv_obj_set_size(dpad, 188, 244);
    lv_obj_align(dpad, LV_ALIGN_TOP_LEFT, 10, 14);
    lv_obj_set_style_bg_opa(dpad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dpad, 0, 0);
    lv_obj_set_style_pad_all(dpad, 0, 0);
    lv_obj_clear_flag(dpad, LV_OBJ_FLAG_SCROLLABLE);

    _upButton = lv_btn_create(dpad);
    style_overlay_button(_upButton, 64, 94);
    lv_obj_align(_upButton, LV_ALIGN_TOP_MID, 0, 0);
    set_rotated_button_label(_upButton, "Left");

    _leftButton = lv_btn_create(dpad);
    style_overlay_button(_leftButton, 64, 94);
    lv_obj_align(_leftButton, LV_ALIGN_LEFT_MID, 0, 0);
    set_rotated_button_label(_leftButton, "Down");

    _rightButton = lv_btn_create(dpad);
    style_overlay_button(_rightButton, 64, 94);
    lv_obj_align(_rightButton, LV_ALIGN_RIGHT_MID, 0, 0);
    set_rotated_button_label(_rightButton, "Up");

    _downButton = lv_btn_create(dpad);
    style_overlay_button(_downButton, 64, 94);
    lv_obj_align(_downButton, LV_ALIGN_BOTTOM_MID, 0, 0);
    set_rotated_button_label(_downButton, "Right");

    lv_obj_t *abcActions = lv_obj_create(_playerScreen);
    lv_obj_set_size(abcActions, 104, 338);
    lv_obj_align(abcActions, LV_ALIGN_BOTTOM_LEFT, 10, -14);
    lv_obj_set_style_bg_opa(abcActions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(abcActions, 0, 0);
    lv_obj_set_style_pad_all(abcActions, 0, 0);
    lv_obj_clear_flag(abcActions, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *systemActions = lv_obj_create(_playerScreen);
    lv_obj_set_size(systemActions, 306, 160);
    lv_obj_align(systemActions, LV_ALIGN_BOTTOM_RIGHT, -10, -14);
    lv_obj_set_style_bg_opa(systemActions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(systemActions, 0, 0);
    lv_obj_set_style_pad_all(systemActions, 0, 0);
    lv_obj_clear_flag(systemActions, LV_OBJ_FLAG_SCROLLABLE);

    _buttonA = lv_btn_create(abcActions);
    style_overlay_button(_buttonA, kSmallOverlayButtonWidth, kSmallOverlayButtonHeight);
    lv_obj_align(_buttonA, LV_ALIGN_TOP_MID, 0, 0);
    set_rotated_button_label(_buttonA, "A");

    _buttonB = lv_btn_create(abcActions);
    style_overlay_button(_buttonB, kSmallOverlayButtonWidth, kSmallOverlayButtonHeight);
    lv_obj_align_to(_buttonB, _buttonA, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    set_rotated_button_label(_buttonB, "B");

    _buttonC = lv_btn_create(abcActions);
    style_overlay_button(_buttonC, kSmallOverlayButtonWidth, kSmallOverlayButtonHeight);
    lv_obj_align_to(_buttonC, _buttonB, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    set_rotated_button_label(_buttonC, "C");

    _startButton = lv_btn_create(systemActions);
    style_overlay_button(_startButton, kSideButtonWidth, kSideButtonHeight);
    lv_obj_align(_startButton, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    set_rotated_button_label(_startButton, "Start");

    _loadButton = lv_btn_create(systemActions);
    style_overlay_button(_loadButton, kSideButtonWidth, kSideButtonHeight);
    lv_obj_align_to(_loadButton, _startButton, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    set_rotated_button_label(_loadButton, "Load");

    _saveButton = lv_btn_create(systemActions);
    style_overlay_button(_saveButton, kSideButtonWidth, kSideButtonHeight);
    lv_obj_align_to(_saveButton, _loadButton, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    set_rotated_button_label(_saveButton, "Save");

    _exitButton = lv_btn_create(systemActions);
    style_overlay_button(_exitButton, kSideButtonWidth, kSideButtonHeight);
    lv_obj_align(_exitButton, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(_exitButton, lv_color_hex(0x3a0f18), 0);
    lv_obj_set_style_bg_color(_exitButton, lv_color_hex(0x6b1a28), LV_STATE_PRESSED);
    set_rotated_button_label(_exitButton, "Exit");

    lv_obj_add_event_cb(_loadButton, onPlayerActionEvent, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_saveButton, onPlayerActionEvent, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_exitButton, onPlayerActionEvent, LV_EVENT_CLICKED, this);

    const struct {
        lv_obj_t *button;
        uint32_t mask;
    } controls[] = {
        {_upButton, kInputLeft},
        {_downButton, kInputRight},
        {_leftButton, kInputDown},
        {_rightButton, kInputUp},
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
    releaseSaveSlotEntries(_saveSlotEntries);
    _saveSlotButtonContexts.clear();
    _playerScreen = nullptr;
    _playerStatus = nullptr;
    _playerTitle = nullptr;
    _playerFps = nullptr;
    _canvas = nullptr;
    _loadStateModal = nullptr;
    _upButton = nullptr;
    _downButton = nullptr;
    _leftButton = nullptr;
    _rightButton = nullptr;
    _buttonA = nullptr;
    _buttonB = nullptr;
    _loadButton = nullptr;
    _buttonC = nullptr;
    _startButton = nullptr;
    _loadButton = nullptr;
    _saveButton = nullptr;
    _exitButton = nullptr;
    _controlRegions.clear();

    _controlBindings.clear();
}

void SegaEmulator::releaseUiState()
{
    closeLoadStatePicker();
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
                    .lastPlayedAt = 0,
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

    _romEntries = entries;
    sortRomEntries();
    entries = _romEntries;

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

    if ((_playerScreen == nullptr) || (_canvas == nullptr)) {
        setBrowserStatus("Failed to create the player interface.");
        return;
    }

    _currentCore = getCoreForPath(path);
    _activeRomPath = path;
    _activeRomName = name;
    markRomAsPlayed(path);
    _stopRequested.store(false);
    _inputMask.store(0);
    _saveRequested.store(false);
    _loadRequested.store(false);
    _loadMenuActive.store(false);
    {
        std::lock_guard<std::mutex> guard(_saveStateMutex);
        _pendingLoadStatePath.clear();
    }
    setPlayerLoadButtonEnabled(hasManualSaveState());
    setPlayerStatus("Loading game...");
    lv_canvas_fill_bg(_canvas, lv_color_black(), LV_OPA_COVER);
    setPlayerFullscreen(true);
    lv_scr_load(_playerScreen);

    resetPerformanceStats();

    if (create_emulator_task_prefer_internal(emulatorTaskEntry, "sega_emu", 8192, this, 4, &_emulatorTask, 1) != pdPASS) {
        _emulatorTask = nullptr;
        setBrowserStatus("Failed to start the emulator task.");
        setPlayerFullscreen(false);
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
    constexpr int64_t kPerfLogWindowUs = 2000000;

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
        const bool showFrameStats = _showFrameStats.load();
        if (!setupAudio(kGenesisOutputSampleRate)) {
            finish("Audio initialization failed.");
            return;
        }

        SegaVector<int16_t> nativeMixbuffer(SEGA_GWENESIS_AUDIO_BUFFER_LENGTH * 2);
        SegaVector<int16_t> outputMixbuffer(((kGenesisOutputSampleRate / 50) + 16) * 2);
        int skipFrames = 0;
        int64_t fpsWindowStartUs = esp_timer_get_time();
        int64_t perfWindowStartUs = fpsWindowStartUs;
        uint32_t emulatedFrameCount = 0;
        uint32_t drawnFrameCount = 0;
        uint32_t perfEmulatedFrameCount = 0;
        uint32_t perfDrawnFrameCount = 0;
        uint32_t perfAudioWriteCount = 0;
        uint64_t coreTotalUs = 0;
        uint64_t renderTotalUs = 0;
        uint64_t audioTotalUs = 0;
        uint32_t coreMaxUs = 0;
        uint32_t renderMaxUs = 0;
        uint32_t audioMaxUs = 0;

        setPlayerStatus("Running Mega Drive emulator");
        setPlayerFps(0, 0);
        sega_gwenesis_set_perf_stats_enabled(showFrameStats);
        if (showFrameStats) {
            sega_gwenesis_reset_perf_stats();
        }

        while (!_stopRequested.load()) {
            if (_loadMenuActive.load()) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }

            const int64_t frameStartUs = esp_timer_get_time();
            const bool drawFrame = (skipFrames == 0);
            uint32_t touchMask = 0;
            if (readTouchInputMask(touchMask)) {
                _inputMask.store(touchMask);
            }

            sega_gwenesis_set_input_mask(_inputMask.load());
            if (showFrameStats) {
                const int64_t coreStartUs = esp_timer_get_time();
                sega_gwenesis_run_frame(drawFrame);
                const uint32_t coreUs = static_cast<uint32_t>(std::max<int64_t>(0, esp_timer_get_time() - coreStartUs));
                coreTotalUs += coreUs;
                coreMaxUs = std::max(coreMaxUs, coreUs);
            } else {
                sega_gwenesis_run_frame(drawFrame);
            }
            ++emulatedFrameCount;
            ++perfEmulatedFrameCount;
            if (drawFrame) {
                if (showFrameStats) {
                    const int64_t renderStartUs = esp_timer_get_time();
                    renderCurrentFrame();
                    const uint32_t renderUs = static_cast<uint32_t>(std::max<int64_t>(0, esp_timer_get_time() - renderStartUs));
                    renderTotalUs += renderUs;
                    renderMaxUs = std::max(renderMaxUs, renderUs);
                } else {
                    renderCurrentFrame();
                }
                ++drawnFrameCount;
                ++perfDrawnFrameCount;
            }

            if (_saveRequested.exchange(false)) {
                const bool saved = saveResumeState();
                if (saved) {
                    setPlayerLoadButtonEnabled(true);
                }
                setPlayerStatus(saved ? "State saved." : "Failed to save state.");
            }
            if (_loadRequested.exchange(false)) {
                SegaString pendingPath;
                {
                    std::lock_guard<std::mutex> guard(_saveStateMutex);
                    pendingPath = _pendingLoadStatePath;
                }

                if (!pendingPath.empty() && loadResumeState(pendingPath)) {
                    setPlayerStatus("State loaded.");
                } else {
                    setPlayerStatus("No saved state available.");
                    setPlayerLoadButtonEnabled(false);
                }
            }

            const int genesisRefreshRate = std::max(1, sega_gwenesis_get_refresh_rate());
            const size_t nativeSampleCount = sega_gwenesis_mix_audio_stereo(nativeMixbuffer.data(), nativeMixbuffer.size() / 2);
            const size_t outputSampleCount = resample_stereo_frames(nativeMixbuffer.data(),
                                                                    nativeSampleCount,
                                                                    genesisInputSampleRate,
                                                                    kGenesisOutputSampleRate,
                                                                    outputMixbuffer.data(),
                                                                    outputMixbuffer.size() / 2);
            if (outputSampleCount > 0) {
                size_t written = 0;
                if (showFrameStats) {
                    const int64_t audioStartUs = esp_timer_get_time();
                    bsp_extra_i2s_write(outputMixbuffer.data(), outputSampleCount * 2 * sizeof(int16_t), &written, 0);
                    const uint32_t audioUs = static_cast<uint32_t>(std::max<int64_t>(0, esp_timer_get_time() - audioStartUs));
                    audioTotalUs += audioUs;
                    audioMaxUs = std::max(audioMaxUs, audioUs);
                } else {
                    bsp_extra_i2s_write(outputMixbuffer.data(), outputSampleCount * 2 * sizeof(int16_t), &written, 0);
                }
                ++perfAudioWriteCount;
            }

            const int64_t elapsedUs = esp_timer_get_time() - frameStartUs;
            const int64_t frameBudgetUs = (1000000LL / genesisRefreshRate) + 1500LL;
            if (skipFrames == 0) {
                if (elapsedUs > frameBudgetUs) {
                    const int64_t overrunFrames = (elapsedUs + frameBudgetUs - 1) / frameBudgetUs;
                    skipFrames = static_cast<int>(std::min<int64_t>(2, std::max<int64_t>(1, overrunFrames - 1)));
                }
            } else {
                skipFrames--;
            }

            const int64_t nowUs = esp_timer_get_time();
            const int64_t fpsWindowUs = nowUs - fpsWindowStartUs;
            if (showFrameStats && (fpsWindowUs >= 500000)) {
                const uint32_t emulatedFpsTimes10 = static_cast<uint32_t>((emulatedFrameCount * 10000000ULL) / fpsWindowUs);
                const uint32_t drawnFpsTimes10 = static_cast<uint32_t>((drawnFrameCount * 10000000ULL) / fpsWindowUs);
                setPlayerFps(emulatedFpsTimes10, drawnFpsTimes10);
                fpsWindowStartUs = nowUs;
                emulatedFrameCount = 0;
                drawnFrameCount = 0;
            }

            const int64_t perfWindowUs = nowUs - perfWindowStartUs;
            if (showFrameStats && (perfWindowUs >= kPerfLogWindowUs)) {
                const uint64_t presentTotalUs = _presentTotalUs.exchange(0);
                const uint32_t presentCount = _presentCount.exchange(0);
                const uint32_t presentMaxUs = _presentMaxUs.exchange(0);
                const uint32_t presentQueueBusyCount = _presentQueueBusyCount.exchange(0);
                sega_gwenesis_perf_stats_t corePerfStats = {};
                sega_gwenesis_get_perf_stats(&corePerfStats);
                sega_gwenesis_reset_perf_stats();
                const uint32_t emuFpsTimes10 = static_cast<uint32_t>((perfEmulatedFrameCount * 10000000ULL) / perfWindowUs);
                const uint32_t drawFpsTimes10 = static_cast<uint32_t>((perfDrawnFrameCount * 10000000ULL) / perfWindowUs);
                const uint32_t coreAvgMs10 = perfEmulatedFrameCount > 0 ? static_cast<uint32_t>((coreTotalUs * 10ULL) / (1000ULL * perfEmulatedFrameCount)) : 0;
                const uint32_t renderAvgMs10 = perfDrawnFrameCount > 0 ? static_cast<uint32_t>((renderTotalUs * 10ULL) / (1000ULL * perfDrawnFrameCount)) : 0;
                const uint32_t audioAvgMs10 = perfAudioWriteCount > 0 ? static_cast<uint32_t>((audioTotalUs * 10ULL) / (1000ULL * perfAudioWriteCount)) : 0;
                const uint32_t presentAvgMs10 = presentCount > 0 ? static_cast<uint32_t>((presentTotalUs * 10ULL) / (1000ULL * presentCount)) : 0;
                const uint32_t coreCpuAvgMs10 = corePerfStats.frame_count > 0 ? static_cast<uint32_t>((corePerfStats.cpu_time_us * 10ULL) / (1000ULL * corePerfStats.frame_count)) : 0;
                const uint32_t coreRenderAvgMs10 = corePerfStats.frame_count > 0 ? static_cast<uint32_t>((corePerfStats.render_time_us * 10ULL) / (1000ULL * corePerfStats.frame_count)) : 0;
                const uint32_t coreSynthAvgMs10 = corePerfStats.frame_count > 0 ? static_cast<uint32_t>((corePerfStats.synth_time_us * 10ULL) / (1000ULL * corePerfStats.frame_count)) : 0;
                ESP_LOGI(kTag,
                         "Perf[MD] EMU=%u.%u DRW=%u.%u core=%u.%u/%ums render=%u.%u/%ums audio=%u.%u/%ums present=%u.%u/%ums qbusy=%u cpu=%u.%u/%ums vdp=%u.%u/%ums synth=%u.%u/%ums",
                         static_cast<unsigned>(emuFpsTimes10 / 10),
                         static_cast<unsigned>(emuFpsTimes10 % 10),
                         static_cast<unsigned>(drawFpsTimes10 / 10),
                         static_cast<unsigned>(drawFpsTimes10 % 10),
                         static_cast<unsigned>(coreAvgMs10 / 10),
                         static_cast<unsigned>(coreAvgMs10 % 10),
                         static_cast<unsigned>(coreMaxUs / 1000),
                         static_cast<unsigned>(renderAvgMs10 / 10),
                         static_cast<unsigned>(renderAvgMs10 % 10),
                         static_cast<unsigned>(renderMaxUs / 1000),
                         static_cast<unsigned>(audioAvgMs10 / 10),
                         static_cast<unsigned>(audioAvgMs10 % 10),
                         static_cast<unsigned>(audioMaxUs / 1000),
                         static_cast<unsigned>(presentAvgMs10 / 10),
                         static_cast<unsigned>(presentAvgMs10 % 10),
                         static_cast<unsigned>(presentMaxUs / 1000),
                         static_cast<unsigned>(presentQueueBusyCount),
                         static_cast<unsigned>(coreCpuAvgMs10 / 10),
                         static_cast<unsigned>(coreCpuAvgMs10 % 10),
                         static_cast<unsigned>(corePerfStats.cpu_max_us / 1000),
                         static_cast<unsigned>(coreRenderAvgMs10 / 10),
                         static_cast<unsigned>(coreRenderAvgMs10 % 10),
                         static_cast<unsigned>(corePerfStats.render_max_us / 1000),
                         static_cast<unsigned>(coreSynthAvgMs10 / 10),
                         static_cast<unsigned>(coreSynthAvgMs10 % 10),
                         static_cast<unsigned>(corePerfStats.synth_max_us / 1000));

                perfWindowStartUs = nowUs;
                perfEmulatedFrameCount = 0;
                perfDrawnFrameCount = 0;
                perfAudioWriteCount = 0;
                coreTotalUs = 0;
                renderTotalUs = 0;
                audioTotalUs = 0;
                coreMaxUs = 0;
                renderMaxUs = 0;
                audioMaxUs = 0;
            }
        }

        sega_gwenesis_set_perf_stats_enabled(false);

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
    int skipFrames = 0;
    const int64_t smsRefreshRate = (sms.display == DISPLAY_NTSC) ? 60LL : 50LL;
    const int64_t frameBudgetUs = (1000000LL / smsRefreshRate) + 1500LL;
    int64_t fpsWindowStartUs = esp_timer_get_time();
    int64_t perfWindowStartUs = fpsWindowStartUs;
    uint32_t emulatedFrameCount = 0;
    uint32_t drawnFrameCount = 0;
    uint32_t perfEmulatedFrameCount = 0;
    uint32_t perfDrawnFrameCount = 0;
    uint32_t perfAudioWriteCount = 0;
    uint64_t coreTotalUs = 0;
    uint64_t renderTotalUs = 0;
    uint64_t audioTotalUs = 0;
    uint32_t coreMaxUs = 0;
    uint32_t renderMaxUs = 0;
    uint32_t audioMaxUs = 0;
    const bool showFrameStats = _showFrameStats.load();

    setPlayerStatus("Running SMS/Game Gear emulator");
    setPlayerFps(0, 0);

    while (!_stopRequested.load()) {
        if (_loadMenuActive.load()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        const int64_t frameStartUs = esp_timer_get_time();
        const bool drawFrame = (skipFrames == 0);
        uint32_t touchMask = 0;
        if (readTouchInputMask(touchMask)) {
            _inputMask.store(touchMask);
        }

        updateSmsInputState();
        if (showFrameStats) {
            const int64_t coreStartUs = esp_timer_get_time();
            system_frame(drawFrame ? 0 : 1);
            const uint32_t coreUs = static_cast<uint32_t>(std::max<int64_t>(0, esp_timer_get_time() - coreStartUs));
            coreTotalUs += coreUs;
            coreMaxUs = std::max(coreMaxUs, coreUs);
        } else {
            system_frame(drawFrame ? 0 : 1);
        }
        ++emulatedFrameCount;
        ++perfEmulatedFrameCount;
        if (drawFrame) {
            if (showFrameStats) {
                const int64_t renderStartUs = esp_timer_get_time();
                renderCurrentFrame();
                const uint32_t renderUs = static_cast<uint32_t>(std::max<int64_t>(0, esp_timer_get_time() - renderStartUs));
                renderTotalUs += renderUs;
                renderMaxUs = std::max(renderMaxUs, renderUs);
            } else {
                renderCurrentFrame();
            }
            ++drawnFrameCount;
            ++perfDrawnFrameCount;
        }

        if (_saveRequested.exchange(false)) {
            const bool saved = saveResumeState();
            if (saved) {
                setPlayerLoadButtonEnabled(true);
            }
            setPlayerStatus(saved ? "State saved." : "Failed to save state.");
        }
        if (_loadRequested.exchange(false)) {
            SegaString pendingPath;
            {
                std::lock_guard<std::mutex> guard(_saveStateMutex);
                pendingPath = _pendingLoadStatePath;
            }

            if (!pendingPath.empty() && loadResumeState(pendingPath)) {
                setPlayerStatus("State loaded.");
            } else {
                setPlayerStatus("No saved state available.");
                    setPlayerLoadButtonEnabled(false);
            }
        }

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
            if (showFrameStats) {
                const int64_t audioStartUs = esp_timer_get_time();
                bsp_extra_i2s_write(mixbuffer.data(), sampleCount * 2 * sizeof(int16_t), &written, 0);
                const uint32_t audioUs = static_cast<uint32_t>(std::max<int64_t>(0, esp_timer_get_time() - audioStartUs));
                audioTotalUs += audioUs;
                audioMaxUs = std::max(audioMaxUs, audioUs);
            } else {
                bsp_extra_i2s_write(mixbuffer.data(), sampleCount * 2 * sizeof(int16_t), &written, 0);
            }
            ++perfAudioWriteCount;
        }

        const int64_t elapsedUs = esp_timer_get_time() - frameStartUs;
        if (skipFrames == 0) {
            if (elapsedUs > frameBudgetUs) {
                skipFrames = 1;
            }
        } else {
            skipFrames--;
        }

        const int64_t nowUs = esp_timer_get_time();
        const int64_t fpsWindowUs = nowUs - fpsWindowStartUs;
        if (showFrameStats && (fpsWindowUs >= 500000)) {
            const uint32_t emulatedFpsTimes10 = static_cast<uint32_t>((emulatedFrameCount * 10000000ULL) / fpsWindowUs);
            const uint32_t drawnFpsTimes10 = static_cast<uint32_t>((drawnFrameCount * 10000000ULL) / fpsWindowUs);
            setPlayerFps(emulatedFpsTimes10, drawnFpsTimes10);
            fpsWindowStartUs = nowUs;
            emulatedFrameCount = 0;
            drawnFrameCount = 0;
        }

        const int64_t perfWindowUs = nowUs - perfWindowStartUs;
        if (showFrameStats && (perfWindowUs >= kPerfLogWindowUs)) {
            const uint64_t presentTotalUs = _presentTotalUs.exchange(0);
            const uint32_t presentCount = _presentCount.exchange(0);
            const uint32_t presentMaxUs = _presentMaxUs.exchange(0);
            const uint32_t presentQueueBusyCount = _presentQueueBusyCount.exchange(0);
            const uint32_t emuFpsTimes10 = static_cast<uint32_t>((perfEmulatedFrameCount * 10000000ULL) / perfWindowUs);
            const uint32_t drawFpsTimes10 = static_cast<uint32_t>((perfDrawnFrameCount * 10000000ULL) / perfWindowUs);
            const uint32_t coreAvgMs10 = perfEmulatedFrameCount > 0 ? static_cast<uint32_t>((coreTotalUs * 10ULL) / (1000ULL * perfEmulatedFrameCount)) : 0;
            const uint32_t renderAvgMs10 = perfDrawnFrameCount > 0 ? static_cast<uint32_t>((renderTotalUs * 10ULL) / (1000ULL * perfDrawnFrameCount)) : 0;
            const uint32_t audioAvgMs10 = perfAudioWriteCount > 0 ? static_cast<uint32_t>((audioTotalUs * 10ULL) / (1000ULL * perfAudioWriteCount)) : 0;
            const uint32_t presentAvgMs10 = presentCount > 0 ? static_cast<uint32_t>((presentTotalUs * 10ULL) / (1000ULL * presentCount)) : 0;
            ESP_LOGI(kTag,
                     "Perf[SMS] EMU=%u.%u DRW=%u.%u core=%u.%u/%ums render=%u.%u/%ums audio=%u.%u/%ums present=%u.%u/%ums qbusy=%u",
                     static_cast<unsigned>(emuFpsTimes10 / 10),
                     static_cast<unsigned>(emuFpsTimes10 % 10),
                     static_cast<unsigned>(drawFpsTimes10 / 10),
                     static_cast<unsigned>(drawFpsTimes10 % 10),
                     static_cast<unsigned>(coreAvgMs10 / 10),
                     static_cast<unsigned>(coreAvgMs10 % 10),
                     static_cast<unsigned>(coreMaxUs / 1000),
                     static_cast<unsigned>(renderAvgMs10 / 10),
                     static_cast<unsigned>(renderAvgMs10 % 10),
                     static_cast<unsigned>(renderMaxUs / 1000),
                     static_cast<unsigned>(audioAvgMs10 / 10),
                     static_cast<unsigned>(audioAvgMs10 % 10),
                     static_cast<unsigned>(audioMaxUs / 1000),
                     static_cast<unsigned>(presentAvgMs10 / 10),
                     static_cast<unsigned>(presentAvgMs10 % 10),
                     static_cast<unsigned>(presentMaxUs / 1000),
                     static_cast<unsigned>(presentQueueBusyCount));

            perfWindowStartUs = nowUs;
            perfEmulatedFrameCount = 0;
            perfDrawnFrameCount = 0;
            perfAudioWriteCount = 0;
            coreTotalUs = 0;
            renderTotalUs = 0;
            audioTotalUs = 0;
            coreMaxUs = 0;
            renderMaxUs = 0;
            audioMaxUs = 0;
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

void SegaEmulator::setPlayerFps(uint32_t emulatedFpsTimes10, uint32_t drawnFpsTimes10)
{
    if (_playerFps == nullptr) {
        return;
    }

    bsp_display_lock(0);
    if (!_showFrameStats.load()) {
        lv_obj_add_flag(_playerFps, LV_OBJ_FLAG_HIDDEN);
        bsp_display_unlock();
        return;
    }

    lv_obj_clear_flag(_playerFps, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(_playerFps,
                          "EMU %u.%u\nDRW %u.%u",
                          static_cast<unsigned>(emulatedFpsTimes10 / 10),
                          static_cast<unsigned>(emulatedFpsTimes10 % 10),
                          static_cast<unsigned>(drawnFpsTimes10 / 10),
                          static_cast<unsigned>(drawnFpsTimes10 % 10));
    bsp_display_unlock();
}

void SegaEmulator::setPlayerLoadButtonEnabled(bool enabled)
{
    if (_loadButton == nullptr) {
        return;
    }

    bsp_display_lock(0);
    if (enabled) {
        lv_obj_clear_state(_loadButton, LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(_loadButton, kOverlayButtonOpacity, 0);
    } else {
        lv_obj_add_state(_loadButton, LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(_loadButton, LV_OPA_20, 0);
    }
    bsp_display_unlock();
}

void SegaEmulator::releaseSaveSlotEntries(SegaVector<SaveSlotEntry> &entries)
{
    for (SaveSlotEntry &entry : entries) {
        if (entry.previewBuffer != nullptr) {
            heap_caps_free(entry.previewBuffer);
            entry.previewBuffer = nullptr;
        }
    }
    SegaVector<SaveSlotEntry>().swap(entries);
}

void SegaEmulator::closeLoadStatePicker()
{
    _loadMenuActive.store(false);

    if ((_loadStateModal != nullptr) && lv_obj_is_valid(_loadStateModal)) {
        lv_obj_del(_loadStateModal);
    }
    _loadStateModal = nullptr;
    _saveSlotButtonContexts.clear();
    releaseSaveSlotEntries(_saveSlotEntries);
}

void SegaEmulator::showLoadStatePicker()
{
    closeLoadStatePicker();

    SegaVector<SaveSlotEntry> entries;
    if (!collectSavedStates(entries, true) || entries.empty()) {
        setPlayerLoadButtonEnabled(false);
        setPlayerStatus("No saved states available.");
        return;
    }

    const size_t previewBytes = sizeof(lv_color_t) * kSaveSlotPreviewWidth * kSaveSlotPreviewHeight;
    for (SaveSlotEntry &entry : entries) {
        entry.previewBuffer = static_cast<lv_color_t *>(heap_caps_malloc(previewBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (entry.previewBuffer == nullptr) {
            entry.previewBuffer = static_cast<lv_color_t *>(heap_caps_malloc(previewBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }
        if (entry.previewBuffer == nullptr) {
            continue;
        }

        memset(entry.previewBuffer, 0, previewBytes);
        FILE *thumbFile = fopen(entry.thumbPath.c_str(), "rb");
        if (thumbFile != nullptr) {
            fread(entry.previewBuffer, 1, previewBytes, thumbFile);
            fclose(thumbFile);
        }
    }

    _saveSlotEntries = std::move(entries);
    _saveSlotButtonContexts.clear();
    _saveSlotButtonContexts.reserve(_saveSlotEntries.size());

    _loadMenuActive.store(true);

    const lv_coord_t screenWidth = lv_obj_get_content_width(_playerScreen);
    const lv_coord_t screenHeight = lv_obj_get_content_height(_playerScreen);
    const lv_coord_t panelWidth = static_cast<lv_coord_t>(std::max<int>(320, std::min<int>(screenWidth - 28, 680)));
    const lv_coord_t panelHeight = static_cast<lv_coord_t>(std::max<int>(220, std::min<int>(screenHeight - 28, 292)));
    const lv_coord_t listWidth = panelWidth - 24;
    const lv_coord_t listHeight = panelHeight - 92;
    const size_t visibleCount = std::max<size_t>(1, _saveSlotEntries.size());
    const lv_coord_t cardGap = 12;
    const lv_coord_t availableCardWidth = listWidth - (static_cast<lv_coord_t>(visibleCount - 1) * cardGap);
    const lv_coord_t cardWidth = static_cast<lv_coord_t>(std::max<int>(112,
                                                                       std::min<int>(148,
                                                                                     availableCardWidth /
                                                                                         static_cast<lv_coord_t>(visibleCount))));
    const lv_coord_t cardHeight = listHeight - 4;

    _loadStateModal = lv_obj_create(_playerScreen);
    lv_obj_set_size(_loadStateModal, LV_PCT(100), LV_PCT(100));
    lv_obj_center(_loadStateModal);
    lv_obj_set_style_bg_color(_loadStateModal, lv_color_hex(0x040714), 0);
    lv_obj_set_style_bg_opa(_loadStateModal, LV_OPA_80, 0);
    lv_obj_set_style_border_width(_loadStateModal, 0, 0);
    lv_obj_clear_flag(_loadStateModal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(_loadStateModal);
    lv_obj_set_size(panel, panelWidth, panelHeight);
    lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 24, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x12182d), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x2f3554), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_transform_angle(panel, 900, 0);
    lv_obj_set_style_transform_pivot_x(panel, panelWidth / 2, 0);
    lv_obj_set_style_transform_pivot_y(panel, panelHeight / 2, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Load Saved Game");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 18, 16);

    lv_obj_t *subtitle = lv_label_create(panel);
    lv_label_set_text(subtitle, "Select one of the latest five manual saves.");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xaeb6d8), 0);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    lv_obj_t *list = lv_obj_create(panel);
    lv_obj_set_size(list, listWidth, listHeight);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_radius(list, 18, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x0d1222), 0);
    lv_obj_set_style_border_color(list, lv_color_hex(0x2f3554), 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_pad_column(list, cardGap, 0);
    lv_obj_set_style_pad_all(list, 10, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (size_t index = 0; index < _saveSlotEntries.size(); ++index) {
        SaveSlotEntry &entry = _saveSlotEntries[index];
        _saveSlotButtonContexts.push_back(SaveSlotButtonContext{this, index});

        lv_obj_t *button = lv_btn_create(list);
        lv_obj_set_size(button, cardWidth, cardHeight);
        lv_obj_set_style_radius(button, 16, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x19223b), 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x29375d), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(button, lv_color_hex(0x46527f), 0);
        lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_set_style_pad_all(button, 8, 0);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(button, onLoadSlotSelected, LV_EVENT_CLICKED, &_saveSlotButtonContexts.back());
        lv_obj_set_layout(button, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(button, 8, 0);

        lv_obj_t *preview = lv_canvas_create(button);
        lv_obj_set_size(preview, kSaveSlotPreviewWidth, kSaveSlotPreviewHeight);
        if (entry.previewBuffer != nullptr) {
            lv_canvas_set_buffer(preview,
                                 entry.previewBuffer,
                                 kSaveSlotPreviewWidth,
                                 kSaveSlotPreviewHeight,
                                 LV_IMG_CF_TRUE_COLOR);
        } else {
            static lv_color_t emptyPreview[kSaveSlotPreviewWidth * kSaveSlotPreviewHeight] = {};
            lv_canvas_set_buffer(preview,
                                 emptyPreview,
                                 kSaveSlotPreviewWidth,
                                 kSaveSlotPreviewHeight,
                                 LV_IMG_CF_TRUE_COLOR);
            lv_canvas_fill_bg(preview, lv_color_hex(0x05070f), LV_OPA_COVER);
        }

        lv_obj_t *nameLabel = lv_label_create(button);
        lv_label_set_text_fmt(nameLabel, index == 0 ? "Latest" : "Save %u", static_cast<unsigned>(index + 1));
        lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(nameLabel, lv_color_white(), 0);
        lv_obj_set_style_text_align(nameLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(nameLabel, cardWidth - 16);

        lv_obj_t *pathLabel = lv_label_create(button);
        lv_label_set_long_mode(pathLabel, LV_LABEL_LONG_DOT);
        lv_obj_set_width(pathLabel, cardWidth - 16);
        lv_label_set_text(pathLabel, entry.slotId.c_str());
        lv_obj_set_style_text_color(pathLabel, lv_color_hex(0xb8c3ea), 0);
        lv_obj_set_style_text_align(pathLabel, LV_TEXT_ALIGN_CENTER, 0);
    }

    lv_obj_t *cancelButton = lv_btn_create(panel);
    style_action_button(cancelButton);
    lv_obj_set_size(cancelButton, 112, 40);
    lv_obj_align(cancelButton, LV_ALIGN_TOP_RIGHT, -16, 14);
    lv_obj_add_event_cb(cancelButton, onLoadPickerCancel, LV_EVENT_CLICKED, this);
    set_button_label(cancelButton, "Cancel");

    lv_obj_move_foreground(_loadStateModal);
}

void SegaEmulator::onLoadSlotSelected(lv_event_t *event)
{
    auto *context = static_cast<SaveSlotButtonContext *>(lv_event_get_user_data(event));
    if ((context == nullptr) || (context->app == nullptr) || (context->index >= context->app->_saveSlotEntries.size())) {
        return;
    }

    {
        std::lock_guard<std::mutex> guard(context->app->_saveStateMutex);
        context->app->_pendingLoadStatePath = context->app->_saveSlotEntries[context->index].statePath;
    }

    context->app->closeLoadStatePicker();
    context->app->_loadRequested.store(true);
    context->app->setPlayerStatus("Loading saved state...");
}

void SegaEmulator::onLoadPickerCancel(lv_event_t *event)
{
    auto *app = static_cast<SegaEmulator *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }

    app->closeLoadStatePicker();
    app->setPlayerStatus("Load cancelled.");
}

void SegaEmulator::setPlayerFullscreen(bool fullscreen)
{
    ESP_Brookesia_Phone *phone = getPhone();
    if (phone == nullptr) {
        return;
    }

    ESP_Brookesia_StatusBar *statusBar = phone->getHome().getStatusBar();
    if (statusBar == nullptr) {
        return;
    }

    statusBar->setVisualMode(fullscreen ? ESP_BROOKESIA_STATUS_BAR_VISUAL_MODE_HIDE :
                                          ESP_BROOKESIA_STATUS_BAR_VISUAL_MODE_SHOW_FIXED);
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

void SegaEmulator::resetPerformanceStats()
{
    _framePresentationQueued.store(false);
    _presentTotalUs.store(0);
    _presentCount.store(0);
    _presentMaxUs.store(0);
    _presentQueueBusyCount.store(0);
}

bool SegaEmulator::queueFramePresentation()
{
    if (!_framePresentationQueued.exchange(true)) {
        lv_async_call(presentFrameAsync, this);
        return true;
    }

    _presentQueueBusyCount.fetch_add(1, std::memory_order_relaxed);
    return false;
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
        const lv_color_t *src = reinterpret_cast<const lv_color_t *>(_emulatorBuffer + SEGA_GWENESIS_FRAME_OFFSET);

        const int dstWidth = kCanvasWidth;
        const int dstHeight = kCanvasHeight;
        const int dstOffsetX = 0;
        const int dstOffsetY = 0;

        for (int y = 0; y < srcHeight; ++y) {
            lv_color_t *dstRow = _ppaSourceBuffer + (y * srcWidth);
            const lv_color_t *srcRow = src + (y * SEGA_GWENESIS_FRAME_STRIDE);
            memcpy(dstRow, srcRow, static_cast<size_t>(srcWidth) * sizeof(lv_color_t));
        }

        if ((_ppaHandle != nullptr) && rotateFrameWithPpa(_ppaSourceBuffer,
                                                          srcWidth,
                                                          srcHeight,
                                                          targetBuffer,
                                                          dstOffsetX,
                                                          dstOffsetY,
                                                          dstWidth,
                                                          dstHeight)) {
            queueFramePresentation();
            return;
        }

        for (int y = 0; y < dstHeight; ++y) {
            const int srcX = (y * srcWidth) / dstHeight;
            for (int x = 0; x < dstWidth; ++x) {
                const int srcY = srcHeight - 1 - ((x * srcHeight) / dstWidth);
                targetBuffer[(dstOffsetY + y) * kCanvasWidth + dstOffsetX + x] =
                    src[srcY * SEGA_GWENESIS_FRAME_STRIDE + srcX];
            }
        }

        queueFramePresentation();
        return;
    }

    uint16 palette[PALETTE_SIZE] = {0};
    render_copy_palette(palette);

    const int srcWidth = bitmap.viewport.w > 0 ? bitmap.viewport.w : SMS_WIDTH;
    const int srcHeight = bitmap.viewport.h > 0 ? bitmap.viewport.h : SMS_HEIGHT;
    const int srcOffsetX = bitmap.viewport.x;
    const int srcOffsetY = bitmap.viewport.y;
    const uint8_t *src = _emulatorBuffer + (srcOffsetY * bitmap.pitch) + srcOffsetX;

    const int dstWidth = kCanvasWidth;
    const int dstHeight = kCanvasHeight;
    const int dstOffsetX = 0;
    const int dstOffsetY = 0;

    for (int y = 0; y < srcHeight; ++y) {
        lv_color_t *dstRow = _ppaSourceBuffer + (y * srcWidth);
        const uint8_t *srcRow = src + (y * bitmap.pitch);
        for (int x = 0; x < srcWidth; ++x) {
            dstRow[x].full = palette[srcRow[x] & PIXEL_MASK];
        }
    }

    if ((_ppaHandle != nullptr) && rotateFrameWithPpa(_ppaSourceBuffer,
                                                      srcWidth,
                                                      srcHeight,
                                                      targetBuffer,
                                                      dstOffsetX,
                                                      dstOffsetY,
                                                      dstWidth,
                                                      dstHeight)) {
        queueFramePresentation();
        return;
    }

    for (int y = 0; y < dstHeight; ++y) {
        const int srcX = (y * srcWidth) / dstHeight;
        for (int x = 0; x < dstWidth; ++x) {
            const int srcY = srcHeight - 1 - ((x * srcHeight) / dstWidth);
            const uint8_t index = src[srcY * bitmap.pitch + srcX];
            targetBuffer[(dstOffsetY + y) * kCanvasWidth + dstOffsetX + x].full = palette[index & PIXEL_MASK];
        }
    }

    queueFramePresentation();
}

bool SegaEmulator::rotateFrameWithPpa(const lv_color_t *sourceBuffer,
                                      int sourceWidth,
                                      int sourceHeight,
                                      lv_color_t *targetBuffer,
                                      int dstOffsetX,
                                      int dstOffsetY,
                                      int dstWidth,
                                      int dstHeight)
{
    if ((_ppaHandle == nullptr) || (sourceBuffer == nullptr) || (targetBuffer == nullptr) ||
        (sourceWidth <= 0) || (sourceHeight <= 0) || (dstWidth <= 0) || (dstHeight <= 0)) {
        return false;
    }

    ppa_in_pic_blk_config_t inputConfig = {};
    inputConfig.buffer = sourceBuffer;
    inputConfig.pic_w = sourceWidth;
    inputConfig.pic_h = sourceHeight;
    inputConfig.block_w = sourceWidth;
    inputConfig.block_h = sourceHeight;
    inputConfig.block_offset_x = 0;
    inputConfig.block_offset_y = 0;
    inputConfig.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    ppa_out_pic_blk_config_t outputConfig = {};
    outputConfig.buffer = targetBuffer;
    outputConfig.buffer_size = aligned_buffer_size(sizeof(lv_color_t) * kCanvasWidth * kCanvasHeight, _cacheLineSize);
    outputConfig.pic_w = kCanvasWidth;
    outputConfig.pic_h = kCanvasHeight;
    outputConfig.block_offset_x = dstOffsetX;
    outputConfig.block_offset_y = dstOffsetY;
    outputConfig.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    ppa_srm_oper_config_t operConfig = {
        inputConfig,
        outputConfig,
    };

    operConfig.rotation_angle = PPA_SRM_ROTATION_ANGLE_270;
    operConfig.scale_x = static_cast<float>(dstHeight) / static_cast<float>(sourceWidth);
    operConfig.scale_y = static_cast<float>(dstWidth) / static_cast<float>(sourceHeight);
    operConfig.rgb_swap = 0;
    operConfig.byte_swap = 0;
    operConfig.mode = PPA_TRANS_MODE_BLOCKING;

    return ppa_do_scale_rotate_mirror(_ppaHandle, &operConfig) == ESP_OK;
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
    const int64_t presentStartUs = esp_timer_get_time();
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

    const uint32_t presentUs = static_cast<uint32_t>(std::max<int64_t>(0, esp_timer_get_time() - presentStartUs));
    _presentTotalUs.fetch_add(presentUs, std::memory_order_relaxed);
    _presentCount.fetch_add(1, std::memory_order_relaxed);
    uint32_t currentMaxUs = _presentMaxUs.load(std::memory_order_relaxed);
    while ((presentUs > currentMaxUs) &&
           !_presentMaxUs.compare_exchange_weak(currentMaxUs, presentUs, std::memory_order_relaxed)) {
    }

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
    setPlayerFullscreen(false);
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
        {_upButton, kInputLeft},
        {_downButton, kInputRight},
        {_leftButton, kInputDown},
        {_rightButton, kInputUp},
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
    const SegaString savePath = buildBatterySavePath();
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

    const SegaString savePath = buildBatterySavePath();
    FILE *file = fopen(savePath.c_str(), "wb");
    if (file == nullptr) {
        ESP_LOGW(kTag, "Failed to save SRAM to %s", savePath.c_str());
        return;
    }

    fwrite(cart.sram, 1, 0x8000, file);
    fclose(file);
}

bool SegaEmulator::loadResumeState(const SegaString &savePath)
{
    if (_currentCore == EmulatorCore::Gwenesis) {
        if (!sega_gwenesis_load_state_file(savePath.c_str())) {
            return false;
        }
        ESP_LOGI(kTag, "Loaded Mega Drive save state from %s", savePath.c_str());
        return true;
    }

    FILE *file = fopen(savePath.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }

    system_load_state(file);
    fclose(file);
    ESP_LOGI(kTag, "Loaded SMS/Game Gear save state from %s", savePath.c_str());
    return true;
}

bool SegaEmulator::saveResumeState(const SegaString &savePath)
{
    if (_currentCore == EmulatorCore::Gwenesis) {
        const bool saved = sega_gwenesis_save_state_file(savePath.c_str());
        if (!saved) {
            ESP_LOGW(kTag, "Failed to save Mega Drive state to %s", savePath.c_str());
            return false;
        }
        return true;
    }

    FILE *file = fopen(savePath.c_str(), "wb");
    if (file == nullptr) {
        ESP_LOGW(kTag, "Failed to save SMS/Game Gear state to %s", savePath.c_str());
        return false;
    }

    system_save_state(file);
    fclose(file);
    return true;
}

bool SegaEmulator::saveStateThumbnail(const SegaString &path)
{
    const size_t previewPixels = kSaveSlotPreviewWidth * kSaveSlotPreviewHeight;
    SegaVector<lv_color_t> preview(previewPixels);

    {
        std::lock_guard<std::mutex> guard(_frameBufferMutex);
        const lv_color_t *source = (_canvasFrontBuffer != nullptr) ? _canvasFrontBuffer : _canvasBackBuffer;
        if (source == nullptr) {
            return false;
        }

        for (size_t y = 0; y < kSaveSlotPreviewHeight; ++y) {
            const size_t srcY = (y * kCanvasHeight) / kSaveSlotPreviewHeight;
            for (size_t x = 0; x < kSaveSlotPreviewWidth; ++x) {
                const size_t srcX = (x * kCanvasWidth) / kSaveSlotPreviewWidth;
                preview[(y * kSaveSlotPreviewWidth) + x] = source[(srcY * kCanvasWidth) + srcX];
            }
        }
    }

    FILE *file = fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }

    const size_t written = fwrite(preview.data(), sizeof(lv_color_t), preview.size(), file);
    fclose(file);
    return written == preview.size();
}

bool SegaEmulator::saveResumeState()
{
    if (!ensure_sd_rom_root_available(true)) {
        return false;
    }

    const SegaString rootPath = buildSavedGamesRootPath();
    const SegaString gamePath = buildGameSaveDirectory();
    if (!ensure_directory_exists(rootPath) || !ensure_directory_exists(gamePath)) {
        return false;
    }

    const SegaString slotBaseName = buildSaveSlotBaseName();
    const SegaString statePath = gamePath + "/" + slotBaseName + ".state";
    const SegaString thumbPath = gamePath + "/" + slotBaseName + ".thumb";
    if (!saveResumeState(statePath)) {
        return false;
    }

    saveStateThumbnail(thumbPath);

    SegaVector<SaveSlotEntry> allEntries;
    if (collectSavedStates(allEntries, false) && (allEntries.size() > kMaxVisibleSaveSlots)) {
        for (size_t index = kMaxVisibleSaveSlots; index < allEntries.size(); ++index) {
            unlink(allEntries[index].statePath.c_str());
            unlink(allEntries[index].thumbPath.c_str());
        }
    }
    releaseSaveSlotEntries(allEntries);
    return true;
}

bool SegaEmulator::hasManualSaveState() const
{
    SegaVector<SaveSlotEntry> entries;
    const bool hasState = collectSavedStates(entries, true) && !entries.empty();
    const_cast<SegaEmulator *>(this)->releaseSaveSlotEntries(entries);
    return hasState;
}

bool SegaEmulator::collectSavedStates(SegaVector<SaveSlotEntry> &entries, bool limitResults) const
{
    SegaVector<SaveSlotEntry>().swap(entries);
    if (!ensure_sd_rom_root_available(true)) {
        return false;
    }

    const SegaString gameDirectory = buildGameSaveDirectory();
    if (!is_directory_path(gameDirectory)) {
        return true;
    }

    DIR *directory = opendir(gameDirectory.c_str());
    if (directory == nullptr) {
        return false;
    }

    struct dirent *entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        if (!has_extension(SegaString(entry->d_name), ".state")) {
            continue;
        }

        SaveSlotEntry slotEntry = {};
        slotEntry.slotId = entry->d_name;
        slotEntry.statePath = gameDirectory + "/" + entry->d_name;
        slotEntry.thumbPath = slotEntry.statePath.substr(0, slotEntry.statePath.size() - 6) + ".thumb";
        slotEntry.sortKey = parse_save_slot_key(entry->d_name);
        entries.push_back(std::move(slotEntry));
    }
    closedir(directory);

    std::sort(entries.begin(), entries.end(), [](const SaveSlotEntry &lhs, const SaveSlotEntry &rhs) {
        if (lhs.sortKey != rhs.sortKey) {
            return lhs.sortKey > rhs.sortKey;
        }
        return lhs.statePath > rhs.statePath;
    });

    if (limitResults && (entries.size() > kMaxVisibleSaveSlots)) {
        entries.resize(kMaxVisibleSaveSlots);
    }

    return true;
}

SegaString SegaEmulator::buildBatterySavePath() const
{
    return _activeRomPath + ".sav";
}

SegaString SegaEmulator::buildSavedGamesRootPath() const
{
    return kSavedGamesRootPath;
}

SegaString SegaEmulator::buildGameSaveDirectory() const
{
    char suffix[16] = {0};
    snprintf(suffix, sizeof(suffix), "%08x", static_cast<unsigned>(hash_path_component(_activeRomPath)));
    return buildSavedGamesRootPath() + "/" + sanitize_path_component(_activeRomName) + "_" + suffix;
}

SegaString SegaEmulator::buildSaveSlotBaseName() const
{
    char name[40] = {0};
    snprintf(name, sizeof(name), "save_%020llu", static_cast<unsigned long long>(esp_timer_get_time()));
    return SegaString(name);
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
    bool version2Format = false;

    while (fgets(line, sizeof(line), file) != nullptr) {
        size_t lineLength = strlen(line);
        while ((lineLength > 0) && ((line[lineLength - 1] == '\n') || (line[lineLength - 1] == '\r'))) {
            line[--lineLength] = '\0';
        }

        if (line[0] == '\0') {
            continue;
        }

        if (!headerSeen) {
            version2Format = strcmp(line, "JC4880_SEGA_INDEX_V2") == 0;
            headerSeen = version2Format || (strcmp(line, "JC4880_SEGA_INDEX_V1") == 0);
            continue;
        }

        char *separator = strchr(line, '\t');
        if (separator == nullptr) {
            continue;
        }

        *separator = '\0';
        uint64_t lastPlayedAt = 0;
        char *pathValue = separator + 1;
        if (version2Format) {
            char *secondSeparator = strchr(pathValue, '\t');
            if (secondSeparator == nullptr) {
                continue;
            }
            *secondSeparator = '\0';
            lastPlayedAt = strtoull(secondSeparator + 1, nullptr, 10);
        }
        loadedEntries.push_back(RomEntry{
            .name = line,
            .path = pathValue,
            .lastPlayedAt = lastPlayedAt,
            .button = nullptr,
        });
    }

    fclose(file);
    if (!headerSeen) {
        return false;
    }

    _romEntries = std::move(loadedEntries);
    sortRomEntries();
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

    fprintf(file, "JC4880_SEGA_INDEX_V2\n");
    for (const RomEntry &entry : entries) {
        fprintf(file, "%s\t%s\t%llu\n",
                entry.name.c_str(),
                entry.path.c_str(),
                static_cast<unsigned long long>(entry.lastPlayedAt));
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

void SegaEmulator::sortRomEntries()
{
    std::sort(_romEntries.begin(), _romEntries.end(), [](const RomEntry &lhs, const RomEntry &rhs) {
        if (lhs.lastPlayedAt != rhs.lastPlayedAt) {
            return lhs.lastPlayedAt > rhs.lastPlayedAt;
        }

        const SegaString left_name = to_lower(lhs.name);
        const SegaString right_name = to_lower(rhs.name);
        if (left_name == right_name) {
            return to_lower(lhs.path) < to_lower(rhs.path);
        }
        return left_name < right_name;
    });
}

void SegaEmulator::markRomAsPlayed(const SegaString &path)
{
    const uint64_t playedAt = static_cast<uint64_t>(esp_timer_get_time());
    for (RomEntry &entry : _romEntries) {
        if (entry.path == path) {
            entry.lastPlayedAt = playedAt;
            sortRomEntries();
            saveIndexFile(_romEntries);
            return;
        }
    }
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

void SegaEmulator::onPlayerActionEvent(lv_event_t *event)
{
    auto *app = static_cast<SegaEmulator *>(lv_event_get_user_data(event));
    if ((app == nullptr) || (lv_event_get_code(event) != LV_EVENT_CLICKED)) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(event);
    if (target == app->_saveButton) {
        app->_saveRequested.store(true);
        app->setPlayerStatus("Saving state...");
        return;
    }

    if (target == app->_loadButton) {
        app->showLoadStatePicker();
        return;
    }

    if (target == app->_exitButton) {
        app->_closingApp.store(false);
        app->setPlayerStatus("Returning to the game list...");
        app->stopEmulation();
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