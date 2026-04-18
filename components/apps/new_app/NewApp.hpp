/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "lvgl.h"
#include "esp_brookesia.hpp"
#include "esp_codec_dev.h"

class NewApp: public ESP_Brookesia_PhoneApp {
public:
    NewApp(uint16_t hor_res, uint16_t ver_res);
    ~NewApp();

    bool run(void);
    bool pause(void);
    bool resume(void);
    bool back(void);
    bool close(void);

    bool init(void) override;
    bool isRecording() const;
    void clearTaskHandle();

private:
    static void onScreenBtnClick(lv_event_t *e);
    esp_err_t setup_audio(void);
    esp_err_t start_echo(void);
    esp_err_t stop_echo(void);

    uint16_t _hor_res;
    uint16_t _ver_res;
    lv_obj_t *_screen;
    lv_obj_t *_start_btn;
    lv_obj_t *_stop_btn;
    lv_obj_t *_status_label;
    
    esp_codec_dev_handle_t _codec_handle;
    TaskHandle_t _echo_task_handle;
    bool _is_recording;
};