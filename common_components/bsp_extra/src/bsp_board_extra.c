/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_sleep.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"
#include "lvgl.h"

static const char *TAG = "bsp_extra_board";

static esp_codec_dev_handle_t play_dev_handle;
static esp_codec_dev_handle_t record_dev_handle;

static bool _is_audio_init = false;
static bool _is_player_init = false;
static int _vloume_intensity = CODEC_DEFAULT_VOLUME;

static audio_player_cb_t audio_idle_callback = NULL;
static void *audio_idle_cb_user_data = NULL;
static char audio_file_path[512];

#define DISPLAY_IDLE_TASK_STACK_SIZE            (4096)
#define DISPLAY_IDLE_TASK_PRIORITY              (1)
#define DISPLAY_IDLE_TASK_PERIOD_MS             (250)
#define DISPLAY_ADAPTIVE_TIMEOUT_MS             (15 * 1000)
#define DISPLAY_ADAPTIVE_TRIGGER_BRIGHTNESS     (80)
#define DISPLAY_ADAPTIVE_DIMMED_BRIGHTNESS      (50)

typedef struct {
    bool initialized;
    bool adaptive_brightness_enabled;
    bool screensaver_enabled;
    uint32_t screen_off_timeout_sec;
    uint32_t sleep_timeout_sec;
    int base_brightness_percent;
    int applied_brightness_percent;
} display_idle_state_t;

static display_idle_state_t s_display_idle_state = {
    .initialized = false,
    .adaptive_brightness_enabled = false,
    .screensaver_enabled = false,
    .screen_off_timeout_sec = 0,
    .sleep_timeout_sec = 0,
    .base_brightness_percent = 100,
    .applied_brightness_percent = -1,
};
static bool s_deep_sleep_warning_logged = false;

/**************************************************************************************************
 *
 * Extra Board Function
 *
 **************************************************************************************************/

static esp_err_t audio_mute_function(AUDIO_PLAYER_MUTE_SETTING setting)
{
    // Volume saved when muting and restored when unmuting. Restoring volume is necessary
    // as es8311_set_voice_mute(true) results in voice volume (REG32) being set to zero.

    bsp_extra_codec_mute_set(setting == AUDIO_PLAYER_MUTE ? true : false);

    // restore the voice volume upon unmuting
    if (setting == AUDIO_PLAYER_UNMUTE) {
        ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(play_dev_handle, _vloume_intensity), TAG, "Set Codec volume failed");
    }

    return ESP_OK;
}

static void audio_callback(audio_player_cb_ctx_t *ctx)
{
    if (audio_idle_callback) {
        ctx->user_ctx = audio_idle_cb_user_data;
        audio_idle_callback(ctx);
    }
}

static void audio_file_path_set(const char *path)
{
    if (path == NULL) {
        audio_file_path[0] = '\0';
        return;
    }

    snprintf(audio_file_path, sizeof(audio_file_path), "%s", path);
}

static int clamp_display_brightness_percent(int brightness_percent)
{
    if (brightness_percent > 100) {
        return 100;
    }
    if (brightness_percent < 0) {
        return 0;
    }
    return brightness_percent;
}

static int display_idle_get_target_brightness(uint32_t inactive_time_ms)
{
    int target_brightness = s_display_idle_state.base_brightness_percent;

    if ((s_display_idle_state.screen_off_timeout_sec > 0) &&
        (inactive_time_ms >= (s_display_idle_state.screen_off_timeout_sec * 1000U))) {
        return 0;
    }

    if (s_display_idle_state.adaptive_brightness_enabled &&
        (s_display_idle_state.base_brightness_percent > DISPLAY_ADAPTIVE_TRIGGER_BRIGHTNESS) &&
        (inactive_time_ms >= DISPLAY_ADAPTIVE_TIMEOUT_MS)) {
        target_brightness = DISPLAY_ADAPTIVE_DIMMED_BRIGHTNESS;
    }

    return target_brightness;
}

static void display_idle_task(void *arg)
{
    (void)arg;

    for (;;) {
        lv_disp_t *display = NULL;
        uint32_t inactive_time_ms = 0;
        bool should_enter_deep_sleep = false;

        if (bsp_display_lock(DISPLAY_IDLE_TASK_PERIOD_MS)) {
            display = lv_disp_get_default();
            if (display != NULL) {
                inactive_time_ms = lv_disp_get_inactive_time(display);
                const int target_brightness = display_idle_get_target_brightness(inactive_time_ms);
                if (target_brightness != s_display_idle_state.applied_brightness_percent) {
                    if (bsp_display_brightness_set(target_brightness) == ESP_OK) {
                        s_display_idle_state.applied_brightness_percent = target_brightness;
                    } else {
                        ESP_LOGW(TAG, "Failed to apply idle brightness target: %d", target_brightness);
                    }
                }

                should_enter_deep_sleep = (s_display_idle_state.sleep_timeout_sec > 0) &&
                                          (inactive_time_ms >= (s_display_idle_state.sleep_timeout_sec * 1000U));
            }
            bsp_display_unlock();
        }

        if (should_enter_deep_sleep) {
            if (!s_deep_sleep_warning_logged) {
                ESP_LOGW(TAG, "Entering deep sleep without a dedicated wake GPIO; wake requires reset or power cycle");
                s_deep_sleep_warning_logged = true;
            }
            ESP_LOGI(TAG, "Entering deep sleep after %lu seconds of inactivity",
                     (unsigned long)s_display_idle_state.sleep_timeout_sec);
            bsp_display_backlight_off();
            esp_deep_sleep_start();
        }

        vTaskDelay(pdMS_TO_TICKS(DISPLAY_IDLE_TASK_PERIOD_MS));
    }
}

esp_err_t bsp_extra_i2s_read(void *audio_buffer, size_t len, size_t *bytes_read, uint32_t timeout_ms)
{
    esp_err_t ret = esp_codec_dev_read(record_dev_handle, audio_buffer, len);
    if (ret == ESP_OK) {
        if (bytes_read) {
            *bytes_read = len;
        }
    } else {
        if (bytes_read) {
            *bytes_read = 0;
        }
    }
    return ret;
}

esp_err_t bsp_extra_i2s_write(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    esp_err_t ret = esp_codec_dev_write(play_dev_handle, audio_buffer, len);
    if (ret == ESP_OK) {
        if (bytes_written) {
            *bytes_written = len;
        }
    } else {
        if (bytes_written) {
            *bytes_written = 0;
        }
    }
    return ret;
}

esp_err_t bsp_extra_codec_set_fs(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG,"rate = %" PRId32 "bits_cfg = %" PRId32, rate,bits_cfg);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = ch,
        .bits_per_sample = bits_cfg,
    };

    if (play_dev_handle) {
        ret = esp_codec_dev_close(play_dev_handle);
    }
    if (record_dev_handle) {
        ret |= esp_codec_dev_close(record_dev_handle);
        ret |= esp_codec_dev_set_in_gain(record_dev_handle, CODEC_DEFAULT_ADC_VOLUME);
    }

    if (play_dev_handle) {
        ret |= esp_codec_dev_open(play_dev_handle, &fs);
    }
    if (record_dev_handle) {
        ret |= esp_codec_dev_open(record_dev_handle, &fs);
    }

    ESP_LOGI(TAG,"ret = 0x%x , %s",ret,strerror(ret));
    return ret;
}

esp_err_t bsp_extra_codec_set_fs_play(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG,"rate = %" PRId32 "bits_cfg = %" PRId32, rate,bits_cfg);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = CODEC_DEFAULT_CHANNEL,
        .bits_per_sample = bits_cfg,
    };

    if (play_dev_handle) {
        ret = esp_codec_dev_close(play_dev_handle);
    }

    if (play_dev_handle) {
        ret |= esp_codec_dev_open(play_dev_handle, &fs);
    }

    ESP_LOGI(TAG,"ret = 0x%x , %s",ret,strerror(ret));
    return ret;
}

esp_err_t bsp_extra_codec_volume_set(int volume, int *volume_set)
{
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(play_dev_handle, volume), TAG, "Set Codec volume failed");
    _vloume_intensity = volume;

    ESP_LOGI(TAG, "Setting volume: %d", volume);

    return ESP_OK;
}

int bsp_extra_codec_volume_get(void)
{
    return _vloume_intensity;
}

esp_err_t bsp_extra_codec_mute_set(bool enable)
{
    esp_err_t ret = ESP_OK;
    ret = esp_codec_dev_set_out_mute(play_dev_handle, enable);
    return ret;
}

esp_err_t bsp_extra_codec_dev_stop(void)
{
    esp_err_t ret = ESP_OK;

    if (play_dev_handle) {
        ret = esp_codec_dev_close(play_dev_handle);
    }

    if (record_dev_handle) {
        ret = esp_codec_dev_close(record_dev_handle);
    }
    return ret;
}

esp_err_t bsp_extra_codec_dev_resume(void)
{
    return bsp_extra_codec_set_fs(CODEC_DEFAULT_SAMPLE_RATE, CODEC_DEFAULT_BIT_WIDTH, CODEC_DEFAULT_CHANNEL);
}

esp_err_t bsp_extra_codec_in_gain_set(int gain)
{
    if (!record_dev_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_set_in_gain(record_dev_handle, gain);
}

esp_err_t bsp_extra_codec_init()
{
    if (_is_audio_init) {
        return ESP_OK;
    }

    play_dev_handle = bsp_audio_codec_speaker_init();
    assert((play_dev_handle) && "play_dev_handle not initialized");

    record_dev_handle = bsp_audio_codec_microphone_init();
    assert((record_dev_handle) && "record_dev_handle not initialized");

    bsp_extra_codec_set_fs(CODEC_DEFAULT_SAMPLE_RATE, CODEC_DEFAULT_BIT_WIDTH, CODEC_DEFAULT_CHANNEL);

    _is_audio_init = true;

    return ESP_OK;
}

esp_err_t bsp_extra_player_init(void)
{
    if (_is_player_init) {
        return ESP_OK;
    }

    audio_player_config_t config = { .mute_fn = audio_mute_function,
                                     .write_fn = bsp_extra_i2s_write,
                                     .clk_set_fn = bsp_extra_codec_set_fs,
                                     .priority = 5
                                   };
    ESP_RETURN_ON_ERROR(audio_player_new(config), TAG, "audio_player_init failed");
    audio_player_callback_register(audio_callback, NULL);

    _is_player_init = true;

    return ESP_OK;
}

esp_err_t bsp_extra_player_del(void)
{
    _is_player_init = false;

    ESP_RETURN_ON_ERROR(audio_player_delete(), TAG, "audio_player_delete failed");

    return ESP_OK;
}

esp_err_t bsp_extra_file_instance_init(const char *path, file_iterator_instance_t **ret_instance)
{
    ESP_RETURN_ON_FALSE(path, ESP_FAIL, TAG, "path is NULL");
    ESP_RETURN_ON_FALSE(ret_instance, ESP_FAIL, TAG, "ret_instance is NULL");

    file_iterator_instance_t *file_iterator = file_iterator_new(path);
    ESP_RETURN_ON_FALSE(file_iterator, ESP_FAIL, TAG, "file_iterator_new failed, %s", path);

    *ret_instance = file_iterator;

    return ESP_OK;
}

esp_err_t bsp_extra_player_play_index(file_iterator_instance_t *instance, int index)
{
    ESP_RETURN_ON_FALSE(instance, ESP_FAIL, TAG, "instance is NULL");

    ESP_LOGI(TAG, "play_index(%d)", index);
    char filename[128];
    int retval = file_iterator_get_full_path_from_index(instance, index, filename, sizeof(filename));
    ESP_RETURN_ON_FALSE(retval != 0, ESP_FAIL, TAG, "file_iterator_get_full_path_from_index failed");

    ESP_LOGI(TAG, "opening file '%s'", filename);
    FILE *fp = fopen(filename, "rb");
    ESP_RETURN_ON_FALSE(fp, ESP_FAIL, TAG, "unable to open file");

    ESP_LOGI(TAG, "Playing '%s'", filename);
    ESP_RETURN_ON_ERROR(audio_player_play(fp), TAG, "audio_player_play failed");

    audio_file_path_set(filename);

    return ESP_OK;
}

esp_err_t bsp_extra_player_play_file(const char *file_path)
{
    ESP_LOGI(TAG, "opening file '%s'", file_path);
    FILE *fp = fopen(file_path, "rb");
    ESP_RETURN_ON_FALSE(fp, ESP_FAIL, TAG, "unable to open file");

    ESP_LOGI(TAG, "Playing '%s'", file_path);
    ESP_RETURN_ON_ERROR(audio_player_play(fp), TAG, "audio_player_play failed");

    audio_file_path_set(file_path);

    return ESP_OK;
}

void bsp_extra_player_register_callback(audio_player_cb_t cb, void *user_data)
{
    audio_idle_callback = cb;
    audio_idle_cb_user_data = user_data;
}

bool bsp_extra_player_is_playing_by_path(const char *file_path)
{
    return (strcmp(audio_file_path, file_path) == 0);
}

bool bsp_extra_player_is_playing_by_index(file_iterator_instance_t *instance, int index)
{
    return (index == file_iterator_get_index(instance));
}

esp_err_t bsp_extra_display_idle_init(void)
{
    if (s_display_idle_state.initialized) {
        return ESP_OK;
    }

    s_display_idle_state.base_brightness_percent = clamp_display_brightness_percent(s_display_idle_state.base_brightness_percent);

    BaseType_t ret = xTaskCreatePinnedToCore(display_idle_task, "display_idle", DISPLAY_IDLE_TASK_STACK_SIZE,
                                             NULL, DISPLAY_IDLE_TASK_PRIORITY, NULL, 1);
    if (ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_display_idle_state.initialized = true;
    return ESP_OK;
}

void bsp_extra_display_idle_set_base_brightness(int brightness_percent)
{
    s_display_idle_state.base_brightness_percent = clamp_display_brightness_percent(brightness_percent);

    if (!s_display_idle_state.adaptive_brightness_enabled ||
        (s_display_idle_state.base_brightness_percent <= DISPLAY_ADAPTIVE_TRIGGER_BRIGHTNESS)) {
        s_display_idle_state.applied_brightness_percent = -1;
    }
}

void bsp_extra_display_idle_configure(bool adaptive_brightness_enabled, bool screensaver_enabled,
                                      uint32_t screen_off_timeout_sec, uint32_t sleep_timeout_sec)
{
    s_display_idle_state.adaptive_brightness_enabled = adaptive_brightness_enabled;
    s_display_idle_state.screensaver_enabled = screensaver_enabled;
    s_display_idle_state.screen_off_timeout_sec = screen_off_timeout_sec;
    s_display_idle_state.sleep_timeout_sec = sleep_timeout_sec;
    s_display_idle_state.applied_brightness_percent = -1;
}