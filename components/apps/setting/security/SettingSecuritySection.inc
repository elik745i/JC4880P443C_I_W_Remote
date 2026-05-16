void AppSettings::refreshSecurityUi(void)
{
#if !CONFIG_JC4880_FEATURE_SECURITY
    return;
#endif
    if (!isUiActive()) {
        return;
    }

    const bool device_lock_enabled = device_security::isLockEnabled(device_security::LockType::Device);
    const bool settings_lock_enabled = device_security::isLockEnabled(device_security::LockType::Settings);

    if (lv_obj_ready(_securityDeviceLockSwitch)) {
        if (device_lock_enabled) {
            lv_obj_add_state(_securityDeviceLockSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_securityDeviceLockSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_securitySettingsLockSwitch)) {
        if (settings_lock_enabled) {
            lv_obj_add_state(_securitySettingsLockSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_securitySettingsLockSwitch, LV_STATE_CHECKED);
        }
    }
}

void AppSettings::handleSecurityToggleResult(device_security::LockType type, bool success)
{
    (void)type;
    (void)success;
    refreshSecurityUi();
}