void AppSettings::onSwitchPanelScreenSettingBluetoothValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (bluetoothMenuDelegatesToJoypadBle()) {
        lv_obj_clear_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_CHECKED);
        app->refreshBluetoothUi();
        goto end;
    }

    enabled = (lv_obj_get_state(ui_SwitchPanelScreenSettingBLESwitch) & LV_STATE_CHECKED) != 0;
    if (!enabled) {
        bleCancelScan();
    }
    if (bleSetEnabled(enabled) != ESP_OK) {
        enabled = false;
        lv_obj_clear_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_CHECKED);
    }

    app->_nvs_param_map[NVS_KEY_BLE_ENABLE] = enabled;
    app->setNvsParam(NVS_KEY_BLE_ENABLE, enabled ? 1 : 0);
    app->refreshBluetoothUi();
    app->refreshRadioStatusBar();

end:
    return;
}

void AppSettings::onBluetoothNameTextAreaEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (code == LV_EVENT_FOCUSED) {
        app->setBluetoothKeyboardVisible(true);
    } else if ((code == LV_EVENT_DEFOCUSED) || (code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        app->setBluetoothKeyboardVisible(false);
    }

end:
    return;
}

void AppSettings::onBluetoothNameSaveClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    app->persistBluetoothNameFromUi();
    app->setBluetoothKeyboardVisible(false);
    app->refreshBluetoothUi();

end:
    return;
}

void AppSettings::onBluetoothKeyboardEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if ((code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        app->setBluetoothKeyboardVisible(false);
        if (code == LV_EVENT_READY) {
            app->persistBluetoothNameFromUi();
            app->refreshBluetoothUi();
        }
    }

end:
    return;
}

void AppSettings::onBluetoothScanClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (bluetoothMenuDelegatesToJoypadBle()) {
        app->refreshBluetoothUi();
        goto end;
    }

    if (s_bleScanInProgress) {
        bleCancelScan();
    } else {
        bleStartScan();
    }

    app->refreshBluetoothUi();

end:
    return;
}