void AppSettings::onSliderPanelVolumeSwitchValueChangeEventCallback(lv_event_t *e)
{
    int volume = lv_slider_get_value(ui_SliderPanelScreenSettingVolumeSwitch);

    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (volume != app->_nvs_param_map[NVS_KEY_AUDIO_VOLUME]) {
        if ((bsp_extra_audio_media_volume_set(volume) != ESP_OK) && (bsp_extra_audio_media_volume_get() != volume)) {
            ESP_LOGE(TAG, "Set volume failed");
            lv_slider_set_value(ui_SliderPanelScreenSettingVolumeSwitch, app->_nvs_param_map[NVS_KEY_AUDIO_VOLUME], LV_ANIM_OFF);
            return;
        }
        app->_nvs_param_map[NVS_KEY_AUDIO_VOLUME] = volume;
        app->setNvsParam(NVS_KEY_AUDIO_VOLUME, volume);
    }

end:
    return;
}

void AppSettings::onSliderPanelSystemVolumeValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    int volume = 0;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");
    ESP_BROOKESIA_CHECK_NULL_GOTO(app->_audioSystemVolumeSlider, end, "Invalid system volume slider");

    volume = lv_slider_get_value(app->_audioSystemVolumeSlider);
    if (volume != app->_nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME]) {
        if ((bsp_extra_audio_system_volume_set(volume) != ESP_OK) &&
            (bsp_extra_audio_system_volume_get() != volume)) {
            ESP_LOGE(TAG, "Set system sound volume failed");
            lv_slider_set_value(app->_audioSystemVolumeSlider, app->_nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME], LV_ANIM_OFF);
            goto end;
        }

        app->_nvs_param_map[NVS_KEY_SYSTEM_AUDIO_VOLUME] = volume;
        app->setNvsParam(NVS_KEY_SYSTEM_AUDIO_VOLUME, volume);
    }

    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        bsp_extra_audio_play_system_notification();
    }

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingTapSoundValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");
    ESP_BROOKESIA_CHECK_NULL_GOTO(app->_audioTapSoundSwitch, end, "Invalid tap sound switch");

    enabled = (lv_obj_get_state(app->_audioTapSoundSwitch) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_AUDIO_TAP_SOUND] = enabled ? 1 : 0;
    app->setNvsParam(NVS_KEY_AUDIO_TAP_SOUND, enabled ? 1 : 0);
    jc_ui_tap_sound_set_enabled(enabled);

end:
    return;
}

void AppSettings::onSwitchPanelScreenSettingHapticFeedbackValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    bool enabled = false;
    lv_obj_t *target = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");
    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid haptic feedback switch");

    enabled = (lv_obj_get_state(target) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_AUDIO_HAPTIC_FEEDBACK] = enabled ? 1 : 0;
    app->setNvsParam(NVS_KEY_AUDIO_HAPTIC_FEEDBACK, enabled ? 1 : 0);
    jc_ui_haptic_feedback_set_enabled(enabled);

end:
    return;
}