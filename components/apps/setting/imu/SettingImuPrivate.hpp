#if defined(APP_SETTINGS_IMU_METHOD_DECLS)
    void refreshImuUi(void);
    void refreshImuLiveUi(void);
    void ensureImuScreen(void);
    bool persistImuConfigFromUi(bool autosave_enabled_only = false);
    void disableImuForLocalController(void);
    bool disableImuForLoRa(void);
    void startImuLivePolling(void);
    void stopImuLivePolling(void);
#elif defined(APP_SETTINGS_IMU_CALLBACK_DECLS)
    static void onImuConfigChangedEventCallback(lv_event_t *e);
    static void onImuSaveClickedEventCallback(lv_event_t *e);
    static void onImuScanClickedEventCallback(lv_event_t *e);
    static void onImuTestClickedEventCallback(lv_event_t *e);
    static void onImuZeroClickedEventCallback(lv_event_t *e);
    static void onImuLiveTimerCallback(lv_timer_t *timer);
#elif defined(APP_SETTINGS_IMU_TIMER_STATE)
    lv_timer_t *_imuLiveTimer;
#elif defined(APP_SETTINGS_IMU_TIMER_INIT)
    _imuLiveTimer(nullptr),
#elif defined(APP_SETTINGS_IMU_MENU_STATE)
    lv_obj_t *_imuMenuItem;
#elif defined(APP_SETTINGS_IMU_MENU_INIT)
    _imuMenuItem(nullptr),
#elif defined(APP_SETTINGS_IMU_SCREEN_STATE)
    lv_obj_t *_imuScreen;
#elif defined(APP_SETTINGS_IMU_SCREEN_INIT)
    _imuScreen(nullptr),
#elif defined(APP_SETTINGS_IMU_UI_STATE)
    lv_obj_t *_imuEnabledSwitch;
    lv_obj_t *_imuModelDropdown;
    lv_obj_t *_imuBusDropdown;
    lv_obj_t *_imuPowerHintLabel;
    lv_obj_t *_imuI2cSdaDropdown;
    lv_obj_t *_imuI2cSclDropdown;
    lv_obj_t *_imuI2cAddressDropdown;
    lv_obj_t *_imuI2cAddressTextArea;
    lv_obj_t *_imuIntDropdown;
    lv_obj_t *_imuDrdyDropdown;
    lv_obj_t *_imuLiveScene;
    lv_obj_t *_imuHeadingArc;
    lv_obj_t *_imuHeadingValueLabel;
    lv_obj_t *_imuRollValueLabel;
    lv_obj_t *_imuPitchValueLabel;
    lv_obj_t *_imuYawValueLabel;
    lv_obj_t *_imuMotionShadow;
    lv_obj_t *_imuMotionDot;
    lv_obj_t *_imuMotionTempLabel;
    lv_obj_t *_imuLiveCaptionLabel;
    lv_obj_t *_imuLiveStatusLabel;
    lv_obj_t *_imuAccelValueLabel;
    lv_obj_t *_imuGyroValueLabel;
    lv_obj_t *_imuMagValueLabel;
    lv_obj_t *_imuEnvValueLabel;
    float _imuBallPosX;
    float _imuBallPosY;
    float _imuBallPosZ;
    float _imuBallVelX;
    float _imuBallVelY;
    float _imuBallVelZ;
    float _imuBallPrevAccelX;
    float _imuBallPrevAccelY;
    float _imuBallPrevAccelZ;
    bool _imuBallDynamicsInitialized;
    std::array<lv_obj_t *, 5> _imuSensorIndicatorDots;
    std::array<lv_obj_t *, 5> _imuSensorIndicatorLabels;
    lv_obj_t *_imuScanButton;
    lv_obj_t *_imuTestButton;
    lv_obj_t *_imuZeroButton;
    lv_obj_t *_imuStatusLabel;
    lv_obj_t *_imuInfoLabel;
#elif defined(APP_SETTINGS_IMU_UI_INIT)
    _imuEnabledSwitch(nullptr),
    _imuModelDropdown(nullptr),
    _imuBusDropdown(nullptr),
    _imuPowerHintLabel(nullptr),
    _imuI2cSdaDropdown(nullptr),
    _imuI2cSclDropdown(nullptr),
    _imuI2cAddressDropdown(nullptr),
    _imuI2cAddressTextArea(nullptr),
    _imuIntDropdown(nullptr),
    _imuDrdyDropdown(nullptr),
    _imuLiveScene(nullptr),
    _imuHeadingArc(nullptr),
    _imuHeadingValueLabel(nullptr),
    _imuRollValueLabel(nullptr),
    _imuPitchValueLabel(nullptr),
    _imuYawValueLabel(nullptr),
    _imuMotionShadow(nullptr),
    _imuMotionDot(nullptr),
    _imuMotionTempLabel(nullptr),
    _imuLiveCaptionLabel(nullptr),
    _imuLiveStatusLabel(nullptr),
    _imuAccelValueLabel(nullptr),
    _imuGyroValueLabel(nullptr),
    _imuMagValueLabel(nullptr),
    _imuEnvValueLabel(nullptr),
    _imuBallPosX(21.0f),
    _imuBallPosY(21.0f),
    _imuBallPosZ(0.0f),
    _imuBallVelX(0.0f),
    _imuBallVelY(0.0f),
    _imuBallVelZ(0.0f),
    _imuBallPrevAccelX(0.0f),
    _imuBallPrevAccelY(0.0f),
    _imuBallPrevAccelZ(0.0f),
    _imuBallDynamicsInitialized(false),
    _imuSensorIndicatorDots{},
    _imuSensorIndicatorLabels{},
    _imuScanButton(nullptr),
    _imuTestButton(nullptr),
    _imuZeroButton(nullptr),
    _imuStatusLabel(nullptr),
    _imuInfoLabel(nullptr),
#endif
