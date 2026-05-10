#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "esp_brookesia.hpp"
#include "lvgl.h"
#include "nvs.h"

class LabyrinthApp: public ESP_Brookesia_PhoneApp
{
public:
    LabyrinthApp();
    ~LabyrinthApp() override;

    bool init() override;
    bool run() override;
    bool pause() override;
    bool resume() override;
    bool back() override;
    bool close() override;

private:
    static constexpr uint16_t kLevelCount = 100;
    static constexpr size_t kHistoryCapacity = 10;
    static constexpr uint32_t kTickPeriodMs = 40;

    struct MazeCell {
        bool north = true;
        bool east = true;
        bool south = true;
        bool west = true;
        bool visited = false;
    };

    struct AttemptRecord {
        uint32_t score = 0;
        uint16_t level_reached = 0;
        uint16_t reserved = 0;
        uint32_t elapsed_sec = 0;
        uint32_t timestamp_sec = 0;
    };

    struct HistoryBlob {
        uint32_t version = 1;
        uint32_t count = 0;
        AttemptRecord attempts[kHistoryCapacity] = {};
    };

    static void onTick(lv_timer_t *timer);
    static void onRetryEvent(lv_event_t *event);
    static void onQuitEvent(lv_event_t *event);

    void buildUi();
    void destroyUi();
    void startNewRun();
    void loadLevel(uint16_t level_index);
    void generateLevel(uint16_t level_index);
    void buildMazeRaster();
    void renderMaze();
    void clearWallObjects();
    void resetBallAtStart();
    void updateHud();
    void tickGame();
    void completeLevel();
    void finishRun(bool completed_all_levels);
    void showResultModal(bool completed_all_levels);
    void closeResultModal();
    void appendAttemptRecord();
    bool loadHistory();
    bool saveHistory();
    uint32_t getLevelTimeLimitMs(uint16_t level_index) const;
    std::string buildRatingText(uint32_t score, uint16_t level_reached) const;
    float sampleControlAxisX() const;
    float sampleControlAxisY() const;
    bool readImuSample(float &control_x, float &control_y);
    bool isBallBlocked(float center_x, float center_y) const;
    bool isPixelBlocked(float local_x, float local_y) const;
    float resolveAxisMove(float current, float delta, bool is_x_axis);
    bool isGoalReached() const;
    void syncBallVisual();
    void syncHoleVisual();
    void ensureImuReady();

    bool _paused;
    bool _runFinished;
    bool _imuReady;
    bool _historyLoaded;
    uint16_t _currentLevel;
    uint16_t _bestLevelThisRun;
    uint32_t _score;
    uint32_t _levelTimeRemainingMs;
    uint64_t _runStartUs;
    uint16_t _mazeRows;
    uint16_t _mazeCols;
    uint16_t _startCellRow;
    uint16_t _startCellCol;
    uint16_t _goalCellRow;
    uint16_t _goalCellCol;
    uint16_t _mazeRasterRows;
    uint16_t _mazeRasterCols;
    float _boardSize;
    float _mazeOffsetX;
    float _mazeOffsetY;
    float _tileSize;
    float _ballRadius;
    float _holeRadius;
    float _ballX;
    float _ballY;
    float _ballVx;
    float _ballVy;
    nvs_handle_t _nvsHandle;
    std::vector<MazeCell> _mazeCells;
    std::vector<uint8_t> _mazeRaster;
    std::vector<lv_obj_t *> _wallObjects;
    std::array<AttemptRecord, kHistoryCapacity> _attemptHistory;
    size_t _attemptCount;

    lv_obj_t *_root;
    lv_obj_t *_titleLabel;
    lv_obj_t *_levelLabel;
    lv_obj_t *_scoreLabel;
    lv_obj_t *_timerLabel;
    lv_obj_t *_statusLabel;
    lv_obj_t *_board;
    lv_obj_t *_mazeLayer;
    lv_obj_t *_hole;
    lv_obj_t *_ball;
    lv_obj_t *_resultOverlay;
    lv_obj_t *_resultPanel;
    lv_obj_t *_resultSummaryLabel;
    lv_obj_t *_resultRatingLabel;
    lv_obj_t *_resultChart;
    lv_chart_series_t *_resultSeries;
    lv_timer_t *_tickTimer;
};