void AppSettings::ensureGpioTestScreen(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    if ((_gpioTestScreen != nullptr) && lv_obj_ready(_gpioTestScreen)) {
        return;
    }

    _gpioTestScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_gpioTestScreen, lv_color_hex(0xE5F3FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_gpioTestScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_gpioTestScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *gpioTitle = lv_label_create(_gpioTestScreen);
    lv_label_set_text(gpioTitle, "GPIO Control");
    lv_obj_set_style_text_font(gpioTitle, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(gpioTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(gpioTitle, LV_ALIGN_TOP_MID, 0, 52);

    lv_obj_t *gpioPanel = lv_obj_create(_gpioTestScreen);
    lv_obj_set_size(gpioPanel, lv_pct(92), 650);
    lv_obj_align(gpioPanel, LV_ALIGN_TOP_MID, 0, 114);
    lv_obj_set_style_radius(gpioPanel, 20, 0);
    lv_obj_set_style_border_width(gpioPanel, 0, 0);
    lv_obj_set_style_bg_color(gpioPanel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_all(gpioPanel, 14, 0);
    lv_obj_set_style_pad_row(gpioPanel, 12, 0);
    lv_obj_set_flex_flow(gpioPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(gpioPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(gpioPanel, LV_DIR_VER);

    lv_obj_t *gpioModeRow = create_settings_toggle_row(gpioPanel, "Mode");
    _hardwareGpioTestModeDropdown = lv_dropdown_create(gpioModeRow);
    lv_dropdown_set_options_static(_hardwareGpioTestModeDropdown, kHardwareGpioTestModeOptions);
    lv_obj_set_width(_hardwareGpioTestModeDropdown, 150);
    lv_obj_align(_hardwareGpioTestModeDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_hardwareGpioTestModeDropdown, onHardwareGpioTestModeChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *gpioAllRow = lv_obj_create(gpioPanel);
    lv_obj_set_width(gpioAllRow, lv_pct(100));
    lv_obj_set_height(gpioAllRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(gpioAllRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(gpioAllRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gpioAllRow, 0, 0);
    lv_obj_set_style_pad_all(gpioAllRow, 0, 0);
    lv_obj_set_style_pad_column(gpioAllRow, 10, 0);
    lv_obj_set_flex_flow(gpioAllRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gpioAllRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _hardwareGpioTestAllLowButton = lv_btn_create(gpioAllRow);
    lv_obj_set_size(_hardwareGpioTestAllLowButton, 120, 42);
    lv_obj_set_style_radius(_hardwareGpioTestAllLowButton, 12, 0);
    lv_obj_set_style_border_width(_hardwareGpioTestAllLowButton, 0, 0);
    lv_obj_set_style_bg_color(_hardwareGpioTestAllLowButton, lv_color_hex(0xCBD5E1), 0);
    lv_obj_add_event_cb(_hardwareGpioTestAllLowButton, onHardwareGpioTestClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *allLowLabel = lv_label_create(_hardwareGpioTestAllLowButton);
    lv_label_set_text(allLowLabel, "All Low");
    lv_obj_set_style_text_color(allLowLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_center(allLowLabel);

    _hardwareGpioTestAllHighButton = lv_btn_create(gpioAllRow);
    lv_obj_set_size(_hardwareGpioTestAllHighButton, 120, 42);
    lv_obj_set_style_radius(_hardwareGpioTestAllHighButton, 12, 0);
    lv_obj_set_style_border_width(_hardwareGpioTestAllHighButton, 0, 0);
    lv_obj_set_style_bg_color(_hardwareGpioTestAllHighButton, lv_color_hex(0xCBD5E1), 0);
    lv_obj_add_event_cb(_hardwareGpioTestAllHighButton, onHardwareGpioTestClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *allHighLabel = lv_label_create(_hardwareGpioTestAllHighButton);
    lv_label_set_text(allHighLabel, "All High");
    lv_obj_set_style_text_color(allHighLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_center(allHighLabel);

    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        appendHardwareGpioTestPinRow(gpioPanel, index);
    }

    appendHardwareGpioTestAudioRows(gpioPanel);

    scanHardwareGpioTestAudioFiles(true);
    refreshHardwareGpioTestAudioDropdowns();
    _hardwareGpioTestAudioScanPending = false;

    _hardwareGpioTestKeyboard = lv_keyboard_create(_gpioTestScreen);
    lv_obj_set_size(_hardwareGpioTestKeyboard, lv_pct(100), lv_pct(34));
    lv_obj_align(_hardwareGpioTestKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(_hardwareGpioTestKeyboard, LV_OBJ_FLAG_HIDDEN);
    jc4880_keyboard_install_case_behavior(_hardwareGpioTestKeyboard);
    lv_obj_add_event_cb(_hardwareGpioTestKeyboard, onHardwareGpioTestKeyboardEventCallback, LV_EVENT_ALL, this);

    _hardwareGpioTestTimerTick = lv_timer_create(onHardwareGpioTestTimerTickCallback, 250, this);

    setHardwareGpioTestMode(HardwareGpioTestMode::Output);
    loadHardwareGpioTestAudioSelections();
    loadHardwareGpioTestSchedules();
    applyHardwareGpioTestScheduledOutputs();
    refreshHardwareGpioTestAudioDropdowns();
    refreshHardwareGpioTestUi();
    _screen_list[UI_GPIO_TEST_SETTING_INDEX] = _gpioTestScreen;
    lv_obj_add_event_cb(_gpioTestScreen, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
#endif
}

void AppSettings::setHardwareGpioTestKeyboardVisible(bool visible, lv_obj_t *textarea)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)visible;
    (void)textarea;
    return;
#else
    if (!isUiActive() || !lv_obj_ready(_hardwareGpioTestKeyboard)) {
        return;
    }

    if (visible) {
        if (textarea != nullptr) {
            _hardwareGpioTestKeyboardTarget = textarea;
        }

        if (!lv_obj_ready(_hardwareGpioTestKeyboardTarget)) {
            return;
        }

        lv_keyboard_set_textarea(_hardwareGpioTestKeyboard, _hardwareGpioTestKeyboardTarget);
        lv_keyboard_set_mode(_hardwareGpioTestKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_obj_clear_flag(_hardwareGpioTestKeyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_hardwareGpioTestKeyboard);
        lv_obj_align(_hardwareGpioTestKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    } else {
        lv_keyboard_set_textarea(_hardwareGpioTestKeyboard, nullptr);
        lv_keyboard_set_mode(_hardwareGpioTestKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_obj_add_flag(_hardwareGpioTestKeyboard, LV_OBJ_FLAG_HIDDEN);
        _hardwareGpioTestKeyboardTarget = nullptr;
    }
#endif
}

void __attribute__((noinline)) AppSettings::appendHardwareGpioTestPinRow(lv_obj_t *gpioPanel, size_t index)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)gpioPanel;
    (void)index;
    return;
#else
    lv_obj_t *row = lv_obj_create(gpioPanel);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(row, 16, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xEFF6FF), 0);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_style_pad_top(row, 10, 0);
    lv_obj_set_style_pad_bottom(row, 10, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_label_create(row);
    lv_obj_set_width(label, 86);
    char gpioLabel[24] = {0};
    snprintf(gpioLabel, sizeof(gpioLabel), "GPIO %ld", static_cast<long>(kHardwareGpioTestPins[index]));
    lv_label_set_text(label, gpioLabel);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x0F172A), 0);

    _hardwareGpioTestLowButtons[index] = lv_btn_create(row);
    lv_obj_set_size(_hardwareGpioTestLowButtons[index], 96, 40);
    lv_obj_set_style_radius(_hardwareGpioTestLowButtons[index], 12, 0);
    lv_obj_set_style_border_width(_hardwareGpioTestLowButtons[index], 0, 0);
    lv_obj_set_style_bg_color(_hardwareGpioTestLowButtons[index], lv_color_hex(0xCBD5E1), 0);
    lv_obj_add_event_cb(_hardwareGpioTestLowButtons[index], onHardwareGpioTestClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *lowLabel = lv_label_create(_hardwareGpioTestLowButtons[index]);
    lv_label_set_text(lowLabel, "Low");
    lv_obj_set_style_text_color(lowLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_center(lowLabel);

    _hardwareGpioTestHighButtons[index] = lv_btn_create(row);
    lv_obj_set_size(_hardwareGpioTestHighButtons[index], 96, 40);
    lv_obj_set_style_radius(_hardwareGpioTestHighButtons[index], 12, 0);
    lv_obj_set_style_border_width(_hardwareGpioTestHighButtons[index], 0, 0);
    lv_obj_set_style_bg_color(_hardwareGpioTestHighButtons[index], lv_color_hex(0xCBD5E1), 0);
    lv_obj_add_event_cb(_hardwareGpioTestHighButtons[index], onHardwareGpioTestClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *highLabel = lv_label_create(_hardwareGpioTestHighButtons[index]);
    lv_label_set_text(highLabel, "High");
    lv_obj_set_style_text_color(highLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_center(highLabel);

    _hardwareGpioTestPwmSliders[index] = lv_slider_create(row);
    lv_obj_set_width(_hardwareGpioTestPwmSliders[index], 170);
    lv_obj_set_flex_grow(_hardwareGpioTestPwmSliders[index], 1);
    lv_slider_set_range(_hardwareGpioTestPwmSliders[index], 0, 100);
    lv_obj_add_flag(_hardwareGpioTestPwmSliders[index], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_hardwareGpioTestPwmSliders[index], onHardwareGpioTestPwmChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

    _hardwareGpioTestWaveDropdowns[index] = lv_dropdown_create(row);
    lv_dropdown_set_options_static(_hardwareGpioTestWaveDropdowns[index], kHardwareGpioTestWaveOptions);
    lv_obj_set_width(_hardwareGpioTestWaveDropdowns[index], 118);
    lv_obj_add_flag(_hardwareGpioTestWaveDropdowns[index], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_hardwareGpioTestWaveDropdowns[index], onHardwareGpioTestWaveChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

    _hardwareGpioTestWaveDurationInputs[index] = lv_textarea_create(row);
    lv_obj_set_size(_hardwareGpioTestWaveDurationInputs[index], 72, 40);
    lv_textarea_set_one_line(_hardwareGpioTestWaveDurationInputs[index], true);
    lv_textarea_set_accepted_chars(_hardwareGpioTestWaveDurationInputs[index], "0123456789.");
    lv_textarea_set_max_length(_hardwareGpioTestWaveDurationInputs[index], 4);
    lv_textarea_set_placeholder_text(_hardwareGpioTestWaveDurationInputs[index], "1.0");
    lv_obj_set_style_radius(_hardwareGpioTestWaveDurationInputs[index], 12, 0);
    lv_obj_set_style_border_width(_hardwareGpioTestWaveDurationInputs[index], 1, 0);
    lv_obj_set_style_border_color(_hardwareGpioTestWaveDurationInputs[index], lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_bg_color(_hardwareGpioTestWaveDurationInputs[index], lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_left(_hardwareGpioTestWaveDurationInputs[index], 10, 0);
    lv_obj_set_style_pad_right(_hardwareGpioTestWaveDurationInputs[index], 10, 0);
    char duration_text[8] = {0};
    format_hardware_gpio_test_wave_tenths(_hardwareGpioTestWaveDurationsTenths[index], duration_text, sizeof(duration_text));
    lv_textarea_set_text(_hardwareGpioTestWaveDurationInputs[index], duration_text);
    lv_obj_add_flag(_hardwareGpioTestWaveDurationInputs[index], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_hardwareGpioTestWaveDurationInputs[index], onHardwareGpioTestWaveDurationEventCallback, LV_EVENT_ALL, this);

    _hardwareGpioTestTimerDurationInputs[index] = lv_textarea_create(row);
    lv_obj_set_size(_hardwareGpioTestTimerDurationInputs[index], 76, 40);
    lv_textarea_set_one_line(_hardwareGpioTestTimerDurationInputs[index], true);
    lv_textarea_set_accepted_chars(_hardwareGpioTestTimerDurationInputs[index], "0123456789");
    lv_textarea_set_max_length(_hardwareGpioTestTimerDurationInputs[index], 4);
    lv_textarea_set_placeholder_text(_hardwareGpioTestTimerDurationInputs[index], "5");
    lv_obj_set_style_radius(_hardwareGpioTestTimerDurationInputs[index], 12, 0);
    lv_obj_set_style_border_width(_hardwareGpioTestTimerDurationInputs[index], 1, 0);
    lv_obj_set_style_border_color(_hardwareGpioTestTimerDurationInputs[index], lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_bg_color(_hardwareGpioTestTimerDurationInputs[index], lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_left(_hardwareGpioTestTimerDurationInputs[index], 10, 0);
    lv_obj_set_style_pad_right(_hardwareGpioTestTimerDurationInputs[index], 10, 0);
    char timer_text[8] = {0};
    format_hardware_gpio_test_timer_seconds(_hardwareGpioTestTimerDurationsSec[index], timer_text, sizeof(timer_text));
    lv_textarea_set_text(_hardwareGpioTestTimerDurationInputs[index], timer_text);
    lv_obj_add_flag(_hardwareGpioTestTimerDurationInputs[index], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_hardwareGpioTestTimerDurationInputs[index], onHardwareGpioTestTimerDurationEventCallback, LV_EVENT_ALL, this);

    _hardwareGpioTestTimerLevelDropdowns[index] = lv_dropdown_create(row);
    lv_dropdown_set_options_static(_hardwareGpioTestTimerLevelDropdowns[index], kHardwareGpioTestTimerLevelOptions);
    lv_obj_set_width(_hardwareGpioTestTimerLevelDropdowns[index], 96);
    lv_obj_add_flag(_hardwareGpioTestTimerLevelDropdowns[index], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_hardwareGpioTestTimerLevelDropdowns[index], onHardwareGpioTestTimerLevelChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

    _hardwareGpioTestTimerButtons[index] = lv_btn_create(row);
    lv_obj_set_size(_hardwareGpioTestTimerButtons[index], 92, 40);
    lv_obj_set_style_radius(_hardwareGpioTestTimerButtons[index], 12, 0);
    lv_obj_set_style_border_width(_hardwareGpioTestTimerButtons[index], 0, 0);
    lv_obj_set_style_bg_color(_hardwareGpioTestTimerButtons[index], lv_color_hex(0xCBD5E1), 0);
    lv_obj_add_flag(_hardwareGpioTestTimerButtons[index], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_hardwareGpioTestTimerButtons[index], onHardwareGpioTestTimerButtonClickedEventCallback, LV_EVENT_CLICKED, this);
    _hardwareGpioTestTimerButtonLabels[index] = lv_label_create(_hardwareGpioTestTimerButtons[index]);
    lv_label_set_text(_hardwareGpioTestTimerButtonLabels[index], "Start");
    lv_obj_set_style_text_color(_hardwareGpioTestTimerButtonLabels[index], lv_color_hex(0x0F172A), 0);
    lv_obj_center(_hardwareGpioTestTimerButtonLabels[index]);

    _hardwareGpioTestAlarmTimeInputs[index] = lv_textarea_create(row);
    lv_obj_set_size(_hardwareGpioTestAlarmTimeInputs[index], 84, 40);
    lv_textarea_set_one_line(_hardwareGpioTestAlarmTimeInputs[index], true);
    lv_textarea_set_accepted_chars(_hardwareGpioTestAlarmTimeInputs[index], "0123456789:");
    lv_textarea_set_max_length(_hardwareGpioTestAlarmTimeInputs[index], 5);
    lv_textarea_set_placeholder_text(_hardwareGpioTestAlarmTimeInputs[index], "08:00");
    lv_obj_set_style_radius(_hardwareGpioTestAlarmTimeInputs[index], 12, 0);
    lv_obj_set_style_border_width(_hardwareGpioTestAlarmTimeInputs[index], 1, 0);
    lv_obj_set_style_border_color(_hardwareGpioTestAlarmTimeInputs[index], lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_bg_color(_hardwareGpioTestAlarmTimeInputs[index], lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_left(_hardwareGpioTestAlarmTimeInputs[index], 10, 0);
    lv_obj_set_style_pad_right(_hardwareGpioTestAlarmTimeInputs[index], 10, 0);
    char alarm_text[8] = {0};
    format_hardware_gpio_test_alarm_minutes(_hardwareGpioTestAlarmMinutesOfDay[index], alarm_text, sizeof(alarm_text));
    lv_textarea_set_text(_hardwareGpioTestAlarmTimeInputs[index], alarm_text);
    lv_obj_add_flag(_hardwareGpioTestAlarmTimeInputs[index], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_hardwareGpioTestAlarmTimeInputs[index], onHardwareGpioTestAlarmTimeEventCallback, LV_EVENT_ALL, this);

    _hardwareGpioTestAlarmLevelDropdowns[index] = lv_dropdown_create(row);
    lv_dropdown_set_options_static(_hardwareGpioTestAlarmLevelDropdowns[index], kHardwareGpioTestAlarmLevelOptions);
    lv_obj_set_width(_hardwareGpioTestAlarmLevelDropdowns[index], 96);
    lv_obj_add_flag(_hardwareGpioTestAlarmLevelDropdowns[index], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_hardwareGpioTestAlarmLevelDropdowns[index], onHardwareGpioTestAlarmLevelChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

    _hardwareGpioTestAlarmToggleButtons[index] = lv_btn_create(row);
    lv_obj_set_size(_hardwareGpioTestAlarmToggleButtons[index], 92, 40);
    lv_obj_set_style_radius(_hardwareGpioTestAlarmToggleButtons[index], 12, 0);
    lv_obj_set_style_border_width(_hardwareGpioTestAlarmToggleButtons[index], 0, 0);
    lv_obj_set_style_bg_color(_hardwareGpioTestAlarmToggleButtons[index], lv_color_hex(0xCBD5E1), 0);
    lv_obj_add_flag(_hardwareGpioTestAlarmToggleButtons[index], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_hardwareGpioTestAlarmToggleButtons[index], onHardwareGpioTestAlarmToggleClickedEventCallback, LV_EVENT_CLICKED, this);
    _hardwareGpioTestAlarmToggleLabels[index] = lv_label_create(_hardwareGpioTestAlarmToggleButtons[index]);
    lv_label_set_text(_hardwareGpioTestAlarmToggleLabels[index], "Off");
    lv_obj_set_style_text_color(_hardwareGpioTestAlarmToggleLabels[index], lv_color_hex(0x0F172A), 0);
    lv_obj_center(_hardwareGpioTestAlarmToggleLabels[index]);

    _hardwareGpioTestStateLabels[index] = lv_label_create(row);
    lv_obj_set_flex_grow(_hardwareGpioTestStateLabels[index], 1);
    lv_obj_set_width(_hardwareGpioTestStateLabels[index], 110);
    lv_label_set_long_mode(_hardwareGpioTestStateLabels[index], LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(_hardwareGpioTestStateLabels[index], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(_hardwareGpioTestStateLabels[index], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_hardwareGpioTestStateLabels[index], lv_color_hex(0x475569), 0);
#endif
}

void __attribute__((noinline)) AppSettings::appendHardwareGpioTestAudioRows(lv_obj_t *gpioPanel)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)gpioPanel;
    return;
#else
    lv_obj_t *timerAudioRow = create_settings_toggle_row(gpioPanel, "Timer Melody");
    _hardwareGpioTestTimerAudioDropdown = lv_dropdown_create(timerAudioRow);
    lv_obj_set_width(_hardwareGpioTestTimerAudioDropdown, 220);
    lv_obj_align(_hardwareGpioTestTimerAudioDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_dropdown_set_options_static(_hardwareGpioTestTimerAudioDropdown, "System sound");
    lv_obj_add_event_cb(_hardwareGpioTestTimerAudioDropdown,
                        onHardwareGpioTestAudioSelectionChangedEventCallback,
                        LV_EVENT_VALUE_CHANGED,
                        this);

    lv_obj_t *alarmAudioRow = create_settings_toggle_row(gpioPanel, "Alarm Melody");
    _hardwareGpioTestAlarmAudioDropdown = lv_dropdown_create(alarmAudioRow);
    lv_obj_set_width(_hardwareGpioTestAlarmAudioDropdown, 220);
    lv_obj_align(_hardwareGpioTestAlarmAudioDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_dropdown_set_options_static(_hardwareGpioTestAlarmAudioDropdown, "System sound");
    lv_obj_add_event_cb(_hardwareGpioTestAlarmAudioDropdown,
                        onHardwareGpioTestAudioSelectionChangedEventCallback,
                        LV_EVENT_VALUE_CHANGED,
                        this);

#endif
}

void AppSettings::refreshHardwareGpioTestUi(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    _hardwareGpioTestUiSyncInProgress = true;
    processHardwareGpioTestTimers();
    processHardwareGpioTestAlarms();
    const bool output_mode = _hardwareGpioTestMode == HardwareGpioTestMode::Output;
    const bool input_mode = _hardwareGpioTestMode == HardwareGpioTestMode::Input;
    const bool pwm_mode = _hardwareGpioTestMode == HardwareGpioTestMode::Pwm;
    const bool wave_mode = _hardwareGpioTestMode == HardwareGpioTestMode::Wave;
    const bool timer_mode = _hardwareGpioTestMode == HardwareGpioTestMode::Timer;
    const bool alarm_mode = _hardwareGpioTestMode == HardwareGpioTestMode::AlarmClock;
    bool all_low_active = true;
    bool all_high_active = true;

    if (lv_obj_ready(_hardwareGpioTestModeDropdown)) {
        lv_dropdown_set_selected(_hardwareGpioTestModeDropdown, static_cast<uint16_t>(_hardwareGpioTestMode));
    }

    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (input_mode) {
            gpio_config_t input_config = {};
            input_config.pin_bit_mask = (1ULL << kHardwareGpioTestPins[index]);
            input_config.mode = GPIO_MODE_INPUT;
            input_config.pull_up_en = GPIO_PULLUP_DISABLE;
            input_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
            input_config.intr_type = GPIO_INTR_DISABLE;
            if (gpio_config(&input_config) == ESP_OK) {
                _hardwareGpioTestLevels[index] = gpio_get_level(static_cast<gpio_num_t>(kHardwareGpioTestPins[index])) != 0 ? 1 : 0;
            }
        }

        if (lv_obj_ready(_hardwareGpioTestStateLabels[index])) {
            if (output_mode) {
                switch (_hardwareGpioTestLevels[index]) {
                case 0:
                    lv_label_set_text(_hardwareGpioTestStateLabels[index], "OUT LOW (0V)");
                    lv_obj_set_style_text_color(_hardwareGpioTestStateLabels[index], lv_color_hex(0x1D4ED8), 0);
                    break;
                case 1:
                    lv_label_set_text(_hardwareGpioTestStateLabels[index], "OUT HIGH (3.3V)");
                    lv_obj_set_style_text_color(_hardwareGpioTestStateLabels[index], lv_color_hex(0x15803D), 0);
                    break;
                default:
                    lv_label_set_text(_hardwareGpioTestStateLabels[index], "Released");
                    lv_obj_set_style_text_color(_hardwareGpioTestStateLabels[index], lv_color_hex(0x475569), 0);
                    break;
                }
            } else if (input_mode) {
                if (_hardwareGpioTestLevels[index] == 0) {
                    lv_label_set_text(_hardwareGpioTestStateLabels[index], "IN LOW (0V)");
                    lv_obj_set_style_text_color(_hardwareGpioTestStateLabels[index], lv_color_hex(0x1D4ED8), 0);
                } else {
                    lv_label_set_text(_hardwareGpioTestStateLabels[index], "IN HIGH (3.3V)");
                    lv_obj_set_style_text_color(_hardwareGpioTestStateLabels[index], lv_color_hex(0x15803D), 0);
                }
            } else if ((_hardwareGpioTestActiveWaveIndex == static_cast<int8_t>(index)) && (_hardwareGpioTestWaveDurationsTenths[index] > 0)) {
                char status_text[32] = {0};
                snprintf(status_text,
                         sizeof(status_text),
                         "%s %u.%us",
                         hardware_gpio_test_wave_name(static_cast<uint8_t>(_hardwareGpioTestWaveforms[index])),
                         static_cast<unsigned>(_hardwareGpioTestWaveDurationsTenths[index] / 10U),
                         static_cast<unsigned>(_hardwareGpioTestWaveDurationsTenths[index] % 10U));
                lv_label_set_text(_hardwareGpioTestStateLabels[index], status_text);
                lv_obj_set_style_text_color(_hardwareGpioTestStateLabels[index], lv_color_hex(0x7C3AED), 0);
            } else if (timer_mode) {
                char status_text[24] = {0};
                snprintf(status_text,
                         sizeof(status_text),
                         _hardwareGpioTestTimerAlertActive[index]
                             ? (_hardwareGpioTestTimerLevelsHigh[index] ? "Timer HIGH done" : "Timer LOW done")
                             : (_hardwareGpioTestTimerLevelsHigh[index] ? "Timer HIGH %us" : "Timer LOW %us"),
                         static_cast<unsigned>(_hardwareGpioTestTimerRunning[index] ? _hardwareGpioTestTimerRemainingSec[index]
                                                                                   : _hardwareGpioTestTimerDurationsSec[index]));
                lv_label_set_text(_hardwareGpioTestStateLabels[index], status_text);
                lv_obj_set_style_text_color(_hardwareGpioTestStateLabels[index],
                                            (_hardwareGpioTestTimerRunning[index] || _hardwareGpioTestTimerAlertActive[index])
                                                ? lv_color_hex(0xB45309)
                                                : lv_color_hex(0x475569),
                                            0);
            } else if (_hardwareGpioTestAlarmActive[index]) {
                lv_label_set_text(_hardwareGpioTestStateLabels[index], _hardwareGpioTestAlarmLevelsHigh[index] ? "Alarm HIGH" : "Alarm LOW");
                lv_obj_set_style_text_color(_hardwareGpioTestStateLabels[index], lv_color_hex(0x15803D), 0);
            } else if (_hardwareGpioTestAlarmArmed[index]) {
                lv_label_set_text(_hardwareGpioTestStateLabels[index], "Alarm armed");
                lv_obj_set_style_text_color(_hardwareGpioTestStateLabels[index], lv_color_hex(0x047857), 0);
            } else if (alarm_mode) {
                lv_label_set_text(_hardwareGpioTestStateLabels[index], "Alarm off");
                lv_obj_set_style_text_color(_hardwareGpioTestStateLabels[index], lv_color_hex(0x475569), 0);
            } else if ((_hardwareGpioTestActivePwmIndex == static_cast<int8_t>(index)) && (_hardwareGpioTestPwmValues[index] > 0)) {
                char status_text[24] = {0};
                snprintf(status_text, sizeof(status_text), "PWM %u%%", static_cast<unsigned>(_hardwareGpioTestPwmValues[index]));
                lv_label_set_text(_hardwareGpioTestStateLabels[index], status_text);
                lv_obj_set_style_text_color(_hardwareGpioTestStateLabels[index], lv_color_hex(0x15803D), 0);
            } else if (wave_mode) {
                lv_label_set_text(_hardwareGpioTestStateLabels[index], "Wave ready");
                lv_obj_set_style_text_color(_hardwareGpioTestStateLabels[index], lv_color_hex(0x475569), 0);
            } else {
                lv_label_set_text(_hardwareGpioTestStateLabels[index], "Released");
                lv_obj_set_style_text_color(_hardwareGpioTestStateLabels[index], lv_color_hex(0x475569), 0);
            }
        }

        if (lv_obj_ready(_hardwareGpioTestLowButtons[index])) {
            if (pwm_mode || wave_mode || timer_mode || alarm_mode) {
                lv_obj_add_flag(_hardwareGpioTestLowButtons[index], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(_hardwareGpioTestLowButtons[index], LV_OBJ_FLAG_HIDDEN);
            }
            if (output_mode) {
                lv_obj_clear_state(_hardwareGpioTestLowButtons[index], LV_STATE_DISABLED);
            } else {
                lv_obj_add_state(_hardwareGpioTestLowButtons[index], LV_STATE_DISABLED);
            }
            lv_obj_set_style_bg_color(_hardwareGpioTestLowButtons[index],
                                      (output_mode && (_hardwareGpioTestLevels[index] == 0)) ? lv_color_hex(0x93C5FD) : lv_color_hex(0xCBD5E1),
                                      0);
        }
        if (lv_obj_ready(_hardwareGpioTestHighButtons[index])) {
            if (pwm_mode || wave_mode || timer_mode || alarm_mode) {
                lv_obj_add_flag(_hardwareGpioTestHighButtons[index], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(_hardwareGpioTestHighButtons[index], LV_OBJ_FLAG_HIDDEN);
            }
            if (output_mode) {
                lv_obj_clear_state(_hardwareGpioTestHighButtons[index], LV_STATE_DISABLED);
            } else {
                lv_obj_add_state(_hardwareGpioTestHighButtons[index], LV_STATE_DISABLED);
            }
            lv_obj_set_style_bg_color(_hardwareGpioTestHighButtons[index],
                                      (output_mode && (_hardwareGpioTestLevels[index] == 1)) ? lv_color_hex(0x86EFAC) : lv_color_hex(0xCBD5E1),
                                      0);
        }

        if (lv_obj_ready(_hardwareGpioTestPwmSliders[index])) {
            if (pwm_mode) {
                lv_obj_clear_flag(_hardwareGpioTestPwmSliders[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_state(_hardwareGpioTestPwmSliders[index], LV_STATE_DISABLED);
            } else {
                lv_obj_add_flag(_hardwareGpioTestPwmSliders[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_state(_hardwareGpioTestPwmSliders[index], LV_STATE_DISABLED);
            }
            if (lv_slider_get_value(_hardwareGpioTestPwmSliders[index]) != _hardwareGpioTestPwmValues[index]) {
                lv_slider_set_value(_hardwareGpioTestPwmSliders[index], _hardwareGpioTestPwmValues[index], LV_ANIM_OFF);
            }
        }

        if (lv_obj_ready(_hardwareGpioTestWaveDropdowns[index])) {
            if (wave_mode) {
                lv_obj_clear_flag(_hardwareGpioTestWaveDropdowns[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_state(_hardwareGpioTestWaveDropdowns[index], LV_STATE_DISABLED);
            } else {
                lv_obj_add_flag(_hardwareGpioTestWaveDropdowns[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_state(_hardwareGpioTestWaveDropdowns[index], LV_STATE_DISABLED);
            }
            lv_dropdown_set_selected(_hardwareGpioTestWaveDropdowns[index], static_cast<uint16_t>(_hardwareGpioTestWaveforms[index]));
        }

        if (lv_obj_ready(_hardwareGpioTestWaveDurationInputs[index])) {
            if (wave_mode) {
                lv_obj_clear_flag(_hardwareGpioTestWaveDurationInputs[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_state(_hardwareGpioTestWaveDurationInputs[index], LV_STATE_DISABLED);
            } else {
                lv_obj_add_flag(_hardwareGpioTestWaveDurationInputs[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_state(_hardwareGpioTestWaveDurationInputs[index], LV_STATE_DISABLED);
            }

            if (!lv_obj_has_state(_hardwareGpioTestWaveDurationInputs[index], LV_STATE_FOCUSED)) {
                char duration_text[8] = {0};
                format_hardware_gpio_test_wave_tenths(_hardwareGpioTestWaveDurationsTenths[index], duration_text, sizeof(duration_text));
                if (strcmp(lv_textarea_get_text(_hardwareGpioTestWaveDurationInputs[index]), duration_text) != 0) {
                    lv_textarea_set_text(_hardwareGpioTestWaveDurationInputs[index], duration_text);
                }
            }
        }

        if (lv_obj_ready(_hardwareGpioTestTimerDurationInputs[index])) {
            if (timer_mode) {
                lv_obj_clear_flag(_hardwareGpioTestTimerDurationInputs[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_state(_hardwareGpioTestTimerDurationInputs[index], LV_STATE_DISABLED);
            } else {
                lv_obj_add_flag(_hardwareGpioTestTimerDurationInputs[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_state(_hardwareGpioTestTimerDurationInputs[index], LV_STATE_DISABLED);
            }

            if (_hardwareGpioTestTimerAlertActive[index] &&
                (_hardwareGpioTestTimerAlertAudioIndex == static_cast<int8_t>(index))) {
                lv_textarea_set_accepted_chars(_hardwareGpioTestTimerDurationInputs[index], nullptr);
                lv_textarea_set_max_length(_hardwareGpioTestTimerDurationInputs[index], 4);
            } else {
                lv_textarea_set_accepted_chars(_hardwareGpioTestTimerDurationInputs[index], "0123456789");
                lv_textarea_set_max_length(_hardwareGpioTestTimerDurationInputs[index], 4);
            }

            if (!lv_obj_has_state(_hardwareGpioTestTimerDurationInputs[index], LV_STATE_FOCUSED)) {
                char timer_text[8] = {0};
                if (_hardwareGpioTestTimerAlertActive[index] &&
                    (_hardwareGpioTestTimerAlertAudioIndex == static_cast<int8_t>(index)) &&
                    _hardwareGpioTestTimerAlertBlinkVisible) {
                    snprintf(timer_text, sizeof(timer_text), "PLAY");
                } else if (_hardwareGpioTestTimerAlertActive[index] &&
                           (_hardwareGpioTestTimerAlertAudioIndex == static_cast<int8_t>(index))) {
                    timer_text[0] = '\0';
                } else {
                    format_hardware_gpio_test_timer_seconds(_hardwareGpioTestTimerRunning[index]
                                                                ? _hardwareGpioTestTimerRemainingSec[index]
                                                                : _hardwareGpioTestTimerDurationsSec[index],
                                                            timer_text,
                                                            sizeof(timer_text));
                }
                if (strcmp(lv_textarea_get_text(_hardwareGpioTestTimerDurationInputs[index]), timer_text) != 0) {
                    lv_textarea_set_text(_hardwareGpioTestTimerDurationInputs[index], timer_text);
                }
            }
        }

        if (lv_obj_ready(_hardwareGpioTestTimerLevelDropdowns[index])) {
            if (timer_mode) {
                lv_obj_clear_flag(_hardwareGpioTestTimerLevelDropdowns[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_state(_hardwareGpioTestTimerLevelDropdowns[index], LV_STATE_DISABLED);
            } else {
                lv_obj_add_flag(_hardwareGpioTestTimerLevelDropdowns[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_state(_hardwareGpioTestTimerLevelDropdowns[index], LV_STATE_DISABLED);
            }
            lv_dropdown_set_selected(_hardwareGpioTestTimerLevelDropdowns[index], _hardwareGpioTestTimerLevelsHigh[index] ? 0 : 1);
        }

        if (lv_obj_ready(_hardwareGpioTestTimerButtons[index])) {
            if (timer_mode) {
                lv_obj_clear_flag(_hardwareGpioTestTimerButtons[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_state(_hardwareGpioTestTimerButtons[index], LV_STATE_DISABLED);
            } else {
                lv_obj_add_flag(_hardwareGpioTestTimerButtons[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_state(_hardwareGpioTestTimerButtons[index], LV_STATE_DISABLED);
            }
            lv_obj_set_style_bg_color(_hardwareGpioTestTimerButtons[index],
                                      (_hardwareGpioTestTimerRunning[index] || _hardwareGpioTestTimerAlertActive[index])
                                          ? lv_color_hex(0xF59E0B)
                                          : lv_color_hex(0xCBD5E1),
                                      0);
            if (lv_obj_ready(_hardwareGpioTestTimerButtonLabels[index])) {
                lv_label_set_text(_hardwareGpioTestTimerButtonLabels[index],
                                  (_hardwareGpioTestTimerRunning[index] || _hardwareGpioTestTimerAlertActive[index]) ? "Stop" : "Start");
                lv_obj_center(_hardwareGpioTestTimerButtonLabels[index]);
            }
        }

        if (lv_obj_ready(_hardwareGpioTestAlarmTimeInputs[index])) {
            if (alarm_mode) {
                lv_obj_clear_flag(_hardwareGpioTestAlarmTimeInputs[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_state(_hardwareGpioTestAlarmTimeInputs[index], LV_STATE_DISABLED);
            } else {
                lv_obj_add_flag(_hardwareGpioTestAlarmTimeInputs[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_state(_hardwareGpioTestAlarmTimeInputs[index], LV_STATE_DISABLED);
            }
            if (!lv_obj_has_state(_hardwareGpioTestAlarmTimeInputs[index], LV_STATE_FOCUSED)) {
                char alarm_text[8] = {0};
                format_hardware_gpio_test_alarm_minutes(_hardwareGpioTestAlarmMinutesOfDay[index], alarm_text, sizeof(alarm_text));
                if (strcmp(lv_textarea_get_text(_hardwareGpioTestAlarmTimeInputs[index]), alarm_text) != 0) {
                    lv_textarea_set_text(_hardwareGpioTestAlarmTimeInputs[index], alarm_text);
                }
            }
        }

        if (lv_obj_ready(_hardwareGpioTestAlarmLevelDropdowns[index])) {
            if (alarm_mode) {
                lv_obj_clear_flag(_hardwareGpioTestAlarmLevelDropdowns[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_state(_hardwareGpioTestAlarmLevelDropdowns[index], LV_STATE_DISABLED);
            } else {
                lv_obj_add_flag(_hardwareGpioTestAlarmLevelDropdowns[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_state(_hardwareGpioTestAlarmLevelDropdowns[index], LV_STATE_DISABLED);
            }
            lv_dropdown_set_selected(_hardwareGpioTestAlarmLevelDropdowns[index], _hardwareGpioTestAlarmLevelsHigh[index] ? 0 : 1);
        }

        if (lv_obj_ready(_hardwareGpioTestAlarmToggleButtons[index])) {
            if (alarm_mode) {
                lv_obj_clear_flag(_hardwareGpioTestAlarmToggleButtons[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_state(_hardwareGpioTestAlarmToggleButtons[index], LV_STATE_DISABLED);
            } else {
                lv_obj_add_flag(_hardwareGpioTestAlarmToggleButtons[index], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_state(_hardwareGpioTestAlarmToggleButtons[index], LV_STATE_DISABLED);
            }
            lv_obj_set_style_bg_color(_hardwareGpioTestAlarmToggleButtons[index],
                                      _hardwareGpioTestAlarmArmed[index] ? lv_color_hex(0x22C55E) : lv_color_hex(0xCBD5E1),
                                      0);
            if (lv_obj_ready(_hardwareGpioTestAlarmToggleLabels[index])) {
                lv_label_set_text(_hardwareGpioTestAlarmToggleLabels[index], _hardwareGpioTestAlarmArmed[index] ? "Set" : "Off");
                lv_obj_center(_hardwareGpioTestAlarmToggleLabels[index]);
            }
        }

        if (lv_obj_ready(_hardwareGpioTestStateLabels[index])) {
            if (timer_mode || alarm_mode) {
                lv_obj_add_flag(_hardwareGpioTestStateLabels[index], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(_hardwareGpioTestStateLabels[index], LV_OBJ_FLAG_HIDDEN);
            }
        }

        all_low_active = all_low_active && output_mode && (_hardwareGpioTestLevels[index] == 0);
        all_high_active = all_high_active && output_mode && (_hardwareGpioTestLevels[index] == 1);
    }

    if (lv_obj_ready(_hardwareGpioTestAllLowButton)) {
        if (pwm_mode || wave_mode || timer_mode || alarm_mode) {
            lv_obj_add_flag(_hardwareGpioTestAllLowButton, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(_hardwareGpioTestAllLowButton, LV_OBJ_FLAG_HIDDEN);
        }
        if (output_mode) {
            lv_obj_clear_state(_hardwareGpioTestAllLowButton, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(_hardwareGpioTestAllLowButton, LV_STATE_DISABLED);
        }
        lv_obj_set_style_bg_color(_hardwareGpioTestAllLowButton,
                                  (output_mode && all_low_active) ? lv_color_hex(0x93C5FD) : lv_color_hex(0xCBD5E1),
                                  0);
    }

    if (lv_obj_ready(_hardwareGpioTestAllHighButton)) {
        if (pwm_mode || wave_mode || timer_mode || alarm_mode) {
            lv_obj_add_flag(_hardwareGpioTestAllHighButton, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(_hardwareGpioTestAllHighButton, LV_OBJ_FLAG_HIDDEN);
        }
        if (output_mode) {
            lv_obj_clear_state(_hardwareGpioTestAllHighButton, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(_hardwareGpioTestAllHighButton, LV_STATE_DISABLED);
        }
        lv_obj_set_style_bg_color(_hardwareGpioTestAllHighButton,
                                  (output_mode && all_high_active) ? lv_color_hex(0x86EFAC) : lv_color_hex(0xCBD5E1),
                                  0);
    }
    _hardwareGpioTestUiSyncInProgress = false;
#endif
}

void AppSettings::setHardwareGpioTestMode(HardwareGpioTestMode mode)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)mode;
    return;
#else
    stopHardwareGpioTestTimers();
    stopHardwareGpioTestWave();
    stopHardwareGpioTestPwm();

    const HardwareGpioTestMode previous_mode = _hardwareGpioTestMode;
    _hardwareGpioTestMode = mode;

    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        gpio_reset_pin(static_cast<gpio_num_t>(kHardwareGpioTestPins[index]));
        _hardwareGpioTestLevels[index] = -1;
        _hardwareGpioTestPwmValues[index] = 0;
        if (previous_mode != HardwareGpioTestMode::Timer) {
            _hardwareGpioTestTimerRemainingSec[index] = _hardwareGpioTestTimerDurationsSec[index];
        }
    }

    applyHardwareGpioTestActiveAlarms();

    refreshHardwareGpioTestUi();
#endif
}

bool AppSettings::applyHardwareGpioTestDrivenLevel(size_t index, bool high)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)index;
    (void)high;
    return false;
#else
    if (index >= AppSettings::kHardwareGpioTestPinCount) {
        return false;
    }

    gpio_config_t config = {};
    config.pin_bit_mask = (1ULL << kHardwareGpioTestPins[index]);
    config.intr_type = GPIO_INTR_DISABLE;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    if (gpio_config(&config) != ESP_OK) {
        return false;
    }
    if (gpio_set_level(static_cast<gpio_num_t>(kHardwareGpioTestPins[index]), high ? 1 : 0) != ESP_OK) {
        return false;
    }

    _hardwareGpioTestLevels[index] = high ? 1 : 0;
    return true;
#endif
}

void AppSettings::applyHardwareGpioTestRunningTimers(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (_hardwareGpioTestTimerRunning[index]) {
            (void)applyHardwareGpioTestDrivenLevel(index, _hardwareGpioTestTimerLevelsHigh[index]);
        }
    }
#endif
}

void AppSettings::setHardwareGpioTestLevel(size_t index, bool high)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)index;
    (void)high;
    return;
#else
    if (index >= AppSettings::kHardwareGpioTestPinCount) {
        return;
    }
    if (_hardwareGpioTestMode != HardwareGpioTestMode::Output) {
        refreshHardwareGpioTestUi();
        return;
    }

    const int8_t requested_level = high ? 1 : 0;
    if (_hardwareGpioTestLevels[index] == requested_level) {
        gpio_reset_pin(static_cast<gpio_num_t>(kHardwareGpioTestPins[index]));
        _hardwareGpioTestLevels[index] = -1;
        refreshHardwareGpioTestUi();
        return;
    }

    (void)applyHardwareGpioTestDrivenLevel(index, high);

    refreshHardwareGpioTestUi();
#endif
}

void AppSettings::setHardwareGpioTestAllLevels(bool high)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)high;
    return;
#else
    if (_hardwareGpioTestMode != HardwareGpioTestMode::Output) {
        refreshHardwareGpioTestUi();
        return;
    }

    const int8_t requested_level = high ? 1 : 0;
    bool all_already_requested = true;
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        all_already_requested = all_already_requested && (_hardwareGpioTestLevels[index] == requested_level);
    }

    if (all_already_requested) {
        releaseHardwareGpioTestPins();
        return;
    }

    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (applyHardwareGpioTestDrivenLevel(index, high)) {
            _hardwareGpioTestLevels[index] = requested_level;
        }
    }

    refreshHardwareGpioTestUi();
#endif
}

void AppSettings::setHardwareGpioTestPwm(size_t index, uint8_t duty_percent)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)index;
    (void)duty_percent;
    return;
#else
    if ((index >= AppSettings::kHardwareGpioTestPinCount) || (_hardwareGpioTestMode != HardwareGpioTestMode::Pwm)) {
        refreshHardwareGpioTestUi();
        return;
    }

    stopHardwareGpioTestTimers();
    stopHardwareGpioTestWave();

    duty_percent = std::min<uint8_t>(duty_percent, 100);

    if ((_hardwareGpioTestActivePwmIndex >= 0) && (_hardwareGpioTestActivePwmIndex != static_cast<int8_t>(index))) {
        const size_t previous_index = static_cast<size_t>(_hardwareGpioTestActivePwmIndex);
        stopHardwareGpioTestPwm();
        _hardwareGpioTestPwmValues[previous_index] = 0;
    }

    _hardwareGpioTestPwmValues[index] = duty_percent;
    _hardwareGpioTestLevels[index] = -1;

    if (duty_percent == 0) {
        if (_hardwareGpioTestActivePwmIndex == static_cast<int8_t>(index)) {
            stopHardwareGpioTestPwm();
        }
        refreshHardwareGpioTestUi();
        return;
    }

    ledc_timer_config_t timer_config = {
        .speed_mode = kHardwareGpioTestPwmSpeedMode,
        .duty_resolution = kHardwareGpioTestPwmResolution,
        .timer_num = kHardwareGpioTestPwmTimer,
        .freq_hz = kHardwareGpioTestPwmFrequencyHz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&timer_config) != ESP_OK) {
        refreshHardwareGpioTestUi();
        return;
    }

    ledc_channel_config_t channel_config = {
        .gpio_num = kHardwareGpioTestPins[index],
        .speed_mode = kHardwareGpioTestPwmSpeedMode,
        .channel = kHardwareGpioTestPwmChannel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = kHardwareGpioTestPwmTimer,
        .duty = (kHardwareGpioTestPwmMaxDuty * duty_percent) / 100U,
        .hpoint = 0,
    };
    if (ledc_channel_config(&channel_config) == ESP_OK) {
        _hardwareGpioTestActivePwmIndex = static_cast<int8_t>(index);
    }

    refreshHardwareGpioTestUi();
#endif
}

void AppSettings::stopHardwareGpioTestPwm(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    if (_hardwareGpioTestActivePwmIndex < 0) {
        return;
    }

    const gpio_num_t gpio = static_cast<gpio_num_t>(kHardwareGpioTestPins[_hardwareGpioTestActivePwmIndex]);
    _hardwareGpioTestPwmValues[static_cast<size_t>(_hardwareGpioTestActivePwmIndex)] = 0;
    ledc_stop(kHardwareGpioTestPwmSpeedMode, kHardwareGpioTestPwmChannel, 0);
    gpio_reset_pin(gpio);
    _hardwareGpioTestActivePwmIndex = -1;
#endif
}

void AppSettings::setHardwareGpioTestWave(size_t index)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)index;
    return;
#else
    if ((index >= AppSettings::kHardwareGpioTestPinCount) || (_hardwareGpioTestMode != HardwareGpioTestMode::Wave)) {
        refreshHardwareGpioTestUi();
        return;
    }

    stopHardwareGpioTestTimers();
    stopHardwareGpioTestPwm();
    if ((_hardwareGpioTestActiveWaveIndex >= 0) && (_hardwareGpioTestActiveWaveIndex != static_cast<int8_t>(index))) {
        stopHardwareGpioTestWave();
    }

    ledc_timer_config_t timer_config = {
        .speed_mode = kHardwareGpioTestPwmSpeedMode,
        .duty_resolution = kHardwareGpioTestPwmResolution,
        .timer_num = kHardwareGpioTestPwmTimer,
        .freq_hz = kHardwareGpioTestPwmFrequencyHz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&timer_config) != ESP_OK) {
        refreshHardwareGpioTestUi();
        return;
    }

    ledc_channel_config_t channel_config = {
        .gpio_num = kHardwareGpioTestPins[index],
        .speed_mode = kHardwareGpioTestPwmSpeedMode,
        .channel = kHardwareGpioTestPwmChannel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = kHardwareGpioTestPwmTimer,
        .duty = 0,
        .hpoint = 0,
    };
    if (ledc_channel_config(&channel_config) != ESP_OK) {
        refreshHardwareGpioTestUi();
        return;
    }

    _hardwareGpioTestActiveWaveIndex = static_cast<int8_t>(index);
    _hardwareGpioTestWaveStartUs = esp_timer_get_time();

    if (_hardwareGpioTestWaveTimer == nullptr) {
        _hardwareGpioTestWaveTimer = lv_timer_create(onHardwareGpioTestWaveTimerCallback, kHardwareGpioTestWaveTickMs, this);
    }
    if (_hardwareGpioTestWaveTimer == nullptr) {
        refreshHardwareGpioTestUi();
        return;
    }

    onHardwareGpioTestWaveTimerCallback(_hardwareGpioTestWaveTimer);
    refreshHardwareGpioTestUi();
#endif
}

void AppSettings::stopHardwareGpioTestWave(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    if (_hardwareGpioTestWaveTimer != nullptr) {
        lv_timer_del(_hardwareGpioTestWaveTimer);
        _hardwareGpioTestWaveTimer = nullptr;
    }
    if (_hardwareGpioTestActiveWaveIndex >= 0) {
        const gpio_num_t gpio = static_cast<gpio_num_t>(kHardwareGpioTestPins[_hardwareGpioTestActiveWaveIndex]);
        _hardwareGpioTestPwmValues[static_cast<size_t>(_hardwareGpioTestActiveWaveIndex)] = 0;
        ledc_stop(kHardwareGpioTestPwmSpeedMode, kHardwareGpioTestPwmChannel, 0);
        gpio_reset_pin(gpio);
        _hardwareGpioTestActiveWaveIndex = -1;
    }
    _hardwareGpioTestWaveStartUs = 0;
#endif
}

void AppSettings::toggleHardwareGpioTestTimer(size_t index)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)index;
    return;
#else
    if ((index >= AppSettings::kHardwareGpioTestPinCount) || (_hardwareGpioTestMode != HardwareGpioTestMode::Timer)) {
        refreshHardwareGpioTestUi();
        return;
    }

    if (_hardwareGpioTestTimerRunning[index] || _hardwareGpioTestTimerAlertActive[index]) {
        stopHardwareGpioTestTimer(index, _hardwareGpioTestTimerRunning[index]);
        refreshHardwareGpioTestUi();
        return;
    }

    stopHardwareGpioTestPwm();
    stopHardwareGpioTestWave();

    uint16_t duration_seconds = _hardwareGpioTestTimerDurationsSec[index];
    if (lv_obj_ready(_hardwareGpioTestTimerDurationInputs[index])) {
        uint16_t parsed_seconds = 0;
        if (parse_hardware_gpio_test_timer_seconds(lv_textarea_get_text(_hardwareGpioTestTimerDurationInputs[index]), &parsed_seconds)) {
            duration_seconds = parsed_seconds;
        }
    }
    duration_seconds = clamp_hardware_gpio_test_timer_seconds(duration_seconds);
    if (duration_seconds == 0) {
        _hardwareGpioTestTimerDurationsSec[index] = 0;
        _hardwareGpioTestTimerRemainingSec[index] = 0;
        refreshHardwareGpioTestUi();
        return;
    }
    _hardwareGpioTestTimerDurationsSec[index] = duration_seconds;
    _hardwareGpioTestTimerRemainingSec[index] = duration_seconds;

    if (!applyHardwareGpioTestDrivenLevel(index, _hardwareGpioTestTimerLevelsHigh[index])) {
        refreshHardwareGpioTestUi();
        return;
    }

    if (_hardwareGpioTestTimerTick == nullptr) {
        _hardwareGpioTestTimerTick = lv_timer_create(onHardwareGpioTestTimerTickCallback, 250, this);
    }

    const int64_t now_epoch = hardware_gpio_test_current_epoch_seconds();
    _hardwareGpioTestTimerEndUs[index] = esp_timer_get_time() + (static_cast<int64_t>(duration_seconds) * 1000000LL);
    _hardwareGpioTestTimerEndEpochSec[index] = (now_epoch > 0) ? (now_epoch + duration_seconds) : 0;
    _hardwareGpioTestTimerRunning[index] = true;
    _hardwareGpioTestTimerAlertActive[index] = false;
    if (_hardwareGpioTestTimerAlertAudioIndex == static_cast<int8_t>(index)) {
        _hardwareGpioTestTimerAlertAudioIndex = -1;
    }

    persistHardwareGpioTestSchedules();

    refreshHardwareGpioTestUi();
#endif
}

void AppSettings::stopHardwareGpioTestTimer(size_t index, bool preserve_remaining)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)index;
    (void)preserve_remaining;
    return;
#else
    if ((index >= AppSettings::kHardwareGpioTestPinCount) ||
        (!_hardwareGpioTestTimerRunning[index] && !_hardwareGpioTestTimerAlertActive[index])) {
        return;
    }

    if (_hardwareGpioTestTimerRunning[index] && preserve_remaining) {
        int64_t remaining_us = std::max<int64_t>(0, _hardwareGpioTestTimerEndUs[index] - esp_timer_get_time());
        const int64_t now_epoch = hardware_gpio_test_current_epoch_seconds();
        if ((now_epoch > 0) && (_hardwareGpioTestTimerEndEpochSec[index] > 0)) {
            remaining_us = std::max<int64_t>(0, (_hardwareGpioTestTimerEndEpochSec[index] - now_epoch) * 1000000LL);
        }
        _hardwareGpioTestTimerRemainingSec[index] = static_cast<uint16_t>((remaining_us + 999999LL) / 1000000LL);
        _hardwareGpioTestTimerDurationsSec[index] = std::max<uint16_t>(kHardwareGpioTestTimerMinSeconds, _hardwareGpioTestTimerRemainingSec[index]);
    } else {
        _hardwareGpioTestTimerRemainingSec[index] = _hardwareGpioTestTimerDurationsSec[index];
    }

    gpio_reset_pin(static_cast<gpio_num_t>(kHardwareGpioTestPins[index]));
    _hardwareGpioTestLevels[index] = -1;
    _hardwareGpioTestTimerRunning[index] = false;
    const bool stop_audio = _hardwareGpioTestTimerAlertActive[index] &&
                            (_hardwareGpioTestTimerAlertAudioIndex == static_cast<int8_t>(index));
    _hardwareGpioTestTimerAlertActive[index] = false;
    _hardwareGpioTestTimerEndUs[index] = 0;
    _hardwareGpioTestTimerEndEpochSec[index] = 0;
    if (_hardwareGpioTestTimerAlertAudioIndex == static_cast<int8_t>(index)) {
        _hardwareGpioTestTimerAlertAudioIndex = -1;
    }

    if (stop_audio) {
        (void)bsp_extra_player_stop();
    }

    if (_hardwareGpioTestAlarmActive[index]) {
        (void)applyHardwareGpioTestDrivenLevel(index, _hardwareGpioTestAlarmLevelsHigh[index]);
    }

    persistHardwareGpioTestSchedules();
#endif
}

void AppSettings::stopHardwareGpioTestTimers(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (_hardwareGpioTestTimerRunning[index] || _hardwareGpioTestTimerAlertActive[index]) {
            stopHardwareGpioTestTimer(index, false);
        }
        _hardwareGpioTestTimerRemainingSec[index] = _hardwareGpioTestTimerDurationsSec[index];
    }
#endif
}

void AppSettings::processHardwareGpioTestTimers(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    const int64_t now_epoch = hardware_gpio_test_current_epoch_seconds();
    const int64_t now_us = esp_timer_get_time();
    bool state_changed = false;

    if ((_hardwareGpioTestTimerAlertAudioIndex >= 0) &&
        !_hardwareGpioTestTimerAlertActive[static_cast<size_t>(_hardwareGpioTestTimerAlertAudioIndex)]) {
        _hardwareGpioTestTimerAlertAudioIndex = -1;
        state_changed = true;
    }

    if ((_hardwareGpioTestTimerAlertAudioIndex >= 0) && !bsp_extra_player_is_active()) {
        playHardwareGpioTestAlertSound(true);
    }

    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (!_hardwareGpioTestTimerRunning[index]) {
            continue;
        }

        if ((now_epoch > 0) && (_hardwareGpioTestTimerEndEpochSec[index] == 0) && (_hardwareGpioTestTimerRemainingSec[index] > 0)) {
            _hardwareGpioTestTimerEndEpochSec[index] = now_epoch + _hardwareGpioTestTimerRemainingSec[index];
            state_changed = true;
        }

        int64_t remaining_us = std::max<int64_t>(0, _hardwareGpioTestTimerEndUs[index] - now_us);
        if ((now_epoch > 0) && (_hardwareGpioTestTimerEndEpochSec[index] > 0)) {
            remaining_us = std::max<int64_t>(0, (_hardwareGpioTestTimerEndEpochSec[index] - now_epoch) * 1000000LL);
        }

        const uint16_t remaining_sec = static_cast<uint16_t>((remaining_us + 999999LL) / 1000000LL);
        if (_hardwareGpioTestTimerRemainingSec[index] != remaining_sec) {
            _hardwareGpioTestTimerRemainingSec[index] = remaining_sec;
        }

        if (remaining_us > 0) {
            continue;
        }

        _hardwareGpioTestTimerRunning[index] = false;
        _hardwareGpioTestTimerAlertActive[index] = true;
        _hardwareGpioTestTimerRemainingSec[index] = _hardwareGpioTestTimerDurationsSec[index];
        _hardwareGpioTestTimerEndUs[index] = 0;
        _hardwareGpioTestTimerEndEpochSec[index] = 0;
        if (_hardwareGpioTestTimerAlertAudioIndex < 0) {
            _hardwareGpioTestTimerAlertAudioIndex = static_cast<int8_t>(index);
            playHardwareGpioTestAlertSound(true);
        }
        state_changed = true;
    }

    if (state_changed) {
        persistHardwareGpioTestSchedules();
    }
#endif
}

void AppSettings::processHardwareGpioTestAlarms(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    const time_t current_epoch = time(nullptr);
    if (current_epoch < kValidUnixTimeFloor) {
        return;
    }

    tm local_time = {};
    if (localtime_r(&current_epoch, &local_time) == nullptr) {
        return;
    }

    const int32_t today_key = hardware_gpio_test_alarm_date_key(local_time);
    const uint16_t current_minutes = static_cast<uint16_t>((local_time.tm_hour * 60) + local_time.tm_min);
    bool state_changed = false;

    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (!_hardwareGpioTestAlarmArmed[index]) {
            continue;
        }

        if (_hardwareGpioTestAlarmActive[index]) {
            continue;
        }

        if ((_hardwareGpioTestAlarmLastFireDate[index] != today_key) && (_hardwareGpioTestAlarmMinutesOfDay[index] == current_minutes)) {
            if (applyHardwareGpioTestDrivenLevel(index, _hardwareGpioTestAlarmLevelsHigh[index])) {
                _hardwareGpioTestAlarmActive[index] = true;
                _hardwareGpioTestAlarmLastFireDate[index] = today_key;
                playHardwareGpioTestAlertSound(false);
                state_changed = true;
            }
        }
    }

    if (state_changed) {
        persistHardwareGpioTestSchedules();
    }
#endif
}

void AppSettings::applyHardwareGpioTestScheduledOutputs(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    applyHardwareGpioTestRunningTimers();
    applyHardwareGpioTestActiveAlarms();
#endif
}

void AppSettings::scanHardwareGpioTestAudioFiles(bool allow_mount)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)allow_mount;
    return;
#else
    _hardwareGpioTestAudioFilePaths.clear();

    const bool sd_available = allow_mount ? app_storage_ensure_sdcard_available() : app_storage_is_sdcard_mounted();
    if (!sd_available) {
        return;
    }

    struct stat directory_stat = {};
    if ((stat("/sdcard/wav", &directory_stat) != 0) || !S_ISDIR(directory_stat.st_mode)) {
        return;
    }

    DIR *directory = opendir("/sdcard/wav");
    if (directory == nullptr) {
        return;
    }

    auto is_supported_audio_extension = [](const char *name) -> bool {
        if (name == nullptr) {
            return false;
        }
        const char *dot = strrchr(name, '.');
        if ((dot == nullptr) || (dot[1] == '\0')) {
            return false;
        }

        std::string extension(dot + 1);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return (extension == "aac") || (extension == "amr") || (extension == "awb") ||
               (extension == "flac") || (extension == "m4a") || (extension == "mp3") ||
               (extension == "mp4") || (extension == "mpga") || (extension == "wav") ||
               (extension == "wave");
    };

    dirent *entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0) || !is_supported_audio_extension(entry->d_name)) {
            continue;
        }

        std::string full_path = std::string("/sdcard/wav/") + entry->d_name;
        struct stat file_stat = {};
        if ((stat(full_path.c_str(), &file_stat) != 0) || !S_ISREG(file_stat.st_mode)) {
            continue;
        }

        _hardwareGpioTestAudioFilePaths.push_back(std::move(full_path));
    }
    closedir(directory);

    std::sort(_hardwareGpioTestAudioFilePaths.begin(), _hardwareGpioTestAudioFilePaths.end());
#endif
}

void AppSettings::refreshHardwareGpioTestAudioDropdowns(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    auto build_options = [this]() {
        std::string options = "System sound";
        for (const std::string &path : _hardwareGpioTestAudioFilePaths) {
            const size_t slash = path.find_last_of('/');
            options.push_back('\n');
            options += (slash == std::string::npos) ? path : path.substr(slash + 1);
        }
        return options;
    };

    auto selected_index_for_path = [this](const std::string &selected_path) -> uint16_t {
        if (selected_path.empty()) {
            return 0;
        }
        for (size_t index = 0; index < _hardwareGpioTestAudioFilePaths.size(); ++index) {
            if (_hardwareGpioTestAudioFilePaths[index] == selected_path) {
                return static_cast<uint16_t>(index + 1);
            }
        }
        return 0;
    };

    const std::string options = build_options();
    if (lv_obj_ready(_hardwareGpioTestTimerAudioDropdown)) {
        lv_dropdown_set_options(_hardwareGpioTestTimerAudioDropdown, options.c_str());
        lv_dropdown_set_selected(_hardwareGpioTestTimerAudioDropdown,
                                 selected_index_for_path(_hardwareGpioTestTimerAudioPath));
    }
    if (lv_obj_ready(_hardwareGpioTestAlarmAudioDropdown)) {
        lv_dropdown_set_options(_hardwareGpioTestAlarmAudioDropdown, options.c_str());
        lv_dropdown_set_selected(_hardwareGpioTestAlarmAudioDropdown,
                                 selected_index_for_path(_hardwareGpioTestAlarmAudioPath));
    }

    if (_hardwareGpioTestAudioFilePaths.empty()) {
        return;
    }

    if ((!_hardwareGpioTestTimerAudioPath.empty()) &&
        (selected_index_for_path(_hardwareGpioTestTimerAudioPath) == 0)) {
        _hardwareGpioTestTimerAudioPath.clear();
    }
    if ((!_hardwareGpioTestAlarmAudioPath.empty()) &&
        (selected_index_for_path(_hardwareGpioTestAlarmAudioPath) == 0)) {
        _hardwareGpioTestAlarmAudioPath.clear();
    }
#endif
}

void AppSettings::loadHardwareGpioTestAudioSelections(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    char path[192] = {};
    if (loadNvsStringParam(NVS_KEY_GPIO_TIMER_SOUND, path, sizeof(path)) && (path[0] != '\0')) {
        _hardwareGpioTestTimerAudioPath = path;
    } else {
        _hardwareGpioTestTimerAudioPath.clear();
    }

    std::memset(path, 0, sizeof(path));
    if (loadNvsStringParam(NVS_KEY_GPIO_ALARM_SOUND, path, sizeof(path)) && (path[0] != '\0')) {
        _hardwareGpioTestAlarmAudioPath = path;
    } else {
        _hardwareGpioTestAlarmAudioPath.clear();
    }
#endif
}

void AppSettings::persistHardwareGpioTestAudioSelections(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    (void)setNvsStringParam(NVS_KEY_GPIO_TIMER_SOUND, _hardwareGpioTestTimerAudioPath.c_str());
    (void)setNvsStringParam(NVS_KEY_GPIO_ALARM_SOUND, _hardwareGpioTestAlarmAudioPath.c_str());
#endif
}

void AppSettings::playHardwareGpioTestAlertSound(bool timer_sound)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)timer_sound;
    return;
#else
    const std::string &selected_path = timer_sound ? _hardwareGpioTestTimerAudioPath : _hardwareGpioTestAlarmAudioPath;
    if (!selected_path.empty()) {
        struct stat file_stat = {};
        if ((stat(selected_path.c_str(), &file_stat) == 0) && S_ISREG(file_stat.st_mode) &&
            (bsp_extra_player_init() == ESP_OK) && (bsp_extra_player_play_file(selected_path.c_str()) == ESP_OK)) {
            return;
        }
    }

    (void)bsp_extra_audio_play_system_sound(timer_sound ? BSP_EXTRA_AUDIO_SYSTEM_SOUND_TIMER_COMPLETE
                                                        : BSP_EXTRA_AUDIO_SYSTEM_SOUND_ALARM_CLOCK);
#endif
}

void AppSettings::applyHardwareGpioTestActiveAlarms(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (!_hardwareGpioTestAlarmActive[index]) {
            continue;
        }

        (void)applyHardwareGpioTestDrivenLevel(index, _hardwareGpioTestAlarmLevelsHigh[index]);
    }
#endif
}

void AppSettings::loadHardwareGpioTestSchedules(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    char timer_state[kHardwareGpioTestPersistBufferSize] = {};
    if (loadNvsStringParam(NVS_KEY_GPIO_TIMER_STATE, timer_state, sizeof(timer_state))) {
        char *context = nullptr;
        size_t index = 0;
        for (char *entry = strtok_r(timer_state, ";", &context);
             (entry != nullptr) && (index < AppSettings::kHardwareGpioTestPinCount);
             entry = strtok_r(nullptr, ";", &context), ++index) {
            unsigned duration = 0;
            unsigned remaining = 0;
            int level_high = 1;
            int running = 0;
            long long end_epoch = 0;
            if (sscanf(entry, "%u,%u,%d,%d,%lld", &duration, &remaining, &level_high, &running, &end_epoch) == 5) {
                _hardwareGpioTestTimerDurationsSec[index] = clamp_hardware_gpio_test_timer_seconds(duration);
                _hardwareGpioTestTimerRemainingSec[index] = clamp_hardware_gpio_test_timer_seconds(remaining);
                _hardwareGpioTestTimerLevelsHigh[index] = level_high != 0;
                _hardwareGpioTestTimerRunning[index] = running != 0;
                _hardwareGpioTestTimerEndEpochSec[index] = static_cast<int64_t>(end_epoch);
                _hardwareGpioTestTimerEndUs[index] = 0;
            }
        }
    }

    char alarm_state[kHardwareGpioTestPersistBufferSize] = {};
    if (loadNvsStringParam(NVS_KEY_GPIO_ALARM_STATE, alarm_state, sizeof(alarm_state))) {
        char *context = nullptr;
        size_t index = 0;
        for (char *entry = strtok_r(alarm_state, ";", &context);
             (entry != nullptr) && (index < AppSettings::kHardwareGpioTestPinCount);
             entry = strtok_r(nullptr, ";", &context), ++index) {
            unsigned minutes_of_day = 0;
            int level_high = 1;
            int armed = 0;
            int active = 0;
            int last_fire_date = -1;
            if (sscanf(entry, "%u,%d,%d,%d,%d", &minutes_of_day, &level_high, &armed, &active, &last_fire_date) == 5) {
                _hardwareGpioTestAlarmMinutesOfDay[index] = static_cast<uint16_t>(std::min<unsigned>(minutes_of_day, (24U * 60U) - 1U));
                _hardwareGpioTestAlarmLevelsHigh[index] = level_high != 0;
                _hardwareGpioTestAlarmArmed[index] = armed != 0;
                _hardwareGpioTestAlarmActive[index] = active != 0;
                _hardwareGpioTestAlarmLastFireDate[index] = last_fire_date;
            }
        }
    }
#endif
}

void AppSettings::persistHardwareGpioTestSchedules(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    char schedule_state[kHardwareGpioTestPersistBufferSize] = {};
    size_t schedule_offset = 0;
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        const int written = snprintf(schedule_state + schedule_offset,
                                     sizeof(schedule_state) - schedule_offset,
                                     "%u,%u,%d,%d,%lld;",
                                     static_cast<unsigned>(_hardwareGpioTestTimerDurationsSec[index]),
                                     static_cast<unsigned>(_hardwareGpioTestTimerRemainingSec[index]),
                                     _hardwareGpioTestTimerLevelsHigh[index] ? 1 : 0,
                                     _hardwareGpioTestTimerRunning[index] ? 1 : 0,
                                     static_cast<long long>(_hardwareGpioTestTimerEndEpochSec[index]));
        if ((written < 0) || (static_cast<size_t>(written) >= (sizeof(schedule_state) - schedule_offset))) {
            break;
        }
        schedule_offset += static_cast<size_t>(written);
    }
    (void)setNvsStringParam(NVS_KEY_GPIO_TIMER_STATE, schedule_state);

    memset(schedule_state, 0, sizeof(schedule_state));
    schedule_offset = 0;
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        const int written = snprintf(schedule_state + schedule_offset,
                                     sizeof(schedule_state) - schedule_offset,
                                     "%u,%d,%d,%d,%d;",
                                     static_cast<unsigned>(_hardwareGpioTestAlarmMinutesOfDay[index]),
                                     _hardwareGpioTestAlarmLevelsHigh[index] ? 1 : 0,
                                     _hardwareGpioTestAlarmArmed[index] ? 1 : 0,
                                     _hardwareGpioTestAlarmActive[index] ? 1 : 0,
                                     static_cast<int>(_hardwareGpioTestAlarmLastFireDate[index]));
        if ((written < 0) || (static_cast<size_t>(written) >= (sizeof(schedule_state) - schedule_offset))) {
            break;
        }
        schedule_offset += static_cast<size_t>(written);
    }
    (void)setNvsStringParam(NVS_KEY_GPIO_ALARM_STATE, schedule_state);
#endif
}

void AppSettings::releaseHardwareGpioTestPins(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    stopHardwareGpioTestWave();
    stopHardwareGpioTestPwm();
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        gpio_reset_pin(static_cast<gpio_num_t>(kHardwareGpioTestPins[index]));
        _hardwareGpioTestLevels[index] = -1;
        _hardwareGpioTestPwmValues[index] = 0;
    }
    applyHardwareGpioTestScheduledOutputs();
    refreshHardwareGpioTestUi();
#endif
}

void AppSettings::onHardwareGpioTestWaveTimerCallback(lv_timer_t *timer)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)timer;
    return;
#else
    AppSettings *app = (timer != nullptr) ? static_cast<AppSettings *>(timer->user_data) : nullptr;
    if ((app == nullptr) || (app->_hardwareGpioTestMode != HardwareGpioTestMode::Wave) ||
        (app->_hardwareGpioTestActiveWaveIndex < 0)) {
        return;
    }

    const size_t index = static_cast<size_t>(app->_hardwareGpioTestActiveWaveIndex);
    const uint16_t duration_tenths = app->_hardwareGpioTestWaveDurationsTenths[index];
    if (duration_tenths < kHardwareGpioTestWaveMinTenths) {
        return;
    }

    const int64_t period_us = static_cast<int64_t>(duration_tenths) * 100000LL;
    const int64_t now_us = esp_timer_get_time();
    const float phase = static_cast<float>((now_us - app->_hardwareGpioTestWaveStartUs) % period_us) /
                        static_cast<float>(period_us);

    float duty_percent = 0.0f;
    switch (app->_hardwareGpioTestWaveforms[index]) {
    case HardwareGpioTestWaveform::Sine:
        static constexpr float kPi = 3.14159265f;
        duty_percent = (std::sin((2.0f * kPi * phase) - (kPi / 2.0f)) + 1.0f) * 50.0f;
        break;
    case HardwareGpioTestWaveform::Sharp:
        duty_percent = (phase < 0.5f) ? 100.0f : 0.0f;
        break;
    case HardwareGpioTestWaveform::Step:
    default: {
        const int step_index = std::min(9, static_cast<int>(phase * 10.0f));
        duty_percent = (static_cast<float>(step_index) / 9.0f) * 100.0f;
        break;
    }
    }

    const uint32_t duty_value = static_cast<uint32_t>((kHardwareGpioTestPwmMaxDuty * duty_percent) / 100.0f);
    app->_hardwareGpioTestPwmValues[index] = static_cast<uint8_t>(std::lround(duty_percent));
    ledc_set_duty(kHardwareGpioTestPwmSpeedMode, kHardwareGpioTestPwmChannel, duty_value);
    ledc_update_duty(kHardwareGpioTestPwmSpeedMode, kHardwareGpioTestPwmChannel);
#endif
}

void AppSettings::onHardwareGpioTestTimerTickCallback(lv_timer_t *timer)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)timer;
    return;
#else
    AppSettings *app = (timer != nullptr) ? static_cast<AppSettings *>(timer->user_data) : nullptr;
    if (app == nullptr) {
        return;
    }

    app->_hardwareGpioTestTimerAlertBlinkVisible = ((lv_tick_get() / 300U) % 2U) == 0U;
    app->processHardwareGpioTestTimers();
    app->processHardwareGpioTestAlarms();
    app->refreshHardwareGpioTestUi();
#endif
}

void AppSettings::onHardwareGpioTestClickedEventCallback(lv_event_t *e)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    if (target == app->_hardwareGpioTestAllLowButton) {
        app->setHardwareGpioTestAllLevels(false);
        return;
    }
    if (target == app->_hardwareGpioTestAllHighButton) {
        app->setHardwareGpioTestAllLevels(true);
        return;
    }

    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (target == app->_hardwareGpioTestLowButtons[index]) {
            app->setHardwareGpioTestLevel(index, false);
            return;
        }
        if (target == app->_hardwareGpioTestHighButtons[index]) {
            app->setHardwareGpioTestLevel(index, true);
            return;
        }
    }
#endif
}

void AppSettings::onHardwareGpioTestModeChangedEventCallback(lv_event_t *e)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }
    if (app->_hardwareGpioTestUiSyncInProgress) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    if (!lv_obj_ready(target)) {
        return;
    }

    app->setHardwareGpioTestMode(static_cast<HardwareGpioTestMode>(lv_dropdown_get_selected(target)));
#endif
}

void AppSettings::onHardwareGpioTestPwmChangedEventCallback(lv_event_t *e)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }
    if (app->_hardwareGpioTestUiSyncInProgress) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (target == app->_hardwareGpioTestPwmSliders[index]) {
            app->setHardwareGpioTestPwm(index, static_cast<uint8_t>(lv_slider_get_value(target)));
            return;
        }
    }
#endif
}

void AppSettings::onHardwareGpioTestWaveChangedEventCallback(lv_event_t *e)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }
    if (app->_hardwareGpioTestUiSyncInProgress) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (target == app->_hardwareGpioTestWaveDropdowns[index]) {
            app->_hardwareGpioTestWaveforms[index] = static_cast<HardwareGpioTestWaveform>(lv_dropdown_get_selected(target));
            if (app->_hardwareGpioTestMode == HardwareGpioTestMode::Wave) {
                app->setHardwareGpioTestWave(index);
            } else {
                app->refreshHardwareGpioTestUi();
            }
            return;
        }
    }
#endif
}

void AppSettings::onHardwareGpioTestWaveDurationEventCallback(lv_event_t *e)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }
    if (app->_hardwareGpioTestUiSyncInProgress) {
        return;
    }

    if ((code == LV_EVENT_FOCUSED) || (code == LV_EVENT_CLICKED)) {
        lv_obj_t *target = lv_event_get_target(e);
        for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
            if (target != app->_hardwareGpioTestTimerDurationInputs[index]) {
                continue;
            }

            if (app->_hardwareGpioTestTimerRunning[index] || app->_hardwareGpioTestTimerAlertActive[index]) {
                app->stopHardwareGpioTestTimer(index, true);
                app->refreshHardwareGpioTestUi();
            }
            break;
        }
        app->setHardwareGpioTestKeyboardVisible(true, lv_event_get_target(e));
        return;
    }

    if ((code == LV_EVENT_DEFOCUSED) || (code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        app->setHardwareGpioTestKeyboardVisible(false);
    }

    if ((code != LV_EVENT_VALUE_CHANGED) && (code != LV_EVENT_DEFOCUSED) && (code != LV_EVENT_READY) && (code != LV_EVENT_CANCEL)) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (target != app->_hardwareGpioTestWaveDurationInputs[index]) {
            continue;
        }

        uint16_t duration_tenths = 0;
        const bool valid = parse_hardware_gpio_test_wave_tenths(lv_textarea_get_text(target), &duration_tenths);
        if (valid) {
            app->_hardwareGpioTestWaveDurationsTenths[index] = std::max<uint16_t>(kHardwareGpioTestWaveMinTenths,
                                                                                  std::min<uint16_t>(kHardwareGpioTestWaveMaxTenths, duration_tenths));
            if (app->_hardwareGpioTestMode == HardwareGpioTestMode::Wave) {
                app->setHardwareGpioTestWave(index);
            }
        }

        if ((code == LV_EVENT_DEFOCUSED) || (code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
            char duration_text[8] = {0};
            format_hardware_gpio_test_wave_tenths(app->_hardwareGpioTestWaveDurationsTenths[index], duration_text, sizeof(duration_text));
            lv_textarea_set_text(target, duration_text);
        }
        return;
    }
#endif
}

void AppSettings::onHardwareGpioTestTimerDurationEventCallback(lv_event_t *e)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }
    if (app->_hardwareGpioTestUiSyncInProgress) {
        return;
    }

    if ((code == LV_EVENT_FOCUSED) || (code == LV_EVENT_CLICKED)) {
        lv_obj_t *target = lv_event_get_target(e);
        for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
            if (target != app->_hardwareGpioTestAlarmTimeInputs[index]) {
                continue;
            }

            if (app->_hardwareGpioTestAlarmActive[index] || app->_hardwareGpioTestAlarmArmed[index]) {
                app->_hardwareGpioTestAlarmActive[index] = false;
                app->_hardwareGpioTestAlarmArmed[index] = false;
                gpio_reset_pin(static_cast<gpio_num_t>(kHardwareGpioTestPins[index]));
                app->_hardwareGpioTestLevels[index] = -1;
                if (app->_hardwareGpioTestTimerRunning[index]) {
                    (void)app->applyHardwareGpioTestDrivenLevel(index, app->_hardwareGpioTestTimerLevelsHigh[index]);
                }
                app->persistHardwareGpioTestSchedules();
                app->refreshHardwareGpioTestUi();
            }
            break;
        }
        app->setHardwareGpioTestKeyboardVisible(true, lv_event_get_target(e));
        return;
    }

    if ((code == LV_EVENT_DEFOCUSED) || (code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        app->setHardwareGpioTestKeyboardVisible(false);
    }

    if ((code != LV_EVENT_VALUE_CHANGED) && (code != LV_EVENT_DEFOCUSED) && (code != LV_EVENT_READY) && (code != LV_EVENT_CANCEL)) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (target != app->_hardwareGpioTestTimerDurationInputs[index]) {
            continue;
        }

        if ((code == LV_EVENT_VALUE_CHANGED) &&
            (app->_hardwareGpioTestTimerRunning[index] || app->_hardwareGpioTestTimerAlertActive[index])) {
            app->stopHardwareGpioTestTimer(index, false);
        }

        uint16_t seconds = 0;
        const bool valid = parse_hardware_gpio_test_timer_seconds(lv_textarea_get_text(target), &seconds);
        if (valid) {
            app->_hardwareGpioTestTimerDurationsSec[index] = seconds;
            app->_hardwareGpioTestTimerRemainingSec[index] = seconds;
            app->_hardwareGpioTestTimerEndEpochSec[index] = 0;
            app->persistHardwareGpioTestSchedules();
        }

        if ((code == LV_EVENT_DEFOCUSED) || (code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
            char timer_text[8] = {0};
            format_hardware_gpio_test_timer_seconds(app->_hardwareGpioTestTimerDurationsSec[index], timer_text, sizeof(timer_text));
            lv_textarea_set_text(target, timer_text);
        }

        app->refreshHardwareGpioTestUi();
        return;
    }
#endif
}

void AppSettings::onHardwareGpioTestTimerLevelChangedEventCallback(lv_event_t *e)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }
    if (app->_hardwareGpioTestUiSyncInProgress) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (target != app->_hardwareGpioTestTimerLevelDropdowns[index]) {
            continue;
        }

        app->_hardwareGpioTestTimerLevelsHigh[index] = lv_dropdown_get_selected(target) == 0;
        if (app->_hardwareGpioTestTimerRunning[index] || app->_hardwareGpioTestTimerAlertActive[index]) {
            app->stopHardwareGpioTestTimer(index, app->_hardwareGpioTestTimerRunning[index]);
        }
        app->persistHardwareGpioTestSchedules();
        app->refreshHardwareGpioTestUi();
        return;
    }
#endif
}

void AppSettings::onHardwareGpioTestTimerButtonClickedEventCallback(lv_event_t *e)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (target == app->_hardwareGpioTestTimerButtons[index]) {
            app->toggleHardwareGpioTestTimer(index);
            return;
        }
    }
#endif
}

void AppSettings::onHardwareGpioTestAlarmTimeEventCallback(lv_event_t *e)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }
    if (app->_hardwareGpioTestUiSyncInProgress) {
        return;
    }

    if ((code == LV_EVENT_FOCUSED) || (code == LV_EVENT_CLICKED)) {
        app->setHardwareGpioTestKeyboardVisible(true, lv_event_get_target(e));
        return;
    }

    if ((code == LV_EVENT_DEFOCUSED) || (code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        app->setHardwareGpioTestKeyboardVisible(false);
    }

    if ((code != LV_EVENT_VALUE_CHANGED) && (code != LV_EVENT_DEFOCUSED) && (code != LV_EVENT_READY) && (code != LV_EVENT_CANCEL)) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (target != app->_hardwareGpioTestAlarmTimeInputs[index]) {
            continue;
        }

        uint16_t minutes_of_day = 0;
        const bool valid = parse_hardware_gpio_test_alarm_minutes(lv_textarea_get_text(target), &minutes_of_day);
        if (valid) {
            app->_hardwareGpioTestAlarmMinutesOfDay[index] = minutes_of_day;
            app->_hardwareGpioTestAlarmLastFireDate[index] = -1;
            if (app->_hardwareGpioTestAlarmActive[index]) {
                gpio_reset_pin(static_cast<gpio_num_t>(kHardwareGpioTestPins[index]));
                app->_hardwareGpioTestLevels[index] = -1;
                app->_hardwareGpioTestAlarmActive[index] = false;
                if (app->_hardwareGpioTestTimerRunning[index]) {
                    (void)app->applyHardwareGpioTestDrivenLevel(index, app->_hardwareGpioTestTimerLevelsHigh[index]);
                }
            }
            app->persistHardwareGpioTestSchedules();
        }

        if ((code == LV_EVENT_DEFOCUSED) || (code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
            char alarm_text[8] = {0};
            format_hardware_gpio_test_alarm_minutes(app->_hardwareGpioTestAlarmMinutesOfDay[index], alarm_text, sizeof(alarm_text));
            lv_textarea_set_text(target, alarm_text);
        }

        app->refreshHardwareGpioTestUi();
        return;
    }
#endif
}

void AppSettings::onHardwareGpioTestAlarmLevelChangedEventCallback(lv_event_t *e)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }
    if (app->_hardwareGpioTestUiSyncInProgress) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (target != app->_hardwareGpioTestAlarmLevelDropdowns[index]) {
            continue;
        }

        app->_hardwareGpioTestAlarmLevelsHigh[index] = lv_dropdown_get_selected(target) == 0;
        if (app->_hardwareGpioTestAlarmActive[index]) {
            gpio_reset_pin(static_cast<gpio_num_t>(kHardwareGpioTestPins[index]));
            app->_hardwareGpioTestLevels[index] = -1;
            app->_hardwareGpioTestAlarmActive[index] = false;
            if (app->_hardwareGpioTestTimerRunning[index]) {
                (void)app->applyHardwareGpioTestDrivenLevel(index, app->_hardwareGpioTestTimerLevelsHigh[index]);
            }
        }
        app->persistHardwareGpioTestSchedules();
        app->refreshHardwareGpioTestUi();
        return;
    }
#endif
}

void AppSettings::onHardwareGpioTestKeyboardEventCallback(lv_event_t *e)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target = nullptr;

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = app->_hardwareGpioTestKeyboardTarget;
    if ((code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        app->setHardwareGpioTestKeyboardVisible(false);
        if ((target != nullptr) && lv_obj_ready(target)) {
            lv_obj_clear_state(target, LV_STATE_FOCUSED);
        }
    }

end:
    return;
#endif
}

void AppSettings::onHardwareGpioTestAlarmToggleClickedEventCallback(lv_event_t *e)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    for (size_t index = 0; index < AppSettings::kHardwareGpioTestPinCount; ++index) {
        if (target != app->_hardwareGpioTestAlarmToggleButtons[index]) {
            continue;
        }

        app->_hardwareGpioTestAlarmArmed[index] = !app->_hardwareGpioTestAlarmArmed[index];
        if (!app->_hardwareGpioTestAlarmArmed[index]) {
            app->_hardwareGpioTestAlarmActive[index] = false;
            gpio_reset_pin(static_cast<gpio_num_t>(kHardwareGpioTestPins[index]));
            app->_hardwareGpioTestLevels[index] = -1;
            if (app->_hardwareGpioTestTimerRunning[index]) {
                (void)app->applyHardwareGpioTestDrivenLevel(index, app->_hardwareGpioTestTimerLevelsHigh[index]);
            }
        } else {
            app->_hardwareGpioTestAlarmLastFireDate[index] = -1;
        }

        app->persistHardwareGpioTestSchedules();
        app->refreshHardwareGpioTestUi();
        return;
    }
#endif
}

void AppSettings::onHardwareGpioTestAudioSelectionChangedEventCallback(lv_event_t *e)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    (void)e;
    return;
#else
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }
    if (app->_hardwareGpioTestUiSyncInProgress) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    const uint16_t selected = lv_dropdown_get_selected(target);
    const std::string selected_path = (selected == 0) || (selected > app->_hardwareGpioTestAudioFilePaths.size())
                                          ? std::string()
                                          : app->_hardwareGpioTestAudioFilePaths[selected - 1];

    if (target == app->_hardwareGpioTestTimerAudioDropdown) {
        app->_hardwareGpioTestTimerAudioPath = selected_path;
        app->persistHardwareGpioTestAudioSelections();
        return;
    }
    if (target == app->_hardwareGpioTestAlarmAudioDropdown) {
        app->_hardwareGpioTestAlarmAudioPath = selected_path;
        app->persistHardwareGpioTestAudioSelections();
        return;
    }
#endif
}

void AppSettings::onDebugBuildGpioControlScreenAsync(void *context)
{
    AppSettings *app = static_cast<AppSettings *>(context);
    if (app == nullptr) {
        return;
    }

    app->ensureGpioTestScreen();
    app->refreshHardwareGpioTestUi();
}

bool AppSettings::debugQueueBuildGpioControlScreen(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return false;
#else
    return lv_async_call(onDebugBuildGpioControlScreenAsync, this) == LV_RES_OK;
#endif
}