void AppSettings::onHardwareBatteryCardClickedEventCallback(lv_event_t *e)
{
#if !CONFIG_JC4880_FEATURE_BATTERY
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");
    app->setBatteryHistoryExpanded(!app->_hardwareBatteryExpanded, true);
end:
    return;
#endif
}

void AppSettings::onHardwareTrendCardClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    lv_obj_t *current_target = lv_event_get_current_target(e);
    for (int index = HARDWARE_TREND_CPU_LOAD; index < HARDWARE_TREND_CARD_COUNT; ++index) {
        if (app->_hardwareTrendUi[index].card == current_target) {
            app->setHardwareTrendExpanded(static_cast<HardwareTrendCardIndex>(index),
                                          !app->_hardwareTrendUi[index].expanded,
                                          true);
            break;
        }
    }
}