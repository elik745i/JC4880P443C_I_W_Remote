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