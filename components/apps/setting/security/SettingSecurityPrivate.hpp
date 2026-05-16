#if defined(APP_SETTINGS_SECURITY_METHOD_DECLS)
    void refreshSecurityUi(void);
    void handleSecurityToggleResult(device_security::LockType type, bool success);
#elif defined(APP_SETTINGS_SECURITY_CALLBACK_DECLS)
    static void onSwitchPanelScreenSettingBLESwitchValueChangeEventCallback(lv_event_t *e);
    static void onSwitchPanelScreenSettingSettingsLockValueChangeEventCallback(lv_event_t *e);
    static void onSecurityToggleRequestFinished(bool success, void *user_data);
#elif defined(APP_SETTINGS_SECURITY_MENU_STATE)
    lv_obj_t *_securityMenuItem;
#elif defined(APP_SETTINGS_SECURITY_MENU_INIT)
    _securityMenuItem(nullptr),
#elif defined(APP_SETTINGS_SECURITY_MENU_CREATE)
    _securityMenuItem = createMainMenuItem("Security", nullptr, LV_SYMBOL_WARNING, nullptr);
#elif defined(APP_SETTINGS_SECURITY_UI_STATE)
    lv_obj_t *_securityDeviceLockSwitch;
    lv_obj_t *_securitySettingsLockSwitch;
    lv_obj_t *_securityInfoLabel;
#elif defined(APP_SETTINGS_SECURITY_UI_INIT)
    _securityDeviceLockSwitch(nullptr),
    _securitySettingsLockSwitch(nullptr),
    _securityInfoLabel(nullptr),
#elif defined(APP_SETTINGS_SECURITY_RUNTIME_STATE)
    struct SecurityToggleContext {
        AppSettings *app;
        device_security::LockType type;
    };
    SecurityToggleContext _deviceLockToggleContext;
    SecurityToggleContext _settingsLockToggleContext;
#elif defined(APP_SETTINGS_SECURITY_RUNTIME_INIT)
    _deviceLockToggleContext{this, device_security::LockType::Device},
    _settingsLockToggleContext{this, device_security::LockType::Settings},
#endif