#include "SettingWifiPrivate.hpp"

#include <cctype>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "../../system_ui_service.h"
#include "../ui/ui.h"

static const char TAG[] = "EUI_Setting";
static constexpr uint32_t kSettingScreenAnimTimeMs = 220;

void AppSettings::onKeyboardScreenSettingVerificationClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    lv_keyboard_set_textarea(target, ui_TextAreaScreenSettingVerificationPassword);

    if (code == LV_EVENT_PRESSED) {
        app->_wifiKeyboardPressedAction = WIFI_KEYBOARD_PRESSED_NONE;

        const uint16_t btn_id = lv_keyboard_get_selected_btn(target);
        const char *btn_text = (btn_id != LV_BTNMATRIX_BTN_NONE) ? lv_keyboard_get_btn_text(target, btn_id) : nullptr;
        if (btn_text != nullptr) {
            const lv_keyboard_mode_t mode = lv_keyboard_get_mode(target);
            if ((mode == LV_KEYBOARD_MODE_TEXT_LOWER) && (strcmp(btn_text, "ABC") == 0)) {
                app->_wifiKeyboardPressedAction = WIFI_KEYBOARD_PRESSED_SHIFT_FROM_LOWER;
            } else if ((mode == LV_KEYBOARD_MODE_TEXT_UPPER) && (strcmp(btn_text, "abc") == 0)) {
                app->_wifiKeyboardPressedAction = WIFI_KEYBOARD_PRESSED_SHIFT_FROM_UPPER;
            } else if ((strcmp(btn_text, "1#") == 0) || (strcmp(btn_text, LV_SYMBOL_KEYBOARD) == 0)) {
                app->_wifiKeyboardPressedAction = WIFI_KEYBOARD_PRESSED_RESET_STATE;
            } else {
                const bool is_single_character = (btn_text[0] != '\0') && (btn_text[1] == '\0');
                const bool is_alpha = is_single_character && std::isalpha(static_cast<unsigned char>(btn_text[0]));
                app->_wifiKeyboardPressedAction = is_alpha ? WIFI_KEYBOARD_PRESSED_ALPHA : WIFI_KEYBOARD_PRESSED_OTHER;
            }
        }
    }

    if (code == LV_EVENT_VALUE_CHANGED) {
        switch (app->_wifiKeyboardPressedAction) {
            case WIFI_KEYBOARD_PRESSED_SHIFT_FROM_LOWER:
                app->_wifiKeyboardCapsLockEnabled = false;
                app->_wifiKeyboardSingleShiftPending = true;
                lv_keyboard_set_mode(target, LV_KEYBOARD_MODE_TEXT_UPPER);
                break;
            case WIFI_KEYBOARD_PRESSED_SHIFT_FROM_UPPER:
                if (app->_wifiKeyboardSingleShiftPending && !app->_wifiKeyboardCapsLockEnabled) {
                    app->_wifiKeyboardCapsLockEnabled = true;
                    app->_wifiKeyboardSingleShiftPending = false;
                    lv_keyboard_set_mode(target, LV_KEYBOARD_MODE_TEXT_UPPER);
                } else {
                    app->_wifiKeyboardCapsLockEnabled = false;
                    app->_wifiKeyboardSingleShiftPending = false;
                    lv_keyboard_set_mode(target, LV_KEYBOARD_MODE_TEXT_LOWER);
                }
                break;
            case WIFI_KEYBOARD_PRESSED_RESET_STATE:
                app->_wifiKeyboardCapsLockEnabled = false;
                app->_wifiKeyboardSingleShiftPending = false;
                break;
            case WIFI_KEYBOARD_PRESSED_ALPHA:
                if (app->_wifiKeyboardSingleShiftPending && !app->_wifiKeyboardCapsLockEnabled) {
                    app->_wifiKeyboardSingleShiftPending = false;
                    lv_keyboard_set_mode(target, LV_KEYBOARD_MODE_TEXT_LOWER);
                }
                break;
            default:
                break;
        }

        app->_wifiKeyboardPressedAction = WIFI_KEYBOARD_PRESSED_NONE;
    }

    if (code == LV_EVENT_CANCEL) {
        app->setWifiKeyboardVisible(false);
    } else if (code == LV_EVENT_READY) {
        app->setWifiKeyboardVisible(false);
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
    }

end:
    return;
}

void AppSettings::onWifiPasswordToggleClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);

    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    app->updateWifiPasswordVisibility(!app->_isWifiPasswordVisible);
    app->setWifiKeyboardVisible(true);

end:
    return;
}

void AppSettings::setWifiKeyboardVisible(bool visible)
{
    if (!isUiActive() || !lv_obj_ready(ui_KeyboardScreenSettingVerification)) {
        return;
    }

    if (visible) {
        _wifiKeyboardCapsLockEnabled = false;
        _wifiKeyboardSingleShiftPending = false;
        _wifiKeyboardPressedAction = WIFI_KEYBOARD_PRESSED_NONE;
        lv_keyboard_set_textarea(ui_KeyboardScreenSettingVerification, ui_TextAreaScreenSettingVerificationPassword);
        lv_keyboard_set_mode(ui_KeyboardScreenSettingVerification, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_obj_clear_flag(ui_KeyboardScreenSettingVerification, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ui_KeyboardScreenSettingVerification);
        lv_obj_align(ui_KeyboardScreenSettingVerification, LV_ALIGN_BOTTOM_MID, 0, 0);
    } else {
        _wifiKeyboardCapsLockEnabled = false;
        _wifiKeyboardSingleShiftPending = false;
        _wifiKeyboardPressedAction = WIFI_KEYBOARD_PRESSED_NONE;
        lv_keyboard_set_mode(ui_KeyboardScreenSettingVerification, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_obj_add_flag(ui_KeyboardScreenSettingVerification, LV_OBJ_FLAG_HIDDEN);
    }
}

void AppSettings::updateWifiPasswordVisibility(bool visible)
{
    _isWifiPasswordVisible = visible;

    if (isUiActive() && lv_obj_ready(ui_TextAreaScreenSettingVerificationPassword)) {
        lv_textarea_set_password_mode(ui_TextAreaScreenSettingVerificationPassword, !visible);
    }

    if (isUiActive() && lv_obj_ready(_wifiPasswordToggleLabel)) {
        lv_label_set_text(_wifiPasswordToggleLabel, visible ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    }
}

void AppSettings::onSwitchPanelScreenSettingWiFiSwitchValueChangeEventCallback(lv_event_t *e)
{
    lv_state_t state = lv_obj_get_state(ui_SwitchPanelScreenSettingWiFiSwitch);

    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (state & LV_STATE_CHECKED) {
        app->_nvs_param_map[NVS_KEY_WIFI_ENABLE] = true;
        app->setNvsParam(NVS_KEY_WIFI_ENABLE, 1);
        if (st_wifi_ssid[0] != '\0') {
            esp_wifi_connect();
        }
    } else {
        app->_nvs_param_map[NVS_KEY_WIFI_ENABLE] = false;
        app->setNvsParam(NVS_KEY_WIFI_ENABLE, 0);
        if (app->_screen_index == UI_WIFI_SCAN_INDEX) {
            app->stopWifiScan();
            app->_suppressDisconnectRecovery = true;
            esp_err_t disconnect_err = esp_wifi_disconnect();
            if ((disconnect_err != ESP_OK) && (disconnect_err != ESP_ERR_WIFI_NOT_INIT) &&
                (disconnect_err != ESP_ERR_WIFI_NOT_STARTED) && (disconnect_err != ESP_ERR_WIFI_NOT_CONNECT)) {
                ESP_LOGW(TAG, "Failed to stop Wi-Fi connection while disabling Wi-Fi: %s", esp_err_to_name(disconnect_err));
                app->_suppressDisconnectRecovery = false;
            }
            xEventGroupClearBits(s_wifi_event_group,
                                 WIFI_EVENT_CONNECTED | WIFI_EVENT_CONNECTING | WIFI_EVENT_SCANING | WIFI_EVENT_SCAN_RUNNING);
            system_ui_service::set_wifi_connected(false);
        }
    }

end:
    return;
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
    app->updateWifiPasswordVisibility(false);
    app->setWifiKeyboardVisible(false);

    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_SCANING);

    esp_wifi_scan_stop();

end:
    return;
}