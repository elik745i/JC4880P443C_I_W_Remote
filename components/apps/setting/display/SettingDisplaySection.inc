void AppSettings::refreshDisplayIdleUi(void)
{
#if !APP_SETTINGS_FEATURE_DISPLAY_MENU
    return;
#endif
    if (!isUiActive()) {
        return;
    }

    if (lv_obj_ready(_displayAdaptiveBrightnessSwitch)) {
        if (_nvs_param_map[NVS_KEY_DISPLAY_ADAPTIVE]) {
            lv_obj_add_state(_displayAdaptiveBrightnessSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_displayAdaptiveBrightnessSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_displayScreensaverSwitch)) {
        if (_nvs_param_map[NVS_KEY_DISPLAY_SCREENSAVER]) {
            lv_obj_add_state(_displayScreensaverSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_displayScreensaverSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_displayTimeoffInGameSwitch)) {
        if (_nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF_IN_GAME]) {
            lv_obj_add_state(_displayTimeoffInGameSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_displayTimeoffInGameSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_displayTimeoffDropdown)) {
        lv_dropdown_set_selected(_displayTimeoffDropdown,
                                 findDropdownIndexForValue(kDisplayTimeoffOptionsSec,
                                                           sizeof(kDisplayTimeoffOptionsSec) / sizeof(kDisplayTimeoffOptionsSec[0]),
                                                           _nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF]));
    }

    if (lv_obj_ready(_displaySleepDropdown)) {
        lv_dropdown_set_selected(_displaySleepDropdown,
                                 findDropdownIndexForValue(kDisplaySleepOptionsSec,
                                                           sizeof(kDisplaySleepOptionsSec) / sizeof(kDisplaySleepOptionsSec[0]),
                                                           _nvs_param_map[NVS_KEY_DISPLAY_SLEEP]));
    }

    if (lv_obj_ready(_displayOrientationDropdown)) {
        lv_dropdown_set_selected(_displayOrientationDropdown,
                                 findDropdownIndexForValue(kDisplayOrientationOptionsDeg,
                                                           sizeof(kDisplayOrientationOptionsDeg) / sizeof(kDisplayOrientationOptionsDeg[0]),
                                                           sanitizeDisplayOrientationDegrees(_nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION])));
    }

    refreshDisplayAutorotateUi();

#if CONFIG_JC4880_FEATURE_TIME_SYNC
    refreshTimezoneUi();
#endif
}

void AppSettings::refreshDisplayAutorotateUi(void)
{
#if !APP_SETTINGS_FEATURE_DISPLAY_MENU
    return;
#endif
    if (!isUiActive()) {
        return;
    }

    const bool enabled = _nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE] != 0;
    const int32_t rotation_axis = sanitizeDisplayAutorotateAxis(_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_IMU]);

    if (lv_obj_ready(_displayAutorotateSwitch)) {
        if (enabled) {
            lv_obj_add_state(_displayAutorotateSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_displayAutorotateSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_displayAutorotateImuDropdown)) {
        lv_dropdown_set_selected(_displayAutorotateImuDropdown,
                                 findDropdownIndexForValue(kDisplayAutorotateAxisOptions,
                                                           sizeof(kDisplayAutorotateAxisOptions) / sizeof(kDisplayAutorotateAxisOptions[0]),
                                                           rotation_axis));
        if (enabled) {
            lv_obj_clear_state(_displayAutorotateImuDropdown, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(_displayAutorotateImuDropdown, LV_STATE_DISABLED);
        }
    }

    if (lv_obj_ready(_displayAutorotateInfoLabel)) {
        const char *axis_name = "X";
        std::string info_text;
        switch (rotation_axis) {
        case 1:
            axis_name = "Y";
            break;
        case 2:
            axis_name = "Z";
            break;
        default:
            break;
        }
        if (enabled) {
            info_text = std::string("Autorotate is live. The display flips from the selected ") + axis_name +
                        " axis between the saved orientation and its 180 degree opposite.";
        } else {
            info_text = std::string("Autorotate is off. When enabled, the ") + axis_name +
                        " axis drives a 180 degree UI flip.";
        }
        lv_label_set_text(_displayAutorotateInfoLabel, info_text.c_str());
    }
}

void AppSettings::updateDisplayAutorotateFromSample(const jc4880::imu::ImuSample *sample, bool sample_ok)
{
#if !APP_SETTINGS_FEATURE_DISPLAY_MENU
    (void)sample;
    (void)sample_ok;
    return;
#else
    const int32_t base_orientation = sanitizeDisplayOrientationDegrees(_nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION]);
    if (_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE] == 0) {
        if (!_displayAutorotateHasAppliedOrientation || (_displayAutorotateAppliedOrientation != base_orientation)) {
            if (applyDisplayOrientationLive(base_orientation)) {
                _displayAutorotateAppliedOrientation = base_orientation;
                _displayAutorotateHasAppliedOrientation = true;
            }
        }
        return;
    }

    if ((sample == nullptr) || !sample_ok || !sample->hasAccel) {
        return;
    }

    const int32_t rotation_axis = sanitizeDisplayAutorotateAxis(_nvs_param_map[NVS_KEY_DISPLAY_AUTOROTATE_IMU]);
    float selected_angle = sample->roll;
    switch (rotation_axis) {
    case 1:
        selected_angle = sample->pitch;
        break;
    case 2:
        selected_angle = sample->yaw;
        break;
    default:
        selected_angle = sample->roll;
        break;
    }
    const int32_t inverted_orientation = oppositeDisplayOrientationDegrees(base_orientation);
    const bool currently_inverted = _displayAutorotateHasAppliedOrientation && (_displayAutorotateAppliedOrientation == inverted_orientation);
    int32_t target_orientation = currently_inverted ? inverted_orientation : base_orientation;

    if (currently_inverted) {
        if (std::fabs(selected_angle) <= 70.0f) {
            target_orientation = base_orientation;
        }
    } else if (std::fabs(selected_angle) >= 110.0f) {
        target_orientation = inverted_orientation;
    }

    if (!_displayAutorotateHasAppliedOrientation || (target_orientation != _displayAutorotateAppliedOrientation)) {
        if (applyDisplayOrientationLive(target_orientation)) {
            _displayAutorotateAppliedOrientation = target_orientation;
            _displayAutorotateHasAppliedOrientation = true;
        }
    }
#endif
}

void AppSettings::requestDisplayOrientationPreview(int32_t orientation_degrees)
{
#if !APP_SETTINGS_FEATURE_DISPLAY_MENU
    (void)orientation_degrees;
    return;
#else
    const int32_t sanitized_orientation = sanitizeDisplayOrientationDegrees(orientation_degrees);
    const int32_t previous_orientation = sanitizeDisplayOrientationDegrees(_nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION]);
    if (sanitized_orientation == previous_orientation) {
        return;
    }

    if (_displayOrientationPreviewMsgbox != nullptr) {
        finishDisplayOrientationPreview(false);
    }

    if ((sanitized_orientation != orientation_degrees) || !applyDisplayOrientationLive(sanitized_orientation)) {
        ESP_LOGW(TAG, "Failed to apply live display orientation %ld", static_cast<long>(orientation_degrees));
        return;
    }

    _displayOrientationPreviewPrevious = previous_orientation;
    _displayOrientationPreviewPending = sanitized_orientation;
    _displayOrientationPreviewSecondsRemaining = kDisplayOrientationPreviewSeconds;
    _displayOrientationPreviewResolving = false;
    _nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION] = sanitized_orientation;

    setNvsParam(NVS_KEY_DISPLAY_ORIENTATION_PENDING, sanitized_orientation);
    setNvsParam(NVS_KEY_DISPLAY_ORIENTATION_PREVIOUS, previous_orientation);
    setNvsParam(NVS_KEY_DISPLAY_ORIENTATION_STATE, 1);

    static const char *buttons[] = {"Cancel", "OK", ""};
    _displayOrientationPreviewMsgbox = lv_msgbox_create(lv_layer_top(), "Display Orientation",
                                                        kDisplayOrientationPreviewInitialText, buttons, false);
    if ((_displayOrientationPreviewMsgbox == nullptr) || !lv_obj_is_valid(_displayOrientationPreviewMsgbox)) {
        _displayOrientationPreviewMsgbox = nullptr;
        finishDisplayOrientationPreview(false);
        return;
    }

    _displayOrientationPreviewLabel = lv_msgbox_get_text(_displayOrientationPreviewMsgbox);
    const lv_coord_t display_width = lv_disp_get_hor_res(nullptr);
    const lv_coord_t preview_msgbox_width = std::min<lv_coord_t>(340, std::max<lv_coord_t>(260, display_width - 96));
    lv_obj_set_width(_displayOrientationPreviewMsgbox, preview_msgbox_width);
    lv_obj_set_style_pad_all(_displayOrientationPreviewMsgbox, 16, 0);
    lv_obj_set_style_pad_row(_displayOrientationPreviewMsgbox, 12, 0);
    if (lv_obj_ready(_displayOrientationPreviewLabel)) {
        lv_obj_set_width(_displayOrientationPreviewLabel, lv_pct(100));
        lv_label_set_long_mode(_displayOrientationPreviewLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(_displayOrientationPreviewLabel, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_t *preview_content = lv_msgbox_get_content(_displayOrientationPreviewMsgbox);
    if (lv_obj_ready(preview_content)) {
        lv_obj_set_flex_flow(preview_content, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(preview_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(preview_content, 12, 0);

        _displayOrientationPreviewSpinner = lv_spinner_create(preview_content, 1000, 80);
        if (lv_obj_ready(_displayOrientationPreviewSpinner)) {
            lv_obj_set_size(_displayOrientationPreviewSpinner, 86, 86);
            lv_obj_clear_flag(_displayOrientationPreviewSpinner, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_arc_width(_displayOrientationPreviewSpinner, 8, LV_PART_MAIN);
            lv_obj_set_style_arc_width(_displayOrientationPreviewSpinner, 8, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(_displayOrientationPreviewSpinner, lv_color_hex(0xD7DCE6), LV_PART_MAIN);
            lv_obj_set_style_arc_color(_displayOrientationPreviewSpinner, lv_color_hex(0x2563EB), LV_PART_INDICATOR);

            _displayOrientationPreviewCountdownLabel = lv_label_create(_displayOrientationPreviewSpinner);
            if (lv_obj_ready(_displayOrientationPreviewCountdownLabel)) {
                lv_obj_set_style_text_align(_displayOrientationPreviewCountdownLabel, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_set_style_text_font(_displayOrientationPreviewCountdownLabel, &lv_font_montserrat_28, 0);
                lv_obj_center(_displayOrientationPreviewCountdownLabel);
            }
        }
    }
    lv_obj_t *preview_buttons = lv_msgbox_get_btns(_displayOrientationPreviewMsgbox);
    if (lv_obj_ready(preview_buttons)) {
        lv_obj_set_size(preview_buttons, lv_pct(100), 54);
        lv_btnmatrix_set_btn_width(preview_buttons, 0, 2);
        lv_btnmatrix_set_btn_width(preview_buttons, 1, 3);
        lv_obj_set_style_pad_all(preview_buttons, 4, 0);
        lv_obj_set_style_pad_column(preview_buttons, 10, 0);
        lv_obj_set_style_radius(preview_buttons, 6, LV_PART_ITEMS);
    }
    lv_obj_center(_displayOrientationPreviewMsgbox);
    lv_obj_add_event_cb(_displayOrientationPreviewMsgbox, onDisplayOrientationPreviewPopupEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(_displayOrientationPreviewMsgbox, onDisplayOrientationPreviewPopupEventCallback,
                        LV_EVENT_DELETE, this);
    updateDisplayOrientationPreviewPopup();

    _displayOrientationPreviewTimer = lv_timer_create(onDisplayOrientationPreviewTimerCallback, 1000, this);
    if (_displayOrientationPreviewTimer == nullptr) {
        finishDisplayOrientationPreview(false);
    }
#endif
}

void AppSettings::updateDisplayOrientationPreviewPopup(void)
{
#if APP_SETTINGS_FEATURE_DISPLAY_MENU
    if (lv_obj_ready(_displayOrientationPreviewLabel)) {
        lv_label_set_text(_displayOrientationPreviewLabel, kDisplayOrientationPreviewInitialText);
    }

    if (lv_obj_ready(_displayOrientationPreviewCountdownLabel)) {
        char countdown[8] = {0};
        snprintf(countdown, sizeof(countdown), "%ld", static_cast<long>(_displayOrientationPreviewSecondsRemaining));
        lv_label_set_text(_displayOrientationPreviewCountdownLabel, countdown);
        lv_obj_center(_displayOrientationPreviewCountdownLabel);
    }
#endif
}

void AppSettings::finishDisplayOrientationPreview(bool keep_orientation)
{
#if APP_SETTINGS_FEATURE_DISPLAY_MENU
    if (_displayOrientationPreviewTimer != nullptr) {
        lv_timer_del(_displayOrientationPreviewTimer);
        _displayOrientationPreviewTimer = nullptr;
    }

    const int32_t final_orientation = keep_orientation
                                          ? sanitizeDisplayOrientationDegrees(_displayOrientationPreviewPending)
                                          : sanitizeDisplayOrientationDegrees(_displayOrientationPreviewPrevious);
    applyDisplayOrientationLive(final_orientation);
    _nvs_param_map[NVS_KEY_DISPLAY_ORIENTATION] = final_orientation;
    setNvsParam(NVS_KEY_DISPLAY_ORIENTATION, final_orientation);
    setNvsParam(NVS_KEY_DISPLAY_ORIENTATION_PENDING, final_orientation);
    setNvsParam(NVS_KEY_DISPLAY_ORIENTATION_PREVIOUS, final_orientation);
    setNvsParam(NVS_KEY_DISPLAY_ORIENTATION_STATE, 0);

    if (lv_obj_ready(_displayOrientationDropdown)) {
        lv_dropdown_set_selected(_displayOrientationDropdown,
                                 findDropdownIndexForValue(kDisplayOrientationOptionsDeg,
                                                           sizeof(kDisplayOrientationOptionsDeg) / sizeof(kDisplayOrientationOptionsDeg[0]),
                                                           final_orientation));
    }

    _displayOrientationPreviewResolving = true;
    if (lv_obj_ready(_displayOrientationPreviewMsgbox)) {
        lv_msgbox_close_async(_displayOrientationPreviewMsgbox);
    } else {
        _displayOrientationPreviewMsgbox = nullptr;
        _displayOrientationPreviewLabel = nullptr;
        _displayOrientationPreviewSpinner = nullptr;
        _displayOrientationPreviewCountdownLabel = nullptr;
        _displayOrientationPreviewSecondsRemaining = 0;
        _displayOrientationPreviewResolving = false;
    }
#else
    (void)keep_orientation;
#endif
}

void AppSettings::applyNeopixelConfig(void)
{
#if !APP_SETTINGS_FEATURE_DISPLAY_MENU
    return;
#else
    jc4880_joypad_config_t joypad_config = {};
    const bool local_controller_active = jc4880_joypad_get_config(&joypad_config) &&
                                         (joypad_config.backend == JC4880_JOYPAD_BACKEND_MANUAL);
    const int32_t neopixel_gpio = sanitizeNeopixelGpio(_nvs_param_map[NVS_KEY_NEOPIXEL_GPIO]);
    jc4880_neopixel_apply_config((_nvs_param_map[NVS_KEY_NEOPIXEL_POWER] != 0) && local_controller_active,
                                 neopixel_gpio,
                                 std::clamp(static_cast<int32_t>(_nvs_param_map[NVS_KEY_NEOPIXEL_BRIGHTNESS]), static_cast<int32_t>(NEOPIXEL_BRIGHTNESS_MIN), static_cast<int32_t>(NEOPIXEL_BRIGHTNESS_MAX)),
                                 std::clamp(static_cast<int32_t>(_nvs_param_map[NVS_KEY_NEOPIXEL_PALETTE]), static_cast<int32_t>(0), static_cast<int32_t>(11)),
                                 std::clamp(static_cast<int32_t>(_nvs_param_map[NVS_KEY_NEOPIXEL_EFFECT]), static_cast<int32_t>(0), static_cast<int32_t>(20)));
#endif
}

void AppSettings::refreshTimezoneUi(void)
{
#if !CONFIG_JC4880_FEATURE_TIME_SYNC
    return;
#endif
    if (!isUiActive()) {
        return;
    }

    if (lv_obj_ready(_displayTimezoneDropdown)) {
        lv_dropdown_set_selected(_displayTimezoneDropdown,
                                 findTimezoneDropdownIndexForOffset(_nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE]));
    }

    if (lv_obj_ready(_displayAutoTimezoneSwitch)) {
        if (_nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO]) {
            lv_obj_add_state(_displayAutoTimezoneSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_displayAutoTimezoneSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_displayTimezoneInfoLabel)) {
        const bool auto_enabled = _nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO] != 0;
        const TimezoneOption &manual_option = getTimezoneOptionForOffset(_nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE]);

        std::string label = std::string("Manual: ") + manual_option.label;
        if (auto_enabled) {
            if (_hasAutoDetectedTimezone) {
                const TimezoneOption &detected_option = getTimezoneOptionForOffset(_autoDetectedTimezoneOffsetMinutes);
                label = std::string("Auto: ") + detected_option.label;
                if (!_autoTimezoneStatus.empty()) {
                    label += " С‚РђРІ " + _autoTimezoneStatus;
                }
            } else if (!_autoTimezoneStatus.empty()) {
                label = _autoTimezoneStatus + " С‚РђРІ fallback " + manual_option.label;
            } else {
                label = std::string("Auto timezone enabled С‚РђРІ fallback ") + manual_option.label;
            }
        }

        lv_label_set_text(_displayTimezoneInfoLabel, label.c_str());
    }
}

void AppSettings::applyDisplayIdleSettings(void)
{
    bsp_extra_display_idle_set_base_brightness(_nvs_param_map[NVS_KEY_DISPLAY_BRIGHTNESS]);
    bsp_extra_display_idle_configure(_nvs_param_map[NVS_KEY_DISPLAY_ADAPTIVE],
                                     _nvs_param_map[NVS_KEY_DISPLAY_SCREENSAVER],
                                     _nvs_param_map[NVS_KEY_DISPLAY_TIMEOFF],
                                     _nvs_param_map[NVS_KEY_DISPLAY_SLEEP]);
}

void AppSettings::applyManualTimezonePreference(void)
{
    const TimezoneOption &option = getTimezoneOptionForOffset(_nvs_param_map[NVS_KEY_DISPLAY_TIMEZONE]);
    app_sntp_set_timezone(option.tz);
}

bool AppSettings::syncAutoTimezoneFromInternet(void)
{
    std::string response;

    esp_http_client_config_t config = {};
    config.url = kTimezoneLookupUrl;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 12000;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = [](esp_http_client_event_t *event) {
        if ((event == nullptr) || (event->user_data == nullptr) || (event->event_id != HTTP_EVENT_ON_DATA) ||
            (event->data == nullptr) || (event->data_len <= 0)) {
            return ESP_OK;
        }

        auto *body = static_cast<std::string *>(event->user_data);
        body->append(static_cast<const char *>(event->data), static_cast<size_t>(event->data_len));
        return ESP_OK;
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        _autoTimezoneStatus = "Auto timezone lookup unavailable";
        refreshTimezoneUi();
        return false;
    }

    bool ok = false;
    if ((esp_http_client_set_header(client, "Accept", "application/json") == ESP_OK) &&
        (esp_http_client_set_header(client, "User-Agent", "JC4880P443C-IW-Remote") == ESP_OK) &&
        (esp_http_client_perform(client) == ESP_OK) &&
        (esp_http_client_get_status_code(client) == 200)) {
        cJSON *root = cJSON_Parse(response.c_str());
        if (cJSON_IsObject(root)) {
            const std::string offset_text = trim_copy(safe_json_string(root, "utc_offset"));
            const std::string city = trim_copy(safe_json_string(root, "city"));
            const std::string region = trim_copy(safe_json_string(root, "region"));
            const std::string country = trim_copy(safe_json_string(root, "country_name"));
            int32_t detected_minutes = 0;

            if (parseUtcOffsetMinutes(offset_text, detected_minutes)) {
                _autoDetectedTimezoneOffsetMinutes = detected_minutes;
                _hasAutoDetectedTimezone = true;

                const TimezoneOption &option = getTimezoneOptionForOffset(detected_minutes);
                app_sntp_set_timezone(option.tz);

                std::string location = city;
                if (!region.empty()) {
                    location = location.empty() ? region : (location + ", " + region);
                }
                if (!country.empty()) {
                    location = location.empty() ? country : (location + ", " + country);
                }

                _autoTimezoneStatus = location.empty() ? std::string("Detected from internet") : location;
                ok = true;
            }
        }
        cJSON_Delete(root);
    }

    esp_http_client_cleanup(client);

    if (!ok) {
        _hasAutoDetectedTimezone = false;
        _autoTimezoneStatus = "Auto timezone lookup failed";
        applyManualTimezonePreference();
    }

    refreshTimezoneUi();
    return ok;
}