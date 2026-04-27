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
static constexpr const char *kCrashReportLocalPath = BSP_SPIFFS_MOUNT_POINT "/last_crash_report.txt";
static constexpr const char *kCrashReportPendingPath = BSP_SPIFFS_MOUNT_POINT "/pending_crash_report.txt";
static constexpr const char *kCrashReportUploadUrl = "";
static constexpr TickType_t kCrashReportUploadDelay = pdMS_TO_TICKS(15000);
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

struct CrashReportUploadTaskContext {
    TickType_t delay = 0;
    bool notify_user = false;
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
static std::vector<std::unique_ptr<lv_indev_drv_t>> s_tapSoundDriverCopies;
static esp_timer_handle_t s_hapticPulseTimer = nullptr;
static bool s_hapticPwmReady = false;

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

static void print_serial_command_help(void)
{
    printf("[serial] Commands:\r\n");
    printf("[serial]   help\r\n");

#if CONFIG_JC4880_APP_INTERNET_RADIO
    printf("[serial]   radio.status\r\n");
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

#if CONFIG_JC4880_APP_INTERNET_RADIO
    if (command == "radio.status") {
        if (s_internetRadioApp == nullptr) {
            printf("[radio] app unavailable\r\n");
            return;
        }
        printf("[radio] %s\r\n", s_internetRadioApp->debugDescribeState().c_str());
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
    if ((command == "radio.status") || (command == "radio.stop") || (command.rfind("radio.play ", 0) == 0)) {
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

    lv_obj_t *msgbox = lv_msgbox_create(nullptr, "Firmware Installed", body.c_str(), buttons, false);
    if (msgbox == nullptr) {
        return;
    }

    lv_obj_set_width(msgbox, 420);
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

    initialize_power_management();

    restore_audio_preferences_from_nvs();

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
        .buffer_size = BSP_LCD_H_RES * 80,
        .double_buffer = false,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
        }
    };
    cfg.lvgl_port_cfg.task_affinity = 0;
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    bsp_display_lock(0);

    ESP_Brookesia_Phone *phone = new ESP_Brookesia_Phone();
    assert(phone != nullptr && "Failed to create phone");

    ESP_Brookesia_PhoneStylesheet_t *phone_stylesheet = new ESP_Brookesia_PhoneStylesheet_t ESP_BROOKESIA_PHONE_480_800_DARK_STYLESHEET();
    ESP_BROOKESIA_CHECK_NULL_EXIT(phone_stylesheet, "Create phone stylesheet failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->addStylesheet(*phone_stylesheet), "Add phone stylesheet failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->activateStylesheet(*phone_stylesheet), "Activate phone stylesheet failed");

    ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->begin(), "Failed to begin phone");
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
    install_app_or_delete(*phone, new AppImageDisplay(), "image viewer");
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
    schedule_pending_release_notes_popup();
}