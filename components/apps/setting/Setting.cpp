/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cmath>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <sstream>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_memory_utils.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "cJSON.h"
#include "nvs.h"

#include "ui/ui.h"
#include "Setting.hpp"
#include "app_sntp.h"

#include "esp_brookesia_versions.h"

#define ENABLE_DEBUG_LOG                (0)

#define HOME_REFRESH_TASK_STACK_SIZE    (1024 * 4)
#define HOME_REFRESH_TASK_PRIORITY      (1)
#define HOME_REFRESH_TASK_PERIOD_MS     (2000)

#define WIFI_SCAN_TASK_STACK_SIZE       (1024 * 6)
#define WIFI_SCAN_TASK_PRIORITY         (1)
#define WIFI_SCAN_TASK_PERIOD_MS        (5 * 1000)

#define WIFI_CONNECT_TASK_STACK_SIZE    (1024 * 4)
#define WIFI_CONNECT_TASK_PRIORITY      (4)
#define WIFI_CONNECT_TASK_STACK_CORE    (0)
#define WIFI_CONNECT_UI_WAIT_TIME_MS    (1 * 1000)
#define WIFI_CONNECT_UI_PANEL_SIZE      (1 * 1000)
#define WIFI_CONNECT_RET_WAIT_TIME_MS   (10 * 1000)
#define WIFI_RECONNECT_RETRY_PERIOD_MS  (5 * 1000)

#define SCREEN_BRIGHTNESS_MIN           (20)
#define SCREEN_BRIGHTNESS_MAX           (BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX)

#define SPEAKER_VOLUME_MIN              (0)
#define SPEAKER_VOLUME_MAX              (100)

#define NVS_STORAGE_NAMESPACE           "storage"
#define NVS_KEY_WIFI_ENABLE             "wifi_en"
#define NVS_KEY_WIFI_SSID               "wifi_ssid"
#define NVS_KEY_WIFI_PASSWORD           "wifi_pass"
#define NVS_KEY_BLE_ENABLE              "ble_en"
#define NVS_KEY_AUDIO_VOLUME            "volume"
#define NVS_KEY_DISPLAY_BRIGHTNESS      "brightness"
#define NVS_KEY_DISPLAY_ADAPTIVE        "disp_adapt"
#define NVS_KEY_DISPLAY_SCREENSAVER     "disp_saver"
#define NVS_KEY_DISPLAY_TIMEOFF         "disp_off_sec"
#define NVS_KEY_DISPLAY_SLEEP           "disp_sleep"
#define NVS_KEY_DISPLAY_TIMEZONE        "disp_tz_min"
#define NVS_KEY_DISPLAY_TZ_AUTO         "disp_tz_auto"

#define UI_MAIN_ITEM_LEFT_OFFSET        (20)
#define UI_WIFI_LIST_UP_OFFSET          (20)
#define UI_WIFI_LIST_UP_PAD             (20)
#define UI_WIFI_LIST_DOWN_PAD           (20)
#define UI_WIFI_LIST_H_PERCENT          (75)
#define UI_WIFI_LIST_ITEM_H             (60)
#define UI_WIFI_LIST_ITEM_FONT          (&lv_font_montserrat_26)
#define UI_WIFI_KEYBOARD_H_PERCENT      (30)
#define UI_WIFI_ICON_LOCK_RIGHT_OFFSET       (-10)
#define UI_WIFI_ICON_SIGNAL_RIGHT_OFFSET     (-50)
#define UI_WIFI_ICON_CONNECT_RIGHT_OFFSET    (-90)

#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_12
 
#define EXAMPLE_ADC2_CHAN0          ADC_CHANNEL_4
static int adc_raw[2][10];
static int voltage[2][10];

using namespace std;

#define SCAN_LIST_SIZE      25

static const char TAG[] = "EUI_Setting";
static constexpr uint32_t kSettingScreenAnimTimeMs = 220;

TaskHandle_t wifi_scan_handle_task;

static EventGroupHandle_t s_wifi_event_group;

static char st_wifi_ssid[32];
static char st_wifi_password[64];

static uint8_t base_mac_addr[6] = {0};
static char mac_str[18] = {0};

static lv_obj_t* panel_wifi_btn[SCAN_LIST_SIZE];
static lv_obj_t* label_wifi_ssid[SCAN_LIST_SIZE];
static lv_obj_t* img_img_wifi_lock[SCAN_LIST_SIZE];
static lv_obj_t* wifi_image[SCAN_LIST_SIZE];
static lv_obj_t* wifi_connect[SCAN_LIST_SIZE];

static int brightness;

static constexpr int32_t kDisplayTimeoffOptionsSec[] = {0, 15, 30, 60, 120, 300};
static constexpr char kDisplayTimeoffOptionsText[] = "Off\n15 sec\n30 sec\n1 min\n2 min\n5 min";
static constexpr int32_t kDisplaySleepOptionsSec[] = {0, 30, 60, 120, 300, 600, 1800};
static constexpr char kDisplaySleepOptionsText[] = "Off\n30 sec\n1 min\n2 min\n5 min\n10 min\n30 min";
static constexpr const char *kFirmwareGithubReleasesUrl = "https://api.github.com/repos/elik745i/JC4880P443C_I_W_Remote/releases";
static constexpr const char *kFirmwareSdDirectory = "/sdcard/firmware";
static constexpr const char *kFirmwareUnknownVersion = "unknown";

struct TimezoneOption {
    int32_t offset_minutes;
    const char *label;
    const char *tz;
};

static constexpr TimezoneOption kTimezoneOptions[] = {
    {-720, "GMT-12:00", "UTC+12"},
    {-660, "GMT-11:00", "UTC+11"},
    {-600, "GMT-10:00", "UTC+10"},
    {-570, "GMT-09:30", "UTC+9:30"},
    {-540, "GMT-09:00", "UTC+9"},
    {-480, "GMT-08:00", "UTC+8"},
    {-420, "GMT-07:00", "UTC+7"},
    {-360, "GMT-06:00", "UTC+6"},
    {-300, "GMT-05:00", "UTC+5"},
    {-240, "GMT-04:00", "UTC+4"},
    {-210, "GMT-03:30", "UTC+3:30"},
    {-180, "GMT-03:00", "UTC+3"},
    {-120, "GMT-02:00", "UTC+2"},
    {-60, "GMT-01:00", "UTC+1"},
    {0, "GMT+00:00", "UTC0"},
    {60, "GMT+01:00", "UTC-1"},
    {120, "GMT+02:00", "UTC-2"},
    {180, "GMT+03:00", "UTC-3"},
    {210, "GMT+03:30", "UTC-3:30"},
    {240, "GMT+04:00", "UTC-4"},
    {270, "GMT+04:30", "UTC-4:30"},
    {300, "GMT+05:00", "UTC-5"},
    {330, "GMT+05:30", "UTC-5:30"},
    {345, "GMT+05:45", "UTC-5:45"},
    {360, "GMT+06:00", "UTC-6"},
    {390, "GMT+06:30", "UTC-6:30"},
    {420, "GMT+07:00", "UTC-7"},
    {480, "GMT+08:00", "UTC-8"},
    {525, "GMT+08:45", "UTC-8:45"},
    {540, "GMT+09:00", "UTC-9"},
    {570, "GMT+09:30", "UTC-9:30"},
    {600, "GMT+10:00", "UTC-10"},
    {630, "GMT+10:30", "UTC-10:30"},
    {660, "GMT+11:00", "UTC-11"},
    {720, "GMT+12:00", "UTC-12"},
    {765, "GMT+12:45", "UTC-12:45"},
    {780, "GMT+13:00", "UTC-13"},
    {840, "GMT+14:00", "UTC-14"},
};

static const char *kTimezoneOptionsText =
    "GMT-12:00\nGMT-11:00\nGMT-10:00\nGMT-09:30\nGMT-09:00\nGMT-08:00\nGMT-07:00\nGMT-06:00\nGMT-05:00\nGMT-04:00\nGMT-03:30\nGMT-03:00\nGMT-02:00\nGMT-01:00\nGMT+00:00\nGMT+01:00\nGMT+02:00\nGMT+03:00\nGMT+03:30\nGMT+04:00\nGMT+04:30\nGMT+05:00\nGMT+05:30\nGMT+05:45\nGMT+06:00\nGMT+06:30\nGMT+07:00\nGMT+08:00\nGMT+08:45\nGMT+09:00\nGMT+09:30\nGMT+10:00\nGMT+10:30\nGMT+11:00\nGMT+12:00\nGMT+12:45\nGMT+13:00\nGMT+14:00";

static constexpr const char *kTimezoneLookupUrl = "https://ipapi.co/json/";

static size_t kTimezoneOptionCount = sizeof(kTimezoneOptions) / sizeof(kTimezoneOptions[0]);

static uint16_t findDropdownIndexForValue(const int32_t *values, size_t value_count, int32_t value)
{
    for (size_t index = 0; index < value_count; ++index) {
        if (values[index] == value) {
            return static_cast<uint16_t>(index);
        }
    }

    return 0;
}

static int32_t getDropdownValueForIndex(const int32_t *values, size_t value_count, uint16_t index)
{
    if (index >= value_count) {
        return values[0];
    }

    return values[index];
}

static uint16_t findTimezoneDropdownIndexForOffset(int32_t offset_minutes)
{
    for (size_t index = 0; index < kTimezoneOptionCount; ++index) {
        if (kTimezoneOptions[index].offset_minutes == offset_minutes) {
            return static_cast<uint16_t>(index);
        }
    }

    return 0;
}

static const TimezoneOption &getTimezoneOptionForIndex(uint16_t index)
{
    if (index >= kTimezoneOptionCount) {
        return kTimezoneOptions[findTimezoneDropdownIndexForOffset(480)];
    }

    return kTimezoneOptions[index];
}

static const TimezoneOption &getTimezoneOptionForOffset(int32_t offset_minutes)
{
    return getTimezoneOptionForIndex(findTimezoneDropdownIndexForOffset(offset_minutes));
}

static bool parseUtcOffsetMinutes(const std::string &offset_text, int32_t &minutes)
{
    if (offset_text.size() < 3) {
        return false;
    }

    const char sign = offset_text[0];
    if ((sign != '+') && (sign != '-')) {
        return false;
    }

    std::string digits;
    for (size_t index = 1; index < offset_text.size(); ++index) {
        const char ch = offset_text[index];
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            digits.push_back(ch);
        }
    }

    if ((digits.size() != 2) && (digits.size() != 4)) {
        return false;
    }

    const int hours = std::stoi(digits.substr(0, 2));
    const int mins = (digits.size() == 4) ? std::stoi(digits.substr(2, 2)) : 0;
    minutes = (hours * 60) + mins;
    if (sign == '-') {
        minutes = -minutes;
    }

    return true;
}

static std::string trim_copy(const std::string &text)
{
    size_t start = 0;
    while ((start < text.size()) && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }

    size_t end = text.size();
    while ((end > start) && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(start, end - start);
}

static std::string lowercase_copy(const std::string &value)
{
    std::string copy = value;
    std::transform(copy.begin(), copy.end(), copy.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return copy;
}

static bool ends_with_bin(const std::string &path)
{
    const std::string lower = lowercase_copy(path);
    return (lower.size() >= 4) && (lower.substr(lower.size() - 4) == ".bin");
}

static std::string basename_from_path(const std::string &path)
{
    const size_t separator = path.find_last_of("/\\");
    return (separator == std::string::npos) ? path : path.substr(separator + 1);
}

static std::string safe_json_string(cJSON *object, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || (item->valuestring == nullptr)) {
        return {};
    }

    return item->valuestring;
}

LV_IMG_DECLARE(img_wifisignal_absent);
LV_IMG_DECLARE(img_wifisignal_wake);
LV_IMG_DECLARE(img_wifisignal_moderate);
LV_IMG_DECLARE(img_wifisignal_good);
LV_IMG_DECLARE(img_wifi_lock);
LV_IMG_DECLARE(img_wifi_connect_success);
LV_IMG_DECLARE(img_wifi_connect_fail);

typedef enum {
    WIFI_EVENT_CONNECTED = BIT(0),
    WIFI_EVENT_INIT_DONE = BIT(1),
    WIFI_EVENT_UI_INIT_DONE = BIT(2),
    WIFI_EVENT_SCANING = BIT(3),
    WIFI_EVENT_CONNECTING = BIT(4)
} wifi_event_id_t;

LV_IMG_DECLARE(img_app_setting);
extern lv_obj_t *ui_Min;
extern lv_obj_t *ui_Hour;
extern lv_obj_t *ui_Sec;
extern lv_obj_t *ui_Date;
extern lv_obj_t *ui_Clock_Number;

AppSettings::AppSettings():
    ESP_Brookesia_PhoneApp("Settings", &img_app_setting, false),                  // auto_resize_visual_area
    _is_ui_resumed(false),
    _is_ui_del(true),
    _screen_index(UI_MAIN_SETTING_INDEX),
    _wifi_signal_strength_level(WIFI_SIGNAL_STRENGTH_NONE),
    _savedWifiPanel(nullptr),
    _savedWifiValueLabel(nullptr),
    _savedWifiForgetButton(nullptr),
    _wifiPasswordToggleButton(nullptr),
    _wifiPasswordToggleLabel(nullptr),
    _displayAdaptiveBrightnessSwitch(nullptr),
    _displayScreensaverSwitch(nullptr),
    _displayTimeoffDropdown(nullptr),
    _displaySleepDropdown(nullptr),
    _displayAutoTimezoneSwitch(nullptr),
    _displayTimezoneDropdown(nullptr),
    _displayTimezoneInfoLabel(nullptr),
    _securitySettingsLockSwitch(nullptr),
    _securityInfoLabel(nullptr),
    _firmwareMenuItem(nullptr),
    _firmwareScreen(nullptr),
    _firmwareSdDropdown(nullptr),
    _firmwareSdFlashButton(nullptr),
    _firmwareOtaDropdown(nullptr),
    _firmwareOtaFlashButton(nullptr),
    _firmwareStatusLabel(nullptr),
    _isWifiPasswordVisible(false),
    _deviceLockToggleContext{this, device_security::LockType::Device},
    _settingsLockToggleContext{this, device_security::LockType::Settings},
    _screen_list({nullptr}),
    _autoTimezoneRefreshPending(false),
    _hasAutoDetectedTimezone(false),
    _autoDetectedTimezoneOffsetMinutes(480),
    _autoTimezoneStatus()
{
}

AppSettings::~AppSettings()
{
}

void AppSettings::initializeDefaultNvsParams(void)
{
    _nvs_param_map[NVS_KEY_WIFI_ENABLE] = false;
    _nvs_param_map[NVS_KEY_BLE_ENABLE] = false;
    _nvs_param_map[NVS_KEY_AUDIO_VOLUME] = bsp_extra_codec_volume_get();
    _nvs_param_map[NVS_KEY_AUDIO_VOLUME] = max(min((int)_nvs_param_map[NVS_KEY_AUDIO_VOLUME], SPEAKER_VOLUME_MAX), SPEAKER_VOLUME_MIN);
    _nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS] = brightness;
    _nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS] = max(min((int)_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS], SCREEN_BRIGHTNESS_MAX), SCREEN_BRIGHTNESS_MIN);
    _nvs_param_map[NVS_KEY_DISPLAY_ADAPTIVE] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_SCREENSAVER] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_SLEEP] = 0;
    _nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE] = 480;
    _nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO] = 0;
}

bool AppSettings::run(void)
{
    _is_ui_del = false;

    // Initialize Squareline UI
    ui_setting_init();

    // Get MAC
    esp_read_mac(base_mac_addr, ESP_MAC_EFUSE_FACTORY);
    snprintf(mac_str, sizeof(mac_str), "%02X-%02X-%02X-%02X-%02X-%02X",
             base_mac_addr[0], base_mac_addr[1], base_mac_addr[2],
             base_mac_addr[3], base_mac_addr[4], base_mac_addr[5]);


    // Initialize custom UI
    extraUiInit();

    // Upate UI by NVS parametres
    updateUiByNvsParam();

    xEventGroupSetBits(s_wifi_event_group, WIFI_EVENT_UI_INIT_DONE);

    return true;
}

bool AppSettings::back(void)
{
    _is_ui_resumed = false;

    if (_screen_index == UI_WIFI_CONNECT_INDEX) {
        lv_scr_load_anim(ui_ScreenSettingWiFi, LV_SCR_LOAD_ANIM_MOVE_RIGHT, kSettingScreenAnimTimeMs, 0, false);
    } else if (_screen_index != UI_MAIN_SETTING_INDEX) {
        lv_scr_load_anim(ui_ScreenSettingMain, LV_SCR_LOAD_ANIM_MOVE_RIGHT, kSettingScreenAnimTimeMs, 0, false);
    } else {
        while(xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_SCANING) {
            ESP_LOGI(TAG, "WiFi is scanning, please wait");
            vTaskDelay(pdMS_TO_TICKS(100));
            stopWifiScan();
        } 
        notifyCoreClosed();
    }

    return true;
}

bool AppSettings::close(void)
{
    while(xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_SCANING) {
        ESP_LOGI(TAG, "WiFi is scanning, please wait");
        vTaskDelay(pdMS_TO_TICKS(100));
        stopWifiScan();
    } 
    
    _is_ui_del = true;
    
    return true;
}

bool AppSettings::init(void)
{
    ESP_Brookesia_Phone *phone = getPhone();
    ESP_Brookesia_PhoneHome& home = phone->getHome();
    status_bar = home.getStatusBar();
    backstage = home.getRecentsScreen();

    adc_battery_estimation_t config = {
        .internal = {
            .adc_unit = ADC_UNIT_2,
            .adc_bitwidth = ADC_BITWIDTH_DEFAULT,
            .adc_atten = EXAMPLE_ADC_ATTEN,
        },
        .adc_channel = EXAMPLE_ADC2_CHAN0,
        .upper_resistor = 68000,
        .lower_resistor = 100000,
        .battery_points = default_battery_points,
        .battery_points_count = DEFAULT_POINTS_COUNT,
        .charging_detect_cb = NULL,
        .charging_detect_user_data = NULL,
    };

    adc_battery_estimation_handle = adc_battery_estimation_create(&config);

    // Initialize NVS parameters
    initializeDefaultNvsParams();
    // Load NVS parameters if exist
    loadNvsParam();
    applyManualTimezonePreference();
    // Update System parameters
    bsp_extra_codec_volume_set(_nvs_param_map[NVS_KEY_AUDIO_VOLUME], (int *)&_nvs_param_map[NVS_KEY_AUDIO_VOLUME]);
    bsp_display_brightness_set(_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS]);
    ESP_ERROR_CHECK(bsp_extra_display_idle_init());
    applyDisplayIdleSettings();

    xTaskCreatePinnedToCore(euiRefresTask, "Home Refresh", HOME_REFRESH_TASK_STACK_SIZE, this, HOME_REFRESH_TASK_PRIORITY, NULL,1);
    xTaskCreatePinnedToCore(euiBatteryTask, "Battey Refresh", HOME_REFRESH_TASK_STACK_SIZE, this, HOME_REFRESH_TASK_PRIORITY, NULL,1);
    xTaskCreatePinnedToCore(wifiScanTask, "WiFi Scan", WIFI_SCAN_TASK_STACK_SIZE, this, WIFI_SCAN_TASK_PRIORITY, NULL,1);

    return true;
}

bool AppSettings::pause(void)
{
    _is_ui_resumed = true;

    return true;
}

bool AppSettings::resume(void)
{
    _is_ui_resumed = false;

    return true;
}

void AppSettings::extraUiInit(void)
{
    /* Main */
    lv_label_set_text(ui_LabelPanelSettingMainContainer3Volume, "Audio");
    lv_label_set_text(ui_LabelPanelSettingMainContainer4Light, "Display");
    lv_label_set_text(ui_LabelPanelSettingMainContainer2Blue, "Security");
    lv_obj_align_to(ui_LabelPanelSettingMainContainer1WiFi, ui_ImagePanelSettingMainContainer1WiFi, LV_ALIGN_OUT_RIGHT_MID,
                    UI_MAIN_ITEM_LEFT_OFFSET, 0);
    lv_obj_align_to(ui_LabelPanelSettingMainContainer2Blue, ui_ImagePanelSettingMainContainer2Blue, LV_ALIGN_OUT_RIGHT_MID,
                    UI_MAIN_ITEM_LEFT_OFFSET, 0);
    lv_obj_align_to(ui_LabelPanelSettingMainContainer3Volume, ui_ImagePanelSettingMainContainer3Volume, LV_ALIGN_OUT_RIGHT_MID,
                    UI_MAIN_ITEM_LEFT_OFFSET, 0);
    lv_obj_align_to(ui_LabelPanelSettingMainContainer4Light, ui_ImagePanelSettingMainContainer4Light, LV_ALIGN_OUT_RIGHT_MID,
                    UI_MAIN_ITEM_LEFT_OFFSET, 0);
    lv_obj_align_to(ui_LabelPanelSettingMainContainer5About, ui_ImagePanelSettingMainContainer5About, LV_ALIGN_OUT_RIGHT_MID,
                    UI_MAIN_ITEM_LEFT_OFFSET, 0);

    _firmwareMenuItem = lv_obj_create(ui_PanelSettingMainContainer);
    lv_obj_set_size(_firmwareMenuItem, lv_pct(100), 70);
    lv_obj_clear_flag(_firmwareMenuItem, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_firmwareMenuItem, 0, 0);
    lv_obj_set_style_border_width(_firmwareMenuItem, 0, 0);
    lv_obj_set_style_bg_color(_firmwareMenuItem, lv_color_hex(0xCBCBCB), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(_firmwareMenuItem, 255, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(_firmwareMenuItem, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(_firmwareMenuItem, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(_firmwareMenuItem, onFirmwareMenuClickedEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t *firmwareLabel = lv_label_create(_firmwareMenuItem);
    lv_label_set_text(firmwareLabel, "Firmware");
    lv_obj_set_style_text_font(firmwareLabel, &lv_font_montserrat_30, 0);
    lv_obj_align(firmwareLabel, LV_ALIGN_LEFT_MID, 40, 0);

    lv_obj_t *firmwareArrow = lv_img_create(_firmwareMenuItem);
    lv_img_set_src(firmwareArrow, &ui_img_arrow_png);
    lv_obj_align(firmwareArrow, LV_ALIGN_RIGHT_MID, -24, 0);

    _firmwareScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_firmwareScreen, lv_color_hex(0xE5F3FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_firmwareScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_firmwareScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *firmwareBackButton = lv_btn_create(_firmwareScreen);
    lv_obj_set_size(firmwareBackButton, 60, 60);
    lv_obj_align(firmwareBackButton, LV_ALIGN_TOP_LEFT, 18, 18);
    lv_obj_set_style_bg_color(firmwareBackButton, lv_color_hex(0xE5F3FF), 0);
    lv_obj_set_style_border_width(firmwareBackButton, 0, 0);
    lv_obj_add_event_cb(firmwareBackButton, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            lv_scr_load_anim(ui_ScreenSettingMain, LV_SCR_LOAD_ANIM_MOVE_RIGHT, kSettingScreenAnimTimeMs, 0, false);
        }
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *firmwareBackImage = lv_img_create(firmwareBackButton);
    lv_img_set_src(firmwareBackImage, &ui_img_return_png);
    lv_obj_center(firmwareBackImage);
    lv_obj_set_style_img_recolor(firmwareBackImage, lv_color_hex(0x000000), 0);
    lv_obj_set_style_img_recolor_opa(firmwareBackImage, 255, 0);

    lv_obj_t *firmwareTitle = lv_label_create(_firmwareScreen);
    lv_label_set_text(firmwareTitle, "Firmware");
    lv_obj_set_style_text_font(firmwareTitle, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(firmwareTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(firmwareTitle, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *firmwarePanel = lv_obj_create(_firmwareScreen);
    lv_obj_set_size(firmwarePanel, lv_pct(92), 650);
    lv_obj_align(firmwarePanel, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_style_radius(firmwarePanel, 20, 0);
    lv_obj_set_style_border_width(firmwarePanel, 0, 0);
    lv_obj_set_style_bg_color(firmwarePanel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_all(firmwarePanel, 14, 0);
    lv_obj_set_style_pad_row(firmwarePanel, 12, 0);
    lv_obj_set_flex_flow(firmwarePanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(firmwarePanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(firmwarePanel, LV_DIR_VER);

    auto createFirmwareSection = [](lv_obj_t *parent, const char *title) {
        lv_obj_t *section = lv_obj_create(parent);
        lv_obj_set_width(section, lv_pct(100));
        lv_obj_set_height(section, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(section, 18, 0);
        lv_obj_set_style_border_width(section, 0, 0);
        lv_obj_set_style_bg_color(section, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_pad_all(section, 14, 0);
        lv_obj_set_style_pad_row(section, 10, 0);
        lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t *sectionTitle = lv_label_create(section);
        lv_label_set_text(sectionTitle, title);
        lv_obj_set_style_text_font(sectionTitle, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(sectionTitle, lv_color_hex(0x0F172A), 0);
        return section;
    };

    auto createFirmwareControlsRow = [](lv_obj_t *parent) {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_row(row, 8, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        return row;
    };

    lv_obj_t *sdSection = createFirmwareSection(firmwarePanel, "Flash from SD Card");
    lv_obj_t *sdHint = lv_label_create(sdSection);
    lv_obj_set_width(sdHint, lv_pct(100));
    lv_label_set_long_mode(sdHint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(sdHint, "Select a validated .bin firmware image from /sdcard or /sdcard/firmware.");
    lv_obj_set_style_text_font(sdHint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sdHint, lv_color_hex(0x475569), 0);

    lv_obj_t *sdControlsRow = createFirmwareControlsRow(sdSection);

    _firmwareSdDropdown = lv_dropdown_create(sdControlsRow);
    lv_obj_set_size(_firmwareSdDropdown, 220, 48);
    lv_obj_set_flex_grow(_firmwareSdDropdown, 1);

    lv_obj_t *sdRefreshButton = lv_btn_create(sdControlsRow);
    lv_obj_set_size(sdRefreshButton, 92, 48);
    lv_obj_set_style_radius(sdRefreshButton, 16, 0);
    lv_obj_set_style_border_width(sdRefreshButton, 0, 0);
    lv_obj_set_style_bg_color(sdRefreshButton, lv_color_hex(0xCBD5E1), 0);
    lv_obj_add_event_cb(sdRefreshButton, onFirmwareSdRefreshClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *sdRefreshLabel = lv_label_create(sdRefreshButton);
    lv_label_set_text(sdRefreshLabel, "Scan");
    lv_obj_center(sdRefreshLabel);

    _firmwareSdFlashButton = lv_btn_create(sdControlsRow);
    lv_obj_set_size(_firmwareSdFlashButton, 92, 48);
    lv_obj_set_style_radius(_firmwareSdFlashButton, 16, 0);
    lv_obj_set_style_border_width(_firmwareSdFlashButton, 0, 0);
    lv_obj_add_event_cb(_firmwareSdFlashButton, onFirmwareSdFlashClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *sdFlashLabel = lv_label_create(_firmwareSdFlashButton);
    lv_label_set_text(sdFlashLabel, "Flash");
    lv_obj_center(sdFlashLabel);

    lv_obj_t *otaSection = createFirmwareSection(firmwarePanel, "Check GitHub Releases");
    lv_obj_t *otaHint = lv_label_create(otaSection);
    lv_obj_set_width(otaHint, lv_pct(100));
    lv_label_set_long_mode(otaHint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(otaHint, "Query GitHub releases, label current versus newer firmware, then select a release asset.");
    lv_obj_set_style_text_font(otaHint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(otaHint, lv_color_hex(0x475569), 0);

    lv_obj_t *otaControlsRow = createFirmwareControlsRow(otaSection);

    lv_obj_t *otaCheckButton = lv_btn_create(otaControlsRow);
    lv_obj_set_size(otaCheckButton, 96, 48);
    lv_obj_set_style_radius(otaCheckButton, 16, 0);
    lv_obj_set_style_border_width(otaCheckButton, 0, 0);
    lv_obj_add_event_cb(otaCheckButton, onFirmwareOtaCheckClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *otaCheckLabel = lv_label_create(otaCheckButton);
    lv_label_set_text(otaCheckLabel, "Check");
    lv_obj_center(otaCheckLabel);

    _firmwareOtaDropdown = lv_dropdown_create(otaControlsRow);
    lv_obj_set_size(_firmwareOtaDropdown, 190, 48);
    lv_obj_set_flex_grow(_firmwareOtaDropdown, 1);

    _firmwareOtaFlashButton = lv_btn_create(otaControlsRow);
    lv_obj_set_size(_firmwareOtaFlashButton, 92, 48);
    lv_obj_set_style_radius(_firmwareOtaFlashButton, 16, 0);
    lv_obj_set_style_border_width(_firmwareOtaFlashButton, 0, 0);
    lv_obj_add_event_cb(_firmwareOtaFlashButton, onFirmwareOtaFlashClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *otaFlashLabel = lv_label_create(_firmwareOtaFlashButton);
    lv_label_set_text(otaFlashLabel, "Flash");
    lv_obj_center(otaFlashLabel);

    _firmwareStatusLabel = lv_label_create(firmwarePanel);
    lv_obj_set_width(_firmwareStatusLabel, lv_pct(100));
    lv_label_set_long_mode(_firmwareStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_firmwareStatusLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_firmwareStatusLabel, lv_color_hex(0x334155), 0);

    lv_obj_t *dangerSection = createFirmwareSection(firmwarePanel, "Danger Zone");
    lv_obj_t *dangerHint = lv_label_create(dangerSection);
    lv_obj_set_width(dangerHint, lv_pct(100));
    lv_label_set_long_mode(dangerHint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(dangerHint, "Factory reset clears saved Settings preferences including Wi-Fi credentials, display, audio, and timezone options.");
    lv_obj_set_style_text_font(dangerHint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(dangerHint, lv_color_hex(0x7F1D1D), 0);

    lv_obj_t *dangerControlsRow = createFirmwareControlsRow(dangerSection);
    lv_obj_t *factoryResetButton = lv_btn_create(dangerControlsRow);
    lv_obj_set_size(factoryResetButton, 170, 50);
    lv_obj_set_style_radius(factoryResetButton, 16, 0);
    lv_obj_set_style_border_width(factoryResetButton, 0, 0);
    lv_obj_set_style_bg_color(factoryResetButton, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_bg_opa(factoryResetButton, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(factoryResetButton, lv_color_hex(0xB91C1C), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(factoryResetButton, onFirmwareFactoryResetClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *factoryResetLabel = lv_label_create(factoryResetButton);
    lv_label_set_text(factoryResetLabel, "Factory Reset");
    lv_obj_set_style_text_color(factoryResetLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(factoryResetLabel);

    // Record the screen index and install the screen loaded event callback
    _screen_list[UI_MAIN_SETTING_INDEX] = ui_ScreenSettingMain;
    lv_obj_add_event_cb(ui_ScreenSettingMain, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);

    /* WiFi */
    // Switch
    lv_obj_add_event_cb(ui_SwitchPanelScreenSettingWiFiSwitch, onSwitchPanelScreenSettingWiFiSwitchValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);
    _savedWifiPanel = lv_obj_create(ui_ScreenSettingWiFi);
    lv_obj_set_size(_savedWifiPanel, lv_pct(90), 72);
    lv_obj_align_to(_savedWifiPanel, ui_PanelScreenSettingWiFiSwitch, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    lv_obj_clear_flag(_savedWifiPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_savedWifiPanel, 18, 0);
    lv_obj_set_style_border_width(_savedWifiPanel, 0, 0);
    lv_obj_set_style_bg_color(_savedWifiPanel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(_savedWifiPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(_savedWifiPanel, 18, 0);
    lv_obj_set_style_pad_right(_savedWifiPanel, 18, 0);
    lv_obj_set_style_pad_top(_savedWifiPanel, 10, 0);
    lv_obj_set_style_pad_bottom(_savedWifiPanel, 10, 0);

    lv_obj_t *savedWifiTitleLabel = lv_label_create(_savedWifiPanel);
    lv_label_set_text(savedWifiTitleLabel, "Saved Network");
    lv_obj_set_style_text_font(savedWifiTitleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(savedWifiTitleLabel, lv_color_hex(0x4A5568), 0);
    lv_obj_align(savedWifiTitleLabel, LV_ALIGN_TOP_LEFT, 0, 0);

    _savedWifiValueLabel = lv_label_create(_savedWifiPanel);
    lv_label_set_text(_savedWifiValueLabel, "None");
    lv_obj_set_width(_savedWifiValueLabel, 250);
    lv_label_set_long_mode(_savedWifiValueLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(_savedWifiValueLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_savedWifiValueLabel, lv_color_hex(0x111827), 0);
    lv_obj_align(_savedWifiValueLabel, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    _savedWifiForgetButton = lv_btn_create(_savedWifiPanel);
    lv_obj_set_size(_savedWifiForgetButton, 96, 40);
    lv_obj_align(_savedWifiForgetButton, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(_savedWifiForgetButton, 16, 0);
    lv_obj_set_style_bg_color(_savedWifiForgetButton, lv_color_hex(0xE53E3E), 0);
    lv_obj_set_style_bg_opa(_savedWifiForgetButton, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_savedWifiForgetButton, 0, 0);
    lv_obj_add_event_cb(_savedWifiForgetButton, onForgetSavedWifiClickedEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t *forgetLabel = lv_label_create(_savedWifiForgetButton);
    lv_label_set_text(forgetLabel, "Forget");
    lv_obj_set_style_text_color(forgetLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(forgetLabel);
    // List
    // lv_obj_clear_flag(ui_PanelScreenSettingWiFiList, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui_PanelScreenSettingWiFiList, LV_DIR_VER);
    lv_obj_set_height(ui_PanelScreenSettingWiFiList, lv_pct(UI_WIFI_LIST_H_PERCENT));
    lv_obj_align_to(ui_PanelScreenSettingWiFiList, _savedWifiPanel, LV_ALIGN_OUT_BOTTOM_MID, 0,
                    UI_WIFI_LIST_UP_OFFSET);
    lv_obj_set_style_pad_all(ui_PanelScreenSettingWiFiList, 0, 0);
    lv_obj_set_style_pad_top(ui_PanelScreenSettingWiFiList, UI_WIFI_LIST_UP_PAD, 0);
    lv_obj_set_style_pad_bottom(ui_PanelScreenSettingWiFiList, UI_WIFI_LIST_DOWN_PAD, 0);
    for(int i = 0; i < SCAN_LIST_SIZE; i++) {
        panel_wifi_btn[i] = lv_obj_create(ui_PanelScreenSettingWiFiList);
        lv_obj_set_size(panel_wifi_btn[i], lv_pct(100), UI_WIFI_LIST_ITEM_H);
        lv_obj_set_style_radius(panel_wifi_btn[i], 0, 0);
        lv_obj_set_style_border_width(panel_wifi_btn[i], 0, 0);
        lv_obj_set_style_text_font(panel_wifi_btn[i], UI_WIFI_LIST_ITEM_FONT, 0);
        lv_obj_add_flag(panel_wifi_btn[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag( panel_wifi_btn[i], LV_OBJ_FLAG_SCROLLABLE );
        lv_obj_set_style_bg_color(panel_wifi_btn[i], lv_color_hex(0xCBCBCB), LV_PART_MAIN | LV_STATE_PRESSED );
        lv_obj_set_style_bg_opa(panel_wifi_btn[i], 255, LV_PART_MAIN| LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(panel_wifi_btn[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
        lv_obj_set_style_border_opa(panel_wifi_btn[i], 255, LV_PART_MAIN| LV_STATE_DEFAULT);

        label_wifi_ssid[i] = lv_label_create(panel_wifi_btn[i]);
        lv_obj_set_align(label_wifi_ssid[i], LV_ALIGN_LEFT_MID);

        img_img_wifi_lock[i] = lv_img_create(panel_wifi_btn[i]);
        lv_obj_align(img_img_wifi_lock[i], LV_ALIGN_RIGHT_MID, UI_WIFI_ICON_LOCK_RIGHT_OFFSET, 0);
        lv_obj_add_flag(img_img_wifi_lock[i], LV_OBJ_FLAG_HIDDEN);

        wifi_image[i] = lv_img_create(panel_wifi_btn[i]);
        lv_obj_align(wifi_image[i], LV_ALIGN_RIGHT_MID, UI_WIFI_ICON_SIGNAL_RIGHT_OFFSET, 0);

        wifi_connect[i] = lv_label_create(panel_wifi_btn[i]);
        lv_label_set_text(wifi_connect[i], LV_SYMBOL_OK);
        lv_obj_align(wifi_connect[i], LV_ALIGN_RIGHT_MID, UI_WIFI_ICON_CONNECT_RIGHT_OFFSET, 0);
        lv_obj_add_flag(wifi_connect[i], LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_event_cb(panel_wifi_btn[i], onButtonWifiListClickedEventCallback, LV_EVENT_CLICKED, this);
        if(!(xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_SCANING)) {
            lv_obj_add_flag(ui_PanelScreenSettingWiFiList, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_SpinnerScreenSettingWiFi, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_add_flag(ui_ButtonScreenSettingWiFiReturn, LV_OBJ_FLAG_HIDDEN);
    // Connect
    lv_obj_add_flag(ui_SpinnerScreenSettingVerification, LV_OBJ_FLAG_HIDDEN);
    _panel_wifi_connect = lv_obj_create(ui_ScreenSettingVerification);
    lv_obj_set_size(_panel_wifi_connect, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(_panel_wifi_connect, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_panel_wifi_connect, LV_OPA_50, 0);
    lv_obj_center(_panel_wifi_connect);
    _img_wifi_connect = lv_img_create(_panel_wifi_connect);
    lv_obj_center(_img_wifi_connect);
    _spinner_wifi_connect = lv_spinner_create(_panel_wifi_connect, 1000, 600);
    lv_obj_set_size(_spinner_wifi_connect, lv_pct(20), lv_pct(20));
    lv_obj_center(_spinner_wifi_connect);
    processWifiConnect(WIFI_CONNECT_HIDE);
    // Keyboard
    lv_obj_set_width(ui_TextAreaScreenSettingVerificationPassword, 328);
    lv_obj_set_x(ui_TextAreaScreenSettingVerificationPassword, -40);
    lv_textarea_set_one_line(ui_TextAreaScreenSettingVerificationPassword, true);
    lv_textarea_set_password_mode(ui_TextAreaScreenSettingVerificationPassword, true);
    lv_obj_add_event_cb(ui_TextAreaScreenSettingVerificationPassword, onWifiPasswordFieldEventCallback, LV_EVENT_ALL, this);

    _wifiPasswordToggleButton = lv_btn_create(ui_ScreenSettingVerification);
    lv_obj_set_size(_wifiPasswordToggleButton, 56, 56);
    lv_obj_align_to(_wifiPasswordToggleButton, ui_TextAreaScreenSettingVerificationPassword, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    lv_obj_set_style_radius(_wifiPasswordToggleButton, 18, 0);
    lv_obj_set_style_bg_color(_wifiPasswordToggleButton, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(_wifiPasswordToggleButton, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_wifiPasswordToggleButton, lv_color_hex(0xC6D4E1), 0);
    lv_obj_set_style_border_width(_wifiPasswordToggleButton, 1, 0);
    lv_obj_set_style_shadow_width(_wifiPasswordToggleButton, 0, 0);
    lv_obj_add_event_cb(_wifiPasswordToggleButton, onWifiPasswordToggleClickedEventCallback, LV_EVENT_CLICKED, this);

    _wifiPasswordToggleLabel = lv_label_create(_wifiPasswordToggleButton);
    lv_obj_set_style_text_font(_wifiPasswordToggleLabel, &lv_font_montserrat_24, 0);
    lv_obj_center(_wifiPasswordToggleLabel);
    updateWifiPasswordVisibility(false);

    lv_obj_set_size(ui_KeyboardScreenSettingVerification, lv_pct(100), lv_pct(34));
    lv_obj_align(ui_KeyboardScreenSettingVerification, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(ui_KeyboardScreenSettingVerification, ui_TextAreaScreenSettingVerificationPassword);
    lv_keyboard_set_popovers(ui_KeyboardScreenSettingVerification, true);
    lv_obj_add_flag(ui_KeyboardScreenSettingVerification, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ui_KeyboardScreenSettingVerification, onKeyboardScreenSettingVerificationClickedEventCallback,
                        LV_EVENT_ALL, this);
    // Record the screen index and install the screen loaded event callback
    lv_obj_add_flag(ui_ButtonScreenSettingBLEReturn, LV_OBJ_FLAG_HIDDEN);
    _screen_list[UI_WIFI_SCAN_INDEX] = ui_ScreenSettingWiFi;
    lv_obj_add_event_cb(ui_ScreenSettingWiFi, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
    _screen_list[UI_WIFI_CONNECT_INDEX] = ui_ScreenSettingVerification;
    lv_obj_add_event_cb(ui_ScreenSettingVerification, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);

    /* Bluetooth */
    lv_obj_clear_flag(ui_PanelSettingMainContainerItem2, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui_LabelPanelScreenSettingBLESwitch, "Device Lock");
    lv_obj_add_flag(ui_ImagePanelScreenSettingBLESwitch, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(ui_LabelPanelScreenSettingBLESwitch, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(ui_PanelScreenSettingBLEList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_SpinnerScreenSettingBLE, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align_to(ui_PanelScreenSettingBLEList, ui_PanelScreenSettingBLESwitch, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    lv_obj_set_size(ui_PanelScreenSettingBLEList, lv_pct(90), 220);
    lv_obj_set_style_pad_all(ui_PanelScreenSettingBLEList, 0, 0);
    lv_obj_set_style_pad_row(ui_PanelScreenSettingBLEList, 12, 0);
    lv_obj_set_style_bg_opa(ui_PanelScreenSettingBLEList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui_PanelScreenSettingBLEList, 0, 0);
    lv_obj_set_scroll_dir(ui_PanelScreenSettingBLEList, LV_DIR_VER);

    auto createSecuritySettingRow = [](lv_obj_t *parent, const char *title) {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_set_size(row, lv_pct(100), 72);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(row, 18, 0);
        lv_obj_set_style_pad_right(row, 18, 0);
        lv_obj_set_style_pad_top(row, 10, 0);
        lv_obj_set_style_pad_bottom(row, 10, 0);

        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x111827), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        return row;
    };

    lv_obj_t *settingsLockRow = createSecuritySettingRow(ui_PanelScreenSettingBLEList, "Settings Lock");
    _securitySettingsLockSwitch = lv_switch_create(settingsLockRow);
    lv_obj_align(_securitySettingsLockSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_securitySettingsLockSwitch, onSwitchPanelScreenSettingSettingsLockValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    _securityInfoLabel = lv_label_create(ui_ScreenSettingBLE);
    lv_obj_set_width(_securityInfoLabel, 360);
    lv_label_set_long_mode(_securityInfoLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_securityInfoLabel,
                      "Enabling a lock asks for a new 4-digit PIN. Disabling it asks for the existing PIN.");
    lv_obj_set_style_text_font(_securityInfoLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_securityInfoLabel, lv_color_hex(0x475569), 0);
    lv_obj_align_to(_securityInfoLabel, ui_PanelScreenSettingBLEList, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);

    lv_obj_add_event_cb(ui_SwitchPanelScreenSettingBLESwitch, onSwitchPanelScreenSettingBLESwitchValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);
    // Record the screen index and install the screen loaded event callback
    _screen_list[UI_BLUETOOTH_SETTING_INDEX] = ui_ScreenSettingBLE;
    lv_obj_add_event_cb(ui_ScreenSettingBLE, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);

    /* Display */
    lv_slider_set_range(ui_SliderPanelScreenSettingLightSwitch1, SCREEN_BRIGHTNESS_MIN, SCREEN_BRIGHTNESS_MAX);
    lv_obj_add_event_cb(ui_SliderPanelScreenSettingLightSwitch1, onSliderPanelLightSwitchValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);
    lv_obj_set_height(ui_PanelScreenSettingLightSwitch, 84);
    lv_obj_set_width(ui_PanelScreenSettingLightSwitch, lv_pct(90));
    lv_obj_set_x(ui_PanelScreenSettingLightSwitch, 0);
    lv_obj_align(ui_PanelScreenSettingLightSwitch, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_x(ui_ImagePanelScreenSettingLightSwitch, 16);
    lv_obj_set_width(ui_LabelPanelScreenSettingLightSwitch, 126);
    lv_obj_set_style_text_font(ui_LabelPanelScreenSettingLightSwitch, &lv_font_montserrat_20, 0);
    lv_obj_align_to(ui_LabelPanelScreenSettingLightSwitch, ui_ImagePanelScreenSettingLightSwitch, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    lv_obj_set_width(ui_SliderPanelScreenSettingLightSwitch1, 168);
    lv_obj_align(ui_SliderPanelScreenSettingLightSwitch1, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_clear_flag(ui_PanelScreenSettingLightList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align_to(ui_PanelScreenSettingLightList, ui_PanelScreenSettingLightSwitch, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    lv_obj_set_size(ui_PanelScreenSettingLightList, lv_pct(90), 340);
    lv_obj_set_style_pad_all(ui_PanelScreenSettingLightList, 0, 0);
    lv_obj_set_style_pad_row(ui_PanelScreenSettingLightList, 12, 0);
    lv_obj_set_style_bg_opa(ui_PanelScreenSettingLightList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui_PanelScreenSettingLightList, 0, 0);
    lv_obj_set_scroll_dir(ui_PanelScreenSettingLightList, LV_DIR_VER);

    auto createDisplaySettingRow = [](lv_obj_t *parent, const char *title) {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_set_size(row, lv_pct(100), 72);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(row, 18, 0);
        lv_obj_set_style_pad_right(row, 18, 0);
        lv_obj_set_style_pad_top(row, 10, 0);
        lv_obj_set_style_pad_bottom(row, 10, 0);

        lv_obj_t *label = lv_label_create(row);
    lv_obj_set_width(label, 180);
        lv_label_set_text(label, title);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x111827), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        return row;
    };

    lv_obj_t *adaptiveRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Adaptive Brightness");
    _displayAdaptiveBrightnessSwitch = lv_switch_create(adaptiveRow);
    lv_obj_align(_displayAdaptiveBrightnessSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displayAdaptiveBrightnessSwitch, onSwitchPanelScreenSettingAdaptiveBrightnessValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *screensaverRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Screensaver");
    _displayScreensaverSwitch = lv_switch_create(screensaverRow);
    lv_obj_align(_displayScreensaverSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displayScreensaverSwitch, onSwitchPanelScreenSettingScreensaverValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *timeoffRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Timeoff Interval");
    _displayTimeoffDropdown = lv_dropdown_create(timeoffRow);
    lv_dropdown_set_options_static(_displayTimeoffDropdown, kDisplayTimeoffOptionsText);
    lv_obj_set_width(_displayTimeoffDropdown, 132);
    lv_obj_align(_displayTimeoffDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displayTimeoffDropdown, onDropdownPanelScreenSettingTimeoffIntervalValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *sleepRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Sleep Interval");
    _displaySleepDropdown = lv_dropdown_create(sleepRow);
    lv_dropdown_set_options_static(_displaySleepDropdown, kDisplaySleepOptionsText);
    lv_obj_set_width(_displaySleepDropdown, 132);
    lv_obj_align(_displaySleepDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displaySleepDropdown, onDropdownPanelScreenSettingSleepIntervalValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *timezoneRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Timezone");
    _displayTimezoneDropdown = lv_dropdown_create(timezoneRow);
    lv_dropdown_set_options_static(_displayTimezoneDropdown, kTimezoneOptionsText);
    lv_obj_set_width(_displayTimezoneDropdown, 156);
    lv_obj_align(_displayTimezoneDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displayTimezoneDropdown, onDropdownPanelScreenSettingTimezoneValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *autoTimezoneRow = createDisplaySettingRow(ui_PanelScreenSettingLightList, "Auto Timezone");
    _displayAutoTimezoneSwitch = lv_switch_create(autoTimezoneRow);
    lv_obj_align(_displayAutoTimezoneSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_displayAutoTimezoneSwitch, onSwitchPanelScreenSettingAutoTimezoneValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    _displayTimezoneInfoLabel = lv_label_create(ui_PanelScreenSettingLightList);
    lv_obj_set_width(_displayTimezoneInfoLabel, lv_pct(100));
    lv_label_set_long_mode(_displayTimezoneInfoLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_displayTimezoneInfoLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_displayTimezoneInfoLabel, lv_color_hex(0x475569), 0);
    lv_obj_add_flag(ui_ButtonScreenSettingLightReturn, LV_OBJ_FLAG_HIDDEN);
    // Record the screen index and install the screen loaded event callback
    _screen_list[UI_BRIGHTNESS_SETTING_INDEX] = ui_ScreenSettingLight;
    lv_obj_add_event_cb(ui_ScreenSettingLight, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);

    /* Audio */
    lv_slider_set_range(ui_SliderPanelScreenSettingVolumeSwitch, SPEAKER_VOLUME_MIN, SPEAKER_VOLUME_MAX);
    lv_obj_add_event_cb(ui_SliderPanelScreenSettingVolumeSwitch, onSliderPanelVolumeSwitchValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_flag(ui_ButtonScreenSettingVolumeReturn, LV_OBJ_FLAG_HIDDEN);
    // Record the screen index and install the screen loaded event callback
    _screen_list[UI_VOLUME_SETTING_INDEX] = ui_ScreenSettingVolume;
    lv_obj_add_event_cb(ui_ScreenSettingVolume, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);

    /* About */
    lv_label_set_text(ui_LabelPanelPanelScreenSettingAbout4, "ESP_Brookesia");
    lv_obj_add_flag(ui_ButtonScreenSettingAboutReturn, LV_OBJ_FLAG_HIDDEN);

    auto styleAboutRow = [](lv_obj_t *row, lv_obj_t *title_label, lv_obj_t *value_label) {
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(row, 72, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(row, 16, 0);
        lv_obj_set_style_pad_right(row, 16, 0);
        lv_obj_set_style_pad_top(row, 12, 0);
        lv_obj_set_style_pad_bottom(row, 12, 0);

        if (title_label != nullptr) {
            lv_obj_set_width(title_label, 150);
            lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(title_label, lv_color_hex(0x0F172A), 0);
        }

        if (value_label != nullptr) {
            lv_obj_set_width(value_label, 220);
            lv_label_set_long_mode(value_label, LV_LABEL_LONG_WRAP);
            lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_style_text_font(value_label, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(value_label, lv_color_hex(0x334155), 0);
        }
    };

    lv_obj_add_flag(ui_PanelScreenSettingAbout, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui_PanelScreenSettingAbout, LV_DIR_VER);
    lv_obj_set_size(ui_PanelScreenSettingAbout, lv_pct(92), 650);
    lv_obj_align(ui_PanelScreenSettingAbout, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_style_pad_all(ui_PanelScreenSettingAbout, 0, 0);
    lv_obj_set_style_pad_row(ui_PanelScreenSettingAbout, 12, 0);
    styleAboutRow(ui_PanelPanelScreenSettingAbout, ui_LabelPanelPanelScreenSettingAboutDevice, ui_LabelPanelPanelScreenSettingAbout2);
    styleAboutRow(ui_PanelPanelScreenSettingAbout1, ui_LabelPanelPanelScreenSettingAboutManufacturer, ui_LabelPanelPanelScreenSettingAbout1);
    styleAboutRow(ui_PanelPanelScreenSettingAbout2, ui_LabelPanelPanelScreenSettingAboutMAC, ui_LabelPanelPanelScreenSettingAbout3);
    styleAboutRow(ui_PanelPanelScreenSettingAbout3, ui_LabelPanelPanelScreenSettingAboutUIFramework, ui_LabelPanelPanelScreenSettingAbout4);
    styleAboutRow(ui_PanelPanelScreenSettingAbout4, ui_LabelPanelPanelScreenSettingAboutSoftwareVersion, ui_LabelPanelPanelScreenSettingAbout5);
    styleAboutRow(ui_PanelPanelScreenSettingAbout5, ui_LabelPanelPanelScreenSettingAboutUIFrameworkVersion, ui_LabelPanelPanelScreenSettingAbout6);

    auto createAboutInfoCard = [](lv_obj_t *parent, const char *title, const char *body, lv_color_t body_color) {
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(card, 18, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(card, 16, 0);
        lv_obj_set_style_pad_right(card, 16, 0);
        lv_obj_set_style_pad_top(card, 14, 0);
        lv_obj_set_style_pad_bottom(card, 14, 0);

        lv_obj_t *title_label = lv_label_create(card);
        lv_label_set_text(title_label, title);
        lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(title_label, lv_color_hex(0x0F172A), 0);
        lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *body_label = lv_label_create(card);
        lv_obj_set_width(body_label, lv_pct(100));
        lv_label_set_long_mode(body_label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(body_label, body);
        lv_obj_set_style_text_font(body_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(body_label, body_color, 0);
        lv_obj_align_to(body_label, title_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

        return card;
    };

    createAboutInfoCard(
        ui_PanelScreenSettingAbout,
        "Project",
        "Custom ESP32-P4 firmware for the JC4880P443C_I_W profile, built on ESP-Brookesia and ESP-IDF 5.4.",
        lv_color_hex(0x334155)
    );
    createAboutInfoCard(
        ui_PanelScreenSettingAbout,
        "Hardware",
        "ESP32-P4 Function EV Board\n1024x600 display\nMIPI camera\nUSB-C and SD card support",
        lv_color_hex(0x334155)
    );
    createAboutInfoCard(
        ui_PanelScreenSettingAbout,
        "GitHub",
        "https://github.com/elik745i/JC4880P443C_I_W_Remote",
        lv_color_hex(0x2563EB)
    );

    _screen_list[UI_FIRMWARE_SETTING_INDEX] = _firmwareScreen;
    lv_obj_add_event_cb(_firmwareScreen, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);

    // Record the screen index and install the screen loaded event callback
    _screen_list[UI_ABOUT_SETTING_INDEX] = ui_ScreenSettingAbout;
    lv_obj_add_event_cb(ui_ScreenSettingAbout, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);

    const esp_app_desc_t *app_desc = esp_app_get_description();
    lv_label_set_text(ui_LabelPanelPanelScreenSettingAbout3, mac_str);
    lv_label_set_text(ui_LabelPanelPanelScreenSettingAbout5,
                      ((app_desc != nullptr) && (app_desc->version[0] != '\0')) ? app_desc->version : "unknown");
    lv_label_set_text(ui_LabelPanelPanelScreenSettingAbout2, "JC4880P443C_I_W Remote\nESP32-P4 Function EV Board");

    char char_ui_version[20];
    snprintf(char_ui_version, sizeof(char_ui_version), "v%d.%d.%d", ESP_BROOKESIA_CONF_VER_MAJOR, ESP_BROOKESIA_CONF_VER_MINOR, ESP_BROOKESIA_CONF_VER_PATCH);
    lv_label_set_text(ui_LabelPanelPanelScreenSettingAbout6, char_ui_version);
    refreshSavedWifiUi();
    refreshSecurityUi();
    scanSdFirmwareEntries();
    refreshFirmwareUi();
}

void AppSettings::processWifiConnect(WifiConnectState_t state)
{
    switch (state) {
    case WIFI_CONNECT_HIDE:
        lv_obj_add_flag(_panel_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_img_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_spinner_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        break;
    case WIFI_CONNECT_RUNNING:
        lv_obj_clear_flag(_panel_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_img_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_spinner_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        break;
    case WIFI_CONNECT_SUCCESS:
        lv_obj_clear_flag(_panel_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_img_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_img_set_src(_img_wifi_connect, &img_wifi_connect_success);
        lv_obj_add_flag(_spinner_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        break;
    case WIFI_CONNECT_FAIL:
        lv_obj_clear_flag(_panel_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_img_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        lv_img_set_src(_img_wifi_connect, &img_wifi_connect_fail);
        lv_obj_add_flag(_spinner_wifi_connect, LV_OBJ_FLAG_HIDDEN);
        break;
    default:
        break;
    }
}

bool AppSettings::loadNvsParam(void)
{
    esp_err_t err = ESP_OK;
    nvs_handle_t nvs_handle;
    err = nvs_open(NVS_STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return false;
    }

    for (auto& key_value : _nvs_param_map) {
        err = nvs_get_i32(nvs_handle, key_value.first.c_str(), &key_value.second);
        switch (err) {
        case ESP_OK:
            ESP_LOGI(TAG, "Load %s: %d", key_value.first.c_str(), key_value.second);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            err = nvs_set_i32(nvs_handle, key_value.first.c_str(), key_value.second);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error (%s) setting %s", esp_err_to_name(err), key_value.first.c_str());
            }
            ESP_LOGW(TAG, "The value of %s is not initialized yet, set it to default value: %d", key_value.first.c_str(),
                     key_value.second);
            break;
        default:
            break;
        }
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) committing NVS changes", esp_err_to_name(err));
        return false;
    }
    nvs_close(nvs_handle);

    return true;
}

bool AppSettings::setNvsParam(std::string key, int value)
{
    esp_err_t err = ESP_OK;
    nvs_handle_t nvs_handle;
    err = nvs_open(NVS_STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_i32(nvs_handle, key.c_str(), value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) setting %s", esp_err_to_name(err), key.c_str());
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) committing NVS changes", esp_err_to_name(err));
        return false;
    }
    nvs_close(nvs_handle);

    return true;
}

bool AppSettings::loadNvsStringParam(const char *key, char *buffer, size_t buffer_size)
{
    if ((key == nullptr) || (buffer == nullptr) || (buffer_size == 0)) {
        return false;
    }

    buffer[0] = '\0';

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle for %s", esp_err_to_name(err), key);
        return false;
    }

    size_t required_size = buffer_size;
    err = nvs_get_str(nvs_handle, key, buffer, &required_size);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        return true;
    }

    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error (%s) reading %s", esp_err_to_name(err), key);
    }
    buffer[0] = '\0';
    return false;
}

bool AppSettings::setNvsStringParam(const char *key, const char *value)
{
    if ((key == nullptr) || (value == nullptr)) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle for %s", esp_err_to_name(err), key);
        return false;
    }

    err = nvs_set_str(nvs_handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) saving %s", esp_err_to_name(err), key);
        return false;
    }

    return true;
}

bool AppSettings::clearSavedWifiCredentials(void)
{
    bool ok = true;
    ok &= setNvsStringParam(NVS_KEY_WIFI_SSID, "");
    ok &= setNvsStringParam(NVS_KEY_WIFI_PASSWORD, "");

    st_wifi_ssid[0] = '\0';
    st_wifi_password[0] = '\0';

    wifi_config_t wifi_config = {};
    esp_err_t err = esp_wifi_disconnect();
    if ((err != ESP_OK) && (err != ESP_ERR_WIFI_NOT_INIT) && (err != ESP_ERR_WIFI_NOT_STARTED)) {
        ESP_LOGW(TAG, "Failed to disconnect Wi-Fi while forgetting network: %s", esp_err_to_name(err));
        ok = false;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if ((err != ESP_OK) && (err != ESP_ERR_WIFI_NOT_INIT)) {
        ESP_LOGW(TAG, "Failed to clear Wi-Fi runtime config: %s", esp_err_to_name(err));
        ok = false;
    }

    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_CONNECTED);
    if (status_bar != nullptr) {
        status_bar->setWifiIconState(0);
    }

    refreshSavedWifiUi();
    return ok;
}

bool AppSettings::factoryResetPreferences(void)
{
    bool ok = true;
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle for factory reset", esp_err_to_name(err));
        return false;
    }

    err = nvs_erase_all(nvs_handle);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) wiping preferences", esp_err_to_name(err));
        return false;
    }

    initializeDefaultNvsParams();
    _hasAutoDetectedTimezone = false;
    _autoTimezoneRefreshPending = false;
    _autoDetectedTimezoneOffsetMinutes = _nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE];
    _autoTimezoneStatus.clear();

    ok &= clearSavedWifiCredentials();
    ok &= loadNvsParam();
    applyManualTimezonePreference();
    bsp_extra_codec_volume_set(_nvs_param_map[NVS_KEY_AUDIO_VOLUME], (int *)&_nvs_param_map[NVS_KEY_AUDIO_VOLUME]);
    bsp_display_brightness_set(_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS]);
    applyDisplayIdleSettings();
    updateUiByNvsParam();
    setFirmwareStatus("Factory reset complete. Saved preferences were cleared.");

    return ok;
}

void AppSettings::refreshSavedWifiUi(void)
{
    if ((_savedWifiPanel == nullptr) || (_savedWifiValueLabel == nullptr) || (_savedWifiForgetButton == nullptr)) {
        return;
    }

    char saved_ssid[sizeof(st_wifi_ssid)] = {0};
    loadNvsStringParam(NVS_KEY_WIFI_SSID, saved_ssid, sizeof(saved_ssid));

    if (saved_ssid[0] == '\0') {
        lv_label_set_text(_savedWifiValueLabel, "None");
        lv_obj_add_flag(_savedWifiForgetButton, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(_savedWifiValueLabel, saved_ssid);
    lv_obj_clear_flag(_savedWifiForgetButton, LV_OBJ_FLAG_HIDDEN);
}

void AppSettings::refreshDisplayIdleUi(void)
{
    if (_displayAdaptiveBrightnessSwitch != nullptr) {
        if (_nvs_param_map[NVS_KEY_DISPLAY_ADAPTIVE]) {
            lv_obj_add_state(_displayAdaptiveBrightnessSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_displayAdaptiveBrightnessSwitch, LV_STATE_CHECKED);
        }
    }

    if (_displayScreensaverSwitch != nullptr) {
        if (_nvs_param_map[NVS_KEY_DISPLAY_SCREENSAVER]) {
            lv_obj_add_state(_displayScreensaverSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_displayScreensaverSwitch, LV_STATE_CHECKED);
        }
    }

    if (_displayTimeoffDropdown != nullptr) {
        lv_dropdown_set_selected(_displayTimeoffDropdown,
                                 findDropdownIndexForValue(kDisplayTimeoffOptionsSec,
                                                           sizeof(kDisplayTimeoffOptionsSec) / sizeof(kDisplayTimeoffOptionsSec[0]),
                                                           _nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF]));
    }

    if (_displaySleepDropdown != nullptr) {
        lv_dropdown_set_selected(_displaySleepDropdown,
                                 findDropdownIndexForValue(kDisplaySleepOptionsSec,
                                                           sizeof(kDisplaySleepOptionsSec) / sizeof(kDisplaySleepOptionsSec[0]),
                                                           _nvs_param_map[NVS_KEY_DISPLAY_SLEEP]));
    }

    refreshTimezoneUi();
}

void AppSettings::refreshTimezoneUi(void)
{
    if (_displayTimezoneDropdown != nullptr) {
        lv_dropdown_set_selected(_displayTimezoneDropdown,
                                 findTimezoneDropdownIndexForOffset(_nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE]));
    }

    if (_displayAutoTimezoneSwitch != nullptr) {
        if (_nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO]) {
            lv_obj_add_state(_displayAutoTimezoneSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_displayAutoTimezoneSwitch, LV_STATE_CHECKED);
        }
    }

    if (_displayTimezoneInfoLabel != nullptr) {
        const bool auto_enabled = _nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO] != 0;
        const TimezoneOption &manual_option = getTimezoneOptionForOffset(_nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE]);

        std::string label = std::string("Manual: ") + manual_option.label;
        if (auto_enabled) {
            if (_hasAutoDetectedTimezone) {
                const TimezoneOption &detected_option = getTimezoneOptionForOffset(_autoDetectedTimezoneOffsetMinutes);
                label = std::string("Auto: ") + detected_option.label;
                if (!_autoTimezoneStatus.empty()) {
                    label += " • " + _autoTimezoneStatus;
                }
            } else if (!_autoTimezoneStatus.empty()) {
                label = _autoTimezoneStatus + " • fallback " + manual_option.label;
            } else {
                label = std::string("Auto timezone enabled • fallback ") + manual_option.label;
            }
        }

        lv_label_set_text(_displayTimezoneInfoLabel, label.c_str());
    }
}

void AppSettings::refreshSecurityUi(void)
{
    const bool device_lock_enabled = device_security::isLockEnabled(device_security::LockType::Device);
    const bool settings_lock_enabled = device_security::isLockEnabled(device_security::LockType::Settings);

    if (ui_SwitchPanelScreenSettingBLESwitch != nullptr) {
        if (device_lock_enabled) {
            lv_obj_add_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_CHECKED);
        }
    }

    if (_securitySettingsLockSwitch != nullptr) {
        if (settings_lock_enabled) {
            lv_obj_add_state(_securitySettingsLockSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_securitySettingsLockSwitch, LV_STATE_CHECKED);
        }
    }
}

void AppSettings::setFirmwareStatus(const std::string &status, bool is_error)
{
    if (_firmwareStatusLabel == nullptr) {
        return;
    }

    lv_label_set_text(_firmwareStatusLabel, status.c_str());
    lv_obj_set_style_text_color(_firmwareStatusLabel, is_error ? lv_color_hex(0xB91C1C) : lv_color_hex(0x334155), 0);
}

std::string AppSettings::getCurrentFirmwareVersion(void) const
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if ((app_desc == nullptr) || (app_desc->version[0] == '\0')) {
        return kFirmwareUnknownVersion;
    }

    return trim_copy(app_desc->version);
}

int AppSettings::compareVersionStrings(const std::string &lhs, const std::string &rhs)
{
    auto parse_values = [](const std::string &input) {
        std::vector<int> values;
        int current_value = -1;

        for (char ch : input) {
            if (std::isdigit(static_cast<unsigned char>(ch))) {
                if (current_value < 0) {
                    current_value = 0;
                }
                current_value = (current_value * 10) + (ch - '0');
            } else if (current_value >= 0) {
                values.push_back(current_value);
                current_value = -1;
            }
        }

        if (current_value >= 0) {
            values.push_back(current_value);
        }

        return values;
    };

    const std::vector<int> lhs_values = parse_values(lhs);
    const std::vector<int> rhs_values = parse_values(rhs);
    const size_t count = std::max(lhs_values.size(), rhs_values.size());

    for (size_t index = 0; index < count; ++index) {
        const int lhs_value = (index < lhs_values.size()) ? lhs_values[index] : 0;
        const int rhs_value = (index < rhs_values.size()) ? rhs_values[index] : 0;
        if (lhs_value != rhs_value) {
            return (lhs_value < rhs_value) ? -1 : 1;
        }
    }

    return 0;
}

std::string AppSettings::formatFirmwareLabel(const FirmwareEntry_t &entry) const
{
    std::string label = entry.label;
    if (!entry.version.empty() && (entry.version != kFirmwareUnknownVersion)) {
        label += " (" + entry.version + ")";
    }
    if (entry.is_current) {
        label += " [current]";
    } else if (entry.is_newer) {
        label += " [new]";
    }
    if (!entry.is_valid) {
        label += " [invalid]";
    }
    return label;
}

void AppSettings::populateFirmwareDropdown(lv_obj_t *dropdown, const std::vector<FirmwareEntry_t> &entries, const char *empty_label)
{
    if (dropdown == nullptr) {
        return;
    }

    if (entries.empty()) {
        lv_dropdown_set_options(dropdown, empty_label);
        lv_dropdown_set_selected(dropdown, 0);
        return;
    }

    std::string options;
    for (size_t index = 0; index < entries.size(); ++index) {
        if (index > 0) {
            options += '\n';
        }
        options += formatFirmwareLabel(entries[index]);
    }
    lv_dropdown_set_options(dropdown, options.c_str());
    lv_dropdown_set_selected(dropdown, 0);
}

bool AppSettings::hasOtaFlashSupport(void) const
{
    return esp_ota_get_next_update_partition(nullptr) != nullptr;
}

bool AppSettings::probeFirmwareFile(const std::string &path, FirmwareEntry_t &entry)
{
    entry = {};
    entry.path_or_url = path;
    entry.label = basename_from_path(path);
    entry.version = kFirmwareUnknownVersion;

    FILE *file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        entry.notes = "Unable to open file";
        return false;
    }

    struct stat file_info = {};
    if (stat(path.c_str(), &file_info) == 0) {
        entry.size_bytes = static_cast<size_t>(file_info.st_size);
    }

    esp_image_header_t image_header = {};
    esp_image_segment_header_t segment_header = {};
    esp_app_desc_t app_desc = {};
    bool ok = false;

    if ((fread(&image_header, 1, sizeof(image_header), file) == sizeof(image_header)) &&
        (image_header.magic == ESP_IMAGE_HEADER_MAGIC) &&
        (fread(&segment_header, 1, sizeof(segment_header), file) == sizeof(segment_header)) &&
        (fread(&app_desc, 1, sizeof(app_desc), file) == sizeof(app_desc))) {
        entry.project_name = trim_copy(app_desc.project_name);
        entry.version = trim_copy(app_desc.version);
        if (entry.version.empty()) {
            entry.version = kFirmwareUnknownVersion;
        }

        const esp_app_desc_t *current_app = esp_app_get_description();
        const std::string current_project = (current_app != nullptr) ? trim_copy(current_app->project_name) : std::string();
        if (!current_project.empty() && !entry.project_name.empty() && (entry.project_name != current_project)) {
            entry.notes = "Project mismatch: " + entry.project_name;
        } else {
            const std::string current_version = getCurrentFirmwareVersion();
            entry.is_valid = true;
            entry.is_current = compareVersionStrings(entry.version, current_version) == 0;
            entry.is_newer = compareVersionStrings(entry.version, current_version) > 0;

            std::ostringstream stream;
            stream << "Project: " << (entry.project_name.empty() ? "unknown" : entry.project_name)
                   << ", size: " << (entry.size_bytes / 1024U) << " KB";
            entry.notes = stream.str();
            ok = true;
        }
    } else {
        entry.notes = "Not a valid ESP firmware image";
    }

    fclose(file);
    return ok;
}

bool AppSettings::scanSdFirmwareEntries(void)
{
    _sdFirmwareEntries.clear();
    const char *directories[] = {kFirmwareSdDirectory, "/sdcard"};

    for (const char *directory_path : directories) {
        DIR *dir = opendir(directory_path);
        if (dir == nullptr) {
            continue;
        }

        struct dirent *item = nullptr;
        while ((item = readdir(dir)) != nullptr) {
            if ((strcmp(item->d_name, ".") == 0) || (strcmp(item->d_name, "..") == 0)) {
                continue;
            }

            const std::string candidate_path = std::string(directory_path) + "/" + item->d_name;
            if (!ends_with_bin(candidate_path)) {
                continue;
            }

            FirmwareEntry_t entry = {};
            probeFirmwareFile(candidate_path, entry);
            _sdFirmwareEntries.push_back(entry);
        }

        closedir(dir);
    }

    std::sort(_sdFirmwareEntries.begin(), _sdFirmwareEntries.end(), [](const FirmwareEntry_t &lhs, const FirmwareEntry_t &rhs) {
        if (lhs.is_valid != rhs.is_valid) {
            return lhs.is_valid > rhs.is_valid;
        }
        if (lhs.is_newer != rhs.is_newer) {
            return lhs.is_newer > rhs.is_newer;
        }
        return lhs.label < rhs.label;
    });

    return !_sdFirmwareEntries.empty();
}

bool AppSettings::fetchGithubFirmwareEntries(void)
{
    _otaFirmwareEntries.clear();
    std::string response;

    esp_http_client_config_t config = {};
    config.url = kFirmwareGithubReleasesUrl;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = [](esp_http_client_event_t *event) {
        if ((event == nullptr) || (event->user_data == nullptr) || (event->event_id != HTTP_EVENT_ON_DATA) ||
            (event->data == nullptr) || (event->data_len <= 0)) {
            return ESP_OK;
        }

        auto *body = static_cast<std::string *>(event->user_data);
        body->append(static_cast<const char *>(event->data), static_cast<size_t>(event->data_len));
        return ESP_OK;
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }

    bool ok = false;
    if ((esp_http_client_set_header(client, "Accept", "application/vnd.github+json") == ESP_OK) &&
        (esp_http_client_set_header(client, "User-Agent", "JC4880P443C-IW-Remote") == ESP_OK) &&
        (esp_http_client_perform(client) == ESP_OK) &&
        (esp_http_client_get_status_code(client) == 200)) {
        cJSON *root = cJSON_Parse(response.c_str());
        if (cJSON_IsArray(root)) {
            const std::string current_version = getCurrentFirmwareVersion();
            cJSON *release = nullptr;
            cJSON_ArrayForEach(release, root) {
                if (!cJSON_IsObject(release) || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(release, "draft"))) {
                    continue;
                }

                const std::string release_name = trim_copy(safe_json_string(release, "name"));
                const std::string tag_name = trim_copy(safe_json_string(release, "tag_name"));
                cJSON *assets = cJSON_GetObjectItemCaseSensitive(release, "assets");
                if (!cJSON_IsArray(assets)) {
                    continue;
                }

                cJSON *asset = nullptr;
                cJSON_ArrayForEach(asset, assets) {
                    if (!cJSON_IsObject(asset)) {
                        continue;
                    }

                    const std::string asset_name = trim_copy(safe_json_string(asset, "name"));
                    const std::string download_url = trim_copy(safe_json_string(asset, "browser_download_url"));
                    if (asset_name.empty() || download_url.empty() || !ends_with_bin(asset_name)) {
                        continue;
                    }

                    FirmwareEntry_t entry = {};
                    entry.label = asset_name;
                    entry.version = tag_name.empty() ? release_name : tag_name;
                    if (entry.version.empty()) {
                        entry.version = kFirmwareUnknownVersion;
                    }
                    entry.path_or_url = download_url;
                    entry.project_name = "JC4880P443C_I_W_Remote";
                    entry.size_bytes = static_cast<size_t>(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(asset, "size")) ?
                                                              cJSON_GetObjectItemCaseSensitive(asset, "size")->valuedouble : 0);
                    entry.notes = release_name.empty() ? std::string("GitHub release") : release_name;
                    entry.is_valid = true;
                    entry.is_current = compareVersionStrings(entry.version, current_version) == 0;
                    entry.is_newer = compareVersionStrings(entry.version, current_version) > 0;
                    _otaFirmwareEntries.push_back(entry);
                }
            }
        }
        cJSON_Delete(root);
        ok = !_otaFirmwareEntries.empty();
    }

    esp_http_client_cleanup(client);

    std::sort(_otaFirmwareEntries.begin(), _otaFirmwareEntries.end(), [](const FirmwareEntry_t &lhs, const FirmwareEntry_t &rhs) {
        if (lhs.is_current != rhs.is_current) {
            return lhs.is_current > rhs.is_current;
        }
        if (lhs.is_newer != rhs.is_newer) {
            return lhs.is_newer > rhs.is_newer;
        }
        return AppSettings::compareVersionStrings(lhs.version, rhs.version) > 0;
    });

    return ok;
}

void AppSettings::refreshFirmwareUi(void)
{
    populateFirmwareDropdown(_firmwareSdDropdown, _sdFirmwareEntries, "No SD firmware found");
    populateFirmwareDropdown(_firmwareOtaDropdown, _otaFirmwareEntries, "Run OTA check first");

    const bool ota_supported = hasOtaFlashSupport();
    const bool sd_ready = ota_supported && !_sdFirmwareEntries.empty() && _sdFirmwareEntries.front().is_valid;
    const bool ota_ready = ota_supported && !_otaFirmwareEntries.empty() && _otaFirmwareEntries.front().is_valid;

    auto update_button = [](lv_obj_t *button, bool enabled) {
        if (button == nullptr) {
            return;
        }

        if (enabled) {
            lv_obj_clear_state(button, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(button, lv_color_hex(0x2563EB), 0);
        } else {
            lv_obj_add_state(button, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(button, lv_color_hex(0x94A3B8), 0);
        }
    };

    update_button(_firmwareSdFlashButton, sd_ready);
    update_button(_firmwareOtaFlashButton, ota_ready);

    if (!ota_supported) {
        setFirmwareStatus("Flash buttons are disabled because this build has only a factory app partition. Safe in-app updates require OTA partitions.");
    } else if (!_sdFirmwareEntries.empty()) {
        setFirmwareStatus(_sdFirmwareEntries.front().notes);
    } else {
        setFirmwareStatus("Firmware screen ready. Scan SD or check GitHub releases.");
    }
}

void AppSettings::applyDisplayIdleSettings(void)
{
    bsp_extra_display_idle_set_base_brightness(_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS]);
    bsp_extra_display_idle_configure(_nvs_param_map[NVS_KEY_DISPLAY_ADAPTIVE],
                                     _nvs_param_map[NVS_KEY_DISPLAY_SCREENSAVER],
                                     _nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF],
                                     _nvs_param_map[NVS_KEY_DISPLAY_SLEEP]);
}

void AppSettings::applyManualTimezonePreference(void)
{
    const TimezoneOption &option = getTimezoneOptionForOffset(_nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE]);
    app_sntp_set_timezone(option.tz);
}

bool AppSettings::syncAutoTimezoneFromInternet(void)
{
    std::string response;

    esp_http_client_config_t config = {};
    config.url = kTimezoneLookupUrl;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 12000;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = [](esp_http_client_event_t *event) {
        if ((event == nullptr) || (event->user_data == nullptr) || (event->event_id != HTTP_EVENT_ON_DATA) ||
            (event->data == nullptr) || (event->data_len <= 0)) {
            return ESP_OK;
        }

        auto *body = static_cast<std::string *>(event->user_data);
        body->append(static_cast<const char *>(event->data), static_cast<size_t>(event->data_len));
        return ESP_OK;
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        _autoTimezoneStatus = "Auto timezone lookup unavailable";
        refreshTimezoneUi();
        return false;
    }

    bool ok = false;
    if ((esp_http_client_set_header(client, "Accept", "application/json") == ESP_OK) &&
        (esp_http_client_set_header(client, "User-Agent", "JC4880P443C-IW-Remote") == ESP_OK) &&
        (esp_http_client_perform(client) == ESP_OK) &&
        (esp_http_client_get_status_code(client) == 200)) {
        cJSON *root = cJSON_Parse(response.c_str());
        if (cJSON_IsObject(root)) {
            const std::string offset_text = trim_copy(safe_json_string(root, "utc_offset"));
            const std::string city = trim_copy(safe_json_string(root, "city"));
            const std::string region = trim_copy(safe_json_string(root, "region"));
            const std::string country = trim_copy(safe_json_string(root, "country_name"));
            int32_t detected_minutes = 0;

            if (parseUtcOffsetMinutes(offset_text, detected_minutes)) {
                _autoDetectedTimezoneOffsetMinutes = detected_minutes;
                _hasAutoDetectedTimezone = true;

                const TimezoneOption &option = getTimezoneOptionForOffset(detected_minutes);
                app_sntp_set_timezone(option.tz);

                std::string location = city;
                if (!region.empty()) {
                    location = location.empty() ? region : (location + ", " + region);
                }
                if (!country.empty()) {
                    location = location.empty() ? country : (location + ", " + country);
                }

                _autoTimezoneStatus = location.empty() ? std::string("Detected from internet") : location;
                ok = true;
            }
        }
        cJSON_Delete(root);
    }

    esp_http_client_cleanup(client);

    if (!ok) {
        _hasAutoDetectedTimezone = false;
        _autoTimezoneStatus = "Auto timezone lookup failed";
        applyManualTimezonePreference();
    }

    refreshTimezoneUi();
    return ok;
}

bool AppSettings::restoreWifiCredentials(void)
{
    loadNvsStringParam(NVS_KEY_WIFI_SSID, st_wifi_ssid, sizeof(st_wifi_ssid));
    loadNvsStringParam(NVS_KEY_WIFI_PASSWORD, st_wifi_password, sizeof(st_wifi_password));

    if ((st_wifi_ssid[0] == '\0') || !_nvs_param_map[NVS_KEY_WIFI_ENABLE]) {
        return false;
    }

    wifi_config_t wifi_config = {};
    memcpy(wifi_config.sta.ssid, st_wifi_ssid, std::min(strlen(st_wifi_ssid), sizeof(wifi_config.sta.ssid) - 1));
    memcpy(wifi_config.sta.password, st_wifi_password, std::min(strlen(st_wifi_password), sizeof(wifi_config.sta.password) - 1));

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restore Wi-Fi config: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Restored Wi-Fi credentials for SSID: %s", st_wifi_ssid);
    return true;
}

void AppSettings::updateUiByNvsParam(void)
{
    loadNvsStringParam(NVS_KEY_WIFI_SSID, st_wifi_ssid, sizeof(st_wifi_ssid));
    loadNvsStringParam(NVS_KEY_WIFI_PASSWORD, st_wifi_password, sizeof(st_wifi_password));

    if (_nvs_param_map[NVS_KEY_WIFI_ENABLE]) {
        lv_obj_add_state(ui_SwitchPanelScreenSettingWiFiSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(ui_SwitchPanelScreenSettingWiFiSwitch, LV_STATE_CHECKED);
    }

    lv_slider_set_value(ui_SliderPanelScreenSettingLightSwitch1, _nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS], LV_ANIM_OFF);
    lv_slider_set_value(ui_SliderPanelScreenSettingVolumeSwitch, _nvs_param_map[NVS_KEY_AUDIO_VOLUME], LV_ANIM_OFF);
    refreshSavedWifiUi();
    refreshSecurityUi();
    refreshDisplayIdleUi();
}

esp_err_t AppSettings::initWifi()
{
    s_wifi_event_group = xEventGroupCreate();
    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_CONNECTED);
    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_INIT_DONE);
    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_SCANING);
    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_CONNECTING);
    if(!(xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_UI_INIT_DONE)) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_UI_INIT_DONE);
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

    esp_event_handler_instance_t instance_any_id;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifiEventHandler,
                                                        this,
                                                        &instance_any_id));
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifiEventHandler,
                                                        this,
                                                        &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    const bool has_saved_wifi = restoreWifiCredentials();
    ESP_ERROR_CHECK(esp_wifi_start());

    if (_nvs_param_map[NVS_KEY_WIFI_ENABLE] && has_saved_wifi) {
        requestWifiConnect("saved credentials");
    }

    return ESP_OK;
}

void AppSettings::requestWifiConnect(const char *reason)
{
    if ((s_wifi_event_group == nullptr) || !_nvs_param_map[NVS_KEY_WIFI_ENABLE] || (st_wifi_ssid[0] == '\0')) {
        return;
    }

    const EventBits_t wifi_bits = xEventGroupGetBits(s_wifi_event_group);
    if ((wifi_bits & WIFI_EVENT_CONNECTED) || (wifi_bits & WIFI_EVENT_CONNECTING)) {
        return;
    }

    esp_err_t err = esp_wifi_connect();
    if ((err == ESP_OK) || (err == ESP_ERR_WIFI_CONN)) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_EVENT_CONNECTING);
        ESP_LOGI(TAG, "Queued Wi-Fi connect (%s) for SSID:%s", (reason != nullptr) ? reason : "unknown", st_wifi_ssid);
        return;
    }

    ESP_LOGW(TAG, "Wi-Fi connect request (%s) failed to start: %s",
             (reason != nullptr) ? reason : "unknown", esp_err_to_name(err));
}

void AppSettings::startWifiScan(void)
{
    ESP_LOGI(TAG, "Start Wi-Fi scan");
    xEventGroupSetBits(s_wifi_event_group, WIFI_EVENT_SCANING);
    lv_obj_clear_flag(ui_SpinnerScreenSettingWiFi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_SwitchPanelScreenSettingWiFiSwitch, LV_OBJ_FLAG_CLICKABLE);
}

AppSettings::WifiSignalStrengthLevel_t AppSettings::wifiSignalStrengthFromRssi(int rssi) const
{
    if (rssi > -100 && rssi <= -80) {
        return WIFI_SIGNAL_STRENGTH_WEAK;
    }

    if (rssi > -80 && rssi <= -60) {
        return WIFI_SIGNAL_STRENGTH_MODERATE;
    }

    if (rssi > -60) {
        return WIFI_SIGNAL_STRENGTH_GOOD;
    }

    return WIFI_SIGNAL_STRENGTH_NONE;
}

void AppSettings::refreshWifiStatusBar(void)
{
    WifiSignalStrengthLevel_t signal_level = WIFI_SIGNAL_STRENGTH_NONE;
    const EventBits_t wifi_bits = xEventGroupGetBits(s_wifi_event_group);

    if (wifi_bits & WIFI_EVENT_CONNECTED) {
        wifi_ap_record_t ap_info = {};
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            signal_level = wifiSignalStrengthFromRssi(ap_info.rssi);
        }
    }

    _wifi_signal_strength_level = signal_level;

    bsp_display_lock(0);
    status_bar->setWifiIconState(static_cast<int>(signal_level));
    bsp_display_unlock();
}

void AppSettings::stopWifiScan(void)
{
    ESP_LOGI(TAG, "Stop Wi-Fi scan");
    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_SCANING);
    lv_obj_add_flag(ui_PanelScreenSettingWiFiList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_SpinnerScreenSettingWiFi, LV_OBJ_FLAG_HIDDEN);
    deinitWifiListButton();
}

void AppSettings::scanWifiAndUpdateUi(void)
{
    bool psk_flag = false;

    uint16_t number = SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[SCAN_LIST_SIZE];
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    esp_wifi_start();
    esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
#if ENABLE_DEBUG_LOG
    ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);
#endif

    bsp_display_lock(0);
    if(xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_SCANING) {
        deinitWifiListButton();
    }
    bsp_display_unlock();

    for (int i = 0; (i < SCAN_LIST_SIZE) && (i < ap_count); i++) {
#if ENABLE_DEBUG_LOG
        ESP_LOGI(TAG, "SSID \t\t%s", ap_info[i].ssid);
        ESP_LOGI(TAG, "RSSI \t\t%d", ap_info[i].rssi);
        ESP_LOGI(TAG, "Channel \t\t%d", ap_info[i].primary);
#endif

        if(ap_info[i].authmode != WIFI_AUTH_OPEN && ap_info[i].authmode != WIFI_AUTH_OWE) {
            psk_flag = true;
        }
#if ENABLE_DEBUG_LOG
        ESP_LOGI(TAG, "psk_flag: %d", psk_flag);
#endif

        _wifi_signal_strength_level = wifiSignalStrengthFromRssi(ap_info[i].rssi);
#if ENABLE_DEBUG_LOG
        ESP_LOGI(TAG, "signal_strength: %d", _wifi_signal_strength_level);
#endif

        bsp_display_lock(0);
        if(xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_SCANING) {
            initWifiListButton(label_wifi_ssid[i], img_img_wifi_lock[i], wifi_image[i], wifi_connect[i],
                                ap_info[i].ssid, psk_flag, _wifi_signal_strength_level);     
        }
        bsp_display_unlock();
    }
}

void AppSettings::initWifiListButton(lv_obj_t* lv_label_ssid, lv_obj_t* lv_img_wifi_lock, lv_obj_t* lv_wifi_img,
                                     lv_obj_t *lv_wifi_connect, uint8_t* ssid, bool psk, WifiSignalStrengthLevel_t signal_strength)
{
    lv_label_set_text_fmt(lv_label_ssid, "%s", (const char*)ssid);

    if (strcmp((const char*)ssid, (const char*)st_wifi_ssid) == 0) {
        lv_obj_clear_flag(lv_wifi_connect, LV_OBJ_FLAG_HIDDEN);
    }

    if(psk) {
        lv_img_set_src(lv_img_wifi_lock, &img_wifi_lock);
        lv_obj_clear_flag(lv_img_wifi_lock, LV_OBJ_FLAG_HIDDEN);
    }

    if (signal_strength == WIFI_SIGNAL_STRENGTH_GOOD) {
        lv_img_set_src(lv_wifi_img, &img_wifisignal_good);
    } else if (signal_strength == WIFI_SIGNAL_STRENGTH_MODERATE) {
        lv_img_set_src(lv_wifi_img, &img_wifisignal_moderate);
    } else if (signal_strength == WIFI_SIGNAL_STRENGTH_WEAK) {
        lv_img_set_src(lv_wifi_img, &img_wifisignal_wake);
    } else {
        lv_img_set_src(lv_wifi_img, &img_wifisignal_absent);
    }
}

void AppSettings::deinitWifiListButton(void)
{
    for (int i = 0; i < SCAN_LIST_SIZE; i++) {
        lv_obj_add_flag(img_img_wifi_lock[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wifi_connect[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void AppSettings::euiRefresTask(void *arg)
{
    AppSettings *app = (AppSettings *)arg;
    time_t now;
    struct tm timeinfo;
    bool is_time_pm = false;
    // char textBuf[50];
    uint16_t free_sram_size_kb = 0;
    uint16_t total_sram_size_kb = 0;
    uint16_t free_psram_size_kb = 0;
    uint16_t total_psram_size_kb = 0;

    if (app == NULL) {
        ESP_LOGE(TAG, "App instance is NULL");
        goto err;
    }

    while (1) {
        /* Update status bar */
        // time
        time(&now);
        localtime_r(&now, &timeinfo);
        is_time_pm = (timeinfo.tm_hour >= 12);

        bsp_display_lock(0);
        if(!app->status_bar->setClock(timeinfo.tm_hour, timeinfo.tm_min, is_time_pm)) {
            ESP_LOGE(TAG, "Set clock failed");
        }
        bsp_display_unlock();

        // Update WiFi icon state
        if((xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_CONNECTED)) {
            app_sntp_init();
        }
        app->refreshWifiStatusBar();

        /* Updte Smart Gadget app */
        // app->updateGadgetTime(timeinfo);

        // Update memory in backstage
        if(app->backstage->checkVisible()) {
            free_sram_size_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
            total_sram_size_kb = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;
            free_psram_size_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
            total_psram_size_kb = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
            ESP_LOGI(TAG, "Free sram size: %d KB, total sram size: %d KB, "
                        "free psram size: %d KB, total psram size: %d KB",
                        free_sram_size_kb, total_sram_size_kb, free_psram_size_kb, total_psram_size_kb);

            bsp_display_lock(0);
            if(!app->backstage->setMemoryLabel(free_sram_size_kb, total_sram_size_kb, free_psram_size_kb, total_psram_size_kb)) {
                ESP_LOGE(TAG, "Update memory usage failed");
            }
            bsp_display_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(HOME_REFRESH_TASK_PERIOD_MS));
    }

err:
    vTaskDelete(NULL);
}


void AppSettings::euiBatteryTask(void *arg)
{
    AppSettings *app = (AppSettings *)arg;
     while(1)
    {
       
        float capacity = 0;
        adc_battery_estimation_get_capacity(app->adc_battery_estimation_handle, &capacity);
        
        adc_battery_estimation_get_charging_state(app->adc_battery_estimation_handle,&app->charge_flag);
        // printf("Battery capacity: %.1f%%\n", capacity);
         
 
         bsp_display_lock(0);

         if(!app->status_bar->setBatteryPercent(app->charge_flag,capacity))
         {
             ESP_LOGE(TAG,"Set battery failed");
         }
        //  if(app->charge_flag)
        //  {
        //     app->status_bar->hideBatteryPercent();
        //  }
        //  else
        //  {
        //     app->status_bar->showBatteryPercent();
        //  }
         bsp_display_unlock();

         vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
    vTaskDelete(NULL);

}

void AppSettings::wifiScanTask(void *arg)
{
    AppSettings *app = (AppSettings *)arg;
    esp_err_t ret = ESP_OK;

    if (app == NULL) {
        ESP_LOGE(TAG, "App instance is NULL");
        goto err;
    }

    ret = app->initWifi();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init Wi-Fi failed");
        goto err;
    }

    if (ret == ESP_OK) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_EVENT_INIT_DONE);
        ESP_LOGI(TAG, "wifi_init done");
    } else {
        ESP_LOGE(TAG, "wifi_init failed");
    }

    while (true) {
        if((xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_INIT_DONE) &&
           (xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_UI_INIT_DONE)){
            lv_obj_add_flag(ui_SwitchPanelScreenSettingWiFiSwitch, LV_OBJ_FLAG_CLICKABLE);
            xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_INIT_DONE);
            xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_UI_INIT_DONE);
        }

        if(xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_SCANING){
            app->scanWifiAndUpdateUi();
            vTaskDelay(pdMS_TO_TICKS(WIFI_SCAN_TASK_PERIOD_MS));
        }

        const EventBits_t wifi_bits = xEventGroupGetBits(s_wifi_event_group);
        if ((wifi_bits & WIFI_EVENT_CONNECTED) && app->_autoTimezoneRefreshPending) {
            app->_autoTimezoneRefreshPending = false;
            if (app->_nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO]) {
                app->syncAutoTimezoneFromInternet();
            } else {
                app->applyManualTimezonePreference();
            }
            app_sntp_init();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

err:
    vTaskDelete(NULL);
}

void AppSettings::wifiConnectTask(void *arg)
{
    AppSettings *app = (AppSettings *)arg;
    wifi_config_t wifi_config = { 0 };

    esp_wifi_disconnect();
    app->status_bar->setWifiIconState(0);

    strncpy(st_wifi_ssid, lv_label_get_text(ui_LabelScreenSettingVerificationSSID), sizeof(st_wifi_ssid) - 1);
    st_wifi_ssid[sizeof(st_wifi_ssid) - 1] = '\0';
    strncpy(st_wifi_password, lv_textarea_get_text(ui_TextAreaScreenSettingVerificationPassword), sizeof(st_wifi_password) - 1);
    st_wifi_password[sizeof(st_wifi_password) - 1] = '\0';

    memcpy(wifi_config.sta.ssid, st_wifi_ssid, std::min(strlen(st_wifi_ssid), sizeof(wifi_config.sta.ssid) - 1));
    memcpy(wifi_config.sta.password, st_wifi_password, std::min(strlen(st_wifi_password), sizeof(wifi_config.sta.password) - 1));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_CONNECTED | WIFI_EVENT_CONNECTING);

    app->setNvsStringParam(NVS_KEY_WIFI_SSID, st_wifi_ssid);
    app->setNvsStringParam(NVS_KEY_WIFI_PASSWORD, st_wifi_password);
    app->setNvsParam(NVS_KEY_WIFI_ENABLE, 1);
    app->_nvs_param_map[NVS_KEY_WIFI_ENABLE] = true;
    ESP_LOGI(TAG, "Starting Wi-Fi connection for SSID: %s", st_wifi_ssid);
    app->requestWifiConnect("manual connection");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_EVENT_CONNECTED,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(WIFI_CONNECT_RET_WAIT_TIME_MS));

    if (bits & WIFI_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "Connected successfully");

        if (!app->_is_ui_del) {
            bsp_display_lock(0);
            app->processWifiConnect(WIFI_CONNECT_SUCCESS);
            bsp_display_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_UI_WAIT_TIME_MS));

        if (!app->_is_ui_del) {
            bsp_display_lock(0);
            app->processWifiConnect(WIFI_CONNECT_HIDE);
            app->setWifiKeyboardVisible(false);
            app->updateWifiPasswordVisibility(false);
            lv_textarea_set_text(ui_TextAreaScreenSettingVerificationPassword, "");
            app->back();
            bsp_display_unlock();
        }

        // app->updateGadgetTime(timeinfo);
    } else {
        ESP_LOGI(TAG, "Connect failed");

        if (!app->_is_ui_del) {
            bsp_display_lock(0);
            app->processWifiConnect(WIFI_CONNECT_FAIL);
            bsp_display_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_UI_WAIT_TIME_MS));

        if (!app->_is_ui_del) {
            bsp_display_lock(0);
            app->processWifiConnect(WIFI_CONNECT_HIDE);
            app->setWifiKeyboardVisible(false);
            app->updateWifiPasswordVisibility(false);
            lv_textarea_set_text(ui_TextAreaScreenSettingVerificationPassword, "");
            // app->back();
            bsp_display_unlock();
        }
    }

    // if (!app->_is_ui_del) {
    //     xEventGroupSetBits(s_wifi_event_group, WIFI_EVENT_SCANING);
    //     app->startWifiScan();
    // }

    vTaskDelete(NULL);
}

void AppSettings::wifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    AppSettings *app = (AppSettings *)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Connected to AP SSID:%s", st_wifi_ssid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (app != nullptr) {
            app->requestWifiConnect("station start");
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_CONNECTED | WIFI_EVENT_CONNECTING);
        ESP_LOGI(TAG, "Disconnected from AP SSID:%s", st_wifi_ssid);
        if (app != nullptr) {
            app->requestWifiConnect("disconnect recovery");
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        if(lv_obj_has_flag(ui_PanelScreenSettingWiFiList, LV_OBJ_FLAG_HIDDEN) &&
           xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_SCANING) {
            if (!app->_is_ui_del) {
                bsp_display_lock(0);
                lv_obj_clear_flag(ui_PanelScreenSettingWiFiList, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_SpinnerScreenSettingWiFi, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_SwitchPanelScreenSettingWiFiSwitch, LV_OBJ_FLAG_CLICKABLE);
                app->status_bar->setWifiIconState(0);
                bsp_display_unlock();
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_CONNECTING);
        xEventGroupSetBits(s_wifi_event_group, WIFI_EVENT_CONNECTED);
        if (app != nullptr) {
            app->_autoTimezoneRefreshPending = true;
            app->refreshWifiStatusBar();
        }
    }
}

void AppSettings::onKeyboardScreenSettingVerificationClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    lv_keyboard_set_textarea(target, ui_TextAreaScreenSettingVerificationPassword);

    if (code == LV_EVENT_CANCEL) {
        app->setWifiKeyboardVisible(false);
    } else if (code == LV_EVENT_READY) {
        app->setWifiKeyboardVisible(false);
        app->processWifiConnect(WIFI_CONNECT_RUNNING);

        app->stopWifiScan();

        xTaskCreatePinnedToCore(wifiConnectTask, "wifi Connect", WIFI_CONNECT_TASK_STACK_SIZE, app,
                                WIFI_CONNECT_TASK_PRIORITY, NULL, WIFI_CONNECT_TASK_STACK_CORE);
    }

end:
    return;
}

void AppSettings::onWifiPasswordFieldEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if ((code == LV_EVENT_FOCUSED) || (code == LV_EVENT_CLICKED)) {
        app->setWifiKeyboardVisible(true);
    }

end:
    return;
}

void AppSettings::onWifiPasswordToggleClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    app->updateWifiPasswordVisibility(!app->_isWifiPasswordVisible);
    app->setWifiKeyboardVisible(true);

end:
    return;
}

void AppSettings::setWifiKeyboardVisible(bool visible)
{
    if (ui_KeyboardScreenSettingVerification == nullptr) {
        return;
    }

    if (visible) {
        lv_keyboard_set_textarea(ui_KeyboardScreenSettingVerification, ui_TextAreaScreenSettingVerificationPassword);
        lv_obj_clear_flag(ui_KeyboardScreenSettingVerification, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ui_KeyboardScreenSettingVerification);
        lv_obj_align(ui_KeyboardScreenSettingVerification, LV_ALIGN_BOTTOM_MID, 0, 0);
    } else {
        lv_obj_add_flag(ui_KeyboardScreenSettingVerification, LV_OBJ_FLAG_HIDDEN);
    }
}

void AppSettings::updateWifiPasswordVisibility(bool visible)
{
    _isWifiPasswordVisible = visible;

    if (ui_TextAreaScreenSettingVerificationPassword != nullptr) {
        lv_textarea_set_password_mode(ui_TextAreaScreenSettingVerificationPassword, !visible);
    }

    if (_wifiPasswordToggleLabel != nullptr) {
        lv_label_set_text(_wifiPasswordToggleLabel, visible ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    }
}

void AppSettings::onScreenLoadEventCallback( lv_event_t * e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    SettingScreenIndex_t last_scr_index = app->_screen_index;

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    for (int i = 0; i < UI_MAX_INDEX; i++) {
        if (app->_screen_list[i] == lv_event_get_target(e)) {
            app->_screen_index = (SettingScreenIndex_t)i;
            break;
        }
    }

    if (last_scr_index == UI_WIFI_SCAN_INDEX) {
        app->stopWifiScan();
    }

    if (app->_screen_index != UI_WIFI_CONNECT_INDEX) {
        app->setWifiKeyboardVisible(false);
        app->updateWifiPasswordVisibility(false);
    }

    if ((app->_screen_index == UI_WIFI_SCAN_INDEX) && (app->_nvs_param_map[NVS_KEY_WIFI_ENABLE] == true)) {
        app->startWifiScan();
    }

    if (app->_screen_index == UI_BLUETOOTH_SETTING_INDEX) {
        app->refreshSecurityUi();
    }

    if (app->_screen_index == UI_FIRMWARE_SETTING_INDEX) {
        app->refreshFirmwareUi();
    }

end:
    return;
}

void AppSettings::onFirmwareMenuClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    lv_scr_load_anim(app->_firmwareScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);

end:
    return;
}

void AppSettings::onFirmwareSdRefreshClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    app->scanSdFirmwareEntries();
    app->refreshFirmwareUi();
    if (app->_sdFirmwareEntries.empty()) {
        app->setFirmwareStatus("No valid .bin firmware images found on /sdcard or /sdcard/firmware.", true);
    }

end:
    return;
}

void AppSettings::onFirmwareOtaCheckClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    app->setFirmwareStatus("Checking GitHub releases...");
    if (!app->fetchGithubFirmwareEntries()) {
        app->refreshFirmwareUi();
        app->setFirmwareStatus("No GitHub firmware releases found or the request failed.", true);
        goto end;
    }

    app->refreshFirmwareUi();

end:
    return;
}

void AppSettings::onFirmwareSdFlashClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (!app->hasOtaFlashSupport()) {
        app->setFirmwareStatus("In-app flashing is blocked because the current partition table has no OTA slot.", true);
        goto end;
    }

    app->setFirmwareStatus("Flashing is only enabled when OTA partitions are provisioned. This build keeps it blocked for safety.", true);

end:
    return;
}

void AppSettings::onFirmwareOtaFlashClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (!app->hasOtaFlashSupport()) {
        app->setFirmwareStatus("OTA flashing is blocked because the current partition table has no OTA slot.", true);
        goto end;
    }

    app->setFirmwareStatus("Flashing is only enabled when OTA partitions are provisioned. This build keeps it blocked for safety.", true);

end:
    return;
}

void AppSettings::onFirmwareFactoryResetClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *msgbox = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    static const char *buttons[] = {"Reset", "Cancel", ""};
    msgbox = lv_msgbox_create(NULL,
                              "Factory Reset",
                              "Erase saved preferences and restore default settings? This clears Wi-Fi, display, audio, and timezone preferences.",
                              buttons,
                              false);
    lv_obj_center(msgbox);
    lv_obj_set_width(msgbox, 360);
    lv_obj_add_event_cb(msgbox, onFirmwareFactoryResetConfirmEventCallback, LV_EVENT_VALUE_CHANGED, app);

end:
    return;
}

void AppSettings::onFirmwareFactoryResetConfirmEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *msgbox = lv_event_get_target(e);
    const char *button_text = nullptr;

    if ((app == nullptr) || (msgbox == nullptr)) {
        return;
    }

    button_text = lv_msgbox_get_active_btn_text(msgbox);
    if ((button_text != nullptr) && (strcmp(button_text, "Reset") == 0)) {
        if (!app->factoryResetPreferences()) {
            app->setFirmwareStatus("Factory reset failed. Preferences were not fully cleared.", true);
        }
    }

    lv_msgbox_close(msgbox);
}

void AppSettings::onSwitchPanelScreenSettingWiFiSwitchValueChangeEventCallback( lv_event_t * e) {
    lv_state_t state = lv_obj_get_state(ui_SwitchPanelScreenSettingWiFiSwitch);

    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (state & LV_STATE_CHECKED) {
        app->_nvs_param_map[NVS_KEY_WIFI_ENABLE] = true;
        app->setNvsParam(NVS_KEY_WIFI_ENABLE, 1);
        if (st_wifi_ssid[0] != '\0') {
            esp_wifi_connect();
        }
        if (app->_screen_index == UI_WIFI_SCAN_INDEX) {
            app->startWifiScan();
        }
    } else {
        app->_nvs_param_map[NVS_KEY_WIFI_ENABLE] = false;
        app->setNvsParam(NVS_KEY_WIFI_ENABLE, 0);
        if (app->_screen_index == UI_WIFI_SCAN_INDEX) {
            app->stopWifiScan();
            if (xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_CONNECTED) {
                ESP_ERROR_CHECK(esp_wifi_disconnect());
                app->status_bar->setWifiIconState(0);
            }
        }
    }

end:
    return;
}

void AppSettings::onForgetSavedWifiClickedEventCallback(lv_event_t * e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    app->clearSavedWifiCredentials();

end:
    return;
}

void AppSettings::onButtonWifiListClickedEventCallback(lv_event_t * e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label_wifi_ssid = lv_obj_get_child(btn, 0);
    lv_area_t btn_click_area;
    lv_point_t point;

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");
    ESP_BROOKESIA_CHECK_NULL_GOTO(label_wifi_ssid, end, "Invalid SSID label");

    lv_obj_get_click_area(btn, &btn_click_area);
    lv_indev_get_point(lv_indev_get_act(), &point);
    if ((point.x < btn_click_area.x1) || (point.x > btn_click_area.x2) ||
        (point.y < btn_click_area.y1) || (point.y > btn_click_area.y2)) {
        return;
    }

    lv_scr_load_anim(ui_ScreenSettingVerification, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
    lv_label_set_text_fmt(ui_LabelScreenSettingVerificationSSID, "%s", lv_label_get_text(label_wifi_ssid));
    lv_textarea_set_text(ui_TextAreaScreenSettingVerificationPassword, "");
    app->updateWifiPasswordVisibility(false);
    app->setWifiKeyboardVisible(false);

    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_SCANING);

    esp_wifi_scan_stop();

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingBLESwitchValueChangeEventCallback( lv_event_t * e) {
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool requested_state = false;
    bool current_state = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    requested_state = (lv_obj_get_state(ui_SwitchPanelScreenSettingBLESwitch) & LV_STATE_CHECKED) != 0;
    current_state = device_security::isLockEnabled(device_security::LockType::Device);
    if (requested_state == current_state) {
        return;
    }

    if (requested_state) {
        device_security::requestEnable(device_security::LockType::Device,
                                       onSecurityToggleRequestFinished,
                                       &app->_deviceLockToggleContext);
    } else {
        device_security::requestDisable(device_security::LockType::Device,
                                        onSecurityToggleRequestFinished,
                                        &app->_deviceLockToggleContext);
    }

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingSettingsLockValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool requested_state = false;
    bool current_state = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    requested_state = (lv_obj_get_state(app->_securitySettingsLockSwitch) & LV_STATE_CHECKED) != 0;
    current_state = device_security::isLockEnabled(device_security::LockType::Settings);
    if (requested_state == current_state) {
        return;
    }

    if (requested_state) {
        device_security::requestEnable(device_security::LockType::Settings,
                                       onSecurityToggleRequestFinished,
                                       &app->_settingsLockToggleContext);
    } else {
        device_security::requestDisable(device_security::LockType::Settings,
                                        onSecurityToggleRequestFinished,
                                        &app->_settingsLockToggleContext);
    }

end:
    return;
}

void AppSettings::onSecurityToggleRequestFinished(bool success, void *user_data)
{
    SecurityToggleContext *context = static_cast<SecurityToggleContext *>(user_data);
    if ((context == nullptr) || (context->app == nullptr)) {
        return;
    }

    context->app->handleSecurityToggleResult(context->type, success);
}

void AppSettings::handleSecurityToggleResult(device_security::LockType type, bool success)
{
    (void)type;
    (void)success;
    refreshSecurityUi();
}

void AppSettings::onSliderPanelVolumeSwitchValueChangeEventCallback( lv_event_t * e) {
    int volume = lv_slider_get_value(ui_SliderPanelScreenSettingVolumeSwitch);

    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (volume != app->_nvs_param_map[NVS_KEY_AUDIO_VOLUME]) {
        if ((bsp_extra_codec_volume_set(volume, NULL) != ESP_OK) && (bsp_extra_codec_volume_get() != volume)) {
            ESP_LOGE(TAG, "Set volume failed");
            lv_slider_set_value(ui_SliderPanelScreenSettingVolumeSwitch, app->_nvs_param_map[NVS_KEY_AUDIO_VOLUME], LV_ANIM_OFF);
            return;
        }
        app->_nvs_param_map[NVS_KEY_AUDIO_VOLUME] = volume;
        app->setNvsParam(NVS_KEY_AUDIO_VOLUME, volume);
    }

end:
    return;
}

void AppSettings::onSliderPanelLightSwitchValueChangeEventCallback( lv_event_t * e) {
    brightness = lv_slider_get_value(ui_SliderPanelScreenSettingLightSwitch1);

    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (brightness != app->_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS]) {
        // if ((bsp_display_brightness_set(brightness) != ESP_OK) && (bsp_display_brightness_get() != brightness)) {
        if (bsp_display_brightness_set(brightness) != ESP_OK) {
            ESP_LOGE(TAG, "Set brightness failed");
            lv_slider_set_value(ui_SliderPanelScreenSettingLightSwitch1, app->_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS], LV_ANIM_OFF);
            return;
        }
        app->_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS] = brightness;
        app->setNvsParam(NVS_KEY_DISPLAY_BRIGHTNESS, brightness);
        app->applyDisplayIdleSettings();
    }

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingAdaptiveBrightnessValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    enabled = (lv_obj_get_state(app->_displayAdaptiveBrightnessSwitch) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_DISPLAY_ADAPTIVE] = enabled;
    app->setNvsParam(NVS_KEY_DISPLAY_ADAPTIVE, enabled ? 1 : 0);
    app->applyDisplayIdleSettings();

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingScreensaverValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    enabled = (lv_obj_get_state(app->_displayScreensaverSwitch) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_DISPLAY_SCREENSAVER] = enabled;
    app->setNvsParam(NVS_KEY_DISPLAY_SCREENSAVER, enabled ? 1 : 0);
    app->applyDisplayIdleSettings();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingTimeoffIntervalValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    uint16_t selected_index = 0;
    int32_t selected_value = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    selected_index = lv_dropdown_get_selected(app->_displayTimeoffDropdown);
    selected_value = getDropdownValueForIndex(kDisplayTimeoffOptionsSec,
                                              sizeof(kDisplayTimeoffOptionsSec) / sizeof(kDisplayTimeoffOptionsSec[0]),
                                              selected_index);
    app->_nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF] = selected_value;
    app->setNvsParam(NVS_KEY_DISPLAY_TIMEOFF, selected_value);
    app->applyDisplayIdleSettings();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingSleepIntervalValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    uint16_t selected_index = 0;
    int32_t selected_value = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    selected_index = lv_dropdown_get_selected(app->_displaySleepDropdown);
    selected_value = getDropdownValueForIndex(kDisplaySleepOptionsSec,
                                              sizeof(kDisplaySleepOptionsSec) / sizeof(kDisplaySleepOptionsSec[0]),
                                              selected_index);
    app->_nvs_param_map[NVS_KEY_DISPLAY_SLEEP] = selected_value;
    app->setNvsParam(NVS_KEY_DISPLAY_SLEEP, selected_value);
    app->applyDisplayIdleSettings();

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingAutoTimezoneValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    enabled = (lv_obj_get_state(app->_displayAutoTimezoneSwitch) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO] = enabled;
    app->setNvsParam(NVS_KEY_DISPLAY_TZ_AUTO, enabled ? 1 : 0);

    if (enabled) {
        app->_hasAutoDetectedTimezone = false;
        app->_autoTimezoneStatus = "Auto timezone enabled";
        app->_autoTimezoneRefreshPending = true;
        if (xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_CONNECTED) {
            app->syncAutoTimezoneFromInternet();
            app->_autoTimezoneRefreshPending = false;
            app_sntp_init();
        }
    } else {
        app->_autoTimezoneRefreshPending = false;
        app->_hasAutoDetectedTimezone = false;
        app->_autoTimezoneStatus.clear();
        app->applyManualTimezonePreference();
        app_sntp_init();
    }

    app->refreshTimezoneUi();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingTimezoneValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    uint16_t selected_index = 0;
    int32_t selected_offset_minutes = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    selected_index = lv_dropdown_get_selected(app->_displayTimezoneDropdown);
    selected_offset_minutes = getTimezoneOptionForIndex(selected_index).offset_minutes;
    app->_nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE] = selected_offset_minutes;
    app->setNvsParam(NVS_KEY_DISPLAY_TIMEZONE, selected_offset_minutes);

    if (!app->_nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO]) {
        app->applyManualTimezonePreference();
        app_sntp_init();
    }

    app->refreshTimezoneUi();

end:
    return;
}
 
