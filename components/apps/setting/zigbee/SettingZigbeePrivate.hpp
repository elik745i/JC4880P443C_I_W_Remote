#if defined(APP_SETTINGS_ZIGBEE_METHOD_DECLS)
    void refreshZigbeeUi(void);
    void setZigbeeKeyboardVisible(bool visible);
    bool persistZigbeeNameFromUi(void);
#elif defined(APP_SETTINGS_ZIGBEE_CALLBACK_DECLS)
    static void onZigbeeEnableSwitchValueChangeEventCallback(lv_event_t *e);
    static void onZigbeeChannelChangedEventCallback(lv_event_t *e);
    static void onZigbeePermitJoinChangedEventCallback(lv_event_t *e);
    static void onZigbeeNameTextAreaEventCallback(lv_event_t *e);
    static void onZigbeeNameSaveClickedEventCallback(lv_event_t *e);
    static void onZigbeeKeyboardEventCallback(lv_event_t *e);
#elif defined(APP_SETTINGS_ZIGBEE_MENU_STATE)
    lv_obj_t *_zigbeeMenuItem;
#elif defined(APP_SETTINGS_ZIGBEE_MENU_INIT)
    _zigbeeMenuItem(nullptr),
#elif defined(APP_SETTINGS_ZIGBEE_MENU_CREATE)
    _zigbeeMenuItem = createMainBadgeMenuItem("ZigBee", "ZB", lv_color_hex(0xD97706), nullptr);
#elif defined(APP_SETTINGS_ZIGBEE_UI_STATE)
    lv_obj_t *_zigbeeEnableSwitch;
    lv_obj_t *_zigbeeNameTextArea;
    lv_obj_t *_zigbeeNameSaveButton;
    lv_obj_t *_zigbeeChannelDropdown;
    lv_obj_t *_zigbeePermitJoinDropdown;
    lv_obj_t *_zigbeeKeyboard;
    lv_obj_t *_zigbeeInfoLabel;
    lv_obj_t *_zigbeeRoleValueLabel;
    lv_obj_t *_zigbeeConfigSummaryLabel;
#elif defined(APP_SETTINGS_ZIGBEE_UI_INIT)
    _zigbeeEnableSwitch(nullptr),
    _zigbeeNameTextArea(nullptr),
    _zigbeeNameSaveButton(nullptr),
    _zigbeeChannelDropdown(nullptr),
    _zigbeePermitJoinDropdown(nullptr),
    _zigbeeKeyboard(nullptr),
    _zigbeeInfoLabel(nullptr),
    _zigbeeRoleValueLabel(nullptr),
    _zigbeeConfigSummaryLabel(nullptr),
#elif defined(APP_SETTINGS_ZIGBEE_RUNTIME_STATE)
    bool _zigbeeStatusIconInstalled;
#elif defined(APP_SETTINGS_ZIGBEE_RUNTIME_INIT)
    _zigbeeStatusIconInstalled(false),
#endif