#if defined(APP_SETTINGS_GPIO_METHOD_DECLS)
    void refreshHardwareGpioTestUi(void);
    void setHardwareGpioTestKeyboardVisible(bool visible, lv_obj_t *textarea = nullptr);
    void setHardwareGpioTestMode(HardwareGpioTestMode mode);
    void setHardwareGpioTestLevel(size_t index, bool high);
    void setHardwareGpioTestAllLevels(bool high);
    void setHardwareGpioTestPwm(size_t index, uint8_t duty_percent);
    void stopHardwareGpioTestPwm(void);
    void setHardwareGpioTestWave(size_t index);
    void stopHardwareGpioTestWave(void);
    void toggleHardwareGpioTestTimer(size_t index);
    void stopHardwareGpioTestTimer(size_t index, bool preserve_remaining);
    void stopHardwareGpioTestTimers(void);
    void processHardwareGpioTestTimers(void);
    void processHardwareGpioTestAlarms(void);
    bool applyHardwareGpioTestDrivenLevel(size_t index, bool high);
    void applyHardwareGpioTestRunningTimers(void);
    void applyHardwareGpioTestActiveAlarms(void);
    void applyHardwareGpioTestScheduledOutputs(void);
    void scanHardwareGpioTestAudioFiles(bool allow_mount);
    void refreshHardwareGpioTestAudioDropdowns(void);
    void loadHardwareGpioTestAudioSelections(void);
    void persistHardwareGpioTestAudioSelections(void);
    void playHardwareGpioTestAlertSound(bool timer_sound);
    void loadHardwareGpioTestSchedules(void);
    void persistHardwareGpioTestSchedules(void);
    void releaseHardwareGpioTestPins(void);
    void ensureGpioTestScreen(void);
    static void onDebugBuildGpioControlScreenAsync(void *context);
    void appendHardwareGpioTestPinRow(lv_obj_t *gpioPanel, size_t index);
    void appendHardwareGpioTestAudioRows(lv_obj_t *gpioPanel);
#elif defined(APP_SETTINGS_GPIO_CALLBACK_DECLS)
    static void onHardwareGpioTestClickedEventCallback(lv_event_t *e);
    static void onHardwareGpioTestModeChangedEventCallback(lv_event_t *e);
    static void onHardwareGpioTestPwmChangedEventCallback(lv_event_t *e);
    static void onHardwareGpioTestWaveChangedEventCallback(lv_event_t *e);
    static void onHardwareGpioTestWaveDurationEventCallback(lv_event_t *e);
    static void onHardwareGpioTestKeyboardEventCallback(lv_event_t *e);
    static void onHardwareGpioTestWaveTimerCallback(lv_timer_t *timer);
    static void onHardwareGpioTestTimerDurationEventCallback(lv_event_t *e);
    static void onHardwareGpioTestTimerLevelChangedEventCallback(lv_event_t *e);
    static void onHardwareGpioTestTimerButtonClickedEventCallback(lv_event_t *e);
    static void onHardwareGpioTestTimerTickCallback(lv_timer_t *timer);
    static void onHardwareGpioTestAlarmTimeEventCallback(lv_event_t *e);
    static void onHardwareGpioTestAlarmLevelChangedEventCallback(lv_event_t *e);
    static void onHardwareGpioTestAlarmToggleClickedEventCallback(lv_event_t *e);
    static void onHardwareGpioTestAudioSelectionChangedEventCallback(lv_event_t *e);
#elif defined(APP_SETTINGS_GPIO_MENU_STATE)
    lv_obj_t *_gpioTestMenuItem;
#elif defined(APP_SETTINGS_GPIO_MENU_INIT)
    _gpioTestMenuItem(nullptr),
#elif defined(APP_SETTINGS_GPIO_SCREEN_STATE)
    lv_obj_t *_gpioTestScreen;
#elif defined(APP_SETTINGS_GPIO_SCREEN_INIT)
    _gpioTestScreen(nullptr),
#elif defined(APP_SETTINGS_GPIO_UI_STATE)
    lv_obj_t *_hardwareGpioTestModeDropdown;
    lv_obj_t *_hardwareGpioTestAllLowButton;
    lv_obj_t *_hardwareGpioTestAllHighButton;
    lv_obj_t *_hardwareGpioTestKeyboard;
    lv_obj_t *_hardwareGpioTestKeyboardTarget;
    lv_obj_t *_hardwareGpioTestTimerAudioDropdown;
    lv_obj_t *_hardwareGpioTestAlarmAudioDropdown;
    std::array<lv_obj_t *, kHardwareGpioTestPinCount> _hardwareGpioTestLowButtons;
    std::array<lv_obj_t *, kHardwareGpioTestPinCount> _hardwareGpioTestHighButtons;
    std::array<lv_obj_t *, kHardwareGpioTestPinCount> _hardwareGpioTestPwmSliders;
    std::array<lv_obj_t *, kHardwareGpioTestPinCount> _hardwareGpioTestWaveDropdowns;
    std::array<lv_obj_t *, kHardwareGpioTestPinCount> _hardwareGpioTestWaveDurationInputs;
    std::array<lv_obj_t *, kHardwareGpioTestPinCount> _hardwareGpioTestTimerDurationInputs;
    std::array<lv_obj_t *, kHardwareGpioTestPinCount> _hardwareGpioTestTimerLevelDropdowns;
    std::array<lv_obj_t *, kHardwareGpioTestPinCount> _hardwareGpioTestTimerButtons;
    std::array<lv_obj_t *, kHardwareGpioTestPinCount> _hardwareGpioTestTimerButtonLabels;
    std::array<lv_obj_t *, kHardwareGpioTestPinCount> _hardwareGpioTestAlarmTimeInputs;
    std::array<lv_obj_t *, kHardwareGpioTestPinCount> _hardwareGpioTestAlarmLevelDropdowns;
    std::array<lv_obj_t *, kHardwareGpioTestPinCount> _hardwareGpioTestAlarmToggleButtons;
    std::array<lv_obj_t *, kHardwareGpioTestPinCount> _hardwareGpioTestAlarmToggleLabels;
    std::array<lv_obj_t *, kHardwareGpioTestPinCount> _hardwareGpioTestStateLabels;
    std::array<int8_t, kHardwareGpioTestPinCount> _hardwareGpioTestLevels;
    std::array<uint8_t, kHardwareGpioTestPinCount> _hardwareGpioTestPwmValues;
    std::array<HardwareGpioTestWaveform, kHardwareGpioTestPinCount> _hardwareGpioTestWaveforms;
    std::array<uint16_t, kHardwareGpioTestPinCount> _hardwareGpioTestWaveDurationsTenths;
    std::array<uint16_t, kHardwareGpioTestPinCount> _hardwareGpioTestTimerDurationsSec;
    std::array<uint16_t, kHardwareGpioTestPinCount> _hardwareGpioTestTimerRemainingSec;
    std::array<bool, kHardwareGpioTestPinCount> _hardwareGpioTestTimerLevelsHigh;
    std::array<bool, kHardwareGpioTestPinCount> _hardwareGpioTestTimerRunning;
    std::array<bool, kHardwareGpioTestPinCount> _hardwareGpioTestTimerAlertActive;
    std::array<int64_t, kHardwareGpioTestPinCount> _hardwareGpioTestTimerEndUs;
    std::array<int64_t, kHardwareGpioTestPinCount> _hardwareGpioTestTimerEndEpochSec;
    std::array<uint16_t, kHardwareGpioTestPinCount> _hardwareGpioTestAlarmMinutesOfDay;
    std::array<bool, kHardwareGpioTestPinCount> _hardwareGpioTestAlarmLevelsHigh;
    std::array<bool, kHardwareGpioTestPinCount> _hardwareGpioTestAlarmArmed;
    std::array<bool, kHardwareGpioTestPinCount> _hardwareGpioTestAlarmActive;
    std::array<int32_t, kHardwareGpioTestPinCount> _hardwareGpioTestAlarmLastFireDate;
    HardwareGpioTestMode _hardwareGpioTestMode;
    int8_t _hardwareGpioTestActivePwmIndex;
    int8_t _hardwareGpioTestActiveWaveIndex;
    int8_t _hardwareGpioTestTimerAlertAudioIndex;
    lv_timer_t *_hardwareGpioTestWaveTimer;
    int64_t _hardwareGpioTestWaveStartUs;
    lv_timer_t *_hardwareGpioTestTimerTick;
    std::vector<std::string> _hardwareGpioTestAudioFilePaths;
    std::string _hardwareGpioTestTimerAudioPath;
    std::string _hardwareGpioTestAlarmAudioPath;
    bool _hardwareGpioTestTimerAlertBlinkVisible;
    bool _hardwareGpioTestAudioScanPending;
    bool _hardwareGpioTestUiSyncInProgress;
#elif defined(APP_SETTINGS_GPIO_UI_INIT)
    _hardwareGpioTestModeDropdown(nullptr),
    _hardwareGpioTestAllLowButton(nullptr),
    _hardwareGpioTestAllHighButton(nullptr),
    _hardwareGpioTestKeyboard(nullptr),
    _hardwareGpioTestKeyboardTarget(nullptr),
    _hardwareGpioTestTimerAudioDropdown(nullptr),
    _hardwareGpioTestAlarmAudioDropdown(nullptr),
    _hardwareGpioTestLowButtons{},
    _hardwareGpioTestHighButtons{},
    _hardwareGpioTestPwmSliders{},
    _hardwareGpioTestWaveDropdowns{},
    _hardwareGpioTestWaveDurationInputs{},
    _hardwareGpioTestTimerDurationInputs{},
    _hardwareGpioTestTimerLevelDropdowns{},
    _hardwareGpioTestTimerButtons{},
    _hardwareGpioTestTimerButtonLabels{},
    _hardwareGpioTestAlarmTimeInputs{},
    _hardwareGpioTestAlarmLevelDropdowns{},
    _hardwareGpioTestAlarmToggleButtons{},
    _hardwareGpioTestAlarmToggleLabels{},
    _hardwareGpioTestStateLabels{},
    _hardwareGpioTestLevels{},
    _hardwareGpioTestPwmValues{},
    _hardwareGpioTestWaveforms{},
    _hardwareGpioTestWaveDurationsTenths{},
    _hardwareGpioTestTimerDurationsSec{},
    _hardwareGpioTestTimerRemainingSec{},
    _hardwareGpioTestTimerLevelsHigh{},
    _hardwareGpioTestTimerRunning{},
    _hardwareGpioTestTimerAlertActive{},
    _hardwareGpioTestTimerEndUs{},
    _hardwareGpioTestTimerEndEpochSec{},
    _hardwareGpioTestAlarmMinutesOfDay{},
    _hardwareGpioTestAlarmLevelsHigh{},
    _hardwareGpioTestAlarmArmed{},
    _hardwareGpioTestAlarmActive{},
    _hardwareGpioTestAlarmLastFireDate{},
    _hardwareGpioTestMode(HardwareGpioTestMode::Output),
    _hardwareGpioTestActivePwmIndex(-1),
    _hardwareGpioTestActiveWaveIndex(-1),
    _hardwareGpioTestTimerAlertAudioIndex(-1),
    _hardwareGpioTestWaveTimer(nullptr),
    _hardwareGpioTestWaveStartUs(0),
    _hardwareGpioTestTimerTick(nullptr),
    _hardwareGpioTestAudioFilePaths(),
    _hardwareGpioTestTimerAudioPath(),
    _hardwareGpioTestAlarmAudioPath(),
    _hardwareGpioTestTimerAlertBlinkVisible(true),
    _hardwareGpioTestAudioScanPending(false),
    _hardwareGpioTestUiSyncInProgress(false),
#endif