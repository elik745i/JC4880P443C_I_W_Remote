void AppSettings::onSliderPanelLightSwitchValueChangeEventCallback(lv_event_t *e)
{
    brightness = lv_slider_get_value(ui_SliderPanelScreenSettingLightSwitch1);

    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (brightness != app->_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS]) {
        if (bsp_display_brightness_set(brightness) != ESP_OK) {
            ESP_LOGE(TAG, "Set brightness failed");
            lv_slider_set_value(ui_SliderPanelScreenSettingLightSwitch1, app->_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS], LV_ANIM_OFF);
            return;
        }
        app->_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS] = brightness;
        app->setNvsParam(NVS_KEY_DISPLAY_BRIGHTNESS, brightness);
        app->applyDisplayIdleSettings();
    }

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingNeopixelPowerValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    bool enabled = false;
    lv_obj_t *target = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid Neopixel power switch");
    enabled = (lv_obj_get_state(target) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_NEOPIXEL_POWER] = enabled ? 1 : 0;
    app->setNvsParam(NVS_KEY_NEOPIXEL_POWER, enabled ? 1 : 0);
    app->applyNeopixelConfig();
    app->refreshDisplayIdleUi();
    app->refreshJoypadUi();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingNeopixelGpioValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    uint16_t selected = 0;
    int32_t value = -1;
    lv_obj_t *target = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid Neopixel GPIO dropdown");
    selected = lv_dropdown_get_selected(target);
    value = getDropdownValueForIndex(kNeopixelGpioOptions,
                                     sizeof(kNeopixelGpioOptions) / sizeof(kNeopixelGpioOptions[0]),
                                     selected);
    value = sanitizeNeopixelGpio(value);
    app->_nvs_param_map[NVS_KEY_NEOPIXEL_GPIO] = value;
    app->setNvsParam(NVS_KEY_NEOPIXEL_GPIO, value);
    app->applyNeopixelConfig();
    app->refreshDisplayIdleUi();
    app->refreshJoypadUi();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingNeopixelPaletteValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    uint16_t selected = 0;
    int32_t value = 0;
    lv_obj_t *target = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid Neopixel palette dropdown");
    selected = lv_dropdown_get_selected(target);
    value = getDropdownValueForIndex(kNeopixelPaletteOptions,
                                     sizeof(kNeopixelPaletteOptions) / sizeof(kNeopixelPaletteOptions[0]),
                                     selected);
    app->_nvs_param_map[NVS_KEY_NEOPIXEL_PALETTE] = value;
    app->setNvsParam(NVS_KEY_NEOPIXEL_PALETTE, value);
    app->applyNeopixelConfig();
    app->refreshDisplayIdleUi();
    app->refreshJoypadUi();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingNeopixelEffectValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    uint16_t selected = 0;
    int32_t value = 0;
    lv_obj_t *target = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid Neopixel effect dropdown");
    selected = lv_dropdown_get_selected(target);
    value = getDropdownValueForIndex(kNeopixelEffectOptions,
                                     sizeof(kNeopixelEffectOptions) / sizeof(kNeopixelEffectOptions[0]),
                                     selected);
    app->_nvs_param_map[NVS_KEY_NEOPIXEL_EFFECT] = value;
    app->setNvsParam(NVS_KEY_NEOPIXEL_EFFECT, value);
    app->applyNeopixelConfig();
    app->refreshDisplayIdleUi();
    app->refreshJoypadUi();

end:
    return;
}

void AppSettings::onSliderPanelScreenSettingNeopixelBrightnessValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    int32_t brightness_value = 0;
    lv_obj_t *target = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid Neopixel brightness slider");
    brightness_value = lv_slider_get_value(target);
    app->_nvs_param_map[NVS_KEY_NEOPIXEL_BRIGHTNESS] = brightness_value;
    app->setNvsParam(NVS_KEY_NEOPIXEL_BRIGHTNESS, brightness_value);
    app->applyNeopixelConfig();
    app->refreshDisplayIdleUi();
    app->refreshJoypadUi();

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingAdaptiveBrightnessValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    enabled = (lv_obj_get_state(app->_displayAdaptiveBrightnessSwitch) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_DISPLAY_ADAPTIVE] = enabled;
    app->setNvsParam(NVS_KEY_DISPLAY_ADAPTIVE, enabled ? 1 : 0);
    app->applyDisplayIdleSettings();

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingScreensaverValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    enabled = (lv_obj_get_state(app->_displayScreensaverSwitch) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_DISPLAY_SCREENSAVER] = enabled;
    app->setNvsParam(NVS_KEY_DISPLAY_SCREENSAVER, enabled ? 1 : 0);
    app->applyDisplayIdleSettings();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingTimeoffIntervalValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    uint16_t selected_index = 0;
    int32_t selected_value = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    selected_index = lv_dropdown_get_selected(app->_displayTimeoffDropdown);
    selected_value = getDropdownValueForIndex(kDisplayTimeoffOptionsSec,
                                              sizeof(kDisplayTimeoffOptionsSec) / sizeof(kDisplayTimeoffOptionsSec[0]),
                                              selected_index);
    app->_nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF] = selected_value;
    app->setNvsParam(NVS_KEY_DISPLAY_TIMEOFF, selected_value);
    app->applyDisplayIdleSettings();

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingTimeoffInGameValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *target = nullptr;
    bool enabled = false;
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    target = lv_event_get_target(e);
    if (target == nullptr) {
        ESP_LOGE(TAG, "Invalid screen timeoff in game switch");
        return;
    }

    enabled = (lv_obj_get_state(target) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF_IN_GAME] = enabled;
    app->setNvsParam(NVS_KEY_DISPLAY_TIMEOFF_IN_GAME, enabled ? 1 : 0);
}

void AppSettings::onDropdownPanelScreenSettingSleepIntervalValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    uint16_t selected_index = 0;
    int32_t selected_value = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    selected_index = lv_dropdown_get_selected(app->_displaySleepDropdown);
    selected_value = getDropdownValueForIndex(kDisplaySleepOptionsSec,
                                              sizeof(kDisplaySleepOptionsSec) / sizeof(kDisplaySleepOptionsSec[0]),
                                              selected_index);
    app->_nvs_param_map[NVS_KEY_DISPLAY_SLEEP] = selected_value;
    app->setNvsParam(NVS_KEY_DISPLAY_SLEEP, selected_value);
    app->applyDisplayIdleSettings();

end:
    return;
}

void AppSettings::onDisplayOrientationPreviewPopupEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *msgbox = lv_event_get_current_target(e);
    const lv_event_code_t code = lv_event_get_code(e);
    if (app == nullptr) {
        return;
    }

    if (code == LV_EVENT_DELETE) {
        if (app->_displayOrientationPreviewResolving) {
            app->_displayOrientationPreviewMsgbox = nullptr;
            app->_displayOrientationPreviewLabel = nullptr;
            app->_displayOrientationPreviewSpinner = nullptr;
            app->_displayOrientationPreviewCountdownLabel = nullptr;
            app->_displayOrientationPreviewSecondsRemaining = 0;
            app->_displayOrientationPreviewResolving = false;
            return;
        }

        if (msgbox == app->_displayOrientationPreviewMsgbox) {
            app->_displayOrientationPreviewMsgbox = nullptr;
            app->_displayOrientationPreviewLabel = nullptr;
            app->_displayOrientationPreviewSpinner = nullptr;
            app->_displayOrientationPreviewCountdownLabel = nullptr;
            app->finishDisplayOrientationPreview(false);
        }
        return;
    }

    if ((code != LV_EVENT_VALUE_CHANGED) || (msgbox == nullptr)) {
        return;
    }

    const char *button_text = lv_msgbox_get_active_btn_text(msgbox);
    if (button_text == nullptr) {
        return;
    }

    if (strcmp(button_text, "OK") == 0) {
        app->finishDisplayOrientationPreview(true);
    } else if (strcmp(button_text, "Cancel") == 0) {
        app->finishDisplayOrientationPreview(false);
    }
}

void AppSettings::onDisplayOrientationPreviewTimerCallback(lv_timer_t *timer)
{
    AppSettings *app = (timer != nullptr) ? static_cast<AppSettings *>(timer->user_data) : nullptr;
    if (app == nullptr) {
        return;
    }

    if (app->_displayOrientationPreviewSecondsRemaining > 0) {
        --app->_displayOrientationPreviewSecondsRemaining;
    }

    if (app->_displayOrientationPreviewSecondsRemaining <= 0) {
        app->finishDisplayOrientationPreview(false);
        return;
    }

    app->updateDisplayOrientationPreviewPopup();
}

void AppSettings::onDropdownPanelScreenSettingOrientationValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    uint16_t selected_index = 0;
    int32_t selected_value = 0;
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    const int32_t previous_value = sanitizeDisplayOrientationDegrees(app->_nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION]);

    selected_index = lv_dropdown_get_selected(app->_displayOrientationDropdown);
    selected_value = getDropdownValueForIndex(kDisplayOrientationOptionsDeg,
                                              sizeof(kDisplayOrientationOptionsDeg) / sizeof(kDisplayOrientationOptionsDeg[0]),
                                              selected_index);
    selected_value = sanitizeDisplayOrientationDegrees(selected_value);
    if (selected_value == previous_value) {
        return;
    }

    app->requestDisplayOrientationPreview(selected_value);
    ESP_LOGI(TAG, "Display orientation updated live to %ld degrees", static_cast<long>(selected_value));
}

void AppSettings::onSwitchPanelScreenSettingAutorotateValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *target = nullptr;
    bool enabled = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid autorotate switch");
    enabled = (lv_obj_get_state(target) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE] = enabled ? 1 : 0;
    app->setNvsParam(NVS_KEY_DISPLAY_AUTOROTATE, enabled ? 1 : 0);
    app->_displayAutorotateHasAppliedOrientation = false;
    if (!enabled) {
        app->updateDisplayAutorotateFromSample(nullptr, false);
    }
    app->refreshDisplayAutorotateUi();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingAutorotateImuValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *target = nullptr;
    uint16_t selected = 0;
    int32_t value = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid rotation axis dropdown");
    selected = lv_dropdown_get_selected(target);
    value = getDropdownValueForIndex(kDisplayAutorotateAxisOptions,
                                     sizeof(kDisplayAutorotateAxisOptions) / sizeof(kDisplayAutorotateAxisOptions[0]),
                                     selected);
    value = sanitizeDisplayAutorotateAxis(value);
    app->_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_IMU] = value;
    app->setNvsParam(NVS_KEY_DISPLAY_AUTOROTATE_IMU, value);
    app->refreshDisplayAutorotateUi();

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingAutoTimezoneValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    enabled = (lv_obj_get_state(app->_displayAutoTimezoneSwitch) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO] = enabled;
    app->setNvsParam(NVS_KEY_DISPLAY_TZ_AUTO, enabled ? 1 : 0);

    if (enabled) {
        app->_hasAutoDetectedTimezone = false;
        app->_autoTimezoneStatus = "Auto timezone enabled";
        app->_autoTimezoneRefreshPending = true;
        if (xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_CONNECTED) {
            app->syncAutoTimezoneFromInternet();
            app->_autoTimezoneRefreshPending = false;
            app_sntp_init();
        }
    } else {
        app->_autoTimezoneRefreshPending = false;
        app->_hasAutoDetectedTimezone = false;
        app->_autoTimezoneStatus.clear();
        app->applyManualTimezonePreference();
        app_sntp_init();
    }

    app->refreshTimezoneUi();

end:
    return;
}

void AppSettings::onDropdownPanelScreenSettingTimezoneValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    uint16_t selected_index = 0;
    int32_t selected_offset_minutes = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    selected_index = lv_dropdown_get_selected(app->_displayTimezoneDropdown);
    selected_offset_minutes = getTimezoneOptionForIndex(selected_index).offset_minutes;
    app->_nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE] = selected_offset_minutes;
    app->setNvsParam(NVS_KEY_DISPLAY_TIMEZONE, selected_offset_minutes);

    if (!app->_nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO]) {
        app->applyManualTimezonePreference();
        app_sntp_init();
    }

    app->refreshTimezoneUi();

end:
    return;
}