#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <cstring>
#include <dirent.h>
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

struct SdcardRuntimeApps {
    ESP_Brookesia_Phone *phone = nullptr;
    FileManager *file_manager = nullptr;
    MusicPlayer *music_player = nullptr;
};

static SemaphoreHandle_t s_sdcardMutex = nullptr;
static bool s_sdcardMounted = false;
static SdcardRuntimeApps s_sdcardRuntimeApps;

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

    install_app_or_delete(*phone, new InternetRadio(), "internet radio");

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
    device_security::promptBootUnlockIfNeeded();
    xTaskCreatePinnedToCore(sdcard_monitor_task, "sdcard_monitor", 4096, nullptr, 1, nullptr, 0);
}