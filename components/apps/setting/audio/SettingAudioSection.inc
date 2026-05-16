void AppSettings::initializeAudioUi(void)
{
    _audioMediaVolumeSlider = ui_SliderPanelScreenSettingVolumeSwitch;
    lv_obj_clear_flag(ui_PanelScreenSettingVolumeList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_left(ui_PanelScreenSettingVolumeList, 0, 0);
    lv_obj_set_style_pad_right(ui_PanelScreenSettingVolumeList, 0, 0);
    lv_obj_set_style_pad_top(ui_PanelScreenSettingVolumeList, 0, 0);
    lv_obj_set_style_pad_bottom(ui_PanelScreenSettingVolumeList, 0, 0);
    lv_obj_set_style_pad_row(ui_PanelScreenSettingVolumeList, 12, 0);
    lv_obj_set_style_bg_opa(ui_PanelScreenSettingVolumeList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui_PanelScreenSettingVolumeList, 0, 0);
    lv_obj_set_scroll_dir(ui_PanelScreenSettingVolumeList, LV_DIR_VER);

    auto styleAudioSliderRow = [](lv_obj_t *row, lv_obj_t *label, lv_obj_t *slider, lv_obj_t *icon) {
        lv_obj_set_parent(row, ui_PanelScreenSettingVolumeList);
        lv_obj_set_size(row, lv_pct(100), 72);
        lv_obj_set_x(row, 0);
        lv_obj_set_y(row, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(row, 18, 0);
        lv_obj_set_style_pad_right(row, 18, 0);
        lv_obj_set_style_pad_top(row, 10, 0);
        lv_obj_set_style_pad_bottom(row, 10, 0);

        if (icon != nullptr) {
            lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
        }

        if (label != nullptr) {
            lv_obj_set_width(label, 110);
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(0x111827), 0);
            if (icon != nullptr) {
                lv_obj_align_to(label, icon, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
            } else {
                lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
            }
        }

        if (slider != nullptr) {
            lv_obj_set_size(slider, 220, 14);
            lv_obj_align(slider, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    };

    auto createAudioSwitchRow = [](const char *title) {
        lv_obj_t *row = lv_obj_create(ui_PanelScreenSettingVolumeList);
        lv_obj_set_size(row, lv_pct(100), 72);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(row, 18, 0);
        lv_obj_set_style_pad_right(row, 18, 0);
        lv_obj_set_style_pad_top(row, 10, 0);
        lv_obj_set_style_pad_bottom(row, 10, 0);

        lv_obj_t *label = lv_label_create(row);
        lv_obj_set_width(label, 180);
        lv_label_set_text(label, title);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x111827), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        return row;
    };

    lv_label_set_text(ui_LabelPanelScreenSettingVolumeSwitch, "Media");
    lv_slider_set_range(ui_SliderPanelScreenSettingVolumeSwitch, SPEAKER_VOLUME_MIN, SPEAKER_VOLUME_MAX);
    lv_obj_add_event_cb(ui_SliderPanelScreenSettingVolumeSwitch, onSliderPanelVolumeSwitchValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);
    styleAudioSliderRow(ui_PanelScreenSettingVolumeSwitch, ui_LabelPanelScreenSettingVolumeSwitch,
                        ui_SliderPanelScreenSettingVolumeSwitch, ui_ImagePanelScreenSettingVolumeSwitch);

    {
        lv_obj_t *row = lv_obj_create(ui_PanelScreenSettingVolumeList);
        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text(label, "System Sounds");

        _audioSystemVolumeSlider = lv_slider_create(row);
        lv_slider_set_range(_audioSystemVolumeSlider, SPEAKER_VOLUME_MIN, SPEAKER_VOLUME_MAX);
        lv_obj_add_event_cb(_audioSystemVolumeSlider, onSliderPanelSystemVolumeValueChangeEventCallback,
                            LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(_audioSystemVolumeSlider, onSliderPanelSystemVolumeValueChangeEventCallback,
                            LV_EVENT_RELEASED, this);

        styleAudioSliderRow(row, label, _audioSystemVolumeSlider, nullptr);
    }

    {
        lv_obj_t *row = createAudioSwitchRow("Tap Sound");
        _audioTapSoundSwitch = lv_switch_create(row);
        lv_obj_align(_audioTapSoundSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(_audioTapSoundSwitch, onSwitchPanelScreenSettingTapSoundValueChangeEventCallback,
                            LV_EVENT_VALUE_CHANGED, this);
    }

    {
        lv_obj_t *row = createAudioSwitchRow("Haptic Feedback");
        _audioHapticFeedbackSwitch = lv_switch_create(row);
        lv_obj_align(_audioHapticFeedbackSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(_audioHapticFeedbackSwitch,
                            onSwitchPanelScreenSettingHapticFeedbackValueChangeEventCallback,
                            LV_EVENT_VALUE_CHANGED, this);
    }

    lv_obj_add_flag(ui_ButtonScreenSettingVolumeReturn, LV_OBJ_FLAG_HIDDEN);
    _screen_list[UI_VOLUME_SETTING_INDEX] = ui_ScreenSettingVolume;
    lv_obj_add_event_cb(ui_ScreenSettingVolume, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
}

void AppSettings::refreshAudioUi(void)
{
#if !CONFIG_JC4880_FEATURE_AUDIO
    return;
#endif
    lv_slider_set_value(ui_SliderPanelScreenSettingVolumeSwitch, _nvs_param_map[NVS_KEY_AUDIO_VOLUME], LV_ANIM_OFF);
    if (_audioSystemVolumeSlider != nullptr) {
        lv_slider_set_value(_audioSystemVolumeSlider, _nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME], LV_ANIM_OFF);
    }
    if (lv_obj_ready(_audioTapSoundSwitch)) {
        if (_nvs_param_map[NVS_KEY_AUDIO_TAP_SOUND] != 0) {
            lv_obj_add_state(_audioTapSoundSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_audioTapSoundSwitch, LV_STATE_CHECKED);
        }
    }
    if (lv_obj_ready(_audioHapticFeedbackSwitch)) {
        if (_nvs_param_map[NVS_KEY_AUDIO_HAPTIC_FEEDBACK] != 0) {
            lv_obj_add_state(_audioHapticFeedbackSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_audioHapticFeedbackSwitch, LV_STATE_CHECKED);
        }
    }
}