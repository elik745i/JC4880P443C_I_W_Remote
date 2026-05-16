void AppSettings::onSwitchPanelScreenSettingBLESwitchValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool requested_state = false;
    bool current_state = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    requested_state = (lv_obj_get_state(app->_securityDeviceLockSwitch) & LV_STATE_CHECKED) != 0;
    current_state = device_security::isLockEnabled(device_security::LockType::Device);
    if (requested_state == current_state) {
        return;
    }

    if (requested_state) {
        device_security::requestEnable(device_security::LockType::Device,
                                       onSecurityToggleRequestFinished,
                                       &app->_deviceLockToggleContext);
    } else {
        device_security::requestDisable(device_security::LockType::Device,
                                        onSecurityToggleRequestFinished,
                                        &app->_deviceLockToggleContext);
    }

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingSettingsLockValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool requested_state = false;
    bool current_state = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    requested_state = (lv_obj_get_state(app->_securitySettingsLockSwitch) & LV_STATE_CHECKED) != 0;
    current_state = device_security::isLockEnabled(device_security::LockType::Settings);
    if (requested_state == current_state) {
        return;
    }

    if (requested_state) {
        device_security::requestEnable(device_security::LockType::Settings,
                                       onSecurityToggleRequestFinished,
                                       &app->_settingsLockToggleContext);
    } else {
        device_security::requestDisable(device_security::LockType::Settings,
                                        onSecurityToggleRequestFinished,
                                        &app->_settingsLockToggleContext);
    }

end:
    return;
}

void AppSettings::onSecurityToggleRequestFinished(bool success, void *user_data)
{
    SecurityToggleContext *context = static_cast<SecurityToggleContext *>(user_data);
    if ((context == nullptr) || (context->app == nullptr)) {
        return;
    }

    context->app->handleSecurityToggleResult(context->type, success);
}