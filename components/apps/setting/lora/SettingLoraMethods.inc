void AppSettings::ensureLoRaScreen(void)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    return;
#else
    if ((_loraScreen != nullptr) && lv_obj_ready(_loraScreen)) {
        return;
    }

    _loraScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_loraScreen, lv_color_hex(0xE5F3FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_loraScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_loraScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(_loraScreen);
    lv_obj_set_size(panel, lv_pct(94), lv_pct(96));
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_set_style_pad_row(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    auto createSection = [](lv_obj_t *parent, const char *section_title, const char *hint) {
        lv_obj_t *section = lv_obj_create(parent);
        lv_obj_set_width(section, lv_pct(100));
        lv_obj_set_height(section, LV_SIZE_CONTENT);
        lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(section, 18, 0);
        lv_obj_set_style_border_width(section, 0, 0);
        lv_obj_set_style_bg_color(section, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_pad_all(section, 14, 0);
        lv_obj_set_style_pad_row(section, 10, 0);
        lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t *sectionLabel = lv_label_create(section);
        lv_label_set_text(sectionLabel, section_title);
        lv_obj_set_style_text_font(sectionLabel, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(sectionLabel, lv_color_hex(0x0F172A), 0);

        lv_obj_t *hintLabel = lv_label_create(section);
        lv_obj_set_width(hintLabel, lv_pct(100));
        lv_label_set_long_mode(hintLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(hintLabel, hint);
        lv_obj_set_style_text_font(hintLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(hintLabel, lv_color_hex(0x475569), 0);
        return section;
    };

    auto createDropdownRow = [this](lv_obj_t *parent, const char *row_title, const char *options, lv_obj_t **dropdown_out) {
        lv_obj_t *row = create_settings_toggle_row(parent, row_title);
        lv_obj_t *dropdown = lv_dropdown_create(row);
        lv_dropdown_set_options_static(dropdown, options);
        lv_obj_set_width(dropdown, 210);
        lv_obj_align(dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(dropdown, onLoRaConfigChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);
        if (dropdown_out != nullptr) {
            *dropdown_out = dropdown;
        }
        return row;
    };

    auto createTextRow = [this](lv_obj_t *parent, const char *row_title, const char *placeholder, lv_obj_t **textarea_out) {
        lv_obj_t *row = create_settings_toggle_row(parent, row_title);
        lv_obj_t *textarea = lv_textarea_create(row);
        lv_obj_set_size(textarea, 220, 46);
        lv_obj_align(textarea, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_textarea_set_one_line(textarea, true);
        lv_textarea_set_placeholder_text(textarea, placeholder);
        lv_obj_set_style_radius(textarea, 14, 0);
        lv_obj_set_style_border_width(textarea, 1, 0);
        lv_obj_set_style_border_color(textarea, lv_color_hex(0xCBD5E1), 0);
        lv_obj_set_style_bg_color(textarea, lv_color_hex(0xF8FAFC), 0);
        lv_obj_set_style_pad_left(textarea, 12, 0);
        lv_obj_set_style_pad_right(textarea, 12, 0);
        lv_obj_add_event_cb(textarea, onLoRaTextAreaEventCallback, LV_EVENT_ALL, this);
        if (textarea_out != nullptr) {
            *textarea_out = textarea;
        }
        return row;
    };

    lv_obj_t *generalSection = createSection(panel,
                                             "Radio",
                                             "These settings are shared with the LoRa Mesh app. Disable the radio here when Local Controller needs shared GPIOs for haptics or Neopixel.");
    lv_obj_t *enabledRow = create_settings_toggle_row(generalSection, "Enable LoRa Radio");
    _loraEnabledSwitch = lv_switch_create(enabledRow);
    lv_obj_align(_loraEnabledSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_loraEnabledSwitch, onLoRaConfigChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *identitySection = createSection(panel,
                                              "Identity",
                                              "Display Name and Common Chat Title are shared with the LoRa Mesh app.");
    createTextRow(identitySection, "Display Name", "Visible chat name", &_loraDisplayNameTextArea);
    createTextRow(identitySection, "Common Chat Title", "Common Mesh Chat", &_loraCommonChatTitleTextArea);

    createDropdownRow(generalSection, "Radio Module", kLoraModuleOptionsText, &_loraModuleDropdown);
    _loraFrequencyRow = createTextRow(generalSection, "Frequency (Hz)", "433125000", &_loraFrequencyTextArea);
    _loraSpreadingFactorRow = createTextRow(generalSection, "Spreading Factor", "9", &_loraSpreadingFactorTextArea);
    _loraBandwidthRow = createTextRow(generalSection, "Bandwidth Index", "4", &_loraBandwidthTextArea);
    _loraCodingRateRow = createTextRow(generalSection, "Coding Rate", "1", &_loraCodingRateTextArea);
    createTextRow(generalSection, "Hop Limit", "4", &_loraHopLimitTextArea);

    _loraE22Section = createSection(panel,
                                    "E22-400T22S UART",
                                    "These settings are programmed into the module before Self Check runs. Frequency maps to the module channel automatically.");
    createTextRow(_loraE22Section, "Address", "0", &_loraE22AddressTextArea);
    createDropdownRow(_loraE22Section, "UART Baud Rate", kLoraE22UartBaudOptionsText, &_loraE22UartBaudDropdown);
    createDropdownRow(_loraE22Section, "Air Data Rate", kLoraE22AirRateOptionsText, &_loraE22AirDataRateDropdown);
    createDropdownRow(_loraE22Section, "Sub Packet Size", kLoraE22SubPacketOptionsText, &_loraE22SubPacketDropdown);
    createDropdownRow(_loraE22Section, "Transmit Power", kLoraE22PowerOptionsText, &_loraE22PowerDropdown);
    createDropdownRow(_loraE22Section, "Transmission Mode", kLoraE22TransmissionOptionsText, &_loraE22TransmissionDropdown);
    createDropdownRow(_loraE22Section, "RSSI Option", kLoraE22RssiOptionsText, &_loraE22RssiDropdown);

    lv_obj_t *meshSection = createSection(panel,
                                          "Mesh Behavior",
                                          "Forwarding relays traffic for nearby peers. Public chat encryption uses the shared mesh group key.");
    lv_obj_t *forwardRow = create_settings_toggle_row(meshSection, "Enable Forwarding");
    _loraForwardingSwitch = lv_switch_create(forwardRow);
    lv_obj_align(_loraForwardingSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *encryptRow = create_settings_toggle_row(meshSection, "Encrypt Public Chat");
    _loraEncryptionSwitch = lv_switch_create(encryptRow);
    lv_obj_align(_loraEncryptionSwitch, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *pinsSection = createSection(panel,
                                          "Pin Mapping",
                                          "The fixed E22 wiring defaults are prefilled. Keep each active signal on a unique GPIO.");
    for (size_t role_index = 0; role_index < kLoraPinRoleCount; ++role_index) {
        _loraPinRows[role_index] = createDropdownRow(pinsSection,
                                                     lora_pin_role_label(static_cast<LoraPinRole>(role_index)),
                                                     kLoraGpioOptionsText,
                                                     &_loraPinDropdowns[role_index]);
    }

    lv_obj_t *applySection = createSection(panel,
                                           "Diagnostics",
                                           "Save writes directly to shared LoRa storage. Self Check uses the saved mapping; Auto Detect also tries UART pin recovery.");
    lv_obj_t *diagnosticButtonRow = lv_obj_create(applySection);
    lv_obj_set_width(diagnosticButtonRow, lv_pct(100));
    lv_obj_set_height(diagnosticButtonRow, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(diagnosticButtonRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(diagnosticButtonRow, 0, 0);
    lv_obj_set_style_pad_all(diagnosticButtonRow, 0, 0);
    lv_obj_set_style_pad_column(diagnosticButtonRow, 10, 0);
    lv_obj_set_flex_flow(diagnosticButtonRow, LV_FLEX_FLOW_ROW);

    _loraSelfCheckButton = lv_btn_create(diagnosticButtonRow);
    lv_obj_set_size(_loraSelfCheckButton, lv_pct(48), 54);
    lv_obj_set_style_radius(_loraSelfCheckButton, 16, 0);
    lv_obj_set_style_border_width(_loraSelfCheckButton, 0, 0);
    lv_obj_add_event_cb(_loraSelfCheckButton, onLoRaSelfCheckClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *selfCheckLabel = lv_label_create(_loraSelfCheckButton);
    lv_label_set_text(selfCheckLabel, "Self Check");
    lv_obj_center(selfCheckLabel);

    _loraAutoDetectButton = lv_btn_create(diagnosticButtonRow);
    lv_obj_set_size(_loraAutoDetectButton, lv_pct(48), 54);
    lv_obj_set_style_radius(_loraAutoDetectButton, 16, 0);
    lv_obj_set_style_border_width(_loraAutoDetectButton, 0, 0);
    lv_obj_set_style_bg_color(_loraAutoDetectButton, lv_color_hex(0x16A34A), 0);
    lv_obj_set_style_bg_color(_loraAutoDetectButton, lv_color_hex(0x15803D), LV_STATE_PRESSED);
    lv_obj_add_event_cb(_loraAutoDetectButton, onLoRaAutoDetectClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *autoDetectLabel = lv_label_create(_loraAutoDetectButton);
    lv_label_set_text(autoDetectLabel, "Auto Detect");
    lv_obj_set_style_text_color(autoDetectLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(autoDetectLabel);

    _loraDiagnosticLogButton = lv_btn_create(applySection);
    lv_obj_set_size(_loraDiagnosticLogButton, lv_pct(100), 46);
    lv_obj_set_style_radius(_loraDiagnosticLogButton, 14, 0);
    lv_obj_set_style_border_width(_loraDiagnosticLogButton, 0, 0);
    lv_obj_set_style_bg_color(_loraDiagnosticLogButton, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_color(_loraDiagnosticLogButton, lv_color_hex(0x1E293B), LV_STATE_PRESSED);
    lv_obj_add_event_cb(_loraDiagnosticLogButton, onLoRaDiagnosticLogClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *logButtonLabel = lv_label_create(_loraDiagnosticLogButton);
    lv_label_set_text(logButtonLabel, "Open Module Log");
    lv_obj_set_style_text_color(logButtonLabel, lv_color_hex(0xE2E8F0), 0);
    lv_obj_center(logButtonLabel);

    _loraSelfCheckStatusLabel = lv_label_create(applySection);
    lv_obj_set_width(_loraSelfCheckStatusLabel, lv_pct(100));
    lv_label_set_long_mode(_loraSelfCheckStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_loraSelfCheckStatusLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_loraSelfCheckStatusLabel, lv_color_hex(0x64748B), 0);

    lv_obj_t *saveButton = lv_btn_create(applySection);
    lv_obj_set_size(saveButton, lv_pct(100), 54);
    lv_obj_set_style_radius(saveButton, 16, 0);
    lv_obj_set_style_border_width(saveButton, 0, 0);
    lv_obj_add_event_cb(saveButton, onLoRaSaveClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *saveLabel = lv_label_create(saveButton);
    lv_label_set_text(saveLabel, "Save LoRa Settings");
    lv_obj_center(saveLabel);

    _loraInfoLabel = lv_label_create(applySection);
    lv_obj_set_width(_loraInfoLabel, lv_pct(100));
    lv_label_set_long_mode(_loraInfoLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_loraInfoLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_loraInfoLabel, lv_color_hex(0x475569), 0);

    _loraKeyboard = lv_keyboard_create(_loraScreen);
    lv_obj_set_size(_loraKeyboard, lv_pct(100), lv_pct(34));
    lv_obj_align(_loraKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(_loraKeyboard, LV_OBJ_FLAG_HIDDEN);
    jc4880_keyboard_install_case_behavior(_loraKeyboard);
    lv_obj_add_event_cb(_loraKeyboard, onLoRaKeyboardEventCallback, LV_EVENT_ALL, this);

    _loraDiagnosticLogOverlay = lv_obj_create(_loraScreen);
    lv_obj_set_size(_loraDiagnosticLogOverlay, lv_pct(100), lv_pct(100));
    lv_obj_center(_loraDiagnosticLogOverlay);
    lv_obj_set_style_bg_color(_loraDiagnosticLogOverlay, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(_loraDiagnosticLogOverlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(_loraDiagnosticLogOverlay, 0, 0);
    lv_obj_set_style_radius(_loraDiagnosticLogOverlay, 0, 0);
    lv_obj_clear_flag(_loraDiagnosticLogOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_loraDiagnosticLogOverlay, LV_OBJ_FLAG_HIDDEN);

    const lv_area_t log_visual_area = getVisualArea();
    _loraDiagnosticLogPanel = lv_obj_create(_loraDiagnosticLogOverlay);
    lv_obj_set_pos(_loraDiagnosticLogPanel, log_visual_area.x1, log_visual_area.y1);
    lv_obj_set_size(_loraDiagnosticLogPanel,
                    log_visual_area.x2 - log_visual_area.x1 + 1,
                    log_visual_area.y2 - log_visual_area.y1 + 1);
    lv_obj_set_style_bg_color(_loraDiagnosticLogPanel, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(_loraDiagnosticLogPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_loraDiagnosticLogPanel, 0, 0);
    lv_obj_set_style_radius(_loraDiagnosticLogPanel, 0, 0);
    lv_obj_set_style_pad_left(_loraDiagnosticLogPanel, 14, 0);
    lv_obj_set_style_pad_right(_loraDiagnosticLogPanel, 14, 0);
    lv_obj_set_style_pad_top(_loraDiagnosticLogPanel, 10, 0);
    lv_obj_set_style_pad_bottom(_loraDiagnosticLogPanel, 10, 0);
    lv_obj_set_style_pad_row(_loraDiagnosticLogPanel, 10, 0);
    lv_obj_set_flex_flow(_loraDiagnosticLogPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_loraDiagnosticLogPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(_loraDiagnosticLogPanel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *logHeader = lv_obj_create(_loraDiagnosticLogPanel);
    lv_obj_set_width(logHeader, lv_pct(100));
    lv_obj_set_height(logHeader, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(logHeader, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(logHeader, 0, 0);
    lv_obj_set_style_pad_all(logHeader, 0, 0);
    lv_obj_clear_flag(logHeader, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *logTitle = lv_label_create(logHeader);
    lv_label_set_text(logTitle, "E22 Module Log");
    lv_obj_set_style_text_font(logTitle, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(logTitle, lv_color_hex(0xE2E8F0), 0);
    lv_obj_align(logTitle, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *logCloseButton = lv_btn_create(logHeader);
    lv_obj_set_size(logCloseButton, 96, 38);
    lv_obj_align(logCloseButton, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(logCloseButton, 12, 0);
    lv_obj_set_style_bg_color(logCloseButton, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(logCloseButton, 0, 0);
    lv_obj_add_event_cb(logCloseButton, onLoRaDiagnosticLogCloseEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *logCloseLabel = lv_label_create(logCloseButton);
    lv_label_set_text(logCloseLabel, "Close");
    lv_obj_set_style_text_color(logCloseLabel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_center(logCloseLabel);

    _loraDiagnosticCommandTextArea = lv_textarea_create(_loraDiagnosticLogPanel);
    lv_obj_set_size(_loraDiagnosticCommandTextArea, lv_pct(100), 52);
    lv_textarea_set_one_line(_loraDiagnosticCommandTextArea, true);
    lv_textarea_set_max_length(_loraDiagnosticCommandTextArea, 191);
    lv_textarea_set_placeholder_text(_loraDiagnosticCommandTextArea, "Enter E22 hex bytes, for example: C1 00 09");
    lv_obj_set_style_radius(_loraDiagnosticCommandTextArea, 14, 0);
    lv_obj_set_style_border_width(_loraDiagnosticCommandTextArea, 1, 0);
    lv_obj_set_style_border_color(_loraDiagnosticCommandTextArea, lv_color_hex(0x334155), 0);
    lv_obj_set_style_bg_color(_loraDiagnosticCommandTextArea, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_text_color(_loraDiagnosticCommandTextArea, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_color(_loraDiagnosticCommandTextArea, lv_color_hex(0x94A3B8), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_pad_left(_loraDiagnosticCommandTextArea, 14, 0);
    lv_obj_set_style_pad_right(_loraDiagnosticCommandTextArea, 52, 0);
    lv_obj_add_event_cb(_loraDiagnosticCommandTextArea, onLoRaTextAreaEventCallback, LV_EVENT_ALL, this);

    _loraDiagnosticCommandSendButton = lv_btn_create(_loraDiagnosticCommandTextArea);
    lv_obj_set_size(_loraDiagnosticCommandSendButton, 40, 40);
    lv_obj_align(_loraDiagnosticCommandSendButton, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_radius(_loraDiagnosticCommandSendButton, 12, 0);
    lv_obj_set_style_bg_color(_loraDiagnosticCommandSendButton, lv_color_hex(0x0EA5E9), 0);
    lv_obj_set_style_border_width(_loraDiagnosticCommandSendButton, 0, 0);
    lv_obj_add_event_cb(_loraDiagnosticCommandSendButton, onLoRaDiagnosticCommandSendClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *commandSendLabel = lv_label_create(_loraDiagnosticCommandSendButton);
    lv_label_set_text(commandSendLabel, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(commandSendLabel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_center(commandSendLabel);

    _loraDiagnosticPresetDropdown = lv_dropdown_create(_loraDiagnosticLogPanel);
    lv_obj_set_width(_loraDiagnosticPresetDropdown, lv_pct(100));
    lv_dropdown_set_options(_loraDiagnosticPresetDropdown, kLoraE22DiagnosticPresetOptionsText);
    lv_dropdown_set_selected(_loraDiagnosticPresetDropdown, 0);
    lv_obj_set_style_radius(_loraDiagnosticPresetDropdown, 14, 0);
    lv_obj_set_style_bg_color(_loraDiagnosticPresetDropdown, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_border_color(_loraDiagnosticPresetDropdown, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(_loraDiagnosticPresetDropdown, 1, 0);
    lv_obj_set_style_text_color(_loraDiagnosticPresetDropdown, lv_color_hex(0xE2E8F0), 0);
    lv_obj_add_event_cb(_loraDiagnosticPresetDropdown, onLoRaDiagnosticPresetChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

    _loraDiagnosticLogScroll = lv_obj_create(_loraDiagnosticLogPanel);
    lv_obj_set_width(_loraDiagnosticLogScroll, lv_pct(100));
    lv_obj_set_height(_loraDiagnosticLogScroll, 0);
    lv_obj_set_flex_grow(_loraDiagnosticLogScroll, 1);
    lv_obj_set_style_bg_color(_loraDiagnosticLogScroll, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(_loraDiagnosticLogScroll, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_loraDiagnosticLogScroll, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_width(_loraDiagnosticLogScroll, 1, 0);
    lv_obj_set_style_radius(_loraDiagnosticLogScroll, 14, 0);
    lv_obj_set_style_pad_all(_loraDiagnosticLogScroll, 12, 0);
    lv_obj_set_scroll_dir(_loraDiagnosticLogScroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_loraDiagnosticLogScroll, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(_loraDiagnosticLogScroll, lv_color_hex(0x7AA2E3), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(_loraDiagnosticLogScroll, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(_loraDiagnosticLogScroll, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(_loraDiagnosticLogScroll, LV_RADIUS_CIRCLE, LV_PART_SCROLLBAR);

    _loraDiagnosticLogLabel = lv_label_create(_loraDiagnosticLogScroll);
    lv_obj_set_width(_loraDiagnosticLogLabel, lv_pct(100));
    lv_label_set_long_mode(_loraDiagnosticLogLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_loraDiagnosticLogLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_loraDiagnosticLogLabel, lv_color_hex(0xBFDBFE), 0);
    lv_label_set_text(_loraDiagnosticLogLabel, "No module log yet.");

    _screen_list[UI_LORA_SETTING_INDEX] = _loraScreen;
    lv_obj_add_event_cb(_loraScreen, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
#endif
}

void AppSettings::refreshLoRaUi(void)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    return;
#else
    if ((_loraScreen == nullptr) || !lv_obj_ready(_loraScreen)) {
        return;
    }

    jc4880::lora_mesh::StoredState state = {};
    jc4880::lora_mesh::load_stored_state(state);
    const bool local_controller_active = is_local_controller_backend_active();
    const bool radio_enabled = state.settings.radio_enabled;
    const auto module = state.settings.radio_module;
    const bool show_e22_uart_settings = lora_module_uses_e22_uart_settings(module);

    auto set_disabled = [](lv_obj_t *object, bool disabled) {
        if ((object == nullptr) || !lv_obj_ready(object)) {
            return;
        }
        if (disabled) {
            lv_obj_add_state(object, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(object, LV_STATE_DISABLED);
        }
    };

    if (radio_enabled) {
        lv_obj_add_state(_loraEnabledSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(_loraEnabledSwitch, LV_STATE_CHECKED);
    }
    set_disabled(_loraEnabledSwitch, false);

    lv_textarea_set_text(_loraDisplayNameTextArea, state.identity.display_name.c_str());
    lv_textarea_set_text(_loraCommonChatTitleTextArea, state.settings.common_chat_name.c_str());
    lv_dropdown_set_selected(_loraModuleDropdown, lora_dropdown_index_from_radio_module(module));
    lv_textarea_set_text(_loraFrequencyTextArea, std::to_string(state.settings.frequency_hz).c_str());
    lv_textarea_set_text(_loraSpreadingFactorTextArea, std::to_string(state.settings.spreading_factor).c_str());
    lv_textarea_set_text(_loraBandwidthTextArea, std::to_string(state.settings.bandwidth).c_str());
    lv_textarea_set_text(_loraCodingRateTextArea, std::to_string(state.settings.coding_rate).c_str());
    lv_textarea_set_text(_loraHopLimitTextArea, std::to_string(state.settings.hop_limit).c_str());
    if ((_loraE22AddressTextArea != nullptr) && lv_obj_ready(_loraE22AddressTextArea)) {
        lv_textarea_set_text(_loraE22AddressTextArea, std::to_string(state.settings.e22_address).c_str());
    }
    if ((_loraE22UartBaudDropdown != nullptr) && lv_obj_ready(_loraE22UartBaudDropdown)) {
        lv_dropdown_set_selected(_loraE22UartBaudDropdown, state.settings.e22_uart_baud_index);
    }
    if ((_loraE22AirDataRateDropdown != nullptr) && lv_obj_ready(_loraE22AirDataRateDropdown)) {
        lv_dropdown_set_selected(_loraE22AirDataRateDropdown, state.settings.e22_air_data_rate_index);
    }
    if ((_loraE22SubPacketDropdown != nullptr) && lv_obj_ready(_loraE22SubPacketDropdown)) {
        lv_dropdown_set_selected(_loraE22SubPacketDropdown, state.settings.e22_sub_packet_index);
    }
    if ((_loraE22PowerDropdown != nullptr) && lv_obj_ready(_loraE22PowerDropdown)) {
        lv_dropdown_set_selected(_loraE22PowerDropdown, state.settings.e22_tx_power_index);
    }
    if ((_loraE22TransmissionDropdown != nullptr) && lv_obj_ready(_loraE22TransmissionDropdown)) {
        lv_dropdown_set_selected(_loraE22TransmissionDropdown, state.settings.e22_fixed_transmission ? 1 : 0);
    }
    if ((_loraE22RssiDropdown != nullptr) && lv_obj_ready(_loraE22RssiDropdown)) {
        lv_dropdown_set_selected(_loraE22RssiDropdown, state.settings.e22_rssi_ambient_noise ? 1 : 0);
    }

    if (state.settings.forwarding_enabled) {
        lv_obj_add_state(_loraForwardingSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(_loraForwardingSwitch, LV_STATE_CHECKED);
    }
    if (state.settings.public_chat_encryption) {
        lv_obj_add_state(_loraEncryptionSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(_loraEncryptionSwitch, LV_STATE_CHECKED);
    }

    for (size_t role_index = 0; role_index < kLoraPinRoleCount; ++role_index) {
        const auto role = static_cast<LoraPinRole>(role_index);
        const bool visible = lora_module_uses_role(module, role);
        if ((_loraPinRows[role_index] != nullptr) && lv_obj_ready(_loraPinRows[role_index])) {
            if (visible) {
                lv_obj_clear_flag(_loraPinRows[role_index], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(_loraPinRows[role_index], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if ((_loraPinDropdowns[role_index] != nullptr) && lv_obj_ready(_loraPinDropdowns[role_index])) {
            lv_dropdown_set_selected(_loraPinDropdowns[role_index],
                                     lora_gpio_choice_index(lora_pin_value_for_role(state.settings, role)));
            set_disabled(_loraPinDropdowns[role_index], local_controller_active);
        }
    }

    set_hidden_if_ready(_loraSpreadingFactorRow, show_e22_uart_settings);
    set_hidden_if_ready(_loraBandwidthRow, show_e22_uart_settings);
    set_hidden_if_ready(_loraCodingRateRow, show_e22_uart_settings);
    set_hidden_if_ready(_loraE22Section, !show_e22_uart_settings);

    set_disabled(_loraModuleDropdown, false);
    set_disabled(_loraDisplayNameTextArea, false);
    set_disabled(_loraCommonChatTitleTextArea, false);
    set_disabled(_loraFrequencyTextArea, false);
    set_disabled(_loraSpreadingFactorTextArea, false);
    set_disabled(_loraBandwidthTextArea, false);
    set_disabled(_loraCodingRateTextArea, false);
    set_disabled(_loraHopLimitTextArea, false);
    set_disabled(_loraE22AddressTextArea, !show_e22_uart_settings);
    set_disabled(_loraE22UartBaudDropdown, !show_e22_uart_settings);
    set_disabled(_loraE22AirDataRateDropdown, !show_e22_uart_settings);
    set_disabled(_loraE22SubPacketDropdown, !show_e22_uart_settings);
    set_disabled(_loraE22PowerDropdown, !show_e22_uart_settings);
    set_disabled(_loraE22TransmissionDropdown, !show_e22_uart_settings);
    set_disabled(_loraE22RssiDropdown, !show_e22_uart_settings);
    set_disabled(_loraForwardingSwitch, false);
    set_disabled(_loraEncryptionSwitch, false);
    set_disabled(_loraSelfCheckButton, !radio_enabled);
    set_disabled(_loraAutoDetectButton, !radio_enabled);
    set_disabled(_loraDiagnosticLogButton, false);
    refreshLoRaSelfCheckStatus();
    refreshLoRaDiagnosticLogModal();

    if (local_controller_active) {
        lv_label_set_text(_loraInfoLabel,
                          radio_enabled
                              ? "LoRa radio is enabled. Local Controller has been turned off to avoid conflicts with haptics and Neopixel on shared GPIOs."
                              : "Local Controller is active. Turning LoRa on will disable Local Controller automatically to avoid GPIO conflicts.");
    } else if (show_e22_uart_settings) {
        lv_label_set_text(_loraInfoLabel,
                          state.settings.e22_fixed_transmission
                              ? "E22-400T22S is using fixed broadcast mode. Self Check programs the module first, then verifies the UART link with the saved channel and address."
                              : "E22-400T22S is using transparent mode. Self Check programs the module first, then verifies the UART link with the saved UART and channel settings. Open Diagnostics to inspect each UART command and response.");
    } else if (is_imu_enabled()) {
        lv_label_set_text(_loraInfoLabel,
                          "IMU is active. Turning LoRa on will disable IMU because the default BMI160 wiring overlaps the E22 JP1 pin profile.");
    } else if (!state.settings.radio_enabled) {
        lv_label_set_text(_loraInfoLabel,
                          "LoRa radio is disabled. Mesh settings stay saved so you can re-enable it later.");
    } else {
        lv_label_set_text(_loraInfoLabel,
                          "LoRa radio is enabled. Reopen the LoRa Mesh app after changing pins or modulation so it reinitializes the radio.");
    }
#endif
}

bool AppSettings::persistLoRaConfigFromUi(void)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    return false;
#else
    jc4880::lora_mesh::StoredState state = {};
    if (!jc4880::lora_mesh::load_stored_state(state)) {
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel, "Failed to load LoRa settings.");
        }
        return false;
    }

    auto parse_uint = [](lv_obj_t *textarea, uint32_t min_value, uint32_t max_value, uint32_t *value_out) {
        if ((textarea == nullptr) || (value_out == nullptr)) {
            return false;
        }
        const char *text = lv_textarea_get_text(textarea);
        if ((text == nullptr) || (*text == '\0')) {
            return false;
        }
        char *end = nullptr;
        const unsigned long parsed = strtoul(text, &end, 10);
        if ((end == text) || (*end != '\0') || (parsed < min_value) || (parsed > max_value)) {
            return false;
        }
        *value_out = static_cast<uint32_t>(parsed);
        return true;
    };

    uint32_t frequency_hz = 0;
    uint32_t spreading_factor = 0;
    uint32_t bandwidth = 0;
    uint32_t coding_rate = 0;
    uint32_t hop_limit = 0;
    uint32_t e22_address = 0;
    if (!parse_uint(_loraFrequencyTextArea, 400000000U, 1000000000U, &frequency_hz) ||
        !parse_uint(_loraSpreadingFactorTextArea, 5U, 12U, &spreading_factor) ||
        !parse_uint(_loraBandwidthTextArea, 0U, 9U, &bandwidth) ||
        !parse_uint(_loraCodingRateTextArea, 1U, 4U, &coding_rate) ||
        !parse_uint(_loraHopLimitTextArea, 1U, 16U, &hop_limit)) {
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel, "LoRa settings contain invalid numeric values.");
        }
        return false;
    }

    bool local_controller_active = is_local_controller_backend_active();
    const auto module = lora_radio_module_from_dropdown(lv_dropdown_get_selected(_loraModuleDropdown));
    const bool use_e22_uart_settings = lora_module_uses_e22_uart_settings(module);
    if (use_e22_uart_settings && !parse_uint(_loraE22AddressTextArea, 0U, 65535U, &e22_address)) {
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel, "E22-400T22S address must be a number between 0 and 65535.");
        }
        return false;
    }
    if (lv_obj_has_state(_loraEnabledSwitch, LV_STATE_CHECKED) && local_controller_active) {
        if (!disableLocalControllerForLoRa()) {
            if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
                lv_label_set_text(_loraInfoLabel, "Failed to disable Local Controller before enabling LoRa.");
            }
            return false;
        }
        local_controller_active = false;
    }
    state.identity.display_name = lv_textarea_get_text(_loraDisplayNameTextArea);
    state.settings.common_chat_name = lv_textarea_get_text(_loraCommonChatTitleTextArea);
    if (state.identity.display_name.empty()) {
        state.identity.display_name = std::string("P4-") + state.identity.device_id.substr(0, 4);
    }
    if (state.settings.common_chat_name.empty()) {
        state.settings.common_chat_name = "Common Mesh Chat";
    }
    state.settings.radio_enabled = !local_controller_active && lv_obj_has_state(_loraEnabledSwitch, LV_STATE_CHECKED);
    state.settings.radio_module = module;
    state.settings.frequency_hz = frequency_hz;
    state.settings.spreading_factor = static_cast<uint8_t>(spreading_factor);
    state.settings.bandwidth = static_cast<uint8_t>(bandwidth);
    state.settings.coding_rate = static_cast<uint8_t>(coding_rate);
    state.settings.hop_limit = static_cast<uint8_t>(hop_limit);
    state.settings.forwarding_enabled = lv_obj_has_state(_loraForwardingSwitch, LV_STATE_CHECKED);
    state.settings.public_chat_encryption = lv_obj_has_state(_loraEncryptionSwitch, LV_STATE_CHECKED);
    if (use_e22_uart_settings) {
        state.settings.e22_address = static_cast<uint16_t>(e22_address);
        state.settings.e22_uart_baud_index = static_cast<uint8_t>(lv_dropdown_get_selected(_loraE22UartBaudDropdown));
        state.settings.e22_air_data_rate_index = static_cast<uint8_t>(lv_dropdown_get_selected(_loraE22AirDataRateDropdown));
        state.settings.e22_sub_packet_index = static_cast<uint8_t>(lv_dropdown_get_selected(_loraE22SubPacketDropdown));
        state.settings.e22_tx_power_index = static_cast<uint8_t>(lv_dropdown_get_selected(_loraE22PowerDropdown));
        state.settings.e22_fixed_transmission = lv_dropdown_get_selected(_loraE22TransmissionDropdown) == 1;
        state.settings.e22_rssi_ambient_noise = lv_dropdown_get_selected(_loraE22RssiDropdown) == 1;
    }

    std::vector<int8_t> active_pins;
    active_pins.reserve(kLoraPinRoleCount);
    for (size_t role_index = 0; role_index < kLoraPinRoleCount; ++role_index) {
        const auto role = static_cast<LoraPinRole>(role_index);
        const int8_t value = lora_gpio_choice_value(lv_dropdown_get_selected(_loraPinDropdowns[role_index]));
        lora_set_pin_value_for_role(state.settings, role, value);
        if (!lora_module_uses_role(module, role)) {
            continue;
        }
        if (value < 0) {
            if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
                lv_label_set_text(_loraInfoLabel, "Each active LoRa signal must have a GPIO assigned.");
            }
            return false;
        }
        if (std::find(active_pins.begin(), active_pins.end(), value) != active_pins.end()) {
            if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
                lv_label_set_text(_loraInfoLabel, "Each visible LoRa signal must use a unique GPIO.");
            }
            return false;
        }
        active_pins.push_back(value);
    }

    if (!jc4880::lora_mesh::save_stored_state(state)) {
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel, "Failed to save LoRa settings.");
        }
        return false;
    }

    LoRaMeshApp *lora_app = find_installed_lora_mesh_app(getCore());
    const bool runtime_synced = (lora_app != nullptr) && lora_app->syncRuntimeStateFromStorage();

    refreshLoRaUi();
    if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
        lv_label_set_text(_loraInfoLabel,
                          local_controller_active
                              ? "Saved. Local Controller is active, so the LoRa radio remains forced off."
                              : (runtime_synced
                                     ? "Saved. The open LoRa Mesh app picked up the new chat labels immediately."
                                     : "Saved. Reopen the LoRa Mesh app if it is already open so the radio picks up these settings."));
    }
    return true;
#endif
}

bool AppSettings::persistLoRaRadioEnabledFromUi(void)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    return false;
#else
    jc4880::lora_mesh::StoredState state = {};
    if (!jc4880::lora_mesh::load_stored_state(state)) {
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel, "Failed to load LoRa settings.");
        }
        return false;
    }

    bool local_controller_active = is_local_controller_backend_active();
    const bool radio_enabled = lv_obj_has_state(_loraEnabledSwitch, LV_STATE_CHECKED);
    if (radio_enabled && local_controller_active) {
        if (!disableLocalControllerForLoRa()) {
            if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
                lv_label_set_text(_loraInfoLabel, "Failed to disable Local Controller before enabling LoRa.");
            }
            return false;
        }
        local_controller_active = false;
    }
    if (radio_enabled && is_imu_enabled() && !disableImuForLoRa()) {
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel, "Failed to disable IMU before enabling LoRa.");
        }
        return false;
    }

    state.settings.radio_enabled = radio_enabled && !local_controller_active;
    if (!jc4880::lora_mesh::save_stored_state(state)) {
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel, "Failed to save LoRa settings.");
        }
        return false;
    }

    if ((_loraSelfCheckButton != nullptr) && lv_obj_ready(_loraSelfCheckButton)) {
        if (state.settings.radio_enabled) {
            lv_obj_clear_state(_loraSelfCheckButton, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(_loraSelfCheckButton, LV_STATE_DISABLED);
        }
    }
    if ((_loraAutoDetectButton != nullptr) && lv_obj_ready(_loraAutoDetectButton)) {
        if (state.settings.radio_enabled) {
            lv_obj_clear_state(_loraAutoDetectButton, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(_loraAutoDetectButton, LV_STATE_DISABLED);
        }
    }
    refreshLoRaSelfCheckStatus();

    if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
        if (state.settings.radio_enabled) {
            lv_label_set_text(_loraInfoLabel,
                              "LoRa radio enabled. IMU and Local Controller are forced off while LoRa owns its JP1 pins. Other LoRa settings still require Save if you change them.");
        } else {
            lv_label_set_text(_loraInfoLabel,
                              "LoRa radio disabled. Other LoRa settings stay saved until you change them and press Save.");
        }
    }

    return true;
#endif
}

bool AppSettings::startLoRaDiagnosticFromUi(bool allow_autodetect)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    (void)allow_autodetect;
    return false;
#else
    if (!persistLoRaConfigFromUi()) {
        if ((_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(_loraSelfCheckStatusLabel)) {
            lv_label_set_text(_loraSelfCheckStatusLabel,
                              allow_autodetect
                                  ? "Auto detect unavailable because the current LoRa settings could not be saved."
                                  : "Self check unavailable because the current LoRa settings could not be saved.");
        }
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel,
                              allow_autodetect
                                  ? "Fix the current LoRa settings and save them before running auto detect."
                                  : "Fix the current LoRa settings and save them before running self check.");
        }
        return false;
    }

    jc4880::lora_mesh::StoredState state = {};
    if (!jc4880::lora_mesh::load_stored_state(state)) {
        if ((_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(_loraSelfCheckStatusLabel)) {
            lv_label_set_text(_loraSelfCheckStatusLabel,
                              allow_autodetect
                                  ? "Auto detect unavailable because LoRa settings could not be loaded."
                                  : "Self check unavailable because LoRa settings could not be loaded.");
        }
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel,
                              allow_autodetect
                                  ? "Failed to load LoRa settings before starting auto detect."
                                  : "Failed to load LoRa settings before starting self check.");
        }
        return false;
    }
    if (!state.settings.radio_enabled) {
        if ((_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(_loraSelfCheckStatusLabel)) {
            lv_label_set_text(_loraSelfCheckStatusLabel,
                              allow_autodetect
                                  ? "Auto detect unavailable while the LoRa radio is disabled."
                                  : "Self check unavailable while the LoRa radio is disabled.");
        }
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel, "Device disabled. Please enable radio in Device Settings.");
        }
        return false;
    }

    LoRaMeshApp *lora_app = find_installed_lora_mesh_app(getCore());
    if (lora_app == nullptr) {
        if ((_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(_loraSelfCheckStatusLabel)) {
            lv_label_set_text(_loraSelfCheckStatusLabel,
                              allow_autodetect
                                  ? "Auto detect unavailable because the LoRa app is not installed."
                                  : "Self check unavailable because the LoRa app is not installed.");
        }
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel, "LoRa Mesh app is not installed.");
        }
        return false;
    }

    _loraRefreshUiFromStoredStateAfterSelfCheck = allow_autodetect;
    const bool started = allow_autodetect ? lora_app->startAutoDetectFromSettings() : lora_app->startSelfTestFromSettings();
    if (!started) {
        _loraRefreshUiFromStoredStateAfterSelfCheck = false;
        if ((_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(_loraSelfCheckStatusLabel)) {
            lv_label_set_text(_loraSelfCheckStatusLabel,
                              allow_autodetect ? "Failed to start LoRa auto detect." : "Failed to start LoRa self check.");
        }
        if ((_loraInfoLabel != nullptr) && lv_obj_ready(_loraInfoLabel)) {
            lv_label_set_text(_loraInfoLabel,
                              allow_autodetect ? "Failed to start LoRa auto detect." : "Failed to start LoRa self check.");
        }
        return false;
    }

    if ((_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(_loraSelfCheckStatusLabel)) {
        lv_label_set_text(_loraSelfCheckStatusLabel,
                          allow_autodetect ? "LoRa auto detect starting..." : "LoRa self check starting...");
    }
    setLoRaDiagnosticLogModalVisible(true);
    startLoRaSelfCheckStatusPolling();
    return true;
#endif
}

bool AppSettings::sendLoRaDiagnosticCommandFromUi(const std::string &command)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    (void)command;
    return false;
#else
    const std::string trimmed = trim_copy(command);
    if (trimmed.empty()) {
        if ((_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(_loraSelfCheckStatusLabel)) {
            lv_label_set_text(_loraSelfCheckStatusLabel, "Enter an E22 hex command or choose a preset first.");
        }
        return false;
    }

    LoRaMeshApp *lora_app = find_installed_lora_mesh_app(getCore());
    if (lora_app == nullptr) {
        if ((_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(_loraSelfCheckStatusLabel)) {
            lv_label_set_text(_loraSelfCheckStatusLabel, "Module commands unavailable because the LoRa app is not installed.");
        }
        return false;
    }

    if (!lora_app->debugSendModuleCommandVisible(trimmed)) {
        if ((_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(_loraSelfCheckStatusLabel)) {
            lv_label_set_text(_loraSelfCheckStatusLabel, "Failed to start the module command. Wait for the current diagnostic to finish.");
        }
        refreshLoRaDiagnosticLogModal();
        return false;
    }

    if ((_loraSelfCheckStatusLabel != nullptr) && lv_obj_ready(_loraSelfCheckStatusLabel)) {
        lv_label_set_text(_loraSelfCheckStatusLabel, "Module command running...");
    }
    setLoRaDiagnosticLogModalVisible(true);
    startLoRaSelfCheckStatusPolling();
    return true;
#endif
}

void AppSettings::setLoRaDiagnosticLogModalVisible(bool visible)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    (void)visible;
    return;
#else
    if ((_loraDiagnosticLogOverlay == nullptr) || !lv_obj_ready(_loraDiagnosticLogOverlay)) {
        return;
    }
    if (visible) {
        LoRaMeshApp *lora_app = find_installed_lora_mesh_app(getCore());
        _loraDiagnosticLogSessionStartLine = (lora_app != nullptr) ? lora_app->debugLogLineCount() : 0;
        _loraDiagnosticLogLastRenderedLineCount = 0;
        if ((_loraDiagnosticLogLabel != nullptr) && lv_obj_ready(_loraDiagnosticLogLabel)) {
            lv_label_set_text(_loraDiagnosticLogLabel, "No module log yet.");
        }
        if ((_loraDiagnosticLogScroll != nullptr) && lv_obj_ready(_loraDiagnosticLogScroll)) {
            lv_obj_scroll_to_y(_loraDiagnosticLogScroll, 0, LV_ANIM_OFF);
        }
        refreshLoRaDiagnosticLogModal();
        lv_obj_clear_flag(_loraDiagnosticLogOverlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_loraDiagnosticLogOverlay);
    } else {
        lv_obj_add_flag(_loraDiagnosticLogOverlay, LV_OBJ_FLAG_HIDDEN);
        setLoRaKeyboardVisible(false);

        LoRaMeshApp *lora_app = find_installed_lora_mesh_app(getCore());
        bool self_test_running = false;
        if (lora_app != nullptr) {
            (void)lora_app->getSelfTestStatus(&self_test_running, nullptr);
        }
        if (!self_test_running) {
            stopLoRaSelfCheckStatusPolling();
        }
    }
#endif
}

void AppSettings::refreshLoRaDiagnosticLogModal(void)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    return;
#else
    if ((_loraDiagnosticLogLabel == nullptr) || !lv_obj_ready(_loraDiagnosticLogLabel)) {
        return;
    }

    LoRaMeshApp *lora_app = find_installed_lora_mesh_app(getCore());
    if (lora_app == nullptr) {
        lv_label_set_text(_loraDiagnosticLogLabel, "LoRa Mesh app is not installed.");
        _loraDiagnosticLogLastRenderedLineCount = 0;
        return;
    }

    const size_t total_line_count = lora_app->debugLogLineCount();
    if (total_line_count < _loraDiagnosticLogSessionStartLine) {
        _loraDiagnosticLogSessionStartLine = total_line_count;
    }
    if (total_line_count == _loraDiagnosticLogLastRenderedLineCount) {
        return;
    }

    const bool auto_scroll = (_loraDiagnosticLogScroll == nullptr) || !lv_obj_ready(_loraDiagnosticLogScroll) ||
                             (lv_obj_get_scroll_bottom(_loraDiagnosticLogScroll) <= 12);
    const lv_coord_t previous_scroll_y = ((_loraDiagnosticLogScroll != nullptr) && lv_obj_ready(_loraDiagnosticLogScroll))
                                             ? lv_obj_get_scroll_y(_loraDiagnosticLogScroll)
                                             : 0;

    const std::vector<std::string> lines = lora_app->debugLogLinesSince(_loraDiagnosticLogSessionStartLine, 160);
    if (lines.empty()) {
        lv_label_set_text(_loraDiagnosticLogLabel, "No module log yet.");
        _loraDiagnosticLogLastRenderedLineCount = total_line_count;
        return;
    }

    std::string combined;
    for (const std::string &line : lines) {
        if (!combined.empty()) {
            combined.push_back('\n');
        }
        combined += line;
    }
    lv_label_set_text(_loraDiagnosticLogLabel, combined.c_str());
    if ((_loraDiagnosticLogScroll != nullptr) && lv_obj_ready(_loraDiagnosticLogScroll)) {
        if (auto_scroll) {
            lv_obj_scroll_to_y(_loraDiagnosticLogScroll, LV_COORD_MAX, LV_ANIM_OFF);
        } else {
            lv_obj_scroll_to_y(_loraDiagnosticLogScroll, previous_scroll_y, LV_ANIM_OFF);
        }
    }
    _loraDiagnosticLogLastRenderedLineCount = total_line_count;
#endif
}

void AppSettings::disableLoRaRadioForLocalController(void)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    return;
#else
    jc4880::lora_mesh::StoredState state = {};
    if (!jc4880::lora_mesh::load_stored_state(state)) {
        return;
    }
    if (state.settings.radio_enabled) {
        state.settings.radio_enabled = false;
        jc4880::lora_mesh::save_stored_state(state);
    }
    refreshLoRaUi();
#endif
}

bool AppSettings::disableLocalControllerForLoRa(void)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    return false;
#else
    jc4880_joypad_config_t config = {};
    if (!jc4880_joypad_get_config(&config)) {
        return false;
    }
    if (config.backend != JC4880_JOYPAD_BACKEND_MANUAL) {
        return true;
    }

    config.backend = JC4880_JOYPAD_BACKEND_DISABLED;
    if (!jc4880_joypad_set_config(&config)) {
        return false;
    }

    if ((_joypadManualActiveSwitch != nullptr) && lv_obj_ready(_joypadManualActiveSwitch)) {
        lv_obj_clear_state(_joypadManualActiveSwitch, LV_STATE_CHECKED);
    }
    applyNeopixelConfig();
    refreshJoypadUi();
    return true;
#endif
}

void AppSettings::refreshLoRaSelfCheckStatus(void)
{
#if !CONFIG_JC4880_APP_LORA_MESH
    return;
#else
    if ((_loraSelfCheckStatusLabel == nullptr) || !lv_obj_ready(_loraSelfCheckStatusLabel)) {
        return;
    }

    jc4880::lora_mesh::StoredState state = {};
    jc4880::lora_mesh::load_stored_state(state);
    if (!state.settings.radio_enabled) {
        lv_label_set_text(_loraSelfCheckStatusLabel, "Self check unavailable while the LoRa radio is disabled.");
        stopLoRaSelfCheckStatusPolling();
        return;
    }

    LoRaMeshApp *lora_app = find_installed_lora_mesh_app(getCore());
    if (lora_app == nullptr) {
        lv_label_set_text(_loraSelfCheckStatusLabel, "Self check unavailable because the LoRa app is not installed.");
        stopLoRaSelfCheckStatusPolling();
        return;
    }

    bool self_test_running = false;
    bool self_test_ran = false;
    const std::string self_test_status = lora_app->getSelfTestStatus(&self_test_running, &self_test_ran);
    if (self_test_running) {
        lv_label_set_text(_loraSelfCheckStatusLabel, self_test_status.c_str());
        refreshLoRaDiagnosticLogModal();
        startLoRaSelfCheckStatusPolling();
        return;
    }
    if (self_test_ran && !self_test_status.empty() && (self_test_status != "Mode: idle")) {
        if (_loraRefreshUiFromStoredStateAfterSelfCheck) {
            _loraRefreshUiFromStoredStateAfterSelfCheck = false;
            refreshLoRaUi();
            return;
        }
        lv_label_set_text(_loraSelfCheckStatusLabel, self_test_status.c_str());
        refreshLoRaDiagnosticLogModal();
        stopLoRaSelfCheckStatusPolling();
        return;
    }

    lv_label_set_text(_loraSelfCheckStatusLabel, "Diagnostics ready. Self Check uses saved pins; Auto Detect also tries UART pin recovery.");
    refreshLoRaDiagnosticLogModal();
    if ((_loraDiagnosticLogOverlay != nullptr) && lv_obj_ready(_loraDiagnosticLogOverlay) &&
        !lv_obj_has_flag(_loraDiagnosticLogOverlay, LV_OBJ_FLAG_HIDDEN)) {
        startLoRaSelfCheckStatusPolling();
        return;
    }
    stopLoRaSelfCheckStatusPolling();
#endif
}

void AppSettings::startLoRaSelfCheckStatusPolling(void)
{
    if (_loraSelfCheckStatusTimer != nullptr) {
        return;
    }

    _loraSelfCheckStatusTimer = lv_timer_create(onLoRaSelfCheckStatusTimerCallback, 250, this);
}

void AppSettings::stopLoRaSelfCheckStatusPolling(void)
{
    if (_loraSelfCheckStatusTimer != nullptr) {
        lv_timer_del(_loraSelfCheckStatusTimer);
        _loraSelfCheckStatusTimer = nullptr;
    }
}