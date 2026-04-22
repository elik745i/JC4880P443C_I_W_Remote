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
#include "esp_http_client.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_memory_utils.h"
#include "esp_heap_caps.h"
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
static constexpr uint32_t kSerialCommandTaskStack = 6144;
static constexpr uint32_t kCrashReportUploadTaskStack = 6144;
static constexpr const char *kNvsStorageNamespace = "storage";
static constexpr const char *kNvsKeyOtaPendingVersion = "ota_ver";
static constexpr const char *kNvsKeyOtaPendingNotes = "ota_notes";
static constexpr const char *kNvsKeyOtaPendingShow = "ota_show";
static constexpr const char *kCrashReportLocalPath = BSP_SPIFFS_MOUNT_POINT "/last_crash_report.txt";
static constexpr const char *kCrashReportPendingPath = BSP_SPIFFS_MOUNT_POINT "/pending_crash_report.txt";
static constexpr const char *kCrashReportUploadUrl = "";
static constexpr TickType_t kCrashReportUploadDelay = pdMS_TO_TICKS(15000);

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

static BaseType_t create_background_task_prefer_psram(TaskFunction_t task,
                                                      const char *name,
                                                      uint32_t stack_depth,
                                                      void *arg,
                                                      UBaseType_t priority,
                                                      BaseType_t core_id);

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
            set_sdcard_probe_log_levels(ESP_LOG_NONE, ESP_LOG_NONE, ESP_LOG_NONE);
            const esp_err_t mount_ret = bsp_sdcard_mount();
            set_sdcard_probe_log_levels(ESP_LOG_INFO, ESP_LOG_ERROR, ESP_LOG_ERROR);

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
    if ((notes == nullptr) || notes->notes.empty()) {
        return;
    }

    clear_pending_release_notes();

    static const char *buttons[] = {"OK", ""};
    const std::string title = notes->version.empty() ? std::string("What's New") : (std::string("What's New in ") + notes->version);
    lv_obj_t *msgbox = lv_msgbox_create(nullptr, title.c_str(), notes->notes.c_str(), buttons, false);
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

    s_sdcardMutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_sdcardMutex != nullptr ? ESP_OK : ESP_ERR_NO_MEM);

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
    install_app_or_delete(*phone, new PhoneAppSquareline(), "phone app squareline");

#if CONFIG_JC4880_APP_CALCULATOR
    install_app_or_delete(*phone, new Calculator(), "calculator");
#endif

#if CONFIG_JC4880_APP_SETTINGS
    install_app_or_delete(*phone, new AppSettings(), "settings");
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