void AppSettings::onFirmwareMenuClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    app->ensureFirmwareScreen();
    lv_scr_load_anim(app->_firmwareScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);

end:
    return;
}

void AppSettings::onFirmwareSdRefreshClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    app->scanSdFirmwareEntries();
    app->refreshFirmwareUi();
    if (app->_sdFirmwareEntries.empty()) {
        app->setFirmwareStatus("No valid .bin firmware images found on /sdcard or /sdcard/firmware.", true);
    }

end:
    return;
}

void AppSettings::onFirmwareOtaCheckClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (app->_firmwareUpdateInProgress) {
        app->setFirmwareStatus("A firmware update is already running.", true);
        goto end;
    }

    if (app->_firmwareOtaCheckInProgress) {
        goto end;
    }

    app->_firmwareOtaCheckInProgress = true;
    app->refreshFirmwareUi();
    app->setFirmwareOtaCheckOverlayVisible(true, "Checking GitHub for firmware releases...\nPlease wait.");
    lv_refr_now(nullptr);
    app->setFirmwareStatus("Checking GitHub releases...");
    app->setFirmwareProgress(0, "Querying GitHub releases...");
    if (!app->fetchGithubFirmwareEntries()) {
        app->setSelectedOtaFirmwareIndex(-1);
        app->_firmwareOtaCheckInProgress = false;
        app->setFirmwareOtaCheckOverlayVisible(false);
        app->refreshFirmwareUi();
        app->setFirmwareStatus("No OTA .bin assets were found in GitHub releases, or the request failed.", true);
        goto end;
    }

    app->setSelectedOtaFirmwareIndex(-1);
    app->_firmwareOtaCheckInProgress = false;
    app->setFirmwareOtaCheckOverlayVisible(false);
    app->refreshFirmwareUi();

end:
    return;
}

void AppSettings::onFirmwareSdFlashClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    if (app->_firmwareUpdateInProgress) {
        app->setFirmwareStatus("A firmware update is already running.", true);
        return;
    }

    if (!app->hasOtaFlashSupport()) {
        app->setFirmwareStatus("In-app flashing is blocked because the current partition table has no OTA slot.", true);
        return;
    }

    const uint16_t selected = (app->_firmwareSdDropdown != nullptr) ? lv_dropdown_get_selected(app->_firmwareSdDropdown) : 0;
    if ((selected >= app->_sdFirmwareEntries.size()) || !app->_sdFirmwareEntries[selected].is_valid) {
        app->setFirmwareStatus("Select a valid SD firmware image first.", true);
        return;
    }

    app->flashFirmwareEntry(app->_sdFirmwareEntries[selected], FIRMWARE_UPDATE_SOURCE_SD);
}

void AppSettings::onFirmwareOtaFlashClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    if (app->_firmwareUpdateInProgress) {
        app->setFirmwareStatus("A firmware update is already running.", true);
        return;
    }

    if (!app->hasOtaFlashSupport()) {
        app->setFirmwareStatus("OTA flashing is blocked because the current partition table has no OTA slot.", true);
        return;
    }

    const int selected = app->getSelectedOtaFirmwareIndex();
    if ((selected < 0) || (static_cast<size_t>(selected) >= app->_otaFirmwareEntries.size()) || !app->_otaFirmwareEntries[selected].is_valid) {
        app->setFirmwareStatus("Select a valid GitHub release asset first.", true);
        return;
    }

    app->flashFirmwareEntry(app->_otaFirmwareEntries[selected], FIRMWARE_UPDATE_SOURCE_OTA);
}

void AppSettings::onFirmwareOtaEntryCheckedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    if (target == nullptr) {
        return;
    }

    const uintptr_t index_value = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target));
    if (index_value == 0) {
        return;
    }

    const int index = static_cast<int>(index_value - 1);
    if (lv_obj_has_state(target, LV_STATE_CHECKED)) {
        app->setSelectedOtaFirmwareIndex(index);
    } else if (app->getSelectedOtaFirmwareIndex() == index) {
        app->setSelectedOtaFirmwareIndex(-1);
    }
    app->refreshFirmwareUi();
}

void AppSettings::onFirmwareSelectionChangedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    app->refreshFirmwareUi();

end:
    return;
}

void AppSettings::onFirmwareFactoryResetClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *msgbox = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    static const char *buttons[] = {"Reset", "Cancel", ""};
    msgbox = lv_msgbox_create(NULL,
                              "Factory Reset",
                              "Erase saved preferences and restore default settings? This clears Wi-Fi, display, audio, and timezone preferences.",
                              buttons,
                              false);
    lv_obj_center(msgbox);
    lv_obj_set_width(msgbox, 360);
    lv_obj_add_event_cb(msgbox, onFirmwareFactoryResetConfirmEventCallback, LV_EVENT_VALUE_CHANGED, app);

end:
    return;
}

void AppSettings::onFirmwareFactoryResetConfirmEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *msgbox = lv_event_get_current_target(e);
    const char *button_text = nullptr;
    const lv_event_code_t code = lv_event_get_code(e);

    if ((app == nullptr) || (msgbox == nullptr) || (code != LV_EVENT_VALUE_CHANGED)) {
        return;
    }

    button_text = lv_msgbox_get_active_btn_text(msgbox);
    if ((button_text != nullptr) && (strcmp(button_text, "Reset") == 0)) {
        if (!app->factoryResetPreferences()) {
            app->setFirmwareStatus("Factory reset failed. Preferences were not fully cleared.", true);
        }
    }

    lv_msgbox_close(msgbox);
}

void AppSettings::onOtaUpdateAvailablePopupEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *msgbox = lv_event_get_current_target(e);
    const lv_event_code_t code = lv_event_get_code(e);

    if (app == nullptr) {
        return;
    }

    if ((code == LV_EVENT_DELETE) && (app->_otaUpdateAvailableMsgbox == msgbox)) {
        app->_otaUpdateAvailableMsgbox = nullptr;
        return;
    }

    if ((code != LV_EVENT_VALUE_CHANGED) || (msgbox == nullptr)) {
        return;
    }

    const char *button_text = lv_msgbox_get_active_btn_text(msgbox);
    app->_otaUpdatePromptDismissedThisBoot = true;
    lv_msgbox_close_async(msgbox);

    if ((button_text != nullptr) && (strcmp(button_text, "Update") == 0)) {
        app->startPreferredOtaUpdate();
    }
}

void AppSettings::onFirmwareAutoUpdateSwitchValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *target = nullptr;
    bool enabled = false;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(target, end, "Invalid OTA auto update switch");

    enabled = (lv_obj_get_state(target) & LV_STATE_CHECKED) != 0;
    app->_nvs_param_map[NVS_KEY_OTA_AUTO_UPDATE] = enabled ? 1 : 0;
    app->setNvsParam(NVS_KEY_OTA_AUTO_UPDATE, enabled ? 1 : 0);
    if (!enabled) {
        app->clearDeferredOtaSchedule(true);
        app->updateOtaUpdateOverlayActions(false, false);
    }

end:
    return;
}

void AppSettings::onOtaUpdateInstallNowEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    app->updateOtaUpdateOverlayActions(false, false);
    app->_otaUpdatePromptDismissedThisBoot = true;
    app->startPreferredOtaUpdate();
}

void AppSettings::onOtaUpdateCancelEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    app->_otaUpdatePromptDismissedThisBoot = true;
    if (app->_firmwareUpdateInProgress) {
        app->_firmwareCancelRequested = true;
        app->setFirmwareStatus("Canceling firmware update...", false);
        app->setFirmwareProgress(0, "Canceling firmware update...", false);
        return;
    }

    app->updateOtaUpdateOverlayActions(false, false);
    app->setOtaUpdateProgressOverlayVisible(false);
    app->setFirmwareStatus("Automatic OTA start canceled.", false);
    app->setFirmwareProgress(0, "Automatic update canceled for now.", false);
}

void AppSettings::onOtaUpdateRescheduleEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    app->updateOtaUpdateOverlayActions(true, true);
    app->updateOtaRescheduleMinuteOptions();
    app->setFirmwareProgress(0, "Choose how long to delay automatic installation.", false);
}

void AppSettings::onOtaUpdateRescheduleHourChangedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    app->updateOtaRescheduleMinuteOptions();
}

void AppSettings::onOtaUpdateRescheduleApplyEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    const uint32_t delay_seconds = app->getSelectedOtaRescheduleDelaySeconds();
    if (delay_seconds == 0) {
        app->setFirmwareStatus("Select a delay longer than zero to reschedule.", true);
        app->setFirmwareProgress(0, "Reschedule requires a future time.", true);
        return;
    }

    const int selected = app->findPreferredOtaEntryIndex(true);
    if ((selected < 0) || (static_cast<size_t>(selected) >= app->_otaFirmwareEntries.size())) {
        app->setFirmwareStatus("No OTA release is available to reschedule.", true);
        app->setFirmwareProgress(0, "Reschedule unavailable.", true);
        return;
    }

    const FirmwareEntry_t &entry = app->_otaFirmwareEntries[selected];
    app->deferOtaUpdateForEntry(entry, delay_seconds);
    if (app->_firmwareUpdateInProgress) {
        app->_otaUpdatePromptDismissedThisBoot = true;
        app->_firmwareCancelRequested = true;
        app->updateOtaUpdateOverlayActions(false, false);
        app->setFirmwareStatus(std::string("Automatic OTA update deferred for ") +
                                   formatDelayMinutes(static_cast<int32_t>(delay_seconds / 60)) + ".",
                               false);
        app->setFirmwareProgress(0, "Canceling current update and applying the new schedule...", false);
        return;
    }

    app->updateOtaUpdateOverlayActions(false, false);
    app->setOtaUpdateProgressOverlayVisible(false);
    app->setFirmwareStatus(std::string("Automatic OTA update deferred for ") + formatDelayMinutes(static_cast<int32_t>(delay_seconds / 60)) + ".",
                           false);
    app->setFirmwareProgress(0, "Automatic update rescheduled.", false);
}

void AppSettings::onOtaUpdateProgressCloseEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    if (app->_firmwareUpdateInProgress) {
        return;
    }

    if (!app->_otaAutoUpdateAwaitingDecision) {
        return;
    }

    lv_obj_t *current_target = lv_event_get_current_target(e);
    if (current_target == app->_otaUpdateProgressOverlay) {
        lv_obj_t *target = lv_event_get_target(e);
        if (target != app->_otaUpdateProgressOverlay) {
            return;
        }
    }

    app->setOtaUpdateProgressOverlayVisible(false);
}
