#if defined(APP_SETTINGS_HARDWARE_METHOD_DECLS)
    void refreshHardwareMonitorUi(void);
    void ensureHardwareScreen(void);
    void setBatteryHistoryExpanded(bool expanded, bool animate);
    void setHardwareTrendExpanded(HardwareTrendCardIndex index, bool expanded, bool animate);
#elif defined(APP_SETTINGS_HARDWARE_CALLBACK_DECLS)
    static void onHardwareBatteryCardClickedEventCallback(lv_event_t *e);
    static void onHardwareTrendCardClickedEventCallback(lv_event_t *e);
#elif defined(APP_SETTINGS_HARDWARE_MENU_STATE)
    lv_obj_t *_hardwareMenuItem;
#elif defined(APP_SETTINGS_HARDWARE_MENU_INIT)
    _hardwareMenuItem(nullptr),
#elif defined(APP_SETTINGS_HARDWARE_SCREEN_STATE)
    lv_obj_t *_hardwareScreen;
#elif defined(APP_SETTINGS_HARDWARE_SCREEN_INIT)
    _hardwareScreen(nullptr),
#elif defined(APP_SETTINGS_HARDWARE_UI_STATE)
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
#elif defined(APP_SETTINGS_HARDWARE_UI_INIT)
    _hardwareCpuSpeedValueLabel(nullptr),
    _hardwareCpuSpeedDetailLabel(nullptr),
    _hardwareCpuSpeedBar(nullptr),
    _hardwareBatteryCard(nullptr),
    _hardwareBatteryValueLabel(nullptr),
    _hardwareBatteryDetailLabel(nullptr),
    _hardwareBatteryBar(nullptr),
    _hardwareBatteryExpandedArea(nullptr),
    _hardwareBatteryExpandLabel(nullptr),
    _hardwareBatteryHistoryTitleLabel(nullptr),
    _hardwareBatteryHistorySummaryLabel(nullptr),
    _hardwareBatteryHistoryChart(nullptr),
    _hardwareBatteryHistorySeries(nullptr),
    _hardwareBatteryHistoryLeftLabel(nullptr),
    _hardwareBatteryHistoryRightLabel(nullptr),
    _hardwareBatteryHistoryFooterLabel(nullptr),
    _hardwareBatteryExpanded(false),
    _hardwareTrendUi{},
    _hardwareFastHistoryScratch(nullptr),
    _hardwareSlowHistoryScratch(nullptr),
    _hardwareCpuTempValueLabel(nullptr),
    _hardwareCpuTempDetailLabel(nullptr),
    _hardwareCpuTempBar(nullptr),
    _hardwareSramValueLabel(nullptr),
    _hardwareSramDetailLabel(nullptr),
    _hardwareSramBar(nullptr),
    _hardwarePsramValueLabel(nullptr),
    _hardwarePsramDetailLabel(nullptr),
    _hardwarePsramBar(nullptr),
    _hardwareSdValueLabel(nullptr),
    _hardwareSdDetailLabel(nullptr),
    _hardwareSdBar(nullptr),
    _hardwareWifiValueLabel(nullptr),
    _hardwareWifiDetailLabel(nullptr),
    _hardwareWifiBar(nullptr),
#endif