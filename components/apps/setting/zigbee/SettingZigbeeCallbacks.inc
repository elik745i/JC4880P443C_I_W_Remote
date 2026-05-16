void AppSettings::onZigbeeEnableSwitchValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    enabled = (lv_obj_get_state(app->_zigbeeEnableSwitch) & LV_STATE_CHECKED) != 0;

    app->_nvs_param_map[NVS_KEY_ZIGBEE_ENABLE] = enabled ? 1 : 0;
    app->setNvsParam(NVS_KEY_ZIGBEE_ENABLE, enabled ? 1 : 0);
    app->refreshZigbeeUi();
    app->refreshRadioStatusBar();

end:
    return;
}

void AppSettings::onZigbeeChannelChangedEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    uint16_t selected = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    selected = lv_dropdown_get_selected(app->_zigbeeChannelDropdown);
    if (selected < (sizeof(kZigbeeChannelOptions) / sizeof(kZigbeeChannelOptions[0]))) {
        app->_nvs_param_map[NVS_KEY_ZIGBEE_CHANNEL] = kZigbeeChannelOptions[selected];
        app->setNvsParam(NVS_KEY_ZIGBEE_CHANNEL, kZigbeeChannelOptions[selected]);
        app->refreshZigbeeUi();
    }

end:
    return;
}

void AppSettings::onZigbeePermitJoinChangedEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    uint16_t selected = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    selected = lv_dropdown_get_selected(app->_zigbeePermitJoinDropdown);
    if (selected < (sizeof(kZigbeePermitJoinOptionsSec) / sizeof(kZigbeePermitJoinOptionsSec[0]))) {
        app->_nvs_param_map[NVS_KEY_ZIGBEE_PERMIT_JOIN] = kZigbeePermitJoinOptionsSec[selected];
        app->setNvsParam(NVS_KEY_ZIGBEE_PERMIT_JOIN, kZigbeePermitJoinOptionsSec[selected]);
        app->refreshZigbeeUi();
    }

end:
    return;
}

void AppSettings::onZigbeeNameTextAreaEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if ((code == LV_EVENT_FOCUSED) || (code == LV_EVENT_CLICKED)) {
        app->setZigbeeKeyboardVisible(true);
    } else if ((code == LV_EVENT_DEFOCUSED) || (code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        app->setZigbeeKeyboardVisible(false);
    }

end:
    return;
}

void AppSettings::onZigbeeNameSaveClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    app->persistZigbeeNameFromUi();
    app->setZigbeeKeyboardVisible(false);
    app->refreshZigbeeUi();

end:
    return;
}

void AppSettings::onZigbeeKeyboardEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (code == LV_EVENT_READY) {
        app->persistZigbeeNameFromUi();
        app->setZigbeeKeyboardVisible(false);
        if (app->_zigbeeNameTextArea != nullptr) {
            lv_obj_clear_state(app->_zigbeeNameTextArea, LV_STATE_FOCUSED);
        }
        app->refreshZigbeeUi();
    } else if (code == LV_EVENT_CANCEL) {
        app->setZigbeeKeyboardVisible(false);
        if (app->_zigbeeNameTextArea != nullptr) {
            lv_obj_clear_state(app->_zigbeeNameTextArea, LV_STATE_FOCUSED);
        }
        app->refreshZigbeeUi();
    }

end:
    return;
}