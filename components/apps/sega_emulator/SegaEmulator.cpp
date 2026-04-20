#include "SegaEmulator.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>

#include "SegaGwenesisBridge.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_err.h"

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
constexpr const char *kRomRoot = BSP_SD_MOUNT_POINT "/sega_games";

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

constexpr size_t kSmsFrameBufferSize = SMS_WIDTH * SMS_HEIGHT;
constexpr size_t kGenesisFrameBufferSize = SEGA_GWENESIS_FRAME_OFFSET + (SEGA_GWENESIS_FRAME_STRIDE * SEGA_GWENESIS_FRAME_HEIGHT);

std::string to_lower(std::string value)
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

bool has_extension(const std::string &path, const char *extension)
{
    const std::string lower = to_lower(path);
    const size_t extensionLength = strlen(extension);
    return (lower.size() >= extensionLength) && (lower.rfind(extension) == lower.size() - extensionLength);
}
}

SegaEmulator::SegaEmulator()
    : ESP_Brookesia_PhoneApp("SEGA Emulator", &img_app_sega, true)
{
}

SegaEmulator::~SegaEmulator()
{
    stopEmulation();

    for (ControlBinding *binding : _controlBindings) {
        delete binding;
    }
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
    if (!ensureUiReady()) {
        return false;
    }

    if (_running.load()) {
        lv_scr_load(_playerScreen);
    } else {
        lv_scr_load(_browserScreen);
    }
    return true;
}

bool SegaEmulator::ensureUiReady()
{
    if ((_browserScreen != nullptr) && (_playerScreen != nullptr) && (_canvas != nullptr)) {
        return true;
    }

    if ((_canvasFrontBuffer == nullptr) || (_canvasBackBuffer == nullptr) || (_emulatorBuffer == nullptr)) {
        ESP_LOGE(kTag, "SEGA buffers are not initialized");
        return false;
    }

    if (_browserScreen == nullptr) {
        createBrowserScreen();
    }
    if (_playerScreen == nullptr) {
        createPlayerScreen();
    }

    return (_browserScreen != nullptr) && (_playerScreen != nullptr) && (_canvas != nullptr);
}

bool SegaEmulator::back()
{
    if (_running.load()) {
        stopEmulation();
        lv_scr_load(_browserScreen);
        return true;
    }
    return notifyCoreClosed();
}

bool SegaEmulator::close()
{
    stopEmulation();
    return true;
}

void SegaEmulator::createBrowserScreen()
{
    _browserScreen = lv_obj_create(nullptr);
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
    lv_label_set_text_fmt(subtitle, "ROM folder: %s", kRomRoot);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xaeb6d8), 0);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    lv_obj_t *refreshButton = lv_btn_create(_browserScreen);
    style_action_button(refreshButton);
    lv_obj_set_size(refreshButton, 108, 46);
    lv_obj_align(refreshButton, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_add_event_cb(refreshButton, onRefreshClicked, LV_EVENT_CLICKED, this);
    set_button_label(refreshButton, "Refresh");

    _browserStatus = lv_label_create(_browserScreen);
    lv_obj_set_width(_browserStatus, 440);
    lv_label_set_long_mode(_browserStatus, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(_browserStatus, lv_color_hex(0xe0b96c), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_align(_browserStatus, LV_ALIGN_TOP_LEFT, 8, 74);

    _romList = lv_list_create(_browserScreen);
    lv_obj_set_size(_romList, 448, 620);
    lv_obj_align(_romList, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_romList, lv_color_hex(0x10162a), 0);
    lv_obj_set_style_border_color(_romList, lv_color_hex(0x2f3554), 0);
    lv_obj_set_style_border_width(_romList, 1, 0);
    lv_obj_set_style_pad_row(_romList, 8, 0);
}

void SegaEmulator::createPlayerScreen()
{
    _playerScreen = lv_obj_create(nullptr);
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

    for (const auto &control : controls) {
        ControlBinding *binding = new ControlBinding{this, control.mask};
        _controlBindings.push_back(binding);
        lv_obj_add_event_cb(control.button, onControlButtonEvent, LV_EVENT_PRESSED, binding);
        lv_obj_add_event_cb(control.button, onControlButtonEvent, LV_EVENT_PRESS_LOST, binding);
        lv_obj_add_event_cb(control.button, onControlButtonEvent, LV_EVENT_RELEASED, binding);
    }
}

void SegaEmulator::refreshRomList()
{
    _romEntries.clear();
    lv_obj_clean(_romList);

    DIR *dir = opendir(kRomRoot);
    if (dir == nullptr) {
        setBrowserStatus("No ROM folder found. Insert the SD card and create /sdcard/sega_games with SMS or Mega Drive ROMs.");
        return;
    }

    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }

        std::string filename(entry->d_name);
        std::string fullPath = std::string(kRomRoot) + "/" + filename;
        if (!hasSupportedExtension(fullPath)) {
            continue;
        }

        _romEntries.push_back(RomEntry{
            .name = filename,
            .path = fullPath,
            .button = nullptr,
        });
    }
    closedir(dir);

    std::sort(_romEntries.begin(), _romEntries.end(), [](const RomEntry &lhs, const RomEntry &rhs) {
        return to_lower(lhs.name) < to_lower(rhs.name);
    });

    if (_romEntries.empty()) {
        setBrowserStatus("No supported ROMs found yet. Supported extensions: .sms, .gg, .sg, .md, .gen, .bin, .smd.");
        return;
    }

    for (RomEntry &rom : _romEntries) {
        rom.button = lv_list_add_btn(_romList, LV_SYMBOL_PLAY, rom.name.c_str());
        lv_obj_add_event_cb(rom.button, onRomSelected, LV_EVENT_CLICKED, this);
    }

    setBrowserStatus("Ready. Tap a ROM to launch it.");
}

void SegaEmulator::startRom(const std::string &path, const std::string &name)
{
    if (_running.load()) {
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

    if (xTaskCreatePinnedToCore(emulatorTaskEntry, "sega_emu", 8192, this, 4, &_emulatorTask, 1) != pdPASS) {
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
            free(cart.rom);
            cart.rom = nullptr;
        }
        if (cart.sram != nullptr) {
            free(cart.sram);
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

    memset(_emulatorBuffer, 0, SMS_WIDTH * SMS_HEIGHT);
    memset(_emulatorBuffer, 0, std::max(kSmsFrameBufferSize, kGenesisFrameBufferSize));

    if (_currentCore == EmulatorCore::Gwenesis) {
        if (!sega_gwenesis_load_rom(_activeRomPath.c_str(), _emulatorBuffer, kGenesisFrameBufferSize)) {
            finish("Failed to load Mega Drive ROM.");
            return;
        }

        if (!setupAudio(sega_gwenesis_get_audio_sample_rate())) {
            finish("Audio initialization failed.");
            return;
        }

        const TickType_t frameInterval = pdMS_TO_TICKS(1000 / SEGA_GWENESIS_REFRESH_RATE);
        TickType_t lastWakeTime = xTaskGetTickCount();
        std::vector<int16_t> mixbuffer(SEGA_GWENESIS_AUDIO_BUFFER_LENGTH * 2);

        setPlayerStatus("Running Mega Drive emulator");

        while (!_stopRequested.load()) {
            sega_gwenesis_set_input_mask(_inputMask.load());
            sega_gwenesis_run_frame();
            renderCurrentFrame();

            const size_t sampleCount = sega_gwenesis_mix_audio_stereo(mixbuffer.data(), mixbuffer.size() / 2);
            if (sampleCount > 0) {
                size_t written = 0;
                bsp_extra_i2s_write(mixbuffer.data(), sampleCount * 2 * sizeof(int16_t), &written, 0);
            }

            vTaskDelayUntil(&lastWakeTime, frameInterval);
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

    std::string lowerPath = to_lower(_activeRomPath);
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
    std::vector<int16_t> mixbuffer(static_cast<size_t>(kSmsAudioSampleRate / 50) * 2);

    setPlayerStatus("Running SMS/Game Gear emulator");

    while (!_stopRequested.load()) {
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

        vTaskDelayUntil(&lastWakeTime, frameInterval);
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
    if (lv_scr_act() == _playerScreen) {
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
    const std::string savePath = buildSavePath();
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

    const std::string savePath = buildSavePath();
    FILE *file = fopen(savePath.c_str(), "wb");
    if (file == nullptr) {
        ESP_LOGW(kTag, "Failed to save SRAM to %s", savePath.c_str());
        return;
    }

    fwrite(cart.sram, 1, 0x8000, file);
    fclose(file);
}

std::string SegaEmulator::buildSavePath() const
{
    return _activeRomPath + ".sav";
}

bool SegaEmulator::hasSupportedExtension(const std::string &path)
{
    return has_extension(path, ".sms") ||
           has_extension(path, ".gg") ||
           has_extension(path, ".sg") ||
           has_extension(path, ".md") ||
           has_extension(path, ".gen") ||
           has_extension(path, ".bin") ||
           has_extension(path, ".smd");
}

SegaEmulator::EmulatorCore SegaEmulator::getCoreForPath(const std::string &path)
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
    app->refreshRomList();
}

void SegaEmulator::onControlButtonEvent(lv_event_t *event)
{
    ControlBinding *binding = static_cast<ControlBinding *>(lv_event_get_user_data(event));
    if ((binding == nullptr) || (binding->app == nullptr)) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    uint32_t current = binding->app->_inputMask.load();
    if (code == LV_EVENT_PRESSED) {
        binding->app->_inputMask.store(current | binding->mask);
    } else if ((code == LV_EVENT_RELEASED) || (code == LV_EVENT_PRESS_LOST)) {
        binding->app->_inputMask.store(current & ~binding->mask);
    }
}