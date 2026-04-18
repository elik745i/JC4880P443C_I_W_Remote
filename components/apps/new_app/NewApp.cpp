/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <cstdint>
#include <cstddef>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_check.h"

#include "NewApp.hpp"
#include "example_config.h"

#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "audio.h"

static const char *TAG = "Echo";

/* ================= 全局控制 ================= */

static bool echo_task_running = false;

/* 图标 */
extern const lv_img_dsc_t round;
const lv_img_dsc_t *echo_app_icon = &round;

/* ================= Echo Task ================= */

void i2s_echo1(void *args)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Echo task start, force audio reset");

    /* =====================================================
     * ★ 方案 A 核心：强制复位音频系统（和 Music 一样）
     * ===================================================== */

    /* 1. 先静音，防止爆音 */
    bsp_extra_codec_mute_set(true);

    /* 2. 重新初始化 codec（I2C + codec 芯片） */
    ret = bsp_extra_codec_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "codec init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    /* 3. 等待 codec 稳定（非常重要） */
    vTaskDelay(pdMS_TO_TICKS(200));

    /* 4. 重新初始化 I2S / player */
    ret = bsp_extra_player_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "player init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    /* 5. 设置 Echo 使用的“标准音频格式” */
    ret = bsp_extra_codec_set_fs(
        CODEC_DEFAULT_SAMPLE_RATE,
        CODEC_DEFAULT_BIT_WIDTH,
        CODEC_DEFAULT_CHANNEL
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set fs failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    /* 6. 设置音量 & 输入增益 */
    int volume_set = 0;
    bsp_extra_codec_volume_set(60, &volume_set);
    bsp_extra_codec_in_gain_set(CODEC_DEFAULT_ADC_VOLUME);

    /* 7. 解除静音 */
    bsp_extra_codec_mute_set(false);

    ESP_LOGI(TAG, "Echo audio reset done");

    /* =====================================================
     *              Echo 正常读写流程
     * ===================================================== */

    uint8_t *buffer = (uint8_t *)malloc(EXAMPLE_RECV_BUF_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "malloc failed");
        vTaskDelete(NULL);
        return;
    }

    echo_task_running = true;

    while (echo_task_running) {
        size_t bytes_read = 0;
        size_t bytes_written = 0;

        ret = bsp_extra_i2s_read(
            buffer,
            EXAMPLE_RECV_BUF_SIZE,
            &bytes_read,
            portMAX_DELAY
        );

        if (ret != ESP_OK || bytes_read == 0) {
            continue;
        }

        bsp_extra_i2s_write(
            buffer,
            bytes_read,
            &bytes_written,
            portMAX_DELAY
        );
    }

    free(buffer);

    bsp_extra_codec_mute_set(true);

    ESP_LOGI(TAG, "Echo task exit");
    vTaskDelete(NULL);
}

/* ================= NewApp ================= */

NewApp::NewApp(uint16_t hor_res, uint16_t ver_res)
    : ESP_Brookesia_PhoneApp("Echo", echo_app_icon, true),
      _hor_res(hor_res),
      _ver_res(ver_res),
      _screen(nullptr),
      _start_btn(nullptr),
      _stop_btn(nullptr),
      _status_label(nullptr),
      _echo_task_handle(nullptr),
      _is_recording(false)
{
}

NewApp::~NewApp()
{
    stop_echo();
    bsp_extra_codec_dev_stop();
}

bool NewApp::init()
{
    _screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_screen, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *title = lv_label_create(_screen);
    lv_label_set_text(title, "Echo");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    _status_label = lv_label_create(_screen);
    lv_label_set_text(_status_label, "Status: Ready");
    lv_obj_align(_status_label, LV_ALIGN_TOP_LEFT, 20, 60);

    _start_btn = lv_btn_create(_screen);
    lv_obj_set_size(_start_btn, 120, 50);
    lv_obj_align(_start_btn, LV_ALIGN_CENTER, -80, 0);
    lv_obj_add_event_cb(_start_btn, onScreenBtnClick, LV_EVENT_CLICKED, this);

    lv_obj_t *start_label = lv_label_create(_start_btn);
    lv_label_set_text(start_label, "Start");
    lv_obj_center(start_label);

    _stop_btn = lv_btn_create(_screen);
    lv_obj_set_size(_stop_btn, 120, 50);
    lv_obj_align(_stop_btn, LV_ALIGN_CENTER, 80, 0);
    lv_obj_add_event_cb(_stop_btn, onScreenBtnClick, LV_EVENT_CLICKED, this);

    lv_obj_t *stop_label = lv_label_create(_stop_btn);
    lv_label_set_text(stop_label, "Stop");
    lv_obj_center(stop_label);

    return true;
}

bool NewApp::run()
{
    if (_screen) {
        lv_scr_load(_screen);
        return true;
    }
    return false;
}

bool NewApp::pause()
{
    stop_echo();
    return notifyCoreClosed();;
}

bool NewApp::resume()
{
    return true;
}

bool NewApp::back()
{
    stop_echo();
    bsp_extra_codec_dev_stop();
    return notifyCoreClosed();
}

bool NewApp::close()
{
    stop_echo();
    bsp_extra_codec_dev_stop();
    return notifyCoreClosed();
}

/* ================= 核心修复点 ================= */

esp_err_t NewApp::start_echo()
{
    if (_echo_task_handle != nullptr) {
        stop_echo();
    }

    /* ★★★★★ 核心修复：强制抢回音频控制权 ★★★★★ */
    bsp_extra_codec_dev_stop();
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_ERROR_CHECK(bsp_extra_codec_init());
    ESP_ERROR_CHECK(bsp_extra_player_init());

    bsp_extra_codec_set_fs(
        CODEC_DEFAULT_SAMPLE_RATE,
        CODEC_DEFAULT_BIT_WIDTH,
        CODEC_DEFAULT_CHANNEL
    );

    bsp_extra_codec_volume_set(60, nullptr);
    bsp_extra_codec_in_gain_set(CODEC_DEFAULT_ADC_VOLUME);
    /* ★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★ */

    _is_recording = true;

    xTaskCreate(
        i2s_echo1,
        "i2s_echo1",
        4096,
        nullptr,
        5,
        &_echo_task_handle
    );

    if (_echo_task_handle == nullptr) {
        _is_recording = false;
        lv_label_set_text(_status_label, "Status: Start failed");
        return ESP_FAIL;
    }

    lv_label_set_text(_status_label, "Status: Echo running");
    return ESP_OK;
}

esp_err_t NewApp::stop_echo()
{
    if (!_is_recording) {
        return ESP_OK;
    }

    echo_task_running = false;
    vTaskDelay(pdMS_TO_TICKS(60));

    _echo_task_handle = nullptr;
    _is_recording = false;

    lv_label_set_text(_status_label, "Status: Stopped");
    return ESP_OK;
}

/* ================= UI Callback ================= */

void NewApp::onScreenBtnClick(lv_event_t *e)
{
    NewApp *app = (NewApp *)lv_event_get_user_data(e);

    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_t *target = lv_event_get_target(e);
        if (target == app->_start_btn) {
            app->start_echo();
        } else if (target == app->_stop_btn) {
            app->stop_echo();
        }
    }
}
