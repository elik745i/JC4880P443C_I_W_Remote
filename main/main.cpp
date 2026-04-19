#include "freertos/FreeRTOS.h"
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
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"
#include "device_security.hpp"
#include "sdmmc_cmd.h"

#include "esp_brookesia.hpp"
#include "app_examples/phone/squareline/src/phone_app_squareline.hpp"
#include "apps.h"
 
static const char *TAG = "main";
static constexpr TickType_t kSdcardMonitorPeriod = pdMS_TO_TICKS(2000);
static constexpr uint32_t kSerialCommandTaskStack = 6144;
static constexpr const char *kNvsStorageNamespace = "storage";
static constexpr const char *kNvsKeyOtaPendingVersion = "ota_ver";
static constexpr const char *kNvsKeyOtaPendingNotes = "ota_notes";
static constexpr const char *kNvsKeyOtaPendingShow = "ota_show";

struct PendingReleaseNotesContext {
    std::string version;
    std::string notes;
};

struct SdcardRuntimeApps {
    ESP_Brookesia_Phone *phone = nullptr;
    FileManager *file_manager = nullptr;
    MusicPlayer *music_player = nullptr;
};

static SemaphoreHandle_t s_sdcardMutex = nullptr;
static bool s_sdcardMounted = false;
static SdcardRuntimeApps s_sdcardRuntimeApps;
static InternetRadio *s_internetRadioApp = nullptr;

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
    printf("[serial]   radio.status\r\n");
    printf("[serial]   radio.stop\r\n");
    printf("[serial]   radio.play Country|Station\r\n");
    printf("[serial] Example: radio.play Azerbaijan|AVTO FM\r\n");
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

    xTaskCreatePinnedToCore(serial_command_task, "serial_cmd", kSerialCommandTaskStack, nullptr, 2, nullptr, 0);
}

template <typename T>
static T *install_app_or_delete(ESP_Brookesia_Phone &phone, T *app, const char *app_name)
{
    if (app == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate %s", app_name);
        return nullptr;
    }

    if (phone.installApp(app) < 0) {
        ESP_LOGW(TAG, "Skipping %s because install/init failed", app_name);
        delete app;
        return nullptr;
    }

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
    }

    if (!s_sdcardMounted && try_mount) {
        s_sdcardMounted = (bsp_sdcard_mount() == ESP_OK) && probe_sdcard_health();
        if (!s_sdcardMounted) {
            bsp_sdcard_unmount();
        }
    }

    const bool mounted = s_sdcardMounted;
    xSemaphoreGive(s_sdcardMutex);
    return mounted;
}

static bool with_display_lock(TickType_t timeout_ms, void (*callback)(void *), void *context)
{
    if (!bsp_display_lock(timeout_ms)) {
        ESP_LOGW(TAG, "Timed out waiting for display lock");
        return false;
    }

    callback(context);
    bsp_display_unlock();
    return true;
}

static void install_storage_apps_locked(void *context)
{
    (void)context;

    if (s_sdcardRuntimeApps.phone == nullptr) {
        return;
    }

    if (s_sdcardRuntimeApps.file_manager == nullptr) {
        s_sdcardRuntimeApps.file_manager = install_app_or_delete(*s_sdcardRuntimeApps.phone, new FileManager(), "file manager");
    }

    if (s_sdcardRuntimeApps.music_player == nullptr) {
        s_sdcardRuntimeApps.music_player = install_app_or_delete(*s_sdcardRuntimeApps.phone, new MusicPlayer(), "music player");
    }
}

static void uninstall_storage_apps_locked(void *context)
{
    (void)context;

    if (s_sdcardRuntimeApps.phone == nullptr) {
        return;
    }

    uninstall_app_and_delete(*s_sdcardRuntimeApps.phone, s_sdcardRuntimeApps.music_player, "music player");
    uninstall_app_and_delete(*s_sdcardRuntimeApps.phone, s_sdcardRuntimeApps.file_manager, "file manager");
}

static void ensure_sdcard_apps_available(void)
{
    if (s_sdcardRuntimeApps.phone == nullptr) {
        return;
    }

    with_display_lock(1000, install_storage_apps_locked, nullptr);
}

static void remove_sdcard_apps(void)
{
    if (s_sdcardRuntimeApps.phone == nullptr) {
        return;
    }

    with_display_lock(1000, uninstall_storage_apps_locked, nullptr);
}

static void sdcard_monitor_task(void *parameter)
{
    (void)parameter;

    bool last_state = sync_sdcard_mount_state(false);
    for (;;) {
        const bool current_state = sync_sdcard_mount_state(true);
        if (current_state != last_state) {
            ESP_LOGI(TAG, "SD card %s", current_state ? "available" : "removed");
            if (current_state) {
                ensure_sdcard_apps_available();
            } else {
                remove_sdcard_apps();
            }
            last_state = current_state;
        } else if (current_state && ((s_sdcardRuntimeApps.file_manager == nullptr) ||
                                     (s_sdcardRuntimeApps.music_player == nullptr))) {
            ensure_sdcard_apps_available();
        }
        vTaskDelay(kSdcardMonitorPeriod);
    }
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
    lv_obj_t *target = lv_event_get_target(event);
    if (target != nullptr) {
        lv_msgbox_close(target);
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

    const bool sdcard_available_on_boot = sync_sdcard_mount_state(true);
    if (sdcard_available_on_boot) {
        ESP_LOGI(TAG, "SD card mount successfully");
    } else {
        ESP_LOGW(TAG, "No SD card detected at boot, continuing without removable storage");
    }
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

    s_sdcardRuntimeApps.phone = phone;

    install_app_or_delete(*phone, new PhoneAppSquareline(), "phone app squareline");

    install_app_or_delete(*phone, new Calculator(), "calculator");

    install_app_or_delete(*phone, new AppSettings(), "settings");

    install_app_or_delete(*phone, new Game2048(), "2048");

    install_app_or_delete(*phone, new SegaEmulator(), "sega emulator");

    Camera *camera = install_app_or_delete(*phone, new Camera(1288, 728), "camera");
    if ((camera != nullptr) && (camera->get_camera_ctlr_handle() < 0)) {
        ESP_LOGW(TAG, "Camera hardware is unavailable, uninstalling camera app");
        phone->uninstallApp(camera);
        delete camera;
        camera = nullptr;
    }
        
    install_app_or_delete(*phone, new AppImageDisplay(), "image viewer");

    s_internetRadioApp = install_app_or_delete(*phone, new InternetRadio(), "internet radio");

    install_app_or_delete(*phone, new NewApp(480, 800), "new app");

    uint16_t free_sram_size_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    uint16_t total_sram_size_kb = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;
    uint16_t free_psram_size_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    uint16_t total_psram_size_kb = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
    ESP_LOGI(TAG, "Free sram size: %d KB, total sram size: %d KB, "
                         "free psram size: %d KB, total psram size: %d KB",
                         free_sram_size_kb, total_sram_size_kb, free_psram_size_kb, total_psram_size_kb);

    if (sdcard_available_on_boot) {
        install_storage_apps_locked(nullptr);
    }

    device_security::init(phone);

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
    device_security::promptBootUnlockIfNeeded();
    schedule_pending_release_notes_popup();
    xTaskCreatePinnedToCore(sdcard_monitor_task, "sdcard_monitor", 4096, nullptr, 1, nullptr, 0);
}