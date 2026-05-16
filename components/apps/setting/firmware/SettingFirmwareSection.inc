void AppSettings::ensureFirmwareScreen(void)
{
#if !CONFIG_JC4880_FEATURE_OTA
    return;
#else
    if ((_firmwareScreen != nullptr) && lv_obj_ready(_firmwareScreen)) {
        return;
    }

    _firmwareScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_firmwareScreen, lv_color_hex(0xE5F3FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_firmwareScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_firmwareScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *firmwareTitle = lv_label_create(_firmwareScreen);
    lv_label_set_text(firmwareTitle, "Firmware");
    lv_obj_set_style_text_font(firmwareTitle, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(firmwareTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(firmwareTitle, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *firmwarePanel = lv_obj_create(_firmwareScreen);
    lv_obj_set_size(firmwarePanel, lv_pct(92), 650);
    lv_obj_align(firmwarePanel, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_style_radius(firmwarePanel, 20, 0);
    lv_obj_set_style_border_width(firmwarePanel, 0, 0);
    lv_obj_set_style_bg_color(firmwarePanel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_all(firmwarePanel, 14, 0);
    lv_obj_set_style_pad_row(firmwarePanel, 12, 0);
    lv_obj_set_flex_flow(firmwarePanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(firmwarePanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(firmwarePanel, LV_DIR_VER);

    auto createFirmwareSection = [](lv_obj_t *parent, const char *title) {
        lv_obj_t *section = lv_obj_create(parent);
        lv_obj_set_width(section, lv_pct(100));
        lv_obj_set_height(section, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(section, 18, 0);
        lv_obj_set_style_border_width(section, 0, 0);
        lv_obj_set_style_bg_color(section, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_pad_all(section, 14, 0);
        lv_obj_set_style_pad_row(section, 10, 0);
        lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t *sectionTitle = lv_label_create(section);
        lv_label_set_text(sectionTitle, title);
        lv_obj_set_style_text_font(sectionTitle, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(sectionTitle, lv_color_hex(0x0F172A), 0);
        return section;
    };

    auto createFirmwareControlsRow = [](lv_obj_t *parent) {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_row(row, 8, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        return row;
    };

    lv_obj_t *currentSection = createFirmwareSection(firmwarePanel, "Installed Firmware");
    _firmwareCurrentVersionLabel = lv_label_create(currentSection);
    lv_obj_set_width(_firmwareCurrentVersionLabel, lv_pct(100));
    lv_label_set_long_mode(_firmwareCurrentVersionLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_firmwareCurrentVersionLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(_firmwareCurrentVersionLabel, lv_color_hex(0x0F172A), 0);

    lv_obj_t *otaAutoUpdateRow = lv_obj_create(currentSection);
    lv_obj_set_width(otaAutoUpdateRow, lv_pct(100));
    lv_obj_set_height(otaAutoUpdateRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(otaAutoUpdateRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(otaAutoUpdateRow, 16, 0);
    lv_obj_set_style_border_width(otaAutoUpdateRow, 0, 0);
    lv_obj_set_style_bg_color(otaAutoUpdateRow, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_opa(otaAutoUpdateRow, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(otaAutoUpdateRow, 14, 0);
    lv_obj_set_style_pad_right(otaAutoUpdateRow, 14, 0);
    lv_obj_set_style_pad_top(otaAutoUpdateRow, 12, 0);
    lv_obj_set_style_pad_bottom(otaAutoUpdateRow, 12, 0);

    lv_obj_t *otaAutoUpdateTitle = lv_label_create(otaAutoUpdateRow);
    lv_label_set_text(otaAutoUpdateTitle, "Auto Update");
    lv_obj_set_style_text_font(otaAutoUpdateTitle, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(otaAutoUpdateTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(otaAutoUpdateTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *otaAutoUpdateDetail = lv_label_create(otaAutoUpdateRow);
    lv_obj_set_width(otaAutoUpdateDetail, 240);
    lv_label_set_long_mode(otaAutoUpdateDetail, LV_LABEL_LONG_WRAP);
    lv_label_set_text(otaAutoUpdateDetail, "Automatically start the preferred OTA release when a new update is detected.");
    lv_obj_set_style_text_font(otaAutoUpdateDetail, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(otaAutoUpdateDetail, lv_color_hex(0x475569), 0);
    lv_obj_align_to(otaAutoUpdateDetail, otaAutoUpdateTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    _firmwareAutoUpdateSwitch = lv_switch_create(otaAutoUpdateRow);
    lv_obj_align(_firmwareAutoUpdateSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_firmwareAutoUpdateSwitch, onFirmwareAutoUpdateSwitchValueChangeEventCallback,
                        LV_EVENT_VALUE_CHANGED, this);
    if (_nvs_param_map[NVS_KEY_OTA_AUTO_UPDATE] != 0) {
        lv_obj_add_state(_firmwareAutoUpdateSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(_firmwareAutoUpdateSwitch, LV_STATE_CHECKED);
    }

    lv_obj_t *sdSection = createFirmwareSection(firmwarePanel, "Flash from SD Card");
    lv_obj_t *sdHint = lv_label_create(sdSection);
    lv_obj_set_width(sdHint, lv_pct(100));
    lv_label_set_long_mode(sdHint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(sdHint, "Select a validated .bin firmware image from /sdcard or /sdcard/firmware.");
    lv_obj_set_style_text_font(sdHint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sdHint, lv_color_hex(0x475569), 0);

    lv_obj_t *sdControlsRow = createFirmwareControlsRow(sdSection);

    _firmwareSdDropdown = lv_dropdown_create(sdControlsRow);
    lv_obj_set_size(_firmwareSdDropdown, 220, 48);
    lv_obj_set_flex_grow(_firmwareSdDropdown, 1);
    lv_obj_add_event_cb(_firmwareSdDropdown, onFirmwareSelectionChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *sdRefreshButton = lv_btn_create(sdControlsRow);
    lv_obj_set_size(sdRefreshButton, 92, 48);
    lv_obj_set_style_radius(sdRefreshButton, 16, 0);
    lv_obj_set_style_border_width(sdRefreshButton, 0, 0);
    lv_obj_set_style_bg_color(sdRefreshButton, lv_color_hex(0xCBD5E1), 0);
    lv_obj_add_event_cb(sdRefreshButton, onFirmwareSdRefreshClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *sdRefreshLabel = lv_label_create(sdRefreshButton);
    lv_label_set_text(sdRefreshLabel, "Scan");
    lv_obj_center(sdRefreshLabel);

    _firmwareSdFlashButton = lv_btn_create(sdControlsRow);
    lv_obj_set_size(_firmwareSdFlashButton, 92, 48);
    lv_obj_set_style_radius(_firmwareSdFlashButton, 16, 0);
    lv_obj_set_style_border_width(_firmwareSdFlashButton, 0, 0);
    lv_obj_add_event_cb(_firmwareSdFlashButton, onFirmwareSdFlashClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *sdFlashLabel = lv_label_create(_firmwareSdFlashButton);
    lv_label_set_text(sdFlashLabel, "Flash");
    lv_obj_center(sdFlashLabel);

    lv_obj_t *otaSection = createFirmwareSection(firmwarePanel, "Check GitHub Releases");
    lv_obj_t *otaHint = lv_label_create(otaSection);
    lv_obj_set_width(otaHint, lv_pct(100));
    lv_label_set_long_mode(otaHint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(otaHint, "Check GitHub releases, review installed and latest versions below, then tick one firmware to flash.");
    lv_obj_set_style_text_font(otaHint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(otaHint, lv_color_hex(0x475569), 0);

    lv_obj_t *otaControlsRow = createFirmwareControlsRow(otaSection);

    _firmwareOtaCheckButton = lv_btn_create(otaControlsRow);
    lv_obj_set_size(_firmwareOtaCheckButton, 96, 48);
    lv_obj_set_style_radius(_firmwareOtaCheckButton, 16, 0);
    lv_obj_set_style_border_width(_firmwareOtaCheckButton, 0, 0);
    lv_obj_add_event_cb(_firmwareOtaCheckButton, onFirmwareOtaCheckClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *otaCheckLabel = lv_label_create(_firmwareOtaCheckButton);
    lv_label_set_text(otaCheckLabel, "Check");
    lv_obj_center(otaCheckLabel);

    _firmwareOtaFlashButton = lv_btn_create(otaControlsRow);
    lv_obj_set_size(_firmwareOtaFlashButton, 92, 48);
    lv_obj_set_style_radius(_firmwareOtaFlashButton, 16, 0);
    lv_obj_set_style_border_width(_firmwareOtaFlashButton, 0, 0);
    lv_obj_add_event_cb(_firmwareOtaFlashButton, onFirmwareOtaFlashClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *otaFlashLabel = lv_label_create(_firmwareOtaFlashButton);
    lv_label_set_text(otaFlashLabel, "Flash");
    lv_obj_center(otaFlashLabel);

    _firmwareOtaSummaryLabel = lv_label_create(otaSection);
    lv_obj_set_width(_firmwareOtaSummaryLabel, lv_pct(100));
    lv_label_set_long_mode(_firmwareOtaSummaryLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_firmwareOtaSummaryLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_firmwareOtaSummaryLabel, lv_color_hex(0x334155), 0);

    _firmwareOtaListContainer = lv_obj_create(otaSection);
    lv_obj_set_width(_firmwareOtaListContainer, lv_pct(100));
    lv_obj_set_height(_firmwareOtaListContainer, 210);
    lv_obj_set_style_radius(_firmwareOtaListContainer, 16, 0);
    lv_obj_set_style_bg_color(_firmwareOtaListContainer, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_border_color(_firmwareOtaListContainer, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_border_width(_firmwareOtaListContainer, 1, 0);
    lv_obj_set_style_pad_all(_firmwareOtaListContainer, 12, 0);
    lv_obj_set_style_pad_row(_firmwareOtaListContainer, 8, 0);
    lv_obj_set_scroll_dir(_firmwareOtaListContainer, LV_DIR_VER);
    lv_obj_set_flex_flow(_firmwareOtaListContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_firmwareOtaListContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    _firmwareProgressBar = lv_bar_create(firmwarePanel);
    lv_obj_set_width(_firmwareProgressBar, lv_pct(100));
    lv_obj_set_height(_firmwareProgressBar, 18);
    lv_bar_set_range(_firmwareProgressBar, 0, 100);
    lv_bar_set_value(_firmwareProgressBar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(_firmwareProgressBar, 9, 0);
    lv_obj_set_style_bg_color(_firmwareProgressBar, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_firmwareProgressBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_firmwareProgressBar, lv_color_hex(0x2563EB), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_firmwareProgressBar, LV_OPA_COVER, LV_PART_INDICATOR);

    _firmwareProgressLabel = lv_label_create(firmwarePanel);
    lv_obj_set_width(_firmwareProgressLabel, lv_pct(100));
    lv_label_set_long_mode(_firmwareProgressLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_firmwareProgressLabel, "Idle");
    lv_obj_set_style_text_font(_firmwareProgressLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_firmwareProgressLabel, lv_color_hex(0x64748B), 0);

    _firmwareStatusLabel = lv_label_create(firmwarePanel);
    lv_obj_set_width(_firmwareStatusLabel, lv_pct(100));
    lv_label_set_long_mode(_firmwareStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_firmwareStatusLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_firmwareStatusLabel, lv_color_hex(0x334155), 0);

    lv_obj_t *dangerSection = createFirmwareSection(firmwarePanel, "Danger Zone");
    lv_obj_t *dangerHint = lv_label_create(dangerSection);
    lv_obj_set_width(dangerHint, lv_pct(100));
    lv_label_set_long_mode(dangerHint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(dangerHint, "Factory reset clears saved Settings preferences including Wi-Fi credentials, display, audio, and timezone options.");
    lv_obj_set_style_text_font(dangerHint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(dangerHint, lv_color_hex(0x7F1D1D), 0);

    lv_obj_t *dangerControlsRow = createFirmwareControlsRow(dangerSection);
    lv_obj_t *factoryResetButton = lv_btn_create(dangerControlsRow);
    lv_obj_set_size(factoryResetButton, 170, 50);
    lv_obj_set_style_radius(factoryResetButton, 16, 0);
    lv_obj_set_style_border_width(factoryResetButton, 0, 0);
    lv_obj_set_style_bg_color(factoryResetButton, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_bg_opa(factoryResetButton, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(factoryResetButton, lv_color_hex(0xB91C1C), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(factoryResetButton, onFirmwareFactoryResetClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *factoryResetLabel = lv_label_create(factoryResetButton);
    lv_label_set_text(factoryResetLabel, "Factory Reset");
    lv_obj_set_style_text_color(factoryResetLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(factoryResetLabel);

    _screen_list[UI_FIRMWARE_SETTING_INDEX] = _firmwareScreen;
    lv_obj_add_event_cb(_firmwareScreen, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
#endif
}
