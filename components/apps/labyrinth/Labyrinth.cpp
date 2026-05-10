#include "Labyrinth.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "ImuService.hpp"

namespace {

static constexpr const char *kTag = "LabyrinthApp";
static constexpr const char *kNvsNamespace = "labyrinth";
static constexpr const char *kNvsHistoryKey = "attempts";
static constexpr const char *kNvsAxisMapXKey = "axis_map_x";
static constexpr const char *kNvsAxisMapYKey = "axis_map_y";
static constexpr const char *kNvsAxisMapZKey = "axis_map_z";
static constexpr uint32_t kHistoryVersion = 1;
static constexpr char kAxisMappingOptions[] = "+X\n-X\n+Y\n-Y\n+Z\n-Z";

enum AxisMapping: uint8_t {
    kAxisMappingPosX = 0,
    kAxisMappingNegX,
    kAxisMappingPosY,
    kAxisMappingNegY,
    kAxisMappingPosZ,
    kAxisMappingNegZ,
};

static float clampf(float value, float min_value, float max_value)
{
    return std::max(min_value, std::min(max_value, value));
}

static uint8_t sanitize_axis_mapping(uint8_t value, uint8_t fallback)
{
    return (value <= kAxisMappingNegZ) ? value : fallback;
}

static float select_axis_mapping_component(float x, float y, float z, uint8_t mapping)
{
    switch (sanitize_axis_mapping(mapping, kAxisMappingPosX)) {
    case kAxisMappingNegX:
        return -x;
    case kAxisMappingPosY:
        return y;
    case kAxisMappingNegY:
        return -y;
    case kAxisMappingPosZ:
        return z;
    case kAxisMappingNegZ:
        return -z;
    case kAxisMappingPosX:
    default:
        return x;
    }
}

static float wrap_phase(float value, float period)
{
    if (period <= 0.0f) {
        return 0.0f;
    }
    value = std::fmod(value, period);
    if (value < 0.0f) {
        value += period;
    }
    return value;
}

static uint32_t seconds_since_boot()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
}

} // namespace

LabyrinthApp::LabyrinthApp()
    : ESP_Brookesia_PhoneApp("Labyrinth", nullptr, true),
      _paused(false),
      _runFinished(false),
      _imuReady(false),
      _historyLoaded(false),
      _currentLevel(0),
      _bestLevelThisRun(0),
      _score(0),
      _levelTimeRemainingMs(0),
      _runStartUs(0),
      _mazeRows(0),
      _mazeCols(0),
      _startCellRow(0),
      _startCellCol(0),
      _goalCellRow(0),
      _goalCellCol(0),
      _mazeRasterRows(0),
      _mazeRasterCols(0),
      _boardSize(0.0f),
      _mazeOffsetX(0.0f),
      _mazeOffsetY(0.0f),
      _tileSize(0.0f),
      _ballRadius(0.0f),
      _holeRadius(0.0f),
      _ballX(0.0f),
      _ballY(0.0f),
      _ballVx(0.0f),
      _ballVy(0.0f),
    _ballTexturePhaseX(0.0f),
    _ballTexturePhaseY(0.0f),
      _nvsHandle(0),
      _mazeCells(),
      _mazeRaster(),
      _wallObjects(),
      _attemptHistory{},
      _attemptCount(0),
      _root(nullptr),
      _titleLabel(nullptr),
      _levelLabel(nullptr),
      _scoreLabel(nullptr),
      _timerLabel(nullptr),
      _statusLabel(nullptr),
    _axisControlsPanel(nullptr),
    _axisMapXDropdown(nullptr),
    _axisMapYDropdown(nullptr),
    _axisMapZDropdown(nullptr),
      _board(nullptr),
      _mazeLayer(nullptr),
      _hole(nullptr),
      _ball(nullptr),
    _ballTextureLayer(nullptr),
    _ballTextureBandA(nullptr),
    _ballTextureBandB(nullptr),
    _ballTextureBandC(nullptr),
    _ballHighlight(nullptr),
    _ballShadow(nullptr),
      _resultOverlay(nullptr),
      _resultPanel(nullptr),
      _resultSummaryLabel(nullptr),
      _resultRatingLabel(nullptr),
      _resultChart(nullptr),
      _resultSeries(nullptr),
    _tickTimer(nullptr),
    _axisMapX(kAxisMappingPosX),
    _axisMapY(kAxisMappingPosY),
    _axisMapZ(kAxisMappingPosZ)
{
}

LabyrinthApp::~LabyrinthApp()
{
    destroyUi();
    if (_nvsHandle != 0) {
        nvs_close(_nvsHandle);
        _nvsHandle = 0;
    }
}

bool LabyrinthApp::init()
{
    if ((_nvsHandle == 0) && (nvs_open(kNvsNamespace, NVS_READWRITE, &_nvsHandle) != ESP_OK)) {
        ESP_LOGW(kTag, "Failed to open NVS namespace; attempt history will be ephemeral");
        _nvsHandle = 0;
    }

    _historyLoaded = loadHistory();
    loadAxisMappings();
    ensureImuReady();
    return true;
}

bool LabyrinthApp::run()
{
    buildUi();
    startNewRun();
    if (_tickTimer != nullptr) {
        lv_timer_del(_tickTimer);
        _tickTimer = nullptr;
    }
    _tickTimer = lv_timer_create(onTick, kTickPeriodMs, this);
    return _tickTimer != nullptr;
}

bool LabyrinthApp::pause()
{
    _paused = true;
    if (_tickTimer != nullptr) {
        lv_timer_pause(_tickTimer);
    }
    return true;
}

bool LabyrinthApp::resume()
{
    _paused = false;
    if (_tickTimer != nullptr) {
        lv_timer_resume(_tickTimer);
    }
    ensureImuReady();
    updateHud();
    return true;
}

bool LabyrinthApp::back()
{
    return notifyCoreClosed();
}

bool LabyrinthApp::close()
{
    destroyUi();
    return true;
}

void LabyrinthApp::onTick(lv_timer_t *timer)
{
    if ((timer == nullptr) || (timer->user_data == nullptr)) {
        return;
    }
    static_cast<LabyrinthApp *>(timer->user_data)->tickGame();
}

void LabyrinthApp::onRetryEvent(lv_event_t *event)
{
    LabyrinthApp *app = static_cast<LabyrinthApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    app->closeResultModal();
    app->startNewRun();
}

void LabyrinthApp::onQuitEvent(lv_event_t *event)
{
    LabyrinthApp *app = static_cast<LabyrinthApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    app->notifyCoreClosed();
}

void LabyrinthApp::onAxisMappingChangedEvent(lv_event_t *event)
{
    LabyrinthApp *app = static_cast<LabyrinthApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }

    if (app->_axisMapXDropdown != nullptr) {
        app->_axisMapX = sanitize_axis_mapping(static_cast<uint8_t>(lv_dropdown_get_selected(app->_axisMapXDropdown)),
                                               kAxisMappingPosX);
    }
    if (app->_axisMapYDropdown != nullptr) {
        app->_axisMapY = sanitize_axis_mapping(static_cast<uint8_t>(lv_dropdown_get_selected(app->_axisMapYDropdown)),
                                               kAxisMappingPosY);
    }
    if (app->_axisMapZDropdown != nullptr) {
        app->_axisMapZ = sanitize_axis_mapping(static_cast<uint8_t>(lv_dropdown_get_selected(app->_axisMapZDropdown)),
                                               kAxisMappingPosZ);
    }

    app->_ballVx = 0.0f;
    app->_ballVy = 0.0f;
    app->saveAxisMappings();
}

void LabyrinthApp::ensureImuReady()
{
    jc4880::imu::ImuConfig config = {};
    if (!jc4880::imu::ImuService::instance().loadConfig(config) || !config.enabled ||
        (config.model == jc4880::imu::ImuModel::IMU_NONE)) {
        _imuReady = false;
        return;
    }

    _imuReady = jc4880::imu::ImuService::instance().begin(&config);
}

void LabyrinthApp::buildUi()
{
    destroyUi();

    const lv_coord_t screen_width = lv_disp_get_hor_res(nullptr);
    const lv_coord_t screen_height = lv_disp_get_ver_res(nullptr);
    _boardSize = static_cast<float>(std::max<lv_coord_t>(220, std::min<lv_coord_t>(screen_width - 40, screen_height - 310)));

    _root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(_root, 0, 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_pad_all(_root, 20, 0);
    lv_obj_set_style_pad_row(_root, 14, 0);
    lv_obj_set_style_bg_color(_root, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_bg_grad_color(_root, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_grad_dir(_root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_scrollbar_mode(_root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _titleLabel = lv_label_create(_root);
    lv_obj_set_style_text_font(_titleLabel, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(_titleLabel, lv_color_hex(0x0F172A), 0);
    lv_label_set_text(_titleLabel, "Labyrinth");

    lv_obj_t *hudRow = lv_obj_create(_root);
    lv_obj_set_size(hudRow, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(hudRow, 24, 0);
    lv_obj_set_style_border_width(hudRow, 0, 0);
    lv_obj_set_style_pad_all(hudRow, 14, 0);
    lv_obj_set_style_bg_color(hudRow, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(hudRow, 220, 0);
    lv_obj_set_style_shadow_width(hudRow, 26, 0);
    lv_obj_set_style_shadow_opa(hudRow, 48, 0);
    lv_obj_set_style_shadow_color(hudRow, lv_color_hex(0x1E293B), 0);
    lv_obj_set_flex_flow(hudRow, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(hudRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _levelLabel = lv_label_create(hudRow);
    _scoreLabel = lv_label_create(hudRow);
    _timerLabel = lv_label_create(hudRow);
    for (lv_obj_t *label: {_levelLabel, _scoreLabel, _timerLabel}) {
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
    }

    _board = lv_obj_create(_root);
    lv_obj_set_size(_board, static_cast<lv_coord_t>(_boardSize), static_cast<lv_coord_t>(_boardSize));
    lv_obj_set_style_radius(_board, 28, 0);
    lv_obj_set_style_border_width(_board, 0, 0);
    lv_obj_set_style_pad_all(_board, 0, 0);
    lv_obj_set_style_bg_color(_board, lv_color_hex(0xEAB308), 0);
    lv_obj_set_style_bg_grad_color(_board, lv_color_hex(0xFDE68A), 0);
    lv_obj_set_style_bg_grad_dir(_board, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_width(_board, 32, 0);
    lv_obj_set_style_shadow_opa(_board, 40, 0);
    lv_obj_set_style_shadow_color(_board, lv_color_hex(0x92400E), 0);
    lv_obj_clear_flag(_board, LV_OBJ_FLAG_SCROLLABLE);

    _mazeLayer = lv_obj_create(_board);
    lv_obj_set_size(_mazeLayer, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(_mazeLayer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_mazeLayer, 0, 0);
    lv_obj_set_style_pad_all(_mazeLayer, 0, 0);
    lv_obj_clear_flag(_mazeLayer, LV_OBJ_FLAG_SCROLLABLE);

    _hole = lv_obj_create(_board);
    lv_obj_set_style_border_width(_hole, 0, 0);
    lv_obj_set_style_bg_color(_hole, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_grad_color(_hole, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_bg_grad_dir(_hole, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_width(_hole, 18, 0);
    lv_obj_set_style_shadow_opa(_hole, 90, 0);
    lv_obj_set_style_shadow_color(_hole, lv_color_hex(0x020617), 0);
    lv_obj_clear_flag(_hole, LV_OBJ_FLAG_SCROLLABLE);

    _ball = lv_obj_create(_board);
    lv_obj_set_style_border_width(_ball, 2, 0);
    lv_obj_set_style_border_color(_ball, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_bg_color(_ball, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_bg_grad_color(_ball, lv_color_hex(0x0369A1), 0);
    lv_obj_set_style_bg_grad_dir(_ball, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_width(_ball, 22, 0);
    lv_obj_set_style_shadow_opa(_ball, 72, 0);
    lv_obj_set_style_shadow_color(_ball, lv_color_hex(0x0284C7), 0);
    lv_obj_set_style_clip_corner(_ball, true, 0);
    lv_obj_clear_flag(_ball, LV_OBJ_FLAG_SCROLLABLE);

    _ballTextureLayer = lv_obj_create(_ball);
    lv_obj_set_style_bg_opa(_ballTextureLayer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_ballTextureLayer, 0, 0);
    lv_obj_set_style_pad_all(_ballTextureLayer, 0, 0);
    lv_obj_clear_flag(_ballTextureLayer, LV_OBJ_FLAG_SCROLLABLE);

    _ballTextureBandA = lv_obj_create(_ballTextureLayer);
    lv_obj_set_style_radius(_ballTextureBandA, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_ballTextureBandA, 0, 0);
    lv_obj_set_style_bg_color(_ballTextureBandA, lv_color_hex(0xE0F2FE), 0);
    lv_obj_set_style_bg_opa(_ballTextureBandA, 128, 0);
    lv_obj_clear_flag(_ballTextureBandA, LV_OBJ_FLAG_SCROLLABLE);

    _ballTextureBandB = lv_obj_create(_ballTextureLayer);
    lv_obj_set_style_radius(_ballTextureBandB, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_ballTextureBandB, 0, 0);
    lv_obj_set_style_bg_color(_ballTextureBandB, lv_color_hex(0xBAE6FD), 0);
    lv_obj_set_style_bg_opa(_ballTextureBandB, 110, 0);
    lv_obj_clear_flag(_ballTextureBandB, LV_OBJ_FLAG_SCROLLABLE);

    _ballTextureBandC = lv_obj_create(_ballTextureLayer);
    lv_obj_set_style_radius(_ballTextureBandC, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_ballTextureBandC, 0, 0);
    lv_obj_set_style_bg_color(_ballTextureBandC, lv_color_hex(0x082F49), 0);
    lv_obj_set_style_bg_opa(_ballTextureBandC, 72, 0);
    lv_obj_clear_flag(_ballTextureBandC, LV_OBJ_FLAG_SCROLLABLE);

    _ballHighlight = lv_obj_create(_ball);
    lv_obj_set_style_border_width(_ballHighlight, 0, 0);
    lv_obj_set_style_bg_color(_ballHighlight, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(_ballHighlight, 138, 0);
    lv_obj_set_style_radius(_ballHighlight, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(_ballHighlight, LV_OBJ_FLAG_SCROLLABLE);

    _ballShadow = lv_obj_create(_ball);
    lv_obj_set_style_border_width(_ballShadow, 0, 0);
    lv_obj_set_style_bg_color(_ballShadow, lv_color_hex(0x082F49), 0);
    lv_obj_set_style_bg_opa(_ballShadow, 82, 0);
    lv_obj_set_style_radius(_ballShadow, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(_ballShadow, LV_OBJ_FLAG_SCROLLABLE);

    _axisControlsPanel = lv_obj_create(_root);
    lv_obj_set_size(_axisControlsPanel, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(_axisControlsPanel, 20, 0);
    lv_obj_set_style_border_width(_axisControlsPanel, 0, 0);
    lv_obj_set_style_pad_all(_axisControlsPanel, 12, 0);
    lv_obj_set_style_pad_row(_axisControlsPanel, 10, 0);
    lv_obj_set_style_pad_column(_axisControlsPanel, 12, 0);
    lv_obj_set_style_bg_color(_axisControlsPanel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(_axisControlsPanel, 228, 0);
    lv_obj_set_style_shadow_width(_axisControlsPanel, 18, 0);
    lv_obj_set_style_shadow_opa(_axisControlsPanel, 24, 0);
    lv_obj_set_style_shadow_color(_axisControlsPanel, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_flex_flow(_axisControlsPanel, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(_axisControlsPanel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(_axisControlsPanel, LV_OBJ_FLAG_SCROLLABLE);

    const auto create_axis_mapping_control = [this](const char *label_text, uint8_t selected) -> lv_obj_t * {
        lv_obj_t *group = lv_obj_create(_axisControlsPanel);
        lv_obj_set_size(group, lv_pct(31), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(group, 0, 0);
        lv_obj_set_style_pad_all(group, 0, 0);
        lv_obj_set_style_pad_row(group, 6, 0);
        lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *label = lv_label_create(group);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x334155), 0);
        lv_label_set_text(label, label_text);

        lv_obj_t *dropdown = lv_dropdown_create(group);
        lv_dropdown_set_options_static(dropdown, kAxisMappingOptions);
        lv_dropdown_set_selected(dropdown, sanitize_axis_mapping(selected, kAxisMappingPosX));
        lv_obj_set_width(dropdown, lv_pct(100));
        lv_obj_add_event_cb(dropdown, onAxisMappingChangedEvent, LV_EVENT_VALUE_CHANGED, this);
        return dropdown;
    };

    _axisMapXDropdown = create_axis_mapping_control("X Axis", _axisMapX);
    _axisMapYDropdown = create_axis_mapping_control("Y Axis", _axisMapY);
    _axisMapZDropdown = create_axis_mapping_control("Z Axis", _axisMapZ);

    _statusLabel = lv_label_create(_root);
    lv_obj_set_width(_statusLabel, lv_pct(100));
    lv_obj_set_style_text_align(_statusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(_statusLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_statusLabel, lv_color_hex(0x334155), 0);
    lv_label_set_long_mode(_statusLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_statusLabel, "Tilt with the IMU to roll the ball into the center hole before time runs out.");
}

void LabyrinthApp::destroyUi()
{
    if (_tickTimer != nullptr) {
        lv_timer_del(_tickTimer);
        _tickTimer = nullptr;
    }
    closeResultModal();
    clearWallObjects();

    if ((_root != nullptr) && lv_obj_is_valid(_root)) {
        lv_obj_del(_root);
    }

    _root = nullptr;
    _titleLabel = nullptr;
    _levelLabel = nullptr;
    _scoreLabel = nullptr;
    _timerLabel = nullptr;
    _statusLabel = nullptr;
    _axisControlsPanel = nullptr;
    _axisMapXDropdown = nullptr;
    _axisMapYDropdown = nullptr;
    _axisMapZDropdown = nullptr;
    _board = nullptr;
    _mazeLayer = nullptr;
    _hole = nullptr;
    _ball = nullptr;
    _ballTextureLayer = nullptr;
    _ballTextureBandA = nullptr;
    _ballTextureBandB = nullptr;
    _ballTextureBandC = nullptr;
    _ballHighlight = nullptr;
    _ballShadow = nullptr;
}

void LabyrinthApp::clearWallObjects()
{
    for (lv_obj_t *wall: _wallObjects) {
        if ((wall != nullptr) && lv_obj_is_valid(wall)) {
            lv_obj_del(wall);
        }
    }
    _wallObjects.clear();
}

void LabyrinthApp::startNewRun()
{
    _runFinished = false;
    _score = 0;
    _currentLevel = 0;
    _bestLevelThisRun = 0;
    _runStartUs = static_cast<uint64_t>(esp_timer_get_time());
    closeResultModal();
    ensureImuReady();
    loadLevel(0);
}

uint32_t LabyrinthApp::getLevelTimeLimitMs(uint16_t level_index) const
{
    const int32_t seconds = std::max<int32_t>(105, 600 - static_cast<int32_t>(level_index) * 5);
    return static_cast<uint32_t>(seconds) * 1000U;
}

void LabyrinthApp::loadLevel(uint16_t level_index)
{
    _currentLevel = level_index;
    _bestLevelThisRun = std::max<uint16_t>(_bestLevelThisRun, static_cast<uint16_t>(level_index + 1));
    _levelTimeRemainingMs = getLevelTimeLimitMs(level_index);
    generateLevel(level_index);
    renderMaze();
    resetBallAtStart();
    updateHud();
    if (_statusLabel != nullptr) {
        lv_label_set_text_fmt(_statusLabel,
                              _imuReady ? "Level %u generated. Guide the ball into the center hole."
                                        : "IMU is not ready. Enable and configure it in Settings to play.",
                              static_cast<unsigned>(level_index + 1));
    }
}

void LabyrinthApp::generateLevel(uint16_t level_index)
{
    const uint16_t size = static_cast<uint16_t>(5 + std::min<uint16_t>(10, level_index / 10));
    _mazeRows = size;
    _mazeCols = size;
    _goalCellRow = _mazeRows / 2;
    _goalCellCol = _mazeCols / 2;

    switch (level_index % 4U) {
    case 1:
        _startCellRow = 0;
        _startCellCol = static_cast<uint16_t>(_mazeCols - 1);
        break;
    case 2:
        _startCellRow = static_cast<uint16_t>(_mazeRows - 1);
        _startCellCol = 0;
        break;
    case 3:
        _startCellRow = static_cast<uint16_t>(_mazeRows - 1);
        _startCellCol = static_cast<uint16_t>(_mazeCols - 1);
        break;
    default:
        _startCellRow = 0;
        _startCellCol = 0;
        break;
    }

    _mazeCells.assign(static_cast<size_t>(_mazeRows) * static_cast<size_t>(_mazeCols), MazeCell{});
    std::mt19937 rng(0x4C414259U + static_cast<uint32_t>(level_index) * 977U);
    std::vector<uint16_t> stack;
    stack.reserve(_mazeRows * _mazeCols);
    const auto index_of = [this](uint16_t row, uint16_t col) {
        return static_cast<size_t>(row) * static_cast<size_t>(_mazeCols) + static_cast<size_t>(col);
    };

    stack.push_back(static_cast<uint16_t>(index_of(_startCellRow, _startCellCol)));
    _mazeCells[index_of(_startCellRow, _startCellCol)].visited = true;

    while (!stack.empty()) {
        const uint16_t current = stack.back();
        const uint16_t row = static_cast<uint16_t>(current / _mazeCols);
        const uint16_t col = static_cast<uint16_t>(current % _mazeCols);

        struct Neighbor {
            uint16_t row;
            uint16_t col;
            uint8_t direction;
        };
        std::array<Neighbor, 4> neighbors = {};
        size_t neighbor_count = 0;

        if ((row > 0) && !_mazeCells[index_of(static_cast<uint16_t>(row - 1), col)].visited) {
            neighbors[neighbor_count++] = {static_cast<uint16_t>(row - 1), col, 0};
        }
        if ((col + 1 < _mazeCols) && !_mazeCells[index_of(row, static_cast<uint16_t>(col + 1))].visited) {
            neighbors[neighbor_count++] = {row, static_cast<uint16_t>(col + 1), 1};
        }
        if ((row + 1 < _mazeRows) && !_mazeCells[index_of(static_cast<uint16_t>(row + 1), col)].visited) {
            neighbors[neighbor_count++] = {static_cast<uint16_t>(row + 1), col, 2};
        }
        if ((col > 0) && !_mazeCells[index_of(row, static_cast<uint16_t>(col - 1))].visited) {
            neighbors[neighbor_count++] = {row, static_cast<uint16_t>(col - 1), 3};
        }

        if (neighbor_count == 0) {
            stack.pop_back();
            continue;
        }

        const Neighbor &next = neighbors[std::uniform_int_distribution<size_t>(0, neighbor_count - 1)(rng)];
        MazeCell &current_cell = _mazeCells[index_of(row, col)];
        MazeCell &next_cell = _mazeCells[index_of(next.row, next.col)];
        switch (next.direction) {
        case 0:
            current_cell.north = false;
            next_cell.south = false;
            break;
        case 1:
            current_cell.east = false;
            next_cell.west = false;
            break;
        case 2:
            current_cell.south = false;
            next_cell.north = false;
            break;
        default:
            current_cell.west = false;
            next_cell.east = false;
            break;
        }

        next_cell.visited = true;
        stack.push_back(static_cast<uint16_t>(index_of(next.row, next.col)));
    }

    buildMazeRaster();
}

void LabyrinthApp::buildMazeRaster()
{
    _mazeRasterRows = static_cast<uint16_t>(_mazeRows * 2 + 1);
    _mazeRasterCols = static_cast<uint16_t>(_mazeCols * 2 + 1);
    _mazeRaster.assign(static_cast<size_t>(_mazeRasterRows) * static_cast<size_t>(_mazeRasterCols), 1);

    const auto raster_index = [this](uint16_t row, uint16_t col) {
        return static_cast<size_t>(row) * static_cast<size_t>(_mazeRasterCols) + static_cast<size_t>(col);
    };

    for (uint16_t row = 0; row < _mazeRows; ++row) {
        for (uint16_t col = 0; col < _mazeCols; ++col) {
            const MazeCell &cell = _mazeCells[static_cast<size_t>(row) * _mazeCols + col];
            const uint16_t rr = static_cast<uint16_t>(row * 2 + 1);
            const uint16_t rc = static_cast<uint16_t>(col * 2 + 1);
            _mazeRaster[raster_index(rr, rc)] = 0;
            if (!cell.north) {
                _mazeRaster[raster_index(static_cast<uint16_t>(rr - 1), rc)] = 0;
            }
            if (!cell.east) {
                _mazeRaster[raster_index(rr, static_cast<uint16_t>(rc + 1))] = 0;
            }
            if (!cell.south) {
                _mazeRaster[raster_index(static_cast<uint16_t>(rr + 1), rc)] = 0;
            }
            if (!cell.west) {
                _mazeRaster[raster_index(rr, static_cast<uint16_t>(rc - 1))] = 0;
            }
        }
    }
}

void LabyrinthApp::renderMaze()
{
    if ((_board == nullptr) || (_mazeLayer == nullptr)) {
        return;
    }

    clearWallObjects();
    _mazeOffsetX = 12.0f;
    _mazeOffsetY = 12.0f;
    _tileSize = (_boardSize - (_mazeOffsetX * 2.0f)) / static_cast<float>(_mazeRasterCols);
    _ballRadius = _tileSize * 0.30f;
    _holeRadius = _tileSize * 0.42f;

    for (uint16_t row = 0; row < _mazeRasterRows; ++row) {
        for (uint16_t col = 0; col < _mazeRasterCols; ++col) {
            if (_mazeRaster[static_cast<size_t>(row) * _mazeRasterCols + col] == 0) {
                continue;
            }
            lv_obj_t *wall = lv_obj_create(_mazeLayer);
            lv_obj_set_pos(wall,
                           static_cast<lv_coord_t>(std::lround(_mazeOffsetX + static_cast<float>(col) * _tileSize)),
                           static_cast<lv_coord_t>(std::lround(_mazeOffsetY + static_cast<float>(row) * _tileSize)));
            lv_obj_set_size(wall,
                            std::max<lv_coord_t>(1, static_cast<lv_coord_t>(std::ceil(_tileSize))),
                            std::max<lv_coord_t>(1, static_cast<lv_coord_t>(std::ceil(_tileSize))));
            lv_obj_set_style_radius(wall, 6, 0);
            lv_obj_set_style_border_width(wall, 0, 0);
            lv_obj_set_style_bg_color(wall, lv_color_hex(0x475569), 0);
            lv_obj_set_style_bg_grad_color(wall, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_bg_grad_dir(wall, LV_GRAD_DIR_VER, 0);
            lv_obj_set_style_shadow_width(wall, 6, 0);
            lv_obj_set_style_shadow_opa(wall, 28, 0);
            lv_obj_set_style_shadow_color(wall, lv_color_hex(0x0F172A), 0);
            lv_obj_clear_flag(wall, LV_OBJ_FLAG_SCROLLABLE);
            _wallObjects.push_back(wall);
        }
    }

    syncHoleVisual();
}

void LabyrinthApp::syncHoleVisual()
{
    if (_hole == nullptr) {
        return;
    }
    const float hole_center_x = _mazeOffsetX + (static_cast<float>(_goalCellCol * 2 + 1) + 0.5f) * _tileSize;
    const float hole_center_y = _mazeOffsetY + (static_cast<float>(_goalCellRow * 2 + 1) + 0.5f) * _tileSize;
    const lv_coord_t diameter = static_cast<lv_coord_t>(std::lround(_holeRadius * 2.0f));
    lv_obj_set_size(_hole, diameter, diameter);
    lv_obj_set_style_radius(_hole, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_pos(_hole,
                   static_cast<lv_coord_t>(std::lround(hole_center_x - _holeRadius)),
                   static_cast<lv_coord_t>(std::lround(hole_center_y - _holeRadius)));
}

void LabyrinthApp::resetBallAtStart()
{
    _ballVx = 0.0f;
    _ballVy = 0.0f;
    _ballTexturePhaseX = 0.0f;
    _ballTexturePhaseY = 0.0f;
    _ballX = _mazeOffsetX + (static_cast<float>(_startCellCol * 2 + 1) + 0.5f) * _tileSize;
    _ballY = _mazeOffsetY + (static_cast<float>(_startCellRow * 2 + 1) + 0.5f) * _tileSize;
    syncBallVisual();
}

void LabyrinthApp::syncBallVisual()
{
    if (_ball == nullptr) {
        return;
    }
    const lv_coord_t diameter = static_cast<lv_coord_t>(std::lround(_ballRadius * 2.0f));
    lv_obj_set_size(_ball, diameter, diameter);
    lv_obj_set_style_radius(_ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_pos(_ball,
                   static_cast<lv_coord_t>(std::lround(_ballX - _ballRadius)),
                   static_cast<lv_coord_t>(std::lround(_ballY - _ballRadius)));

    if ((_ballTextureLayer == nullptr) || (_ballTextureBandA == nullptr) || (_ballTextureBandB == nullptr) ||
        (_ballTextureBandC == nullptr) || (_ballHighlight == nullptr) || (_ballShadow == nullptr)) {
        return;
    }

    const float dt = static_cast<float>(kTickPeriodMs) / 1000.0f;
    const float diameter_f = std::max(1.0f, static_cast<float>(diameter));
    const float texture_span = diameter_f * 1.6f;
    _ballTexturePhaseX = wrap_phase(_ballTexturePhaseX - (_ballVx * dt * 0.26f), texture_span);
    _ballTexturePhaseY = wrap_phase(_ballTexturePhaseY - (_ballVy * dt * 0.26f), texture_span);

    const lv_coord_t layer_size = static_cast<lv_coord_t>(std::lround(diameter_f * 1.8f));
    lv_obj_set_size(_ballTextureLayer, layer_size, layer_size);
    lv_obj_set_pos(_ballTextureLayer,
                   static_cast<lv_coord_t>(std::lround(-diameter_f * 0.40f)),
                   static_cast<lv_coord_t>(std::lround(-diameter_f * 0.40f)));

    const float speed = std::sqrt((_ballVx * _ballVx) + (_ballVy * _ballVy));
    const float motion_bias_x = clampf(_ballVx / 220.0f, -1.0f, 1.0f);
    const float motion_bias_y = clampf(_ballVy / 220.0f, -1.0f, 1.0f);

    const lv_coord_t band_wide_width = static_cast<lv_coord_t>(std::lround(diameter_f * 1.15f));
    const lv_coord_t band_wide_height = std::max<lv_coord_t>(4, static_cast<lv_coord_t>(std::lround(diameter_f * 0.18f)));
    const lv_coord_t band_mid_width = static_cast<lv_coord_t>(std::lround(diameter_f * 0.82f));
    const lv_coord_t band_mid_height = std::max<lv_coord_t>(3, static_cast<lv_coord_t>(std::lround(diameter_f * 0.14f)));
    const lv_coord_t band_dark_width = static_cast<lv_coord_t>(std::lround(diameter_f * 0.60f));
    const lv_coord_t band_dark_height = std::max<lv_coord_t>(3, static_cast<lv_coord_t>(std::lround(diameter_f * 0.10f)));

    lv_obj_set_size(_ballTextureBandA, band_wide_width, band_wide_height);
    lv_obj_set_pos(_ballTextureBandA,
                   static_cast<lv_coord_t>(std::lround(-diameter_f * 0.08f + _ballTexturePhaseX - texture_span * 0.50f)),
                   static_cast<lv_coord_t>(std::lround(diameter_f * 0.24f + _ballTexturePhaseY * 0.18f)));
    lv_obj_set_style_transform_angle(_ballTextureBandA, 260, 0);
    lv_obj_set_style_bg_opa(_ballTextureBandA,
                            static_cast<lv_opa_t>(std::lround(clampf(110.0f + speed * 0.10f, 96.0f, 168.0f))),
                            0);

    lv_obj_set_size(_ballTextureBandB, band_mid_width, band_mid_height);
    lv_obj_set_pos(_ballTextureBandB,
                   static_cast<lv_coord_t>(std::lround(diameter_f * 0.18f - _ballTexturePhaseX * 0.72f)),
                   static_cast<lv_coord_t>(std::lround(diameter_f * 0.74f - _ballTexturePhaseY * 0.34f)));
    lv_obj_set_style_transform_angle(_ballTextureBandB, 312, 0);
    lv_obj_set_style_bg_opa(_ballTextureBandB,
                            static_cast<lv_opa_t>(std::lround(clampf(90.0f + speed * 0.08f, 76.0f, 150.0f))),
                            0);

    lv_obj_set_size(_ballTextureBandC, band_dark_width, band_dark_height);
    lv_obj_set_pos(_ballTextureBandC,
                   static_cast<lv_coord_t>(std::lround(diameter_f * 0.48f - _ballTexturePhaseX * 0.42f)),
                   static_cast<lv_coord_t>(std::lround(diameter_f * 0.44f + _ballTexturePhaseY * 0.52f)));
    lv_obj_set_style_transform_angle(_ballTextureBandC, 218, 0);
    lv_obj_set_style_bg_opa(_ballTextureBandC,
                            static_cast<lv_opa_t>(std::lround(clampf(60.0f + speed * 0.06f, 52.0f, 104.0f))),
                            0);

    const lv_coord_t highlight_size = std::max<lv_coord_t>(6, static_cast<lv_coord_t>(std::lround(diameter_f * 0.34f)));
    lv_obj_set_size(_ballHighlight, highlight_size, highlight_size);
    lv_obj_set_pos(_ballHighlight,
                   static_cast<lv_coord_t>(std::lround(diameter_f * (0.18f - motion_bias_x * 0.05f))),
                   static_cast<lv_coord_t>(std::lround(diameter_f * (0.14f - motion_bias_y * 0.05f))));
    lv_obj_set_style_bg_opa(_ballHighlight,
                            static_cast<lv_opa_t>(std::lround(clampf(132.0f + speed * 0.04f, 118.0f, 172.0f))),
                            0);

    const lv_coord_t shadow_w = std::max<lv_coord_t>(8, static_cast<lv_coord_t>(std::lround(diameter_f * 0.52f)));
    const lv_coord_t shadow_h = std::max<lv_coord_t>(5, static_cast<lv_coord_t>(std::lround(diameter_f * 0.22f)));
    lv_obj_set_size(_ballShadow, shadow_w, shadow_h);
    lv_obj_set_pos(_ballShadow,
                   static_cast<lv_coord_t>(std::lround(diameter_f * (0.34f + motion_bias_x * 0.07f))),
                   static_cast<lv_coord_t>(std::lround(diameter_f * (0.60f + motion_bias_y * 0.05f))));
    lv_obj_set_style_transform_angle(_ballShadow, 180, 0);
    lv_obj_set_style_bg_opa(_ballShadow,
                            static_cast<lv_opa_t>(std::lround(clampf(74.0f + speed * 0.03f, 64.0f, 96.0f))),
                            0);
}

void LabyrinthApp::updateHud()
{
    if (_levelLabel != nullptr) {
        lv_label_set_text_fmt(_levelLabel, "Level %u/%u", static_cast<unsigned>(_currentLevel + 1), static_cast<unsigned>(kLevelCount));
    }
    if (_scoreLabel != nullptr) {
        lv_label_set_text_fmt(_scoreLabel, "Score %lu", static_cast<unsigned long>(_score));
    }
    if (_timerLabel != nullptr) {
        const uint32_t total_seconds = (_levelTimeRemainingMs + 999U) / 1000U;
        lv_label_set_text_fmt(_timerLabel,
                              "Time %02lu:%02lu",
                              static_cast<unsigned long>(total_seconds / 60U),
                              static_cast<unsigned long>(total_seconds % 60U));
    }
}

float LabyrinthApp::sampleControlAxisX() const
{
    return 0.0f;
}

float LabyrinthApp::sampleControlAxisY() const
{
    return 0.0f;
}

void LabyrinthApp::loadAxisMappings()
{
    if (_nvsHandle == 0) {
        _axisMapX = kAxisMappingPosX;
        _axisMapY = kAxisMappingPosY;
        _axisMapZ = kAxisMappingPosZ;
        return;
    }

    uint8_t value = 0;
    if (nvs_get_u8(_nvsHandle, kNvsAxisMapXKey, &value) == ESP_OK) {
        _axisMapX = sanitize_axis_mapping(value, kAxisMappingPosX);
    }
    if (nvs_get_u8(_nvsHandle, kNvsAxisMapYKey, &value) == ESP_OK) {
        _axisMapY = sanitize_axis_mapping(value, kAxisMappingPosY);
    }
    if (nvs_get_u8(_nvsHandle, kNvsAxisMapZKey, &value) == ESP_OK) {
        _axisMapZ = sanitize_axis_mapping(value, kAxisMappingPosZ);
    }
}

void LabyrinthApp::saveAxisMappings() const
{
    if (_nvsHandle == 0) {
        return;
    }

    bool ok = true;
    ok = ok && (nvs_set_u8(_nvsHandle, kNvsAxisMapXKey, _axisMapX) == ESP_OK);
    ok = ok && (nvs_set_u8(_nvsHandle, kNvsAxisMapYKey, _axisMapY) == ESP_OK);
    ok = ok && (nvs_set_u8(_nvsHandle, kNvsAxisMapZKey, _axisMapZ) == ESP_OK);
    if (ok && (nvs_commit(_nvsHandle) == ESP_OK)) {
        return;
    }

    ESP_LOGW(kTag, "Failed to persist axis mapping settings");
}

bool LabyrinthApp::readImuSample(float &control_x, float &control_y)
{
    control_x = 0.0f;
    control_y = 0.0f;

    jc4880::imu::ImuSample sample = {};
    bool sample_ok = jc4880::imu::ImuService::instance().getLastSample(sample);
    if (!sample_ok) {
        sample_ok = jc4880::imu::ImuService::instance().read(sample);
    }
    if (!sample_ok) {
        ensureImuReady();
        return false;
    }

    if (sample.hasAccel || sample.hasFusion) {
        constexpr float kTiltDeadzoneDegrees = 3.5f;
        constexpr float kFullTiltDegrees = 32.0f;
        auto normalize_tilt = [](float angle_degrees) -> float {
            const float magnitude = std::fabs(angle_degrees);
            if (magnitude <= kTiltDeadzoneDegrees) {
                return 0.0f;
            }

            const float normalized = clampf((magnitude - kTiltDeadzoneDegrees) /
                                                (kFullTiltDegrees - kTiltDeadzoneDegrees),
                                            0.0f,
                                            1.0f);
            const float curved = normalized * normalized;
            return std::copysign(curved, angle_degrees);
        };

        const float mapped_x = select_axis_mapping_component(sample.pitch, sample.roll, sample.yaw, _axisMapX);
        const float mapped_y = select_axis_mapping_component(sample.pitch, sample.roll, sample.yaw, _axisMapY);
        control_x = normalize_tilt(mapped_x);
        control_y = normalize_tilt(mapped_y);
    } else if (sample.hasGyro) {
        const float mapped_x = select_axis_mapping_component(sample.gx, sample.gy, sample.gz, _axisMapX);
        const float mapped_y = select_axis_mapping_component(sample.gx, sample.gy, sample.gz, _axisMapY);
        control_x = clampf(mapped_x / 220.0f, -1.0f, 1.0f);
        control_y = clampf(mapped_y / 220.0f, -1.0f, 1.0f);
    } else {
        return false;
    }

    return true;
}

bool LabyrinthApp::isPixelBlocked(float local_x, float local_y) const
{
    if ((local_x < _mazeOffsetX) || (local_y < _mazeOffsetY)) {
        return true;
    }

    const float max_x = _mazeOffsetX + static_cast<float>(_mazeRasterCols) * _tileSize;
    const float max_y = _mazeOffsetY + static_cast<float>(_mazeRasterRows) * _tileSize;
    if ((local_x >= max_x) || (local_y >= max_y)) {
        return true;
    }

    const int32_t col = static_cast<int32_t>((local_x - _mazeOffsetX) / _tileSize);
    const int32_t row = static_cast<int32_t>((local_y - _mazeOffsetY) / _tileSize);
    if ((row < 0) || (col < 0) || (row >= _mazeRasterRows) || (col >= _mazeRasterCols)) {
        return true;
    }

    return _mazeRaster[static_cast<size_t>(row) * _mazeRasterCols + static_cast<size_t>(col)] != 0;
}

bool LabyrinthApp::isBallBlocked(float center_x, float center_y) const
{
    static constexpr std::array<float, 8> kAngles = {0.0f, 0.785398f, 1.570796f, 2.356194f,
                                                     3.141593f, 3.926991f, 4.712389f, 5.497787f};
    if (isPixelBlocked(center_x, center_y)) {
        return true;
    }
    for (float angle: kAngles) {
        const float sample_x = center_x + std::cos(angle) * _ballRadius;
        const float sample_y = center_y + std::sin(angle) * _ballRadius;
        if (isPixelBlocked(sample_x, sample_y)) {
            return true;
        }
    }
    return false;
}

float LabyrinthApp::resolveAxisMove(float current, float delta, bool is_x_axis)
{
    const float original_x = _ballX;
    const float original_y = _ballY;
    float accepted = current;
    float low = 0.0f;
    float high = 1.0f;

    for (int iteration = 0; iteration < 7; ++iteration) {
        const float mid = (low + high) * 0.5f;
        const float candidate = current + delta * mid;
        const float test_x = is_x_axis ? candidate : original_x;
        const float test_y = is_x_axis ? original_y : candidate;
        if (!isBallBlocked(test_x, test_y)) {
            accepted = candidate;
            low = mid;
        } else {
            high = mid;
        }
    }

    return accepted;
}

bool LabyrinthApp::isGoalReached() const
{
    const float goal_x = _mazeOffsetX + (static_cast<float>(_goalCellCol * 2 + 1) + 0.5f) * _tileSize;
    const float goal_y = _mazeOffsetY + (static_cast<float>(_goalCellRow * 2 + 1) + 0.5f) * _tileSize;
    const float dx = _ballX - goal_x;
    const float dy = _ballY - goal_y;
    const float target_radius = std::max(4.0f, _holeRadius * 0.55f);
    return ((dx * dx) + (dy * dy)) <= (target_radius * target_radius);
}

void LabyrinthApp::completeLevel()
{
    const uint32_t bonus = ((_levelTimeRemainingMs + 999U) / 1000U) * 10U +
                           static_cast<uint32_t>(_currentLevel + 1) * 125U;
    _score += bonus;

    if (_currentLevel + 1 >= kLevelCount) {
        finishRun(true);
        return;
    }

    if (_statusLabel != nullptr) {
        lv_label_set_text_fmt(_statusLabel,
                              "Level %u clear. The ball dropped into the center hole for %lu points.",
                              static_cast<unsigned>(_currentLevel + 1),
                              static_cast<unsigned long>(bonus));
    }
    loadLevel(static_cast<uint16_t>(_currentLevel + 1));
}

void LabyrinthApp::tickGame()
{
    if (_paused || _runFinished) {
        return;
    }

    float control_x = 0.0f;
    float control_y = 0.0f;
    const bool sample_ok = readImuSample(control_x, control_y);
    const float dt = static_cast<float>(kTickPeriodMs) / 1000.0f;

    if (sample_ok) {
        constexpr float kMaxSpeed = 360.0f;
        constexpr float kVelocityResponse = 7.5f;
        const float response = clampf(dt * kVelocityResponse, 0.0f, 1.0f);
        const float target_vx = control_x * kMaxSpeed;
        const float target_vy = control_y * kMaxSpeed;
        _ballVx += (target_vx - _ballVx) * response;
        _ballVy += (target_vy - _ballVy) * response;
    } else {
        _ballVx *= 0.88f;
        _ballVy *= 0.88f;
    }

    _ballVx *= 0.992f;
    _ballVy *= 0.992f;

    const float target_x = _ballX + _ballVx * dt;
    const float resolved_x = resolveAxisMove(_ballX, target_x - _ballX, true);
    if (std::fabs(resolved_x - target_x) > 0.5f) {
        _ballVx = 0.0f;
    }
    _ballX = resolved_x;

    const float target_y = _ballY + _ballVy * dt;
    const float resolved_y = resolveAxisMove(_ballY, target_y - _ballY, false);
    if (std::fabs(resolved_y - target_y) > 0.5f) {
        _ballVy = 0.0f;
    }
    _ballY = resolved_y;

    syncBallVisual();

    if (_levelTimeRemainingMs > kTickPeriodMs) {
        _levelTimeRemainingMs -= kTickPeriodMs;
    } else {
        _levelTimeRemainingMs = 0;
    }
    updateHud();

    if (isGoalReached()) {
        completeLevel();
        return;
    }

    if (_levelTimeRemainingMs == 0) {
        finishRun(false);
    }
}

std::string LabyrinthApp::buildRatingText(uint32_t score, uint16_t level_reached) const
{
    if ((level_reached >= kLevelCount) || (score >= 50000U)) {
        return "Labyrinth Legend";
    }
    if ((level_reached >= 60) || (score >= 25000U)) {
        return "Master Navigator";
    }
    if ((level_reached >= 30) || (score >= 12000U)) {
        return "Skilled Pathfinder";
    }
    if ((level_reached >= 10) || (score >= 4000U)) {
        return "Rising Explorer";
    }
    return "Maze Rookie";
}

void LabyrinthApp::appendAttemptRecord()
{
    AttemptRecord record = {};
    record.score = _score;
    record.level_reached = _bestLevelThisRun;
    record.elapsed_sec = static_cast<uint32_t>((static_cast<uint64_t>(esp_timer_get_time()) - _runStartUs) / 1000000ULL);
    record.timestamp_sec = seconds_since_boot();

    if (_attemptCount < kHistoryCapacity) {
        _attemptHistory[_attemptCount++] = record;
    } else {
        for (size_t index = 1; index < kHistoryCapacity; ++index) {
            _attemptHistory[index - 1] = _attemptHistory[index];
        }
        _attemptHistory[kHistoryCapacity - 1] = record;
    }
    saveHistory();
}

bool LabyrinthApp::loadHistory()
{
    if (_nvsHandle == 0) {
        _attemptCount = 0;
        return false;
    }

    HistoryBlob blob = {};
    size_t length = sizeof(blob);
    const esp_err_t err = nvs_get_blob(_nvsHandle, kNvsHistoryKey, &blob, &length);
    if ((err != ESP_OK) || (length != sizeof(blob)) || (blob.version != kHistoryVersion)) {
        _attemptCount = 0;
        return false;
    }

    _attemptCount = std::min<size_t>(blob.count, kHistoryCapacity);
    for (size_t index = 0; index < _attemptCount; ++index) {
        _attemptHistory[index] = blob.attempts[index];
    }
    return true;
}

bool LabyrinthApp::saveHistory()
{
    if (_nvsHandle == 0) {
        return false;
    }

    HistoryBlob blob = {};
    blob.version = kHistoryVersion;
    blob.count = static_cast<uint32_t>(_attemptCount);
    for (size_t index = 0; index < _attemptCount; ++index) {
        blob.attempts[index] = _attemptHistory[index];
    }
    if (nvs_set_blob(_nvsHandle, kNvsHistoryKey, &blob, sizeof(blob)) != ESP_OK) {
        return false;
    }
    return nvs_commit(_nvsHandle) == ESP_OK;
}

void LabyrinthApp::finishRun(bool completed_all_levels)
{
    if (_runFinished) {
        return;
    }
    _runFinished = true;
    appendAttemptRecord();
    showResultModal(completed_all_levels);
}

void LabyrinthApp::closeResultModal()
{
    if ((_resultOverlay != nullptr) && lv_obj_is_valid(_resultOverlay)) {
        lv_obj_del(_resultOverlay);
    }
    _resultOverlay = nullptr;
    _resultPanel = nullptr;
    _resultSummaryLabel = nullptr;
    _resultRatingLabel = nullptr;
    _resultChart = nullptr;
    _resultSeries = nullptr;
}

void LabyrinthApp::showResultModal(bool completed_all_levels)
{
    closeResultModal();

    _resultOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_resultOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(_resultOverlay, 0, 0);
    lv_obj_set_style_border_width(_resultOverlay, 0, 0);
    lv_obj_set_style_bg_color(_resultOverlay, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(_resultOverlay, 178, 0);
    lv_obj_set_style_pad_all(_resultOverlay, 24, 0);

    _resultPanel = lv_obj_create(_resultOverlay);
    lv_obj_set_size(_resultPanel, lv_pct(92), lv_pct(74));
    lv_obj_center(_resultPanel);
    lv_obj_set_style_radius(_resultPanel, 28, 0);
    lv_obj_set_style_border_width(_resultPanel, 0, 0);
    lv_obj_set_style_pad_all(_resultPanel, 22, 0);
    lv_obj_set_style_pad_row(_resultPanel, 14, 0);
    lv_obj_set_style_bg_color(_resultPanel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_shadow_width(_resultPanel, 36, 0);
    lv_obj_set_style_shadow_opa(_resultPanel, 44, 0);
    lv_obj_set_style_shadow_color(_resultPanel, lv_color_hex(0x020617), 0);
    lv_obj_set_flex_flow(_resultPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_resultPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(_resultPanel);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0F172A), 0);
    lv_label_set_text(title, completed_all_levels ? "100 Levels Cleared" : "Run Over");

    _resultSummaryLabel = lv_label_create(_resultPanel);
    lv_obj_set_width(_resultSummaryLabel, lv_pct(100));
    lv_obj_set_style_text_align(_resultSummaryLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(_resultSummaryLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_resultSummaryLabel, lv_color_hex(0x334155), 0);
    lv_label_set_text_fmt(_resultSummaryLabel,
                          "Score %lu\nBest Level %u/%u\nElapsed %lu sec",
                          static_cast<unsigned long>(_score),
                          static_cast<unsigned>(_bestLevelThisRun),
                          static_cast<unsigned>(kLevelCount),
                          static_cast<unsigned long>((static_cast<uint64_t>(esp_timer_get_time()) - _runStartUs) / 1000000ULL));

    _resultRatingLabel = lv_label_create(_resultPanel);
    lv_obj_set_style_text_font(_resultRatingLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_resultRatingLabel, lv_color_hex(0x0369A1), 0);
    lv_label_set_text_fmt(_resultRatingLabel, "Rating: %s", buildRatingText(_score, _bestLevelThisRun).c_str());

    lv_obj_t *chartTitle = lv_label_create(_resultPanel);
    lv_obj_set_style_text_font(chartTitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(chartTitle, lv_color_hex(0x475569), 0);
    lv_label_set_text(chartTitle, "Last 10 attempts");

    _resultChart = lv_chart_create(_resultPanel);
    lv_obj_set_size(_resultChart, lv_pct(100), 220);
    lv_obj_set_style_bg_color(_resultChart, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_bg_opa(_resultChart, 255, 0);
    lv_obj_set_style_border_width(_resultChart, 0, 0);
    lv_obj_set_style_radius(_resultChart, 18, 0);
    lv_chart_set_type(_resultChart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(_resultChart, static_cast<uint16_t>(kHistoryCapacity));
    lv_chart_set_div_line_count(_resultChart, 4, static_cast<uint8_t>(kHistoryCapacity));

    uint32_t max_score = 1000;
    for (size_t index = 0; index < _attemptCount; ++index) {
        max_score = std::max(max_score, _attemptHistory[index].score + 250U);
    }
    lv_chart_set_range(_resultChart, LV_CHART_AXIS_PRIMARY_Y, 0, static_cast<lv_coord_t>(max_score));
    _resultSeries = lv_chart_add_series(_resultChart, lv_color_hex(0x0EA5E9), LV_CHART_AXIS_PRIMARY_Y);
    if (_resultSeries != nullptr) {
        for (size_t index = 0; index < kHistoryCapacity; ++index) {
            const lv_coord_t value = (index < _attemptCount)
                                         ? static_cast<lv_coord_t>(_attemptHistory[index].score)
                                         : 0;
            lv_chart_set_next_value(_resultChart, _resultSeries, value);
        }
    }
    lv_chart_refresh(_resultChart);

    lv_obj_t *buttonRow = lv_obj_create(_resultPanel);
    lv_obj_set_size(buttonRow, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(buttonRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(buttonRow, 0, 0);
    lv_obj_set_style_pad_all(buttonRow, 0, 0);
    lv_obj_set_style_pad_column(buttonRow, 12, 0);
    lv_obj_set_flex_flow(buttonRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttonRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *retry = lv_btn_create(buttonRow);
    lv_obj_set_size(retry, 150, 54);
    lv_obj_set_style_radius(retry, 24, 0);
    lv_obj_set_style_border_width(retry, 0, 0);
    lv_obj_set_style_bg_color(retry, lv_color_hex(0x0EA5E9), 0);
    lv_obj_add_event_cb(retry, onRetryEvent, LV_EVENT_CLICKED, this);
    lv_obj_t *retry_label = lv_label_create(retry);
    lv_obj_set_style_text_font(retry_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(retry_label, lv_color_white(), 0);
    lv_label_set_text(retry_label, "Retry");
    lv_obj_center(retry_label);

    lv_obj_t *quit = lv_btn_create(buttonRow);
    lv_obj_set_size(quit, 150, 54);
    lv_obj_set_style_radius(quit, 24, 0);
    lv_obj_set_style_border_width(quit, 0, 0);
    lv_obj_set_style_bg_color(quit, lv_color_hex(0x1E293B), 0);
    lv_obj_add_event_cb(quit, onQuitEvent, LV_EVENT_CLICKED, this);
    lv_obj_t *quit_label = lv_label_create(quit);
    lv_obj_set_style_text_font(quit_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(quit_label, lv_color_white(), 0);
    lv_label_set_text(quit_label, "Quit");
    lv_obj_center(quit_label);
}