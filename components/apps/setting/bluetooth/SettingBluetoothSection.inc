void AppSettings::refreshBluetoothUi(void)
{
#if !APP_SETTINGS_FEATURE_BLUETOOTH_MENU
    return;
#endif
    if (!isUiActive()) {
        return;
    }

    const bool bluetooth_enabled = _nvs_param_map[NVS_KEY_BLE_ENABLE] != 0;
    const bool delegated_to_joypad = bluetoothMenuDelegatesToJoypadBle();

    if (lv_obj_ready(ui_SwitchPanelScreenSettingBLESwitch)) {
        if (delegated_to_joypad) {
            lv_obj_clear_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_CHECKED);
            lv_obj_add_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_DISABLED);
        } else if (bluetooth_enabled) {
            lv_obj_add_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_CHECKED);
            lv_obj_clear_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_CHECKED);
            lv_obj_clear_state(ui_SwitchPanelScreenSettingBLESwitch, LV_STATE_DISABLED);
        }
    }

    if (lv_obj_ready(ui_SpinnerScreenSettingBLE)) {
        if (s_bleRuntimeState == BleRuntimeState::Starting) {
            lv_obj_clear_flag(ui_SpinnerScreenSettingBLE, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui_SpinnerScreenSettingBLE, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (lv_obj_ready(_bluetoothInfoLabel)) {
        const std::string status = bleStatusText(bluetooth_enabled);
        lv_label_set_text(_bluetoothInfoLabel, status.c_str());
    }

    if (lv_obj_ready(_bluetoothNameTextArea)) {
        char ble_name[32] = {0};
        if (!loadNvsStringParam(NVS_KEY_BLE_DEVICE_NAME, ble_name, sizeof(ble_name)) || (ble_name[0] == '\0')) {
            strlcpy(ble_name, kBleDefaultDeviceName, sizeof(ble_name));
        }

        if (!lv_obj_has_state(_bluetoothNameTextArea, LV_STATE_FOCUSED)) {
            lv_textarea_set_text(_bluetoothNameTextArea, ble_name);
        }

        if (delegated_to_joypad) {
            lv_obj_add_state(_bluetoothNameTextArea, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(_bluetoothNameTextArea, LV_STATE_DISABLED);
        }
    }

    if (lv_obj_ready(_bluetoothNameSaveButton)) {
        if (delegated_to_joypad) {
            lv_obj_add_state(_bluetoothNameSaveButton, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(_bluetoothNameSaveButton, LV_STATE_DISABLED);
        }
    }

    if (lv_obj_ready(_bluetoothScanButtonLabel)) {
        lv_label_set_text(_bluetoothScanButtonLabel,
                          delegated_to_joypad ? "Use Joypad Menu" : (s_bleScanInProgress ? "Stop Scan" : "Scan Nearby"));
    }

    if (lv_obj_ready(_bluetoothScanButton)) {
        if (delegated_to_joypad) {
            lv_obj_add_state(_bluetoothScanButton, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(_bluetoothScanButton, LV_STATE_DISABLED);
        }
    }

    if (lv_obj_ready(_bluetoothScanStatusLabel)) {
        if (delegated_to_joypad) {
            lv_label_set_text(_bluetoothScanStatusLabel,
                              "Open Settings > Joypad to enable Bluepad32, pair a controller, or manage discovery on the ESP32-C6.");
        } else if (!bluetooth_enabled) {
            lv_label_set_text(_bluetoothScanStatusLabel, "Enable BLE first to scan nearby devices.");
        } else {
            lv_label_set_text(_bluetoothScanStatusLabel, s_bleScanStatus.c_str());
        }
    }

    if (lv_obj_ready(_bluetoothScanResultsLabel)) {
        std::string results;
        if (delegated_to_joypad) {
            results = "The Joypad screen owns controller pairing and stored-controller selection when Joypad BLE is active.";
        } else if (s_bleScanResults.empty()) {
            results = bluetooth_enabled ? "No discovery results yet." : "Discovery is unavailable while BLE is off.";
        } else {
            for (size_t index = 0; index < s_bleScanResults.size(); ++index) {
                const auto &entry = s_bleScanResults[index];
                results += std::to_string(index + 1) + ". ";
                results += entry.name.empty() ? std::string("Unnamed device") : entry.name;
                results += "\n";
                results += entry.address + "  RSSI " + std::to_string(entry.rssi) + " dBm";
                if ((index + 1) < s_bleScanResults.size()) {
                    results += "\n\n";
                }
            }
        }
        lv_label_set_text(_bluetoothScanResultsLabel, results.c_str());
    }
}

void AppSettings::setBluetoothKeyboardVisible(bool visible)
{
    if (!isUiActive() || !lv_obj_ready(_bluetoothKeyboard) || !lv_obj_ready(_bluetoothNameTextArea)) {
        return;
    }

    if (visible) {
        lv_keyboard_set_textarea(_bluetoothKeyboard, _bluetoothNameTextArea);
        lv_obj_clear_flag(_bluetoothKeyboard, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_keyboard_set_textarea(_bluetoothKeyboard, nullptr);
        lv_obj_add_flag(_bluetoothKeyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

bool AppSettings::persistBluetoothNameFromUi(void)
{
    if (_bluetoothNameTextArea == nullptr) {
        return false;
    }

    std::string name = trim_copy(lv_textarea_get_text(_bluetoothNameTextArea));
    if (name.empty()) {
        name = kBleDefaultDeviceName;
    }

    if (name.size() > 31) {
        name.resize(31);
    }

    lv_textarea_set_text(_bluetoothNameTextArea, name.c_str());
    if (!setNvsStringParam(NVS_KEY_BLE_DEVICE_NAME, name.c_str())) {
        return false;
    }

    return bleUpdateConfiguredName(name) == ESP_OK;
}