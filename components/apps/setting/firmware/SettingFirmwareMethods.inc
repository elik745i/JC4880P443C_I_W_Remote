void AppSettings::setFirmwareStatus(const std::string &status, bool is_error)
{
    if (lv_obj_ready(_otaUpdateProgressStatusLabel)) {
        lv_label_set_text(_otaUpdateProgressStatusLabel, status.c_str());
        lv_obj_set_style_text_color(_otaUpdateProgressStatusLabel, is_error ? lv_color_hex(0xB91C1C) : lv_color_hex(0x334155), 0);
    }

    if (!isUiActive() || !lv_obj_ready(_firmwareStatusLabel)) {
        return;
    }

    lv_label_set_text(_firmwareStatusLabel, status.c_str());
    lv_obj_set_style_text_color(_firmwareStatusLabel, is_error ? lv_color_hex(0xB91C1C) : lv_color_hex(0x334155), 0);
}

void AppSettings::ensureFirmwareOtaCheckOverlay(void)
{
    if ((_firmwareOtaCheckOverlay != nullptr) && lv_obj_ready(_firmwareOtaCheckOverlay)) {
        return;
    }

    _firmwareOtaCheckOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_firmwareOtaCheckOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(_firmwareOtaCheckOverlay, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(_firmwareOtaCheckOverlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_firmwareOtaCheckOverlay, 0, 0);
    lv_obj_set_style_pad_all(_firmwareOtaCheckOverlay, 0, 0);
    lv_obj_add_flag(_firmwareOtaCheckOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_firmwareOtaCheckOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(_firmwareOtaCheckOverlay);
    lv_obj_add_flag(_firmwareOtaCheckOverlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *firmwareOtaCheckCard = lv_obj_create(_firmwareOtaCheckOverlay);
    lv_obj_set_size(firmwareOtaCheckCard, 320, 210);
    lv_obj_center(firmwareOtaCheckCard);
    lv_obj_set_style_radius(firmwareOtaCheckCard, 24, 0);
    lv_obj_set_style_bg_color(firmwareOtaCheckCard, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_border_width(firmwareOtaCheckCard, 0, 0);
    lv_obj_set_style_pad_all(firmwareOtaCheckCard, 20, 0);
    lv_obj_set_style_pad_row(firmwareOtaCheckCard, 16, 0);
    lv_obj_clear_flag(firmwareOtaCheckCard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(firmwareOtaCheckCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(firmwareOtaCheckCard, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _firmwareOtaCheckSpinner = lv_spinner_create(firmwareOtaCheckCard, 1000, 90);
    lv_obj_set_size(_firmwareOtaCheckSpinner, 72, 72);

    _firmwareOtaCheckStatusLabel = lv_label_create(firmwareOtaCheckCard);
    lv_obj_set_width(_firmwareOtaCheckStatusLabel, lv_pct(100));
    lv_label_set_long_mode(_firmwareOtaCheckStatusLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_firmwareOtaCheckStatusLabel, "Checking GitHub for firmware releases...");
    lv_obj_set_style_text_align(_firmwareOtaCheckStatusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(_firmwareOtaCheckStatusLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(_firmwareOtaCheckStatusLabel, lv_color_hex(0x334155), 0);
}

void AppSettings::setFirmwareOtaCheckOverlayVisible(bool visible, const std::string &status)
{
    if (visible) {
        ensureFirmwareOtaCheckOverlay();
    }

    if ((_firmwareOtaCheckOverlay == nullptr) || !lv_obj_ready(_firmwareOtaCheckOverlay)) {
        return;
    }

    if ((_firmwareOtaCheckStatusLabel != nullptr) && lv_obj_ready(_firmwareOtaCheckStatusLabel) && !status.empty()) {
        lv_label_set_text(_firmwareOtaCheckStatusLabel, status.c_str());
    }

    if (visible) {
        lv_obj_move_foreground(_firmwareOtaCheckOverlay);
        lv_obj_clear_flag(_firmwareOtaCheckOverlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_firmwareOtaCheckOverlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void AppSettings::ensureOtaUpdateProgressOverlay(void)
{
    if ((_otaUpdateProgressOverlay != nullptr) && lv_obj_ready(_otaUpdateProgressOverlay)) {
        return;
    }

    _otaUpdateProgressOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_otaUpdateProgressOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(_otaUpdateProgressOverlay, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(_otaUpdateProgressOverlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_otaUpdateProgressOverlay, 0, 0);
    lv_obj_set_style_pad_all(_otaUpdateProgressOverlay, 0, 0);
    lv_obj_add_flag(_otaUpdateProgressOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_otaUpdateProgressOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_otaUpdateProgressOverlay, onOtaUpdateProgressCloseEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_center(_otaUpdateProgressOverlay);
    lv_obj_add_flag(_otaUpdateProgressOverlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *card = lv_obj_create(_otaUpdateProgressOverlay);
    lv_obj_set_size(card, 340, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_style_pad_row(card, 14, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _otaUpdateProgressCornerCloseButton = lv_btn_create(card);
    lv_obj_set_size(_otaUpdateProgressCornerCloseButton, 36, 36);
    lv_obj_align(_otaUpdateProgressCornerCloseButton, LV_ALIGN_TOP_RIGHT, 8, -8);
    lv_obj_set_style_radius(_otaUpdateProgressCornerCloseButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_otaUpdateProgressCornerCloseButton, 0, 0);
    lv_obj_set_style_bg_color(_otaUpdateProgressCornerCloseButton, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_bg_opa(_otaUpdateProgressCornerCloseButton, LV_OPA_COVER, 0);
    lv_obj_add_flag(_otaUpdateProgressCornerCloseButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_otaUpdateProgressCornerCloseButton, onOtaUpdateProgressCloseEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t *cornerCloseLabel = lv_label_create(_otaUpdateProgressCornerCloseButton);
    lv_label_set_text(cornerCloseLabel, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(cornerCloseLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(cornerCloseLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_center(cornerCloseLabel);

    lv_obj_t *titleLabel = lv_label_create(card);
    lv_label_set_text(titleLabel, "OTA update");
    lv_obj_set_width(titleLabel, lv_pct(100));
    lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x0F172A), 0);

    _otaUpdateProgressStatusLabel = lv_label_create(card);
    lv_obj_set_width(_otaUpdateProgressStatusLabel, lv_pct(100));
    lv_label_set_long_mode(_otaUpdateProgressStatusLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_otaUpdateProgressStatusLabel, "Preparing OTA update...");
    lv_obj_set_style_text_align(_otaUpdateProgressStatusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(_otaUpdateProgressStatusLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(_otaUpdateProgressStatusLabel, lv_color_hex(0x334155), 0);

    _otaUpdateProgressBar = lv_bar_create(card);
    lv_obj_set_width(_otaUpdateProgressBar, lv_pct(100));
    lv_obj_set_height(_otaUpdateProgressBar, 18);
    lv_bar_set_range(_otaUpdateProgressBar, 0, 100);
    lv_bar_set_value(_otaUpdateProgressBar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(_otaUpdateProgressBar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_otaUpdateProgressBar, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_otaUpdateProgressBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_otaUpdateProgressBar, lv_color_hex(0x2563EB), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_otaUpdateProgressBar, LV_OPA_COVER, LV_PART_INDICATOR);

    _otaUpdateProgressLabel = lv_label_create(card);
    lv_obj_set_width(_otaUpdateProgressLabel, lv_pct(100));
    lv_label_set_long_mode(_otaUpdateProgressLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_otaUpdateProgressLabel, "Preparing OTA update...");
    lv_obj_set_style_text_align(_otaUpdateProgressLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(_otaUpdateProgressLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_otaUpdateProgressLabel, lv_color_hex(0x64748B), 0);

    _otaUpdateProgressActionRow = lv_obj_create(card);
    lv_obj_set_width(_otaUpdateProgressActionRow, lv_pct(100));
    lv_obj_set_height(_otaUpdateProgressActionRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(_otaUpdateProgressActionRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(_otaUpdateProgressActionRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_otaUpdateProgressActionRow, 0, 0);
    lv_obj_set_style_pad_all(_otaUpdateProgressActionRow, 0, 0);
    lv_obj_set_style_pad_column(_otaUpdateProgressActionRow, 8, 0);
    lv_obj_set_style_pad_row(_otaUpdateProgressActionRow, 8, 0);
    lv_obj_set_flex_flow(_otaUpdateProgressActionRow, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_add_flag(_otaUpdateProgressActionRow, LV_OBJ_FLAG_HIDDEN);

    _otaUpdateProgressInstallButton = lv_btn_create(_otaUpdateProgressActionRow);
    lv_obj_set_size(_otaUpdateProgressInstallButton, 100, 46);
    lv_obj_set_style_radius(_otaUpdateProgressInstallButton, 16, 0);
    lv_obj_set_style_border_width(_otaUpdateProgressInstallButton, 0, 0);
    lv_obj_set_style_bg_color(_otaUpdateProgressInstallButton, lv_color_hex(0x2563EB), 0);
    lv_obj_add_event_cb(_otaUpdateProgressInstallButton, onOtaUpdateInstallNowEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *installLabel = lv_label_create(_otaUpdateProgressInstallButton);
    lv_label_set_text(installLabel, "Install Now");
    lv_obj_set_style_text_font(installLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(installLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(installLabel);

    _otaUpdateProgressRescheduleButton = lv_btn_create(_otaUpdateProgressActionRow);
    lv_obj_set_size(_otaUpdateProgressRescheduleButton, 108, 46);
    lv_obj_set_style_radius(_otaUpdateProgressRescheduleButton, 16, 0);
    lv_obj_set_style_border_width(_otaUpdateProgressRescheduleButton, 0, 0);
    lv_obj_set_style_bg_color(_otaUpdateProgressRescheduleButton, lv_color_hex(0xCBD5E1), 0);
    lv_obj_add_event_cb(_otaUpdateProgressRescheduleButton, onOtaUpdateRescheduleEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *rescheduleLabel = lv_label_create(_otaUpdateProgressRescheduleButton);
    lv_label_set_text(rescheduleLabel, "Reschedule");
    lv_obj_set_style_text_font(rescheduleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(rescheduleLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_center(rescheduleLabel);

    _otaUpdateProgressCancelButton = lv_btn_create(_otaUpdateProgressActionRow);
    lv_obj_set_size(_otaUpdateProgressCancelButton, 92, 46);
    lv_obj_set_style_radius(_otaUpdateProgressCancelButton, 16, 0);
    lv_obj_set_style_border_width(_otaUpdateProgressCancelButton, 0, 0);
    lv_obj_set_style_bg_color(_otaUpdateProgressCancelButton, lv_color_hex(0xE2E8F0), 0);
    lv_obj_add_event_cb(_otaUpdateProgressCancelButton, onOtaUpdateCancelEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *cancelLabel = lv_label_create(_otaUpdateProgressCancelButton);
    lv_label_set_text(cancelLabel, "Cancel");
    lv_obj_set_style_text_font(cancelLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(cancelLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_center(cancelLabel);

    _otaUpdateReschedulePanel = lv_obj_create(card);
    lv_obj_set_width(_otaUpdateReschedulePanel, lv_pct(100));
    lv_obj_set_height(_otaUpdateReschedulePanel, LV_SIZE_CONTENT);
    lv_obj_clear_flag(_otaUpdateReschedulePanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_otaUpdateReschedulePanel, 16, 0);
    lv_obj_set_style_bg_color(_otaUpdateReschedulePanel, lv_color_hex(0xEFF6FF), 0);
    lv_obj_set_style_bg_opa(_otaUpdateReschedulePanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_otaUpdateReschedulePanel, 0, 0);
    lv_obj_set_style_pad_all(_otaUpdateReschedulePanel, 12, 0);
    lv_obj_set_style_pad_row(_otaUpdateReschedulePanel, 10, 0);
    lv_obj_set_flex_flow(_otaUpdateReschedulePanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(_otaUpdateReschedulePanel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *rescheduleHint = lv_label_create(_otaUpdateReschedulePanel);
    lv_obj_set_width(rescheduleHint, lv_pct(100));
    lv_label_set_long_mode(rescheduleHint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(rescheduleHint, "Delay automatic installation until a later time.");
    lv_obj_set_style_text_font(rescheduleHint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rescheduleHint, lv_color_hex(0x334155), 0);

    lv_obj_t *reschedulePickerRow = lv_obj_create(_otaUpdateReschedulePanel);
    lv_obj_set_width(reschedulePickerRow, lv_pct(100));
    lv_obj_set_height(reschedulePickerRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(reschedulePickerRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(reschedulePickerRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(reschedulePickerRow, 0, 0);
    lv_obj_set_style_pad_all(reschedulePickerRow, 0, 0);
    lv_obj_set_style_pad_column(reschedulePickerRow, 8, 0);
    lv_obj_set_flex_flow(reschedulePickerRow, LV_FLEX_FLOW_ROW);

    _otaUpdateRescheduleHourDropdown = lv_dropdown_create(reschedulePickerRow);
    lv_dropdown_set_options(_otaUpdateRescheduleHourDropdown, "0 hours\n1 hour\n2 hours\n4 hours\n8 hours\n12 hours\n24 hours");
    lv_dropdown_set_selected(_otaUpdateRescheduleHourDropdown, 1);
    lv_obj_set_width(_otaUpdateRescheduleHourDropdown, 150);
    lv_obj_add_event_cb(_otaUpdateRescheduleHourDropdown, onOtaUpdateRescheduleHourChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

    _otaUpdateRescheduleMinuteDropdown = lv_dropdown_create(reschedulePickerRow);
    lv_dropdown_set_options(_otaUpdateRescheduleMinuteDropdown, "0 minutes\n15 minutes\n30 minutes\n45 minutes");
    lv_dropdown_set_selected(_otaUpdateRescheduleMinuteDropdown, 0);
    lv_obj_set_width(_otaUpdateRescheduleMinuteDropdown, 150);

    _otaUpdateRescheduleApplyButton = lv_btn_create(_otaUpdateReschedulePanel);
    lv_obj_set_width(_otaUpdateRescheduleApplyButton, lv_pct(100));
    lv_obj_set_height(_otaUpdateRescheduleApplyButton, 46);
    lv_obj_set_style_radius(_otaUpdateRescheduleApplyButton, 16, 0);
    lv_obj_set_style_border_width(_otaUpdateRescheduleApplyButton, 0, 0);
    lv_obj_set_style_bg_color(_otaUpdateRescheduleApplyButton, lv_color_hex(0x1D4ED8), 0);
    lv_obj_add_event_cb(_otaUpdateRescheduleApplyButton, onOtaUpdateRescheduleApplyEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *applyLabel = lv_label_create(_otaUpdateRescheduleApplyButton);
    lv_label_set_text(applyLabel, "Apply Delay");
    lv_obj_set_style_text_font(applyLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(applyLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(applyLabel);

    _otaUpdateProgressCloseButton = lv_btn_create(card);
    lv_obj_set_width(_otaUpdateProgressCloseButton, lv_pct(100));
    lv_obj_set_height(_otaUpdateProgressCloseButton, 48);
    lv_obj_set_style_radius(_otaUpdateProgressCloseButton, 16, 0);
    lv_obj_set_style_bg_color(_otaUpdateProgressCloseButton, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_bg_opa(_otaUpdateProgressCloseButton, LV_OPA_COVER, 0);
    lv_obj_add_flag(_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_otaUpdateProgressCloseButton, onOtaUpdateProgressCloseEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t *closeLabel = lv_label_create(_otaUpdateProgressCloseButton);
    lv_label_set_text(closeLabel, "Close");
    lv_obj_set_style_text_font(closeLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(closeLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_center(closeLabel);
}

void AppSettings::updateOtaUpdateOverlayActions(bool waiting_for_decision, bool show_reschedule_picker)
{
    _otaAutoUpdateAwaitingDecision = waiting_for_decision;
    const bool show_action_row = waiting_for_decision || _firmwareUpdateInProgress;
    const bool allow_manual_dismiss = waiting_for_decision && !_firmwareUpdateInProgress;

    if (lv_obj_ready(_otaUpdateProgressActionRow)) {
        if (show_action_row) {
            lv_obj_clear_flag(_otaUpdateProgressActionRow, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_otaUpdateProgressActionRow, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (lv_obj_ready(_otaUpdateProgressInstallButton)) {
        if (show_action_row) {
            lv_obj_clear_flag(_otaUpdateProgressInstallButton, LV_OBJ_FLAG_HIDDEN);
            if (_firmwareUpdateInProgress) {
                lv_obj_add_state(_otaUpdateProgressInstallButton, LV_STATE_DISABLED);
            } else {
                lv_obj_clear_state(_otaUpdateProgressInstallButton, LV_STATE_DISABLED);
            }
        } else {
            lv_obj_add_flag(_otaUpdateProgressInstallButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_state(_otaUpdateProgressInstallButton, LV_STATE_DISABLED);
        }
    }

    if (lv_obj_ready(_otaUpdateProgressRescheduleButton)) {
        if (show_action_row) {
            lv_obj_clear_flag(_otaUpdateProgressRescheduleButton, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_otaUpdateProgressRescheduleButton, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (lv_obj_ready(_otaUpdateProgressCancelButton)) {
        if (show_action_row) {
            lv_obj_clear_flag(_otaUpdateProgressCancelButton, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_otaUpdateProgressCancelButton, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (lv_obj_ready(_otaUpdateReschedulePanel)) {
        if (show_action_row && show_reschedule_picker) {
            lv_obj_clear_flag(_otaUpdateReschedulePanel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_otaUpdateReschedulePanel, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (lv_obj_ready(_otaUpdateProgressCornerCloseButton)) {
        if (allow_manual_dismiss) {
            lv_obj_clear_flag(_otaUpdateProgressCornerCloseButton, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_otaUpdateProgressCornerCloseButton, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (lv_obj_ready(_otaUpdateProgressCloseButton)) {
        if (allow_manual_dismiss) {
            lv_obj_clear_flag(_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
        } else if (show_action_row) {
            lv_obj_add_flag(_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void AppSettings::setOtaUpdateProgressOverlayVisible(bool visible)
{
    if (visible) {
        ensureOtaUpdateProgressOverlay();
    }

    if ((_otaUpdateProgressOverlay == nullptr) || !lv_obj_ready(_otaUpdateProgressOverlay)) {
        return;
    }

    if (visible) {
        lv_obj_move_foreground(_otaUpdateProgressOverlay);
        lv_obj_clear_flag(_otaUpdateProgressOverlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_otaUpdateProgressOverlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void AppSettings::setFirmwareProgress(int32_t percent, const std::string &phase, bool is_error)
{
    const int32_t clamped = std::max<int32_t>(0, std::min<int32_t>(100, percent));

    if (lv_obj_ready(_otaUpdateProgressBar)) {
        lv_bar_set_value(_otaUpdateProgressBar, clamped, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_otaUpdateProgressBar,
                                  is_error ? lv_color_hex(0xFECACA) : lv_color_hex(0xCBD5E1),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_color(_otaUpdateProgressBar,
                                  is_error ? lv_color_hex(0xDC2626) : lv_color_hex(0x2563EB),
                                  LV_PART_INDICATOR);
    }

    if (lv_obj_ready(_otaUpdateProgressLabel)) {
        lv_label_set_text(_otaUpdateProgressLabel, phase.c_str());
        lv_obj_set_style_text_color(_otaUpdateProgressLabel,
                                    is_error ? lv_color_hex(0xB91C1C) : lv_color_hex(0x64748B),
                                    0);
    }

    if (!isUiActive()) {
        return;
    }

    if (lv_obj_ready(_firmwareProgressBar)) {
        lv_bar_set_value(_firmwareProgressBar, clamped, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_firmwareProgressBar,
                                  is_error ? lv_color_hex(0xFECACA) : lv_color_hex(0xCBD5E1),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_color(_firmwareProgressBar,
                                  is_error ? lv_color_hex(0xDC2626) : lv_color_hex(0x2563EB),
                                  LV_PART_INDICATOR);
    }

    if (lv_obj_ready(_firmwareProgressLabel)) {
        lv_label_set_text(_firmwareProgressLabel, phase.c_str());
        lv_obj_set_style_text_color(_firmwareProgressLabel,
                                    is_error ? lv_color_hex(0xB91C1C) : lv_color_hex(0x64748B),
                                    0);
    }
}

void AppSettings::queueFirmwareUiUpdate(const char *status, int32_t percent, bool busy, bool is_error)
{
    auto *context = new AsyncFirmwareUiContext{};
    if (context == nullptr) {
        ESP_LOGW(TAG, "Failed to allocate firmware UI context");
        return;
    }

    context->app = this;
    context->percent = percent;
    context->busy = busy;
    context->is_error = is_error;
    snprintf(context->status, sizeof(context->status), "%s", (status != nullptr) ? status : "");

    bsp_display_lock(0);
    if (lv_async_call(applyAsyncFirmwareUiUpdate, context) != LV_RES_OK) {
        bsp_display_unlock();
        delete context;
        ESP_LOGW(TAG, "Failed to queue firmware UI update");
        return;
    }
    bsp_display_unlock();
}

std::string AppSettings::getCurrentFirmwareVersion(void) const
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if ((app_desc == nullptr) || (app_desc->version[0] == '\0')) {
        return kFirmwareUnknownVersion;
    }

    return trim_copy(app_desc->version);
}

int AppSettings::compareVersionStrings(const std::string &lhs, const std::string &rhs)
{
    auto parse_values = [](const std::string &input) {
        std::vector<int> values;
        int current_value = -1;

        for (char ch : input) {
            if (std::isdigit(static_cast<unsigned char>(ch))) {
                if (current_value < 0) {
                    current_value = 0;
                }
                current_value = (current_value * 10) + (ch - '0');
            } else if (current_value >= 0) {
                values.push_back(current_value);
                current_value = -1;
            }
        }

        if (current_value >= 0) {
            values.push_back(current_value);
        }

        return values;
    };

    const std::vector<int> lhs_values = parse_values(lhs);
    const std::vector<int> rhs_values = parse_values(rhs);
    const size_t count = std::max(lhs_values.size(), rhs_values.size());
    for (size_t index = 0; index < count; ++index) {
        const int lhs_value = (index < lhs_values.size()) ? lhs_values[index] : 0;
        const int rhs_value = (index < rhs_values.size()) ? rhs_values[index] : 0;
        if (lhs_value != rhs_value) {
            return (lhs_value < rhs_value) ? -1 : 1;
        }
    }

    return 0;
}

void AppSettings::refreshDeferredOtaScheduleState(void)
{
    char version[32] = {};
    _otaDeferredAutoUpdateUntilUs = 0;
    _otaDeferredAutoUpdateVersion.clear();

    if (loadNvsStringParam(NVS_KEY_OTA_RESCHEDULE_VERSION, version, sizeof(version)) && (version[0] != '\0')) {
        _otaDeferredAutoUpdateVersion = trim_copy(version);
    }

    const int32_t deferred_until_epoch = _nvs_param_map[NVS_KEY_OTA_RESCHEDULE_UNTIL];
    const int64_t current_epoch = static_cast<int64_t>(time(nullptr));
    if (_otaDeferredAutoUpdateVersion.empty() || (deferred_until_epoch <= 0)) {
        return;
    }

    if ((current_epoch >= kValidUnixTimeFloor) && (deferred_until_epoch <= current_epoch)) {
        clearDeferredOtaSchedule(true);
        return;
    }

    if ((current_epoch >= kValidUnixTimeFloor) && (deferred_until_epoch > current_epoch)) {
        const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
        _otaDeferredAutoUpdateUntilUs = now_us + (static_cast<uint64_t>(deferred_until_epoch - current_epoch) * 1000000ULL);
        if ((_nextOtaAvailabilityCheckUs == 0) || (_otaDeferredAutoUpdateUntilUs < _nextOtaAvailabilityCheckUs)) {
            _nextOtaAvailabilityCheckUs = _otaDeferredAutoUpdateUntilUs;
        }
    }
}

void AppSettings::clearDeferredOtaSchedule(bool persist)
{
    _otaDeferredAutoUpdateUntilUs = 0;
    _otaDeferredAutoUpdateVersion.clear();
    if (!persist) {
        return;
    }

    _nvs_param_map[NVS_KEY_OTA_RESCHEDULE_UNTIL] = 0;
    setNvsParam(NVS_KEY_OTA_RESCHEDULE_UNTIL, 0);
    setNvsStringParam(NVS_KEY_OTA_RESCHEDULE_VERSION, "");
}

void AppSettings::deferOtaUpdateForEntry(const FirmwareEntry_t &entry, uint32_t delay_seconds)
{
    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    _otaDeferredAutoUpdateUntilUs = now_us + (static_cast<uint64_t>(delay_seconds) * 1000000ULL);
    _otaDeferredAutoUpdateVersion = entry.version;
    _nvs_param_map[NVS_KEY_OTA_RESCHEDULE_UNTIL] = 0;

    const int64_t current_epoch = static_cast<int64_t>(time(nullptr));
    if (current_epoch >= kValidUnixTimeFloor) {
        const int64_t deferred_epoch = current_epoch + static_cast<int64_t>(delay_seconds);
        if (deferred_epoch <= std::numeric_limits<int32_t>::max()) {
            _nvs_param_map[NVS_KEY_OTA_RESCHEDULE_UNTIL] = static_cast<int32_t>(deferred_epoch);
        }
    }

    setNvsParam(NVS_KEY_OTA_RESCHEDULE_UNTIL, _nvs_param_map[NVS_KEY_OTA_RESCHEDULE_UNTIL]);
    setNvsStringParam(NVS_KEY_OTA_RESCHEDULE_VERSION, _otaDeferredAutoUpdateVersion.c_str());
    _nextOtaAvailabilityCheckUs = (_nextOtaAvailabilityCheckUs == 0)
                                    ? _otaDeferredAutoUpdateUntilUs
                                    : std::min(_nextOtaAvailabilityCheckUs, _otaDeferredAutoUpdateUntilUs);
}

bool AppSettings::shouldSuppressDeferredOtaUpdate(const FirmwareEntry_t &entry, uint64_t now_us)
{
    if (_otaDeferredAutoUpdateVersion.empty() || (_otaDeferredAutoUpdateVersion != entry.version)) {
        return false;
    }

    if ((_otaDeferredAutoUpdateUntilUs != 0) && (now_us < _otaDeferredAutoUpdateUntilUs)) {
        _nextOtaAvailabilityCheckUs = (_nextOtaAvailabilityCheckUs == 0)
                                        ? _otaDeferredAutoUpdateUntilUs
                                        : std::min(_nextOtaAvailabilityCheckUs, _otaDeferredAutoUpdateUntilUs);
        return true;
    }

    const int32_t deferred_until_epoch = _nvs_param_map[NVS_KEY_OTA_RESCHEDULE_UNTIL];
    const int64_t current_epoch = static_cast<int64_t>(time(nullptr));
    if ((deferred_until_epoch > 0) && (current_epoch >= kValidUnixTimeFloor) && (deferred_until_epoch > current_epoch)) {
        _otaDeferredAutoUpdateUntilUs = now_us + (static_cast<uint64_t>(deferred_until_epoch - current_epoch) * 1000000ULL);
        _nextOtaAvailabilityCheckUs = (_nextOtaAvailabilityCheckUs == 0)
                                        ? _otaDeferredAutoUpdateUntilUs
                                        : std::min(_nextOtaAvailabilityCheckUs, _otaDeferredAutoUpdateUntilUs);
        return true;
    }

    return false;
}

bool AppSettings::shouldResumeDeferredOtaUpdate(const FirmwareEntry_t &entry, uint64_t now_us)
{
    if (_otaDeferredAutoUpdateVersion.empty() || (_otaDeferredAutoUpdateVersion != entry.version)) {
        return false;
    }

    if ((_otaDeferredAutoUpdateUntilUs != 0) && (now_us >= _otaDeferredAutoUpdateUntilUs)) {
        return true;
    }

    const int32_t deferred_until_epoch = _nvs_param_map[NVS_KEY_OTA_RESCHEDULE_UNTIL];
    const int64_t current_epoch = static_cast<int64_t>(time(nullptr));
    return (deferred_until_epoch > 0) && (current_epoch >= kValidUnixTimeFloor) && (deferred_until_epoch <= current_epoch);
}

void AppSettings::updateOtaRescheduleMinuteOptions()
{
    if (!lv_obj_ready(_otaUpdateRescheduleHourDropdown) || !lv_obj_ready(_otaUpdateRescheduleMinuteDropdown)) {
        return;
    }

    const uint16_t hour_index = lv_dropdown_get_selected(_otaUpdateRescheduleHourDropdown);
    const size_t hour_slot = std::min<size_t>(hour_index, std::size(kOtaRescheduleHourOptions) - 1);
    if (kOtaRescheduleHourOptions[hour_slot] == 0) {
        lv_dropdown_set_options(_otaUpdateRescheduleMinuteDropdown, "15 minutes\n30 minutes\n45 minutes");
        if (lv_dropdown_get_selected(_otaUpdateRescheduleMinuteDropdown) > 2) {
            lv_dropdown_set_selected(_otaUpdateRescheduleMinuteDropdown, 0);
        }
        return;
    }

    const uint16_t previous_index = lv_dropdown_get_selected(_otaUpdateRescheduleMinuteDropdown);
    lv_dropdown_set_options(_otaUpdateRescheduleMinuteDropdown, "0 minutes\n15 minutes\n30 minutes\n45 minutes");
    lv_dropdown_set_selected(_otaUpdateRescheduleMinuteDropdown, std::min<uint16_t>(previous_index, 3));
}

uint32_t AppSettings::getSelectedOtaRescheduleDelaySeconds(void) const
{
    const uint16_t hour_index = lv_obj_ready(_otaUpdateRescheduleHourDropdown) ? lv_dropdown_get_selected(_otaUpdateRescheduleHourDropdown) : 0;
    const uint16_t minute_index = lv_obj_ready(_otaUpdateRescheduleMinuteDropdown) ? lv_dropdown_get_selected(_otaUpdateRescheduleMinuteDropdown) : 0;
    const size_t hour_slot = std::min<size_t>(hour_index, std::size(kOtaRescheduleHourOptions) - 1);
    const bool zero_hours = kOtaRescheduleHourOptions[hour_slot] == 0;
    if (zero_hours) {
        const size_t minute_slot = std::min<size_t>(minute_index, std::size(kOtaRescheduleMinuteFutureOnlyOptions) - 1);
        return static_cast<uint32_t>((kOtaRescheduleMinuteFutureOnlyOptions[minute_slot]) * 60);
    }

    const size_t minute_slot = std::min<size_t>(minute_index, std::size(kOtaRescheduleMinuteOptions) - 1);
    return static_cast<uint32_t>((kOtaRescheduleHourOptions[hour_slot] * 60 + kOtaRescheduleMinuteOptions[minute_slot]) * 60);
}

void AppSettings::showAutoUpdateDecisionOverlay(const FirmwareEntry_t &entry)
{
    setOtaUpdateProgressOverlayVisible(true);
    updateOtaUpdateOverlayActions(true, false);
    setFirmwareStatus(std::string("Auto update is ready for ") + entry.version + ".", false);
    setFirmwareProgress(0, "Install now, reschedule, or cancel this automatic update.", false);
    if (lv_obj_ready(_otaUpdateRescheduleHourDropdown)) {
        lv_dropdown_set_selected(_otaUpdateRescheduleHourDropdown, 1);
    }
    if (lv_obj_ready(_otaUpdateRescheduleMinuteDropdown)) {
        lv_dropdown_set_selected(_otaUpdateRescheduleMinuteDropdown, 0);
    }
    updateOtaRescheduleMinuteOptions();
}

std::string AppSettings::formatFirmwareLabel(const FirmwareEntry_t &entry) const
{
    std::string label = entry.label;
    if (!entry.version.empty() && (entry.version != kFirmwareUnknownVersion)) {
        label += " (" + entry.version + ")";
    }
    if (entry.is_current) {
        label += " [current]";
    } else if (entry.is_newer) {
        label += " [new]";
    }
    if (!entry.is_valid) {
        label += " [invalid]";
    }
    return label;
}

void AppSettings::populateFirmwareDropdown(lv_obj_t *dropdown, const std::vector<FirmwareEntry_t> &entries, const char *empty_label)
{
    if (dropdown == nullptr) {
        return;
    }

    if (entries.empty()) {
        lv_dropdown_set_options(dropdown, empty_label);
        lv_dropdown_set_selected(dropdown, 0);
        return;
    }

    std::string options;
    for (size_t index = 0; index < entries.size(); ++index) {
        if (index > 0) {
            options += '\n';
        }
        options += formatFirmwareLabel(entries[index]);
    }
    lv_dropdown_set_options(dropdown, options.c_str());
    lv_dropdown_set_selected(dropdown, 0);
}

int AppSettings::getSelectedOtaFirmwareIndex(void) const
{
    if ((_selectedOtaFirmwareIndex < 0) || (static_cast<size_t>(_selectedOtaFirmwareIndex) >= _otaFirmwareEntries.size())) {
        return -1;
    }

    return _selectedOtaFirmwareIndex;
}

void AppSettings::setSelectedOtaFirmwareIndex(int index)
{
    _selectedOtaFirmwareIndex = ((index >= 0) && (static_cast<size_t>(index) < _otaFirmwareEntries.size())) ? index : -1;

    for (size_t entry_index = 0; entry_index < _firmwareOtaCheckboxes.size(); ++entry_index) {
        lv_obj_t *checkbox = _firmwareOtaCheckboxes[entry_index];
        if ((checkbox == nullptr) || !lv_obj_ready(checkbox)) {
            continue;
        }

        if (static_cast<int>(entry_index) == _selectedOtaFirmwareIndex) {
            lv_obj_add_state(checkbox, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(checkbox, LV_STATE_CHECKED);
        }
    }
}

void AppSettings::rebuildFirmwareOtaList(void)
{
    if ((_firmwareOtaListContainer == nullptr) || !lv_obj_ready(_firmwareOtaListContainer)) {
        return;
    }

    lv_obj_clean(_firmwareOtaListContainer);
    _firmwareOtaCheckboxes.clear();

    const std::string current_version = getCurrentFirmwareVersion();
    std::string latest_version = "No GitHub firmware checked yet";
    for (const FirmwareEntry_t &entry : _otaFirmwareEntries) {
        if (entry.is_valid) {
            latest_version = entry.version.empty() ? entry.label : entry.version;
            break;
        }
    }

    if ((_selectedOtaFirmwareIndex >= 0) && (static_cast<size_t>(_selectedOtaFirmwareIndex) >= _otaFirmwareEntries.size())) {
        _selectedOtaFirmwareIndex = -1;
    }

    if (_firmwareOtaSummaryLabel != nullptr) {
        char summary[256] = {};
        snprintf(summary,
                 sizeof(summary),
                 "Installed: %s\nLatest available: %s\nAvailable releases: %u",
                 current_version.c_str(),
                 latest_version.c_str(),
                 static_cast<unsigned>(_otaFirmwareEntries.size()));
        lv_label_set_text(_firmwareOtaSummaryLabel, summary);
    }

    if (_otaFirmwareEntries.empty()) {
        lv_obj_t *emptyLabel = lv_label_create(_firmwareOtaListContainer);
        lv_obj_set_width(emptyLabel, lv_pct(100));
        lv_label_set_long_mode(emptyLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(emptyLabel, "Press Check to load OTA-ready firmware releases from GitHub.");
        lv_obj_set_style_text_color(emptyLabel, lv_color_hex(0x64748B), 0);
        return;
    }

    _firmwareOtaCheckboxes.reserve(_otaFirmwareEntries.size());
    for (size_t index = 0; index < _otaFirmwareEntries.size(); ++index) {
        const FirmwareEntry_t &entry = _otaFirmwareEntries[index];

        lv_obj_t *checkbox = lv_checkbox_create(_firmwareOtaListContainer);
        lv_obj_set_width(checkbox, lv_pct(100));
        lv_checkbox_set_text(checkbox, formatFirmwareLabel(entry).c_str());
        lv_obj_set_style_pad_ver(checkbox, 8, 0);
        lv_obj_set_style_text_font(checkbox, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(checkbox, lv_color_hex(0x0F172A), 0);
        lv_obj_add_event_cb(checkbox, onFirmwareOtaEntryCheckedEventCallback, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_set_user_data(checkbox, reinterpret_cast<void *>(index + 1));
        if (!entry.is_valid) {
            lv_obj_add_state(checkbox, LV_STATE_DISABLED);
        }
        _firmwareOtaCheckboxes.push_back(checkbox);
    }

    if ((_selectedOtaFirmwareIndex < 0) || (static_cast<size_t>(_selectedOtaFirmwareIndex) >= _otaFirmwareEntries.size()) ||
        !_otaFirmwareEntries[_selectedOtaFirmwareIndex].is_valid) {
        int preferred_index = -1;
        for (size_t index = 0; index < _otaFirmwareEntries.size(); ++index) {
            if (_otaFirmwareEntries[index].is_valid && _otaFirmwareEntries[index].is_newer) {
                preferred_index = static_cast<int>(index);
                break;
            }
        }
        if ((preferred_index < 0) && !_otaFirmwareEntries.empty() && _otaFirmwareEntries.front().is_valid) {
            preferred_index = 0;
        }
        _selectedOtaFirmwareIndex = preferred_index;
    }

    setSelectedOtaFirmwareIndex(_selectedOtaFirmwareIndex);
}

void AppSettings::releaseFirmwareOtaResources(void)
{
    _firmwareOtaCheckInProgress = false;
    _selectedOtaFirmwareIndex = -1;

    if ((_firmwareOtaListContainer != nullptr) && lv_obj_ready(_firmwareOtaListContainer)) {
        lv_obj_clean(_firmwareOtaListContainer);
    }

    _firmwareOtaCheckboxes.clear();
    std::vector<lv_obj_t *>().swap(_firmwareOtaCheckboxes);
    _otaFirmwareEntries.clear();
    std::vector<FirmwareEntry_t>().swap(_otaFirmwareEntries);

    if ((_firmwareOtaSummaryLabel != nullptr) && lv_obj_ready(_firmwareOtaSummaryLabel)) {
        lv_label_set_text(_firmwareOtaSummaryLabel, "");
    }

    if ((_firmwareOtaCheckOverlay != nullptr) && lv_obj_ready(_firmwareOtaCheckOverlay)) {
        lv_obj_del(_firmwareOtaCheckOverlay);
    }
    _firmwareOtaCheckOverlay = nullptr;
    _firmwareOtaCheckSpinner = nullptr;
    _firmwareOtaCheckStatusLabel = nullptr;
}

bool AppSettings::hasOtaFlashSupport(void) const
{
    return esp_ota_get_next_update_partition(nullptr) != nullptr;
}

bool AppSettings::probeFirmwareFile(const std::string &path, FirmwareEntry_t &entry)
{
    entry = {};
    entry.path_or_url = path;
    entry.label = basename_from_path(path);
    entry.version = kFirmwareUnknownVersion;

    FILE *file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        entry.notes = "Unable to open file";
        return false;
    }

    struct stat file_info = {};
    if (stat(path.c_str(), &file_info) == 0) {
        entry.size_bytes = static_cast<size_t>(file_info.st_size);
    }

    esp_image_header_t image_header = {};
    esp_image_segment_header_t segment_header = {};
    esp_app_desc_t app_desc = {};
    bool ok = false;

    if ((fread(&image_header, 1, sizeof(image_header), file) == sizeof(image_header)) &&
        (image_header.magic == ESP_IMAGE_HEADER_MAGIC) &&
        (fread(&segment_header, 1, sizeof(segment_header), file) == sizeof(segment_header)) &&
        (fread(&app_desc, 1, sizeof(app_desc), file) == sizeof(app_desc))) {
        entry.project_name = trim_copy(app_desc.project_name);
        entry.version = trim_copy(app_desc.version);
        if (entry.version.empty()) {
            entry.version = kFirmwareUnknownVersion;
        }

        const esp_app_desc_t *current_app = esp_app_get_description();
        const std::string current_project = (current_app != nullptr) ? trim_copy(current_app->project_name) : std::string();
        if (!current_project.empty() && !entry.project_name.empty() && (entry.project_name != current_project)) {
            entry.notes = "Project mismatch: " + entry.project_name;
        } else {
            const std::string current_version = getCurrentFirmwareVersion();
            entry.is_valid = true;
            entry.is_current = compareVersionStrings(entry.version, current_version) == 0;
            entry.is_newer = compareVersionStrings(entry.version, current_version) > 0;

            std::ostringstream stream;
            stream << "Project: " << (entry.project_name.empty() ? "unknown" : entry.project_name)
                   << ", size: " << (entry.size_bytes / 1024U) << " KB";
            entry.notes = stream.str();
            ok = true;
        }
    } else {
        entry.notes = "Not a valid ESP firmware image";
    }

    fclose(file);
    return ok;
}

bool AppSettings::scanSdFirmwareEntries(void)
{
    _sdFirmwareEntries.clear();
    const char *directories[] = {kFirmwareSdDirectory, "/sdcard"};

    for (const char *directory_path : directories) {
        DIR *dir = opendir(directory_path);
        if (dir == nullptr) {
            continue;
        }

        struct dirent *item = nullptr;
        while ((item = readdir(dir)) != nullptr) {
            if ((strcmp(item->d_name, ".") == 0) || (strcmp(item->d_name, "..") == 0)) {
                continue;
            }

            const std::string candidate_path = std::string(directory_path) + "/" + item->d_name;
            if (!ends_with_bin(candidate_path)) {
                continue;
            }

            FirmwareEntry_t entry = {};
            probeFirmwareFile(candidate_path, entry);
            _sdFirmwareEntries.push_back(entry);
        }

        closedir(dir);
    }

    std::sort(_sdFirmwareEntries.begin(), _sdFirmwareEntries.end(), [](const FirmwareEntry_t &lhs, const FirmwareEntry_t &rhs) {
        if (lhs.is_valid != rhs.is_valid) {
            return lhs.is_valid > rhs.is_valid;
        }
        if (lhs.is_newer != rhs.is_newer) {
            return lhs.is_newer > rhs.is_newer;
        }
        return lhs.label < rhs.label;
    });

    return !_sdFirmwareEntries.empty();
}

bool AppSettings::fetchGithubFirmwareEntries(void)
{
    std::vector<FirmwareEntry_t> entries;
    if (!fetchGithubFirmwareEntriesForVersion(getCurrentFirmwareVersion(), entries)) {
        _otaFirmwareEntries.clear();
        _otaUpdateAvailableThisBoot = false;
        refreshRadioStatusBar();
        return false;
    }

    _otaFirmwareEntries = std::move(entries);
    _otaUpdateAvailableThisBoot = std::any_of(_otaFirmwareEntries.begin(), _otaFirmwareEntries.end(), [](const FirmwareEntry_t &entry) {
        return entry.is_valid && entry.is_newer;
    });
    refreshRadioStatusBar();
    return !_otaFirmwareEntries.empty();
}

bool AppSettings::fetchGithubFirmwareEntriesForVersion(const std::string &current_version, std::vector<FirmwareEntry_t> &entries)
{
    entries.clear();
    std::string response;

    esp_http_client_config_t config = {};
    config.url = kFirmwareGithubReleasesUrl;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
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
        return false;
    }

    bool ok = false;
    if ((esp_http_client_set_header(client, "Accept", "application/vnd.github+json") == ESP_OK) &&
        (esp_http_client_set_header(client, "User-Agent", "JC4880P443C-IW-Remote") == ESP_OK) &&
        (esp_http_client_perform(client) == ESP_OK) &&
        (esp_http_client_get_status_code(client) == 200)) {
        cJSON *root = cJSON_Parse(response.c_str());
        if (cJSON_IsArray(root)) {
            cJSON *release = nullptr;
            cJSON_ArrayForEach(release, root) {
                if (!cJSON_IsObject(release) || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(release, "draft"))) {
                    continue;
                }

                const std::string release_name = trim_copy(safe_json_string(release, "name"));
                const std::string tag_name = trim_copy(safe_json_string(release, "tag_name"));
                const std::string release_body = trim_copy(safe_json_string(release, "body"));
                cJSON *assets = cJSON_GetObjectItemCaseSensitive(release, "assets");
                if (!cJSON_IsArray(assets)) {
                    continue;
                }

                cJSON *asset = nullptr;
                cJSON_ArrayForEach(asset, assets) {
                    if (!cJSON_IsObject(asset)) {
                        continue;
                    }

                    const std::string asset_name = trim_copy(safe_json_string(asset, "name"));
                    const std::string download_url = trim_copy(safe_json_string(asset, "browser_download_url"));
                    if (asset_name.empty() || download_url.empty() || !ends_with_bin(asset_name)) {
                        continue;
                    }

                    FirmwareEntry_t entry = {};
                    entry.label = asset_name;
                    entry.version = tag_name.empty() ? release_name : tag_name;
                    if (entry.version.empty()) {
                        entry.version = kFirmwareUnknownVersion;
                    }
                    entry.path_or_url = download_url;
                    entry.project_name = "JC4880P443C_I_W_Remote";
                    entry.size_bytes = static_cast<size_t>(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(asset, "size")) ?
                                                              cJSON_GetObjectItemCaseSensitive(asset, "size")->valuedouble : 0);
                    entry.notes = release_name.empty() ? std::string("GitHub release") : release_name;
                    entry.release_notes = release_body;
                    entry.is_valid = true;
                    entry.is_current = compareVersionStrings(entry.version, current_version) == 0;
                    entry.is_newer = compareVersionStrings(entry.version, current_version) > 0;
                    entries.push_back(entry);
                }
            }
        }
        cJSON_Delete(root);
        ok = !entries.empty();
    }

    esp_http_client_cleanup(client);

    std::sort(entries.begin(), entries.end(), [](const FirmwareEntry_t &lhs, const FirmwareEntry_t &rhs) {
        if (lhs.is_current != rhs.is_current) {
            return lhs.is_current > rhs.is_current;
        }
        if (lhs.is_newer != rhs.is_newer) {
            return lhs.is_newer > rhs.is_newer;
        }
        return AppSettings::compareVersionStrings(lhs.version, rhs.version) > 0;
    });

    return ok;
}

bool AppSettings::isWifiConnectedForOtaCheck(void) const
{
#if !CONFIG_JC4880_FEATURE_WIFI
    return false;
#else
    wifi_ap_record_t ap_info = {};
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
#endif
}

int AppSettings::findPreferredOtaEntryIndex(bool prefer_newer) const
{
    if (prefer_newer) {
        for (size_t index = 0; index < _otaFirmwareEntries.size(); ++index) {
            if (_otaFirmwareEntries[index].is_valid && _otaFirmwareEntries[index].is_newer) {
                return static_cast<int>(index);
            }
        }
    }

    for (size_t index = 0; index < _otaFirmwareEntries.size(); ++index) {
        if (_otaFirmwareEntries[index].is_valid) {
            return static_cast<int>(index);
        }
    }

    return -1;
}

void AppSettings::requestFirmwareScreenOpen(bool prefer_newer)
{
#if !CONFIG_JC4880_FEATURE_OTA
    (void)prefer_newer;
    return;
#else
    const int preferred_index = findPreferredOtaEntryIndex(prefer_newer);
    if (preferred_index >= 0) {
        setSelectedOtaFirmwareIndex(preferred_index);
    }

    _pendingOpenFirmwareScreen = true;
    if (isUiActive()) {
        openFirmwareScreenIfPending();
        return;
    }

    if ((jc_security_handle_app_launch_request != nullptr) &&
        jc_security_handle_app_launch_request(getId(), getName())) {
        return;
    }

    ESP_Brookesia_CoreAppEventData_t app_event_data = {
        .id = getId(),
        .type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START,
        .data = nullptr,
    };
    getPhone()->sendAppEvent(&app_event_data);
#endif
}

void AppSettings::openFirmwareScreenIfPending(void)
{
    if (!_pendingOpenFirmwareScreen) {
        return;
    }

    _pendingOpenFirmwareScreen = false;
    ensureFirmwareScreen();
    refreshFirmwareUi();
    if ((_firmwareScreen != nullptr) && (lv_scr_act() != _firmwareScreen)) {
        lv_scr_load_anim(_firmwareScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
    }
}

void AppSettings::applyAsyncOtaAvailabilityResult(void *arg)
{
    std::unique_ptr<AsyncOtaAvailabilityContext> context(static_cast<AsyncOtaAvailabilityContext *>(arg));
    if ((context == nullptr) || (context->app == nullptr)) {
        return;
    }

    AppSettings *app = context->app;
    app->_otaAvailabilityCheckInProgress = false;

    if (!context->success) {
        return;
    }

    app->_otaFirmwareEntries = std::move(context->entries);
    app->_otaUpdateAvailableThisBoot = std::any_of(app->_otaFirmwareEntries.begin(), app->_otaFirmwareEntries.end(),
                                                   [](const FirmwareEntry_t &entry) {
                                                       return entry.is_valid && entry.is_newer;
                                                   });

    if (app->getSelectedOtaFirmwareIndex() < 0) {
        app->setSelectedOtaFirmwareIndex(app->findPreferredOtaEntryIndex(true));
    }

    if (!app->_otaUpdateAvailableThisBoot && (app->_otaUpdateAvailableMsgbox != nullptr) && lv_obj_is_valid(app->_otaUpdateAvailableMsgbox)) {
        lv_msgbox_close_async(app->_otaUpdateAvailableMsgbox);
    }

    app->refreshRadioStatusBar();
    if (app->isUiActive()) {
        app->refreshFirmwareUi();
    }

    if (!app->_otaUpdateAvailableThisBoot || app->_otaUpdatePromptDismissedThisBoot ||
        ((app->_otaUpdateAvailableMsgbox != nullptr) && lv_obj_is_valid(app->_otaUpdateAvailableMsgbox))) {
        return;
    }

    const int preferred_index = app->findPreferredOtaEntryIndex(true);
    if (preferred_index < 0) {
        return;
    }

    const FirmwareEntry_t &entry = app->_otaFirmwareEntries[preferred_index];
    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());

    if (app->_nvs_param_map[NVS_KEY_OTA_AUTO_UPDATE] != 0) {
        if (app->shouldSuppressDeferredOtaUpdate(entry, now_us)) {
            return;
        }

        if (app->shouldResumeDeferredOtaUpdate(entry, now_us)) {
            app->clearDeferredOtaSchedule(true);
            app->_otaUpdatePromptDismissedThisBoot = true;
            app->startPreferredOtaUpdate();
            return;
        }

        app->setSelectedOtaFirmwareIndex(preferred_index);
        app->_otaUpdatePromptDismissedThisBoot = true;
        app->startPreferredOtaUpdate();
        return;
    }

    const std::string message = std::string("Current firmware: ") + app->getCurrentFirmwareVersion() +
                                "\nNew firmware: " + entry.version +
                                "\n\nStart the OTA update now?";
    static const char *buttons[] = {"Update", "Cancel", ""};
    app->_otaUpdateAvailableMsgbox = lv_msgbox_create(lv_layer_top(), "Update Available", message.c_str(), buttons, false);
    if ((app->_otaUpdateAvailableMsgbox == nullptr) || !lv_obj_is_valid(app->_otaUpdateAvailableMsgbox)) {
        app->_otaUpdateAvailableMsgbox = nullptr;
        return;
    }

    (void)bsp_extra_audio_play_system_sound(BSP_EXTRA_AUDIO_SYSTEM_SOUND_UPDATE_AVAILABLE);
    lv_obj_set_width(app->_otaUpdateAvailableMsgbox, LV_MIN(LV_HOR_RES - 24, 460));
    lv_obj_center(app->_otaUpdateAvailableMsgbox);
    lv_obj_add_event_cb(app->_otaUpdateAvailableMsgbox, onOtaUpdateAvailablePopupEventCallback, LV_EVENT_VALUE_CHANGED, app);
    lv_obj_add_event_cb(app->_otaUpdateAvailableMsgbox, onOtaUpdateAvailablePopupEventCallback, LV_EVENT_DELETE, app);
}

void AppSettings::maybeRunOtaAvailabilityCheck(void)
{
#if !CONFIG_JC4880_FEATURE_OTA
    return;
#else
    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    if ((_otaDeferredAutoUpdateUntilUs != 0) && ((_nextOtaAvailabilityCheckUs == 0) || (_otaDeferredAutoUpdateUntilUs < _nextOtaAvailabilityCheckUs))) {
        _nextOtaAvailabilityCheckUs = _otaDeferredAutoUpdateUntilUs;
    }

    if (_otaAvailabilityCheckInProgress || _firmwareUpdateInProgress || _firmwareOtaCheckInProgress || (now_us < _nextOtaAvailabilityCheckUs)) {
        return;
    }

    if (!hasOtaFlashSupport() || !isWifiConnectedForOtaCheck()) {
        _nextOtaAvailabilityCheckUs = now_us + kOtaAvailabilityRetryIntervalUs;
        return;
    }

    _otaAvailabilityCheckInProgress = true;
    _nextOtaAvailabilityCheckUs = now_us + kOtaAvailabilitySuccessIntervalUs;

    auto *context = new AsyncOtaAvailabilityContext{};
    if (context == nullptr) {
        _otaAvailabilityCheckInProgress = false;
        _nextOtaAvailabilityCheckUs = now_us + kOtaAvailabilityRetryIntervalUs;
        return;
    }

    context->app = this;
    context->success = fetchGithubFirmwareEntriesForVersion(getCurrentFirmwareVersion(), context->entries);
    if (!context->success) {
        _nextOtaAvailabilityCheckUs = now_us + kOtaAvailabilityRetryIntervalUs;
    }

    bsp_display_lock(0);
    if (lv_async_call(applyAsyncOtaAvailabilityResult, context) != LV_RES_OK) {
        bsp_display_unlock();
        delete context;
        _otaAvailabilityCheckInProgress = false;
        _nextOtaAvailabilityCheckUs = now_us + kOtaAvailabilityRetryIntervalUs;
        return;
    }
    bsp_display_unlock();
#endif
}

void AppSettings::refreshFirmwareUi(void)
{
#if !CONFIG_JC4880_FEATURE_OTA
    return;
#endif
    if (lv_obj_ready(_firmwareAutoUpdateSwitch)) {
        if (_nvs_param_map[NVS_KEY_OTA_AUTO_UPDATE] != 0) {
            lv_obj_add_state(_firmwareAutoUpdateSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_firmwareAutoUpdateSwitch, LV_STATE_CHECKED);
        }
    }

    populateFirmwareDropdown(_firmwareSdDropdown, _sdFirmwareEntries, "No SD firmware found");
    rebuildFirmwareOtaList();

    if (_firmwareCurrentVersionLabel != nullptr) {
        const std::string current_version = getCurrentFirmwareVersion();
        const esp_app_desc_t *app_desc = esp_app_get_description();
        const std::string project_name = ((app_desc != nullptr) && (app_desc->project_name[0] != '\0'))
                                             ? trim_copy(app_desc->project_name)
                                             : std::string("unknown project");
        std::string installed_text = "Current firmware: " + current_version;
        if (!project_name.empty()) {
            installed_text += "\nProject: " + project_name;
        }
        lv_label_set_text(_firmwareCurrentVersionLabel, installed_text.c_str());
    }

    const bool ota_supported = hasOtaFlashSupport();
    const uint16_t sd_index = (_firmwareSdDropdown != nullptr) ? lv_dropdown_get_selected(_firmwareSdDropdown) : 0;
    const int ota_index = getSelectedOtaFirmwareIndex();
    const bool controls_busy = _firmwareUpdateInProgress || _firmwareOtaCheckInProgress;
    const bool sd_ready = ota_supported && !controls_busy && (sd_index < _sdFirmwareEntries.size()) && _sdFirmwareEntries[sd_index].is_valid;
    const bool ota_ready = ota_supported && !controls_busy && (ota_index >= 0) &&
                           (static_cast<size_t>(ota_index) < _otaFirmwareEntries.size()) && _otaFirmwareEntries[ota_index].is_valid;

    auto update_button = [](lv_obj_t *button, bool enabled) {
        if (button == nullptr) {
            return;
        }

        if (enabled) {
            lv_obj_clear_state(button, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(button, lv_color_hex(0x2563EB), 0);
        } else {
            lv_obj_add_state(button, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(button, lv_color_hex(0x94A3B8), 0);
        }
    };

    update_button(_firmwareSdFlashButton, sd_ready);
    update_button(_firmwareOtaCheckButton, !_firmwareUpdateInProgress && !_firmwareOtaCheckInProgress);
    update_button(_firmwareOtaFlashButton, ota_ready);

    if (_firmwareUpdateInProgress) {
        return;
    }

    if (_firmwareOtaCheckInProgress) {
        setFirmwareStatus("Checking GitHub for firmware releases...");
        setFirmwareProgress(0, "Fetching release list from server...");
        return;
    }

    if (!ota_supported) {
        setFirmwareStatus("Flash buttons are disabled because this build has only a factory app partition. Safe in-app updates require OTA partitions.");
    } else if ((ota_index >= 0) && (static_cast<size_t>(ota_index) < _otaFirmwareEntries.size())) {
        const FirmwareEntry_t &entry = _otaFirmwareEntries[ota_index];
        setFirmwareStatus(entry.release_notes.empty() ? (entry.notes.empty() ? std::string("No release notes available.") : entry.notes)
                                                     : entry.release_notes);
        setFirmwareProgress(0, "Ready to download and flash the selected GitHub firmware.");
    } else if ((sd_index < _sdFirmwareEntries.size()) && !_sdFirmwareEntries.empty()) {
        setFirmwareStatus(_sdFirmwareEntries[sd_index].notes);
        setFirmwareProgress(0, "Ready to flash selected SD firmware image.");
    } else {
        setFirmwareStatus("Firmware screen ready. Scan SD or check GitHub releases for OTA .bin assets.");
        setFirmwareProgress(0, "Idle");
    }
}

void AppSettings::applyAsyncFirmwareUiUpdate(void *arg)
{
    auto *context = static_cast<AsyncFirmwareUiContext *>(arg);
    if ((context == nullptr) || (context->app == nullptr)) {
        delete context;
        return;
    }

    const bool canceled = !context->busy && !context->is_error &&
                          (strcmp(context->status, "Firmware update canceled.") == 0);

    context->app->_firmwareUpdateInProgress = context->busy;
    context->app->updateOtaUpdateOverlayActions(canceled, false);
    context->app->setOtaUpdateProgressOverlayVisible(context->busy || context->is_error || canceled);
    if (lv_obj_ready(context->app->_otaUpdateProgressCloseButton)) {
        if (context->busy || (!context->is_error && !canceled)) {
            lv_obj_add_flag(context->app->_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(context->app->_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (context->is_error) {
        context->app->refreshFirmwareUi();
        context->app->setFirmwareStatus(context->status, true);
        context->app->setFirmwareProgress(context->percent, context->status, true);
        (void)bsp_extra_audio_play_system_sound(BSP_EXTRA_AUDIO_SYSTEM_SOUND_ERROR);
        delete context;
        return;
    }

    if (canceled) {
        context->app->refreshFirmwareUi();
        context->app->setFirmwareStatus(context->status, false);
        context->app->setFirmwareProgress(0, "Install now, reschedule, or close this automatic update.", false);
        delete context;
        return;
    }

    if (!context->busy) {
        context->app->setFirmwareStatus(context->status, false);
    }
    context->app->setFirmwareProgress(context->percent, context->status, context->is_error);
    context->app->refreshFirmwareUi();
    delete context;
}

bool AppSettings::validateFirmwareImageHeader(const uint8_t *data, size_t data_len, const std::string &source_label,
                                             std::string &error_message, bool &header_checked)
{
    if (header_checked) {
        return true;
    }

    const size_t required_bytes = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
    if (data_len < required_bytes) {
        return true;
    }

    auto *image_header = reinterpret_cast<const esp_image_header_t *>(data);
    if (image_header->magic != ESP_IMAGE_HEADER_MAGIC) {
        error_message = source_label + " is not a valid ESP firmware image.";
        return false;
    }

    auto *app_desc = reinterpret_cast<const esp_app_desc_t *>(data + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    const esp_app_desc_t *current_app = esp_app_get_description();
    const std::string current_project = (current_app != nullptr) ? trim_copy(current_app->project_name) : std::string();
    const std::string incoming_project = trim_copy(app_desc->project_name);
    if (!current_project.empty() && !incoming_project.empty() && (incoming_project != current_project)) {
        error_message = "Firmware project mismatch: " + incoming_project;
        return false;
    }

    header_checked = true;
    return true;
}

void AppSettings::persistPendingReleaseNotes(const FirmwareEntry_t &entry)
{
    setNvsStringParam(NVS_KEY_OTA_PENDING_VERSION, entry.version.empty() ? kFirmwareUnknownVersion : entry.version.c_str());
    setNvsStringParam(NVS_KEY_OTA_PENDING_NOTES,
                      entry.release_notes.empty() ? entry.notes.c_str() : entry.release_notes.c_str());
    setNvsParam(NVS_KEY_OTA_PENDING_SHOW, 1);
}

bool AppSettings::flashFirmwareFromFile(const FirmwareEntry_t &entry, std::string &error_message)
{
    ESP_LOGI(TAG, "Starting SD firmware flash: label='%s' path='%s'", entry.label.c_str(), entry.path_or_url.c_str());

    FILE *file = fopen(entry.path_or_url.c_str(), "rb");
    if (file == nullptr) {
        error_message = "Unable to open selected firmware file.";
        ESP_LOGE(TAG, "%s", error_message.c_str());
        return false;
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    if (partition == nullptr) {
        fclose(file);
        error_message = "No OTA partition is available.";
        ESP_LOGE(TAG, "%s", error_message.c_str());
        return false;
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        fclose(file);
        error_message = std::string("esp_ota_begin failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        return false;
    }

    uint8_t *buffer = static_cast<uint8_t *>(allocate_psram_preferred_buffer(4096));
    uint8_t *header_buffer = static_cast<uint8_t *>(allocate_psram_preferred_buffer(512));
    size_t header_size = 0;
    bool header_checked = false;
    size_t written_total = 0;
    int last_percent = -1;
    bool success = false;

    if ((buffer == nullptr) || (header_buffer == nullptr)) {
        error_message = "Unable to allocate firmware flashing buffers.";
        ESP_LOGE(TAG, "%s", error_message.c_str());
        goto cleanup_alloc;
    }

    while (true) {
        if (_firmwareCancelRequested) {
            error_message = "Firmware update canceled.";
            ESP_LOGI(TAG, "SD firmware flash canceled by user");
            goto cleanup;
        }

        const size_t read_bytes = fread(buffer, 1, 4096, file);
        if (read_bytes == 0) {
            if (feof(file)) {
                break;
            }
            error_message = "Reading firmware file failed.";
            ESP_LOGE(TAG, "%s", error_message.c_str());
            goto cleanup;
        }

        if (!header_checked && (header_size < 512)) {
            const size_t copy_bytes = std::min<size_t>(512 - header_size, read_bytes);
            memcpy(header_buffer + header_size, buffer, copy_bytes);
            header_size += copy_bytes;
            if (!validateFirmwareImageHeader(header_buffer, header_size, entry.label, error_message, header_checked)) {
                ESP_LOGE(TAG, "%s", error_message.c_str());
                goto cleanup;
            }
        }

        err = esp_ota_write(ota_handle, buffer, read_bytes);
        if (err != ESP_OK) {
            error_message = std::string("esp_ota_write failed: ") + esp_err_to_name(err);
            ESP_LOGE(TAG, "%s", error_message.c_str());
            goto cleanup;
        }

        written_total += read_bytes;
        if (entry.size_bytes > 0) {
            const int percent = static_cast<int>((written_total * 100U) / entry.size_bytes);
            if (percent != last_percent) {
                last_percent = percent;
                char phase[128] = {};
                snprintf(phase, sizeof(phase), "Flashing from SD... %d%%", percent);
                queueFirmwareUiUpdate(phase, percent, true, false);
            }
        }
    }

    if (_firmwareCancelRequested) {
        error_message = "Firmware update canceled.";
        ESP_LOGI(TAG, "SD firmware flash canceled before finalization");
        goto cleanup;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        error_message = std::string("esp_ota_end failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        goto cleanup_no_abort;
    }

    if (_firmwareCancelRequested) {
        error_message = "Firmware update canceled.";
        ESP_LOGI(TAG, "SD firmware flash canceled before boot partition switch");
        return false;
    }

    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        error_message = std::string("esp_ota_set_boot_partition failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        goto cleanup_no_abort;
    }

    success = true;
    ESP_LOGI(TAG, "SD firmware flash staged successfully: version='%s'", entry.version.c_str());

cleanup_no_abort:
    heap_caps_free(header_buffer);
    heap_caps_free(buffer);
    fclose(file);
    if (!success) {
        return false;
    }
    return true;

cleanup_alloc:
    heap_caps_free(header_buffer);
    heap_caps_free(buffer);
    fclose(file);
    return false;

cleanup:
    esp_ota_abort(ota_handle);
    heap_caps_free(header_buffer);
    heap_caps_free(buffer);
    fclose(file);
    return false;
}

bool AppSettings::flashFirmwareFromUrl(const FirmwareEntry_t &entry, std::string &error_message)
{
    ESP_LOGI(TAG, "Starting OTA firmware flash: label='%s' version='%s' url='%s'",
             entry.label.c_str(), entry.version.c_str(), entry.path_or_url.c_str());

    constexpr int kMaxHttpRedirects = 5;
    constexpr int kHttpClientBufferSize = 4096;
    struct RedirectCapture {
        std::string location;
    } redirect_capture;

    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    if (partition == nullptr) {
        error_message = "No OTA partition is available.";
        ESP_LOGE(TAG, "%s", error_message.c_str());
        return false;
    }

    esp_http_client_config_t config = {};
    config.url = entry.path_or_url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.buffer_size = kHttpClientBufferSize;
    config.buffer_size_tx = kHttpClientBufferSize;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.disable_auto_redirect = true;
    config.max_redirection_count = kMaxHttpRedirects;
    config.user_data = &redirect_capture;
    config.event_handler = [](esp_http_client_event_t *event) {
        if ((event == nullptr) || (event->user_data == nullptr)) {
            return ESP_OK;
        }

        auto *capture = static_cast<RedirectCapture *>(event->user_data);
        if ((event->event_id == HTTP_EVENT_ON_HEADER) && (event->header_key != nullptr) && (event->header_value != nullptr) &&
            (strcasecmp(event->header_key, "Location") == 0)) {
            capture->location = event->header_value;
        }
        return ESP_OK;
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        error_message = "Failed to create HTTP client.";
        ESP_LOGE(TAG, "%s", error_message.c_str());
        return false;
    }

    esp_http_client_set_header(client, "Accept", "application/octet-stream");
    esp_http_client_set_header(client, "User-Agent", "JC4880P443C-IW-Remote");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        error_message = std::string("Failed to open release asset: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        esp_http_client_cleanup(client);
        return false;
    }

    int header_status = esp_http_client_fetch_headers(client);
    int http_status = esp_http_client_get_status_code(client);
    int redirect_count = 0;
    while ((http_status >= 300) && (http_status < 400) && (redirect_count < kMaxHttpRedirects)) {
        if (redirect_capture.location.empty()) {
            error_message = "GitHub redirect response did not include a valid Location header.";
            ESP_LOGE(TAG, "%s HTTP status=%d", error_message.c_str(), http_status);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }

        ESP_LOGI(TAG,
                 "Following OTA redirect %d/%d: HTTP status=%d location='%s'",
                 redirect_count + 1,
                 kMaxHttpRedirects,
                 http_status,
                 redirect_capture.location.c_str());

        esp_http_client_close(client);
        err = esp_http_client_set_url(client, redirect_capture.location.c_str());
        if (err != ESP_OK) {
            error_message = std::string("Failed to set redirected asset URL: ") + esp_err_to_name(err);
            ESP_LOGE(TAG, "%s", error_message.c_str());
            esp_http_client_cleanup(client);
            return false;
        }

        redirect_capture.location.clear();
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            error_message = std::string("Failed to open redirected asset URL: ") + esp_err_to_name(err);
            ESP_LOGE(TAG, "%s", error_message.c_str());
            esp_http_client_cleanup(client);
            return false;
        }

        header_status = esp_http_client_fetch_headers(client);
        http_status = esp_http_client_get_status_code(client);
        ++redirect_count;
    }

    if ((header_status < 0) || ((http_status / 100) != 2)) {
        if ((http_status >= 300) && (http_status < 400)) {
            error_message = "GitHub asset download redirect limit reached.";
        } else {
            error_message = "GitHub asset download request failed.";
        }
        ESP_LOGE(TAG,
                 "%s HTTP status=%d header_status=%d redirects=%d",
                 error_message.c_str(),
                 http_status,
                 header_status,
                 redirect_count);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    const int64_t content_length = esp_http_client_get_content_length(client);
    esp_ota_handle_t ota_handle = 0;
    err = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        error_message = std::string("esp_ota_begin failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    uint8_t *buffer = static_cast<uint8_t *>(allocate_psram_preferred_buffer(4096));
    uint8_t *header_buffer = static_cast<uint8_t *>(allocate_psram_preferred_buffer(512));
    size_t header_size = 0;
    bool header_checked = false;
    int last_percent = -1;
    size_t written_total = 0;
    bool success = false;

    if ((buffer == nullptr) || (header_buffer == nullptr)) {
        error_message = "Unable to allocate OTA download buffers.";
        ESP_LOGE(TAG, "%s", error_message.c_str());
        goto ota_cleanup_alloc;
    }

    while (true) {
        if (_firmwareCancelRequested) {
            error_message = "Firmware update canceled.";
            ESP_LOGI(TAG, "OTA firmware flash canceled by user");
            goto ota_cleanup;
        }

        const int read_bytes = esp_http_client_read(client, reinterpret_cast<char *>(buffer), 4096);
        if (read_bytes < 0) {
            error_message = "Release asset download failed.";
            ESP_LOGE(TAG, "%s", error_message.c_str());
            goto ota_cleanup;
        }
        if (read_bytes == 0) {
            break;
        }

        if (!header_checked && (header_size < 512)) {
            const size_t copy_bytes = std::min<size_t>(512 - header_size, static_cast<size_t>(read_bytes));
            memcpy(header_buffer + header_size, buffer, copy_bytes);
            header_size += copy_bytes;
            if (!validateFirmwareImageHeader(header_buffer, header_size, entry.label, error_message, header_checked)) {
                ESP_LOGE(TAG, "%s", error_message.c_str());
                goto ota_cleanup;
            }
        }

        err = esp_ota_write(ota_handle, buffer, static_cast<size_t>(read_bytes));
        if (err != ESP_OK) {
            error_message = std::string("esp_ota_write failed: ") + esp_err_to_name(err);
            ESP_LOGE(TAG, "%s", error_message.c_str());
            goto ota_cleanup;
        }

        written_total += static_cast<size_t>(read_bytes);
        if (content_length > 0) {
            const int percent = static_cast<int>((written_total * 100ULL) / static_cast<uint64_t>(content_length));
            if (percent != last_percent) {
                last_percent = percent;
                char phase[128] = {};
                snprintf(phase, sizeof(phase), "Downloading and flashing... %d%%", percent);
                queueFirmwareUiUpdate(phase, percent, true, false);
            }
        }
    }

    if (_firmwareCancelRequested) {
        error_message = "Firmware update canceled.";
        ESP_LOGI(TAG, "OTA firmware flash canceled before finalization");
        goto ota_cleanup;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        error_message = std::string("esp_ota_end failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        goto ota_cleanup_no_abort;
    }

    if (_firmwareCancelRequested) {
        error_message = "Firmware update canceled.";
        ESP_LOGI(TAG, "OTA firmware flash canceled before boot partition switch");
        return false;
    }

    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        error_message = std::string("esp_ota_set_boot_partition failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", error_message.c_str());
        goto ota_cleanup_no_abort;
    }

    success = true;
    ESP_LOGI(TAG, "OTA firmware flash staged successfully: version='%s'", entry.version.c_str());

ota_cleanup_no_abort:
    heap_caps_free(header_buffer);
    heap_caps_free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (!success) {
        return false;
    }
    return true;

ota_cleanup_alloc:
    heap_caps_free(header_buffer);
    heap_caps_free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;

ota_cleanup:
    esp_ota_abort(ota_handle);
    heap_caps_free(header_buffer);
    heap_caps_free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
}

bool AppSettings::flashFirmwareEntry(const FirmwareEntry_t &entry, FirmwareUpdateSource_t source)
{
    auto *context = new FirmwareUpdateTaskContext{this, entry, source};
    if (context == nullptr) {
        setFirmwareStatus("Failed to allocate firmware update task.", true);
        ESP_LOGE(TAG, "Failed to allocate firmware update task context");
        return false;
    }

    _firmwareCancelRequested = false;
    _firmwareUpdateInProgress = true;
    updateOtaUpdateOverlayActions(false, false);
    refreshFirmwareUi();
    setFirmwareProgress(0, source == FIRMWARE_UPDATE_SOURCE_OTA ? "Preparing OTA update..." : "Preparing SD flash...");
    setFirmwareStatus(source == FIRMWARE_UPDATE_SOURCE_OTA ? "Starting OTA update..." : "Starting SD flash...");
    ESP_LOGI(TAG,
             "Queueing firmware update task: source=%s label='%s' version='%s' target='%s'",
             source == FIRMWARE_UPDATE_SOURCE_OTA ? "ota" : "sd",
             entry.label.c_str(),
             entry.version.c_str(),
             entry.path_or_url.c_str());

    // OTA finalization disables flash cache, so this worker must keep its stack in internal RAM.
    if (xTaskCreatePinnedToCore(firmwareUpdateTask,
                                "firmware_update",
                                FIRMWARE_UPDATE_TASK_STACK_SIZE,
                                context,
                                FIRMWARE_UPDATE_TASK_PRIORITY,
                                nullptr,
                                1) != pdPASS) {
        delete context;
        _firmwareUpdateInProgress = false;
        _firmwareCancelRequested = false;
        updateOtaUpdateOverlayActions(false, false);
        refreshFirmwareUi();
        setFirmwareStatus("Failed to start firmware update task.", true);
        ESP_LOGE(TAG, "Failed to start firmware update background task");
        return false;
    }

    return true;
}

bool AppSettings::startPreferredOtaUpdate(void)
{
    if (_firmwareUpdateInProgress) {
        setOtaUpdateProgressOverlayVisible(true);
        return true;
    }

    if (!hasOtaFlashSupport()) {
        setOtaUpdateProgressOverlayVisible(true);
        setFirmwareStatus("OTA flashing is blocked because the current partition table has no OTA slot.", true);
        setFirmwareProgress(0, "OTA update unavailable.", true);
        if (lv_obj_ready(_otaUpdateProgressCloseButton)) {
            lv_obj_clear_flag(_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
        }
        return false;
    }

    const int selected = findPreferredOtaEntryIndex(true);
    if ((selected < 0) || (static_cast<size_t>(selected) >= _otaFirmwareEntries.size()) || !_otaFirmwareEntries[selected].is_valid) {
        setOtaUpdateProgressOverlayVisible(true);
        setFirmwareStatus("No valid OTA release asset is available for update.", true);
        setFirmwareProgress(0, "OTA update unavailable.", true);
        if (lv_obj_ready(_otaUpdateProgressCloseButton)) {
            lv_obj_clear_flag(_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
        }
        return false;
    }

    setSelectedOtaFirmwareIndex(selected);
    if (!_otaDeferredAutoUpdateVersion.empty() && (_otaDeferredAutoUpdateVersion == _otaFirmwareEntries[selected].version)) {
        clearDeferredOtaSchedule(true);
    }
    setOtaUpdateProgressOverlayVisible(true);
    updateOtaUpdateOverlayActions(false, false);
    setFirmwareStatus("Starting OTA update...");
    setFirmwareProgress(0, "Preparing OTA update...");
    if (lv_obj_ready(_otaUpdateProgressCloseButton)) {
        lv_obj_add_flag(_otaUpdateProgressCloseButton, LV_OBJ_FLAG_HIDDEN);
    }

    return flashFirmwareEntry(_otaFirmwareEntries[selected], FIRMWARE_UPDATE_SOURCE_OTA);
}

void AppSettings::firmwareUpdateTask(void *arg)
{
    auto *context = static_cast<FirmwareUpdateTaskContext *>(arg);
    if ((context == nullptr) || (context->app == nullptr)) {
        delete context;
        vTaskDelete(nullptr);
        return;
    }

    AppSettings *app = context->app;
    const FirmwareEntry_t entry = context->entry;
    const FirmwareUpdateSource_t source = context->source;
    delete context;

    std::string error_message;
    const bool ok = (source == FIRMWARE_UPDATE_SOURCE_OTA)
                        ? app->flashFirmwareFromUrl(entry, error_message)
                        : app->flashFirmwareFromFile(entry, error_message);

    if (!ok) {
        const bool canceled = error_message == "Firmware update canceled.";
        ESP_LOGE(TAG,
                 "Firmware update failed: source=%s label='%s' version='%s' reason='%s'",
                 source == FIRMWARE_UPDATE_SOURCE_OTA ? "ota" : "sd",
                 entry.label.c_str(),
                 entry.version.c_str(),
                 error_message.empty() ? "Firmware update failed." : error_message.c_str());
        app->_firmwareCancelRequested = false;
        app->queueFirmwareUiUpdate(error_message.empty() ? "Firmware update failed." : error_message.c_str(),
                                   0,
                                   false,
                                   canceled ? false : true);
        vTaskDelete(nullptr);
        return;
    }

    app->_firmwareCancelRequested = false;
    ESP_LOGI(TAG,
             "Firmware update complete, rebooting: source=%s label='%s' version='%s'",
             source == FIRMWARE_UPDATE_SOURCE_OTA ? "ota" : "sd",
             entry.label.c_str(),
             entry.version.c_str());
    app->persistPendingReleaseNotes(entry);
    app->queueFirmwareUiUpdate("Firmware update complete. Rebooting...", 100, false, false);
    (void)bsp_extra_audio_play_system_sound(BSP_EXTRA_AUDIO_SYSTEM_SOUND_REBOOTING);
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}
