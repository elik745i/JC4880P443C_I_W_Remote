/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <fcntl.h>
#include <dirent.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "VideoPlayer.hpp"

#define APP_MJPEG_PATH              "/sdcard/mjpeg"
#define APP_SUPPORT_VIDEO_FILE_EXT  ".mjpeg"
#define APP_MAX_VIDEO_NUM           (15)

static const char *TAG = "AppVideoPlayer";

static jpeg_decode_cfg_t decode_cfg_rgb = {
    .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
    .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
};

static jpeg_decode_memory_alloc_cfg_t rx_mem_cfg = {
    .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
};

LV_IMG_DECLARE(img_app_video_player);

AppVideoPlayer::AppVideoPlayer()
    : ESP_Brookesia_PhoneApp("Video Player", &img_app_video_player, true)
{
    memset(_video_path, 0, sizeof(_video_path));
    _video_name = nullptr;

    playing = false;
    play_end = true;
    cnt = 0;
    avi_cnt = 0;
    index = 0;

    video_background = nullptr;
    video_canvas = nullptr;
    _file_iterator = nullptr;
    avi_handle = nullptr;
    semph_event = nullptr;
    video_task_event = nullptr;
    avi_jpgd_handle = nullptr;

    jpeg_data_buffer[0] = nullptr;
    jpeg_data_buffer[1] = nullptr;
    jpeg_buf_size[0] = 0;
    jpeg_buf_size[1] = 0;

    mjpeg_data_queue = nullptr;
}

AppVideoPlayer::~AppVideoPlayer()
{
    close();
}

/* ================= run ================= */
bool AppVideoPlayer::run(void)
{
    if (playing) {
        close();
    }

    if (!init()) {
        ESP_LOGE(TAG, "init failed");
        return false;
    }

    bsp_display_lock(0);
    video_background = lv_obj_create(NULL);
    lv_obj_set_size(video_background, 480, 800);
    lv_obj_clear_flag(video_background, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(video_background, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(video_background, 255, 0);

    video_canvas = lv_canvas_create(video_background);
    lv_obj_set_size(video_canvas, 480, 800);
    lv_obj_align(video_canvas, LV_ALIGN_CENTER, 0, 0);

    lv_scr_load(video_background);
    bsp_display_unlock();

    jpeg_data_buffer[0] = (uint8_t *)jpeg_alloc_decoder_mem(
        800 * 480 * 2, &rx_mem_cfg, &jpeg_buf_size[0]);
    jpeg_data_buffer[1] = (uint8_t *)jpeg_alloc_decoder_mem(
        800 * 480 * 2, &rx_mem_cfg, &jpeg_buf_size[1]);

    playing = true;
    play_end = false;
    cnt = 0;

    xTaskCreatePinnedToCore(
        play_avi_task,
        "avi_play_task",
        4096,
        this,
        4,
        nullptr,
        1
    );

    xSemaphoreGive(semph_event);
    return true;
}

/* ================= pause ================= */
/* 只 mute，绝不 stop codec */
bool AppVideoPlayer::pause(void)
{
    ESP_LOGI(TAG, "video pause");

    if (avi_handle) {
        avi_player_play_stop(avi_handle);
    }

    bsp_extra_codec_mute_set(true);
    return true;
}

/* ================= resume ================= */
bool AppVideoPlayer::resume(void)
{
    ESP_LOGI(TAG, "video resume");

    if (!playing) {
        return false;
    }

    bsp_extra_codec_mute_set(false);

    if (_video_path[0] != '\0') {
        avi_player_play_from_file(avi_handle, _video_path);
    }
    return true;
}

/* ================= back ================= */
bool AppVideoPlayer::back(void)
{
     //close();
    return  true;
}

/* ================= close ================= */
/* ★ 核心：不 stop 音频硬件 */
bool AppVideoPlayer::close(void)
{
    ESP_LOGI(TAG, "Closing video player");

    playing = false;
    play_end = true;

    /* 只 mute，不 stop */
    bsp_extra_codec_mute_set(true);

    if (avi_handle) {
        avi_player_play_stop(avi_handle);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (semph_event) {
        xSemaphoreGive(semph_event);
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    if (avi_jpgd_handle) {
        jpeg_del_decoder_engine(avi_jpgd_handle);
        avi_jpgd_handle = nullptr;
    }

    if (semph_event) {
        vSemaphoreDelete(semph_event);
        semph_event = nullptr;
    }

    if (video_task_event) {
        vSemaphoreDelete(video_task_event);
        video_task_event = nullptr;
    }

    if (jpeg_data_buffer[0]) {
        free(jpeg_data_buffer[0]);
        jpeg_data_buffer[0] = nullptr;
    }

    if (jpeg_data_buffer[1]) {
        free(jpeg_data_buffer[1]);
        jpeg_data_buffer[1] = nullptr;
    }

    if (mjpeg_data_queue) {
        vQueueDelete(mjpeg_data_queue);
        mjpeg_data_queue = nullptr;
    }

    if (avi_handle) {
        avi_player_deinit(avi_handle);
        avi_handle = nullptr;
    }

    bsp_display_lock(0);
    if (video_canvas) {
        lv_obj_del(video_canvas);
        video_canvas = nullptr;
    }
    if (video_background) {
        lv_obj_del(video_background);
        video_background = nullptr;
    }
    bsp_display_unlock();

    /* ★ 给 Echo 留活的音频 */
    bsp_extra_codec_mute_set(false);

    ESP_LOGI(TAG, "Video closed, audio kept alive");
    return true;
}

/* ================= init ================= */
bool AppVideoPlayer::init(void)
{
    ESP_LOGI(TAG, "video init");

    ESP_ERROR_CHECK(bsp_extra_codec_init());
    ESP_ERROR_CHECK(bsp_extra_player_init());
    bsp_extra_codec_volume_set(60, nullptr);
    bsp_extra_codec_mute_set(false);

    esp_err_t ret = bsp_extra_file_instance_init(APP_MJPEG_PATH, &_file_iterator);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Video directory unavailable at %s: %s", APP_MJPEG_PATH, esp_err_to_name(ret));
        return false;
    }

    avi_cnt = file_iterator_get_count(_file_iterator);

    avi_player_config_t config = {
        .buffer_size = 60 * 1024,
        .video_cb = video_write,
        .audio_cb = audio_write,
        .audio_set_clock_cb = audio_set_clock,
        .avi_play_end_cb = avi_play_end,
        .priority = 4,
        .coreID = 0,
        .user_data = this,
        .stack_size = 4096,
    };

    ret = avi_player_init(config, &avi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "avi_player_init failed: %s", esp_err_to_name(ret));
        return false;
    }

    semph_event = xSemaphoreCreateBinary();
    video_task_event = xSemaphoreCreateBinary();

    jpeg_decode_engine_cfg_t jpeg_cfg = {
        .timeout_ms = 40,
    };
    ret = jpeg_new_decoder_engine(&jpeg_cfg, &avi_jpgd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "jpeg_new_decoder_engine failed: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

/* ================= callbacks ================= */
void AppVideoPlayer::audio_set_clock(uint32_t rate, uint32_t bits, uint32_t ch, void *arg)
{
    bsp_extra_codec_set_fs_play(rate, bits, I2S_SLOT_MODE_STEREO);
}

void AppVideoPlayer::audio_write(frame_data_t *data, void *arg)
{
    if (!data || data->data_bytes == 0) {
        return;
    }

    size_t write_bytes;
    bsp_extra_i2s_write(data->data, data->data_bytes, &write_bytes, 0);
}

void AppVideoPlayer::video_write(frame_data_t *data, void *arg)
{
    AppVideoPlayer *app = (AppVideoPlayer *)arg;
    if (!app->playing || data->data_bytes == 0) {
        return;
    }

    app->index ^= 1;
    uint32_t out_size = 0;

    if (jpeg_decoder_process(
            app->avi_jpgd_handle,
            &decode_cfg_rgb,
            data->data,
            data->data_bytes,
            app->jpeg_data_buffer[app->index],
            app->jpeg_buf_size[app->index],
            &out_size) != ESP_OK) {
        return;
    }

    bsp_display_lock(0);
    lv_canvas_set_buffer(
        app->video_canvas,
        app->jpeg_data_buffer[app->index],
        480,
        800,
        LV_IMG_CF_TRUE_COLOR
    );
    bsp_display_unlock();
}

void AppVideoPlayer::avi_play_end(void *arg)
{
    AppVideoPlayer *app = (AppVideoPlayer *)arg;
    if (app->playing) {
        app->play_end = true;
        ESP_LOGI(TAG, "video play end");
    }
}

void AppVideoPlayer::play_avi_task(void *arg)
{
    AppVideoPlayer *app = (AppVideoPlayer *)arg;

    bsp_extra_codec_mute_set(false);

    if (xSemaphoreTake(app->semph_event, portMAX_DELAY) == pdTRUE) {
        if (!app->playing) {
            vTaskDelete(nullptr);
            return;
        }

        file_iterator_get_full_path_from_index(
            app->_file_iterator,
            app->cnt,
            app->_video_path,
            sizeof(app->_video_path)
        );

        ESP_LOGI(TAG, "play video: %s", app->_video_path);
        avi_player_play_from_file(app->avi_handle, app->_video_path);
    }

    vTaskDelete(nullptr);
}
