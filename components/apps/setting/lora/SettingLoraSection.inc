void AppSettings::setLoRaKeyboardVisible(bool visible, lv_obj_t *textarea)
{
    if (!isUiActive() || !lv_obj_ready(_loraKeyboard)) {
        return;
    }

    if (visible) {
        if (lv_obj_ready(textarea)) {
            _loraKeyboardTarget = textarea;
        }

        if (!lv_obj_ready(_loraKeyboardTarget)) {
            return;
        }

        lv_keyboard_set_textarea(_loraKeyboard, _loraKeyboardTarget);
        lv_keyboard_set_mode(_loraKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_obj_clear_flag(_loraKeyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_loraKeyboard);
        lv_obj_align(_loraKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    } else {
        lv_keyboard_set_textarea(_loraKeyboard, nullptr);
        lv_keyboard_set_mode(_loraKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_obj_add_flag(_loraKeyboard, LV_OBJ_FLAG_HIDDEN);
        _loraKeyboardTarget = nullptr;
    }
}

void AppSettings::onLoRaConfigChangedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

#if CONFIG_JC4880_APP_LORA_MESH
    if (lv_event_get_target(e) == app->_loraEnabledSwitch) {
        if (!app->persistLoRaRadioEnabledFromUi()) {
            app->refreshLoRaUi();
        }
        return;
    }

    const auto module = lora_radio_module_from_dropdown(lv_dropdown_get_selected(app->_loraModuleDropdown));
    const bool show_e22_uart_settings = lora_module_uses_e22_uart_settings(module);
    for (size_t role_index = 0; role_index < kLoraPinRoleCount; ++role_index) {
        if ((app->_loraPinRows[role_index] == nullptr) || !lv_obj_ready(app->_loraPinRows[role_index])) {
            continue;
        }
        if (lora_module_uses_role(module, static_cast<LoraPinRole>(role_index))) {
            lv_obj_clear_flag(app->_loraPinRows[role_index], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(app->_loraPinRows[role_index], LV_OBJ_FLAG_HIDDEN);
        }
    }
    set_hidden_if_ready(app->_loraSpreadingFactorRow, show_e22_uart_settings);
    set_hidden_if_ready(app->_loraBandwidthRow, show_e22_uart_settings);
    set_hidden_if_ready(app->_loraCodingRateRow, show_e22_uart_settings);
    set_hidden_if_ready(app->_loraE22Section, !show_e22_uart_settings);

    if ((app->_loraInfoLabel != nullptr) && lv_obj_ready(app->_loraInfoLabel) && is_local_controller_backend_active()) {
        lv_label_set_text(app->_loraInfoLabel,
                          "Local Controller is active. LoRa radio is forced off to avoid conflicts with haptics and Neopixel on shared GPIOs.");
    }
#else
    (void)e;
#endif
}

void AppSettings::onLoRaSaveClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");
    app->persistLoRaConfigFromUi();
    app->setLoRaKeyboardVisible(false);
end:
    return;
}

void AppSettings::onLoRaSelfCheckClickedEventCallback(lv_event_t *e)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_BROOKESIA_LOGE("Invalid app pointer");
        return;
    }
    (void)app->startLoRaDiagnosticFromUi(false);
#endif
}

void AppSettings::onLoRaAutoDetectClickedEventCallback(lv_event_t *e)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_BROOKESIA_LOGE("Invalid app pointer");
        return;
    }
    (void)app->startLoRaDiagnosticFromUi(true);
#endif
}

void AppSettings::onLoRaDiagnosticLogClickedEventCallback(lv_event_t *e)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_BROOKESIA_LOGE("Invalid app pointer");
        return;
    }
    app->setLoRaDiagnosticLogModalVisible(true);
#endif
}

void AppSettings::onLoRaDiagnosticLogCloseEventCallback(lv_event_t *e)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_BROOKESIA_LOGE("Invalid app pointer");
        return;
    }
    app->setLoRaDiagnosticLogModalVisible(false);
#endif
}

void AppSettings::onLoRaDiagnosticCommandSendClickedEventCallback(lv_event_t *e)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if ((app == nullptr) || (app->_loraDiagnosticCommandTextArea == nullptr) || !lv_obj_ready(app->_loraDiagnosticCommandTextArea)) {
        return;
    }

    if (app->sendLoRaDiagnosticCommandFromUi(lv_textarea_get_text(app->_loraDiagnosticCommandTextArea))) {
        lv_textarea_set_text(app->_loraDiagnosticCommandTextArea, "");
        app->setLoRaKeyboardVisible(false);
    }
#endif
}

void AppSettings::onLoRaDiagnosticPresetChangedEventCallback(lv_event_t *e)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if ((app == nullptr) || (app->_loraDiagnosticPresetDropdown == nullptr) || !lv_obj_ready(app->_loraDiagnosticPresetDropdown)) {
        return;
    }

    const uint16_t selected = lv_dropdown_get_selected(app->_loraDiagnosticPresetDropdown);
    const char *command = nullptr;
    switch (selected) {
    case 1:
        command = "C1 00 09";
        break;
    case 2:
        command = "C1 08 03";
        break;
    case 3:
        command = kLoraE22WriteCurrentSettingsCommand;
        break;
    case 4:
        command = kLoraE22WriteSavedSettingsCommand;
        break;
    case 5:
        command = kLoraE22ResetRadioCommand;
        break;
    default:
        return;
    }

    (void)app->sendLoRaDiagnosticCommandFromUi(command);
    lv_dropdown_set_selected(app->_loraDiagnosticPresetDropdown, 0);
#endif
}

void AppSettings::onLoRaSelfCheckStatusTimerCallback(lv_timer_t *timer)
{
    AppSettings *app = (timer != nullptr) ? static_cast<AppSettings *>(timer->user_data) : nullptr;
    if (app == nullptr) {
        return;
    }

    app->refreshLoRaSelfCheckStatus();
}

void AppSettings::onLoRaTextAreaEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if ((code == LV_EVENT_FOCUSED) || (code == LV_EVENT_CLICKED)) {
        app->setLoRaKeyboardVisible(true, lv_event_get_target(e));
    } else if ((code == LV_EVENT_DEFOCUSED) || (code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        app->setLoRaKeyboardVisible(false);
    }

end:
    return;
}

void AppSettings::onLoRaKeyboardEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target = nullptr;

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = app->_loraKeyboardTarget;
    if ((code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        app->setLoRaKeyboardVisible(false);
        if ((target != nullptr) && lv_obj_ready(target)) {
            lv_obj_clear_state(target, LV_STATE_FOCUSED);
        }
    }

end:
    return;
}
