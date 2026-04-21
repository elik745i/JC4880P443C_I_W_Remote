/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "sdkconfig.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"

#include "gui_music/music_player_ui.h"
#include "gui_music/music_player_main_ui.h"
#include "MusicPlayer.hpp"
#include "music_library.h"
#include "storage_access.h"

using namespace std;

LV_IMG_DECLARE(img_app_music_player);

static const char *TAG = "MusicPlayer";

MusicPlayer::MusicPlayer():
    ESP_Brookesia_PhoneApp("Music Player", &img_app_music_player, true)
{
}

MusicPlayer::~MusicPlayer()
{
}

bool MusicPlayer::run(void)
{
    music_library_init();
    music_player_ui_create(lv_scr_act());

    return true;
}

bool MusicPlayer::pause(void)
{
    ESP_LOGI(TAG, "pause");
    ESP_LOGI(TAG, "Keeping playback active while the app is minimized");
    return true;
}

bool MusicPlayer::back(void)
{
    ESP_LOGI(TAG, "back");
    _music_player_ui_exit_pause();

    const esp_err_t ret = audio_player_pause();
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE)) {
        ESP_LOGE(TAG, "audio_player_pause failed: %s", esp_err_to_name(ret));
        return false;
    }

    return notifyCoreClosed();
}

bool MusicPlayer::close(void)
{
    ESP_LOGI(TAG, "close");
    stop_audio_fully();

    music_player_ui_close();
    music_library_deinit();

    return true;
}

bool MusicPlayer::init(void)
{
    return true;
}

std::vector<ESP_Brookesia_PhoneQuickAccessActionData_t> MusicPlayer::getQuickAccessActions(void) const
{
    if (!checkInitialized()) {
        return {};
    }

    const bool has_tracks = (_music_player_ui_get_track_count() > 0);
    const char *playback_symbol = (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY;
    return {
        {QUICK_ACCESS_ACTION_PREVIOUS, LV_SYMBOL_PREV, has_tracks},
        {QUICK_ACCESS_ACTION_TOGGLE_PLAYBACK, playback_symbol, has_tracks},
        {QUICK_ACCESS_ACTION_NEXT, LV_SYMBOL_NEXT, has_tracks},
    };
}

ESP_Brookesia_PhoneQuickAccessDetailData_t MusicPlayer::getQuickAccessDetail(void) const
{
    if (!checkInitialized()) {
        return {};
    }

    const uint32_t track_count = _music_player_ui_get_track_count();
    if (track_count == 0) {
        return {
            .text = "No tracks",
            .scroll_text = false,
            .progress_percent = 0,
        };
    }

    uint32_t track_index = music_library_get_current_index();
    if (track_index >= track_count) {
        track_index = 0;
    }

    const char *title = _music_player_ui_get_title(track_index);
    const uint32_t elapsed = _music_player_ui_get_elapsed_time();
    const uint32_t duration = _music_player_ui_get_track_length(track_index);
    const int progress_percent = (duration > 0) ? std::min<int>((elapsed * 100U) / duration, 100U) : 0;

    return {
        .text = ((title != nullptr) && (title[0] != '\0')) ? title : getName(),
        .scroll_text = true,
        .progress_percent = progress_percent,
    };
}

bool MusicPlayer::handleQuickAccessAction(int action_id)
{
    switch (action_id) {
    case QUICK_ACCESS_ACTION_PREVIOUS:
        _music_player_ui_album_next(false);
        return true;
    case QUICK_ACCESS_ACTION_TOGGLE_PLAYBACK:
        if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
            _music_player_ui_pause();
            return true;
        }
        if (audio_player_get_state() == AUDIO_PLAYER_STATE_PAUSE) {
            _music_player_ui_resume();
            return true;
        }
        if (_music_player_ui_get_track_count() > 0) {
            _music_player_ui_resume();
            return true;
        }
        return false;
    case QUICK_ACCESS_ACTION_NEXT:
        _music_player_ui_album_next(true);
        return true;
    default:
        return false;
    }
}

void MusicPlayer::stop_audio_fully(void)
{
    /* 1. Stop the LVGL music UI state machine. */
    _music_player_ui_exit_pause();

    /* 2. Stop the shared audio player task. */
    audio_player_stop();

    /* 3. Stop the I2S hardware path. */
    bsp_extra_codec_dev_stop();

    /* 4. Mute the codec to avoid residual output. */
    bsp_extra_codec_mute_set(true);

    /* 5. Give DMA and I2S time to settle fully. */
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "Music audio fully stopped");
}