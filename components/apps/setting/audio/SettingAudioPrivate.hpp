#if defined(APP_SETTINGS_AUDIO_METHOD_DECLS)
    void initializeAudioUi(void);
    void refreshAudioUi(void);
#elif defined(APP_SETTINGS_AUDIO_CALLBACK_DECLS)
    static void onSliderPanelVolumeSwitchValueChangeEventCallback(lv_event_t *e);
    static void onSliderPanelSystemVolumeValueChangeEventCallback(lv_event_t *e);
    static void onSwitchPanelScreenSettingTapSoundValueChangeEventCallback(lv_event_t *e);
    static void onSwitchPanelScreenSettingHapticFeedbackValueChangeEventCallback(lv_event_t *e);
#elif defined(APP_SETTINGS_AUDIO_UI_STATE)
    lv_obj_t *_audioMediaVolumeSlider;
    lv_obj_t *_audioSystemVolumeSlider;
    lv_obj_t *_audioTapSoundSwitch;
    lv_obj_t *_audioHapticFeedbackSwitch;
#elif defined(APP_SETTINGS_AUDIO_UI_INIT)
    _audioMediaVolumeSlider(nullptr),
    _audioSystemVolumeSlider(nullptr),
    _audioTapSoundSwitch(nullptr),
    _audioHapticFeedbackSwitch(nullptr),
#elif defined(APP_SETTINGS_AUDIO_MENU_STATE)
    lv_obj_t *_audioMenuItem;
#elif defined(APP_SETTINGS_AUDIO_MENU_INIT)
    _audioMenuItem(nullptr),
#elif defined(APP_SETTINGS_AUDIO_MENU_CREATE)
    _audioMenuItem = createMainMenuItem("Audio", &ui_img_sound_png, nullptr, nullptr);
#endif