#include <cstring>
#include <string>
#include "esp_log.h"
#include "esp_check.h"
#include "nvs.h"
#include "lvgl.h"
#include "esp_brookesia.hpp"
#include "device_security.hpp"

namespace device_security {
namespace {

static const char *TAG = "device_security";
static constexpr size_t kPinLength = 4;

enum class PromptMode : uint8_t {
    None = 0,
    SetPin,
    VerifyUnlock,
    VerifyDisable,
};

enum class KeyAction : intptr_t {
    Digit0 = 0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Backspace,
    Clear,
};

struct State {
    ESP_Brookesia_Phone *phone = nullptr;
    bool cache_loaded = false;
    bool device_lock_enabled = false;
    bool settings_lock_enabled = false;
    bool device_unlocked = false;
    lv_obj_t *backdrop = nullptr;
    lv_obj_t *title_label = nullptr;
    lv_obj_t *subtitle_label = nullptr;
    lv_obj_t *pin_label = nullptr;
    lv_obj_t *error_label = nullptr;
    PromptMode mode = PromptMode::None;
    LockType lock_type = LockType::Device;
    RequestCallback callback = nullptr;
    void *callback_user_data = nullptr;
    int pending_app_id = -1;
    bool allow_cancel = true;
    char pin_buffer[kPinLength + 1] = {0};
    size_t pin_length = 0;
};

State s_state;

const char *get_enabled_key(LockType type)
{
    return (type == LockType::Device) ? kDeviceLockEnabledKey : kSettingsLockEnabledKey;
}

const char *get_pin_key(LockType type)
{
    return (type == LockType::Device) ? kDeviceLockPinKey : kSettingsLockPinKey;
}

bool read_i32_with_default(const char *key, int32_t default_value, int32_t &value)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for %s: %s", key, esp_err_to_name(err));
        return false;
    }

    err = nvs_get_i32(handle, key, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value = default_value;
        err = nvs_set_i32(handle, key, value);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
    }

    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read %s: %s", key, esp_err_to_name(err));
        return false;
    }

    return true;
}

std::string read_pin(LockType type)
{
    nvs_handle_t handle = 0;
    char buffer[16] = {0};
    size_t required_size = sizeof(buffer);
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for PIN: %s", esp_err_to_name(err));
        return {};
    }

    err = nvs_get_str(handle, get_pin_key(type), buffer, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        buffer[0] = '\0';
        err = nvs_set_str(handle, get_pin_key(type), "");
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
    }

    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read PIN for %s", get_pin_key(type));
        return {};
    }

    return std::string(buffer);
}

bool write_lock_state(LockType type, bool enabled, const char *pin)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for lock state: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_i32(handle, get_enabled_key(type), enabled ? 1 : 0);
    if ((err == ESP_OK) && (pin != nullptr)) {
        err = nvs_set_str(handle, get_pin_key(type), pin);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist lock state: %s", esp_err_to_name(err));
        return false;
    }

    if (type == LockType::Device) {
        s_state.device_lock_enabled = enabled;
    } else {
        s_state.settings_lock_enabled = enabled;
    }

    return true;
}

void ensure_cache_loaded(void)
{
    if (s_state.cache_loaded) {
        return;
    }

    int32_t value = 0;
    read_i32_with_default(kDeviceLockEnabledKey, 0, value);
    s_state.device_lock_enabled = (value != 0);
    read_i32_with_default(kSettingsLockEnabledKey, 0, value);
    s_state.settings_lock_enabled = (value != 0);
    s_state.cache_loaded = true;
    s_state.device_unlocked = !s_state.device_lock_enabled;

    (void)read_pin(LockType::Device);
    (void)read_pin(LockType::Settings);
}

void update_pin_label(void)
{
    char masked[(kPinLength * 2) + 1] = {0};
    size_t offset = 0;
    for (size_t index = 0; index < kPinLength; ++index) {
        masked[offset++] = (index < s_state.pin_length) ? '*' : '_';
        if (index + 1 < kPinLength) {
            masked[offset++] = ' ';
        }
    }
    lv_label_set_text(s_state.pin_label, masked);
}

void clear_error(void)
{
    if (s_state.error_label != nullptr) {
        lv_label_set_text(s_state.error_label, "");
        lv_obj_add_flag(s_state.error_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void show_error(const char *message)
{
    if (s_state.error_label != nullptr) {
        lv_label_set_text(s_state.error_label, message);
        lv_obj_clear_flag(s_state.error_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void reset_entry(void)
{
    memset(s_state.pin_buffer, 0, sizeof(s_state.pin_buffer));
    s_state.pin_length = 0;
    clear_error();
    if (s_state.pin_label != nullptr) {
        update_pin_label();
    }
}

void dispatch_app_start(int app_id)
{
    if ((s_state.phone == nullptr) || (app_id < 0)) {
        return;
    }

    ESP_Brookesia_CoreAppEventData_t app_event_data = {
        .id = app_id,
        .type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START,
    };
    if (!s_state.phone->sendAppEvent(&app_event_data)) {
        ESP_LOGW(TAG, "Failed to dispatch deferred app launch for id %d", app_id);
    }
}

void finish_prompt(bool success)
{
    RequestCallback callback = s_state.callback;
    void *callback_user_data = s_state.callback_user_data;
    const PromptMode mode = s_state.mode;
    const int pending_app_id = s_state.pending_app_id;

    if (s_state.backdrop != nullptr) {
        lv_obj_del_async(s_state.backdrop);
    }

    reset_entry();

    s_state.backdrop = nullptr;
    s_state.title_label = nullptr;
    s_state.subtitle_label = nullptr;
    s_state.pin_label = nullptr;
    s_state.error_label = nullptr;
    s_state.mode = PromptMode::None;
    s_state.callback = nullptr;
    s_state.callback_user_data = nullptr;
    s_state.pending_app_id = -1;
    s_state.allow_cancel = true;

    if ((mode == PromptMode::VerifyUnlock) && success && (s_state.lock_type == LockType::Settings)) {
        dispatch_app_start(pending_app_id);
    }

    if (callback != nullptr) {
        callback(success, callback_user_data);
    }
}

bool verify_entered_pin(void)
{
    const std::string saved_pin = read_pin(s_state.lock_type);
    return (saved_pin.length() == kPinLength) && (saved_pin == s_state.pin_buffer);
}

void handle_pin_complete(void)
{
    switch (s_state.mode) {
    case PromptMode::SetPin:
        if (!write_lock_state(s_state.lock_type, true, s_state.pin_buffer)) {
            show_error("Failed to save PIN");
            reset_entry();
            return;
        }
        if (s_state.lock_type == LockType::Device) {
            s_state.device_unlocked = true;
        }
        finish_prompt(true);
        break;
    case PromptMode::VerifyUnlock:
        if (!verify_entered_pin()) {
            show_error("Incorrect PIN");
            reset_entry();
            return;
        }
        if (s_state.lock_type == LockType::Device) {
            s_state.device_unlocked = true;
        }
        finish_prompt(true);
        break;
    case PromptMode::VerifyDisable:
        if (!verify_entered_pin()) {
            show_error("Incorrect PIN");
            reset_entry();
            return;
        }
        if (!write_lock_state(s_state.lock_type, false, "")) {
            show_error("Failed to clear PIN");
            reset_entry();
            return;
        }
        if (s_state.lock_type == LockType::Device) {
            s_state.device_unlocked = true;
        }
        finish_prompt(true);
        break;
    case PromptMode::None:
    default:
        break;
    }
}

void on_cancel_clicked(lv_event_t *event)
{
    (void)event;
    finish_prompt(false);
}

void on_keypad_button_clicked(lv_event_t *event)
{
    const auto action = static_cast<KeyAction>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));

    clear_error();
    switch (action) {
    case KeyAction::Backspace:
        if (s_state.pin_length > 0) {
            s_state.pin_buffer[--s_state.pin_length] = '\0';
        }
        break;
    case KeyAction::Clear:
        reset_entry();
        return;
    default:
        if (s_state.pin_length < kPinLength) {
            s_state.pin_buffer[s_state.pin_length++] = static_cast<char>('0' + static_cast<int>(action));
            s_state.pin_buffer[s_state.pin_length] = '\0';
        }
        break;
    }

    update_pin_label();
    if (s_state.pin_length >= kPinLength) {
        handle_pin_complete();
    }
}

bool open_prompt(PromptMode mode, LockType type, const char *title, const char *subtitle, bool allow_cancel,
                 RequestCallback callback, void *user_data, int pending_app_id)
{
    if (s_state.backdrop != nullptr) {
        if (callback != nullptr) {
            callback(false, user_data);
        }
        return false;
    }

    lv_obj_t *parent = lv_layer_top();
    lv_obj_t *panel = nullptr;
    lv_obj_t *grid = nullptr;
    static const char *button_labels[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "<", "0", "CLR"};
    static const KeyAction button_actions[] = {
        KeyAction::Digit1, KeyAction::Digit2, KeyAction::Digit3,
        KeyAction::Digit4, KeyAction::Digit5, KeyAction::Digit6,
        KeyAction::Digit7, KeyAction::Digit8, KeyAction::Digit9,
        KeyAction::Backspace, KeyAction::Digit0, KeyAction::Clear,
    };

    s_state.mode = mode;
    s_state.lock_type = type;
    s_state.callback = callback;
    s_state.callback_user_data = user_data;
    s_state.pending_app_id = pending_app_id;
    s_state.allow_cancel = allow_cancel;

    s_state.backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(s_state.backdrop);
    lv_obj_set_size(s_state.backdrop, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_state.backdrop, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(s_state.backdrop, LV_OPA_70, 0);
    lv_obj_add_flag(s_state.backdrop, LV_OBJ_FLAG_CLICKABLE);

    panel = lv_obj_create(s_state.backdrop);
    lv_obj_set_size(panel, 380, 560);
    lv_obj_center(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(panel, 24, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_left(panel, 24, 0);
    lv_obj_set_style_pad_right(panel, 24, 0);
    lv_obj_set_style_pad_top(panel, 24, 0);
    lv_obj_set_style_pad_bottom(panel, 24, 0);

    s_state.title_label = lv_label_create(panel);
    lv_label_set_text(s_state.title_label, title);
    lv_obj_set_style_text_font(s_state.title_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_state.title_label, lv_color_hex(0x111827), 0);
    lv_obj_align(s_state.title_label, LV_ALIGN_TOP_MID, 0, 0);

    s_state.subtitle_label = lv_label_create(panel);
    lv_obj_set_width(s_state.subtitle_label, 320);
    lv_label_set_long_mode(s_state.subtitle_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_state.subtitle_label, subtitle);
    lv_obj_set_style_text_font(s_state.subtitle_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_state.subtitle_label, lv_color_hex(0x475569), 0);
    lv_obj_align_to(s_state.subtitle_label, s_state.title_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 14);

    s_state.pin_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_state.pin_label, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(s_state.pin_label, lv_color_hex(0x0F172A), 0);
    lv_obj_align_to(s_state.pin_label, s_state.subtitle_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 28);

    s_state.error_label = lv_label_create(panel);
    lv_obj_set_width(s_state.error_label, 300);
    lv_label_set_long_mode(s_state.error_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_state.error_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_state.error_label, lv_color_hex(0xDC2626), 0);
    lv_obj_align_to(s_state.error_label, s_state.pin_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);
    lv_obj_add_flag(s_state.error_label, LV_OBJ_FLAG_HIDDEN);

    grid = lv_obj_create(panel);
    lv_obj_set_size(grid, lv_pct(100), 280);
    lv_obj_align_to(grid, s_state.error_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (size_t index = 0; index < (sizeof(button_labels) / sizeof(button_labels[0])); ++index) {
        lv_obj_t *button = lv_btn_create(grid);
        lv_obj_set_size(button, 96, 60);
        lv_obj_set_style_radius(button, 18, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0xE2E8F0), 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0xCBD5E1), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_add_event_cb(button, on_keypad_button_clicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(button_actions[index])));

        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, button_labels[index]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x0F172A), 0);
        lv_obj_center(label);
    }

    if (allow_cancel) {
        lv_obj_t *cancel_button = lv_btn_create(panel);
        lv_obj_set_size(cancel_button, lv_pct(100), 54);
        lv_obj_align(cancel_button, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_radius(cancel_button, 18, 0);
        lv_obj_set_style_bg_color(cancel_button, lv_color_hex(0xCBD5E1), 0);
        lv_obj_set_style_bg_color(cancel_button, lv_color_hex(0x94A3B8), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(cancel_button, 0, 0);
        lv_obj_add_event_cb(cancel_button, on_cancel_clicked, LV_EVENT_CLICKED, nullptr);

        lv_obj_t *cancel_label = lv_label_create(cancel_button);
        lv_label_set_text(cancel_label, "Cancel");
        lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(cancel_label, lv_color_hex(0x0F172A), 0);
        lv_obj_center(cancel_label);
    }

    reset_entry();
    return true;
}

} // namespace

void init(ESP_Brookesia_Phone *phone)
{
    s_state.phone = phone;
    ensure_cache_loaded();
}

void promptBootUnlockIfNeeded(void)
{
    ensure_cache_loaded();
    if (!s_state.device_lock_enabled || s_state.device_unlocked || (s_state.backdrop != nullptr)) {
        return;
    }

    open_prompt(PromptMode::VerifyUnlock, LockType::Device, "Device Locked",
                "Enter the 4-digit device PIN to continue.", false, nullptr, nullptr, -1);
}

bool isLockEnabled(LockType type)
{
    ensure_cache_loaded();
    return (type == LockType::Device) ? s_state.device_lock_enabled : s_state.settings_lock_enabled;
}

bool isPromptActive(void)
{
    return (s_state.backdrop != nullptr);
}

void requestEnable(LockType type, RequestCallback callback, void *user_data)
{
    ensure_cache_loaded();
    if (isLockEnabled(type)) {
        if (callback != nullptr) {
            callback(true, user_data);
        }
        return;
    }

    open_prompt(PromptMode::SetPin, type,
                (type == LockType::Device) ? "Set Device PIN" : "Set Settings PIN",
                "Enter a new 4-digit PIN.", true, callback, user_data, -1);
}

void requestDisable(LockType type, RequestCallback callback, void *user_data)
{
    ensure_cache_loaded();
    if (!isLockEnabled(type)) {
        if (callback != nullptr) {
            callback(true, user_data);
        }
        return;
    }

    open_prompt(PromptMode::VerifyDisable, type,
                (type == LockType::Device) ? "Disable Device Lock" : "Disable Settings Lock",
                "Enter the current 4-digit PIN to disable this lock.", true, callback, user_data, -1);
}

bool request_settings_unlock_for_app(int app_id)
{
    ensure_cache_loaded();
    if (!s_state.settings_lock_enabled) {
        return false;
    }

    if ((s_state.device_lock_enabled && !s_state.device_unlocked) || (s_state.backdrop != nullptr)) {
        return true;
    }

    return open_prompt(PromptMode::VerifyUnlock, LockType::Settings, "Settings Locked",
                       "Enter the 4-digit settings PIN to open Settings.", true, nullptr, nullptr, app_id);
}

} // namespace device_security

extern "C" bool jc_security_handle_app_launch_request(int app_id, const char *app_name)
{
    if ((app_name == nullptr) || (strcmp(app_name, "Settings") != 0)) {
        return false;
    }
    if (!device_security::isLockEnabled(device_security::LockType::Settings)) {
        return false;
    }

    return device_security::request_settings_unlock_for_app(app_id);
}