/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "esp_brookesia.hpp"
#include "file_iterator.h"
#include "avi_player.h"
#include "driver/jpeg_decode.h"

typedef struct
{
    uint8_t *frame;
    size_t len;

}jpeg_frame_data_t;

class AppVideoPlayer: public ESP_Brookesia_PhoneApp {
public:
    AppVideoPlayer();
    ~AppVideoPlayer();

    bool run(void);
    bool pause(void);
    bool resume(void);
    bool back(void);
    bool close(void);

    bool init(void) override;

    static void video_write(frame_data_t *data, void *arg);
    static void audio_write(frame_data_t *data, void *arg);
    static void audio_set_clock(uint32_t rate, uint32_t bits_cfg, uint32_t ch, void *arg);
    static void avi_play_end(void *arg);

    static void play_avi_task(void *arg);
    static void jpeg_decode_task(void *arg);

private:
    typedef struct {
        std::string video_name;
        // std::string bgm_path;
    } MideaInfo_t;

    char _video_path[256];
    const char *_video_name;
    bool playing;
    bool play_end = true;
    int cnt = 0;
    int avi_cnt;
    int index = 0;
    std::vector<MideaInfo_t> _midea_info_vect;
    lv_obj_t *video_background;
    lv_obj_t *video_canvas;
    file_iterator_instance_t *_file_iterator;
    avi_player_handle_t avi_handle;
    SemaphoreHandle_t semph_event;
    SemaphoreHandle_t video_task_event;


    jpeg_decoder_handle_t avi_jpgd_handle;
    uint8_t *jpeg_data_buffer[2];
    size_t jpeg_buf_size[2];

    QueueHandle_t mjpeg_data_queue;
    jpeg_frame_data_t frame_data_ = {0,0};
};
