#if defined(APP_SETTINGS_LORA_METHOD_DECLS)
    void refreshLoRaUi(void);
    void setLoRaKeyboardVisible(bool visible, lv_obj_t *textarea = nullptr);
    void ensureLoRaScreen(void);
    bool persistLoRaConfigFromUi(void);
    bool startLoRaDiagnosticFromUi(bool allow_autodetect);
    bool sendLoRaDiagnosticCommandFromUi(const std::string &command);
    bool persistLoRaRadioEnabledFromUi(void);
    void scheduleLoRaConfigApply(uint32_t delay_ms = 2000U);
    void cancelLoRaConfigApply(void);
    bool flushPendingLoRaConfigApply(void);
    void setLoRaDiagnosticLogModalVisible(bool visible);
    void refreshLoRaDiagnosticLogModal(void);
    void refreshLoRaSelfCheckStatus(void);
    void startLoRaSelfCheckStatusPolling(void);
    void stopLoRaSelfCheckStatusPolling(void);
    void disableLoRaRadioForLocalController(void);
    bool disableLocalControllerForLoRa(void);
#elif defined(APP_SETTINGS_LORA_CALLBACK_DECLS)
    static void onLoRaConfigChangedEventCallback(lv_event_t *e);
    static void onLoRaTextAreaEventCallback(lv_event_t *e);
    static void onLoRaKeyboardEventCallback(lv_event_t *e);
    static void onLoRaSaveClickedEventCallback(lv_event_t *e);
    static void onLoRaSelfCheckClickedEventCallback(lv_event_t *e);
    static void onLoRaAutoDetectClickedEventCallback(lv_event_t *e);
    static void onLoRaDiagnosticLogClickedEventCallback(lv_event_t *e);
    static void onLoRaDiagnosticLogCloseEventCallback(lv_event_t *e);
    static void onLoRaDiagnosticCommandSendClickedEventCallback(lv_event_t *e);
    static void onLoRaDiagnosticPresetChangedEventCallback(lv_event_t *e);
    static void onLoRaSelfCheckStatusTimerCallback(lv_timer_t *timer);
#elif defined(APP_SETTINGS_LORA_TIMER_STATE)
    lv_timer_t *_loraSelfCheckStatusTimer;
#elif defined(APP_SETTINGS_LORA_TIMER_INIT)
    _loraSelfCheckStatusTimer(nullptr),
#elif defined(APP_SETTINGS_LORA_MENU_STATE)
    lv_obj_t *_loraMenuItem;
#elif defined(APP_SETTINGS_LORA_MENU_INIT)
    _loraMenuItem(nullptr),
#elif defined(APP_SETTINGS_LORA_SCREEN_STATE)
    lv_obj_t *_loraScreen;
#elif defined(APP_SETTINGS_LORA_SCREEN_INIT)
    _loraScreen(nullptr),
#elif defined(APP_SETTINGS_LORA_UI_STATE)
    lv_obj_t *_loraEnabledSwitch;
    lv_obj_t *_loraModuleDropdown;
    lv_obj_t *_loraKeyboard;
    lv_obj_t *_loraKeyboardTarget;
    lv_obj_t *_loraDisplayNameTextArea;
    lv_obj_t *_loraCommonChatTitleTextArea;
    lv_obj_t *_loraFrequencyRow;
    lv_obj_t *_loraFrequencyTextArea;
    lv_obj_t *_loraSpreadingFactorRow;
    lv_obj_t *_loraSpreadingFactorTextArea;
    lv_obj_t *_loraBandwidthRow;
    lv_obj_t *_loraBandwidthTextArea;
    lv_obj_t *_loraCodingRateRow;
    lv_obj_t *_loraCodingRateTextArea;
    lv_obj_t *_loraHopLimitTextArea;
    lv_obj_t *_loraForwardingSwitch;
    lv_obj_t *_loraEncryptionSwitch;
    lv_obj_t *_loraE22Section;
    lv_obj_t *_loraE22AddressTextArea;
    lv_obj_t *_loraE22UartBaudDropdown;
    lv_obj_t *_loraE22AirDataRateDropdown;
    lv_obj_t *_loraE22SubPacketDropdown;
    lv_obj_t *_loraE22PowerDropdown;
    lv_obj_t *_loraE22TransmissionDropdown;
    lv_obj_t *_loraE22RssiDropdown;
    lv_obj_t *_loraSelfCheckButton;
    lv_obj_t *_loraAutoDetectButton;
    lv_obj_t *_loraDiagnosticLogButton;
    lv_obj_t *_loraSelfCheckStatusLabel;
    lv_obj_t *_loraInfoLabel;
    lv_obj_t *_loraDiagnosticLogOverlay;
    lv_obj_t *_loraDiagnosticLogPanel;
    lv_obj_t *_loraDiagnosticCommandTextArea;
    lv_obj_t *_loraDiagnosticCommandSendButton;
    lv_obj_t *_loraDiagnosticPresetDropdown;
    lv_obj_t *_loraDiagnosticLogScroll;
    lv_obj_t *_loraDiagnosticLogLabel;
    size_t _loraDiagnosticLogSessionStartLine;
    size_t _loraDiagnosticLogLastRenderedLineCount;
    std::array<lv_obj_t *, 14> _loraPinRows;
    std::array<lv_obj_t *, 14> _loraPinDropdowns;
    bool _loraRefreshUiFromStoredStateAfterSelfCheck;
#elif defined(APP_SETTINGS_LORA_UI_INIT)
    _loraEnabledSwitch(nullptr),
    _loraModuleDropdown(nullptr),
    _loraKeyboard(nullptr),
    _loraKeyboardTarget(nullptr),
    _loraDisplayNameTextArea(nullptr),
    _loraCommonChatTitleTextArea(nullptr),
    _loraFrequencyRow(nullptr),
    _loraFrequencyTextArea(nullptr),
    _loraSpreadingFactorRow(nullptr),
    _loraSpreadingFactorTextArea(nullptr),
    _loraBandwidthRow(nullptr),
    _loraBandwidthTextArea(nullptr),
    _loraCodingRateRow(nullptr),
    _loraCodingRateTextArea(nullptr),
    _loraHopLimitTextArea(nullptr),
    _loraForwardingSwitch(nullptr),
    _loraEncryptionSwitch(nullptr),
    _loraE22Section(nullptr),
    _loraE22AddressTextArea(nullptr),
    _loraE22UartBaudDropdown(nullptr),
    _loraE22AirDataRateDropdown(nullptr),
    _loraE22SubPacketDropdown(nullptr),
    _loraE22PowerDropdown(nullptr),
    _loraE22TransmissionDropdown(nullptr),
    _loraE22RssiDropdown(nullptr),
    _loraSelfCheckButton(nullptr),
    _loraAutoDetectButton(nullptr),
    _loraDiagnosticLogButton(nullptr),
    _loraSelfCheckStatusLabel(nullptr),
    _loraInfoLabel(nullptr),
    _loraDiagnosticLogOverlay(nullptr),
    _loraDiagnosticLogPanel(nullptr),
    _loraDiagnosticCommandTextArea(nullptr),
    _loraDiagnosticCommandSendButton(nullptr),
    _loraDiagnosticPresetDropdown(nullptr),
    _loraDiagnosticLogScroll(nullptr),
    _loraDiagnosticLogLabel(nullptr),
    _loraDiagnosticLogSessionStartLine(0),
    _loraDiagnosticLogLastRenderedLineCount(0),
    _loraPinRows{},
    _loraPinDropdowns{},
    _loraRefreshUiFromStoredStateAfterSelfCheck(false),
#endif