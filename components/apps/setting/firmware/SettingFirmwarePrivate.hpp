#if defined(APP_SETTINGS_FIRMWARE_METHOD_DECLS)
    void refreshFirmwareUi(void);
    void setFirmwareStatus(const std::string &status, bool is_error = false);
    void ensureFirmwareScreen(void);
    void ensureFirmwareOtaCheckOverlay(void);
    void ensureOtaUpdateProgressOverlay(void);
    void updateOtaUpdateOverlayActions(bool waiting_for_decision, bool show_reschedule_picker);
    void setFirmwareProgress(int32_t percent, const std::string &phase, bool is_error = false);
    void setFirmwareOtaCheckOverlayVisible(bool visible, const std::string &status = std::string());
    void setOtaUpdateProgressOverlayVisible(bool visible);
    void queueFirmwareUiUpdate(const char *status, int32_t percent, bool busy, bool is_error);
    void populateFirmwareDropdown(lv_obj_t *dropdown, const std::vector<FirmwareEntry_t> &entries, const char *empty_label);
    void rebuildFirmwareOtaList(void);
    void releaseFirmwareOtaResources(void);
    int getSelectedOtaFirmwareIndex(void) const;
    void setSelectedOtaFirmwareIndex(int index);
    bool scanSdFirmwareEntries(void);
    bool fetchGithubFirmwareEntries(void);
    static bool fetchGithubFirmwareEntriesForVersion(const std::string &current_version, std::vector<FirmwareEntry_t> &entries);
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
    bool isWifiConnectedForOtaCheck(void) const;
    void maybeRunOtaAvailabilityCheck(void);
    void requestFirmwareScreenOpen(bool prefer_newer);
    void openFirmwareScreenIfPending(void);
    int findPreferredOtaEntryIndex(bool prefer_newer) const;
    void refreshDeferredOtaScheduleState(void);
    void clearDeferredOtaSchedule(bool persist = true);
    void deferOtaUpdateForEntry(const FirmwareEntry_t &entry, uint32_t delay_seconds);
    bool shouldSuppressDeferredOtaUpdate(const FirmwareEntry_t &entry, uint64_t now_us);
    bool shouldResumeDeferredOtaUpdate(const FirmwareEntry_t &entry, uint64_t now_us);
    void updateOtaRescheduleMinuteOptions();
    uint32_t getSelectedOtaRescheduleDelaySeconds(void) const;
    void showAutoUpdateDecisionOverlay(const FirmwareEntry_t &entry);
    bool startPreferredOtaUpdate(void);
    static void firmwareUpdateTask(void *arg);
    static void applyAsyncFirmwareUiUpdate(void *arg);
    static void applyAsyncOtaAvailabilityResult(void *arg);
#elif defined(APP_SETTINGS_FIRMWARE_CALLBACK_DECLS)
    static void onFirmwareMenuClickedEventCallback(lv_event_t *e);
    static void onFirmwareSdRefreshClickedEventCallback(lv_event_t *e);
    static void onFirmwareOtaCheckClickedEventCallback(lv_event_t *e);
    static void onFirmwareSdFlashClickedEventCallback(lv_event_t *e);
    static void onFirmwareOtaFlashClickedEventCallback(lv_event_t *e);
    static void onFirmwareSelectionChangedEventCallback(lv_event_t *e);
    static void onFirmwareOtaEntryCheckedEventCallback(lv_event_t *e);
    static void onFirmwareFactoryResetClickedEventCallback(lv_event_t *e);
    static void onFirmwareFactoryResetConfirmEventCallback(lv_event_t *e);
    static void onFirmwareAutoUpdateSwitchValueChangeEventCallback(lv_event_t *e);
    static void onOtaUpdateAvailablePopupEventCallback(lv_event_t *e);
    static void onOtaUpdateInstallNowEventCallback(lv_event_t *e);
    static void onOtaUpdateCancelEventCallback(lv_event_t *e);
    static void onOtaUpdateRescheduleEventCallback(lv_event_t *e);
    static void onOtaUpdateRescheduleHourChangedEventCallback(lv_event_t *e);
    static void onOtaUpdateRescheduleApplyEventCallback(lv_event_t *e);
    static void onOtaUpdateProgressCloseEventCallback(lv_event_t *e);
#elif defined(APP_SETTINGS_FIRMWARE_MENU_STATE)
    lv_obj_t *_firmwareMenuItem;
#elif defined(APP_SETTINGS_FIRMWARE_MENU_INIT)
    _firmwareMenuItem(nullptr),
#elif defined(APP_SETTINGS_FIRMWARE_MENU_CREATE)
    _firmwareMenuItem = createMainMenuItem("Firmware OTA", nullptr, LV_SYMBOL_DOWNLOAD, nullptr);
#elif defined(APP_SETTINGS_FIRMWARE_SCREEN_STATE)
    lv_obj_t *_firmwareScreen;
#elif defined(APP_SETTINGS_FIRMWARE_SCREEN_INIT)
    _firmwareScreen(nullptr),
#elif defined(APP_SETTINGS_FIRMWARE_UI_STATE)
    lv_obj_t *_firmwareSdDropdown;
    lv_obj_t *_firmwareSdFlashButton;
    lv_obj_t *_firmwareOtaCheckButton;
    lv_obj_t *_firmwareOtaFlashButton;
    lv_obj_t *_firmwareAutoUpdateSwitch;
    lv_obj_t *_firmwareCurrentVersionLabel;
    lv_obj_t *_firmwareOtaSummaryLabel;
    lv_obj_t *_firmwareOtaListContainer;
    lv_obj_t *_firmwareOtaCheckOverlay;
    lv_obj_t *_firmwareOtaCheckSpinner;
    lv_obj_t *_firmwareOtaCheckStatusLabel;
    lv_obj_t *_firmwareStatusLabel;
    lv_obj_t *_firmwareProgressBar;
    lv_obj_t *_firmwareProgressLabel;
    lv_obj_t *_otaUpdateAvailableMsgbox;
    lv_obj_t *_otaUpdateProgressOverlay;
    lv_obj_t *_otaUpdateProgressStatusLabel;
    lv_obj_t *_otaUpdateProgressBar;
    lv_obj_t *_otaUpdateProgressLabel;
    lv_obj_t *_otaUpdateProgressActionRow;
    lv_obj_t *_otaUpdateProgressInstallButton;
    lv_obj_t *_otaUpdateProgressRescheduleButton;
    lv_obj_t *_otaUpdateProgressCancelButton;
    lv_obj_t *_otaUpdateProgressCornerCloseButton;
    lv_obj_t *_otaUpdateReschedulePanel;
    lv_obj_t *_otaUpdateRescheduleHourDropdown;
    lv_obj_t *_otaUpdateRescheduleMinuteDropdown;
    lv_obj_t *_otaUpdateRescheduleApplyButton;
    lv_obj_t *_otaUpdateProgressCloseButton;
#elif defined(APP_SETTINGS_FIRMWARE_UI_INIT)
    _firmwareSdDropdown(nullptr),
    _firmwareSdFlashButton(nullptr),
    _firmwareOtaCheckButton(nullptr),
    _firmwareOtaFlashButton(nullptr),
    _firmwareAutoUpdateSwitch(nullptr),
    _firmwareCurrentVersionLabel(nullptr),
    _firmwareOtaSummaryLabel(nullptr),
    _firmwareOtaListContainer(nullptr),
    _firmwareOtaCheckOverlay(nullptr),
    _firmwareOtaCheckSpinner(nullptr),
    _firmwareOtaCheckStatusLabel(nullptr),
    _firmwareStatusLabel(nullptr),
    _firmwareProgressBar(nullptr),
    _firmwareProgressLabel(nullptr),
    _otaUpdateAvailableMsgbox(nullptr),
    _otaUpdateProgressOverlay(nullptr),
    _otaUpdateProgressStatusLabel(nullptr),
    _otaUpdateProgressBar(nullptr),
    _otaUpdateProgressLabel(nullptr),
    _otaUpdateProgressActionRow(nullptr),
    _otaUpdateProgressInstallButton(nullptr),
    _otaUpdateProgressRescheduleButton(nullptr),
    _otaUpdateProgressCancelButton(nullptr),
    _otaUpdateProgressCornerCloseButton(nullptr),
    _otaUpdateReschedulePanel(nullptr),
    _otaUpdateRescheduleHourDropdown(nullptr),
    _otaUpdateRescheduleMinuteDropdown(nullptr),
    _otaUpdateRescheduleApplyButton(nullptr),
    _otaUpdateProgressCloseButton(nullptr),
#elif defined(APP_SETTINGS_FIRMWARE_RUNTIME_STATE)
    bool _firmwareUpdateInProgress;
    bool _firmwareCancelRequested;
    bool _firmwareOtaCheckInProgress;
    bool _otaStatusIconInstalled;
    bool _otaUpdateAvailableThisBoot;
    bool _otaUpdatePromptDismissedThisBoot;
    bool _otaAutoUpdateAwaitingDecision;
    bool _otaAvailabilityCheckInProgress;
    bool _pendingOpenFirmwareScreen;
    uint64_t _otaDeferredAutoUpdateUntilUs;
    uint64_t _nextOtaAvailabilityCheckUs;
    std::string _otaDeferredAutoUpdateVersion;
    std::vector<FirmwareEntry_t> _sdFirmwareEntries;
    std::vector<FirmwareEntry_t> _otaFirmwareEntries;
    std::vector<lv_obj_t *> _firmwareOtaCheckboxes;
    int _selectedOtaFirmwareIndex;
#elif defined(APP_SETTINGS_FIRMWARE_RUNTIME_INIT)
    _firmwareUpdateInProgress(false),
    _firmwareCancelRequested(false),
    _firmwareOtaCheckInProgress(false),
    _otaStatusIconInstalled(false),
    _otaUpdateAvailableThisBoot(false),
    _otaUpdatePromptDismissedThisBoot(false),
    _otaAutoUpdateAwaitingDecision(false),
    _otaAvailabilityCheckInProgress(false),
    _pendingOpenFirmwareScreen(false),
    _otaDeferredAutoUpdateUntilUs(0),
    _nextOtaAvailabilityCheckUs(0),
    _otaDeferredAutoUpdateVersion(),
    _sdFirmwareEntries(),
    _otaFirmwareEntries(),
    _firmwareOtaCheckboxes(),
    _selectedOtaFirmwareIndex(-1),
#endif
