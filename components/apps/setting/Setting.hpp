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
#include "lvgl.h"
#include "esp_brookesia.hpp"
#include "device_security.hpp"

class AppSettings: public ESP_Brookesia_PhoneApp {
public:
    AppSettings();
    ~AppSettings();

    bool run(void);
    bool back(void);
    bool close(void);

    bool init(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    typedef enum {
        UI_MAIN_SETTING_INDEX = 0,
        UI_WIFI_SCAN_INDEX,
        UI_WIFI_CONNECT_INDEX,
        UI_BLUETOOTH_SETTING_INDEX,
        UI_SECURITY_SETTING_INDEX,
        UI_VOLUME_SETTING_INDEX,
        UI_BRIGHTNESS_SETTING_INDEX,
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

    /* Operations */
    // UI
    void extraUiInit(void);
    void processWifiConnect(WifiConnectState_t state);
    void initWifiListButton(lv_obj_t* lv_label_ssid, lv_obj_t* lv_img_wifi_lock, lv_obj_t* lv_wifi_img,
                              lv_obj_t *lv_wifi_connect, uint8_t* ssid, bool psk, WifiSignalStrengthLevel_t signal_strength);
    void deinitWifiListButton(void);
    void refreshSavedWifiUi(void);
    void refreshDisplayIdleUi(void);
    void refreshTimezoneUi(void);
    void refreshBluetoothUi(void);
    void refreshSecurityUi(void);
    void refreshFirmwareUi(void);
    void refreshHardwareMonitorUi(void);
    void applyDisplayIdleSettings(void);
    void applyManualTimezonePreference(void);
    bool syncAutoTimezoneFromInternet(void);
    void initializeDefaultNvsParams(void);
    bool factoryResetPreferences(void);
    void setWifiKeyboardVisible(bool visible);
    void updateWifiPasswordVisibility(bool visible);
    void handleSecurityToggleResult(device_security::LockType type, bool success);
    void setFirmwareStatus(const std::string &status, bool is_error = false);
    void setFirmwareProgress(int32_t percent, const std::string &phase, bool is_error = false);
    void queueFirmwareUiUpdate(const char *status, int32_t percent, bool busy, bool is_error);
    void populateFirmwareDropdown(lv_obj_t *dropdown, const std::vector<FirmwareEntry_t> &entries, const char *empty_label);
    bool scanSdFirmwareEntries(void);
    bool fetchGithubFirmwareEntries(void);
    bool probeFirmwareFile(const std::string &path, FirmwareEntry_t &entry);
    bool hasOtaFlashSupport(void) const;
    std::string getCurrentFirmwareVersion(void) const;
    std::string formatFirmwareLabel(const FirmwareEntry_t &entry) const;
    static int compareVersionStrings(const std::string &lhs, const std::string &rhs);
    bool flashFirmwareEntry(const FirmwareEntry_t &entry, FirmwareUpdateSource_t source);
    bool flashFirmwareFromFile(const FirmwareEntry_t &entry, std::string &error_message);
    bool flashFirmwareFromUrl(const FirmwareEntry_t &entry, std::string &error_message);
    bool validateFirmwareImageHeader(const uint8_t *data, size_t data_len, const std::string &source_label,
                                     std::string &error_message, bool &header_checked);
    void persistPendingReleaseNotes(const FirmwareEntry_t &entry);
    static void firmwareUpdateTask(void *arg);
    static void applyAsyncFirmwareUiUpdate(void *arg);
    // NVS Parameters
    bool loadNvsParam(void);
    bool setNvsParam(std::string key, int value);
    bool loadNvsStringParam(const char *key, char *buffer, size_t buffer_size);
    bool setNvsStringParam(const char *key, const char *value);
    bool clearSavedWifiCredentials(void);
    void updateUiByNvsParam(void);
    // WiFi
    esp_err_t initWifi(void);
    void requestWifiConnect(const char *reason);
    bool restoreWifiCredentials(void);
    void startWifiScan(void);
    void stopWifiScan(void);
    void scanWifiAndUpdateUi(void);
    WifiSignalStrengthLevel_t wifiSignalStrengthFromRssi(int rssi) const;
    void refreshWifiStatusBar(void);
    // Smart Gadget
    // void updateGadgetTime(struct tm timeinfo);

    /* Task */
    static void euiRefresTask(void *arg);
    static void euiBatteryTask(void *arg);
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
    static void onWifiPasswordToggleClickedEventCallback(lv_event_t *e);
    static void onForgetSavedWifiClickedEventCallback(lv_event_t *e);
    // Bluetooth
    static void onSwitchPanelScreenSettingBluetoothValueChangeEventCallback(lv_event_t *e);
    static void onSwitchPanelScreenSettingBLESwitchValueChangeEventCallback( lv_event_t * e);
    static void onSwitchPanelScreenSettingSettingsLockValueChangeEventCallback(lv_event_t *e);
    static void onSecurityToggleRequestFinished(bool success, void *user_data);
    static void onMainMenuItemClickedEventCallback(lv_event_t *e);
    static void onFirmwareMenuClickedEventCallback(lv_event_t *e);
    static void onFirmwareSdRefreshClickedEventCallback(lv_event_t *e);
    static void onFirmwareOtaCheckClickedEventCallback(lv_event_t *e);
    static void onFirmwareSdFlashClickedEventCallback(lv_event_t *e);
    static void onFirmwareOtaFlashClickedEventCallback(lv_event_t *e);
    static void onFirmwareSelectionChangedEventCallback(lv_event_t *e);
    static void onFirmwareFactoryResetClickedEventCallback(lv_event_t *e);
    static void onFirmwareFactoryResetConfirmEventCallback(lv_event_t *e);
    // Audio
    static void onSliderPanelVolumeSwitchValueChangeEventCallback( lv_event_t * e);
    // Brightness
    static void onSliderPanelLightSwitchValueChangeEventCallback( lv_event_t * e);
    static void onSwitchPanelScreenSettingAdaptiveBrightnessValueChangeEventCallback(lv_event_t *e);
    static void onSwitchPanelScreenSettingScreensaverValueChangeEventCallback(lv_event_t *e);
    static void onDropdownPanelScreenSettingTimeoffIntervalValueChangeEventCallback(lv_event_t *e);
    static void onDropdownPanelScreenSettingSleepIntervalValueChangeEventCallback(lv_event_t *e);
    static void onSwitchPanelScreenSettingAutoTimezoneValueChangeEventCallback(lv_event_t *e);
    static void onDropdownPanelScreenSettingTimezoneValueChangeEventCallback(lv_event_t *e);

    bool _is_ui_resumed;
    bool _is_ui_del;
    SettingScreenIndex_t _screen_index;
    WifiSignalStrengthLevel_t _wifi_signal_strength_level;
    lv_obj_t *_panel_wifi_connect;
    lv_obj_t *_spinner_wifi_connect;
    lv_obj_t *_img_wifi_connect;
    lv_obj_t *_savedWifiPanel;
    lv_obj_t *_savedWifiValueLabel;
    lv_obj_t *_savedWifiForgetButton;
    lv_obj_t *_wifiPasswordToggleButton;
    lv_obj_t *_wifiPasswordToggleLabel;
    lv_obj_t *_displayAdaptiveBrightnessSwitch;
    lv_obj_t *_displayScreensaverSwitch;
    lv_obj_t *_displayTimeoffDropdown;
    lv_obj_t *_displaySleepDropdown;
    lv_obj_t *_displayAutoTimezoneSwitch;
    lv_obj_t *_displayTimezoneDropdown;
    lv_obj_t *_displayTimezoneInfoLabel;
    lv_obj_t *_bluetoothMenuItem;
    lv_obj_t *_wifiMenuItem;
    lv_obj_t *_audioMenuItem;
    lv_obj_t *_displayMenuItem;
    lv_obj_t *_hardwareMenuItem;
    lv_obj_t *_securityMenuItem;
    lv_obj_t *_aboutMenuItem;
    lv_obj_t *_bluetoothInfoLabel;
    lv_obj_t *_securityDeviceLockSwitch;
    lv_obj_t *_securitySettingsLockSwitch;
    lv_obj_t *_securityInfoLabel;
    lv_obj_t *_firmwareMenuItem;
    lv_obj_t *_firmwareScreen;
    lv_obj_t *_hardwareScreen;
    lv_obj_t *_securityScreen;
    lv_obj_t *_hardwareCpuSpeedValueLabel;
    lv_obj_t *_hardwareCpuSpeedDetailLabel;
    lv_obj_t *_hardwareCpuSpeedBar;
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
    lv_obj_t *_firmwareSdDropdown;
    lv_obj_t *_firmwareSdFlashButton;
    lv_obj_t *_firmwareOtaDropdown;
    lv_obj_t *_firmwareOtaFlashButton;
    lv_obj_t *_firmwareCurrentVersionLabel;
    lv_obj_t *_firmwareStatusLabel;
    lv_obj_t *_firmwareProgressBar;
    lv_obj_t *_firmwareProgressLabel;
    bool _firmwareUpdateInProgress;
    bool _isWifiPasswordVisible;
    struct SecurityToggleContext {
        AppSettings *app;
        device_security::LockType type;
    };
    SecurityToggleContext _deviceLockToggleContext;
    SecurityToggleContext _settingsLockToggleContext;
    std::array<lv_obj_t *, UI_MAX_INDEX> _screen_list;
    std::vector<FirmwareEntry_t> _sdFirmwareEntries;
    std::vector<FirmwareEntry_t> _otaFirmwareEntries;
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
    bool charge_flag;
    adc_oneshot_unit_handle_t adc2_handle;
    adc_cali_handle_t adc2_cali_handle;

    adc_battery_estimation_handle_t adc_battery_estimation_handle;
};
