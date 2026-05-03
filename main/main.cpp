#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <dirent.h>
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_app_desc.h"
#include "esp_core_dump.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "esp_pm.h"
#include "esp_system.h"
#include "esp_memory_utils.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"

#if CONFIG_JC4880_FEATURE_SECURITY
#include "device_security.hpp"
#endif

#include "sdmmc_cmd.h"

#include "esp_brookesia.hpp"
#include "app_examples/phone/squareline/src/phone_app_squareline.hpp"
#include "apps.h"
#include "storage_access.h"
#include "system_ui_service.h"
#include "web_server/WebServerService.hpp"
 
static const char *TAG = "main";
static constexpr TickType_t kSdcardMountRetryPeriod = pdMS_TO_TICKS(2000);
static constexpr TickType_t kSdcardStartupDelay = pdMS_TO_TICKS(1000);
static constexpr uint32_t kSdcardStartupTaskStack = 4096;
static constexpr uint32_t kSdcardStartupMountAttempts = 5;
static constexpr uint32_t kSerialCommandTaskStack = 6144;
static constexpr uint32_t kCrashReportUploadTaskStack = 6144;
static constexpr const char *kNvsStorageNamespace = "storage";
static constexpr const char *kNvsKeyOtaPendingVersion = "ota_ver";
static constexpr const char *kNvsKeyOtaPendingNotes = "ota_notes";
static constexpr const char *kNvsKeyOtaPendingShow = "ota_show";
static constexpr const char *kNvsKeyAudioVolume = "volume";
static constexpr const char *kNvsKeySystemAudioVolume = "sys_volume";
static constexpr const char *kNvsKeyTapSound = "tap_sound";
static constexpr const char *kNvsKeyHapticGpio = "haptic_gpio";
static constexpr const char *kNvsKeyHapticLevel = "haptic_lvl";
static constexpr const char *kNvsKeyDisplayOrientation = "disp_rot";
static constexpr const char *kNvsKeyDisplayOrientationPending = "disp_rot_pend";
static constexpr const char *kNvsKeyDisplayOrientationPrevious = "disp_rot_prev";
static constexpr const char *kNvsKeyDisplayOrientationState = "disp_rot_state";
static constexpr const char *kCrashReportLocalPath = BSP_SPIFFS_MOUNT_POINT "/last_crash_report.txt";
static constexpr const char *kCrashReportPendingPath = BSP_SPIFFS_MOUNT_POINT "/pending_crash_report.txt";
static constexpr const char *kCrashReportUploadUrl = "";
static constexpr TickType_t kCrashReportUploadDelay = pdMS_TO_TICKS(15000);
static constexpr TickType_t kOtaValidationDelay = pdMS_TO_TICKS(10000);
static constexpr TickType_t kDisplayRotationPreviewTimeout = pdMS_TO_TICKS(30000);
static constexpr uint32_t kOtaValidationTaskStack = 4096;
static constexpr uint32_t kDisplayRotationPreviewTaskStack = 3072;
static constexpr const char *kSegaEmulatorAppName = "SEGA Emulator";
static constexpr uint32_t kTapSoundDebounceMs = 80;
static constexpr gpio_num_t kDefaultHapticFeedbackGpio = GPIO_NUM_49;
static constexpr int32_t kDefaultHapticFeedbackLevel = 2;
static constexpr uint32_t kHapticPulseUsLow = 35000;
static constexpr uint32_t kHapticPulseUsMedium = 60000;
static constexpr uint32_t kHapticPulseUsHigh = 90000;
static constexpr uint32_t kHapticTestPulseUsLow = 500000;
static constexpr uint32_t kHapticTestPulseUsMedium = 800000;
static constexpr uint32_t kHapticTestPulseUsHigh = 1200000;
static constexpr ledc_mode_t kHapticLedcMode = LEDC_LOW_SPEED_MODE;
static constexpr ledc_timer_t kHapticLedcTimer = LEDC_TIMER_0;
static constexpr ledc_channel_t kHapticLedcChannel = LEDC_CHANNEL_0;
static constexpr ledc_timer_bit_t kHapticLedcResolution = LEDC_TIMER_10_BIT;
static constexpr uint32_t kHapticLedcFrequencyHz = 200;
static constexpr uint32_t kHapticDutyOff = 0;
static constexpr uint32_t kHapticDutyLow = 384;
static constexpr uint32_t kHapticDutyMedium = 700;
static constexpr uint32_t kHapticDutyHigh = 1023;

struct PendingReleaseNotesContext {
    std::string version;
    std::string notes;
};

struct PendingResetNoticeContext {
    std::string title;
    std::string details;
    bool has_report = false;
};

struct PendingInfoPopupContext {
    std::string title;
    std::string details;
};

struct PendingDisplayRotationPopupContext {
    int32_t pending_degrees = 0;
    int32_t previous_degrees = 0;
};

struct CrashReportUploadTaskContext {
    TickType_t delay = 0;
    bool notify_user = false;
};

enum DisplayRotationPreviewState : int32_t {
    DISPLAY_ROTATION_PREVIEW_NONE = 0,
    DISPLAY_ROTATION_PREVIEW_ARMED = 1,
    DISPLAY_ROTATION_PREVIEW_BOOTED = 2,
};

struct DisplayRotationBootState {
    int32_t startup_degrees = 0;
    int32_t confirmed_degrees = 0;
    int32_t pending_degrees = 0;
    int32_t previous_degrees = 0;
    bool preview_active = false;
    bool reverted_unconfirmed_preview = false;
};

static SemaphoreHandle_t s_sdcardMutex = nullptr;
static bool s_sdcardMounted = false;
static TickType_t s_nextSdcardMountAttempt = 0;

#if CONFIG_JC4880_APP_INTERNET_RADIO
static InternetRadio *s_internetRadioApp = nullptr;
#endif

static bool s_crashReportUploadInFlight = false;
static bool s_tapSoundEnabled = true;
static bool s_hapticFeedbackEnabled = true;
static gpio_num_t s_hapticFeedbackGpio = kDefaultHapticFeedbackGpio;
static int32_t s_hapticFeedbackLevel = kDefaultHapticFeedbackLevel;
static uint32_t s_lastTapSoundTick = 0;
static ESP_Brookesia_Phone *s_phone = nullptr;
static AppImageDisplay *s_imageDisplayApp = nullptr;
static std::vector<std::unique_ptr<lv_indev_drv_t>> s_tapSoundDriverCopies;
static esp_timer_handle_t s_hapticPulseTimer = nullptr;
static bool s_hapticPwmReady = false;
static lv_obj_t *s_displayRotationPreviewMsgbox = nullptr;

static bool sync_sdcard_mount_state(bool try_mount);

static BaseType_t create_background_task_prefer_psram(TaskFunction_t task,
                                                      const char *name,
                                                      uint32_t stack_depth,
                                                      void *arg,
                                                      UBaseType_t priority,
                                                      BaseType_t core_id);

extern "C" bool jc_ui_tap_sound_is_enabled(void)
{
    return s_tapSoundEnabled;
}

extern "C" void jc_ui_tap_sound_set_enabled(bool enabled)
{
    s_tapSoundEnabled = enabled;
}

extern "C" bool jc_ui_haptic_feedback_is_enabled(void)
{
    return s_hapticFeedbackEnabled;
}

extern "C" void jc_ui_haptic_feedback_set_enabled(bool enabled)
{
    s_hapticFeedbackEnabled = enabled;
    if (!enabled && (s_hapticFeedbackGpio != GPIO_NUM_NC)) {
        ledc_stop(kHapticLedcMode, kHapticLedcChannel, 0);
    }
}

static uint32_t get_haptic_duty_for_level(int32_t level)
{
    if (level <= 1) {
        return kHapticDutyLow;
    }
    if (level == 2) {
        return kHapticDutyMedium;
    }
    return kHapticDutyHigh;
}

static uint32_t get_haptic_pulse_us_for_level(int32_t level, bool test_pulse)
{
    if (test_pulse) {
        if (level <= 1) {
            return kHapticTestPulseUsLow;
        }
        if (level == 2) {
            return kHapticTestPulseUsMedium;
        }
        return kHapticTestPulseUsHigh;
    }

    if (level <= 1) {
        return kHapticPulseUsLow;
    }
    if (level == 2) {
        return kHapticPulseUsMedium;
    }
    return kHapticPulseUsHigh;
}

static void stop_haptic_output(void)
{
    if (s_hapticPwmReady) {
        ledc_stop(kHapticLedcMode, kHapticLedcChannel, 0);
    } else if (s_hapticFeedbackGpio != GPIO_NUM_NC) {
        gpio_set_level(s_hapticFeedbackGpio, 0);
    }
}

extern "C" void jc_ui_haptic_feedback_set_gpio(int gpio)
{
    gpio_num_t next_gpio = (gpio < 0) ? GPIO_NUM_NC : static_cast<gpio_num_t>(gpio);

    if (s_hapticFeedbackGpio != GPIO_NUM_NC) {
        stop_haptic_output();
        gpio_reset_pin(s_hapticFeedbackGpio);
    }

    s_hapticPwmReady = false;

    s_hapticFeedbackGpio = next_gpio;
    if (s_hapticFeedbackGpio == GPIO_NUM_NC) {
        return;
    }

    ledc_timer_config_t timer_config = {};
    timer_config.speed_mode = kHapticLedcMode;
    timer_config.timer_num = kHapticLedcTimer;
    timer_config.duty_resolution = kHapticLedcResolution;
    timer_config.freq_hz = kHapticLedcFrequencyHz;
    timer_config.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer_config) != ESP_OK) {
        s_hapticFeedbackGpio = GPIO_NUM_NC;
        return;
    }

    ledc_channel_config_t channel_config = {};
    channel_config.gpio_num = s_hapticFeedbackGpio;
    channel_config.speed_mode = kHapticLedcMode;
    channel_config.channel = kHapticLedcChannel;
    channel_config.intr_type = LEDC_INTR_DISABLE;
    channel_config.timer_sel = kHapticLedcTimer;
    channel_config.duty = kHapticDutyOff;
    channel_config.hpoint = 0;
    if (ledc_channel_config(&channel_config) != ESP_OK) {
        s_hapticFeedbackGpio = GPIO_NUM_NC;
        return;
    }

    s_hapticPwmReady = true;
    stop_haptic_output();
}

extern "C" void jc_ui_haptic_feedback_set_level(int level)
{
    s_hapticFeedbackLevel = std::max<int32_t>(0, std::min<int32_t>(3, level));
    if (s_hapticFeedbackLevel == 0 && (s_hapticFeedbackGpio != GPIO_NUM_NC)) {
        stop_haptic_output();
    }
}

static void on_haptic_pulse_timer(void *arg)
{
    (void)arg;
    stop_haptic_output();
}

static bool init_ui_haptic_feedback_output(void)
{
    static bool initialized = false;
    if (initialized) {
        return true;
    }

    int32_t configured_gpio = static_cast<int32_t>(kDefaultHapticFeedbackGpio);
    int32_t configured_level = kDefaultHapticFeedbackLevel;
    nvs_handle_t handle;
    if (nvs_open(kNvsStorageNamespace, NVS_READONLY, &handle) == ESP_OK) {
        (void)nvs_get_i32(handle, kNvsKeyHapticGpio, &configured_gpio);
        (void)nvs_get_i32(handle, kNvsKeyHapticLevel, &configured_level);
        nvs_close(handle);
    }

    jc_ui_haptic_feedback_set_gpio(configured_gpio);
    jc_ui_haptic_feedback_set_level(configured_level);

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = on_haptic_pulse_timer;
    timer_args.name = "ui_haptic";
    if (esp_timer_create(&timer_args, &s_hapticPulseTimer) != ESP_OK) {
        return false;
    }

    initialized = true;
    return true;
}

static void trigger_ui_haptic_feedback(bool test_pulse = false)
{
    if (!s_hapticFeedbackEnabled ||
        (s_hapticFeedbackLevel == 0) ||
        (s_hapticPulseTimer == nullptr) ||
        (s_hapticFeedbackGpio == GPIO_NUM_NC) ||
        !s_hapticPwmReady) {
        return;
    }

    const uint32_t pulse_us = get_haptic_pulse_us_for_level(s_hapticFeedbackLevel, test_pulse);
    const uint32_t duty = get_haptic_duty_for_level(s_hapticFeedbackLevel);

    esp_timer_stop(s_hapticPulseTimer);
    ledc_set_duty(kHapticLedcMode, kHapticLedcChannel, duty);
    ledc_update_duty(kHapticLedcMode, kHapticLedcChannel);
    if (esp_timer_start_once(s_hapticPulseTimer, pulse_us) != ESP_OK) {
        stop_haptic_output();
    }
}

extern "C" void jc_ui_haptic_feedback_test(void)
{
    trigger_ui_haptic_feedback(true);
}

static void initialize_power_management(void)
{
#if CONFIG_PM_ENABLE
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_XTAL_FREQ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true,
#else
        .light_sleep_enable = false,
#endif
    };

    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
    ESP_LOGI(TAG,
             "Power management enabled: max=%d MHz min=%d MHz light_sleep=%s",
             pm_config.max_freq_mhz,
             pm_config.min_freq_mhz,
             pm_config.light_sleep_enable ? "on" : "off");
#endif
}

static void on_lvgl_input_feedback(lv_indev_drv_t *indev_drv, uint8_t event_code)
{
    if ((indev_drv == nullptr) || (indev_drv->type != LV_INDEV_TYPE_POINTER) || !s_tapSoundEnabled ||
        (event_code != LV_EVENT_CLICKED)) {
        return;
    }

    if (s_phone != nullptr) {
        ESP_Brookesia_CoreApp *active_app = s_phone->getManager().getActiveApp();
        if ((active_app != nullptr) && (std::strcmp(active_app->getName(), kSegaEmulatorAppName) == 0)) {
            return;
        }
    }

    if ((s_lastTapSoundTick != 0) && (lv_tick_elaps(s_lastTapSoundTick) < kTapSoundDebounceMs)) {
        return;
    }

    s_lastTapSoundTick = lv_tick_get();
    trigger_ui_haptic_feedback();
    bsp_extra_audio_play_system_notification();
}

static bool install_ui_tap_sound_feedback(ESP_Brookesia_Phone &phone)
{
    bool installed = false;

    s_phone = &phone;
    s_tapSoundDriverCopies.clear();
    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev != nullptr; indev = lv_indev_get_next(indev)) {
        if ((indev->driver == nullptr) || (indev->driver->type != LV_INDEV_TYPE_POINTER)) {
            continue;
        }

        std::unique_ptr<lv_indev_drv_t> driver = std::make_unique<lv_indev_drv_t>(*indev->driver);
        driver->feedback_cb = on_lvgl_input_feedback;
        lv_indev_drv_update(indev, driver.get());
        s_tapSoundDriverCopies.push_back(std::move(driver));
        installed = true;
    }

    return installed;
}

static std::string json_escape_copy(const std::string &value)
{
    std::string escaped;
    escaped.reserve(value.size() + 32);

    for (const char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }

    return escaped;
}

static bool read_text_file(const char *path, std::string &contents)
{
    FILE *file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }

    std::string buffer;
    char chunk[256] = {};
    while (true) {
        const size_t read = std::fread(chunk, 1, sizeof(chunk), file);
        if (read > 0) {
            buffer.append(chunk, read);
        }
        if (read < sizeof(chunk)) {
            break;
        }
    }

    std::fclose(file);
    contents = std::move(buffer);
    return true;
}

static bool write_text_file(const char *path, const std::string &contents)
{
    FILE *file = std::fopen(path, "wb");
    if (file == nullptr) {
        return false;
    }

    const bool ok = std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    std::fclose(file);
    return ok;
}

static void restore_audio_preferences_from_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(kNvsStorageNamespace, NVS_READONLY, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Unable to open NVS for audio preference restore");
        return;
    }

    int32_t media_volume = bsp_extra_audio_media_volume_get();
    int32_t system_volume = bsp_extra_audio_system_volume_get();

    if (nvs_get_i32(handle, kNvsKeyAudioVolume, &media_volume) == ESP_OK) {
        bsp_extra_audio_media_volume_set(media_volume);
    }

    if (nvs_get_i32(handle, kNvsKeySystemAudioVolume, &system_volume) == ESP_OK) {
        bsp_extra_audio_system_volume_set(system_volume);
    }

    nvs_close(handle);
}

static bool is_valid_display_orientation_degrees(int32_t orientation_degrees)
{
    switch (orientation_degrees) {
    case 0:
    case 90:
    case 180:
    case 270:
        return true;
    default:
        return false;
    }
}

static int32_t sanitize_display_orientation_degrees(int32_t orientation_degrees)
{
    return is_valid_display_orientation_degrees(orientation_degrees) ? orientation_degrees : 0;
}

static bool load_nvs_i32(const char *key, int32_t &value)
{
    nvs_handle_t handle;
    if (nvs_open(kNvsStorageNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    const esp_err_t err = nvs_get_i32(handle, key, &value);
    nvs_close(handle);
    return err == ESP_OK;
}

static lv_disp_rotation_t display_orientation_degrees_to_lv_rotation(int32_t orientation_degrees)
{
    switch (sanitize_display_orientation_degrees(orientation_degrees)) {
    case 90:
        return static_cast<lv_disp_rotation_t>(LV_DISP_ROT_90);
    case 180:
        return static_cast<lv_disp_rotation_t>(LV_DISP_ROT_180);
    case 270:
        return static_cast<lv_disp_rotation_t>(LV_DISP_ROT_270);
    case 0:
    default:
        return static_cast<lv_disp_rotation_t>(LV_DISP_ROT_NONE);
    }
}

static bool is_display_rotation_preview_state(int32_t preview_state)
{
    return (preview_state == DISPLAY_ROTATION_PREVIEW_ARMED) || (preview_state == DISPLAY_ROTATION_PREVIEW_BOOTED);
}

static bool clear_display_rotation_preview_state(nvs_handle_t handle, int32_t confirmed_degrees)
{
    const int32_t sanitized_degrees = sanitize_display_orientation_degrees(confirmed_degrees);
    esp_err_t err = nvs_set_i32(handle, kNvsKeyDisplayOrientation, sanitized_degrees);
    err = (err == ESP_OK) ? nvs_set_i32(handle, kNvsKeyDisplayOrientationPending, sanitized_degrees) : err;
    err = (err == ESP_OK) ? nvs_set_i32(handle, kNvsKeyDisplayOrientationPrevious, sanitized_degrees) : err;
    err = (err == ESP_OK) ? nvs_set_i32(handle, kNvsKeyDisplayOrientationState, DISPLAY_ROTATION_PREVIEW_NONE) : err;
    err = (err == ESP_OK) ? nvs_commit(handle) : err;
    return err == ESP_OK;
}

static DisplayRotationBootState resolve_display_rotation_boot_state(void)
{
    DisplayRotationBootState result = {};

    nvs_handle_t handle;
    if (nvs_open(kNvsStorageNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return result;
    }

    int32_t confirmed_degrees = 0;
    int32_t pending_degrees = 0;
    int32_t previous_degrees = 0;
    int32_t preview_state = DISPLAY_ROTATION_PREVIEW_NONE;
    (void)nvs_get_i32(handle, kNvsKeyDisplayOrientation, &confirmed_degrees);
    (void)nvs_get_i32(handle, kNvsKeyDisplayOrientationPending, &pending_degrees);
    (void)nvs_get_i32(handle, kNvsKeyDisplayOrientationPrevious, &previous_degrees);
    (void)nvs_get_i32(handle, kNvsKeyDisplayOrientationState, &preview_state);

    confirmed_degrees = sanitize_display_orientation_degrees(confirmed_degrees);
    pending_degrees = sanitize_display_orientation_degrees(pending_degrees);
    previous_degrees = sanitize_display_orientation_degrees(previous_degrees);
    if (!is_display_rotation_preview_state(preview_state)) {
        preview_state = DISPLAY_ROTATION_PREVIEW_NONE;
    }

    result.confirmed_degrees = confirmed_degrees;
    result.pending_degrees = pending_degrees;
    result.previous_degrees = previous_degrees;
    result.startup_degrees = confirmed_degrees;

    if (preview_state == DISPLAY_ROTATION_PREVIEW_BOOTED) {
        if (clear_display_rotation_preview_state(handle, previous_degrees)) {
            result.confirmed_degrees = previous_degrees;
            result.startup_degrees = previous_degrees;
            result.reverted_unconfirmed_preview = true;
        }
    } else if (preview_state == DISPLAY_ROTATION_PREVIEW_ARMED) {
        if ((nvs_set_i32(handle, kNvsKeyDisplayOrientationState, DISPLAY_ROTATION_PREVIEW_BOOTED) == ESP_OK) &&
            (nvs_commit(handle) == ESP_OK)) {
            result.startup_degrees = pending_degrees;
            result.preview_active = true;
        }
    }

    nvs_close(handle);
    return result;
}

static int32_t load_display_orientation_from_nvs(void)
{
    int32_t orientation_degrees = 0;
    if (!load_nvs_i32(kNvsKeyDisplayOrientation, orientation_degrees)) {
        orientation_degrees = 0;
    }
    return sanitize_display_orientation_degrees(orientation_degrees);
}

static bool save_display_orientation_to_nvs(int32_t orientation_degrees)
{
    const int32_t sanitized_degrees = sanitize_display_orientation_degrees(orientation_degrees);

    nvs_handle_t handle;
    if (nvs_open(kNvsStorageNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    esp_err_t err = nvs_set_i32(handle, kNvsKeyDisplayOrientation, sanitized_degrees);
    err = (err == ESP_OK) ? nvs_set_i32(handle, kNvsKeyDisplayOrientationPending, sanitized_degrees) : err;
    err = (err == ESP_OK) ? nvs_set_i32(handle, kNvsKeyDisplayOrientationPrevious, sanitized_degrees) : err;
    err = (err == ESP_OK) ? nvs_set_i32(handle, kNvsKeyDisplayOrientationState, DISPLAY_ROTATION_PREVIEW_NONE) : err;
    err = (err == ESP_OK) ? nvs_commit(handle) : err;
    nvs_close(handle);
    return err == ESP_OK;
}

static bool apply_display_orientation_live(int32_t orientation_degrees, bool persist)
{
    lv_display_t *display = lv_disp_get_default();
    if (display == nullptr) {
        return false;
    }

    if (!bsp_display_lock(0)) {
        return false;
    }

    bsp_display_rotate(display, display_orientation_degrees_to_lv_rotation(orientation_degrees));
    bsp_display_unlock();

    return !persist || save_display_orientation_to_nvs(orientation_degrees);
}

static bool request_display_orientation_preview(int32_t orientation_degrees)
{
    const int32_t sanitized_degrees = sanitize_display_orientation_degrees(orientation_degrees);
    const int32_t current_degrees = load_display_orientation_from_nvs();

    nvs_handle_t handle;
    if (nvs_open(kNvsStorageNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    const esp_err_t set_err =
        (nvs_set_i32(handle, kNvsKeyDisplayOrientationPrevious, current_degrees) == ESP_OK) &&
        (nvs_set_i32(handle, kNvsKeyDisplayOrientationPending, sanitized_degrees) == ESP_OK) &&
        (nvs_set_i32(handle, kNvsKeyDisplayOrientationState, DISPLAY_ROTATION_PREVIEW_ARMED) == ESP_OK)
            ? ESP_OK
            : ESP_FAIL;
    const esp_err_t commit_err = (set_err == ESP_OK) ? nvs_commit(handle) : set_err;
    nvs_close(handle);
    return commit_err == ESP_OK;
}

static bool confirm_display_orientation_preview(void)
{
    nvs_handle_t handle;
    if (nvs_open(kNvsStorageNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    int32_t pending_degrees = 0;
    if (nvs_get_i32(handle, kNvsKeyDisplayOrientationPending, &pending_degrees) != ESP_OK) {
        pending_degrees = load_display_orientation_from_nvs();
    }
    const bool ok = clear_display_rotation_preview_state(handle, pending_degrees);
    nvs_close(handle);
    return ok;
}

static bool revert_display_orientation_preview(void)
{
    nvs_handle_t handle;
    if (nvs_open(kNvsStorageNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    int32_t previous_degrees = 0;
    if (nvs_get_i32(handle, kNvsKeyDisplayOrientationPrevious, &previous_degrees) != ESP_OK) {
        previous_degrees = load_display_orientation_from_nvs();
    }
    const bool ok = clear_display_rotation_preview_state(handle, previous_degrees);
    nvs_close(handle);
    return ok;
}

static bool get_display_rotation_preview_state(int32_t &preview_state, int32_t &pending_degrees, int32_t &previous_degrees)
{
    preview_state = DISPLAY_ROTATION_PREVIEW_NONE;
    pending_degrees = load_display_orientation_from_nvs();
    previous_degrees = pending_degrees;

    nvs_handle_t handle;
    if (nvs_open(kNvsStorageNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    (void)nvs_get_i32(handle, kNvsKeyDisplayOrientationState, &preview_state);
    (void)nvs_get_i32(handle, kNvsKeyDisplayOrientationPending, &pending_degrees);
    (void)nvs_get_i32(handle, kNvsKeyDisplayOrientationPrevious, &previous_degrees);
    nvs_close(handle);

    preview_state = is_display_rotation_preview_state(preview_state) ? preview_state : DISPLAY_ROTATION_PREVIEW_NONE;
    pending_degrees = sanitize_display_orientation_degrees(pending_degrees);
    previous_degrees = sanitize_display_orientation_degrees(previous_degrees);
    return preview_state != DISPLAY_ROTATION_PREVIEW_NONE;
}

static bool load_crash_report_for_upload(std::string &report)
{
    if (read_text_file(kCrashReportPendingPath, report) && !report.empty()) {
        return true;
    }

    return read_text_file(kCrashReportLocalPath, report) && !report.empty();
}

static bool has_saved_crash_report(void)
{
    std::string report;
    return load_crash_report_for_upload(report);
}

static bool is_crash_report_upload_configured(void)
{
    return (kCrashReportUploadUrl != nullptr) && (kCrashReportUploadUrl[0] != '\0');
}

static bool is_wifi_connected(void)
{
    wifi_ap_record_t ap_info = {};
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
}

static std::string format_device_id(void)
{
    uint8_t mac[6] = {};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
        return "unknown";
    }

    char buffer[24] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0],
                  mac[1],
                  mac[2],
                  mac[3],
                  mac[4],
                  mac[5]);
    return buffer;
}

static std::string format_wifi_context(void)
{
    wifi_ap_record_t ap_info = {};
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return "not-connected";
    }

    char ssid_buffer[sizeof(ap_info.ssid) + 1] = {};
    std::memcpy(ssid_buffer, ap_info.ssid, sizeof(ap_info.ssid));

    char buffer[96] = {};
    std::snprintf(buffer, sizeof(buffer), "%s (RSSI %d)", ssid_buffer, ap_info.rssi);
    return buffer;
}

static std::string build_crash_report_text(esp_reset_reason_t reset_reason)
{
    std::ostringstream report;
    const esp_app_desc_t *app_desc = esp_app_get_description();

    report << "Crash report\n";
    report << "Device: " << format_device_id() << "\n";
    report << "Project: " << ((app_desc != nullptr) ? app_desc->project_name : "unknown") << "\n";
    report << "Version: " << ((app_desc != nullptr) ? app_desc->version : "unknown") << "\n";
    report << "ELF SHA256: " << esp_app_get_elf_sha256_str() << "\n";
    report << "Reset reason: " << static_cast<int>(reset_reason) << "\n";
    report << "Wi-Fi: " << format_wifi_context() << "\n";

    char panic_reason[200] = {};
    if (esp_core_dump_get_panic_reason(panic_reason, sizeof(panic_reason)) == ESP_OK) {
        report << "Panic reason: " << panic_reason << "\n";
    }

    esp_core_dump_summary_t summary = {};
    if (esp_core_dump_get_summary(&summary) == ESP_OK) {
        report << "Exception task: " << summary.exc_task << "\n";
        report << "Exception PC: 0x" << std::hex << summary.exc_pc << std::dec << "\n";
        report << "Dump size: " << summary.exc_bt_info.dump_size << "\n";
        report << "MCAUSE: 0x" << std::hex << summary.ex_info.mcause << std::dec << "\n";
        report << "MTVAL: 0x" << std::hex << summary.ex_info.mtval << std::dec << "\n";
        report << "RA: 0x" << std::hex << summary.ex_info.ra << std::dec << "\n";
        report << "SP: 0x" << std::hex << summary.ex_info.sp << std::dec << "\n";
    }

    size_t image_addr = 0;
    size_t image_size = 0;
    if (esp_core_dump_image_get(&image_addr, &image_size) == ESP_OK) {
        report << "Core dump flash addr: 0x" << std::hex << image_addr << std::dec << "\n";
        report << "Core dump size: " << image_size << "\n";
    }

    return report.str();
}

static void capture_panic_report_if_present(void)
{
    size_t image_addr = 0;
    size_t image_size = 0;
    if (esp_core_dump_image_get(&image_addr, &image_size) != ESP_OK) {
        return;
    }

    const esp_reset_reason_t reset_reason = esp_reset_reason();
    const std::string report = build_crash_report_text(reset_reason);
    if (!report.empty()) {
        (void)write_text_file(kCrashReportLocalPath, report);
        (void)write_text_file(kCrashReportPendingPath, report);
    }

    if (esp_core_dump_image_erase() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to erase coredump partition after persisting report");
    }
}

static bool upload_crash_report(const std::string &report)
{
    if (!is_crash_report_upload_configured()) {
        return false;
    }

    const esp_app_desc_t *app_desc = esp_app_get_description();
    std::ostringstream body;
    body << "{";
    body << "\"device\":\"" << json_escape_copy(format_device_id()) << "\",";
    body << "\"project\":\"" << json_escape_copy((app_desc != nullptr) ? app_desc->project_name : "unknown") << "\",";
    body << "\"version\":\"" << json_escape_copy((app_desc != nullptr) ? app_desc->version : "unknown") << "\",";
    body << "\"report\":\"" << json_escape_copy(report) << "\"";
    body << "}";

    const std::string payload = body.str();
    esp_http_client_config_t config = {};
    config.url = kCrashReportUploadUrl;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 15000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload.c_str(), payload.size());
    const esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    return (err == ESP_OK) && (status >= 200) && (status < 300);
}

static void close_info_popup(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_current_target(event);
    if (target != nullptr) {
        lv_msgbox_close_async(target);
    }
}

static void show_info_popup(void *context)
{
    std::unique_ptr<PendingInfoPopupContext> info(static_cast<PendingInfoPopupContext *>(context));
    if ((info == nullptr) || info->details.empty()) {
        return;
    }

    static const char *buttons[] = {"OK", ""};
    lv_obj_t *msgbox = lv_msgbox_create(nullptr, info->title.c_str(), info->details.c_str(), buttons, false);
    if (msgbox == nullptr) {
        return;
    }

    lv_obj_set_width(msgbox, 420);
    lv_obj_center(msgbox);
    lv_obj_add_event_cb(msgbox, close_info_popup, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void schedule_info_popup(const char *title, const char *details)
{
    auto *context = new PendingInfoPopupContext{title != nullptr ? title : "Notice", details != nullptr ? details : ""};
    if ((context == nullptr) || context->details.empty()) {
        delete context;
        return;
    }

    bsp_display_lock(0);
    if (lv_async_call(show_info_popup, context) != LV_RES_OK) {
        bsp_display_unlock();
        delete context;
        return;
    }
    bsp_display_unlock();
}

static void on_display_rotation_preview_popup_event(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_current_target(event);
    if (target == nullptr) {
        return;
    }

    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        if (s_displayRotationPreviewMsgbox == target) {
            s_displayRotationPreviewMsgbox = nullptr;
        }
        return;
    }

    const char *button_text = lv_msgbox_get_active_btn_text(target);
    if (button_text == nullptr) {
        return;
    }

    if (std::strcmp(button_text, "OK") == 0) {
        if (confirm_display_orientation_preview()) {
            printf("[display] preview confirmed and saved\r\n");
        } else {
            printf("[display] failed to confirm preview\r\n");
        }
        lv_msgbox_close_async(target);
        return;
    }

    if (std::strcmp(button_text, "Revert") == 0) {
        if (revert_display_orientation_preview()) {
            printf("[display] preview reverted; restarting\r\n");
        } else {
            printf("[display] failed to revert preview; restarting anyway\r\n");
        }
        fflush(stdout);
        esp_restart();
    }
}

static void show_display_rotation_preview_popup(void *context)
{
    std::unique_ptr<PendingDisplayRotationPopupContext> preview(static_cast<PendingDisplayRotationPopupContext *>(context));
    if (preview == nullptr) {
        return;
    }

    static const char *buttons[] = {"OK", "Revert", ""};
    char body[224] = {};
    std::snprintf(body,
                  sizeof(body),
                  "Previewing %ld degrees. Tap OK within 30 seconds to keep it.\n\nIf you do nothing, the device will revert to %ld degrees and restart.",
                  static_cast<long>(preview->pending_degrees),
                  static_cast<long>(preview->previous_degrees));

    lv_obj_t *msgbox = lv_msgbox_create(lv_layer_top(), "Confirm Rotation", body, buttons, false);
    if (msgbox == nullptr) {
        return;
    }

    s_displayRotationPreviewMsgbox = msgbox;
    lv_obj_set_width(msgbox, 440);
    lv_obj_center(msgbox);
    lv_obj_add_event_cb(msgbox, on_display_rotation_preview_popup_event, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(msgbox, on_display_rotation_preview_popup_event, LV_EVENT_DELETE, nullptr);
}

static void display_rotation_preview_timeout_task(void *parameter)
{
    (void)parameter;
    vTaskDelay(kDisplayRotationPreviewTimeout);

    int32_t preview_state = DISPLAY_ROTATION_PREVIEW_NONE;
    int32_t pending_degrees = 0;
    int32_t previous_degrees = 0;
    if (get_display_rotation_preview_state(preview_state, pending_degrees, previous_degrees) &&
        (preview_state == DISPLAY_ROTATION_PREVIEW_BOOTED)) {
        ESP_LOGW(TAG,
                 "Display rotation preview timed out after 30 seconds; reverting from %ld to %ld",
                 static_cast<long>(pending_degrees),
                 static_cast<long>(previous_degrees));
        (void)revert_display_orientation_preview();
        esp_restart();
    }

    vTaskDelete(nullptr);
}

static void schedule_display_rotation_preview_popup(int32_t pending_degrees, int32_t previous_degrees)
{
    auto *context = new PendingDisplayRotationPopupContext{pending_degrees, previous_degrees};
    if (context == nullptr) {
        return;
    }

    bsp_display_lock(0);
    if (lv_async_call(show_display_rotation_preview_popup, context) != LV_RES_OK) {
        bsp_display_unlock();
        delete context;
        return;
    }
    bsp_display_unlock();
}

static void crash_report_upload_task(void *parameter)
{
    std::unique_ptr<CrashReportUploadTaskContext> context(static_cast<CrashReportUploadTaskContext *>(parameter));
    const TickType_t delay = (context != nullptr) ? context->delay : 0;
    const bool notify_user = (context != nullptr) ? context->notify_user : false;
    if (delay > 0) {
        vTaskDelay(delay);
    }

    std::string report;
    if (!load_crash_report_for_upload(report)) {
        if (notify_user) {
            schedule_info_popup("Report Unavailable", "No saved crash report was found on this device.");
        }
        s_crashReportUploadInFlight = false;
        vTaskDelete(nullptr);
        return;
    }

    if (!is_wifi_connected()) {
        if (notify_user) {
            schedule_info_popup("Not Connected", "Connect the device to Wi-Fi before sending a crash report.");
        }
        s_crashReportUploadInFlight = false;
        vTaskDelete(nullptr);
        return;
    }

    if (!is_crash_report_upload_configured()) {
        if (notify_user) {
            schedule_info_popup("Reporting Not Configured", "This firmware does not have a private developer report relay configured yet.");
        }
        s_crashReportUploadInFlight = false;
        vTaskDelete(nullptr);
        return;
    }

    if (upload_crash_report(report)) {
        std::remove(kCrashReportPendingPath);
        ESP_LOGI(TAG, "Crash report uploaded successfully");
        if (notify_user) {
            schedule_info_popup("Report Sent", "The crash report was sent to the developer relay.");
        }
    } else {
        ESP_LOGW(TAG, "Crash report upload failed; pending report kept at %s", kCrashReportPendingPath);
        if (notify_user) {
            schedule_info_popup("Send Failed", "The crash report could not be delivered. It is still saved locally on the device.");
        }
    }

    s_crashReportUploadInFlight = false;
    vTaskDelete(nullptr);
}

static void start_crash_report_upload(TickType_t delay, bool notify_user)
{
    if (s_crashReportUploadInFlight) {
        if (notify_user) {
            schedule_info_popup("Report In Progress", "A crash report is already being sent.");
        }
        return;
    }

    std::string report;
    if (!load_crash_report_for_upload(report)) {
        if (notify_user) {
            schedule_info_popup("Report Unavailable", "No saved crash report was found on this device.");
        }
        return;
    }

    auto *context = new CrashReportUploadTaskContext{delay, notify_user};
    if (context == nullptr) {
        if (notify_user) {
            schedule_info_popup("Report Failed", "The device could not start the crash report task.");
        }
        return;
    }

    s_crashReportUploadInFlight = true;
    if (create_background_task_prefer_psram(crash_report_upload_task,
                                            "crash_report_up",
                                            kCrashReportUploadTaskStack,
                                            context,
                                            1,
                                            0) != pdPASS) {
        s_crashReportUploadInFlight = false;
        delete context;
        ESP_LOGW(TAG, "Failed to start crash report upload task");
        if (notify_user) {
            schedule_info_popup("Report Failed", "The device could not start the crash report task.");
        }
    }
}

static void set_sdcard_probe_log_levels(esp_log_level_t sdmmc_periph_level,
                                        esp_log_level_t sdmmc_common_level,
                                        esp_log_level_t vfs_fat_sdmmc_level)
{
    esp_log_level_set("sdmmc_periph", sdmmc_periph_level);
    esp_log_level_set("sdmmc_common", sdmmc_common_level);
    esp_log_level_set("vfs_fat_sdmmc", vfs_fat_sdmmc_level);
}

static BaseType_t create_background_task_prefer_psram(TaskFunction_t task,
                                                      const char *name,
                                                      const uint32_t stack_depth,
                                                      void *arg,
                                                      const UBaseType_t priority,
                                                      const BaseType_t core_id)
{
    if (xTaskCreatePinnedToCoreWithCaps(task,
                                        name,
                                        stack_depth,
                                        arg,
                                        priority,
                                        nullptr,
                                        core_id,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        ESP_LOGI(TAG, "Started %s with a PSRAM-backed stack", name);
        return pdPASS;
    }

    ESP_LOGW(TAG,
             "Falling back to internal RAM stack for %s. Internal free=%u largest=%u PSRAM free=%u",
             name,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    return xTaskCreatePinnedToCore(task, name, stack_depth, arg, priority, nullptr, core_id);
}

static std::string trim_copy(const std::string &value)
{
    size_t begin = 0;
    while ((begin < value.size()) && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    size_t end = value.size();
    while ((end > begin) && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(begin, end - begin);
}

static std::string lowercase_copy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static ESP_Brookesia_CoreApp *find_installed_app_by_name(const std::string &query)
{
    if (s_phone == nullptr) {
        return nullptr;
    }

    const std::string normalized_query = lowercase_copy(trim_copy(query));
    if (normalized_query.empty()) {
        return nullptr;
    }

    for (int app_id = 0; app_id < 128; ++app_id) {
        ESP_Brookesia_CoreApp *app = s_phone->getManager().getInstalledApp(app_id);
        if ((app == nullptr) || (app->getName() == nullptr)) {
            continue;
        }

        const std::string app_name = app->getName();
        const std::string normalized_name = lowercase_copy(app_name);
        if ((normalized_name == normalized_query) || (lowercase_copy(trim_copy(app_name)) == normalized_query)) {
            return app;
        }

        std::string collapsed_name = normalized_name;
        collapsed_name.erase(std::remove(collapsed_name.begin(), collapsed_name.end(), ' '), collapsed_name.end());

        std::string collapsed_query = normalized_query;
        collapsed_query.erase(std::remove(collapsed_query.begin(), collapsed_query.end(), ' '), collapsed_query.end());
        if (collapsed_name == collapsed_query) {
            return app;
        }
    }

    return nullptr;
}

static void list_serial_apps(void)
{
    if (s_phone == nullptr) {
        printf("[app] phone unavailable\r\n");
        return;
    }

    ESP_Brookesia_CoreApp *active_app = s_phone->getManager().getActiveApp();
    printf("[app] Installed apps:\r\n");
    for (int app_id = 0; app_id < 128; ++app_id) {
        ESP_Brookesia_CoreApp *app = s_phone->getManager().getInstalledApp(app_id);
        if ((app == nullptr) || (app->getName() == nullptr)) {
            continue;
        }

        const bool is_running = (s_phone->getManager().getRunningAppById(app_id) != nullptr);
        const bool is_active = (active_app == app);
        printf("[app]   id=%d name=\"%s\" running=%s active=%s\r\n",
               app->getId(),
               app->getName(),
               is_running ? "yes" : "no",
               is_active ? "yes" : "no");
    }
}

static bool start_serial_app(const std::string &query)
{
    if (s_phone == nullptr) {
        printf("[app] phone unavailable\r\n");
        return false;
    }

    ESP_Brookesia_CoreApp *app = find_installed_app_by_name(query);
    if (app == nullptr) {
        printf("[app] not found: %s\r\n", query.c_str());
        return false;
    }

    ESP_Brookesia_CoreAppEventData_t app_event_data = {
        .id = app->getId(),
        .type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START,
    };

    bsp_display_lock(0);
    const bool ok = s_phone->sendAppEvent(&app_event_data);
    bsp_display_unlock();

    printf("[app] start name=\"%s\" id=%d %s\r\n", app->getName(), app->getId(), ok ? "queued" : "failed");
    return ok;
}

static bool start_serial_app_instance(ESP_Brookesia_CoreApp *app, const char *label)
{
    if (s_phone == nullptr) {
        printf("[app] phone unavailable\r\n");
        return false;
    }

    if (app == nullptr) {
        printf("[app] unavailable: %s\r\n", label == nullptr ? "unknown" : label);
        return false;
    }

    ESP_Brookesia_CoreAppEventData_t app_event_data = {
        .id = app->getId(),
        .type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START,
    };

    bsp_display_lock(0);
    const bool ok = s_phone->sendAppEvent(&app_event_data);
    bsp_display_unlock();

    printf("[app] start name=\"%s\" id=%d %s\r\n", label == nullptr ? app->getName() : label, app->getId(), ok ? "queued" : "failed");
    return ok;
}

static void print_serial_command_help(void)
{
    printf("[serial] Commands:\r\n");
    printf("[serial]   help\r\n");
    printf("[serial]   app.list\r\n");
    printf("[serial]   app.start <app name>\r\n");
    printf("[serial]   display.status\r\n");
    printf("[serial]   display.rotate 0|90|180|270\r\n");
    printf("[serial]   image.status\r\n");
    printf("[serial]   image.list\r\n");
    printf("[serial]   image.find <text>\r\n");
    printf("[serial]   image.open <index>\r\n");
    printf("[serial]   image.openname <text>\r\n");
    printf("[serial]   web.status\r\n");
    printf("[serial]   web.start\r\n");
    printf("[serial]   web.stop\r\n");
    printf("[serial]   web.toggle\r\n");

#if CONFIG_JC4880_APP_INTERNET_RADIO
    printf("[serial]   radio.status\r\n");
    printf("[serial]   radio.open\r\n");
    printf("[serial]   radio.openstation Country|Station\r\n");
    printf("[serial]   radio.openplay Country|Station\r\n");
    printf("[serial]   radio.stop\r\n");
    printf("[serial]   radio.play Country|Station\r\n");
    printf("[serial] Example: radio.play Azerbaijan|AVTO FM\r\n");
#endif
}

static void handle_serial_command(const std::string &raw_command)
{
    const std::string command = trim_copy(raw_command);
    if (command.empty()) {
        return;
    }

    printf("[serial] cmd=%s\r\n", command.c_str());
    if ((command == "help") || (command == "?")) {
        print_serial_command_help();
        return;
    }

    if (command == "app.list") {
        list_serial_apps();
        return;
    }

    if (command == "display.status") {
        const int32_t confirmed_degrees = load_display_orientation_from_nvs();
        printf("[display] saved orientation=%ld\r\n", static_cast<long>(confirmed_degrees));
        printf("[display] runtime live rotation enabled\r\n");
        return;
    }

    if (command == "display.confirm") {
        printf("[display] live rotation mode has no pending preview to confirm\r\n");
        return;
    }

    if (command == "display.revert") {
        printf("[display] live rotation mode has no pending preview to revert\r\n");
        return;
    }

    static constexpr const char *kDisplayRotatePrefix = "display.rotate ";
    if (command.rfind(kDisplayRotatePrefix, 0) == 0) {
        const std::string degrees_text = trim_copy(command.substr(std::strlen(kDisplayRotatePrefix)));
        if (degrees_text.empty()) {
            printf("[display] usage: display.rotate 0|90|180|270\r\n");
            return;
        }

        char *end = nullptr;
        const long parsed_degrees = std::strtol(degrees_text.c_str(), &end, 10);
        if ((end == degrees_text.c_str()) || (*end != '\0') || !is_valid_display_orientation_degrees(static_cast<int32_t>(parsed_degrees))) {
            printf("[display] invalid orientation: %s\r\n", degrees_text.c_str());
            printf("[display] usage: display.rotate 0|90|180|270\r\n");
            return;
        }

        if (!apply_display_orientation_live(static_cast<int32_t>(parsed_degrees), true)) {
            printf("[display] failed to apply live orientation=%ld\r\n", parsed_degrees);
            return;
        }

        printf("[display] applied live orientation=%ld\r\n", parsed_degrees);
        return;
    }

    static constexpr const char *kAppStartPrefix = "app.start ";
    if (command.rfind(kAppStartPrefix, 0) == 0) {
        const std::string app_name = trim_copy(command.substr(std::strlen(kAppStartPrefix)));
        if (app_name.empty()) {
            printf("[app] usage: app.start <app name>\r\n");
            return;
        }
        start_serial_app(app_name);
        return;
    }

    if (command == "image.status") {
        if (s_imageDisplayApp == nullptr) {
            printf("[image] app unavailable\r\n");
            return;
        }

        const std::string state = s_imageDisplayApp->debugDescribeState();
        printf("[image] %s\r\n", state.c_str());
        return;
    }

    if (command == "image.list") {
        if (s_imageDisplayApp == nullptr) {
            printf("[image] app unavailable\r\n");
            return;
        }

        const std::vector<std::string> image_paths = s_imageDisplayApp->scanImagePaths();
        printf("[image] count=%u\r\n", static_cast<unsigned>(image_paths.size()));
        for (size_t index = 0; index < image_paths.size(); ++index) {
            printf("[image]   %u %s\r\n",
                   static_cast<unsigned>(index),
                   image_paths[index].c_str());
        }
        return;
    }

    static constexpr const char *kImageFindPrefix = "image.find ";
    if (command.rfind(kImageFindPrefix, 0) == 0) {
        if (s_imageDisplayApp == nullptr) {
            printf("[image] app unavailable\r\n");
            return;
        }

        const std::string query = lowercase_copy(trim_copy(command.substr(std::strlen(kImageFindPrefix))));
        if (query.empty()) {
            printf("[image] usage: image.find <text>\r\n");
            return;
        }

        const std::vector<std::string> image_paths = s_imageDisplayApp->scanImagePaths();
        size_t matches = 0;
        for (size_t index = 0; index < image_paths.size(); ++index) {
            const std::string &image_path = image_paths[index];
            const size_t slash = image_path.find_last_of('/');
            const std::string basename = (slash == std::string::npos) ? image_path : image_path.substr(slash + 1);
            if (lowercase_copy(basename).find(query) == std::string::npos) {
                continue;
            }

            ++matches;
            printf("[image]   match index=%u path=%s\r\n",
                   static_cast<unsigned>(index),
                   image_path.c_str());
        }

        printf("[image] matches=%u query=%s\r\n", static_cast<unsigned>(matches), query.c_str());
        return;
    }

    static constexpr const char *kImageOpenPrefix = "image.open ";
    if (command.rfind(kImageOpenPrefix, 0) == 0) {
        if (s_imageDisplayApp == nullptr) {
            printf("[image] app unavailable\r\n");
            return;
        }

        const std::string index_text = trim_copy(command.substr(std::strlen(kImageOpenPrefix)));
        if (index_text.empty()) {
            printf("[image] usage: image.open <index>\r\n");
            return;
        }

        char *end = nullptr;
        const unsigned long parsed_index = std::strtoul(index_text.c_str(), &end, 10);
        if ((end == index_text.c_str()) || (*end != '\0')) {
            printf("[image] invalid index: %s\r\n", index_text.c_str());
            return;
        }

        if (!s_imageDisplayApp->debugQueueOpenIndex(static_cast<size_t>(parsed_index))) {
            printf("[image] open rejected index=%lu\r\n", parsed_index);
            return;
        }

        printf("[image] queued open index=%lu\r\n", parsed_index);
        start_serial_app_instance(s_imageDisplayApp, "image viewer");
        return;
    }

    static constexpr const char *kImageOpenNamePrefix = "image.openname ";
    if (command.rfind(kImageOpenNamePrefix, 0) == 0) {
        if (s_imageDisplayApp == nullptr) {
            printf("[image] app unavailable\r\n");
            return;
        }

        const std::string query = lowercase_copy(trim_copy(command.substr(std::strlen(kImageOpenNamePrefix))));
        if (query.empty()) {
            printf("[image] usage: image.openname <text>\r\n");
            return;
        }

        const std::vector<std::string> image_paths = s_imageDisplayApp->scanImagePaths();
        int matched_index = -1;
        size_t match_count = 0;
        for (size_t index = 0; index < image_paths.size(); ++index) {
            const std::string &image_path = image_paths[index];
            const size_t slash = image_path.find_last_of('/');
            const std::string basename = (slash == std::string::npos) ? image_path : image_path.substr(slash + 1);
            if (lowercase_copy(basename).find(query) == std::string::npos) {
                continue;
            }

            matched_index = static_cast<int>(index);
            ++match_count;
            if (match_count > 1U) {
                break;
            }
        }

        if (match_count == 0U) {
            printf("[image] no match for %s\r\n", query.c_str());
            return;
        }

        if (match_count > 1U) {
            printf("[image] ambiguous query %s; use image.find first\r\n", query.c_str());
            return;
        }

        if (!s_imageDisplayApp->debugQueueOpenIndex(static_cast<size_t>(matched_index))) {
            printf("[image] open rejected for %s index=%d\r\n", query.c_str(), matched_index);
            return;
        }

        printf("[image] queued open name=%s index=%d\r\n", query.c_str(), matched_index);
        start_serial_app_instance(s_imageDisplayApp, "image viewer");
        return;
    }

    if (command == "web.status") {
        printf("[web] running=%s\r\n", WebServerService::instance().isRunning() ? "yes" : "no");
        printf("[web] %s\r\n", WebServerService::instance().statusText().c_str());
        return;
    }

    if (command == "web.start") {
        printf("[web] start %s\r\n", WebServerService::instance().start() ? "ok" : "failed");
        printf("[web] %s\r\n", WebServerService::instance().statusText().c_str());
        return;
    }

    if (command == "web.stop") {
        printf("[web] stop %s\r\n", WebServerService::instance().stop() ? "ok" : "failed");
        printf("[web] %s\r\n", WebServerService::instance().statusText().c_str());
        return;
    }

    if (command == "web.toggle") {
        printf("[web] toggle %s\r\n", WebServerService::instance().toggle() ? "ok" : "failed");
        printf("[web] %s\r\n", WebServerService::instance().statusText().c_str());
        return;
    }

#if CONFIG_JC4880_APP_INTERNET_RADIO
    if (command == "radio.status") {
        if (s_internetRadioApp == nullptr) {
            printf("[radio] app unavailable\r\n");
            return;
        }
        printf("[radio] %s\r\n", s_internetRadioApp->debugDescribeState().c_str());
        return;
    }

    if (command == "radio.open") {
        if (s_internetRadioApp == nullptr) {
            printf("[radio] app unavailable\r\n");
            return;
        }

        printf("[radio] opening app on screen\r\n");
        start_serial_app_instance(s_internetRadioApp, "internet radio");
        printf("[radio] open %s\r\n", s_internetRadioApp->debugOpenVisible() ? "queued" : "failed");
        return;
    }

    auto parse_radio_country_station = [](const std::string &arguments, std::string *country_out, std::string *station_out) {
        if ((country_out == nullptr) || (station_out == nullptr)) {
            return false;
        }

        const size_t separator = arguments.find('|');
        if (separator == std::string::npos) {
            return false;
        }

        *country_out = trim_copy(arguments.substr(0, separator));
        *station_out = trim_copy(arguments.substr(separator + 1));
        return !country_out->empty() && !station_out->empty();
    };

    static constexpr const char *kOpenStationPrefix = "radio.openstation ";
    if (command.rfind(kOpenStationPrefix, 0) == 0) {
        if (s_internetRadioApp == nullptr) {
            printf("[radio] app unavailable\r\n");
            return;
        }

        std::string country;
        std::string station;
        if (!parse_radio_country_station(trim_copy(command.substr(std::strlen(kOpenStationPrefix))), &country, &station)) {
            printf("[radio] usage: radio.openstation Country|Station\r\n");
            return;
        }

        printf("[radio] opening visible station country='%s' station='%s'\r\n", country.c_str(), station.c_str());
        start_serial_app_instance(s_internetRadioApp, "internet radio");
        printf("[radio] openstation %s\r\n", s_internetRadioApp->debugOpenStationVisible(country, station, false) ? "queued" : "failed");
        return;
    }

    static constexpr const char *kOpenPlayPrefix = "radio.openplay ";
    if (command.rfind(kOpenPlayPrefix, 0) == 0) {
        if (s_internetRadioApp == nullptr) {
            printf("[radio] app unavailable\r\n");
            return;
        }

        std::string country;
        std::string station;
        if (!parse_radio_country_station(trim_copy(command.substr(std::strlen(kOpenPlayPrefix))), &country, &station)) {
            printf("[radio] usage: radio.openplay Country|Station\r\n");
            return;
        }

        printf("[radio] opening visible play country='%s' station='%s'\r\n", country.c_str(), station.c_str());
        start_serial_app_instance(s_internetRadioApp, "internet radio");
        printf("[radio] openplay %s\r\n", s_internetRadioApp->debugOpenStationVisible(country, station, true) ? "queued" : "failed");
        return;
    }

    if (command == "radio.stop") {
        if (s_internetRadioApp == nullptr) {
            printf("[radio] app unavailable\r\n");
            return;
        }
        printf("[radio] stop requested\r\n");
        printf("[radio] stop %s\r\n", s_internetRadioApp->debugStopPlayback() ? "queued" : "failed");
        return;
    }

    static constexpr const char *kPlayPrefix = "radio.play ";
    if (command.rfind(kPlayPrefix, 0) == 0) {
        if (s_internetRadioApp == nullptr) {
            printf("[radio] app unavailable\r\n");
            return;
        }

        const std::string arguments = trim_copy(command.substr(std::strlen(kPlayPrefix)));
        const size_t separator = arguments.find('|');
        if (separator == std::string::npos) {
            printf("[radio] usage: radio.play Country|Station\r\n");
            return;
        }

        const std::string country = trim_copy(arguments.substr(0, separator));
        const std::string station = trim_copy(arguments.substr(separator + 1));
        if (country.empty() || station.empty()) {
            printf("[radio] usage: radio.play Country|Station\r\n");
            return;
        }

        printf("[radio] play requested country='%s' station='%s'\r\n", country.c_str(), station.c_str());
        printf("[radio] play %s\r\n", s_internetRadioApp->debugPlayStation(country, station) ? "queued" : "failed");
        return;
    }
#else
    if ((command == "radio.status") || (command == "radio.open") || (command == "radio.stop") ||
        (command.rfind("radio.openstation ", 0) == 0) || (command.rfind("radio.openplay ", 0) == 0) ||
        (command.rfind("radio.play ", 0) == 0)) {
        printf("[radio] internet radio app is disabled in menuconfig\r\n");
        return;
    }
#endif

    printf("[serial] Unknown command. Type 'help'.\r\n");
}

static void serial_command_task(void *parameter)
{
    (void)parameter;

    std::string line;
    line.reserve(256);
    printf("[serial] USB serial radio debug ready\r\n");
    print_serial_command_help();
    printf("> ");
    fflush(stdout);

    for (;;) {
        uint8_t byte = 0;
        const int read = usb_serial_jtag_read_bytes(&byte, sizeof(byte), pdMS_TO_TICKS(20));
        if (read <= 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (byte == '\r') {
            continue;
        }

        if (byte == '\n') {
            if (!line.empty()) {
                handle_serial_command(line);
                line.clear();
            }

            printf("> ");
            fflush(stdout);
            continue;
        }

        if (((byte == '\b') || (byte == 0x7F)) && !line.empty()) {
            line.pop_back();
            continue;
        }

        if (line.size() < 255) {
            line.push_back(static_cast<char>(byte));
        }
    }
}

static void init_serial_command_console(void)
{
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t ret = usb_serial_jtag_driver_install(&config);
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE)) {
        ESP_LOGW(TAG, "USB Serial/JTAG driver install failed: %s", esp_err_to_name(ret));
        return;
    }

    usb_serial_jtag_vfs_use_driver();
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    if (create_background_task_prefer_psram(serial_command_task,
                                            "serial_cmd",
                                            kSerialCommandTaskStack,
                                            nullptr,
                                            2,
                                            0) != pdPASS) {
        ESP_LOGW(TAG, "Failed to start serial command console task");
    }
}

template <typename T>
static T *install_app_or_delete(ESP_Brookesia_Phone &phone, T *app, const char *app_name)
{
    const size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    if (app == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate %s", app_name);
        return nullptr;
    }

    if (phone.installApp(app) < 0) {
        ESP_LOGW(TAG,
                 "Skipping %s because install/init failed (internal delta: %d bytes, psram delta: %d bytes)",
                 app_name,
                 (int)(internal_before - heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 (int)(psram_before - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        delete app;
        return nullptr;
    }

    ESP_LOGI(TAG,
             "Installed %s (internal delta: %d bytes, psram delta: %d bytes, internal free: %u KB, psram free: %u KB)",
             app_name,
             (int)(internal_before - heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             (int)(psram_before - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

    return app;
}

template <typename T>
static void uninstall_app_and_delete(ESP_Brookesia_Phone &phone, T *&app, const char *app_name)
{
    if (app == nullptr) {
        return;
    }

    if (phone.uninstallApp(app) < 0) {
        ESP_LOGW(TAG, "Failed to uninstall %s", app_name);
        return;
    }

    delete app;
    app = nullptr;
}

static bool probe_sdcard_health(void)
{
    return (bsp_sdcard != nullptr) && (sdmmc_get_status(bsp_sdcard) == ESP_OK);
}

static void sdcard_startup_mount_task(void *arg)
{
    (void)arg;

    vTaskDelay(kSdcardStartupDelay);

    for (uint32_t attempt = 0; attempt < kSdcardStartupMountAttempts; ++attempt) {
        const bool mounted = sync_sdcard_mount_state(true);
        if (mounted) {
            ESP_LOGI(TAG, "SD card mounted by background startup task");
            vTaskDelete(nullptr);
            return;
        }

        vTaskDelay(kSdcardMountRetryPeriod);
    }

    ESP_LOGI(TAG, "SD card not mounted during startup probe window");

    vTaskDelete(nullptr);
}

static bool sync_sdcard_mount_state(bool try_mount)
{
    if (s_sdcardMutex == nullptr) {
        return false;
    }

    xSemaphoreTake(s_sdcardMutex, portMAX_DELAY);

    if (s_sdcardMounted && !probe_sdcard_health()) {
        esp_err_t unmount_ret = bsp_sdcard_unmount();
        if (unmount_ret != ESP_OK) {
            ESP_LOGW(TAG, "SD card unmount after removal failed: %s", esp_err_to_name(unmount_ret));
        }
        s_sdcardMounted = false;
        s_nextSdcardMountAttempt = xTaskGetTickCount() + kSdcardMountRetryPeriod;
    }

    if (!s_sdcardMounted && try_mount) {
        const TickType_t now = xTaskGetTickCount();
        if ((s_nextSdcardMountAttempt == 0) || (now >= s_nextSdcardMountAttempt)) {
            set_sdcard_probe_log_levels(ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_ERROR);
            const esp_err_t mount_ret = bsp_sdcard_mount();
            set_sdcard_probe_log_levels(ESP_LOG_INFO, ESP_LOG_ERROR, ESP_LOG_ERROR);

            if (mount_ret != ESP_OK) {
                ESP_LOGW(TAG, "SD card mount failed: %s", esp_err_to_name(mount_ret));
            }

            s_sdcardMounted = (mount_ret == ESP_OK) && probe_sdcard_health();
            if (!s_sdcardMounted) {
                bsp_sdcard_unmount();
                s_nextSdcardMountAttempt = now + kSdcardMountRetryPeriod;
            } else {
                s_nextSdcardMountAttempt = 0;
            }
        }
    }

    const bool mounted = s_sdcardMounted;
    xSemaphoreGive(s_sdcardMutex);
    return mounted;
}

extern "C" bool app_storage_is_sdcard_mounted(void)
{
    return sync_sdcard_mount_state(false);
}

extern "C" bool app_storage_ensure_sdcard_available(void)
{
    return sync_sdcard_mount_state(true);
}

static bool load_nvs_string(const char *key, std::string &value)
{
    nvs_handle_t handle;
    if (nvs_open(kNvsStorageNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    size_t required_size = 0;
    esp_err_t err = nvs_get_str(handle, key, nullptr, &required_size);
    if ((err != ESP_OK) || (required_size == 0)) {
        nvs_close(handle);
        return false;
    }

    std::string buffer(required_size, '\0');
    err = nvs_get_str(handle, key, buffer.data(), &required_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        return false;
    }

    if (!buffer.empty() && (buffer.back() == '\0')) {
        buffer.pop_back();
    }
    value = buffer;
    return !value.empty();
}

static void clear_pending_release_notes(void)
{
    nvs_handle_t handle;
    if (nvs_open(kNvsStorageNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    nvs_set_i32(handle, kNvsKeyOtaPendingShow, 0);
    nvs_set_str(handle, kNvsKeyOtaPendingVersion, "");
    nvs_set_str(handle, kNvsKeyOtaPendingNotes, "");
    nvs_commit(handle);
    nvs_close(handle);
}

static bool get_running_ota_image_state(esp_ota_img_states_t &state)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == nullptr) {
        return false;
    }

    return esp_ota_get_state_partition(running, &state) == ESP_OK;
}

static bool pending_release_notes_match_running_version(void)
{
    std::string pending_version;
    if (!load_nvs_string(kNvsKeyOtaPendingVersion, pending_version) || pending_version.empty()) {
        return true;
    }

    const esp_app_desc_t *app_desc = esp_app_get_description();
    if ((app_desc == nullptr) || (app_desc->version[0] == '\0')) {
        return false;
    }

    return pending_version == app_desc->version;
}

static void close_release_notes_popup(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_current_target(event);
    if (target != nullptr) {
        lv_msgbox_close_async(target);
    }
}

static void close_reset_notice_popup(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_current_target(event);
    if (target == nullptr) {
        return;
    }

    const char *button_text = lv_msgbox_get_active_btn_text(target);
    if ((button_text != nullptr) && (std::strcmp(button_text, "Report") == 0)) {
        start_crash_report_upload(0, true);
        return;
    }

    if ((button_text != nullptr) && (std::strcmp(button_text, "OK") == 0)) {
        lv_msgbox_close_async(target);
    }
}

static void show_pending_release_notes_popup(void *context)
{
    std::unique_ptr<PendingReleaseNotesContext> notes(static_cast<PendingReleaseNotesContext *>(context));
    if ((notes == nullptr) || (notes->notes.empty() && notes->version.empty())) {
        return;
    }

    clear_pending_release_notes();

    static const char *buttons[] = {"Close", ""};
    std::string body = "Firmware installed successfully.";
    if (!notes->version.empty()) {
        body += "\nVersion: " + notes->version;
    }
    body += "\n\nNotes:\n";
    body += notes->notes.empty() ? std::string("No release notes were provided.") : notes->notes;

    lv_obj_t *msgbox = lv_msgbox_create(lv_layer_top(), "Firmware Installed", body.c_str(), buttons, false);
    if (msgbox == nullptr) {
        return;
    }

    lv_obj_set_size(msgbox, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(msgbox, 0, 0);
    lv_obj_center(msgbox);
    lv_obj_add_event_cb(msgbox, close_release_notes_popup, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void schedule_pending_release_notes_popup(void)
{
    nvs_handle_t handle;
    if (nvs_open(kNvsStorageNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    int32_t should_show = 0;
    if ((nvs_get_i32(handle, kNvsKeyOtaPendingShow, &should_show) != ESP_OK) || (should_show == 0)) {
        nvs_close(handle);
        return;
    }
    nvs_close(handle);

    if (!pending_release_notes_match_running_version()) {
        clear_pending_release_notes();
        return;
    }

    auto *context = new PendingReleaseNotesContext();
    if (context == nullptr) {
        return;
    }

    if (!load_nvs_string(kNvsKeyOtaPendingNotes, context->notes)) {
        delete context;
        clear_pending_release_notes();
        return;
    }
    load_nvs_string(kNvsKeyOtaPendingVersion, context->version);

    bsp_display_lock(0);
    if (lv_async_call(show_pending_release_notes_popup, context) != LV_RES_OK) {
        bsp_display_unlock();
        delete context;
        return;
    }
    bsp_display_unlock();
}

static void ota_validation_task(void *arg)
{
    (void)arg;

    vTaskDelay(kOtaValidationDelay);

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (!get_running_ota_image_state(state) || (state != ESP_OTA_IMG_PENDING_VERIFY)) {
        vTaskDelete(nullptr);
        return;
    }

    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mark running OTA image valid: %s", esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "Marked running OTA image valid after boot grace period");
    schedule_pending_release_notes_popup();
    vTaskDelete(nullptr);
}

static void schedule_ota_validation_if_needed(void)
{
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (!get_running_ota_image_state(state) || (state != ESP_OTA_IMG_PENDING_VERIFY)) {
        schedule_pending_release_notes_popup();
        return;
    }

    if (xTaskCreatePinnedToCore(ota_validation_task,
                                "ota_validate",
                                kOtaValidationTaskStack,
                                nullptr,
                                2,
                                nullptr,
                                1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start OTA validation task");
    }
}

static bool describe_reset_reason(esp_reset_reason_t reason, std::string &title, std::string &details)
{
    switch (reason) {
    case ESP_RST_PANIC:
        title = "Recovered After Crash";
        details = "The device restarted after a fatal software exception. This is usually caused by invalid memory access or an LVGL/UI misuse.";
        return true;
    case ESP_RST_TASK_WDT:
        title = "Recovered After Watchdog";
        details = "The device restarted because a task stopped responding for too long.";
        return true;
    case ESP_RST_INT_WDT:
        title = "Recovered After Watchdog";
        details = "The device restarted because an interrupt or critical section blocked the system for too long.";
        return true;
    case ESP_RST_WDT:
        title = "Recovered After Watchdog";
        details = "The device restarted because a watchdog timer expired.";
        return true;
    case ESP_RST_BROWNOUT:
        title = "Recovered After Power Drop";
        details = "The device restarted because input power dropped below a safe level.";
        return true;
    case ESP_RST_CPU_LOCKUP:
        title = "Recovered After CPU Lockup";
        details = "The device restarted because the CPU stopped making forward progress.";
        return true;
    default:
        return false;
    }
}

static void show_reset_notice_popup(void *context)
{
    std::unique_ptr<PendingResetNoticeContext> notice(static_cast<PendingResetNoticeContext *>(context));
    if ((notice == nullptr) || notice->details.empty()) {
        return;
    }

    static const char *buttons_with_report[] = {"OK", "Report", ""};
    static const char *buttons_ok_only[] = {"OK", ""};
    const char **buttons = notice->has_report ? buttons_with_report : buttons_ok_only;
    lv_obj_t *msgbox = lv_msgbox_create(nullptr, notice->title.c_str(), notice->details.c_str(), buttons, false);
    if (msgbox == nullptr) {
        return;
    }

    lv_obj_set_width(msgbox, 420);
    lv_obj_center(msgbox);
    lv_obj_add_event_cb(msgbox, close_reset_notice_popup, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void schedule_previous_reset_notice_popup(void)
{
    std::string title;
    std::string details;
    if (!describe_reset_reason(esp_reset_reason(), title, details)) {
        return;
    }

    auto *context = new PendingResetNoticeContext{title, details, has_saved_crash_report()};
    if (context == nullptr) {
        return;
    }

    bsp_display_lock(0);
    if (lv_async_call(show_reset_notice_popup, context) != LV_RES_OK) {
        bsp_display_unlock();
        delete context;
        return;
    }
    bsp_display_unlock();
}

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    const int32_t saved_display_orientation = load_display_orientation_from_nvs();

    initialize_power_management();

    restore_audio_preferences_from_nvs();
    bsp_display_set_startup_rotation(static_cast<lv_disp_rotation_t>(LV_DISP_ROT_NONE));
    ESP_LOGI(TAG,
             "Startup display orientation preference=%ld; applying live rotation after display init",
             static_cast<long>(saved_display_orientation));

    s_sdcardMutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_sdcardMutex != nullptr ? ESP_OK : ESP_ERR_NO_MEM);

    if (create_background_task_prefer_psram(sdcard_startup_mount_task,
                                            "sdcard_startup",
                                            kSdcardStartupTaskStack,
                                            nullptr,
                                            1,
                                            1) != pdPASS) {
        ESP_LOGW(TAG, "Failed to start SD card startup mount task");
    }

    ESP_ERROR_CHECK(bsp_spiffs_mount());
    ESP_LOGI(TAG, "SPIFFS mount successfully");
    capture_panic_report_if_present();

 //    ESP_ERROR_CHECK(bsp_extra_codec_init());

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 120,
        .double_buffer = true,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = true,
        }
    };
    cfg.lvgl_port_cfg.task_affinity = 0;
    lv_display_t *display = bsp_display_start_with_config(&cfg);
    ESP_ERROR_CHECK(display != nullptr ? ESP_OK : ESP_ERR_INVALID_STATE);
    bsp_display_backlight_on();

    bsp_display_lock(0);

    ESP_Brookesia_Phone *phone = new ESP_Brookesia_Phone();
    assert(phone != nullptr && "Failed to create phone");

    ESP_Brookesia_PhoneStylesheet_t *phone_stylesheet = new ESP_Brookesia_PhoneStylesheet_t ESP_BROOKESIA_PHONE_480_800_DARK_STYLESHEET();
    ESP_BROOKESIA_CHECK_NULL_EXIT(phone_stylesheet, "Create phone stylesheet failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->addStylesheet(*phone_stylesheet), "Add phone stylesheet failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->activateStylesheet(*phone_stylesheet), "Activate phone stylesheet failed");

    ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->begin(), "Failed to begin phone");

    if ((saved_display_orientation != 0) && !apply_display_orientation_live(saved_display_orientation, false)) {
        ESP_LOGW(TAG,
                 "Failed to apply saved live display orientation %ld after phone init; continuing at base orientation",
                 static_cast<long>(saved_display_orientation));
    }

    if (!system_ui_service::initialize(*phone)) {
        ESP_LOGW(TAG, "System UI service initialization failed");
    }
    if (!init_ui_haptic_feedback_output()) {
        ESP_LOGW(TAG, "Haptic feedback GPIO34 init failed");
    }
    if (!install_ui_tap_sound_feedback(*phone)) {
        ESP_LOGW(TAG, "Tap sound feedback hook was not installed");
    }
    install_app_or_delete(*phone, new PhoneAppSquareline(), "phone app squareline");

#if CONFIG_JC4880_APP_CALCULATOR
    install_app_or_delete(*phone, new Calculator(), "calculator");
#endif

#if CONFIG_JC4880_APP_SETTINGS
    if (AppSettings *settings = install_app_or_delete(*phone, new AppSettings(), "settings"); settings != nullptr) {
        ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->hideLauncherIcon(settings->getId()), "Hide Settings launcher icon failed");
    }
#endif

#if CONFIG_JC4880_APP_SEGA_EMULATOR
    install_app_or_delete(*phone, new SegaEmulator(), "sega emulator");
#endif

#if CONFIG_JC4880_APP_IMAGE_VIEWER
    s_imageDisplayApp = install_app_or_delete(*phone, new AppImageDisplay(), "image viewer");
#endif

#if CONFIG_JC4880_APP_FILE_MANAGER
    install_app_or_delete(*phone, new FileManager(), "file manager");
#endif

#if CONFIG_JC4880_APP_WEB_SERVER
    install_app_or_delete(*phone, new WebServerApp(), "web server");
#endif

#if CONFIG_JC4880_APP_MUSIC_PLAYER
    install_app_or_delete(*phone, new MusicPlayer(), "music player");
#endif

#if CONFIG_JC4880_APP_INTERNET_RADIO
    s_internetRadioApp = install_app_or_delete(*phone, new InternetRadio(), "internet radio");
#endif

#if CONFIG_JC4880_APP_EREADER
    install_app_or_delete(*phone, new EReaderApp(), "e-reader");
#endif

#if CONFIG_JC4880_APP_MQTT
    install_app_or_delete(*phone, new MqttApp(), "mqtt");
#endif

    uint16_t free_sram_size_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    uint16_t total_sram_size_kb = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;
    uint16_t free_psram_size_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    uint16_t total_psram_size_kb = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
    ESP_LOGI(TAG, "Free sram size: %d KB, total sram size: %d KB, "
                         "free psram size: %d KB, total psram size: %d KB",
                         free_sram_size_kb, total_sram_size_kb, free_psram_size_kb, total_psram_size_kb);

#if CONFIG_JC4880_FEATURE_SECURITY
    device_security::init(phone);
#endif

    free_sram_size_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    total_sram_size_kb = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;
    free_psram_size_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    total_psram_size_kb = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
    ESP_LOGI(TAG, "Free sram size: %d KB, total sram size: %d KB, "
                         "free psram size: %d KB, total psram size: %d KB",
                         free_sram_size_kb, total_sram_size_kb, free_psram_size_kb, total_psram_size_kb);

    
    ESP_LOGI(TAG,"setup done");
    bsp_display_unlock();
    init_serial_command_console();

#if CONFIG_JC4880_FEATURE_SECURITY
    device_security::promptBootUnlockIfNeeded();
#endif

    schedule_previous_reset_notice_popup();
    schedule_ota_validation_if_needed();
}