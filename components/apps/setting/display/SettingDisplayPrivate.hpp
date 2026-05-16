#if defined(APP_SETTINGS_DISPLAY_METHOD_DECLS)
    void refreshDisplayIdleUi(void);
    void refreshDisplayAutorotateUi(void);
    void updateDisplayAutorotateFromSample(const jc4880::imu::ImuSample *sample, bool sample_ok);
    void requestDisplayOrientationPreview(int32_t orientation_degrees);
    void updateDisplayOrientationPreviewPopup(void);
    void finishDisplayOrientationPreview(bool keep_orientation);
    void applyNeopixelConfig(void);
    void refreshTimezoneUi(void);
    void applyDisplayIdleSettings(void);
    void applyManualTimezonePreference(void);
    bool syncAutoTimezoneFromInternet(void);
#elif defined(APP_SETTINGS_DISPLAY_CALLBACK_DECLS)
    static void onSliderPanelLightSwitchValueChangeEventCallback(lv_event_t *e);
    static void onSwitchPanelScreenSettingNeopixelPowerValueChangeEventCallback(lv_event_t *e);
    static void onDropdownPanelScreenSettingNeopixelGpioValueChangeEventCallback(lv_event_t *e);
    static void onDropdownPanelScreenSettingNeopixelPaletteValueChangeEventCallback(lv_event_t *e);
    static void onDropdownPanelScreenSettingNeopixelEffectValueChangeEventCallback(lv_event_t *e);
    static void onSliderPanelScreenSettingNeopixelBrightnessValueChangeEventCallback(lv_event_t *e);
    static void onSwitchPanelScreenSettingAdaptiveBrightnessValueChangeEventCallback(lv_event_t *e);
    static void onSwitchPanelScreenSettingScreensaverValueChangeEventCallback(lv_event_t *e);
    static void onDropdownPanelScreenSettingTimeoffIntervalValueChangeEventCallback(lv_event_t *e);
    static void onSwitchPanelScreenSettingTimeoffInGameValueChangeEventCallback(lv_event_t *e);
    static void onDropdownPanelScreenSettingSleepIntervalValueChangeEventCallback(lv_event_t *e);
    static void onDisplayOrientationPreviewPopupEventCallback(lv_event_t *e);
    static void onDisplayOrientationPreviewTimerCallback(lv_timer_t *timer);
    static void onDropdownPanelScreenSettingOrientationValueChangeEventCallback(lv_event_t *e);
    static void onSwitchPanelScreenSettingAutorotateValueChangeEventCallback(lv_event_t *e);
    static void onDropdownPanelScreenSettingAutorotateImuValueChangeEventCallback(lv_event_t *e);
    static void onSwitchPanelScreenSettingAutoTimezoneValueChangeEventCallback(lv_event_t *e);
    static void onDropdownPanelScreenSettingTimezoneValueChangeEventCallback(lv_event_t *e);
#elif defined(APP_SETTINGS_DISPLAY_TIMER_STATE)
    lv_timer_t *_displayOrientationPreviewTimer;
#elif defined(APP_SETTINGS_DISPLAY_TIMER_INIT)
    _displayOrientationPreviewTimer(nullptr),
#elif defined(APP_SETTINGS_DISPLAY_MENU_STATE)
    lv_obj_t *_displayMenuItem;
#elif defined(APP_SETTINGS_DISPLAY_MENU_INIT)
    _displayMenuItem(nullptr),
#elif defined(APP_SETTINGS_DISPLAY_MENU_CREATE)
    _displayMenuItem = createMainMenuItem("Display", &ui_img_light_png, nullptr, nullptr);
#elif defined(APP_SETTINGS_DISPLAY_UI_STATE)
    lv_obj_t *_displayAdaptiveBrightnessSwitch;
    lv_obj_t *_displayNeopixelPowerSwitch;
    lv_obj_t *_displayNeopixelGpioDropdown;
    lv_obj_t *_displayNeopixelPaletteDropdown;
    lv_obj_t *_displayNeopixelEffectDropdown;
    lv_obj_t *_displayNeopixelBrightnessSlider;
    lv_obj_t *_displayNeopixelInfoLabel;
    lv_obj_t *_displayScreensaverSwitch;
    lv_obj_t *_displayTimeoffInGameSwitch;
    lv_obj_t *_displayTimeoffDropdown;
    lv_obj_t *_displaySleepDropdown;
    lv_obj_t *_displayOrientationDropdown;
    lv_obj_t *_displayOrientationPreviewMsgbox;
    lv_obj_t *_displayOrientationPreviewLabel;
    lv_obj_t *_displayOrientationPreviewSpinner;
    lv_obj_t *_displayOrientationPreviewCountdownLabel;
    int32_t _displayOrientationPreviewPrevious;
    int32_t _displayOrientationPreviewPending;
    int32_t _displayOrientationPreviewSecondsRemaining;
    bool _displayOrientationPreviewResolving;
    int32_t _displayAutorotateAppliedOrientation;
    bool _displayAutorotateHasAppliedOrientation;
    lv_obj_t *_displayAutorotateSwitch;
    lv_obj_t *_displayAutorotateImuDropdown;
    lv_obj_t *_displayAutorotateInfoLabel;
    lv_obj_t *_displayAutoTimezoneSwitch;
    lv_obj_t *_displayTimezoneDropdown;
    lv_obj_t *_displayTimezoneInfoLabel;
#elif defined(APP_SETTINGS_DISPLAY_UI_INIT)
    _displayAdaptiveBrightnessSwitch(nullptr),
    _displayNeopixelPowerSwitch(nullptr),
    _displayNeopixelGpioDropdown(nullptr),
    _displayNeopixelPaletteDropdown(nullptr),
    _displayNeopixelEffectDropdown(nullptr),
    _displayNeopixelBrightnessSlider(nullptr),
    _displayNeopixelInfoLabel(nullptr),
    _displayScreensaverSwitch(nullptr),
    _displayTimeoffInGameSwitch(nullptr),
    _displayTimeoffDropdown(nullptr),
    _displaySleepDropdown(nullptr),
    _displayOrientationDropdown(nullptr),
    _displayOrientationPreviewMsgbox(nullptr),
    _displayOrientationPreviewLabel(nullptr),
    _displayOrientationPreviewSpinner(nullptr),
    _displayOrientationPreviewCountdownLabel(nullptr),
    _displayOrientationPreviewPrevious(0),
    _displayOrientationPreviewPending(0),
    _displayOrientationPreviewSecondsRemaining(0),
    _displayOrientationPreviewResolving(false),
    _displayAutorotateAppliedOrientation(0),
    _displayAutorotateHasAppliedOrientation(false),
    _displayAutorotateSwitch(nullptr),
    _displayAutorotateImuDropdown(nullptr),
    _displayAutorotateInfoLabel(nullptr),
    _displayAutoTimezoneSwitch(nullptr),
    _displayTimezoneDropdown(nullptr),
    _displayTimezoneInfoLabel(nullptr),
#endif