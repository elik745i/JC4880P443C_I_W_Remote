/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "sdkconfig.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"

#include "gui_music/lv_demo_music.h"
#include "gui_music/lv_demo_music_main.h"
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
    lv_demo_music(lv_scr_act());

    return true;
}

bool MusicPlayer::pause(void)
{
    ESP_LOGI(TAG, "pause");
    _lv_demo_music_pause();

    return true;
}

bool MusicPlayer::back(void)
{
    ESP_LOGI(TAG, "back");
    _lv_demo_music_exit_pause();

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

    lv_demo_music_close();
    music_library_deinit();

    return true;
}

bool MusicPlayer::init(void)
{
    return true;
}
void MusicPlayer::stop_audio_fully(void)
{
    /* 1. 退出 LV music（停 UI + 停内部状态机） */
    _lv_demo_music_exit_pause();

    /* 2. 停 audio_player 任务 */
    audio_player_stop();

    /* 3. 停 I2S 硬件 */
    bsp_extra_codec_dev_stop();

    /* 4. 静音 codec（防止残音） */
    bsp_extra_codec_mute_set(true);

    /* 5. 给一点时间让 DMA / I2S 真正停干净 */
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "Music audio fully stopped");
}