#if defined(APP_SETTINGS_BLUETOOTH_METHOD_DECLS)
    void refreshBluetoothUi(void);
    void setBluetoothKeyboardVisible(bool visible);
    bool persistBluetoothNameFromUi(void);
#elif defined(APP_SETTINGS_BLUETOOTH_CALLBACK_DECLS)
    static void onSwitchPanelScreenSettingBluetoothValueChangeEventCallback(lv_event_t *e);
    static void onBluetoothNameTextAreaEventCallback(lv_event_t *e);
    static void onBluetoothNameSaveClickedEventCallback(lv_event_t *e);
    static void onBluetoothKeyboardEventCallback(lv_event_t *e);
    static void onBluetoothScanClickedEventCallback(lv_event_t *e);
#elif defined(APP_SETTINGS_BLUETOOTH_MENU_STATE)
    lv_obj_t *_bluetoothMenuItem;
#elif defined(APP_SETTINGS_BLUETOOTH_MENU_INIT)
    _bluetoothMenuItem(nullptr),
#elif defined(APP_SETTINGS_BLUETOOTH_MENU_CREATE)
    _bluetoothMenuItem = createMainMenuItem("Bluetooth", &ui_img_bluetooth_png, nullptr, nullptr);
#elif defined(APP_SETTINGS_BLUETOOTH_UI_STATE)
    lv_obj_t *_bluetoothInfoLabel;
    lv_obj_t *_bluetoothNameTextArea;
    lv_obj_t *_bluetoothNameSaveButton;
    lv_obj_t *_bluetoothScanButton;
    lv_obj_t *_bluetoothScanButtonLabel;
    lv_obj_t *_bluetoothScanStatusLabel;
    lv_obj_t *_bluetoothScanResultsLabel;
    lv_obj_t *_bluetoothKeyboard;
#elif defined(APP_SETTINGS_BLUETOOTH_UI_INIT)
    _bluetoothInfoLabel(nullptr),
    _bluetoothNameTextArea(nullptr),
    _bluetoothNameSaveButton(nullptr),
    _bluetoothScanButton(nullptr),
    _bluetoothScanButtonLabel(nullptr),
    _bluetoothScanStatusLabel(nullptr),
    _bluetoothScanResultsLabel(nullptr),
    _bluetoothKeyboard(nullptr),
#elif defined(APP_SETTINGS_BLUETOOTH_RUNTIME_STATE)
    bool _bluetoothStatusIconInstalled;
#elif defined(APP_SETTINGS_BLUETOOTH_RUNTIME_INIT)
    _bluetoothStatusIconInstalled(false),
#endif