/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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
#include "esp_netif.h"
#include "esp_sleep.h"
#include "lwip/dns.h"

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
static int s_system_volume_intensity = CODEC_DEFAULT_VOLUME;
static int s_mic_gain_level = 10;
static bool s_codec_devices_open = false;

typedef struct {
    SemaphoreHandle_t mutex;
    uint8_t *mix_buffer;
    size_t mix_buffer_size;
    uint32_t sample_rate;
    uint32_t bits_per_sample;
    i2s_slot_mode_t channel_mode;
    bool notification_active;
    bool notification_direct_task_running;
    uint32_t notification_total_frames;
    uint32_t notification_remaining_frames;
    float notification_phase;
    float notification_phase_step;
    int applied_output_volume;
} audio_mix_state_t;

static audio_mix_state_t s_audio_mix_state = {
    .mutex = NULL,
    .mix_buffer = NULL,
    .mix_buffer_size = 0,
    .sample_rate = CODEC_DEFAULT_SAMPLE_RATE,
    .bits_per_sample = CODEC_DEFAULT_BIT_WIDTH,
    .channel_mode = CODEC_DEFAULT_CHANNEL,
    .notification_active = false,
    .notification_direct_task_running = false,
    .notification_total_frames = 0,
    .notification_remaining_frames = 0,
    .notification_phase = 0.0f,
    .notification_phase_step = 0.0f,
    .applied_output_volume = -1,
};

static audio_player_cb_t audio_idle_callback = NULL;
static void *audio_idle_cb_user_data = NULL;
static char audio_file_path[512];

#define AUDIO_NOTIFICATION_TASK_STACK_SIZE       (4096)
#define AUDIO_NOTIFICATION_TASK_PRIORITY         (4)
#define AUDIO_NOTIFICATION_CHUNK_FRAMES         (256)
#define AUDIO_NOTIFICATION_FREQUENCY_HZ         (1046)
#define AUDIO_NOTIFICATION_DURATION_MS          (180)
#define AUDIO_NOTIFICATION_AMPLITUDE            (0.35f)
#define AUDIO_WRITE_STALL_WARN_MS               (100)

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
    bool screen_off_suppressed;
    uint32_t screen_off_timeout_sec;
    uint32_t sleep_timeout_sec;
    int base_brightness_percent;
    int applied_brightness_percent;
} display_idle_state_t;

static display_idle_state_t s_display_idle_state = {
    .initialized = false,
    .adaptive_brightness_enabled = false,
    .screensaver_enabled = false,
    .screen_off_suppressed = false,
    .screen_off_timeout_sec = 0,
    .sleep_timeout_sec = 0,
    .base_brightness_percent = 100,
    .applied_brightness_percent = -1,
};
static bool s_deep_sleep_warning_logged = false;

static int audio_clamp_mic_gain_level(int level)
{
    if (level < 1) {
        return 1;
    }
    if (level > 10) {
        return 10;
    }
    return level;
}

static int audio_mic_gain_level_to_codec_gain(int level)
{
    return (audio_clamp_mic_gain_level(level) - 1) * 24 / 9;
}

static bool bsp_extra_dns_server_configured(const ip_addr_t *address)
{
    return (address != NULL) && !ip_addr_isany(address);
}

static BaseType_t create_background_task_prefer_psram(TaskFunction_t task,
                                                      const char *name,
                                                      const uint32_t stack_depth,
                                                      void *arg,
                                                      const UBaseType_t priority,
                                                      TaskHandle_t *task_handle,
                                                      const BaseType_t core_id)
{
    if (xTaskCreatePinnedToCoreWithCaps(task,
                                        name,
                                        stack_depth,
                                        arg,
                                        priority,
                                        task_handle,
                                        core_id,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        ESP_LOGI(TAG, "Started %s with a PSRAM-backed stack", name);
        return pdPASS;
    }

    ESP_LOGW(TAG,
             "Falling back to internal RAM stack for %s. Internal free=%u largest=%u PSRAM free=%u",
             name,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return xTaskCreatePinnedToCore(task, name, stack_depth, arg, priority, task_handle, core_id);
}

static int audio_clamp_volume(int volume)
{
    if (volume < 0) {
        return 0;
    }
    if (volume > 100) {
        return 100;
    }
    return volume;
}

bool bsp_extra_network_has_ip(void)
{
    esp_netif_t *station_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (station_netif == NULL) {
        return false;
    }

    esp_netif_ip_info_t ip_info = {0};
    if (esp_netif_get_ip_info(station_netif, &ip_info) != ESP_OK) {
        return false;
    }

    return ip_info.ip.addr != 0;
}

bool bsp_extra_network_has_dns(void)
{
    if (!bsp_extra_network_has_ip()) {
        return false;
    }

    return bsp_extra_dns_server_configured(dns_getserver(0)) ||
           bsp_extra_dns_server_configured(dns_getserver(1)) ||
           bsp_extra_dns_server_configured(dns_getserver(2));
}

static bool audio_mix_lock(TickType_t timeout)
{
    if (s_audio_mix_state.mutex == NULL) {
        s_audio_mix_state.mutex = xSemaphoreCreateMutex();
        if (s_audio_mix_state.mutex == NULL) {
            return false;
        }
    }

    return xSemaphoreTake(s_audio_mix_state.mutex, timeout) == pdTRUE;
}

static void audio_mix_unlock(void)
{
    if (s_audio_mix_state.mutex != NULL) {
        xSemaphoreGive(s_audio_mix_state.mutex);
    }
}

static int audio_get_output_target_volume_locked(void)
{
    int target = _vloume_intensity;
    if (s_audio_mix_state.notification_active) {
        target = (target > s_system_volume_intensity) ? target : s_system_volume_intensity;
    }
    return audio_clamp_volume(target);
}

static esp_err_t audio_apply_output_volume_locked(void)
{
    const int target = audio_get_output_target_volume_locked();
    if ((play_dev_handle == NULL) || (s_audio_mix_state.applied_output_volume == target)) {
        s_audio_mix_state.applied_output_volume = target;
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(play_dev_handle, target), TAG, "Set Codec volume failed");
    s_audio_mix_state.applied_output_volume = target;
    return ESP_OK;
}

static uint8_t *audio_ensure_mix_buffer(size_t len)
{
    if (s_audio_mix_state.mix_buffer_size >= len) {
        return s_audio_mix_state.mix_buffer;
    }

    uint8_t *buffer = (uint8_t *)heap_caps_realloc(s_audio_mix_state.mix_buffer, len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        buffer = (uint8_t *)heap_caps_realloc(s_audio_mix_state.mix_buffer, len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (buffer == NULL) {
        return NULL;
    }

    s_audio_mix_state.mix_buffer = buffer;
    s_audio_mix_state.mix_buffer_size = len;
    return s_audio_mix_state.mix_buffer;
}

static inline int16_t audio_clip_sample(int32_t sample)
{
    if (sample > INT16_MAX) {
        return INT16_MAX;
    }
    if (sample < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)sample;
}

static inline int16_t audio_generate_notification_sample(uint32_t total_frames,
                                                         uint32_t *remaining_frames,
                                                         float *phase,
                                                         float phase_step)
{
    if ((remaining_frames == NULL) || (phase == NULL) || (*remaining_frames == 0) || (total_frames == 0)) {
        return 0;
    }

    const float envelope = (float)(*remaining_frames) / (float)total_frames;
    const float sample = sinf(*phase) * envelope * AUDIO_NOTIFICATION_AMPLITUDE * (float)INT16_MAX;

    *phase += phase_step;
    if (*phase >= (2.0f * (float)M_PI)) {
        *phase -= (2.0f * (float)M_PI);
    }

    (*remaining_frames)--;
    return (int16_t)sample;
}

static esp_err_t audio_player_clock_set(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    if (audio_mix_lock(pdMS_TO_TICKS(1000))) {
        s_audio_mix_state.sample_rate = rate;
        s_audio_mix_state.bits_per_sample = bits_cfg;
        s_audio_mix_state.channel_mode = ch;
        audio_mix_unlock();
    }

    return bsp_extra_codec_set_fs(rate, bits_cfg, ch);
}

static esp_err_t audio_player_write_with_mix(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    bool notification_active = false;
    uint32_t notification_total_frames = 0;
    uint32_t notification_remaining_frames = 0;
    float notification_phase = 0.0f;
    float notification_phase_step = 0.0f;
    uint32_t bits_per_sample = CODEC_DEFAULT_BIT_WIDTH;
    i2s_slot_mode_t channel_mode = CODEC_DEFAULT_CHANNEL;
    int media_volume = _vloume_intensity;
    int system_volume = s_system_volume_intensity;
    int target_volume = media_volume;

    if (audio_mix_lock(pdMS_TO_TICKS(1000))) {
        notification_active = s_audio_mix_state.notification_active;
        notification_total_frames = s_audio_mix_state.notification_total_frames;
        notification_remaining_frames = s_audio_mix_state.notification_remaining_frames;
        notification_phase = s_audio_mix_state.notification_phase;
        notification_phase_step = s_audio_mix_state.notification_phase_step;
        bits_per_sample = s_audio_mix_state.bits_per_sample;
        channel_mode = s_audio_mix_state.channel_mode;
        media_volume = _vloume_intensity;
        system_volume = s_system_volume_intensity;
        target_volume = audio_get_output_target_volume_locked();
        audio_apply_output_volume_locked();
        audio_mix_unlock();
    }

    if (!notification_active || (audio_buffer == NULL) || (len == 0) || (bits_per_sample != 16)) {
        return bsp_extra_i2s_write(audio_buffer, len, bytes_written, timeout_ms);
    }

    uint8_t *mix_buffer = audio_ensure_mix_buffer(len);
    if (mix_buffer == NULL) {
        return bsp_extra_i2s_write(audio_buffer, len, bytes_written, timeout_ms);
    }

    const size_t channel_count = (channel_mode == I2S_SLOT_MODE_MONO) ? 1U : 2U;
    const size_t frame_count = len / (sizeof(int16_t) * channel_count);
    const int16_t *input = (const int16_t *)audio_buffer;
    int16_t *output = (int16_t *)mix_buffer;

    for (size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        const int16_t notification_sample = audio_generate_notification_sample(notification_total_frames,
                                                                               &notification_remaining_frames,
                                                                               &notification_phase,
                                                                               notification_phase_step);
        for (size_t channel_index = 0; channel_index < channel_count; ++channel_index) {
            const size_t sample_index = frame_index * channel_count + channel_index;
            int32_t mixed_sample = 0;
            if (target_volume > 0) {
                mixed_sample += ((int32_t)input[sample_index] * media_volume) / target_volume;
                mixed_sample += ((int32_t)notification_sample * system_volume) / target_volume;
            }
            output[sample_index] = audio_clip_sample(mixed_sample);
        }
    }

    const esp_err_t write_ret = bsp_extra_i2s_write(output, len, bytes_written, timeout_ms);

    if (audio_mix_lock(pdMS_TO_TICKS(1000))) {
        s_audio_mix_state.notification_remaining_frames = notification_remaining_frames;
        s_audio_mix_state.notification_phase = notification_phase;
        s_audio_mix_state.notification_active = notification_remaining_frames > 0;
        audio_apply_output_volume_locked();
        audio_mix_unlock();
    }

    return write_ret;
}

static void audio_notification_output_task(void *arg)
{
    (void)arg;

    int16_t *buffer = (int16_t *)heap_caps_malloc(AUDIO_NOTIFICATION_CHUNK_FRAMES * 2 * sizeof(int16_t),
                                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        if (audio_mix_lock(pdMS_TO_TICKS(1000))) {
            s_audio_mix_state.notification_direct_task_running = false;
            audio_mix_unlock();
        }
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
            break;
        }

        uint32_t total_frames = 0;
        uint32_t remaining_frames = 0;
        float phase = 0.0f;
        float phase_step = 0.0f;
        int system_volume = CODEC_DEFAULT_VOLUME;

        if (!audio_mix_lock(pdMS_TO_TICKS(1000))) {
            break;
        }

        if (!s_audio_mix_state.notification_active || (s_audio_mix_state.notification_remaining_frames == 0)) {
            s_audio_mix_state.notification_direct_task_running = false;
            audio_apply_output_volume_locked();
            audio_mix_unlock();
            break;
        }

        total_frames = s_audio_mix_state.notification_total_frames;
        remaining_frames = s_audio_mix_state.notification_remaining_frames;
        phase = s_audio_mix_state.notification_phase;
        phase_step = s_audio_mix_state.notification_phase_step;
        system_volume = s_system_volume_intensity;
        audio_apply_output_volume_locked();
        audio_mix_unlock();

        const size_t frames_to_write = (remaining_frames > AUDIO_NOTIFICATION_CHUNK_FRAMES)
                                           ? AUDIO_NOTIFICATION_CHUNK_FRAMES
                                           : remaining_frames;
        for (size_t frame_index = 0; frame_index < frames_to_write; ++frame_index) {
            const int16_t sample = audio_generate_notification_sample(total_frames, &remaining_frames, &phase, phase_step);
            buffer[(frame_index * 2) + 0] = sample;
            buffer[(frame_index * 2) + 1] = sample;
        }

        size_t bytes_written = 0;
        bsp_extra_i2s_write(buffer, frames_to_write * 2 * sizeof(int16_t), &bytes_written, 1000);

        if (!audio_mix_lock(pdMS_TO_TICKS(1000))) {
            break;
        }
        s_audio_mix_state.notification_remaining_frames = remaining_frames;
        s_audio_mix_state.notification_phase = phase;
        s_audio_mix_state.notification_active = remaining_frames > 0;
        s_audio_mix_state.notification_direct_task_running = s_audio_mix_state.notification_active;
        audio_apply_output_volume_locked();
        audio_mix_unlock();
    }

    free(buffer);
    if (audio_mix_lock(pdMS_TO_TICKS(1000))) {
        s_audio_mix_state.notification_direct_task_running = false;
        audio_apply_output_volume_locked();
        audio_mix_unlock();
    }
    vTaskDelete(NULL);
}

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
        if (audio_mix_lock(pdMS_TO_TICKS(1000))) {
            s_audio_mix_state.applied_output_volume = -1;
            audio_apply_output_volume_locked();
            audio_mix_unlock();
        }
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

    if (!s_display_idle_state.screen_off_suppressed &&
        (s_display_idle_state.screen_off_timeout_sec > 0) &&
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
    const uint32_t start_ms = esp_log_timestamp();
    esp_err_t ret = esp_codec_dev_write(play_dev_handle, audio_buffer, len);
    const uint32_t elapsed_ms = esp_log_timestamp() - start_ms;
    if (ret == ESP_OK) {
        if (bytes_written) {
            *bytes_written = len;
        }
        if (elapsed_ms >= AUDIO_WRITE_STALL_WARN_MS) {
            ESP_LOGW(TAG,
                     "Audio output write stalled for %u ms (len=%u timeout=%u notif=%d)",
                     (unsigned)elapsed_ms,
                     (unsigned)len,
                     (unsigned)timeout_ms,
                     s_audio_mix_state.notification_active ? 1 : 0);
        }
    } else {
        if (bytes_written) {
            *bytes_written = 0;
        }
        ESP_LOGW(TAG,
                 "Audio output write failed after %u ms: %s (len=%u timeout=%u notif=%d)",
                 (unsigned)elapsed_ms,
                 esp_err_to_name(ret),
                 (unsigned)len,
                 (unsigned)timeout_ms,
                 s_audio_mix_state.notification_active ? 1 : 0);
    }
    return ret;
}

esp_err_t bsp_extra_codec_set_fs(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    bool format_unchanged = false;
    if (audio_mix_lock(pdMS_TO_TICKS(1000))) {
        format_unchanged = (s_audio_mix_state.sample_rate == rate) &&
                           (s_audio_mix_state.bits_per_sample == bits_cfg) &&
                           (s_audio_mix_state.channel_mode == ch);
        audio_mix_unlock();
    }

    if (format_unchanged && s_codec_devices_open) {
        ESP_LOGI(TAG,
                 "Skipping codec reconfigure for unchanged format rate=%" PRId32 " bits=%" PRId32 " ch=%d",
                 rate,
                 bits_cfg,
                 (int)ch);
        return ESP_OK;
    }

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
    }

    if (play_dev_handle) {
        ret |= esp_codec_dev_open(play_dev_handle, &fs);
    }
    if (record_dev_handle) {
        ret |= esp_codec_dev_open(record_dev_handle, &fs);
    }

    if ((ret == ESP_OK) && (record_dev_handle != NULL)) {
        ret |= esp_codec_dev_set_in_gain(record_dev_handle, audio_mic_gain_level_to_codec_gain(s_mic_gain_level));
    }

    s_codec_devices_open = (ret == ESP_OK);

    if (audio_mix_lock(pdMS_TO_TICKS(1000))) {
        s_audio_mix_state.sample_rate = rate;
        s_audio_mix_state.bits_per_sample = bits_cfg;
        s_audio_mix_state.channel_mode = ch;
        audio_mix_unlock();
    }

    ESP_LOGI(TAG,"ret = 0x%x , %s",ret,strerror(ret));
    return ret;
}

esp_err_t bsp_extra_codec_set_fs_play(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    bool format_unchanged = false;
    if (audio_mix_lock(pdMS_TO_TICKS(1000))) {
        format_unchanged = (s_audio_mix_state.sample_rate == rate) &&
                           (s_audio_mix_state.bits_per_sample == bits_cfg) &&
                           (s_audio_mix_state.channel_mode == ch);
        audio_mix_unlock();
    }

    if (format_unchanged && s_codec_devices_open) {
        ESP_LOGI(TAG,
                 "Skipping play codec reconfigure for unchanged format rate=%" PRId32 " bits=%" PRId32 " ch=%d",
                 rate,
                 bits_cfg,
                 (int)ch);
        return ESP_OK;
    }

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

    if (play_dev_handle) {
        ret |= esp_codec_dev_open(play_dev_handle, &fs);
    }

    if (ret == ESP_OK && audio_mix_lock(pdMS_TO_TICKS(1000))) {
        s_audio_mix_state.sample_rate = rate;
        s_audio_mix_state.bits_per_sample = bits_cfg;
        s_audio_mix_state.channel_mode = ch;
        audio_mix_unlock();
    }

    s_codec_devices_open = (ret == ESP_OK);

    ESP_LOGI(TAG,"ret = 0x%x , %s",ret,strerror(ret));
    return ret;
}

esp_err_t bsp_extra_codec_volume_set(int volume, int *volume_set)
{
    ESP_RETURN_ON_ERROR(bsp_extra_audio_media_volume_set(volume), TAG, "Set media volume failed");

    if (volume_set != NULL) {
        *volume_set = _vloume_intensity;
    }

    ESP_LOGI(TAG, "Setting volume: %d", _vloume_intensity);

    return ESP_OK;
}

esp_err_t bsp_extra_audio_media_volume_set(int volume)
{
    _vloume_intensity = audio_clamp_volume(volume);

    if (audio_mix_lock(pdMS_TO_TICKS(1000))) {
        s_audio_mix_state.applied_output_volume = -1;
        audio_apply_output_volume_locked();
        audio_mix_unlock();
    }

    return ESP_OK;
}

int bsp_extra_audio_media_volume_get(void)
{
    return _vloume_intensity;
}

esp_err_t bsp_extra_audio_system_volume_set(int volume)
{
    s_system_volume_intensity = audio_clamp_volume(volume);

    if (audio_mix_lock(pdMS_TO_TICKS(1000))) {
        s_audio_mix_state.applied_output_volume = -1;
        audio_apply_output_volume_locked();
        audio_mix_unlock();
    }

    return ESP_OK;
}

int bsp_extra_audio_system_volume_get(void)
{
    return s_system_volume_intensity;
}

esp_err_t bsp_extra_audio_mic_gain_set_level(int level)
{
    s_mic_gain_level = audio_clamp_mic_gain_level(level);

    if (record_dev_handle == NULL) {
        return ESP_OK;
    }

    return bsp_extra_codec_in_gain_set(audio_mic_gain_level_to_codec_gain(s_mic_gain_level));
}

int bsp_extra_audio_mic_gain_get_level(void)
{
    return s_mic_gain_level;
}

esp_err_t bsp_extra_audio_play_system_notification(void)
{
    const bool media_playing = (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING);
    if (!media_playing) {
        ESP_RETURN_ON_ERROR(bsp_extra_codec_init(), TAG, "speaker codec init failed");
        ESP_RETURN_ON_ERROR(bsp_extra_codec_set_fs_play(CODEC_DEFAULT_SAMPLE_RATE, CODEC_DEFAULT_BIT_WIDTH, CODEC_DEFAULT_CHANNEL),
                            TAG,
                            "Failed to prepare codec output path for notification");
        ESP_RETURN_ON_ERROR(bsp_extra_codec_mute_set(false), TAG, "Failed to unmute codec output");
    }

    if (!audio_mix_lock(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    s_audio_mix_state.notification_total_frames = (CODEC_DEFAULT_SAMPLE_RATE * AUDIO_NOTIFICATION_DURATION_MS) / 1000U;
    s_audio_mix_state.notification_remaining_frames = s_audio_mix_state.notification_total_frames;
    s_audio_mix_state.notification_phase = 0.0f;
    s_audio_mix_state.notification_phase_step = (2.0f * (float)M_PI * (float)AUDIO_NOTIFICATION_FREQUENCY_HZ) /
                                               (float)CODEC_DEFAULT_SAMPLE_RATE;
    s_audio_mix_state.notification_active = s_audio_mix_state.notification_remaining_frames > 0;

    const bool start_direct_task = !media_playing &&
                                   !s_audio_mix_state.notification_direct_task_running;
    if (start_direct_task) {
        s_audio_mix_state.notification_direct_task_running = true;
    }

    s_audio_mix_state.applied_output_volume = -1;
    audio_apply_output_volume_locked();
    audio_mix_unlock();

    if (start_direct_task) {
        if (xTaskCreatePinnedToCore(audio_notification_output_task,
                                    "audio_notify",
                                    AUDIO_NOTIFICATION_TASK_STACK_SIZE,
                                    NULL,
                                    AUDIO_NOTIFICATION_TASK_PRIORITY,
                                    NULL,
                                    1) != pdPASS) {
            if (audio_mix_lock(pdMS_TO_TICKS(1000))) {
                s_audio_mix_state.notification_direct_task_running = false;
                s_audio_mix_state.notification_active = false;
                s_audio_mix_state.notification_remaining_frames = 0;
                audio_apply_output_volume_locked();
                audio_mix_unlock();
            }
            return ESP_ERR_NO_MEM;
        }
    }

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
    s_codec_devices_open = false;
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
    ESP_RETURN_ON_FALSE(play_dev_handle != NULL, ESP_ERR_NO_MEM, TAG, "speaker codec init failed");

    record_dev_handle = bsp_audio_codec_microphone_init();
    if (record_dev_handle == NULL) {
        esp_codec_dev_close(play_dev_handle);
        play_dev_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(bsp_extra_codec_set_fs(CODEC_DEFAULT_SAMPLE_RATE,
                                               CODEC_DEFAULT_BIT_WIDTH,
                                               CODEC_DEFAULT_CHANNEL),
                        TAG,
                        "Failed to set default codec sample format");

    ESP_RETURN_ON_ERROR(bsp_extra_audio_mic_gain_set_level(s_mic_gain_level), TAG, "Failed to set default mic gain");

    _is_audio_init = true;

    return ESP_OK;
}

esp_err_t bsp_extra_codec_deinit()
{
    esp_err_t ret = bsp_extra_codec_dev_stop();

    if (play_dev_handle != NULL) {
        esp_codec_dev_delete(play_dev_handle);
        play_dev_handle = NULL;
    }
    if (record_dev_handle != NULL) {
        esp_codec_dev_delete(record_dev_handle);
        record_dev_handle = NULL;
    }

    bsp_audio_deinit();
    _is_audio_init = false;
    s_codec_devices_open = false;

    return ret;
}

esp_err_t bsp_extra_player_init(void)
{
    if (_is_player_init) {
        return ESP_OK;
    }

                audio_player_config_t config = { .mute_fn = audio_mute_function,
                                                                                                                                                 .write_fn = audio_player_write_with_mix,
                                                                                                                                                 .clk_set_fn = audio_player_clock_set,
                                                                                                                                                 .priority = 5,
                                                                                                                                                 .coreID = 1
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
    ESP_RETURN_ON_ERROR(audio_player_play_file(fp, filename), TAG, "audio_player_play_file failed");

    audio_file_path_set(filename);

    return ESP_OK;
}

esp_err_t bsp_extra_player_play_file(const char *file_path)
{
    ESP_LOGI(TAG, "opening file '%s'", file_path);
    FILE *fp = fopen(file_path, "rb");
    ESP_RETURN_ON_FALSE(fp, ESP_FAIL, TAG, "unable to open file");

    ESP_LOGI(TAG, "Playing '%s'", file_path);
    ESP_RETURN_ON_ERROR(audio_player_play_file(fp, file_path), TAG, "audio_player_play_file failed");

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

    BaseType_t ret = create_background_task_prefer_psram(display_idle_task,
                                                         "display_idle",
                                                         DISPLAY_IDLE_TASK_STACK_SIZE,
                                                         NULL,
                                                         DISPLAY_IDLE_TASK_PRIORITY,
                                                         NULL,
                                                         1);
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

void bsp_extra_display_idle_set_screen_off_suppressed(bool suppressed)
{
    s_display_idle_state.screen_off_suppressed = suppressed;
    s_display_idle_state.applied_brightness_percent = -1;
}

void bsp_extra_display_idle_notify_activity(void)
{
    if (bsp_display_lock(DISPLAY_IDLE_TASK_PERIOD_MS)) {
        lv_disp_t *display = lv_disp_get_default();
        if (display != NULL) {
            lv_disp_trig_activity(display);
            s_deep_sleep_warning_logged = false;

            const int target_brightness = display_idle_get_target_brightness(0);
            if (target_brightness != s_display_idle_state.applied_brightness_percent) {
                if (bsp_display_brightness_set(target_brightness) == ESP_OK) {
                    s_display_idle_state.applied_brightness_percent = target_brightness;
                } else {
                    ESP_LOGW(TAG, "Failed to restore brightness after activity: %d", target_brightness);
                }
            }
        }
        bsp_display_unlock();
    }
}