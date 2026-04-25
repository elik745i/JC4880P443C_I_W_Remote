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

class AppSettings: public ESP_Brookesia_PhoneApp {
public:
    AppSettings();
    ~AppSettings();

    bool run(void);
    bool back(void);
    bool close(void);
    bool handleQuickAccessAction(int action_id) override;

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
        UI_ZIGBEE_SETTING_INDEX,
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
    void refreshDisplayIdleUi(void);
    void refreshTimezoneUi(void);
    void refreshBluetoothUi(void);
    void refreshJoypadUi(void);
    void refreshJoypadCalibrationUi(const jc4880_joypad_ble_report_state_t &report);
    void refreshRadioStatusBar(void);
    void refreshZigbeeUi(void);
    void refreshSecurityUi(void);
    void refreshFirmwareUi(void);
    void refreshHardwareMonitorUi(void);
    void setBatteryHistoryExpanded(bool expanded, bool animate);
    void setHardwareTrendExpanded(HardwareTrendCardIndex index, bool expanded, bool animate);
    void applyDisplayIdleSettings(void);
    void applyManualTimezonePreference(void);
    bool syncAutoTimezoneFromInternet(void);
    void initializeDefaultNvsParams(void);
    bool factoryResetPreferences(void);
    void setWifiKeyboardVisible(bool visible);
    void setWifiApKeyboardVisible(bool visible, lv_obj_t *textarea = nullptr);
    void setBluetoothKeyboardVisible(bool visible);
    void setZigbeeKeyboardVisible(bool visible);
    void refreshWifiApUi(void);
    bool persistWifiApSettingsFromUi(bool apply_runtime);
    bool persistBluetoothNameFromUi(void);
    bool persistZigbeeNameFromUi(void);
    void handleSecurityToggleResult(device_security::LockType type, bool success);
    void ensureHardwareScreen(void);
    void ensureJoypadScreen(void);
    void ensureZigbeeScreen(void);
    void ensureSecurityScreen(void);
    bool persistJoypadConfigFromUi(void);
    void setFirmwareStatus(const std::string &status, bool is_error = false);
    void ensureFirmwareScreen(void);
    void ensureFirmwareOtaCheckOverlay(void);
    void setFirmwareProgress(int32_t percent, const std::string &phase, bool is_error = false);
    void setFirmwareOtaCheckOverlayVisible(bool visible, const std::string &status = std::string());
    void queueFirmwareUiUpdate(const char *status, int32_t percent, bool busy, bool is_error);
    void populateFirmwareDropdown(lv_obj_t *dropdown, const std::vector<FirmwareEntry_t> &entries, const char *empty_label);
    void rebuildFirmwareOtaList(void);
    void releaseFirmwareOtaResources(void);
    int getSelectedOtaFirmwareIndex(void) const;
    void setSelectedOtaFirmwareIndex(int index);
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
    bool isUiActive(void) const;
    static void firmwareUpdateTask(void *arg);
    static void applyAsyncFirmwareUiUpdate(void *arg);
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
    static void onSwitchPanelScreenSettingBluetoothValueChangeEventCallback(lv_event_t *e);
    static void onSwitchPanelScreenSettingBLESwitchValueChangeEventCallback( lv_event_t * e);
    static void onBluetoothNameTextAreaEventCallback(lv_event_t *e);
    static void onBluetoothNameSaveClickedEventCallback(lv_event_t *e);
    static void onBluetoothKeyboardEventCallback(lv_event_t *e);
    static void onBluetoothScanClickedEventCallback(lv_event_t *e);
    static void onJoypadConfigChangedEventCallback(lv_event_t *e);
    static void onJoypadCalibrationClickedEventCallback(lv_event_t *e);
    // ZigBee
    static void onZigbeeEnableSwitchValueChangeEventCallback(lv_event_t *e);
    static void onZigbeeChannelChangedEventCallback(lv_event_t *e);
    static void onZigbeePermitJoinChangedEventCallback(lv_event_t *e);
    static void onZigbeeNameTextAreaEventCallback(lv_event_t *e);
    static void onZigbeeNameSaveClickedEventCallback(lv_event_t *e);
    static void onZigbeeKeyboardEventCallback(lv_event_t *e);
    static void onSwitchPanelScreenSettingSettingsLockValueChangeEventCallback(lv_event_t *e);
    static void onSecurityToggleRequestFinished(bool success, void *user_data);
    static void onMainMenuItemClickedEventCallback(lv_event_t *e);
    static void onHardwareBatteryCardClickedEventCallback(lv_event_t *e);
    static void onHardwareTrendCardClickedEventCallback(lv_event_t *e);
    static void onFirmwareMenuClickedEventCallback(lv_event_t *e);
    static void onFirmwareSdRefreshClickedEventCallback(lv_event_t *e);
    static void onFirmwareOtaCheckClickedEventCallback(lv_event_t *e);
    static void onFirmwareSdFlashClickedEventCallback(lv_event_t *e);
    static void onFirmwareOtaFlashClickedEventCallback(lv_event_t *e);
    static void onFirmwareSelectionChangedEventCallback(lv_event_t *e);
    static void onFirmwareOtaEntryCheckedEventCallback(lv_event_t *e);
    static void onFirmwareFactoryResetClickedEventCallback(lv_event_t *e);
    static void onFirmwareFactoryResetConfirmEventCallback(lv_event_t *e);
    // Audio
    static void onSliderPanelVolumeSwitchValueChangeEventCallback( lv_event_t * e);
    static void onSliderPanelSystemVolumeValueChangeEventCallback(lv_event_t * e);
    static void onSwitchPanelScreenSettingTapSoundValueChangeEventCallback(lv_event_t *e);
    static void onSwitchPanelScreenSettingHapticFeedbackValueChangeEventCallback(lv_event_t *e);
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
    lv_obj_t *_displayAdaptiveBrightnessSwitch;
    lv_obj_t *_displayScreensaverSwitch;
    lv_obj_t *_displayTimeoffDropdown;
    lv_obj_t *_displaySleepDropdown;
    lv_obj_t *_displayAutoTimezoneSwitch;
    lv_obj_t *_displayTimezoneDropdown;
    lv_obj_t *_displayTimezoneInfoLabel;
    lv_obj_t *_audioMediaVolumeSlider;
    lv_obj_t *_audioSystemVolumeSlider;
    lv_obj_t *_audioTapSoundSwitch;
    lv_obj_t *_audioHapticFeedbackSwitch;
    lv_obj_t *_bluetoothMenuItem;
    lv_obj_t *_joypadMenuItem;
    lv_obj_t *_zigbeeMenuItem;
    lv_obj_t *_wifiMenuItem;
    lv_obj_t *_audioMenuItem;
    lv_obj_t *_displayMenuItem;
    lv_obj_t *_hardwareMenuItem;
    lv_obj_t *_securityMenuItem;
    lv_obj_t *_aboutMenuItem;
    lv_obj_t *_bluetoothInfoLabel;
    lv_obj_t *_bluetoothNameTextArea;
    lv_obj_t *_bluetoothNameSaveButton;
    lv_obj_t *_bluetoothScanButton;
    lv_obj_t *_bluetoothScanButtonLabel;
    lv_obj_t *_bluetoothScanStatusLabel;
    lv_obj_t *_bluetoothScanResultsLabel;
    lv_obj_t *_bluetoothKeyboard;
    lv_obj_t *_joypadScreen;
    lv_obj_t *_joypadBleScreen;
    lv_obj_t *_joypadLocalScreen;
    lv_obj_t *_joypadBleMenuItem;
    lv_obj_t *_joypadLocalMenuItem;
    lv_obj_t *_joypadBleActiveSwitch;
    lv_obj_t *_joypadManualActiveSwitch;
    lv_obj_t *_joypadBleEnableSwitch;
    lv_obj_t *_joypadBleDiscoverySwitch;
    lv_obj_t *_joypadBleDeviceDropdown;
    lv_obj_t *_joypadBleStatusLabel;
    lv_obj_t *_joypadBleCalibrationInfoLabel;
    lv_obj_t *_joypadBleCalibrationButton;
    lv_obj_t *_joypadBleCalibrationButtonLabel;
    lv_obj_t *_joypadBackendDropdown;
    lv_obj_t *_joypadManualModeDropdown;
    lv_obj_t *_joypadInfoLabel;
    std::array<lv_obj_t *, 2> _joypadBleTriggerBars;
    std::array<lv_obj_t *, 2> _joypadBleShoulderIndicators;
    std::array<lv_obj_t *, 2> _joypadBleStickBases;
    std::array<lv_obj_t *, 2> _joypadBleStickKnobs;
    std::array<lv_obj_t *, 4> _joypadBleDpadIndicators;
    std::array<lv_obj_t *, 4> _joypadBleFaceIndicators;
    std::array<int16_t, 4> _joypadBlePreviewCenterAxes;
    std::array<char, 18> _joypadBlePreviewDeviceAddr;
    bool _joypadBlePreviewCenterValid;
    std::array<lv_obj_t *, JC4880_JOYPAD_BLE_CONTROL_COUNT> _joypadBleRemapDropdowns;
    std::array<lv_obj_t *, JC4880_JOYPAD_SPI_CONTROL_COUNT> _joypadManualSpiDropdowns;
    std::array<lv_obj_t *, 2> _joypadManualResistiveDropdowns;
    std::array<lv_obj_t *, JC4880_JOYPAD_BUTTON_CONTROL_COUNT> _joypadManualButtonDropdowns;
    std::vector<std::string> _joypadBleDeviceOptions;
    lv_obj_t *_zigbeeEnableSwitch;
    lv_obj_t *_zigbeeNameTextArea;
    lv_obj_t *_zigbeeNameSaveButton;
    lv_obj_t *_zigbeeChannelDropdown;
    lv_obj_t *_zigbeePermitJoinDropdown;
    lv_obj_t *_zigbeeKeyboard;
    lv_obj_t *_zigbeeInfoLabel;
    lv_obj_t *_zigbeeRoleValueLabel;
    lv_obj_t *_zigbeeConfigSummaryLabel;
    lv_obj_t *_securityDeviceLockSwitch;
    lv_obj_t *_securitySettingsLockSwitch;
    lv_obj_t *_securityInfoLabel;
    lv_obj_t *_firmwareMenuItem;
    lv_obj_t *_firmwareScreen;
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
    lv_obj_t *_firmwareSdDropdown;
    lv_obj_t *_firmwareSdFlashButton;
    lv_obj_t *_firmwareOtaCheckButton;
    lv_obj_t *_firmwareOtaFlashButton;
    lv_obj_t *_firmwareCurrentVersionLabel;
    lv_obj_t *_firmwareOtaSummaryLabel;
    lv_obj_t *_firmwareOtaListContainer;
    lv_obj_t *_firmwareOtaCheckOverlay;
    lv_obj_t *_firmwareOtaCheckSpinner;
    lv_obj_t *_firmwareOtaCheckStatusLabel;
    lv_obj_t *_firmwareStatusLabel;
    lv_obj_t *_firmwareProgressBar;
    lv_obj_t *_firmwareProgressLabel;
    bool _firmwareUpdateInProgress;
    bool _firmwareOtaCheckInProgress;
    bool _bluetoothStatusIconInstalled;
    bool _zigbeeStatusIconInstalled;
    struct SecurityToggleContext {
        AppSettings *app;
        device_security::LockType type;
    };
    SecurityToggleContext _deviceLockToggleContext;
    SecurityToggleContext _settingsLockToggleContext;
    std::array<lv_obj_t *, UI_MAX_INDEX> _screen_list;
    std::vector<SavedWifiCredential> _savedWifiRenderedCredentials;
    std::vector<FirmwareEntry_t> _sdFirmwareEntries;
    std::vector<FirmwareEntry_t> _otaFirmwareEntries;
    std::vector<lv_obj_t *> _firmwareOtaCheckboxes;
    std::map<std::string, int32_t> _nvs_param_map;
    int _selectedOtaFirmwareIndex;
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
