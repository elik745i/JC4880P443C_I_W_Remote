/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_event.h"
#include "adc_battery_estimation.h"
#include "esp_wifi.h"
#include "joypad_runtime.h"
#include "lvgl.h"
#include "esp_brookesia.hpp"
#include "device_security.hpp"

namespace jc4880::imu {
struct ImuSample;
}

class AppSettings: public ESP_Brookesia_PhoneApp {
public:
    AppSettings();
    ~AppSettings();

    bool run(void);
    bool back(void);
    bool close(void);
    bool handleQuickAccessAction(int action_id) override;
    bool debugQueueBuildGpioControlScreen(void);

    bool init(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    typedef enum {
        UI_MAIN_SETTING_INDEX = 0,
        UI_WIFI_SCAN_INDEX,
        UI_WIFI_CONNECT_INDEX,
        UI_BLUETOOTH_SETTING_INDEX,
        UI_JOYPAD_SETTING_INDEX,
        UI_IMU_SETTING_INDEX,
        UI_LORA_SETTING_INDEX,
        UI_ZIGBEE_SETTING_INDEX,
        UI_SECURITY_SETTING_INDEX,
        UI_VOLUME_SETTING_INDEX,
        UI_BRIGHTNESS_SETTING_INDEX,
        UI_GPIO_TEST_SETTING_INDEX,
        UI_HARDWARE_SETTING_INDEX,
        UI_FIRMWARE_SETTING_INDEX,
        UI_ABOUT_SETTING_INDEX,
        UI_MAX_INDEX,
    } SettingScreenIndex_t;

    typedef struct {
        std::string label;
        std::string version;
        std::string path_or_url;
        std::string project_name;
        std::string notes;
        std::string release_notes;
        size_t size_bytes;
        bool is_current;
        bool is_newer;
        bool is_valid;
    } FirmwareEntry_t;

    typedef enum {
        FIRMWARE_UPDATE_SOURCE_SD = 0,
        FIRMWARE_UPDATE_SOURCE_OTA,
    } FirmwareUpdateSource_t;

    struct FirmwareUpdateTaskContext {
        AppSettings *app;
        FirmwareEntry_t entry;
        FirmwareUpdateSource_t source;
    };

    struct AsyncFirmwareUiContext {
        AppSettings *app;
        char status[224];
        int32_t percent;
        bool busy;
        bool is_error;
    };

    struct AsyncOtaAvailabilityContext {
        AppSettings *app;
        std::vector<FirmwareEntry_t> entries;
        bool success;
    };

    typedef enum {
        WIFI_SIGNAL_STRENGTH_NONE = 0,
        WIFI_SIGNAL_STRENGTH_WEAK = 1,
        WIFI_SIGNAL_STRENGTH_MODERATE = 2,
        WIFI_SIGNAL_STRENGTH_GOOD = 3,
    } WifiSignalStrengthLevel_t;

    typedef enum {
        WIFI_CONNECT_HIDE = 0,
        WIFI_CONNECT_RUNNING,
        WIFI_CONNECT_SUCCESS,
        WIFI_CONNECT_FAIL,
    } WifiConnectState_t;

    struct SavedWifiCredential {
        std::string ssid;
        std::string password;
    };

    enum HardwareTrendCardIndex {
        HARDWARE_TREND_CPU_LOAD = 0,
        HARDWARE_TREND_SRAM,
        HARDWARE_TREND_PSRAM,
        HARDWARE_TREND_CPU_TEMP,
        HARDWARE_TREND_WIFI,
        HARDWARE_TREND_CARD_COUNT,
    };

    struct HardwareTrendUi {
        lv_obj_t *card;
        lv_obj_t *expandLabel;
        lv_obj_t *expandedArea;
        lv_obj_t *historyTitleLabel;
        lv_obj_t *historySummaryLabel;
        lv_obj_t *historyChart;
        lv_chart_series_t *historySeries;
        lv_obj_t *historyLeftLabel;
        lv_obj_t *historyRightLabel;
        lv_obj_t *historyFooterLabel;
        bool expanded;
    };

    enum class HardwareGpioTestMode : uint8_t {
        Output = 0,
        Input = 1,
        Pwm = 2,
        Wave = 3,
        Timer = 4,
        AlarmClock = 5,
    };

    enum class HardwareGpioTestWaveform : uint8_t {
        Sine = 0,
        Sharp = 1,
        Step = 2,
    };

    static constexpr size_t kHardwareGpioTestPinCount = 12;

    struct WifiConnectTaskContext {
        AppSettings *app;
        SavedWifiCredential credential;
        SavedWifiCredential previous_credential;
        bool has_previous_connection;
        bool dismiss_keyboard;
        bool navigate_back_on_success;
    };

    /* Operations */
    // UI
    void extraUiInit(void);
    void processWifiConnect(WifiConnectState_t state);
    void initWifiListButton(lv_obj_t* lv_label_ssid, lv_obj_t* lv_img_wifi_lock, lv_obj_t* lv_wifi_img,
                              lv_obj_t *lv_wifi_connect, uint8_t* ssid, bool psk, WifiSignalStrengthLevel_t signal_strength);
    void deinitWifiListButton(void);
    void refreshSavedWifiUi(void);
    void refreshAboutWifiUi(void);
    void refreshAboutPeripheralUi(void);
    void refreshPeripheralMenuVisibility(void);
#define APP_SETTINGS_DISPLAY_METHOD_DECLS
#include "display/SettingDisplayPrivate.hpp"
#undef APP_SETTINGS_DISPLAY_METHOD_DECLS
#define APP_SETTINGS_BLUETOOTH_METHOD_DECLS
#include "bluetooth/SettingBluetoothPrivate.hpp"
#undef APP_SETTINGS_BLUETOOTH_METHOD_DECLS
#define APP_SETTINGS_JOYPAD_METHOD_DECLS
#include "joypad/SettingJoypadPrivate.hpp"
#undef APP_SETTINGS_JOYPAD_METHOD_DECLS
#define APP_SETTINGS_IMU_METHOD_DECLS
#include "imu/SettingImuPrivate.hpp"
#undef APP_SETTINGS_IMU_METHOD_DECLS
 #define APP_SETTINGS_LORA_METHOD_DECLS
#include "lora/SettingLoraPrivate.hpp"
#undef APP_SETTINGS_LORA_METHOD_DECLS
    void refreshRadioStatusBar(void);
#define APP_SETTINGS_ZIGBEE_METHOD_DECLS
#include "zigbee/SettingZigbeePrivate.hpp"
#undef APP_SETTINGS_ZIGBEE_METHOD_DECLS
#define APP_SETTINGS_SECURITY_METHOD_DECLS
#include "security/SettingSecurityPrivate.hpp"
#undef APP_SETTINGS_SECURITY_METHOD_DECLS
#define APP_SETTINGS_FIRMWARE_METHOD_DECLS
#include "firmware/SettingFirmwarePrivate.hpp"
#undef APP_SETTINGS_FIRMWARE_METHOD_DECLS
    void refreshHardwareMonitorUi(void);
#define APP_SETTINGS_GPIO_METHOD_DECLS
#include "gpio/SettingGpioControlPrivate.hpp"
#undef APP_SETTINGS_GPIO_METHOD_DECLS
    void setBatteryHistoryExpanded(bool expanded, bool animate);
    void setHardwareTrendExpanded(HardwareTrendCardIndex index, bool expanded, bool animate);
    void initializeDefaultNvsParams(void);
    bool factoryResetPreferences(void);
    void setWifiKeyboardVisible(bool visible);
    void setWifiApKeyboardVisible(bool visible, lv_obj_t *textarea = nullptr);
    void refreshWifiApUi(void);
    bool persistWifiApSettingsFromUi(bool apply_runtime);
#define APP_SETTINGS_AUDIO_METHOD_DECLS
#include "audio/SettingAudioPrivate.hpp"
#undef APP_SETTINGS_AUDIO_METHOD_DECLS
    void ensureHardwareScreen(void);
    void ensureZigbeeScreen(void);
    void ensureSecurityScreen(void);
    bool isUiActive(void) const;
    // NVS Parameters
    bool loadNvsParam(void);
    bool setNvsParam(std::string key, int value);
    bool loadNvsStringParam(const char *key, char *buffer, size_t buffer_size);
    bool setNvsStringParam(const char *key, const char *value);
    SavedWifiCredential sanitizeWifiCredential(const char *ssid, const char *password) const;
    SavedWifiCredential sanitizeWifiApCredential(const char *ssid, const char *password) const;
    void populateWifiStaConfig(wifi_config_t &wifi_config, const SavedWifiCredential &credential) const;
    void populateWifiApConfig(wifi_config_t &wifi_config, const SavedWifiCredential &credential) const;
    std::vector<SavedWifiCredential> loadSavedWifiCredentials(void) const;
    bool saveSavedWifiCredentials(const std::vector<SavedWifiCredential> &credentials);
    bool loadLatestSavedWifiCredential(SavedWifiCredential &credential);
    bool selectAutoConnectWifiCredential(SavedWifiCredential &credential);
    bool persistLatestSavedWifiCredential(const SavedWifiCredential *credential);
    bool rememberWifiCredential(const SavedWifiCredential &credential);
    bool clearSavedWifiCredentials(void);
    bool forgetSavedWifiCredential(const std::string &ssid);
    bool launchWifiConnection(const SavedWifiCredential &credential, bool dismiss_keyboard, bool navigate_back_on_success);
    void updateUiByNvsParam(void);
    // WiFi
    esp_err_t initWifi(void);
    esp_err_t applyWifiOperatingMode(bool reconnect_sta, const char *reason);
    void requestWifiConnect(const char *reason);
    bool restoreWifiCredentials(void);
    void startWifiScan(void);
    void stopWifiScan(void);
    void scanWifiAndUpdateUi(void);
    WifiSignalStrengthLevel_t wifiSignalStrengthFromRssi(int rssi) const;
    std::map<std::string, int> getScannedWifiRssiBySsid(void) const;
    void updateSavedWifiPanelLayout(bool list_visible, size_t row_count);
    size_t getSavedWifiRenderedIndexFromEventTarget(lv_obj_t *target) const;
    // Smart Gadget
    // void updateGadgetTime(struct tm timeinfo);

    /* Task */
    static void euiRefresTask(void *arg);
    static void wifiScanTask(void *arg);
    static void wifiConnectTask(void *arg);

    /* Event Handler */
    // WiFi
    static void wifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

    /* UI Event Callback */
    // Main
    static void onScreenLoadEventCallback( lv_event_t * e);
    // WiFi
    static void onSwitchPanelScreenSettingWiFiSwitchValueChangeEventCallback( lv_event_t * e);
    static void onButtonWifiListClickedEventCallback(lv_event_t * e);
    static void onKeyboardScreenSettingVerificationClickedEventCallback(lv_event_t *e);
    static void onWifiPasswordFieldEventCallback(lv_event_t *e);
    static void onWifiScanClickedEventCallback(lv_event_t *e);
    static void onWifiApSwitchValueChangeEventCallback(lv_event_t *e);
    static void onWifiApFieldEventCallback(lv_event_t *e);
    static void onWifiApSaveClickedEventCallback(lv_event_t *e);
    static void onWifiApKeyboardEventCallback(lv_event_t *e);
    static void onWifiKeyboardBackdropClickedEventCallback(lv_event_t *e);
    static void onSavedWifiDropdownClickedEventCallback(lv_event_t *e);
    static void onConnectSavedWifiClickedEventCallback(lv_event_t *e);
    static void onForgetSavedWifiClickedEventCallback(lv_event_t *e);
    // Bluetooth
#define APP_SETTINGS_BLUETOOTH_CALLBACK_DECLS
#include "bluetooth/SettingBluetoothPrivate.hpp"
#undef APP_SETTINGS_BLUETOOTH_CALLBACK_DECLS
#define APP_SETTINGS_JOYPAD_CALLBACK_DECLS
#include "joypad/SettingJoypadPrivate.hpp"
#undef APP_SETTINGS_JOYPAD_CALLBACK_DECLS
#define APP_SETTINGS_IMU_CALLBACK_DECLS
#include "imu/SettingImuPrivate.hpp"
#undef APP_SETTINGS_IMU_CALLBACK_DECLS
#define APP_SETTINGS_LORA_CALLBACK_DECLS
#include "lora/SettingLoraPrivate.hpp"
#undef APP_SETTINGS_LORA_CALLBACK_DECLS
    // ZigBee
#define APP_SETTINGS_ZIGBEE_CALLBACK_DECLS
#include "zigbee/SettingZigbeePrivate.hpp"
#undef APP_SETTINGS_ZIGBEE_CALLBACK_DECLS
#define APP_SETTINGS_SECURITY_CALLBACK_DECLS
#include "security/SettingSecurityPrivate.hpp"
#undef APP_SETTINGS_SECURITY_CALLBACK_DECLS
    static void onMainMenuItemClickedEventCallback(lv_event_t *e);
    static void onHardwareBatteryCardClickedEventCallback(lv_event_t *e);
    static void onHardwareTrendCardClickedEventCallback(lv_event_t *e);
#define APP_SETTINGS_GPIO_CALLBACK_DECLS
#include "gpio/SettingGpioControlPrivate.hpp"
#undef APP_SETTINGS_GPIO_CALLBACK_DECLS
#define APP_SETTINGS_FIRMWARE_CALLBACK_DECLS
#include "firmware/SettingFirmwarePrivate.hpp"
#undef APP_SETTINGS_FIRMWARE_CALLBACK_DECLS
#define APP_SETTINGS_AUDIO_CALLBACK_DECLS
#include "audio/SettingAudioPrivate.hpp"
#undef APP_SETTINGS_AUDIO_CALLBACK_DECLS
    static void onDropdownJoypadLocalHapticGpioValueChangeEventCallback(lv_event_t *e);
    static void onDropdownJoypadLocalHapticLevelValueChangeEventCallback(lv_event_t *e);
#define APP_SETTINGS_DISPLAY_CALLBACK_DECLS
#include "display/SettingDisplayPrivate.hpp"
#undef APP_SETTINGS_DISPLAY_CALLBACK_DECLS

    bool _is_ui_resumed;
    bool _is_ui_del;
    SettingScreenIndex_t _screen_index;
    WifiSignalStrengthLevel_t _wifi_signal_strength_level;
    lv_obj_t *_panel_wifi_connect;
    lv_obj_t *_spinner_wifi_connect;
    lv_obj_t *_img_wifi_connect;
    lv_obj_t *_savedWifiPanel;
    lv_obj_t *_wifiScanButton;
    lv_obj_t *_wifiScanButtonLabel;
    lv_obj_t *_savedWifiTitleLabel;
    lv_obj_t *_savedWifiExpandButton;
    lv_obj_t *_savedWifiExpandLabel;
    lv_obj_t *_savedWifiListContainer;
    lv_obj_t *_wifiApPanel;
    lv_obj_t *_wifiApSwitch;
    lv_obj_t *_wifiApStatusLabel;
    lv_obj_t *_wifiApSsidTextArea;
    lv_obj_t *_wifiApPasswordTextArea;
    lv_obj_t *_wifiApSaveButton;
    lv_obj_t *_wifiApKeyboard;
    lv_obj_t *_wifiApKeyboardTarget;
    bool _savedWifiListExpanded;
    bool _suppressDisconnectRecovery;
    std::string _savedWifiUiStateKey;
    std::string _wifiScanUiStateKey;
    lv_obj_t *_aboutWifiValueLabel;
    lv_obj_t *_aboutPeripheralValueLabel;
#define APP_SETTINGS_DISPLAY_UI_STATE
#include "display/SettingDisplayPrivate.hpp"
#undef APP_SETTINGS_DISPLAY_UI_STATE
#define APP_SETTINGS_DISPLAY_TIMER_STATE
#include "display/SettingDisplayPrivate.hpp"
#undef APP_SETTINGS_DISPLAY_TIMER_STATE
#define APP_SETTINGS_IMU_TIMER_STATE
#include "imu/SettingImuPrivate.hpp"
#undef APP_SETTINGS_IMU_TIMER_STATE
#define APP_SETTINGS_LORA_TIMER_STATE
#include "lora/SettingLoraPrivate.hpp"
#undef APP_SETTINGS_LORA_TIMER_STATE
#define APP_SETTINGS_AUDIO_UI_STATE
#include "audio/SettingAudioPrivate.hpp"
#undef APP_SETTINGS_AUDIO_UI_STATE
#define APP_SETTINGS_BLUETOOTH_MENU_STATE
#include "bluetooth/SettingBluetoothPrivate.hpp"
#undef APP_SETTINGS_BLUETOOTH_MENU_STATE
#define APP_SETTINGS_JOYPAD_MENU_STATE
#include "joypad/SettingJoypadPrivate.hpp"
#undef APP_SETTINGS_JOYPAD_MENU_STATE
#define APP_SETTINGS_IMU_MENU_STATE
#include "imu/SettingImuPrivate.hpp"
#undef APP_SETTINGS_IMU_MENU_STATE
#define APP_SETTINGS_LORA_MENU_STATE
#include "lora/SettingLoraPrivate.hpp"
#undef APP_SETTINGS_LORA_MENU_STATE
#define APP_SETTINGS_ZIGBEE_MENU_STATE
#include "zigbee/SettingZigbeePrivate.hpp"
#undef APP_SETTINGS_ZIGBEE_MENU_STATE
    lv_obj_t *_wifiMenuItem;
#define APP_SETTINGS_AUDIO_MENU_STATE
#include "audio/SettingAudioPrivate.hpp"
#undef APP_SETTINGS_AUDIO_MENU_STATE
#define APP_SETTINGS_DISPLAY_MENU_STATE
#include "display/SettingDisplayPrivate.hpp"
#undef APP_SETTINGS_DISPLAY_MENU_STATE
#define APP_SETTINGS_GPIO_MENU_STATE
#include "gpio/SettingGpioControlPrivate.hpp"
#undef APP_SETTINGS_GPIO_MENU_STATE
    lv_obj_t *_hardwareMenuItem;
#define APP_SETTINGS_SECURITY_MENU_STATE
#include "security/SettingSecurityPrivate.hpp"
#undef APP_SETTINGS_SECURITY_MENU_STATE
    lv_obj_t *_aboutMenuItem;
#define APP_SETTINGS_BLUETOOTH_UI_STATE
#include "bluetooth/SettingBluetoothPrivate.hpp"
#undef APP_SETTINGS_BLUETOOTH_UI_STATE
#define APP_SETTINGS_JOYPAD_SCREEN_STATE
#include "joypad/SettingJoypadPrivate.hpp"
#undef APP_SETTINGS_JOYPAD_SCREEN_STATE
#define APP_SETTINGS_IMU_SCREEN_STATE
#include "imu/SettingImuPrivate.hpp"
#undef APP_SETTINGS_IMU_SCREEN_STATE
#define APP_SETTINGS_LORA_SCREEN_STATE
#include "lora/SettingLoraPrivate.hpp"
#undef APP_SETTINGS_LORA_SCREEN_STATE
#define APP_SETTINGS_GPIO_SCREEN_STATE
#include "gpio/SettingGpioControlPrivate.hpp"
#undef APP_SETTINGS_GPIO_SCREEN_STATE
#define APP_SETTINGS_JOYPAD_UI_STATE
#include "joypad/SettingJoypadPrivate.hpp"
#undef APP_SETTINGS_JOYPAD_UI_STATE
#define APP_SETTINGS_IMU_UI_STATE
#include "imu/SettingImuPrivate.hpp"
#undef APP_SETTINGS_IMU_UI_STATE
#define APP_SETTINGS_LORA_UI_STATE
#include "lora/SettingLoraPrivate.hpp"
#undef APP_SETTINGS_LORA_UI_STATE
#define APP_SETTINGS_GPIO_UI_STATE
#include "gpio/SettingGpioControlPrivate.hpp"
#undef APP_SETTINGS_GPIO_UI_STATE
#define APP_SETTINGS_ZIGBEE_UI_STATE
#include "zigbee/SettingZigbeePrivate.hpp"
#undef APP_SETTINGS_ZIGBEE_UI_STATE
#define APP_SETTINGS_SECURITY_UI_STATE
#include "security/SettingSecurityPrivate.hpp"
#undef APP_SETTINGS_SECURITY_UI_STATE
#define APP_SETTINGS_FIRMWARE_MENU_STATE
#include "firmware/SettingFirmwarePrivate.hpp"
#undef APP_SETTINGS_FIRMWARE_MENU_STATE
#define APP_SETTINGS_FIRMWARE_SCREEN_STATE
#include "firmware/SettingFirmwarePrivate.hpp"
#undef APP_SETTINGS_FIRMWARE_SCREEN_STATE
    lv_obj_t *_hardwareScreen;
    lv_obj_t *_securityScreen;
    lv_obj_t *_zigbeeScreen;
    lv_obj_t *_hardwareCpuSpeedValueLabel;
    lv_obj_t *_hardwareCpuSpeedDetailLabel;
    lv_obj_t *_hardwareCpuSpeedBar;
    lv_obj_t *_hardwareBatteryCard;
    lv_obj_t *_hardwareBatteryValueLabel;
    lv_obj_t *_hardwareBatteryDetailLabel;
    lv_obj_t *_hardwareBatteryBar;
    lv_obj_t *_hardwareBatteryExpandedArea;
    lv_obj_t *_hardwareBatteryExpandLabel;
    lv_obj_t *_hardwareBatteryHistoryTitleLabel;
    lv_obj_t *_hardwareBatteryHistorySummaryLabel;
    lv_obj_t *_hardwareBatteryHistoryChart;
    lv_chart_series_t *_hardwareBatteryHistorySeries;
    lv_obj_t *_hardwareBatteryHistoryLeftLabel;
    lv_obj_t *_hardwareBatteryHistoryRightLabel;
    lv_obj_t *_hardwareBatteryHistoryFooterLabel;
    bool _hardwareBatteryExpanded;
    std::array<HardwareTrendUi, HARDWARE_TREND_CARD_COUNT> _hardwareTrendUi;
    uint8_t *_hardwareFastHistoryScratch;
    uint8_t *_hardwareSlowHistoryScratch;
    lv_obj_t *_hardwareCpuTempValueLabel;
    lv_obj_t *_hardwareCpuTempDetailLabel;
    lv_obj_t *_hardwareCpuTempBar;
    lv_obj_t *_hardwareSramValueLabel;
    lv_obj_t *_hardwareSramDetailLabel;
    lv_obj_t *_hardwareSramBar;
    lv_obj_t *_hardwarePsramValueLabel;
    lv_obj_t *_hardwarePsramDetailLabel;
    lv_obj_t *_hardwarePsramBar;
    lv_obj_t *_hardwareSdValueLabel;
    lv_obj_t *_hardwareSdDetailLabel;
    lv_obj_t *_hardwareSdBar;
    lv_obj_t *_hardwareWifiValueLabel;
    lv_obj_t *_hardwareWifiDetailLabel;
    lv_obj_t *_hardwareWifiBar;
#define APP_SETTINGS_FIRMWARE_UI_STATE
#include "firmware/SettingFirmwarePrivate.hpp"
#undef APP_SETTINGS_FIRMWARE_UI_STATE
#define APP_SETTINGS_FIRMWARE_RUNTIME_STATE
#include "firmware/SettingFirmwarePrivate.hpp"
#undef APP_SETTINGS_FIRMWARE_RUNTIME_STATE
#define APP_SETTINGS_BLUETOOTH_RUNTIME_STATE
#include "bluetooth/SettingBluetoothPrivate.hpp"
#undef APP_SETTINGS_BLUETOOTH_RUNTIME_STATE
#define APP_SETTINGS_ZIGBEE_RUNTIME_STATE
#include "zigbee/SettingZigbeePrivate.hpp"
#undef APP_SETTINGS_ZIGBEE_RUNTIME_STATE
#define APP_SETTINGS_SECURITY_RUNTIME_STATE
#include "security/SettingSecurityPrivate.hpp"
#undef APP_SETTINGS_SECURITY_RUNTIME_STATE
    std::array<lv_obj_t *, UI_MAX_INDEX> _screen_list;
    std::vector<SavedWifiCredential> _savedWifiRenderedCredentials;
    std::map<std::string, int32_t> _nvs_param_map;
    bool _autoTimezoneRefreshPending;
    bool _hasAutoDetectedTimezone;
    int32_t _autoDetectedTimezoneOffsetMinutes;
    std::string _autoTimezoneStatus;
    const ESP_Brookesia_StatusBar *status_bar; 
    const ESP_Brookesia_RecentsScreen *backstage;

    int adc_raw[2][10];
    int voltage[2][10];
    bool do_calibration2;
    adc_oneshot_unit_handle_t adc2_handle;
    adc_cali_handle_t adc2_cali_handle;
};
