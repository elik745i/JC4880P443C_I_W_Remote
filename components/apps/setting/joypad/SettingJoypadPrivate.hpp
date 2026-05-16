#if defined(APP_SETTINGS_JOYPAD_METHOD_DECLS)
    void refreshJoypadUi(void);
    void refreshJoypadCalibrationUi(const jc4880_joypad_ble_report_state_t &report);
    void ensureJoypadScreen(void);
    void ensureJoypadBleScreen(void);
    void ensureJoypadLocalScreen(void);
    bool persistJoypadConfigFromUi(void);
#elif defined(APP_SETTINGS_JOYPAD_CALLBACK_DECLS)
    static void onJoypadConfigChangedEventCallback(lv_event_t *e);
    static void onJoypadCalibrationClickedEventCallback(lv_event_t *e);
#elif defined(APP_SETTINGS_JOYPAD_MENU_STATE)
    lv_obj_t *_joypadMenuItem;
#elif defined(APP_SETTINGS_JOYPAD_MENU_INIT)
    _joypadMenuItem(nullptr),
#elif defined(APP_SETTINGS_JOYPAD_SCREEN_STATE)
    lv_obj_t *_joypadScreen;
    lv_obj_t *_joypadBleScreen;
    lv_obj_t *_joypadLocalScreen;
#elif defined(APP_SETTINGS_JOYPAD_SCREEN_INIT)
    _joypadScreen(nullptr),
    _joypadBleScreen(nullptr),
    _joypadLocalScreen(nullptr),
#elif defined(APP_SETTINGS_JOYPAD_UI_STATE)
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
    std::array<lv_obj_t *, 2> _joypadLocalTriggerBars;
    std::array<lv_obj_t *, 2> _joypadLocalShoulderIndicators;
    std::array<lv_obj_t *, 2> _joypadLocalStickBases;
    std::array<lv_obj_t *, 2> _joypadLocalStickKnobs;
    std::array<lv_obj_t *, 4> _joypadLocalDpadIndicators;
    std::array<lv_obj_t *, 4> _joypadLocalFaceIndicators;
    std::array<int16_t, 4> _joypadBlePreviewCenterAxes;
    std::array<char, 18> _joypadBlePreviewDeviceAddr;
    bool _joypadBlePreviewCenterValid;
    std::array<lv_obj_t *, JC4880_JOYPAD_BLE_CONTROL_COUNT> _joypadBleRemapDropdowns;
    std::array<lv_obj_t *, JC4880_JOYPAD_SPI_CONTROL_COUNT> _joypadManualSpiDropdowns;
    std::array<lv_obj_t *, 2> _joypadManualResistiveDropdowns;
    std::array<lv_obj_t *, JC4880_JOYPAD_BUTTON_CONTROL_COUNT> _joypadManualResistiveButtonDropdowns;
    std::array<lv_obj_t *, 2> _joypadManualMcpDropdowns;
    std::array<lv_obj_t *, JC4880_JOYPAD_SPI_CONTROL_COUNT> _joypadManualMcpButtonDropdowns;
    lv_obj_t *_joypadLocalHapticGpioDropdown;
    lv_obj_t *_joypadLocalHapticLevelDropdown;
    lv_obj_t *_joypadLocalNeopixelPowerSwitch;
    lv_obj_t *_joypadLocalNeopixelGpioDropdown;
    lv_obj_t *_joypadLocalNeopixelPaletteDropdown;
    lv_obj_t *_joypadLocalNeopixelEffectDropdown;
    lv_obj_t *_joypadLocalNeopixelBrightnessSlider;
    lv_obj_t *_joypadLocalNeopixelInfoLabel;
    std::vector<std::string> _joypadBleDeviceOptions;
#elif defined(APP_SETTINGS_JOYPAD_UI_INIT)
    _joypadBleMenuItem(nullptr),
    _joypadLocalMenuItem(nullptr),
    _joypadBleActiveSwitch(nullptr),
    _joypadManualActiveSwitch(nullptr),
    _joypadBleEnableSwitch(nullptr),
    _joypadBleDiscoverySwitch(nullptr),
    _joypadBleDeviceDropdown(nullptr),
    _joypadBleStatusLabel(nullptr),
    _joypadBleCalibrationInfoLabel(nullptr),
    _joypadBleCalibrationButton(nullptr),
    _joypadBleCalibrationButtonLabel(nullptr),
    _joypadBackendDropdown(nullptr),
    _joypadManualModeDropdown(nullptr),
    _joypadInfoLabel(nullptr),
    _joypadBleTriggerBars{},
    _joypadBleShoulderIndicators{},
    _joypadBleStickBases{},
    _joypadBleStickKnobs{},
    _joypadBleDpadIndicators{},
    _joypadBleFaceIndicators{},
    _joypadLocalTriggerBars{},
    _joypadLocalShoulderIndicators{},
    _joypadLocalStickBases{},
    _joypadLocalStickKnobs{},
    _joypadLocalDpadIndicators{},
    _joypadLocalFaceIndicators{},
    _joypadBlePreviewCenterAxes{},
    _joypadBlePreviewDeviceAddr{},
    _joypadBlePreviewCenterValid(false),
    _joypadBleRemapDropdowns{},
    _joypadManualSpiDropdowns{},
    _joypadManualResistiveDropdowns{},
    _joypadManualResistiveButtonDropdowns{},
    _joypadManualMcpDropdowns{},
    _joypadManualMcpButtonDropdowns{},
    _joypadLocalHapticGpioDropdown(nullptr),
    _joypadLocalHapticLevelDropdown(nullptr),
    _joypadLocalNeopixelPowerSwitch(nullptr),
    _joypadLocalNeopixelGpioDropdown(nullptr),
    _joypadLocalNeopixelPaletteDropdown(nullptr),
    _joypadLocalNeopixelEffectDropdown(nullptr),
    _joypadLocalNeopixelBrightnessSlider(nullptr),
    _joypadLocalNeopixelInfoLabel(nullptr),
    _joypadBleDeviceOptions(),
#endif