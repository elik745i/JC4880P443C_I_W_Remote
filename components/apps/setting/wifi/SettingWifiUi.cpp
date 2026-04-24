#include "SettingWifiPrivate.hpp"

#include <cctype>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "lvgl_input_helper.h"
#include "../../system_ui_service.h"
#include "../ui/ui.h"

static const char TAG[] = "EUI_Setting";
static constexpr uint32_t kSettingScreenAnimTimeMs = 220;
static constexpr const char *kWifiApDefaultSsid = "JC4880P443C Remote";

void AppSettings::setWifiApKeyboardVisible(bool visible, lv_obj_t *textarea)
{
    if (!isUiActive() || !lv_obj_ready(_wifiApKeyboard)) {
        return;
    }

    if (visible) {
        if (lv_obj_ready(textarea)) {
            _wifiApKeyboardTarget = textarea;
        }

        if (!lv_obj_ready(_wifiApKeyboardTarget)) {
            return;
        }
        lv_keyboard_set_textarea(_wifiApKeyboard, _wifiApKeyboardTarget);
        lv_keyboard_set_mode(_wifiApKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_obj_clear_flag(_wifiApKeyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_wifiApKeyboard);
        lv_obj_align(_wifiApKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    } else {
        lv_keyboard_set_textarea(_wifiApKeyboard, nullptr);
        lv_keyboard_set_mode(_wifiApKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_obj_add_flag(_wifiApKeyboard, LV_OBJ_FLAG_HIDDEN);
        _wifiApKeyboardTarget = nullptr;
    }
}

bool AppSettings::persistWifiApSettingsFromUi(bool apply_runtime)
{
    if (!lv_obj_ready(_wifiApSsidTextArea) || !lv_obj_ready(_wifiApPasswordTextArea)) {
        return false;
    }

    SavedWifiCredential credential = sanitizeWifiApCredential(lv_textarea_get_text(_wifiApSsidTextArea),
                                                              lv_textarea_get_text(_wifiApPasswordTextArea));
    if (credential.ssid.empty()) {
        credential.ssid = kWifiApDefaultSsid;
    }

    if (!credential.password.empty() && (credential.password.size() < 8)) {
        if (lv_obj_ready(_wifiApStatusLabel)) {
            lv_label_set_text(_wifiApStatusLabel, "AP password must be at least 8 characters, or leave it blank for an open hotspot.");
        }
        return false;
    }

    lv_textarea_set_text(_wifiApSsidTextArea, credential.ssid.c_str());
    lv_textarea_set_text(_wifiApPasswordTextArea, credential.password.c_str());

    snprintf(st_wifi_ap_ssid, sizeof(st_wifi_ap_ssid), "%s", credential.ssid.c_str());
    snprintf(st_wifi_ap_password, sizeof(st_wifi_ap_password), "%s", credential.password.c_str());

    bool ok = true;
    ok &= setNvsStringParam(NVS_KEY_WIFI_AP_SSID, credential.ssid.c_str());
    ok &= setNvsStringParam(NVS_KEY_WIFI_AP_PASSWORD, credential.password.c_str());
    if (!ok) {
        if (lv_obj_ready(_wifiApStatusLabel)) {
            lv_label_set_text(_wifiApStatusLabel, "Failed to save AP settings to storage.");
        }
        return false;
    }

    if (apply_runtime && _nvs_param_map[NVS_KEY_WIFI_ENABLE] && _nvs_param_map[NVS_KEY_WIFI_AP_ENABLE]) {
        const esp_err_t err = applyWifiOperatingMode(st_wifi_ssid[0] != '\0', "AP settings updated");
        if (err != ESP_OK) {
            if (lv_obj_ready(_wifiApStatusLabel)) {
                lv_label_set_text(_wifiApStatusLabel, "AP settings were saved, but the hotspot could not be restarted.");
            }
            return false;
        }
    }

    refreshWifiApUi();
    return true;
}

void AppSettings::refreshWifiApUi(void)
{
    if (!isUiActive()) {
        return;
    }

    const bool ap_enabled = _nvs_param_map[NVS_KEY_WIFI_AP_ENABLE] != 0;
    const bool wifi_enabled = _nvs_param_map[NVS_KEY_WIFI_ENABLE] != 0;
    const char *ap_ssid = (st_wifi_ap_ssid[0] != '\0') ? st_wifi_ap_ssid : kWifiApDefaultSsid;

    if (lv_obj_ready(_wifiApSwitch)) {
        if (ap_enabled) {
            lv_obj_add_state(_wifiApSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_wifiApSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_wifiApSsidTextArea) && (_wifiApKeyboardTarget != _wifiApSsidTextArea)) {
        lv_textarea_set_text(_wifiApSsidTextArea, ap_ssid);
    }

    if (lv_obj_ready(_wifiApPasswordTextArea) && (_wifiApKeyboardTarget != _wifiApPasswordTextArea)) {
        lv_textarea_set_text(_wifiApPasswordTextArea, st_wifi_ap_password);
    }

    if (lv_obj_ready(_wifiApStatusLabel)) {
        char detail[224] = {};
        if (ap_enabled && wifi_enabled) {
            snprintf(detail,
                     sizeof(detail),
                     st_wifi_ap_password[0] == '\0' ?
                         "Broadcasting \"%s\" as an open hotspot alongside station Wi-Fi." :
                         "Broadcasting \"%s\" alongside station Wi-Fi with WPA2 security.",
                     ap_ssid);
        } else if (ap_enabled) {
            snprintf(detail,
                     sizeof(detail),
                     "AP mode is armed for \"%s\". Turn Wi-Fi on to start the hotspot.",
                     ap_ssid);
        } else {
            snprintf(detail,
                     sizeof(detail),
                     "Enable AP mode to broadcast \"%s\" while keeping station Wi-Fi available.",
                     ap_ssid);
        }
        lv_label_set_text(_wifiApStatusLabel, detail);
    }
}

void AppSettings::onKeyboardScreenSettingVerificationClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (code == LV_EVENT_CANCEL) {
        app->setWifiKeyboardVisible(false);
        if (lv_obj_ready(ui_TextAreaScreenSettingVerificationPassword)) {
            lv_obj_clear_state(ui_TextAreaScreenSettingVerificationPassword, LV_STATE_FOCUSED);
        }
    } else if (code == LV_EVENT_READY) {
        app->setWifiKeyboardVisible(false);
        if (lv_obj_ready(ui_TextAreaScreenSettingVerificationPassword)) {
            lv_obj_clear_state(ui_TextAreaScreenSettingVerificationPassword, LV_STATE_FOCUSED);
        }
        app->launchWifiConnection(app->sanitizeWifiCredential(
                                      lv_label_get_text(ui_LabelScreenSettingVerificationSSID),
                                      lv_textarea_get_text(ui_TextAreaScreenSettingVerificationPassword)),
                                  true,
                                  true);
    }

end:
    return;
}

void AppSettings::onWifiPasswordFieldEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if ((code == LV_EVENT_FOCUSED) || (code == LV_EVENT_CLICKED)) {
        app->setWifiKeyboardVisible(true);
    } else if ((code == LV_EVENT_DEFOCUSED) || (code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        app->setWifiKeyboardVisible(false);
    }

end:
    return;
}

void AppSettings::setWifiKeyboardVisible(bool visible)
{
    if (!isUiActive() || !lv_obj_ready(ui_KeyboardScreenSettingVerification)) {
        return;
    }

    if (visible) {
        lv_keyboard_set_textarea(ui_KeyboardScreenSettingVerification, ui_TextAreaScreenSettingVerificationPassword);
        lv_keyboard_set_mode(ui_KeyboardScreenSettingVerification, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_obj_clear_flag(ui_KeyboardScreenSettingVerification, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ui_KeyboardScreenSettingVerification);
        lv_obj_align(ui_KeyboardScreenSettingVerification, LV_ALIGN_BOTTOM_MID, 0, 0);
    } else {
        lv_keyboard_set_textarea(ui_KeyboardScreenSettingVerification, nullptr);
        lv_keyboard_set_mode(ui_KeyboardScreenSettingVerification, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_obj_add_flag(ui_KeyboardScreenSettingVerification, LV_OBJ_FLAG_HIDDEN);
    }
}

void AppSettings::onSwitchPanelScreenSettingWiFiSwitchValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

    lv_state_t state = lv_obj_get_state(ui_SwitchPanelScreenSettingWiFiSwitch);
    const bool enabled = (state & LV_STATE_CHECKED) != 0;

    app->_nvs_param_map[NVS_KEY_WIFI_ENABLE] = enabled ? 1 : 0;
    app->setNvsParam(NVS_KEY_WIFI_ENABLE, enabled ? 1 : 0);

    if (enabled) {
        if ((app->_nvs_param_map[NVS_KEY_WIFI_AP_ENABLE] != 0) && !app->persistWifiApSettingsFromUi(false)) {
            app->_nvs_param_map[NVS_KEY_WIFI_AP_ENABLE] = 0;
            app->setNvsParam(NVS_KEY_WIFI_AP_ENABLE, 0);
            if (lv_obj_ready(app->_wifiApSwitch)) {
                lv_obj_clear_state(app->_wifiApSwitch, LV_STATE_CHECKED);
            }
        }
    }

    esp_err_t err = app->applyWifiOperatingMode(enabled && (st_wifi_ssid[0] != '\0'),
                                                enabled ? "Wi-Fi enabled" : "Wi-Fi disabled");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply Wi-Fi mode from master switch: %s", esp_err_to_name(err));
        if (enabled) {
            app->_nvs_param_map[NVS_KEY_WIFI_ENABLE] = 0;
            app->setNvsParam(NVS_KEY_WIFI_ENABLE, 0);
            lv_obj_clear_state(ui_SwitchPanelScreenSettingWiFiSwitch, LV_STATE_CHECKED);
        }
    }

    app->refreshWifiApUi();
}

void AppSettings::onSavedWifiDropdownClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    app->_savedWifiListExpanded = !app->_savedWifiListExpanded;
    app->refreshSavedWifiUi();
}

void AppSettings::onWifiScanClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    if (!app->_nvs_param_map[NVS_KEY_WIFI_ENABLE]) {
        ESP_LOGI(TAG, "Manual Wi-Fi scan requested while Wi-Fi is disabled; enabling Wi-Fi first");
        if (lv_obj_ready(ui_SwitchPanelScreenSettingWiFiSwitch) &&
            ((lv_obj_get_state(ui_SwitchPanelScreenSettingWiFiSwitch) & LV_STATE_CHECKED) == 0)) {
            lv_obj_add_state(ui_SwitchPanelScreenSettingWiFiSwitch, LV_STATE_CHECKED);
            lv_event_send(ui_SwitchPanelScreenSettingWiFiSwitch, LV_EVENT_VALUE_CHANGED, nullptr);
        } else {
            app->_nvs_param_map[NVS_KEY_WIFI_ENABLE] = true;
            app->setNvsParam(NVS_KEY_WIFI_ENABLE, 1);
        }
    }

    app->startWifiScan();
}

void AppSettings::onWifiApSwitchValueChangeEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    const bool enabled = (lv_obj_get_state(app->_wifiApSwitch) & LV_STATE_CHECKED) != 0;
    if (enabled && !app->persistWifiApSettingsFromUi(false)) {
        app->_nvs_param_map[NVS_KEY_WIFI_AP_ENABLE] = 0;
        app->setNvsParam(NVS_KEY_WIFI_AP_ENABLE, 0);
        if (lv_obj_ready(app->_wifiApSwitch)) {
            lv_obj_clear_state(app->_wifiApSwitch, LV_STATE_CHECKED);
        }
        app->refreshWifiApUi();
        return;
    }

    app->_nvs_param_map[NVS_KEY_WIFI_AP_ENABLE] = enabled ? 1 : 0;
    app->setNvsParam(NVS_KEY_WIFI_AP_ENABLE, enabled ? 1 : 0);

    if (app->_nvs_param_map[NVS_KEY_WIFI_ENABLE]) {
        esp_err_t err = app->applyWifiOperatingMode(st_wifi_ssid[0] != '\0', enabled ? "AP mode enabled" : "AP mode disabled");
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to apply AP mode change: %s", esp_err_to_name(err));
        }
    }

    app->refreshWifiApUi();
}

void AppSettings::onWifiApFieldEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    if (app == nullptr) {
        return;
    }

    if ((code == LV_EVENT_FOCUSED) || (code == LV_EVENT_CLICKED)) {
        app->setWifiApKeyboardVisible(true, lv_event_get_target(e));
    } else if ((code == LV_EVENT_DEFOCUSED) || (code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        app->setWifiApKeyboardVisible(false);
    }
}

void AppSettings::onWifiApSaveClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    app->persistWifiApSettingsFromUi(true);
    app->setWifiApKeyboardVisible(false);
    if (lv_obj_ready(app->_wifiApSsidTextArea)) {
        lv_obj_clear_state(app->_wifiApSsidTextArea, LV_STATE_FOCUSED);
    }
    if (lv_obj_ready(app->_wifiApPasswordTextArea)) {
        lv_obj_clear_state(app->_wifiApPasswordTextArea, LV_STATE_FOCUSED);
    }
}

void AppSettings::onWifiApKeyboardEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    if (app == nullptr) {
        return;
    }

    if ((code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        if (code == LV_EVENT_READY) {
            app->persistWifiApSettingsFromUi(true);
        } else {
            app->refreshWifiApUi();
        }

        app->setWifiApKeyboardVisible(false);
        if (lv_obj_ready(app->_wifiApSsidTextArea)) {
            lv_obj_clear_state(app->_wifiApSsidTextArea, LV_STATE_FOCUSED);
        }
        if (lv_obj_ready(app->_wifiApPasswordTextArea)) {
            lv_obj_clear_state(app->_wifiApPasswordTextArea, LV_STATE_FOCUSED);
        }
    }
}

void AppSettings::onWifiKeyboardBackdropClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *target = lv_event_get_target(e);
    if (app == nullptr) {
        return;
    }

    if ((target == app->_wifiApPanel) || (target == ui_ScreenSettingWiFi)) {
        app->setWifiApKeyboardVisible(false);
        if (lv_obj_ready(app->_wifiApSsidTextArea)) {
            lv_obj_clear_state(app->_wifiApSsidTextArea, LV_STATE_FOCUSED);
        }
        if (lv_obj_ready(app->_wifiApPasswordTextArea)) {
            lv_obj_clear_state(app->_wifiApPasswordTextArea, LV_STATE_FOCUSED);
        }
    }

    if (target == ui_ScreenSettingVerification) {
        app->setWifiKeyboardVisible(false);
        if (lv_obj_ready(ui_TextAreaScreenSettingVerificationPassword)) {
            lv_obj_clear_state(ui_TextAreaScreenSettingVerificationPassword, LV_STATE_FOCUSED);
        }
    }
}

void AppSettings::onConnectSavedWifiClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

    const size_t index = app->getSavedWifiRenderedIndexFromEventTarget(lv_event_get_target(e));
    if (index < app->_savedWifiRenderedCredentials.size()) {
        app->launchWifiConnection(app->_savedWifiRenderedCredentials[index], false, false);
    }
}

void AppSettings::onForgetSavedWifiClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    {
        const size_t index = app->getSavedWifiRenderedIndexFromEventTarget(lv_event_get_target(e));
        if (index < app->_savedWifiRenderedCredentials.size()) {
            app->forgetSavedWifiCredential(app->_savedWifiRenderedCredentials[index].ssid);
        }
    }

end:
    return;
}

void AppSettings::onButtonWifiListClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label_wifi_ssid = lv_obj_get_child(btn, 0);
    lv_area_t btn_click_area;
    lv_point_t point;

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");
    ESP_BROOKESIA_CHECK_NULL_GOTO(label_wifi_ssid, end, "Invalid SSID label");

    lv_obj_get_click_area(btn, &btn_click_area);
    lv_indev_get_point(lv_indev_get_act(), &point);
    if ((point.x < btn_click_area.x1) || (point.x > btn_click_area.x2) ||
        (point.y < btn_click_area.y1) || (point.y > btn_click_area.y2)) {
        return;
    }

    lv_scr_load_anim(ui_ScreenSettingVerification, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
    lv_label_set_text_fmt(ui_LabelScreenSettingVerificationSSID, "%s", lv_label_get_text(label_wifi_ssid));
    lv_textarea_set_text(ui_TextAreaScreenSettingVerificationPassword, "");
    jc4880_password_textarea_set_visibility(ui_TextAreaScreenSettingVerificationPassword, false);
    app->setWifiKeyboardVisible(false);

    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_SCANING);

    esp_wifi_scan_stop();

end:
    return;
}