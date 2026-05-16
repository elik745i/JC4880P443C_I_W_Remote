void AppSettings::refreshZigbeeUi(void)
{
#if !CONFIG_JC4880_FEATURE_ZIGBEE
    return;
#endif
    if (!isUiActive()) {
        return;
    }

    const bool zigbee_enabled = _nvs_param_map[NVS_KEY_ZIGBEE_ENABLE] != 0;
    const int32_t preferred_channel = _nvs_param_map[NVS_KEY_ZIGBEE_CHANNEL];
    const int32_t permit_join_seconds = _nvs_param_map[NVS_KEY_ZIGBEE_PERMIT_JOIN];

    if (lv_obj_ready(_zigbeeEnableSwitch)) {
        if (zigbee_enabled) {
            lv_obj_add_state(_zigbeeEnableSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_zigbeeEnableSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_zigbeeChannelDropdown)) {
        lv_dropdown_set_selected(_zigbeeChannelDropdown,
                                 findDropdownIndexForValue(kZigbeeChannelOptions,
                                                           sizeof(kZigbeeChannelOptions) / sizeof(kZigbeeChannelOptions[0]),
                                                           preferred_channel));
    }

    if (lv_obj_ready(_zigbeePermitJoinDropdown)) {
        lv_dropdown_set_selected(_zigbeePermitJoinDropdown,
                                 findDropdownIndexForValue(kZigbeePermitJoinOptionsSec,
                                                           sizeof(kZigbeePermitJoinOptionsSec) / sizeof(kZigbeePermitJoinOptionsSec[0]),
                                                           permit_join_seconds));
    }

    if (lv_obj_ready(_zigbeeNameTextArea)) {
        char zigbee_name[32] = {0};
        if (!loadNvsStringParam(NVS_KEY_ZIGBEE_DEVICE_NAME, zigbee_name, sizeof(zigbee_name)) || (zigbee_name[0] == '\0')) {
            strlcpy(zigbee_name, kZigbeeDefaultDeviceName, sizeof(zigbee_name));
        }

        if (lv_obj_has_state(_zigbeeNameTextArea, LV_STATE_FOCUSED) == false) {
            lv_textarea_set_text(_zigbeeNameTextArea, zigbee_name);
        }
    }

    if (lv_obj_ready(_zigbeeRoleValueLabel)) {
        lv_label_set_text(_zigbeeRoleValueLabel,
                          "Coordinator on the ESP32-C6 coprocessor. The current firmware starts ZigBee natively on boot and keeps Wi-Fi/BLE coexistence enabled there.");
    }

    if (lv_obj_ready(_zigbeeConfigSummaryLabel)) {
        char zigbee_name[32] = {0};
        if (!loadNvsStringParam(NVS_KEY_ZIGBEE_DEVICE_NAME, zigbee_name, sizeof(zigbee_name)) || (zigbee_name[0] == '\0')) {
            strlcpy(zigbee_name, kZigbeeDefaultDeviceName, sizeof(zigbee_name));
        }

        std::string summary = std::string("Device name: ") + zigbee_name +
                              "\nPreferred channel: " + zigbeeChannelPreferenceLabel(preferred_channel) +
                              "\nPermit joining: " + zigbeePermitJoinLabel(permit_join_seconds);
        lv_label_set_text(_zigbeeConfigSummaryLabel, summary.c_str());
    }

    if (lv_obj_ready(_zigbeeInfoLabel)) {
        const std::string status = zigbee_enabled ?
            "ZigBee preferences are enabled on the P4. Current controls are host-side preferences only: the existing ESP32-C6 firmware does not expose live ZigBee RPC control, joined-device lists, PAN ID, or permit-join commands back to the P4 yet." :
            "ZigBee preference is disabled on the P4 UI. Note that the current ESP32-C6 release still starts ZigBee natively at boot when compiled with CONFIG_ZB_ENABLED, so this setting currently acts as a host-side preference gate for future integration.";
        lv_label_set_text(_zigbeeInfoLabel, status.c_str());
    }
}

void AppSettings::setZigbeeKeyboardVisible(bool visible)
{
    if (!isUiActive() || !lv_obj_ready(_zigbeeKeyboard) || !lv_obj_ready(_zigbeeNameTextArea)) {
        return;
    }

    if (visible) {
        lv_keyboard_set_textarea(_zigbeeKeyboard, _zigbeeNameTextArea);
        lv_obj_clear_flag(_zigbeeKeyboard, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_keyboard_set_textarea(_zigbeeKeyboard, nullptr);
        lv_obj_add_flag(_zigbeeKeyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

bool AppSettings::persistZigbeeNameFromUi(void)
{
    if (_zigbeeNameTextArea == nullptr) {
        return false;
    }

    std::string name = trim_copy(lv_textarea_get_text(_zigbeeNameTextArea));
    if (name.empty()) {
        name = kZigbeeDefaultDeviceName;
    }

    if (name.size() > 31) {
        name.resize(31);
    }

    lv_textarea_set_text(_zigbeeNameTextArea, name.c_str());
    return setNvsStringParam(NVS_KEY_ZIGBEE_DEVICE_NAME, name.c_str());
}