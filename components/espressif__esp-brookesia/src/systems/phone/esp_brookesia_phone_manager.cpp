/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <cmath>
#include <vector>
#include "bsp/esp32_p4_function_ev_board.h"
#include "esp_brookesia_phone_manager.hpp"
#include "esp_brookesia_phone.hpp"

LV_IMG_DECLARE(airplane_png);
LV_IMG_DECLARE(sleep_png);
LV_IMG_DECLARE(unmuted_png);
LV_IMG_DECLARE(muted_png);
LV_IMG_DECLARE(mutedall_png);

extern "C" bool __attribute__((weak)) jc_security_handle_app_launch_request(int app_id, const char *app_name);

extern "C" {
#include "esp_heap_caps.h"
#include "esp_hosted_power_save.h"
#include "nvs.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_wifi.h"

typedef enum {
    AUDIO_PLAYER_STATE_IDLE = 0,
    AUDIO_PLAYER_STATE_PLAYING,
    AUDIO_PLAYER_STATE_PAUSE,
    AUDIO_PLAYER_STATE_SHUTDOWN,
} audio_player_state_t;

int bsp_extra_audio_media_volume_get(void);
int bsp_extra_audio_media_volume_set(int volume);
int bsp_extra_audio_system_volume_get(void);
int bsp_extra_audio_system_volume_set(int volume);
int bsp_extra_audio_play_system_notification(void);
}

#if !ESP_BROOKESIA_LOG_ENABLE_DEBUG_PHONE_MANAGER
#undef ESP_BROOKESIA_LOGD
#define ESP_BROOKESIA_LOGD(...)
#endif

using namespace std;

namespace {

static constexpr int kHomeSwipeAutoCloseInternalHeapPercent = 80;
static constexpr int kQuickAccessPanelRadius = 20;
static constexpr int kQuickAccessOverlayBg = 0x000000;
static constexpr int kQuickAccessPanelBg = 0x1B2533;
static constexpr int kQuickAccessCardBg = 0x243244;
static constexpr int kQuickAccessAccent = 0x7DD3FC;
static constexpr int kQuickAccessText = 0xF8FAFC;
static constexpr int kQuickAccessMutedText = 0xB6C2D1;
static constexpr int kQuickAccessShadow = 32;
static constexpr int kQuickAccessAnimTimeMs = 220;
static constexpr lv_opa_t kQuickAccessOverlayOpa = LV_OPA_50;
static constexpr lv_opa_t kQuickAccessPanelOpa = 220;
static constexpr int kQuickAccessAppListTopOffset = 72;
static constexpr int kQuickAccessAppFooterHeight = 64;
static constexpr int kQuickAccessRowBorder = 1;
static constexpr uint16_t kQuickAccessIconZoom = 128;
static constexpr int kQuickAccessIconSlotWidth = 28;
static constexpr int kQuickAccessMusicButtonWidth = 68;
static constexpr int kQuickAccessMusicButtonHeight = 34;
static constexpr int kQuickAccessMusicButtonGap = 6;
static constexpr uint32_t kQuickAccessRefreshMs = 250;
static constexpr int kQuickAccessPowerButtonSize = 88;
static constexpr int kQuickAccessPowerButtonGap = 12;
static constexpr const char *kQuickAccessSettingsAppName = "Settings";
static constexpr const char *kSettingsStorageNamespace = "storage";
static constexpr const char *kSettingsWifiEnableKey = "wifi_en";
static constexpr const char *kSettingsWifiApEnableKey = "wifi_ap_en";
static constexpr const char *kSettingsBleEnableKey = "ble_en";
static constexpr const char *kSettingsZigbeeEnableKey = "zb_en";
static constexpr const char *kSettingsAudioVolumeKey = "volume";
static constexpr const char *kSettingsSystemAudioVolumeKey = "sys_volume";
static constexpr int kQuickAccessActionApplyAirplaneRadioPreferences = 0x41525031;

static lv_obj_t *create_quick_access_symbol_button(lv_obj_t *parent, const char *symbol)
{
    lv_obj_t *button = lv_btn_create(parent);
    if (button == nullptr) {
        return nullptr;
    }

    lv_obj_set_size(button, kQuickAccessMusicButtonWidth, kQuickAccessMusicButtonHeight);
    lv_obj_set_style_radius(button, kQuickAccessMusicButtonHeight / 2, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x3B4B61), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(button);
    if (label == nullptr) {
        return button;
    }

    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, lv_color_hex(kQuickAccessText), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_center(label);

    return button;
}

static lv_obj_t *create_quick_access_power_button(lv_obj_t *parent, uint32_t bg_color, uint32_t pressed_color,
        uint32_t checked_color, uint32_t text_color, const char *symbol, bool checkable)
{
    lv_obj_t *button = lv_btn_create(parent);
    if (button == nullptr) {
        return nullptr;
    }

    lv_obj_set_size(button, kQuickAccessPowerButtonSize, kQuickAccessPowerButtonSize);
    lv_obj_set_style_radius(button, 24, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(bg_color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(checked_color), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 3, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(button, lv_color_hex(0xDBEAFE), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(button, 18, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(0x04070A), 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_40, 0);
    lv_obj_set_style_translate_y(button, 3, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    if (checkable) {
        lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
    }

    if (symbol != nullptr) {
        lv_obj_t *label = lv_label_create(button);
        if (label != nullptr) {
            lv_label_set_text(label, symbol);
            lv_obj_set_style_text_color(label, lv_color_hex(text_color), 0);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
            lv_obj_center(label);
        }
    }

    return button;
}

static void create_quick_access_image_icon(lv_obj_t *button, const void *icon_resource, uint16_t zoom = 128)
{
    if (icon_resource == nullptr) {
        return;
    }

    lv_obj_t *icon = lv_img_create(button);
    if (icon == nullptr) {
        return;
    }

    lv_img_set_src(icon, icon_resource);
    lv_img_set_zoom(icon, zoom);
    lv_obj_center(icon);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
}

static void enter_quick_access_deep_sleep(void)
{
    bsp_display_backlight_off();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    if (esp_hosted_power_save_enabled()) {
        if (esp_hosted_power_save_start(HOSTED_POWER_SAVE_TYPE_DEEP_SLEEP) == 0) {
            return;
        }
    }

    esp_deep_sleep_start();
}

static int get_internal_heap_used_percent()
{
    const size_t total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    if (total == 0) {
        return 0;
    }

    const size_t free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t used = (free <= total) ? (total - free) : 0;
    return static_cast<int>((used * 100U) / total);
}

} // namespace

ESP_Brookesia_PhoneManager::ESP_Brookesia_PhoneManager(ESP_Brookesia_Core &core_in, ESP_Brookesia_PhoneHome &home_in,
        const ESP_Brookesia_PhoneManagerData_t &data_in):
    ESP_Brookesia_CoreManager(core_in, core_in.getCoreData().manager),
    home(home_in),
    data(data_in),
    _flags{},
    _home_active_screen(ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAX),
    _app_launcher_gesture_dir(ESP_BROOKESIA_GESTURE_DIR_NONE),
    _navigation_bar_gesture_dir(ESP_BROOKESIA_GESTURE_DIR_NONE),
    _gesture(nullptr),
    _quick_access_overlay(nullptr),
    _quick_access_app_panel(nullptr),
    _quick_access_app_list(nullptr),
    _quick_access_close_all_button(nullptr),
    _quick_access_volume_panel(nullptr),
    _quick_access_media_volume_slider(nullptr),
    _quick_access_media_volume_value_label(nullptr),
    _quick_access_system_volume_slider(nullptr),
    _quick_access_system_volume_value_label(nullptr),
    _quick_access_restart_button(nullptr),
    _quick_access_shutdown_button(nullptr),
    _quick_access_sleep_button(nullptr),
    _quick_access_settings_button(nullptr),
    _quick_access_notification_button(nullptr),
    _quick_access_notification_icon(nullptr),
    _quick_access_airplane_button(nullptr),
    _quick_access_shutdown_confirm(nullptr),
    _quick_access_airplane_mode_enabled(false),
    _quick_access_airplane_saved_wifi_enabled(false),
    _quick_access_airplane_saved_wifi_ap_enabled(false),
    _quick_access_airplane_saved_ble_enabled(false),
    _quick_access_airplane_saved_zigbee_enabled(false),
    _quick_access_notification_mode(QuickAccessNotificationMode::SOUND_AND_VIBRATION),
    _quick_access_saved_media_volume(50),
    _quick_access_saved_system_volume(50),
    _quick_access_saved_vibration_enabled(true),
    _quick_access_panel_type(QuickAccessPanelType::NONE),
    _quick_access_close_button_app_id_map(),
    _quick_access_row_app_id_map(),
    _quick_access_action_button_map(),
    _quick_access_refresh_timer(nullptr),
    _quick_access_close_all_timer(nullptr),
    _quick_access_close_all_queue(),
    _recents_screen_drag_tan_threshold(0),
    _recents_screen_start_point{},
    _recents_screen_last_point{},
    _recents_screen_active_app(nullptr)
{
}

ESP_Brookesia_PhoneManager::~ESP_Brookesia_PhoneManager()
{
    ESP_BROOKESIA_LOGD("Destroy(0x%p)", this);
    if (!del()) {
        ESP_BROOKESIA_LOGE("Failed to delete");
    }
}
bool ESP_Brookesia_PhoneManager::calibrateData(const ESP_Brookesia_StyleSize_t screen_size, ESP_Brookesia_PhoneHome &home,
        ESP_Brookesia_PhoneManagerData_t &data)
{
    ESP_BROOKESIA_LOGD("Calibrate data");

    if (data.flags.enable_gesture) {
        ESP_BROOKESIA_CHECK_FALSE_RETURN(ESP_Brookesia_Gesture::calibrateData(screen_size, home, data.gesture), false,
                                         "Calibrate gesture data failed");
    }

    return true;
}

bool ESP_Brookesia_PhoneManager::begin(void)
{
    const ESP_Brookesia_RecentsScreen *recents_screen = home.getRecentsScreen();
    unique_ptr<ESP_Brookesia_Gesture> gesture = nullptr;
    lv_indev_t *touch = nullptr;

    ESP_BROOKESIA_LOGD("Begin(@0x%p)", this);
    ESP_BROOKESIA_CHECK_FALSE_RETURN(!checkInitialized(), false, "Already initialized");

    // Home
    lv_obj_add_event_cb(home.getMainScreen(), onHomeMainScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);

    // Gesture
    if (data.flags.enable_gesture) {
        // Get the touch device
        touch = _core.getTouchDevice();
        if (touch == nullptr) {
            ESP_BROOKESIA_LOGW("No touch device is set, try to use default touch device");

            touch = esp_brookesia_core_utils_get_input_dev(_core.getDisplayDevice(), LV_INDEV_TYPE_POINTER);
            ESP_BROOKESIA_CHECK_NULL_RETURN(touch, false, "No touch device is initialized");
            ESP_BROOKESIA_LOGW("Using default touch device(@0x%p)", touch);

            ESP_BROOKESIA_CHECK_FALSE_RETURN(_core.setTouchDevice(touch), false, "Core set touch device failed");
        }

        // Create and begin gesture
        gesture = make_unique<ESP_Brookesia_Gesture>(_core, data.gesture);
        ESP_BROOKESIA_CHECK_NULL_RETURN(gesture, false, "Create gesture failed");
        ESP_BROOKESIA_CHECK_FALSE_RETURN(gesture->begin(home.getSystemScreenObject()), false, "Gesture begin failed");
        ESP_BROOKESIA_CHECK_FALSE_RETURN(gesture->setMaskObjectVisible(false), false, "Hide mask object failed");
        ESP_BROOKESIA_CHECK_FALSE_RETURN(
            gesture->setIndicatorBarVisible(ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_LEFT, false),
            false, "Set left indicator bar visible failed"
        );
        ESP_BROOKESIA_CHECK_FALSE_RETURN(
            gesture->setIndicatorBarVisible(ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_RIGHT, false),
            false, "Set right indicator bar visible failed"
        );
        ESP_BROOKESIA_CHECK_FALSE_RETURN(
            gesture->setIndicatorBarVisible(ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_BOTTOM, true),
            false, "Set bottom indicator bar visible failed"
        );

        _flags.enable_gesture_navigation = true;
        // Navigation events
        lv_obj_add_event_cb(gesture->getEventObj(), onGestureNavigationPressingEventCallback,
                            gesture->getPressingEventCode(), this);
        lv_obj_add_event_cb(gesture->getEventObj(), onGestureNavigationReleaseEventCallback,
                            gesture->getReleaseEventCode(), this);
        // Mask object and indicator bar events
        lv_obj_add_event_cb(gesture->getEventObj(), onGestureMaskIndicatorPressingEventCallback,
                            gesture->getPressingEventCode(), this);
        lv_obj_add_event_cb(gesture->getEventObj(), onGestureMaskIndicatorReleaseEventCallback,
                            gesture->getReleaseEventCode(), this);
    }

    if (gesture != nullptr) {
        // App Launcher
        lv_obj_add_event_cb(gesture->getEventObj(), onAppLauncherGestureEventCallback,
                            gesture->getPressingEventCode(), this);
        lv_obj_add_event_cb(gesture->getEventObj(), onAppLauncherGestureEventCallback,
                            gesture->getReleaseEventCode(), this);

        // Navigation Bar
        if (home.getNavigationBar() != nullptr) {
            lv_obj_add_event_cb(gesture->getEventObj(), onNavigationBarGestureEventCallback,
                                gesture->getPressingEventCode(), this);
            lv_obj_add_event_cb(gesture->getEventObj(), onNavigationBarGestureEventCallback,
                                gesture->getReleaseEventCode(), this);
        }
    }

    // Recents Screen
    if (recents_screen != nullptr) {
        // Hide recents_screen by default
        ESP_BROOKESIA_CHECK_FALSE_RETURN(recents_screen->setVisible(false), false, "Recents screen set visible failed");
        _recents_screen_drag_tan_threshold = tan(data.recents_screen.drag_snapshot_angle_threshold * M_PI / 180);
        lv_obj_add_event_cb(recents_screen->getEventObject(), onRecentsScreenSnapshotDeletedEventCallback,
                            recents_screen->getSnapshotDeletedEventCode(), this);
        // Register gesture event
        if (gesture != nullptr) {
            ESP_BROOKESIA_LOGD("Enable recents_screen gesture");
            lv_obj_add_event_cb(gesture->getEventObj(), onRecentsScreenGesturePressEventCallback,
                                gesture->getPressEventCode(), this);
            lv_obj_add_event_cb(gesture->getEventObj(), onRecentsScreenGesturePressingEventCallback,
                                gesture->getPressingEventCode(), this);
            lv_obj_add_event_cb(gesture->getEventObj(), onRecentsScreenGestureReleaseEventCallback,
                                gesture->getReleaseEventCode(), this);
        }
    }

    if (gesture != nullptr) {
        _gesture = std::move(gesture);
    }

    ESP_BROOKESIA_CHECK_FALSE_RETURN(beginQuickAccessOverlay(), false, "Quick access overlay begin failed");

    _flags.is_initialized = true;

    ESP_BROOKESIA_CHECK_FALSE_RETURN(processHomeScreenChange(ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAIN, nullptr), false,
                                     "Process screen change failed");

    return true;
}

bool ESP_Brookesia_PhoneManager::del(void)
{
    lv_obj_t *temp_obj = nullptr;

    ESP_BROOKESIA_LOGD("Delete phone manager(0x%p)", this);

    if (!checkInitialized()) {
        return true;
    }

    if (_gesture != nullptr) {
        _gesture.reset();
    }
    _quick_access_close_button_app_id_map.clear();
    _quick_access_row_app_id_map.clear();
    _quick_access_action_button_map.clear();
    _quick_access_overlay = nullptr;
    _quick_access_app_panel = nullptr;
    _quick_access_app_list = nullptr;
    _quick_access_close_all_button = nullptr;
    _quick_access_volume_panel = nullptr;
    _quick_access_media_volume_slider = nullptr;
    _quick_access_media_volume_value_label = nullptr;
    _quick_access_system_volume_slider = nullptr;
    _quick_access_system_volume_value_label = nullptr;
    _quick_access_restart_button = nullptr;
    _quick_access_shutdown_button = nullptr;
    _quick_access_sleep_button = nullptr;
    _quick_access_settings_button = nullptr;
    _quick_access_notification_button = nullptr;
    _quick_access_notification_icon = nullptr;
    _quick_access_airplane_button = nullptr;
    _quick_access_shutdown_confirm = nullptr;
    _quick_access_airplane_mode_enabled = false;
    _quick_access_airplane_saved_wifi_enabled = false;
    _quick_access_airplane_saved_wifi_ap_enabled = false;
    _quick_access_airplane_saved_ble_enabled = false;
    _quick_access_airplane_saved_zigbee_enabled = false;
    _quick_access_notification_mode = QuickAccessNotificationMode::SOUND_AND_VIBRATION;
    _quick_access_saved_media_volume = 50;
    _quick_access_saved_system_volume = 50;
    _quick_access_saved_vibration_enabled = true;
    _quick_access_panel_type = QuickAccessPanelType::NONE;
    if (_quick_access_refresh_timer != nullptr) {
        lv_timer_del(_quick_access_refresh_timer);
        _quick_access_refresh_timer = nullptr;
    }
    if (_quick_access_close_all_timer != nullptr) {
        lv_timer_del(_quick_access_close_all_timer);
        _quick_access_close_all_timer = nullptr;
    }
    _quick_access_close_all_queue.clear();
    if (home.getRecentsScreen() != nullptr) {
        temp_obj = home.getRecentsScreen()->getEventObject();
        if (temp_obj != nullptr && lv_obj_is_valid(temp_obj)) {
            lv_obj_remove_event_cb(temp_obj, onRecentsScreenSnapshotDeletedEventCallback);
        }
    }
    _flags.is_initialized = false;
    _recents_screen_active_app = nullptr;

    return true;
}

bool ESP_Brookesia_PhoneManager::processAppRunExtra(ESP_Brookesia_CoreApp *app)
{
    ESP_Brookesia_PhoneApp *phone_app = static_cast<ESP_Brookesia_PhoneApp *>(app);

    ESP_BROOKESIA_CHECK_NULL_RETURN(phone_app, false, "Invalid phone app");
    ESP_BROOKESIA_LOGD("Process app(%p) run extra", phone_app);

    ESP_BROOKESIA_CHECK_FALSE_RETURN(processHomeScreenChange(ESP_BROOKESIA_PHONE_MANAGER_SCREEN_APP, phone_app), false,
                                     "Process screen change failed");

    return true;
}

bool ESP_Brookesia_PhoneManager::processAppStartPrepare(ESP_Brookesia_CoreApp *app)
{
    ESP_Brookesia_CoreApp *candidate = nullptr;

    ESP_BROOKESIA_CHECK_NULL_RETURN(app, false, "Invalid app");

    while ((getRunningAppCount() > 0) && (get_internal_heap_used_percent() >= kHomeSwipeAutoCloseInternalHeapPercent)) {
        candidate = getActiveApp();
        if ((candidate == nullptr) || (candidate == app)) {
            candidate = nullptr;
            for (int index = static_cast<int>(getRunningAppCount()) - 1; index >= 0; --index) {
                ESP_Brookesia_CoreApp *running_app = getRunningAppByIdenx(index);
                if ((running_app != nullptr) && (running_app != app)) {
                    candidate = running_app;
                    break;
                }
            }
        }

        if (candidate == nullptr) {
            break;
        }

        ESP_BROOKESIA_LOGW(
            "Internal heap is at %d%% before opening app(%d), closing running app(%d) to free memory",
            get_internal_heap_used_percent(), app->getId(), candidate->getId()
        );
        ESP_BROOKESIA_CHECK_FALSE_RETURN(processAppClose(candidate), false, "Close app for memory pressure failed");
    }

    return true;
}

bool ESP_Brookesia_PhoneManager::processAppResumeExtra(ESP_Brookesia_CoreApp *app)
{
    ESP_Brookesia_PhoneApp *phone_app = static_cast<ESP_Brookesia_PhoneApp *>(app);

    ESP_BROOKESIA_CHECK_NULL_RETURN(phone_app, false, "Invalid phone app");
    ESP_BROOKESIA_LOGD("Process app(%p) resume extra", phone_app);

    ESP_BROOKESIA_CHECK_FALSE_RETURN(processHomeScreenChange(ESP_BROOKESIA_PHONE_MANAGER_SCREEN_APP, phone_app), false,
                                     "Process screen change failed");

    return true;
}

bool ESP_Brookesia_PhoneManager::processAppCloseExtra(ESP_Brookesia_CoreApp *app)
{
    ESP_Brookesia_PhoneApp *phone_app = static_cast<ESP_Brookesia_PhoneApp *>(app);

    ESP_BROOKESIA_CHECK_NULL_RETURN(phone_app, false, "Invalid phone app");
    ESP_BROOKESIA_LOGD("Process app(%p) close extra", phone_app);

    if (getActiveApp() == app) {
        // Switch to the main screen to release the app resources
        ESP_BROOKESIA_CHECK_FALSE_RETURN(processHomeScreenChange(ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAIN, nullptr), false,
                                         "Process screen change failed");
        // If the recents_screen is visible, change back to the recents_screen
        if (home.getRecentsScreen()->checkVisible()) {
            ESP_BROOKESIA_CHECK_FALSE_RETURN(processHomeScreenChange(ESP_BROOKESIA_PHONE_MANAGER_SCREEN_RECENTS_SCREEN, nullptr), false,
                                             "Process screen change failed");
        }
    }

    return true;
}

bool ESP_Brookesia_PhoneManager::processHomeScreenChange(ESP_Brookesia_PhoneManagerScreen_t screen, void *param)
{
    ESP_BROOKESIA_LOGD("Process Screen Change(%d)", screen);
    ESP_BROOKESIA_CHECK_FALSE_RETURN(checkInitialized(), false, "Not initialized");
    ESP_BROOKESIA_CHECK_FALSE_RETURN(screen < ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAX, false, "Invalid screen");

    hideQuickAccessOverlay();

    ESP_BROOKESIA_CHECK_FALSE_RETURN(processStatusBarScreenChange(screen, param), false, "Process status bar failed");
    ESP_BROOKESIA_CHECK_FALSE_RETURN(processNavigationBarScreenChange(screen, param), false, "Process navigation bar failed");
    ESP_BROOKESIA_CHECK_FALSE_RETURN(processGestureScreenChange(screen, param), false, "Process gesture failed");

    if (screen == ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAIN) {
        ESP_BROOKESIA_CHECK_FALSE_RETURN(home.processMainScreenLoad(), false, "Home load main screen failed");
    }
    _home_active_screen = screen;

    return true;
}

bool ESP_Brookesia_PhoneManager::beginQuickAccessOverlay(void)
{
    lv_obj_t *parent = home.getSystemScreenObject();
    ESP_BROOKESIA_CHECK_NULL_RETURN(parent, false, "Invalid system screen object");

    if ((_quick_access_overlay != nullptr) && lv_obj_is_valid(_quick_access_overlay)) {
        return true;
    }

    _quick_access_overlay = lv_obj_create(parent);
    ESP_BROOKESIA_CHECK_NULL_RETURN(_quick_access_overlay, false, "Create quick access overlay failed");
    lv_obj_remove_style_all(_quick_access_overlay);
    lv_obj_set_size(_quick_access_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(_quick_access_overlay, lv_color_hex(kQuickAccessOverlayBg), 0);
    lv_obj_set_style_bg_opa(_quick_access_overlay, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(_quick_access_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_quick_access_overlay, onQuickAccessOverlayTouchEventCallback, LV_EVENT_CLICKED, this);

    _quick_access_app_panel = lv_obj_create(_quick_access_overlay);
    ESP_BROOKESIA_CHECK_NULL_RETURN(_quick_access_app_panel, false, "Create quick access app panel failed");
    lv_obj_set_size(_quick_access_app_panel, lv_pct(100), lv_pct(50));
    lv_obj_set_pos(_quick_access_app_panel, 0, getQuickAccessPanelHiddenY(QuickAccessPanelType::APPS));
    lv_obj_set_style_radius(_quick_access_app_panel, 0, 0);
    lv_obj_set_style_bg_color(_quick_access_app_panel, lv_color_hex(kQuickAccessPanelBg), 0);
    lv_obj_set_style_bg_grad_color(_quick_access_app_panel, lv_color_hex(0x2A3B50), 0);
    lv_obj_set_style_bg_grad_dir(_quick_access_app_panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(_quick_access_app_panel, kQuickAccessPanelOpa, 0);
    lv_obj_set_style_border_width(_quick_access_app_panel, 1, 0);
    lv_obj_set_style_border_color(_quick_access_app_panel, lv_color_hex(0x4B5E78), 0);
    lv_obj_set_style_shadow_width(_quick_access_app_panel, kQuickAccessShadow, 0);
    lv_obj_set_style_shadow_color(_quick_access_app_panel, lv_color_hex(0x04070A), 0);
    lv_obj_set_style_pad_all(_quick_access_app_panel, 16, 0);
    lv_obj_set_style_pad_row(_quick_access_app_panel, 12, 0);
    lv_obj_set_scrollbar_mode(_quick_access_app_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(_quick_access_app_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *app_title = lv_label_create(_quick_access_app_panel);
    lv_label_set_text(app_title, "Open Apps");
    lv_obj_set_style_text_color(app_title, lv_color_hex(kQuickAccessText), 0);
    lv_obj_set_style_text_font(app_title, &lv_font_montserrat_20, 0);
    lv_obj_align(app_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *app_subtitle = lv_label_create(_quick_access_app_panel);
    lv_label_set_text(app_subtitle, "Tap an app to reopen it");
    lv_obj_set_style_text_color(app_subtitle, lv_color_hex(kQuickAccessMutedText), 0);
    lv_obj_align(app_subtitle, LV_ALIGN_TOP_LEFT, 0, 28);

    _quick_access_close_all_button = lv_btn_create(_quick_access_app_panel);
    ESP_BROOKESIA_CHECK_NULL_RETURN(_quick_access_close_all_button, false, "Create quick access close-all button failed");
    lv_obj_set_size(_quick_access_close_all_button, lv_pct(100), 44);
    lv_obj_align(_quick_access_close_all_button, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(_quick_access_close_all_button, 18, 0);
    lv_obj_set_style_bg_color(_quick_access_close_all_button, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_bg_opa(_quick_access_close_all_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_quick_access_close_all_button, 0, 0);
    lv_obj_add_event_cb(_quick_access_close_all_button, onQuickAccessCloseAllEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *close_all_label = lv_label_create(_quick_access_close_all_button);
    lv_label_set_text(close_all_label, "Close All");
    lv_obj_set_style_text_color(close_all_label, lv_color_hex(kQuickAccessText), 0);
    lv_obj_center(close_all_label);

    _quick_access_app_list = lv_obj_create(_quick_access_app_panel);
    ESP_BROOKESIA_CHECK_NULL_RETURN(_quick_access_app_list, false, "Create quick access app list failed");
    lv_obj_set_size(_quick_access_app_list, lv_pct(100), lv_pct(100));
    lv_obj_align(_quick_access_app_list, LV_ALIGN_TOP_MID, 0, kQuickAccessAppListTopOffset);
    lv_obj_set_style_bg_opa(_quick_access_app_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_quick_access_app_list, 0, 0);
    lv_obj_set_style_pad_all(_quick_access_app_list, 0, 0);
    lv_obj_set_style_pad_row(_quick_access_app_list, 10, 0);
    lv_obj_set_style_pad_bottom(_quick_access_app_list, 10, 0);
    lv_obj_set_flex_flow(_quick_access_app_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_quick_access_app_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(_quick_access_app_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_quick_access_app_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_height(_quick_access_app_list, lv_obj_get_height(_quick_access_app_panel) - kQuickAccessAppListTopOffset - kQuickAccessAppFooterHeight);

    _quick_access_volume_panel = lv_obj_create(_quick_access_overlay);
    ESP_BROOKESIA_CHECK_NULL_RETURN(_quick_access_volume_panel, false, "Create quick access volume panel failed");
    lv_obj_set_size(_quick_access_volume_panel, lv_pct(100), lv_pct(50));
    lv_obj_set_pos(_quick_access_volume_panel, 0, getQuickAccessPanelHiddenY(QuickAccessPanelType::AUDIO));
    lv_obj_set_style_radius(_quick_access_volume_panel, 0, 0);
    lv_obj_set_style_bg_color(_quick_access_volume_panel, lv_color_hex(kQuickAccessPanelBg), 0);
    lv_obj_set_style_bg_grad_color(_quick_access_volume_panel, lv_color_hex(0x314256), 0);
    lv_obj_set_style_bg_grad_dir(_quick_access_volume_panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(_quick_access_volume_panel, kQuickAccessPanelOpa, 0);
    lv_obj_set_style_border_width(_quick_access_volume_panel, 1, 0);
    lv_obj_set_style_border_color(_quick_access_volume_panel, lv_color_hex(0x4B5E78), 0);
    lv_obj_set_style_shadow_width(_quick_access_volume_panel, kQuickAccessShadow, 0);
    lv_obj_set_style_shadow_color(_quick_access_volume_panel, lv_color_hex(0x04070A), 0);
    lv_obj_set_style_pad_all(_quick_access_volume_panel, 16, 0);
    lv_obj_add_flag(_quick_access_volume_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *volume_title = lv_label_create(_quick_access_volume_panel);
    lv_label_set_text(volume_title, "Quick Controls");
    lv_obj_set_style_text_color(volume_title, lv_color_hex(kQuickAccessText), 0);
    lv_obj_set_style_text_font(volume_title, &lv_font_montserrat_18, 0);
    lv_obj_align(volume_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *audio_hint = lv_label_create(_quick_access_volume_panel);
    lv_label_set_text(audio_hint, "Power and audio");
    lv_obj_set_style_text_color(audio_hint, lv_color_hex(kQuickAccessMutedText), 0);
    lv_obj_align(audio_hint, LV_ALIGN_TOP_LEFT, 0, 28);

    lv_obj_t *power_grid = lv_obj_create(_quick_access_volume_panel);
    ESP_BROOKESIA_CHECK_NULL_RETURN(power_grid, false, "Create quick access power grid failed");
    lv_obj_remove_style_all(power_grid);
    lv_obj_set_size(power_grid,
                    (kQuickAccessPowerButtonSize * 2) + kQuickAccessPowerButtonGap,
                    (kQuickAccessPowerButtonSize * 3) + (kQuickAccessPowerButtonGap * 2));
    lv_obj_align(power_grid, LV_ALIGN_TOP_LEFT, 0, 64);
    lv_obj_set_flex_flow(power_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(power_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(power_grid, 0, 0);
    lv_obj_set_style_pad_row(power_grid, kQuickAccessPowerButtonGap, 0);
    lv_obj_set_style_pad_column(power_grid, kQuickAccessPowerButtonGap, 0);
    lv_obj_clear_flag(power_grid, LV_OBJ_FLAG_SCROLLABLE);

    _quick_access_restart_button = create_quick_access_power_button(power_grid, 0x22C55E, 0x16A34A, 0x22C55E,
                                                                    kQuickAccessText, LV_SYMBOL_REFRESH, false);
    ESP_BROOKESIA_CHECK_NULL_RETURN(_quick_access_restart_button, false, "Create restart button failed");
    lv_obj_add_event_cb(_quick_access_restart_button, onQuickAccessPowerButtonEventCallback, LV_EVENT_CLICKED, this);

    _quick_access_shutdown_button = create_quick_access_power_button(power_grid, 0xEF4444, 0xDC2626, 0xEF4444,
                                                                     kQuickAccessText, LV_SYMBOL_POWER, false);
    ESP_BROOKESIA_CHECK_NULL_RETURN(_quick_access_shutdown_button, false, "Create shutdown button failed");
    lv_obj_add_event_cb(_quick_access_shutdown_button, onQuickAccessPowerButtonEventCallback, LV_EVENT_CLICKED, this);

    _quick_access_sleep_button = create_quick_access_power_button(power_grid, 0xFACC15, 0xEAB308, 0xFACC15,
                                                                  0x1F2937, nullptr, false);
    ESP_BROOKESIA_CHECK_NULL_RETURN(_quick_access_sleep_button, false, "Create sleep button failed");
    create_quick_access_image_icon(_quick_access_sleep_button, &sleep_png);
    lv_obj_add_event_cb(_quick_access_sleep_button, onQuickAccessPowerButtonEventCallback, LV_EVENT_CLICKED, this);

    _quick_access_airplane_button = create_quick_access_power_button(power_grid, 0x3B82F6, 0x2563EB, 0x1D4ED8,
                                                                     kQuickAccessText, nullptr, true);
    ESP_BROOKESIA_CHECK_NULL_RETURN(_quick_access_airplane_button, false, "Create airplane button failed");
    create_quick_access_image_icon(_quick_access_airplane_button, &airplane_png);
    lv_obj_add_event_cb(_quick_access_airplane_button, onQuickAccessPowerButtonEventCallback, LV_EVENT_VALUE_CHANGED, this);

    _quick_access_settings_button = create_quick_access_power_button(power_grid, 0x64748B, 0x475569, 0x64748B,
                                                                     kQuickAccessText, LV_SYMBOL_SETTINGS, false);
    ESP_BROOKESIA_CHECK_NULL_RETURN(_quick_access_settings_button, false, "Create settings button failed");
    lv_obj_add_event_cb(_quick_access_settings_button, onQuickAccessPowerButtonEventCallback, LV_EVENT_CLICKED, this);

    _quick_access_notification_button = create_quick_access_power_button(power_grid, 0x8B5CF6, 0x7C3AED, 0x8B5CF6,
                                                                         kQuickAccessText, nullptr, false);
    ESP_BROOKESIA_CHECK_NULL_RETURN(_quick_access_notification_button, false, "Create notification button failed");
    _quick_access_notification_icon = lv_img_create(_quick_access_notification_button);
    ESP_BROOKESIA_CHECK_NULL_RETURN(_quick_access_notification_icon, false, "Create notification icon failed");
    lv_obj_center(_quick_access_notification_icon);
    lv_obj_clear_flag(_quick_access_notification_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_quick_access_notification_icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_quick_access_notification_button, onQuickAccessPowerButtonEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t *audio_panel = lv_obj_create(_quick_access_volume_panel);
    ESP_BROOKESIA_CHECK_NULL_RETURN(audio_panel, false, "Create quick access audio panel failed");
    lv_obj_remove_style_all(audio_panel);
    lv_obj_set_size(audio_panel, lv_pct(50), lv_pct(100));
    lv_obj_align(audio_panel, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_clear_flag(audio_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *media_label = lv_label_create(audio_panel);
    lv_label_set_text(media_label, "Media");
    lv_obj_set_style_text_color(media_label, lv_color_hex(kQuickAccessText), 0);
    lv_obj_align(media_label, LV_ALIGN_TOP_LEFT, 16, 60);

    _quick_access_media_volume_value_label = lv_label_create(audio_panel);
    lv_obj_set_style_text_color(_quick_access_media_volume_value_label, lv_color_hex(kQuickAccessMutedText), 0);
    lv_obj_align(_quick_access_media_volume_value_label, LV_ALIGN_TOP_LEFT, 16, 84);

    _quick_access_media_volume_slider = lv_slider_create(audio_panel);
    ESP_BROOKESIA_CHECK_NULL_RETURN(_quick_access_media_volume_slider, false, "Create quick access media slider failed");
    lv_slider_set_range(_quick_access_media_volume_slider, 0, 100);
    lv_slider_set_mode(_quick_access_media_volume_slider, LV_SLIDER_MODE_NORMAL);
    lv_obj_set_size(_quick_access_media_volume_slider, 18, 176);
    lv_obj_align(_quick_access_media_volume_slider, LV_ALIGN_BOTTOM_LEFT, 46, -16);
    lv_obj_add_event_cb(_quick_access_media_volume_slider, onQuickAccessVolumeSliderEventCallback, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(_quick_access_media_volume_slider, onQuickAccessVolumeSliderEventCallback, LV_EVENT_RELEASED, this);

    lv_obj_t *system_label = lv_label_create(audio_panel);
    lv_label_set_text(system_label, "System");
    lv_obj_set_style_text_color(system_label, lv_color_hex(kQuickAccessText), 0);
    lv_obj_align(system_label, LV_ALIGN_TOP_RIGHT, -18, 60);

    _quick_access_system_volume_value_label = lv_label_create(audio_panel);
    lv_obj_set_style_text_color(_quick_access_system_volume_value_label, lv_color_hex(kQuickAccessMutedText), 0);
    lv_obj_align(_quick_access_system_volume_value_label, LV_ALIGN_TOP_RIGHT, -18, 84);

    _quick_access_system_volume_slider = lv_slider_create(audio_panel);
    ESP_BROOKESIA_CHECK_NULL_RETURN(_quick_access_system_volume_slider, false, "Create quick access system slider failed");
    lv_slider_set_range(_quick_access_system_volume_slider, 0, 100);
    lv_slider_set_mode(_quick_access_system_volume_slider, LV_SLIDER_MODE_NORMAL);
    lv_obj_set_size(_quick_access_system_volume_slider, 18, 176);
    lv_obj_align(_quick_access_system_volume_slider, LV_ALIGN_BOTTOM_RIGHT, -46, -16);
    lv_obj_add_event_cb(_quick_access_system_volume_slider, onQuickAccessVolumeSliderEventCallback, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(_quick_access_system_volume_slider, onQuickAccessVolumeSliderEventCallback, LV_EVENT_RELEASED, this);

    refreshQuickAccessVolumePanel();
    return true;
}

lv_obj_t *ESP_Brookesia_PhoneManager::getQuickAccessPanel(QuickAccessPanelType type) const
{
    switch (type) {
    case QuickAccessPanelType::APPS:
        return _quick_access_app_panel;
    case QuickAccessPanelType::AUDIO:
        return _quick_access_volume_panel;
    case QuickAccessPanelType::NONE:
    default:
        return nullptr;
    }
}

int ESP_Brookesia_PhoneManager::getQuickAccessPanelShownY(QuickAccessPanelType type) const
{
    switch (type) {
    case QuickAccessPanelType::APPS:
        return home.getData().status_bar.data.main.size.height;
    case QuickAccessPanelType::AUDIO:
        return home.getData().status_bar.data.main.size.height;
    case QuickAccessPanelType::NONE:
    default:
        return 0;
    }
}

int ESP_Brookesia_PhoneManager::getQuickAccessPanelHiddenY(QuickAccessPanelType type) const
{
    lv_obj_t *panel = getQuickAccessPanel(type);
    if ((panel == nullptr) || !lv_obj_is_valid(panel)) {
        return 0;
    }

    return getQuickAccessPanelShownY(type) - lv_obj_get_height(panel);
}

void ESP_Brookesia_PhoneManager::onQuickAccessAnimateY(void *var, int32_t value)
{
    lv_obj_t *obj = static_cast<lv_obj_t *>(var);
    if ((obj != nullptr) && lv_obj_is_valid(obj)) {
        lv_obj_set_y(obj, value);
    }
}

void ESP_Brookesia_PhoneManager::onQuickAccessAnimateBackdrop(void *var, int32_t value)
{
    ESP_Brookesia_PhoneManager *manager = static_cast<ESP_Brookesia_PhoneManager *>(var);
    if ((manager == nullptr) || (manager->_quick_access_overlay == nullptr) || !lv_obj_is_valid(manager->_quick_access_overlay)) {
        return;
    }

    lv_obj_set_style_bg_opa(manager->_quick_access_overlay, static_cast<lv_opa_t>(value), 0);
}

void ESP_Brookesia_PhoneManager::onQuickAccessHideAnimationReady(lv_anim_t *anim)
{
    ESP_Brookesia_PhoneManager *manager = static_cast<ESP_Brookesia_PhoneManager *>(anim->var);
    if (manager == nullptr) {
        return;
    }

    manager->hideQuickAccessOverlay(false);
}

void ESP_Brookesia_PhoneManager::animateQuickAccessHide(void)
{
    lv_obj_t *panel = getQuickAccessPanel(_quick_access_panel_type);
    if ((panel == nullptr) || !lv_obj_is_valid(panel) || (_quick_access_overlay == nullptr) || !lv_obj_is_valid(_quick_access_overlay)) {
        hideQuickAccessOverlay(false);
        return;
    }

    lv_anim_del(panel, onQuickAccessAnimateY);
    lv_anim_del(this, onQuickAccessAnimateBackdrop);

    lv_anim_t panel_anim;
    lv_anim_init(&panel_anim);
    lv_anim_set_var(&panel_anim, panel);
    lv_anim_set_exec_cb(&panel_anim, onQuickAccessAnimateY);
    lv_anim_set_values(&panel_anim, lv_obj_get_y(panel), getQuickAccessPanelHiddenY(_quick_access_panel_type));
    lv_anim_set_time(&panel_anim, kQuickAccessAnimTimeMs);
    lv_anim_set_path_cb(&panel_anim, lv_anim_path_ease_in);
    lv_anim_start(&panel_anim);

    lv_anim_t backdrop_anim;
    lv_anim_init(&backdrop_anim);
    lv_anim_set_var(&backdrop_anim, this);
    lv_anim_set_exec_cb(&backdrop_anim, onQuickAccessAnimateBackdrop);
    lv_anim_set_values(&backdrop_anim, lv_obj_get_style_bg_opa(_quick_access_overlay, 0), LV_OPA_TRANSP);
    lv_anim_set_time(&backdrop_anim, kQuickAccessAnimTimeMs);
    lv_anim_set_path_cb(&backdrop_anim, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&backdrop_anim, onQuickAccessHideAnimationReady);
    lv_anim_start(&backdrop_anim);
}

void ESP_Brookesia_PhoneManager::hideQuickAccessOverlay(bool animate)
{
    if ((_quick_access_overlay == nullptr) || !lv_obj_is_valid(_quick_access_overlay)) {
        return;
    }

    if (animate && (_quick_access_panel_type != QuickAccessPanelType::NONE) && !lv_obj_has_flag(_quick_access_overlay, LV_OBJ_FLAG_HIDDEN)) {
        animateQuickAccessHide();
        return;
    }

    if (_quick_access_refresh_timer != nullptr) {
        lv_timer_pause(_quick_access_refresh_timer);
    }

    lv_obj_add_flag(_quick_access_overlay, LV_OBJ_FLAG_HIDDEN);
    if ((_quick_access_app_panel != nullptr) && lv_obj_is_valid(_quick_access_app_panel)) {
        lv_obj_add_flag(_quick_access_app_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(_quick_access_app_panel, getQuickAccessPanelHiddenY(QuickAccessPanelType::APPS));
    }
    if ((_quick_access_volume_panel != nullptr) && lv_obj_is_valid(_quick_access_volume_panel)) {
        lv_obj_add_flag(_quick_access_volume_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(_quick_access_volume_panel, getQuickAccessPanelHiddenY(QuickAccessPanelType::AUDIO));
    }
    if ((_quick_access_shutdown_confirm != nullptr) && lv_obj_is_valid(_quick_access_shutdown_confirm)) {
        lv_msgbox_close(_quick_access_shutdown_confirm);
        _quick_access_shutdown_confirm = nullptr;
    }
    lv_obj_set_style_bg_opa(_quick_access_overlay, LV_OPA_TRANSP, 0);
    _quick_access_panel_type = QuickAccessPanelType::NONE;
}

void ESP_Brookesia_PhoneManager::refreshQuickAccessAppList(void)
{
    if ((_quick_access_app_list == nullptr) || !lv_obj_is_valid(_quick_access_app_list)) {
        return;
    }

    _quick_access_close_button_app_id_map.clear();
    _quick_access_row_app_id_map.clear();
    _quick_access_action_button_map.clear();
    _quick_access_detail_label_map.clear();
    _quick_access_detail_progress_bar_map.clear();
    lv_obj_clean(_quick_access_app_list);

    const int running_app_count = getRunningAppCount();
    if (running_app_count <= 0) {
        lv_obj_t *label = lv_label_create(_quick_access_app_list);
        lv_label_set_text(label, "No apps are currently open");
        lv_obj_set_style_text_color(label, lv_color_hex(kQuickAccessMutedText), 0);
        return;
    }

    for (int index = running_app_count - 1; index >= 0; --index) {
        ESP_Brookesia_CoreApp *app = getRunningAppByIdenx(index);
        if (app == nullptr) {
            continue;
        }

        ESP_Brookesia_PhoneApp *phone_app = static_cast<ESP_Brookesia_PhoneApp *>(app);
        const void *icon_resource = (phone_app != nullptr) ? phone_app->getQuickAccessIconResource() : app->getLauncherIcon().resource;
        const std::vector<ESP_Brookesia_PhoneQuickAccessActionData_t> quick_access_actions =
            (phone_app != nullptr) ? phone_app->getQuickAccessActions() : std::vector<ESP_Brookesia_PhoneQuickAccessActionData_t>{};
        const ESP_Brookesia_PhoneQuickAccessDetailData_t quick_access_detail =
            (phone_app != nullptr) ? phone_app->getQuickAccessDetail() : ESP_Brookesia_PhoneQuickAccessDetailData_t{};

        lv_obj_t *row = lv_obj_create(_quick_access_app_list);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 64);
        lv_obj_set_style_radius(row, 16, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(kQuickAccessCardBg), 0);
        lv_obj_set_style_bg_grad_color(row, lv_color_hex(0x314256), 0);
        lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        const bool is_active = (getActiveApp() == app);
        lv_obj_set_style_border_width(row, kQuickAccessRowBorder, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(is_active ? kQuickAccessAccent : 0x3B4D63), 0);
        lv_obj_set_style_pad_left(row, 14, 0);
        lv_obj_set_style_pad_right(row, 12, 0);
        lv_obj_set_style_pad_top(row, 8, 0);
        lv_obj_set_style_pad_bottom(row, 8, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *icon_button = lv_btn_create(row);
        lv_obj_set_size(icon_button, 44, 44);
        lv_obj_set_style_radius(icon_button, 14, 0);
        lv_obj_set_style_bg_color(icon_button, lv_color_hex(0x243244), 0);
        lv_obj_set_style_bg_opa(icon_button, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(icon_button, 0, 0);
        lv_obj_set_style_shadow_width(icon_button, 0, 0);
        lv_obj_clear_flag(icon_button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(icon_button, onQuickAccessAppRowEventCallback, LV_EVENT_CLICKED, this);
        _quick_access_row_app_id_map[icon_button] = app->getId();

        lv_obj_t *icon_slot = lv_obj_create(icon_button);
        lv_obj_remove_style_all(icon_slot);
        lv_obj_set_size(icon_slot, kQuickAccessIconSlotWidth, lv_pct(100));
        lv_obj_center(icon_slot);
        lv_obj_clear_flag(icon_slot, LV_OBJ_FLAG_SCROLLABLE);

        if (icon_resource != nullptr) {
            lv_obj_t *icon = lv_img_create(icon_slot);
            lv_img_set_src(icon, icon_resource);
            lv_img_set_zoom(icon, kQuickAccessIconZoom);
            lv_obj_center(icon);
        }

        const bool has_detail = !quick_access_detail.text.empty() || (quick_access_detail.progress_percent >= 0);
        lv_obj_t *detail_slot = lv_obj_create(row);
        lv_obj_remove_style_all(detail_slot);
        lv_obj_set_height(detail_slot, 32);
        lv_obj_set_flex_grow(detail_slot, 1);
        lv_obj_clear_flag(detail_slot, LV_OBJ_FLAG_SCROLLABLE);

        if (has_detail) {
            lv_obj_set_style_radius(detail_slot, 14, 0);
            lv_obj_set_style_bg_color(detail_slot, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_bg_opa(detail_slot, LV_OPA_70, 0);
            lv_obj_set_style_border_width(detail_slot, 1, 0);
            lv_obj_set_style_border_color(detail_slot, lv_color_hex(0x334155), 0);
            lv_obj_set_style_pad_left(detail_slot, 10, 0);
            lv_obj_set_style_pad_right(detail_slot, 10, 0);
            lv_obj_set_style_pad_top(detail_slot, 0, 0);
            lv_obj_set_style_pad_bottom(detail_slot, 0, 0);

            if (quick_access_detail.progress_percent >= 0) {
                lv_obj_t *progress_bar = lv_bar_create(detail_slot);
                lv_obj_set_size(progress_bar, lv_pct(100), lv_pct(100));
                lv_obj_center(progress_bar);
                lv_bar_set_range(progress_bar, 0, 100);
                lv_bar_set_value(progress_bar, quick_access_detail.progress_percent, LV_ANIM_OFF);
                lv_obj_set_style_radius(progress_bar, 14, 0);
                lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x0F172A), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(progress_bar, LV_OPA_70, LV_PART_MAIN);
                lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x38BDF8), LV_PART_INDICATOR);
                lv_obj_set_style_bg_grad_color(progress_bar, lv_color_hex(0x0EA5E9), LV_PART_INDICATOR);
                lv_obj_set_style_bg_grad_dir(progress_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
                lv_obj_set_style_radius(progress_bar, 14, LV_PART_INDICATOR);
                lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
                _quick_access_detail_progress_bar_map[app->getId()] = progress_bar;
            }

            if (!quick_access_detail.text.empty()) {
                lv_obj_t *detail_label = lv_label_create(detail_slot);
                lv_label_set_text(detail_label, quick_access_detail.text.c_str());
                lv_obj_set_width(detail_label, lv_pct(100));
                lv_obj_set_style_text_color(detail_label, lv_color_hex(kQuickAccessText), 0);
                lv_obj_set_style_text_font(detail_label, &lv_font_montserrat_14, 0);
                lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_LEFT, 0);
                if (quick_access_detail.scroll_text) {
                    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
                } else {
                    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_DOT);
                }
                lv_obj_center(detail_label);
                lv_obj_add_flag(detail_label, LV_OBJ_FLAG_IGNORE_LAYOUT);
                _quick_access_detail_label_map[app->getId()] = detail_label;
            }
        }

        if (!quick_access_actions.empty()) {
            lv_obj_t *control_bar = lv_obj_create(row);
            lv_obj_remove_style_all(control_bar);
            const int action_count = static_cast<int>(quick_access_actions.size());
            const int control_bar_width = (kQuickAccessMusicButtonWidth * action_count) +
                                          (kQuickAccessMusicButtonGap * (action_count - 1));
            lv_obj_set_size(control_bar, control_bar_width, kQuickAccessMusicButtonHeight);
            lv_obj_set_flex_flow(control_bar, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(control_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(control_bar, kQuickAccessMusicButtonGap, 0);
            lv_obj_clear_flag(control_bar, LV_OBJ_FLAG_SCROLLABLE);

            for (const ESP_Brookesia_PhoneQuickAccessActionData_t &action : quick_access_actions) {
                lv_obj_t *action_button = create_quick_access_symbol_button(control_bar, action.symbol);
                if (action_button == nullptr) {
                    continue;
                }

                if (!action.enabled) {
                    lv_obj_add_state(action_button, LV_STATE_DISABLED);
                }
                lv_obj_add_event_cb(action_button, onQuickAccessActionEventCallback, LV_EVENT_CLICKED, this);
                _quick_access_action_button_map[action_button] = {app->getId(), action.action_id};
            }
        }

        lv_obj_t *close_button = lv_btn_create(row);
        lv_obj_set_size(close_button, 70, 34);
        lv_obj_set_style_radius(close_button, 16, 0);
        lv_obj_set_style_bg_color(close_button, lv_color_hex(0x475569), 0);
        lv_obj_set_style_bg_opa(close_button, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(close_button, 0, 0);
        lv_obj_add_event_cb(close_button, onQuickAccessCloseAppEventCallback, LV_EVENT_CLICKED, this);
        _quick_access_close_button_app_id_map[close_button] = app->getId();

        lv_obj_t *close_label = lv_label_create(close_button);
        lv_label_set_text(close_label, "Close");
        lv_obj_set_style_text_color(close_label, lv_color_hex(kQuickAccessText), 0);
        lv_obj_center(close_label);
    }
}

void ESP_Brookesia_PhoneManager::refreshQuickAccessAppDetails(void)
{
    if ((_quick_access_app_list == nullptr) || !lv_obj_is_valid(_quick_access_app_list)) {
        return;
    }

    const int running_app_count = getRunningAppCount();
    for (int index = 0; index < running_app_count; ++index) {
        ESP_Brookesia_CoreApp *app = getRunningAppByIdenx(index);
        if (app == nullptr) {
            refreshQuickAccessAppList();
            return;
        }

        ESP_Brookesia_PhoneApp *phone_app = static_cast<ESP_Brookesia_PhoneApp *>(app);
        const ESP_Brookesia_PhoneQuickAccessDetailData_t quick_access_detail =
            (phone_app != nullptr) ? phone_app->getQuickAccessDetail() : ESP_Brookesia_PhoneQuickAccessDetailData_t{};

        auto label_it = _quick_access_detail_label_map.find(app->getId());
        auto progress_it = _quick_access_detail_progress_bar_map.find(app->getId());

        const bool needs_label = !quick_access_detail.text.empty();
        const bool has_label = (label_it != _quick_access_detail_label_map.end()) &&
                               (label_it->second != nullptr) && lv_obj_is_valid(label_it->second);
        if (needs_label != has_label) {
            refreshQuickAccessAppList();
            return;
        }

        const bool needs_progress = (quick_access_detail.progress_percent >= 0);
        const bool has_progress = (progress_it != _quick_access_detail_progress_bar_map.end()) &&
                                  (progress_it->second != nullptr) && lv_obj_is_valid(progress_it->second);
        if (needs_progress != has_progress) {
            refreshQuickAccessAppList();
            return;
        }

        if (has_label) {
            lv_obj_t *detail_label = label_it->second;
            const char *current_text = lv_label_get_text(detail_label);
            if ((current_text == nullptr) || (strcmp(current_text, quick_access_detail.text.c_str()) != 0)) {
                lv_label_set_text(detail_label, quick_access_detail.text.c_str());
            }

            const lv_label_long_mode_t desired_long_mode =
                quick_access_detail.scroll_text ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT;
            if (lv_label_get_long_mode(detail_label) != desired_long_mode) {
                lv_label_set_long_mode(detail_label, desired_long_mode);
            }
        }

        if (has_progress) {
            lv_obj_t *progress_bar = progress_it->second;
            if (lv_bar_get_value(progress_bar) != quick_access_detail.progress_percent) {
                lv_bar_set_value(progress_bar, quick_access_detail.progress_percent, LV_ANIM_OFF);
            }
        }
    }
}

void ESP_Brookesia_PhoneManager::refreshQuickAccessVolumePanel(void)
{
    if ((_quick_access_media_volume_slider == nullptr) || !lv_obj_is_valid(_quick_access_media_volume_slider) ||
        (_quick_access_media_volume_value_label == nullptr) || !lv_obj_is_valid(_quick_access_media_volume_value_label) ||
        (_quick_access_system_volume_slider == nullptr) || !lv_obj_is_valid(_quick_access_system_volume_slider) ||
        (_quick_access_system_volume_value_label == nullptr) || !lv_obj_is_valid(_quick_access_system_volume_value_label)) {
        return;
    }

    const int media_volume = bsp_extra_audio_media_volume_get();
    const int system_volume = bsp_extra_audio_system_volume_get();
    lv_slider_set_value(_quick_access_media_volume_slider, media_volume, LV_ANIM_OFF);
    lv_slider_set_value(_quick_access_system_volume_slider, system_volume, LV_ANIM_OFF);
    lv_label_set_text_fmt(_quick_access_media_volume_value_label, "%d%%", media_volume);
    lv_label_set_text_fmt(_quick_access_system_volume_value_label, "%d%%", system_volume);

    if ((_quick_access_notification_mode == QuickAccessNotificationMode::VIBRATION_ONLY) &&
        ((media_volume != 0) || (system_volume != 0))) {
        _quick_access_saved_media_volume = media_volume;
        _quick_access_saved_system_volume = system_volume;
        _quick_access_notification_mode = QuickAccessNotificationMode::SOUND_AND_VIBRATION;
    } else if ((_quick_access_notification_mode == QuickAccessNotificationMode::MUTED_ALL) &&
               ((media_volume != 0) || (system_volume != 0))) {
        _quick_access_saved_media_volume = media_volume;
        _quick_access_saved_system_volume = system_volume;
        _quick_access_saved_vibration_enabled = true;
        _quick_access_notification_mode = QuickAccessNotificationMode::SOUND_AND_VIBRATION;
    }

    refreshQuickAccessNotificationButton();

    if ((_quick_access_airplane_button != nullptr) && lv_obj_is_valid(_quick_access_airplane_button)) {
        if (_quick_access_airplane_mode_enabled) {
            lv_obj_add_state(_quick_access_airplane_button, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_quick_access_airplane_button, LV_STATE_CHECKED);
        }
    }
}

void ESP_Brookesia_PhoneManager::refreshQuickAccessNotificationButton(void)
{
    if ((_quick_access_notification_button == nullptr) || !lv_obj_is_valid(_quick_access_notification_button) ||
        (_quick_access_notification_icon == nullptr) || !lv_obj_is_valid(_quick_access_notification_icon)) {
        return;
    }

    const void *icon_resource = &unmuted_png;
    switch (_quick_access_notification_mode) {
    case QuickAccessNotificationMode::VIBRATION_ONLY:
        icon_resource = &muted_png;
        break;
    case QuickAccessNotificationMode::MUTED_ALL:
        icon_resource = &mutedall_png;
        break;
    case QuickAccessNotificationMode::SOUND_AND_VIBRATION:
    default:
        icon_resource = &unmuted_png;
        break;
    }

    lv_img_set_src(_quick_access_notification_icon, icon_resource);
    lv_img_set_zoom(_quick_access_notification_icon, kQuickAccessIconZoom);
    lv_obj_center(_quick_access_notification_icon);
}

void ESP_Brookesia_PhoneManager::cycleQuickAccessNotificationMode(void)
{
    const int media_volume = bsp_extra_audio_media_volume_get();
    const int system_volume = bsp_extra_audio_system_volume_get();

    switch (_quick_access_notification_mode) {
    case QuickAccessNotificationMode::SOUND_AND_VIBRATION:
        _quick_access_saved_media_volume = media_volume;
        _quick_access_saved_system_volume = system_volume;
        _quick_access_saved_vibration_enabled = true;
        bsp_extra_audio_media_volume_set(0);
        bsp_extra_audio_system_volume_set(0);
        _quick_access_notification_mode = QuickAccessNotificationMode::VIBRATION_ONLY;
        break;
    case QuickAccessNotificationMode::VIBRATION_ONLY:
        _quick_access_saved_vibration_enabled = false;
        _quick_access_notification_mode = QuickAccessNotificationMode::MUTED_ALL;
        break;
    case QuickAccessNotificationMode::MUTED_ALL:
    default:
        bsp_extra_audio_media_volume_set(_quick_access_saved_media_volume);
        bsp_extra_audio_system_volume_set(_quick_access_saved_system_volume);
        _quick_access_saved_vibration_enabled = true;
        _quick_access_notification_mode = QuickAccessNotificationMode::SOUND_AND_VIBRATION;
        break;
    }

    refreshQuickAccessVolumePanel();
}

void ESP_Brookesia_PhoneManager::showQuickAccessAppList(void)
{
    if ((_quick_access_app_panel != nullptr) && lv_obj_is_valid(_quick_access_app_panel) &&
        (_quick_access_app_list != nullptr) && lv_obj_is_valid(_quick_access_app_list) &&
        (_quick_access_close_all_button != nullptr) && lv_obj_is_valid(_quick_access_close_all_button)) {
        int list_height = 0;

        lv_obj_update_layout(_quick_access_app_panel);
        list_height = lv_obj_get_height(_quick_access_app_panel) - kQuickAccessAppListTopOffset - lv_obj_get_height(_quick_access_close_all_button) - 20;
        if (list_height < 48) {
            list_height = 48;
        }

        lv_obj_set_pos(_quick_access_app_list, 0, kQuickAccessAppListTopOffset);
        lv_obj_set_size(_quick_access_app_list, lv_pct(100), list_height);
    }

    refreshQuickAccessAppList();
    showQuickAccessOverlay(QuickAccessPanelType::APPS);
}

void ESP_Brookesia_PhoneManager::showQuickAccessVolumePanel(void)
{
    refreshQuickAccessVolumePanel();
    showQuickAccessOverlay(QuickAccessPanelType::AUDIO);
}

void ESP_Brookesia_PhoneManager::showQuickAccessOverlay(QuickAccessPanelType type)
{
    lv_obj_t *panel = nullptr;
    lv_obj_t *other_panel = nullptr;

    if ((_quick_access_overlay == nullptr) || !lv_obj_is_valid(_quick_access_overlay)) {
        return;
    }

    if ((_quick_access_panel_type == type) && !lv_obj_has_flag(_quick_access_overlay, LV_OBJ_FLAG_HIDDEN)) {
        hideQuickAccessOverlay(true);
        return;
    }

    panel = getQuickAccessPanel(type);
    other_panel = getQuickAccessPanel((type == QuickAccessPanelType::APPS) ? QuickAccessPanelType::AUDIO : QuickAccessPanelType::APPS);
    if ((panel == nullptr) || !lv_obj_is_valid(panel)) {
        return;
    }

    lv_anim_del(panel, onQuickAccessAnimateY);
    lv_anim_del(this, onQuickAccessAnimateBackdrop);
    if ((other_panel != nullptr) && lv_obj_is_valid(other_panel)) {
        lv_anim_del(other_panel, onQuickAccessAnimateY);
        lv_obj_add_flag(other_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(other_panel, getQuickAccessPanelHiddenY((type == QuickAccessPanelType::APPS) ? QuickAccessPanelType::AUDIO : QuickAccessPanelType::APPS));
    }

    lv_obj_clear_flag(_quick_access_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(panel, getQuickAccessPanelHiddenY(type));
    lv_obj_move_foreground(_quick_access_overlay);
    if ((home.getStatusBar() != nullptr) && (home.getStatusBar()->getMainObject() != nullptr) &&
        lv_obj_is_valid(home.getStatusBar()->getMainObject())) {
        lv_obj_move_foreground(home.getStatusBar()->getMainObject());
    }
    _quick_access_panel_type = type;

    if (_quick_access_refresh_timer == nullptr) {
        _quick_access_refresh_timer = lv_timer_create(onQuickAccessRefreshTimerCallback, kQuickAccessRefreshMs, this);
    }
    if (_quick_access_refresh_timer != nullptr) {
        if (type == QuickAccessPanelType::APPS) {
            lv_timer_resume(_quick_access_refresh_timer);
            lv_timer_ready(_quick_access_refresh_timer);
        } else {
            lv_timer_pause(_quick_access_refresh_timer);
        }
    }

    lv_anim_t panel_anim;
    lv_anim_init(&panel_anim);
    lv_anim_set_var(&panel_anim, panel);
    lv_anim_set_exec_cb(&panel_anim, onQuickAccessAnimateY);
    lv_anim_set_values(&panel_anim, getQuickAccessPanelHiddenY(type), getQuickAccessPanelShownY(type));
    lv_anim_set_time(&panel_anim, kQuickAccessAnimTimeMs);
    lv_anim_set_path_cb(&panel_anim, lv_anim_path_ease_out);
    lv_anim_start(&panel_anim);

    lv_anim_t backdrop_anim;
    lv_anim_init(&backdrop_anim);
    lv_anim_set_var(&backdrop_anim, this);
    lv_anim_set_exec_cb(&backdrop_anim, onQuickAccessAnimateBackdrop);
    lv_anim_set_values(&backdrop_anim, lv_obj_get_style_bg_opa(_quick_access_overlay, 0), kQuickAccessOverlayOpa);
    lv_anim_set_time(&backdrop_anim, kQuickAccessAnimTimeMs);
    lv_anim_set_path_cb(&backdrop_anim, lv_anim_path_ease_out);
    lv_anim_start(&backdrop_anim);
}

void ESP_Brookesia_PhoneManager::onQuickAccessRefreshTimerCallback(lv_timer_t *timer)
{
    ESP_Brookesia_PhoneManager *manager = (timer != nullptr) ? static_cast<ESP_Brookesia_PhoneManager *>(timer->user_data) : nullptr;
    if (manager == nullptr) {
        return;
    }

    if ((manager->_quick_access_panel_type != QuickAccessPanelType::APPS) ||
        (manager->_quick_access_overlay == nullptr) ||
        !lv_obj_is_valid(manager->_quick_access_overlay) ||
        lv_obj_has_flag(manager->_quick_access_overlay, LV_OBJ_FLAG_HIDDEN)) {
        if (manager->_quick_access_refresh_timer != nullptr) {
            lv_timer_pause(manager->_quick_access_refresh_timer);
        }
        return;
    }

    manager->refreshQuickAccessAppDetails();
}

void ESP_Brookesia_PhoneManager::onQuickAccessOverlayTouchEventCallback(lv_event_t *event)
{
    ESP_Brookesia_PhoneManager *manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");

    if (lv_event_get_target(event) == manager->_quick_access_overlay) {
        manager->hideQuickAccessOverlay(true);
    }
}

void ESP_Brookesia_PhoneManager::onQuickAccessAppRowEventCallback(lv_event_t *event)
{
    ESP_Brookesia_PhoneManager *manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");

    lv_obj_t *row = lv_event_get_target(event);
    auto it = manager->_quick_access_row_app_id_map.find(row);
    ESP_BROOKESIA_CHECK_FALSE_EXIT(it != manager->_quick_access_row_app_id_map.end(), "Unknown quick access app row");

    ESP_Brookesia_CoreApp *app = manager->getRunningAppById(it->second);
    if (app == nullptr) {
        return;
    }

    manager->hideQuickAccessOverlay(false);
    ESP_BROOKESIA_CHECK_FALSE_EXIT(manager->processAppResume(app), "Quick access resume app failed");
}

void ESP_Brookesia_PhoneManager::onQuickAccessCloseAppEventCallback(lv_event_t *event)
{
    ESP_Brookesia_PhoneManager *manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");

    lv_obj_t *button = lv_event_get_target(event);
    auto it = manager->_quick_access_close_button_app_id_map.find(button);
    ESP_BROOKESIA_CHECK_FALSE_EXIT(it != manager->_quick_access_close_button_app_id_map.end(), "Unknown quick access app button");

    ESP_Brookesia_CoreApp *app = manager->getRunningAppById(it->second);
    if (app != nullptr) {
        ESP_BROOKESIA_CHECK_FALSE_EXIT(manager->processAppClose(app), "Quick access close app failed");
    }

    manager->refreshQuickAccessAppList();
    if (manager->getRunningAppCount() == 0) {
        manager->hideQuickAccessOverlay(true);
    }
}

void ESP_Brookesia_PhoneManager::onQuickAccessActionEventCallback(lv_event_t *event)
{
    ESP_Brookesia_PhoneManager *manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");

    lv_obj_t *button = lv_event_get_target(event);
    auto it = manager->_quick_access_action_button_map.find(button);
    ESP_BROOKESIA_CHECK_FALSE_EXIT(it != manager->_quick_access_action_button_map.end(), "Unknown quick access action");

    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);

    ESP_Brookesia_CoreApp *app = manager->getRunningAppById(it->second.app_id);
    ESP_BROOKESIA_CHECK_NULL_EXIT(app, "Invalid quick access action app");

    ESP_Brookesia_PhoneApp *phone_app = static_cast<ESP_Brookesia_PhoneApp *>(app);
    ESP_BROOKESIA_CHECK_NULL_EXIT(phone_app, "Invalid quick access phone app");

    if (phone_app->handleQuickAccessAction(it->second.action_id)) {
        manager->refreshQuickAccessAppList();
    }
}

void ESP_Brookesia_PhoneManager::onQuickAccessCloseAllEventCallback(lv_event_t *event)
{
    ESP_Brookesia_PhoneManager *manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");

    if (manager->_quick_access_close_all_timer != nullptr) {
        lv_timer_del(manager->_quick_access_close_all_timer);
        manager->_quick_access_close_all_timer = nullptr;
    }

    manager->_quick_access_close_all_queue.clear();

    ESP_Brookesia_CoreApp *active_app = manager->getActiveApp();
    if (active_app != nullptr) {
        manager->_quick_access_close_all_queue.push_back(active_app->getId());
    }

    const int running_app_count = manager->getRunningAppCount();
    for (int index = 0; index < running_app_count; ++index) {
        ESP_Brookesia_CoreApp *app = manager->getRunningAppByIdenx(index);
        if ((app != nullptr) && ((active_app == nullptr) || (app->getId() != active_app->getId()))) {
            manager->_quick_access_close_all_queue.push_back(app->getId());
        }
    }

    manager->hideQuickAccessOverlay(false);

    if (manager->_quick_access_close_all_queue.empty()) {
        return;
    }

    manager->_quick_access_close_all_timer = lv_timer_create(onQuickAccessCloseAllTimerCallback, 80, manager);
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager->_quick_access_close_all_timer, "Create quick access close-all timer failed");
    lv_timer_set_repeat_count(manager->_quick_access_close_all_timer, -1);
}

void ESP_Brookesia_PhoneManager::onQuickAccessCloseAllTimerCallback(lv_timer_t *timer)
{
    ESP_Brookesia_PhoneManager *manager = (timer != nullptr) ? static_cast<ESP_Brookesia_PhoneManager *>(timer->user_data) : nullptr;
    if (manager == nullptr) {
        if (timer != nullptr) {
            lv_timer_del(timer);
        }
        return;
    }

    if (manager->_quick_access_close_all_queue.empty()) {
        if (manager->_quick_access_close_all_timer != nullptr) {
            lv_timer_del(manager->_quick_access_close_all_timer);
            manager->_quick_access_close_all_timer = nullptr;
        }
        manager->refreshQuickAccessAppList();
        return;
    }

    const int app_id = manager->_quick_access_close_all_queue.front();
    manager->_quick_access_close_all_queue.erase(manager->_quick_access_close_all_queue.begin());

    ESP_Brookesia_CoreApp *app = manager->getRunningAppById(app_id);
    if (app != nullptr) {
        if (!manager->processAppClose(app)) {
            ESP_BROOKESIA_LOGE("Quick access close-all failed for app(%d)", app_id);
        }
    }

    if (manager->_quick_access_close_all_queue.empty()) {
        if (manager->_quick_access_close_all_timer != nullptr) {
            lv_timer_del(manager->_quick_access_close_all_timer);
            manager->_quick_access_close_all_timer = nullptr;
        }
    }
}

void ESP_Brookesia_PhoneManager::onQuickAccessVolumeSliderEventCallback(lv_event_t *event)
{
    ESP_Brookesia_PhoneManager *manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");

    lv_obj_t *slider = lv_event_get_target(event);
    const int volume = lv_slider_get_value(slider);
    const lv_event_code_t event_code = lv_event_get_code(event);
    if (slider == manager->_quick_access_media_volume_slider) {
        bsp_extra_audio_media_volume_set(volume);
        if (event_code == LV_EVENT_RELEASED) {
            manager->setQuickAccessAirplaneNvsFlag(kSettingsAudioVolumeKey, volume);
        }
    } else if (slider == manager->_quick_access_system_volume_slider) {
        bsp_extra_audio_system_volume_set(volume);
        if (event_code == LV_EVENT_RELEASED) {
            manager->setQuickAccessAirplaneNvsFlag(kSettingsSystemAudioVolumeKey, volume);
        }
    } else {
        ESP_BROOKESIA_CHECK_FALSE_EXIT(false, "Unknown quick access volume slider");
    }

    manager->refreshQuickAccessVolumePanel();

    if (event_code == LV_EVENT_RELEASED) {
        bsp_extra_audio_play_system_notification();
    }
}

void ESP_Brookesia_PhoneManager::onQuickAccessPowerButtonEventCallback(lv_event_t *event)
{
    ESP_Brookesia_PhoneManager *manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");

    lv_obj_t *button = lv_event_get_target(event);
    if (button == manager->_quick_access_restart_button) {
        manager->hideQuickAccessOverlay(false);
        esp_restart();
        return;
    }

    if (button == manager->_quick_access_shutdown_button) {
        if ((manager->_quick_access_shutdown_confirm != nullptr) && lv_obj_is_valid(manager->_quick_access_shutdown_confirm)) {
            return;
        }

        static const char *shutdown_buttons[] = {"Cancel", "Confirm", ""};
        manager->_quick_access_shutdown_confirm = lv_msgbox_create(lv_layer_top(), "Shut down?",
                                                                   "Shutdown is not wired yet.", shutdown_buttons, false);
        ESP_BROOKESIA_CHECK_NULL_EXIT(manager->_quick_access_shutdown_confirm, "Create shutdown confirm failed");
        lv_obj_center(manager->_quick_access_shutdown_confirm);
        lv_obj_add_event_cb(manager->_quick_access_shutdown_confirm, onQuickAccessShutdownConfirmEventCallback,
                            LV_EVENT_VALUE_CHANGED, manager);
        lv_obj_add_event_cb(manager->_quick_access_shutdown_confirm, onQuickAccessShutdownConfirmEventCallback,
                            LV_EVENT_DELETE, manager);
        return;
    }

    if (button == manager->_quick_access_sleep_button) {
        manager->hideQuickAccessOverlay(false);
        enter_quick_access_deep_sleep();
        return;
    }

    if (button == manager->_quick_access_settings_button) {
        ESP_BROOKESIA_CHECK_FALSE_EXIT(manager->openQuickAccessSettingsApp(), "Open quick access settings app failed");
        return;
    }

    if (button == manager->_quick_access_notification_button) {
        manager->cycleQuickAccessNotificationMode();
        return;
    }

    if (button == manager->_quick_access_airplane_button) {
        const bool airplane_mode_enabled = lv_obj_has_state(button, LV_STATE_CHECKED);
        const int err = manager->setQuickAccessAirplaneMode(airplane_mode_enabled);
        if (err != ESP_OK) {
            if (manager->_quick_access_airplane_mode_enabled) {
                lv_obj_add_state(button, LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(button, LV_STATE_CHECKED);
            }
            ESP_BROOKESIA_LOGE("Quick access airplane mode transition failed: %s", esp_err_to_name(err));
            return;
        }

        manager->_quick_access_airplane_mode_enabled = airplane_mode_enabled;
        manager->refreshQuickAccessVolumePanel();
        return;
    }

    ESP_BROOKESIA_CHECK_FALSE_EXIT(false, "Unknown quick access power button");
}

int ESP_Brookesia_PhoneManager::setQuickAccessAirplaneMode(bool enabled)
{
    if (enabled) {
        _quick_access_airplane_saved_wifi_enabled = getQuickAccessAirplaneNvsFlag(kSettingsWifiEnableKey, false);
        _quick_access_airplane_saved_wifi_ap_enabled = getQuickAccessAirplaneNvsFlag(kSettingsWifiApEnableKey, false);
        _quick_access_airplane_saved_ble_enabled = getQuickAccessAirplaneNvsFlag(kSettingsBleEnableKey, false);
        _quick_access_airplane_saved_zigbee_enabled = getQuickAccessAirplaneNvsFlag(kSettingsZigbeeEnableKey, false);

        if (!setQuickAccessAirplaneNvsFlag(kSettingsWifiEnableKey, 0) ||
                !setQuickAccessAirplaneNvsFlag(kSettingsWifiApEnableKey, 0) ||
                !setQuickAccessAirplaneNvsFlag(kSettingsBleEnableKey, 0) ||
                !setQuickAccessAirplaneNvsFlag(kSettingsZigbeeEnableKey, 0)) {
            return ESP_FAIL;
        }

        return syncQuickAccessAirplaneSettingsRadios() ? ESP_OK : ESP_FAIL;
    }

    if (!setQuickAccessAirplaneNvsFlag(kSettingsWifiEnableKey, _quick_access_airplane_saved_wifi_enabled ? 1 : 0) ||
            !setQuickAccessAirplaneNvsFlag(kSettingsWifiApEnableKey, _quick_access_airplane_saved_wifi_ap_enabled ? 1 : 0) ||
            !setQuickAccessAirplaneNvsFlag(kSettingsBleEnableKey, _quick_access_airplane_saved_ble_enabled ? 1 : 0) ||
            !setQuickAccessAirplaneNvsFlag(kSettingsZigbeeEnableKey, _quick_access_airplane_saved_zigbee_enabled ? 1 : 0)) {
        return ESP_FAIL;
    }

    return syncQuickAccessAirplaneSettingsRadios() ? ESP_OK : ESP_FAIL;
}

bool ESP_Brookesia_PhoneManager::openQuickAccessSettingsApp(void)
{
    ESP_Brookesia_CoreAppEventData_t app_event_data = {
        .type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START,
    };

    for (int app_id = 0; app_id < 64; ++app_id) {
        ESP_Brookesia_CoreApp *app = getInstalledApp(app_id);
        if ((app == nullptr) || (app->getName() == nullptr) || (strcmp(app->getName(), kQuickAccessSettingsAppName) != 0)) {
            continue;
        }

        hideQuickAccessOverlay(false);
        if ((jc_security_handle_app_launch_request != nullptr) &&
            jc_security_handle_app_launch_request(app->getId(), app->getName())) {
            return true;
        }

        app_event_data.id = app->getId();
        return _core.sendAppEvent(&app_event_data);
    }

    ESP_BROOKESIA_LOGW("Quick access settings app not found");
    return false;
}

bool ESP_Brookesia_PhoneManager::syncQuickAccessAirplaneSettingsRadios(void)
{
    for (int app_id = 1; app_id <= 32; ++app_id) {
        ESP_Brookesia_CoreApp *core_app = getInstalledApp(app_id);
        ESP_Brookesia_PhoneApp *app = (core_app != nullptr) ? static_cast<ESP_Brookesia_PhoneApp *>(core_app) : nullptr;
        if ((app == nullptr) || (app->getName() == nullptr)) {
            continue;
        }

        if (strcmp(app->getName(), "Settings") == 0) {
            return app->handleQuickAccessAction(kQuickAccessActionApplyAirplaneRadioPreferences);
        }
    }

    ESP_BROOKESIA_LOGW("Settings app not found while syncing airplane mode radios");
    return false;
}

bool ESP_Brookesia_PhoneManager::setQuickAccessAirplaneNvsFlag(const char *key, int value)
{
    if (key == nullptr) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(kSettingsStorageNamespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_BROOKESIA_LOGE("Open NVS for airplane mode failed: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_i32(nvs_handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_BROOKESIA_LOGE("Write airplane mode NVS key %s failed: %s", key, esp_err_to_name(err));
        return false;
    }

    return true;
}

bool ESP_Brookesia_PhoneManager::getQuickAccessAirplaneNvsFlag(const char *key, bool default_value) const
{
    if (key == nullptr) {
        return default_value;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(kSettingsStorageNamespace, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return default_value;
    }

    int32_t value = default_value ? 1 : 0;
    err = nvs_get_i32(nvs_handle, key, &value);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        return default_value;
    }

    return value != 0;
}

void ESP_Brookesia_PhoneManager::onQuickAccessShutdownConfirmEventCallback(lv_event_t *event)
{
    ESP_Brookesia_PhoneManager *manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");

    lv_obj_t *msgbox = lv_event_get_target(event);
    switch (lv_event_get_code(event)) {
    case LV_EVENT_VALUE_CHANGED:
        if ((msgbox != nullptr) && lv_obj_is_valid(msgbox)) {
            lv_msgbox_close(msgbox);
        }
        break;
    case LV_EVENT_DELETE:
        if (manager->_quick_access_shutdown_confirm == msgbox) {
            manager->_quick_access_shutdown_confirm = nullptr;
        }
        break;
    default:
        break;
    }
}

bool ESP_Brookesia_PhoneManager::processStatusBarScreenChange(ESP_Brookesia_PhoneManagerScreen_t screen, void *param)
{
    ESP_Brookesia_StatusBar *status_bar = home._status_bar.get();
    ESP_Brookesia_StatusBarVisualMode_t status_bar_visual_mode = ESP_BROOKESIA_STATUS_BAR_VISUAL_MODE_HIDE;
    const ESP_Brookesia_PhoneAppData_t *app_data = nullptr;

    ESP_BROOKESIA_LOGD("Process status bar when screen change");
    ESP_BROOKESIA_CHECK_FALSE_RETURN(checkInitialized(), false, "Not initialized");
    ESP_BROOKESIA_CHECK_FALSE_RETURN(screen < ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAX, false, "Invalid screen");

    if (status_bar == nullptr) {
        return true;
    }

    switch (screen) {
    case ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAIN:
        status_bar_visual_mode = home.getData().status_bar.visual_mode;
        break;
    case ESP_BROOKESIA_PHONE_MANAGER_SCREEN_APP:
        ESP_BROOKESIA_CHECK_NULL_RETURN(param, false, "Invalid param");
        app_data = &((ESP_Brookesia_PhoneApp *)param)->getActiveData();
        status_bar_visual_mode = app_data->status_bar_visual_mode;
        break;
    case ESP_BROOKESIA_PHONE_MANAGER_SCREEN_RECENTS_SCREEN:
        status_bar_visual_mode = home.getData().recents_screen.status_bar_visual_mode;
        break;
    default:
        ESP_BROOKESIA_CHECK_FALSE_RETURN(false, false, "Invalid screen");
        break;
    }
    ESP_BROOKESIA_LOGD("Visual Mode: status bar(%d)", status_bar_visual_mode);

    ESP_BROOKESIA_CHECK_FALSE_RETURN(status_bar->setVisualMode(status_bar_visual_mode), false,
                                     "Status bar set visual mode failed");

    return true;
}

bool ESP_Brookesia_PhoneManager::processNavigationBarScreenChange(ESP_Brookesia_PhoneManagerScreen_t screen, void *param)
{
    ESP_Brookesia_NavigationBar *navigation_bar = home._navigation_bar.get();
    ESP_Brookesia_NavigationBarVisualMode_t navigation_bar_visual_mode = ESP_BROOKESIA_NAVIGATION_BAR_VISUAL_MODE_HIDE;
    const ESP_Brookesia_PhoneAppData_t *app_data = nullptr;

    ESP_BROOKESIA_LOGD("Process navigation bar when screen change");
    ESP_BROOKESIA_CHECK_FALSE_RETURN(checkInitialized(), false, "Not initialized");
    ESP_BROOKESIA_CHECK_FALSE_RETURN(screen < ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAX, false, "Invalid screen");

    // Process status bar
    if (navigation_bar == nullptr) {
        return true;
    }

    switch (screen) {
    case ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAIN:
        navigation_bar_visual_mode = home.getData().navigation_bar.visual_mode;
        break;
    case ESP_BROOKESIA_PHONE_MANAGER_SCREEN_APP:
        ESP_BROOKESIA_CHECK_NULL_RETURN(param, false, "Invalid param");
        app_data = &((ESP_Brookesia_PhoneApp *)param)->getActiveData();
        navigation_bar_visual_mode = app_data->navigation_bar_visual_mode;
        break;
    case ESP_BROOKESIA_PHONE_MANAGER_SCREEN_RECENTS_SCREEN:
        navigation_bar_visual_mode = home.getData().recents_screen.navigation_bar_visual_mode;
        break;
    default:
        ESP_BROOKESIA_CHECK_FALSE_RETURN(false, false, "Invalid screen");
        break;
    }
    ESP_BROOKESIA_LOGD("Visual Mode: navigation bar(%d)", navigation_bar_visual_mode);

    _flags.enable_navigation_bar_gesture = (navigation_bar_visual_mode == ESP_BROOKESIA_NAVIGATION_BAR_VISUAL_MODE_SHOW_FLEX);
    ESP_BROOKESIA_CHECK_FALSE_RETURN(navigation_bar->setVisualMode(navigation_bar_visual_mode), false,
                                     "Navigation bar set visual mode failed");

    return true;
}

bool ESP_Brookesia_PhoneManager::processGestureScreenChange(ESP_Brookesia_PhoneManagerScreen_t screen, void *param)
{
    ESP_Brookesia_NavigationBar *navigation_bar = home._navigation_bar.get();
    ESP_Brookesia_NavigationBarVisualMode_t navigation_bar_visual_mode = ESP_BROOKESIA_NAVIGATION_BAR_VISUAL_MODE_HIDE;
    const ESP_Brookesia_PhoneAppData_t *app_data = nullptr;

    ESP_BROOKESIA_LOGD("Process gesture when screen change");
    ESP_BROOKESIA_CHECK_FALSE_RETURN(checkInitialized(), false, "Not initialized");
    ESP_BROOKESIA_CHECK_FALSE_RETURN(screen < ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAX, false, "Invalid screen");

    switch (screen) {
    case ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAIN:
        navigation_bar_visual_mode = home.getData().navigation_bar.visual_mode;
        _flags.enable_gesture_navigation = ((navigation_bar == nullptr) ||
                                            (navigation_bar_visual_mode == ESP_BROOKESIA_NAVIGATION_BAR_VISUAL_MODE_HIDE));
        _flags.enable_gesture_navigation_back = false;
        _flags.enable_gesture_navigation_home = false;
        _flags.enable_gesture_navigation_recents_app = _flags.enable_gesture_navigation;
        _flags.enable_gesture_show_mask_left_right_edge = false;
        _flags.enable_gesture_show_mask_bottom_edge = (_flags.enable_gesture_navigation ||
                (navigation_bar_visual_mode == ESP_BROOKESIA_NAVIGATION_BAR_VISUAL_MODE_SHOW_FLEX));
        _flags.enable_gesture_show_left_right_indicator_bar = false;
        _flags.enable_gesture_show_bottom_indicator_bar = _flags.enable_gesture_show_mask_bottom_edge;
        break;
    case ESP_BROOKESIA_PHONE_MANAGER_SCREEN_APP:
        ESP_BROOKESIA_CHECK_NULL_RETURN(param, false, "Invalid param");
        app_data = &((ESP_Brookesia_PhoneApp *)param)->getActiveData();
        navigation_bar_visual_mode = app_data->navigation_bar_visual_mode;
        _flags.enable_gesture_navigation = (app_data->flags.enable_navigation_gesture &&
                                            (navigation_bar_visual_mode != ESP_BROOKESIA_NAVIGATION_BAR_VISUAL_MODE_SHOW_FIXED));
        _flags.enable_gesture_navigation_back = (_flags.enable_gesture_navigation &&
                                                data.flags.enable_gesture_navigation_back);
        _flags.enable_gesture_navigation_home = (_flags.enable_gesture_navigation &&
                                                (navigation_bar_visual_mode == ESP_BROOKESIA_NAVIGATION_BAR_VISUAL_MODE_HIDE));
        _flags.enable_gesture_navigation_recents_app = _flags.enable_gesture_navigation_home;
        _flags.enable_gesture_show_mask_left_right_edge = (_flags.enable_gesture_navigation ||
                (navigation_bar_visual_mode == ESP_BROOKESIA_NAVIGATION_BAR_VISUAL_MODE_SHOW_FLEX));
        _flags.enable_gesture_show_mask_bottom_edge = (_flags.enable_gesture_navigation ||
                (navigation_bar_visual_mode == ESP_BROOKESIA_NAVIGATION_BAR_VISUAL_MODE_SHOW_FLEX));
        _flags.enable_gesture_show_left_right_indicator_bar = _flags.enable_gesture_show_mask_left_right_edge;
        _flags.enable_gesture_show_bottom_indicator_bar = _flags.enable_gesture_show_mask_bottom_edge;
        break;
    case ESP_BROOKESIA_PHONE_MANAGER_SCREEN_RECENTS_SCREEN:
        _flags.enable_gesture_navigation = false;
        _flags.enable_gesture_navigation_back = false;
        _flags.enable_gesture_navigation_home = false;
        _flags.enable_gesture_navigation_recents_app = false;
        _flags.enable_gesture_show_mask_left_right_edge = false;
        _flags.enable_gesture_show_mask_bottom_edge = false;
        _flags.enable_gesture_show_left_right_indicator_bar = false;
        _flags.enable_gesture_show_bottom_indicator_bar = false;
        break;
    default:
        ESP_BROOKESIA_CHECK_FALSE_RETURN(false, false, "Invalid screen");
        break;
    }
    ESP_BROOKESIA_LOGD("Gesture Navigation: all(%d), back(%d), home(%d), recents(%d)", _flags.enable_gesture_navigation,
                       _flags.enable_gesture_navigation_back, _flags.enable_gesture_navigation_home,
                       _flags.enable_gesture_navigation_recents_app);
    ESP_BROOKESIA_LOGD("Gesture Mask & Indicator: mask(left_right: %d, bottom: %d), indicator_left_right(%d), "
                       "indicator_bottom(%d)", _flags.enable_gesture_show_mask_left_right_edge,
                       _flags.enable_gesture_show_mask_bottom_edge, _flags.enable_gesture_show_left_right_indicator_bar,
                       _flags.enable_gesture_show_bottom_indicator_bar);

    if (!_flags.enable_gesture_show_left_right_indicator_bar) {
        ESP_BROOKESIA_CHECK_FALSE_RETURN(
            _gesture->setIndicatorBarVisible(ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_LEFT, false), false,
            "Gesture set left indicator bar visible failed"
        );
        ESP_BROOKESIA_CHECK_FALSE_RETURN(
            _gesture->setIndicatorBarVisible(ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_RIGHT, false), false,
            "Gesture set right indicator bar visible failed"
        );
    }
    ESP_BROOKESIA_CHECK_FALSE_RETURN(
        _gesture->setIndicatorBarVisible(
            ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_BOTTOM, _flags.enable_gesture_show_bottom_indicator_bar
        ), false, "Gesture set bottom indicator bar visible failed"
    );

    return true;
}

void ESP_Brookesia_PhoneManager::onHomeMainScreenLoadEventCallback(lv_event_t *event)
{
    ESP_Brookesia_PhoneManager *manager = nullptr;
    ESP_Brookesia_RecentsScreen *recents_screen = nullptr;

    ESP_BROOKESIA_LOGD("Home main screen load event callback");
    ESP_BROOKESIA_CHECK_NULL_EXIT(event, "Invalid event");

    manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");
    recents_screen = manager->home.getRecentsScreen();

    // Only process the screen change if the recents_screen is not visible
    if ((recents_screen == nullptr) || !recents_screen->checkVisible()) {
        ESP_BROOKESIA_CHECK_FALSE_EXIT(
            manager->processStatusBarScreenChange(ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAIN, nullptr),
            "Process status bar failed");
        ESP_BROOKESIA_CHECK_FALSE_EXIT(
            manager->processNavigationBarScreenChange(ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAIN, nullptr),
            "Process navigation bar failed");
        ESP_BROOKESIA_CHECK_FALSE_EXIT(
            manager->processGestureScreenChange(ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAIN, nullptr),
            "Process gesture failed");
    }
}

void ESP_Brookesia_PhoneManager::onAppLauncherGestureEventCallback(lv_event_t *event)
{
    lv_event_code_t event_code = _LV_EVENT_LAST;
    ESP_Brookesia_PhoneManager *manager = nullptr;
    ESP_Brookesia_AppLauncher *app_launcher = nullptr;
    ESP_Brookesia_RecentsScreen *recents_screen = nullptr;
    ESP_Brookesia_Gesture *gesture = nullptr;
    ESP_Brookesia_GestureInfo_t *gesture_info = nullptr;
    ESP_Brookesia_GestureDirection_t dir_type = ESP_BROOKESIA_GESTURE_DIR_NONE;

    // ESP_BROOKESIA_LOGD("App launcher gesture event callback");
    ESP_BROOKESIA_CHECK_NULL_GOTO(event, end, "Invalid event");

    manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_GOTO(manager, end, "Invalid manager");
    gesture = manager->_gesture.get();
    ESP_BROOKESIA_CHECK_NULL_GOTO(gesture, end, "Invalid gesture");
    recents_screen = manager->home._recents_screen.get();
    app_launcher = &manager->home._app_launcher;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app_launcher, end, "Invalid app launcher");
    event_code = lv_event_get_code(event);
    ESP_BROOKESIA_CHECK_FALSE_GOTO(
        (event_code == gesture->getPressingEventCode()) || (event_code == gesture->getReleaseEventCode()), end,
        "Invalid event code"
    );

    // Here is to prevent detecting gestures when the app exits, which could trigger unexpected behaviors
    if (event_code == gesture->getReleaseEventCode()) {
        if (manager->_flags.is_app_launcher_gesture_disabled) {
            manager->_flags.is_app_launcher_gesture_disabled = false;
            return;
        }
    }

    // Check if the app launcher and recents_screen are visible
    if (!app_launcher->checkVisible() || manager->_flags.is_app_launcher_gesture_disabled ||
            ((recents_screen != nullptr) && recents_screen->checkVisible())) {
        return;
    }

    dir_type = manager->_app_launcher_gesture_dir;
    // Check if the dir type is already set. If so, just ignore and return
    if (dir_type != ESP_BROOKESIA_GESTURE_DIR_NONE) {
        // Check if the gesture is released
        if (event_code == gesture->getReleaseEventCode()) {   // If so, reset the navigation type
            dir_type = ESP_BROOKESIA_GESTURE_DIR_NONE;
            goto end;
        }
        return;
    }

    gesture_info = (ESP_Brookesia_GestureInfo_t *)lv_event_get_param(event);
    ESP_BROOKESIA_CHECK_NULL_GOTO(gesture_info, end, "Invalid gesture info");
    // Check if there is a gesture
    if (gesture_info->direction == ESP_BROOKESIA_GESTURE_DIR_NONE) {
        return;
    }

    dir_type = gesture_info->direction;
    switch (dir_type) {
    case ESP_BROOKESIA_GESTURE_DIR_LEFT:
        ESP_BROOKESIA_LOGD("App table gesture left");
        ESP_BROOKESIA_CHECK_FALSE_GOTO(app_launcher->scrollToRightPage(), end, "App table scroll to right page failed");
        break;
    case ESP_BROOKESIA_GESTURE_DIR_RIGHT:
        ESP_BROOKESIA_LOGD("App table gesture right");
        ESP_BROOKESIA_CHECK_FALSE_GOTO(app_launcher->scrollToLeftPage(), end, "App table scroll to left page failed");
        break;
    default:
        break;
    }

end:
    manager->_app_launcher_gesture_dir = dir_type;
}

void ESP_Brookesia_PhoneManager::onNavigationBarGestureEventCallback(lv_event_t *event)
{
    lv_event_code_t event_code = _LV_EVENT_LAST;
    ESP_Brookesia_PhoneManager *manager = nullptr;
    ESP_Brookesia_NavigationBar *navigation_bar = nullptr;
    ESP_Brookesia_GestureInfo_t *gesture_info = nullptr;
    ESP_Brookesia_GestureDirection_t dir_type = ESP_BROOKESIA_GESTURE_DIR_NONE;

    // ESP_BROOKESIA_LOGD("Navigation bar gesture event callback");
    ESP_BROOKESIA_CHECK_NULL_EXIT(event, "Invalid event");

    manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");
    navigation_bar = manager->home.getNavigationBar();
    ESP_BROOKESIA_CHECK_NULL_EXIT(navigation_bar, "Invalid navigation bar");
    event_code = lv_event_get_code(event);
    ESP_BROOKESIA_CHECK_FALSE_EXIT((event_code == manager->_gesture->getPressingEventCode()) ||
                                   (event_code == manager->_gesture->getReleaseEventCode()), "Invalid event code");

    // Here is to prevent detecting gestures when the app exits, which could trigger unexpected behaviors
    if (manager->_flags.is_navigation_bar_gesture_disabled && (event_code == manager->_gesture->getReleaseEventCode())) {
        manager->_flags.is_navigation_bar_gesture_disabled = false;
        return;
    }

    // Check if the gesture is enabled or the app is running
    if (manager->_flags.is_navigation_bar_gesture_disabled || (!manager->_flags.enable_navigation_bar_gesture)) {
        return;
    }

    dir_type = manager->_navigation_bar_gesture_dir;
    // Check if the dir type is already set. If so, just ignore and return
    if (dir_type != ESP_BROOKESIA_GESTURE_DIR_NONE) {
        // Check if the gesture is released
        if (event_code == manager->_gesture->getReleaseEventCode()) {   // If so, reset the navigation type
            dir_type = ESP_BROOKESIA_GESTURE_DIR_NONE;
            goto end;
        }
        return;
    }

    gesture_info = (ESP_Brookesia_GestureInfo_t *)lv_event_get_param(event);
    ESP_BROOKESIA_CHECK_NULL_EXIT(gesture_info, "Invalid gesture info");

    // Check if there is a valid gesture
    dir_type = gesture_info->direction;
    if ((dir_type == ESP_BROOKESIA_GESTURE_DIR_UP) &&
            (gesture_info->start_area & ESP_BROOKESIA_GESTURE_AREA_BOTTOM_EDGE)) {
        ESP_BROOKESIA_LOGD("Navigation bar gesture up");
        ESP_BROOKESIA_CHECK_FALSE_EXIT(navigation_bar->triggerVisualFlexShow(), "Navigation bar trigger visual flex show failed");
    }

end:
    manager->_navigation_bar_gesture_dir = dir_type;
}

bool ESP_Brookesia_PhoneManager::processNavigationEvent(ESP_Brookesia_CoreNavigateType_t type)
{
    bool ret = true;
    ESP_Brookesia_RecentsScreen *recents_screen = home._recents_screen.get();
    ESP_Brookesia_PhoneApp *active_app = static_cast<ESP_Brookesia_PhoneApp *>(getActiveApp());
    ESP_Brookesia_PhoneApp *phone_app = nullptr;

    ESP_BROOKESIA_LOGD("Process navigation event type(%d)", type);

    // Disable the gesture function of widgets
    _flags.is_app_launcher_gesture_disabled = true;
    _flags.is_navigation_bar_gesture_disabled = true;

    // Check if the recents_screen is visible
    if ((recents_screen != nullptr) && recents_screen->checkVisible()) {
        // Hide if the recents_screen is visible
        if (!processRecentsScreenHide()) {
            ESP_BROOKESIA_LOGE("Hide recents_screen failed");
            ret = false;
        }
        // Directly return if the type is not home
        if (type != ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME) {
            return ret;
        }
    }

    switch (type) {
    case ESP_BROOKESIA_CORE_NAVIGATE_TYPE_BACK:
        if (active_app == nullptr) {
            goto end;
        }
        // Call app back function
        ESP_BROOKESIA_CHECK_FALSE_GOTO(ret = (active_app->back()), end, "App(%d) back failed", active_app->getId());
        break;
    case ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME:
        if (active_app == nullptr) {
            goto end;
        }
        {
            const int internal_heap_used_percent = get_internal_heap_used_percent();
            if (internal_heap_used_percent > kHomeSwipeAutoCloseInternalHeapPercent) {
                ESP_BROOKESIA_LOGW("Home swipe is auto-closing app(%d) at internal heap usage %d%%",
                                   active_app->getId(), internal_heap_used_percent);
                ESP_BROOKESIA_CHECK_FALSE_GOTO(ret = processAppClose(active_app), end,
                                               "App(%d) close failed", active_app->getId());
            } else {
                // Process app pause
                ESP_BROOKESIA_CHECK_FALSE_GOTO(ret = processAppPause(active_app), end,
                                               "App(%d) pause failed", active_app->getId());
                ESP_BROOKESIA_CHECK_FALSE_RETURN(processHomeScreenChange(ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAIN, nullptr), false,
                                                 "Process screen change failed");
                resetActiveApp();
            }
        }
        break;
    case ESP_BROOKESIA_CORE_NAVIGATE_TYPE_RECENTS_SCREEN:
        if (recents_screen == nullptr) {
            ESP_BROOKESIA_LOGW("Recents screen is disabled");
            goto end;
        }
        // Save the active app and pause it
        if (active_app != nullptr) {
            ret = processAppPause(active_app);
            ESP_BROOKESIA_CHECK_FALSE_GOTO(ret, end, "Process app pause failed");
        }
        _recents_screen_pause_app = active_app;
        // Show recents_screen
        ESP_BROOKESIA_CHECK_FALSE_GOTO(ret = processRecentsScreenShow(), end, "Process recents_screen show failed");
        // Get the active app for recents screen, if the active app is not set, set the last app as the active app
        _recents_screen_active_app = active_app != nullptr ? active_app : getRunningAppByIdenx(getRunningAppCount() - 1);
        // Scroll to the active app
        if ((_recents_screen_active_app != nullptr) &&
                !recents_screen->scrollToSnapshotById(_recents_screen_active_app->getId())) {
            ESP_BROOKESIA_LOGE("Recents screen scroll to snapshot(%d) failed", _recents_screen_active_app->getId());
            ret = false;
        }
        // Update the snapshot, need to be called after `processAppPause()`
        for (int i = 0; i < getRunningAppCount(); i++) {
            phone_app = static_cast<ESP_Brookesia_PhoneApp *>(getRunningAppByIdenx(i));
            ESP_BROOKESIA_CHECK_FALSE_GOTO(ret = (phone_app != nullptr), end, "Invalid active app");

            // Update snapshot conf and image
            ESP_BROOKESIA_CHECK_FALSE_GOTO(ret = phone_app->updateRecentsScreenSnapshotConf(getAppSnapshot(phone_app->getId())),
                                           end, "App update snapshot(%d) conf failed", phone_app->getId());
            ESP_BROOKESIA_CHECK_FALSE_GOTO(ret = recents_screen->updateSnapshotImage(phone_app->getId()), end,
                                           "Recents screen update snapshot(%d) image failed", phone_app->getId());
        }
        break;
    default:
        break;
    }

end:
    return ret;
}

void ESP_Brookesia_PhoneManager::onGestureNavigationPressingEventCallback(lv_event_t *event)
{
    ESP_Brookesia_PhoneManager *manager = nullptr;
    ESP_Brookesia_GestureInfo_t *gesture_info = nullptr;
    ESP_Brookesia_CoreNavigateType_t navigation_type = ESP_BROOKESIA_CORE_NAVIGATE_TYPE_MAX;

    // ESP_BROOKESIA_LOGD("Gesture navigation pressing event callback");
    ESP_BROOKESIA_CHECK_NULL_EXIT(event, "Invalid event");

    manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");
    // Check if the gesture is released and enabled
    if (!manager->_flags.enable_gesture_navigation || manager->_flags.is_gesture_navigation_disabled) {
        return;
    }

    gesture_info = (ESP_Brookesia_GestureInfo_t *)lv_event_get_param(event);
    ESP_BROOKESIA_CHECK_NULL_EXIT(gesture_info, "Invalid gesture info");
    // Check if there is a gesture
    if (gesture_info->direction == ESP_BROOKESIA_GESTURE_DIR_NONE) {
        return;
    }

    // Check if there is a "back" gesture
    if ((gesture_info->start_area & (ESP_BROOKESIA_GESTURE_AREA_LEFT_EDGE | ESP_BROOKESIA_GESTURE_AREA_RIGHT_EDGE)) &&
            (gesture_info->direction & ESP_BROOKESIA_GESTURE_DIR_HOR) && manager->_flags.enable_gesture_navigation_back) {
        navigation_type = ESP_BROOKESIA_CORE_NAVIGATE_TYPE_BACK;
    } else if ((gesture_info->start_area & ESP_BROOKESIA_GESTURE_AREA_BOTTOM_EDGE) && (!gesture_info->flags.short_duration) &&
               (gesture_info->direction & ESP_BROOKESIA_GESTURE_DIR_UP) && manager->_flags.enable_gesture_navigation_recents_app) {
        // Check if there is a "recents_screen" gesture
        navigation_type = ESP_BROOKESIA_CORE_NAVIGATE_TYPE_RECENTS_SCREEN;
    }

    // Only process the navigation event if the navigation type is valid
    if (navigation_type != ESP_BROOKESIA_CORE_NAVIGATE_TYPE_MAX) {
        manager->_flags.is_gesture_navigation_disabled = true;
        ESP_BROOKESIA_CHECK_FALSE_EXIT(manager->processNavigationEvent(navigation_type), "Process navigation event failed");
    }
}

void ESP_Brookesia_PhoneManager::onGestureNavigationReleaseEventCallback(lv_event_t *event)
{
    ESP_Brookesia_PhoneManager *manager = nullptr;
    ESP_Brookesia_GestureInfo_t *gesture_info = nullptr;
    ESP_Brookesia_CoreNavigateType_t navigation_type = ESP_BROOKESIA_CORE_NAVIGATE_TYPE_MAX;

    ESP_BROOKESIA_LOGD("Gesture navigation release event callback");
    ESP_BROOKESIA_CHECK_NULL_EXIT(event, "Invalid event");

    manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");
    manager->_flags.is_gesture_navigation_disabled = false;
    // Check if the gesture is released and enabled
    if (!manager->_flags.enable_gesture_navigation) {
        return;
    }

    gesture_info = (ESP_Brookesia_GestureInfo_t *)lv_event_get_param(event);
    ESP_BROOKESIA_CHECK_NULL_EXIT(gesture_info, "Invalid gesture info");
    // Check if there is a gesture
    if (gesture_info->direction == ESP_BROOKESIA_GESTURE_DIR_NONE) {
        return;
    }

    if ((gesture_info->start_area & ESP_BROOKESIA_GESTURE_AREA_TOP_EDGE) &&
        (gesture_info->direction & ESP_BROOKESIA_GESTURE_DIR_DOWN)) {
        const int screen_midpoint = manager->_core.getCoreData().screen_size.width / 2;
        if (gesture_info->start_x < screen_midpoint) {
            manager->showQuickAccessAppList();
        } else {
            manager->showQuickAccessVolumePanel();
        }
        return;
    }

    // Check if there is a "home" gesture
    if ((gesture_info->start_area & ESP_BROOKESIA_GESTURE_AREA_BOTTOM_EDGE) && (gesture_info->flags.short_duration) &&
            (gesture_info->direction & ESP_BROOKESIA_GESTURE_DIR_UP) && manager->_flags.enable_gesture_navigation_home) {
        navigation_type = ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME;
    }

    // Only process the navigation event if the navigation type is valid
    if (navigation_type != ESP_BROOKESIA_CORE_NAVIGATE_TYPE_MAX) {
        ESP_BROOKESIA_CHECK_FALSE_EXIT(manager->processNavigationEvent(navigation_type), "Process navigation event failed");
    }
}

void ESP_Brookesia_PhoneManager::onGestureMaskIndicatorPressingEventCallback(lv_event_t *event)
{
    bool is_gesture_mask_enabled = false;
    int gesture_indicator_offset = 0;
    ESP_Brookesia_PhoneManager *manager = nullptr;
    ESP_Brookesia_NavigationBar *navigation_bar = nullptr;
    ESP_Brookesia_Gesture *gesture = nullptr;
    ESP_Brookesia_GestureInfo_t *gesture_info = nullptr;
    ESP_Brookesia_GestureIndicatorBarType_t gesture_indicator_bar_type = ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_MAX;

    ESP_BROOKESIA_CHECK_NULL_EXIT(event, "Invalid event");

    manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");
    gesture = manager->getGesture();
    ESP_BROOKESIA_CHECK_NULL_EXIT(gesture, "Invalid gesture");
    gesture_info = (ESP_Brookesia_GestureInfo_t *)lv_event_get_param(event);
    ESP_BROOKESIA_CHECK_NULL_EXIT(gesture_info, "Invalid gesture info");
    navigation_bar = manager->home.getNavigationBar();

    // Just return if the navigation bar is visible or the gesture duration is less than the trigger time
    if (((navigation_bar != nullptr) && navigation_bar->checkVisible()) ||
            (gesture_info->duration_ms < manager->data.gesture_mask_indicator_trigger_time_ms)) {
        return;
    }

    // Get the type of the indicator bar and the offset of the gesture
    switch (gesture_info->start_area) {
    case ESP_BROOKESIA_GESTURE_AREA_LEFT_EDGE:
        if (manager->_flags.enable_gesture_show_left_right_indicator_bar) {
            gesture_indicator_bar_type = ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_LEFT;
            gesture_indicator_offset = gesture_info->stop_x - gesture_info->start_x;
        }
        is_gesture_mask_enabled = manager->_flags.enable_gesture_show_mask_left_right_edge;
        break;
    case ESP_BROOKESIA_GESTURE_AREA_RIGHT_EDGE:
        if (manager->_flags.enable_gesture_show_left_right_indicator_bar) {
            gesture_indicator_bar_type = ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_RIGHT;
            gesture_indicator_offset = gesture_info->start_x - gesture_info->stop_x;
        }
        is_gesture_mask_enabled = manager->_flags.enable_gesture_show_mask_left_right_edge;
        break;
    case ESP_BROOKESIA_GESTURE_AREA_BOTTOM_EDGE:
        if (manager->_flags.enable_gesture_show_bottom_indicator_bar) {
            gesture_indicator_bar_type = ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_BOTTOM;
            gesture_indicator_offset = gesture_info->start_y - gesture_info->stop_y;
        }
        is_gesture_mask_enabled = manager->_flags.enable_gesture_show_mask_bottom_edge;
        break;
    default:
        break;
    }

    // If the gesture indicator bar type is valid, update the indicator bar
    if (gesture_indicator_bar_type < ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_MAX) {
        if (gesture->checkIndicatorBarVisible(gesture_indicator_bar_type)) {
            ESP_BROOKESIA_CHECK_FALSE_EXIT(
                gesture->setIndicatorBarLengthByOffset(gesture_indicator_bar_type, gesture_indicator_offset),
                "Gesture set bottom indicator bar length by offset failed"
            );
        } else {
            if (gesture->checkIndicatorBarScaleBackAnimRunning(gesture_indicator_bar_type)) {
                ESP_BROOKESIA_CHECK_FALSE_EXIT(
                    gesture->controlIndicatorBarScaleBackAnim(gesture_indicator_bar_type, false),
                    "Gesture control indicator bar scale back anim failed"
                );
            }
            ESP_BROOKESIA_CHECK_FALSE_EXIT(
                gesture->setIndicatorBarVisible(gesture_indicator_bar_type, true),
                "Gesture set indicator bar visible failed"
            );
        }
    }

    // If the gesture mask is enabled, show the mask object
    if (is_gesture_mask_enabled && !gesture->checkMaskVisible()) {
        ESP_BROOKESIA_CHECK_FALSE_EXIT(gesture->setMaskObjectVisible(true), "Gesture show mask object failed");
    }
}

void ESP_Brookesia_PhoneManager::onGestureMaskIndicatorReleaseEventCallback(lv_event_t *event)
{
    ESP_Brookesia_PhoneManager *manager = nullptr;
    ESP_Brookesia_Gesture *gesture = nullptr;
    ESP_Brookesia_GestureInfo_t *gesture_info = nullptr;
    ESP_Brookesia_GestureIndicatorBarType_t gesture_indicator_bar_type = ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_MAX;

    ESP_BROOKESIA_CHECK_NULL_EXIT(event, "Invalid event");

    manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");
    gesture = manager->getGesture();
    ESP_BROOKESIA_CHECK_NULL_EXIT(gesture, "Invalid gesture");
    gesture_info = (ESP_Brookesia_GestureInfo_t *)lv_event_get_param(event);
    ESP_BROOKESIA_CHECK_NULL_EXIT(gesture_info, "Invalid gesture info");

    // Update the mask object and indicator bar of the gesture
    ESP_BROOKESIA_CHECK_FALSE_EXIT(gesture->setMaskObjectVisible(false), "Gesture hide mask object failed");
    switch (gesture_info->start_area) {
    case ESP_BROOKESIA_GESTURE_AREA_LEFT_EDGE:
        gesture_indicator_bar_type = ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_LEFT;
        break;
    case ESP_BROOKESIA_GESTURE_AREA_RIGHT_EDGE:
        gesture_indicator_bar_type = ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_RIGHT;
        break;
    case ESP_BROOKESIA_GESTURE_AREA_BOTTOM_EDGE:
        gesture_indicator_bar_type = ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_BOTTOM;
        break;
    default:
        break;
    }
    if (gesture_indicator_bar_type < ESP_BROOKESIA_GESTURE_INDICATOR_BAR_TYPE_MAX &&
            (gesture->checkIndicatorBarVisible(gesture_indicator_bar_type))) {
        ESP_BROOKESIA_CHECK_FALSE_EXIT(
            gesture->controlIndicatorBarScaleBackAnim(gesture_indicator_bar_type, true),
            "Gesture control indicator bar scale back anim failed"
        );
    }
}

bool ESP_Brookesia_PhoneManager::processRecentsScreenShow(void)
{
    ESP_BROOKESIA_LOGD("Process recents_screen show");

    ESP_BROOKESIA_CHECK_FALSE_RETURN(home.processRecentsScreenShow(), false, "Load recents_screen failed");
    ESP_BROOKESIA_CHECK_FALSE_RETURN(processHomeScreenChange(ESP_BROOKESIA_PHONE_MANAGER_SCREEN_RECENTS_SCREEN, nullptr), false,
                                     "Process screen change failed");

    return true;
}

bool ESP_Brookesia_PhoneManager::processRecentsScreenHide(void)
{
    ESP_Brookesia_RecentsScreen *recents_screen = home.getRecentsScreen();
    ESP_Brookesia_PhoneApp *active_app = static_cast<ESP_Brookesia_PhoneApp *>(getActiveApp());

    ESP_BROOKESIA_LOGD("Process recents_screen hide");
    ESP_BROOKESIA_CHECK_NULL_RETURN(recents_screen, false, "Invalid recents_screen");
    ESP_BROOKESIA_CHECK_FALSE_RETURN(recents_screen->setVisible(false), false, "Hide recents_screen failed");

    // Load the main screen if there is no active app
    if (active_app == nullptr) {
        ESP_BROOKESIA_CHECK_FALSE_RETURN(processHomeScreenChange(ESP_BROOKESIA_PHONE_MANAGER_SCREEN_MAIN, nullptr), false,
                                         "Process screen change failed");
    }

    return true;
}

bool ESP_Brookesia_PhoneManager::processRecentsScreenMoveLeft(void)
{
    int recents_screen_active_app_index = getRunningAppIndexByApp(_recents_screen_active_app);
    ESP_Brookesia_RecentsScreen *recents_screen = home._recents_screen.get();

    ESP_BROOKESIA_LOGD("Process recents_screen move left");
    ESP_BROOKESIA_CHECK_NULL_RETURN(recents_screen, false, "Invalid recents_screen");
    ESP_BROOKESIA_CHECK_FALSE_RETURN(recents_screen_active_app_index >= 0, false, "Invalid recents_screen active app index");

    // Check if the active app is at the leftmost
    if (++recents_screen_active_app_index >= getRunningAppCount()) {
        ESP_BROOKESIA_LOGD("Recents screen snapshot is at the rightmost");
        return true;
    }

    ESP_BROOKESIA_LOGD("Recents screen scroll snapshot(%d) left(%d)", _recents_screen_active_app->getId(),
                       recents_screen_active_app_index);
    // Move the snapshot to the left
    ESP_BROOKESIA_CHECK_FALSE_RETURN(recents_screen->scrollToSnapshotByIndex(recents_screen_active_app_index), false,
                                     "Recents screen scroll snapshot left failed");
    // Update the active app
    _recents_screen_active_app = getRunningAppByIdenx(recents_screen_active_app_index);

    return true;
}

bool ESP_Brookesia_PhoneManager::processRecentsScreenMoveRight(void)
{
    int recents_screen_active_app_index = getRunningAppIndexByApp(_recents_screen_active_app);
    ESP_Brookesia_RecentsScreen *recents_screen = home._recents_screen.get();

    ESP_BROOKESIA_LOGD("Process recents_screen move right");
    ESP_BROOKESIA_CHECK_NULL_RETURN(recents_screen, false, "Invalid recents_screen");
    ESP_BROOKESIA_CHECK_FALSE_RETURN(recents_screen_active_app_index >= 0, false, "Invalid recents_screen active app index");

    // Check if the active app is at the rightmost
    if (--recents_screen_active_app_index < 0) {
        ESP_BROOKESIA_LOGD("Recents screen snapshot is at the leftmost");
        return true;
    }

    ESP_BROOKESIA_LOGD("Recents screen scroll snapshot(%d) right(%d)", _recents_screen_active_app->getId(),
                       recents_screen_active_app_index);
    // Move the snapshot to the right
    ESP_BROOKESIA_CHECK_FALSE_RETURN(recents_screen->scrollToSnapshotByIndex(recents_screen_active_app_index), false,
                                     "Recents screen scroll snapshot right failed");
    // Update the active app
    _recents_screen_active_app = getRunningAppByIdenx(recents_screen_active_app_index);

    return true;
}

void ESP_Brookesia_PhoneManager::onRecentsScreenGesturePressEventCallback(lv_event_t *event)
{
    ESP_Brookesia_PhoneManager *manager = nullptr;
    const ESP_Brookesia_RecentsScreen *recents_screen = nullptr;
    ESP_Brookesia_GestureInfo_t *gesture_info = nullptr;
    lv_point_t start_point = {};

    ESP_BROOKESIA_CHECK_NULL_EXIT(event, "Invalid event");

    manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");

    recents_screen = manager->home.getRecentsScreen();
    ESP_BROOKESIA_CHECK_NULL_EXIT(recents_screen, "Invalid recents_screen");

    // Check if recents_screen is visible
    if (!recents_screen->checkVisible()) {
        return;
    }

    gesture_info = (ESP_Brookesia_GestureInfo_t *)lv_event_get_param(event);
    ESP_BROOKESIA_CHECK_NULL_EXIT(gesture_info, "Invalid gesture info");

    start_point = (lv_point_t) {
        (lv_coord_t)gesture_info->start_x, (lv_coord_t)gesture_info->start_y
    };

    // Check if the start point is inside the recents_screen
    if (!recents_screen->checkPointInsideMain(start_point)) {
        return;
    }

    manager->_recents_screen_start_point = start_point;
    manager->_recents_screen_last_point = start_point;
    manager->_flags.is_recents_screen_pressed = true;
    manager->_flags.is_recents_screen_snapshot_move_hor = false;
    manager->_flags.is_recents_screen_snapshot_move_ver = false;

    ESP_BROOKESIA_LOGD("Recents screen press(%d, %d)", start_point.x, start_point.y);
}

void ESP_Brookesia_PhoneManager::onRecentsScreenGesturePressingEventCallback(lv_event_t *event)
{
    ESP_Brookesia_PhoneManager *manager = nullptr;
    ESP_Brookesia_RecentsScreen *recents_screen = nullptr;
    ESP_Brookesia_GestureInfo_t *gesture_info = nullptr;
    const ESP_Brookesia_PhoneManagerData_t *data = nullptr;
    lv_point_t start_point = { 0 };
    int drag_app_id = -1;
    int app_y_max = 0;
    int app_y_min = 0;
    int app_y_current = 0;
    int app_y_target = 0;
    int distance_x = 0;
    int distance_y = 0;
    float tan_value = 0;

    ESP_BROOKESIA_CHECK_NULL_EXIT(event, "Invalid event");

    manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");

    // Check if there is an active app and the recents_screen is pressed
    if ((!manager->_flags.is_recents_screen_pressed) || (manager->_recents_screen_active_app == nullptr)) {
        return;
    }

    recents_screen = manager->home._recents_screen.get();
    ESP_BROOKESIA_CHECK_NULL_EXIT(recents_screen, "Invalid recents_screen");

    gesture_info = (ESP_Brookesia_GestureInfo_t *)lv_event_get_param(event);
    ESP_BROOKESIA_CHECK_NULL_EXIT(gesture_info, "Invalid gesture info");

    // Check if scroll to the left or right
    if ((gesture_info->direction & ESP_BROOKESIA_GESTURE_DIR_LEFT) && !manager->_flags.is_recents_screen_snapshot_move_hor &&
            !manager->_flags.is_recents_screen_snapshot_move_ver) {
        if (!manager->processRecentsScreenMoveLeft()) {
            ESP_BROOKESIA_LOGE("Recents screen app move left failed");
        }
        manager->_flags.is_recents_screen_snapshot_move_hor = true;
    } else if ((gesture_info->direction & ESP_BROOKESIA_GESTURE_DIR_RIGHT) &&
               !manager->_flags.is_recents_screen_snapshot_move_hor &&
               !manager->_flags.is_recents_screen_snapshot_move_ver) {
        if (!manager->processRecentsScreenMoveRight()) {
            ESP_BROOKESIA_LOGE("Recents screen app move right failed");
        }
        manager->_flags.is_recents_screen_snapshot_move_hor = true;
    }

    start_point = (lv_point_t) {
        (lv_coord_t)gesture_info->start_x, (lv_coord_t)gesture_info->start_y,
    };
    drag_app_id = recents_screen->getSnapshotIdPointIn(start_point);
    data = &manager->data;
    // Check if the snapshot is dragged
    if (drag_app_id < 0) {
        return;
    }

    app_y_current = recents_screen->getSnapshotCurrentY(drag_app_id);
    distance_x = gesture_info->stop_x - manager->_recents_screen_last_point.x;
    distance_y = gesture_info->stop_y - manager->_recents_screen_last_point.y;
    // If the vertical distance is less than the step, return
    if (abs(distance_y) < data->recents_screen.drag_snapshot_y_step) {
        return;
    }
    if (distance_x != 0) {
        tan_value = fabs((float)distance_y / distance_x);
        if (tan_value < manager->_recents_screen_drag_tan_threshold) {
            distance_y = 0;
        }
    }

    app_y_max = data->recents_screen.drag_snapshot_y_threshold;
    app_y_min = -app_y_max;
    if (data->flags.enable_recents_screen_snapshot_drag && !manager->_flags.is_recents_screen_snapshot_move_hor &&
            (((distance_y > 0) && (app_y_current < app_y_max)) || ((distance_y < 0) && (app_y_current > app_y_min)))) {
        app_y_target = min(max(app_y_current + distance_y, app_y_min), app_y_max);
        ESP_BROOKESIA_CHECK_FALSE_EXIT(recents_screen->moveSnapshotY(drag_app_id, app_y_target),
                                       "Recents screen move snapshot(%d) y failed", drag_app_id);
        manager->_flags.is_recents_screen_snapshot_move_ver = true;
    }

    manager->_recents_screen_last_point = (lv_point_t) {
        (lv_coord_t)gesture_info->stop_x, (lv_coord_t)gesture_info->stop_y
    };
}

void ESP_Brookesia_PhoneManager::onRecentsScreenGestureReleaseEventCallback(lv_event_t *event)
{
    enum {
        // Default
        RECENTS_SCREEN_NONE =      0,
        // Operate recents_screen
        RECENTS_SCREEN_HIDE =      (1 << 0),
        // Operate app
        RECENTS_SCREEN_APP_CLOSE = (1 << 1),
        RECENTS_SCREEN_APP_SHOW =  (1 << 2),
        // Operate Snapshot
        RECENTS_SCREEN_SNAPSHOT_MOVE_BACK = (1 << 3),
    };

    int recents_screen_active_snapshot_index = 0;
    int target_app_id = -1;
    int distance_move_up_threshold = 0;
    int distance_move_down_threshold = 0;
    int distance_move_up_exit_threshold = 0;
    int distance_y = 0;
    int state = RECENTS_SCREEN_NONE;
    lv_event_code_t event_code = _LV_EVENT_LAST;
    lv_point_t start_point = { 0 };
    ESP_Brookesia_PhoneManager *manager = nullptr;
    ESP_Brookesia_RecentsScreen *recents_screen = nullptr;
    ESP_Brookesia_GestureInfo_t *gesture_info = nullptr;
    ESP_Brookesia_CoreAppEventData_t app_event_data = {
        .type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_MAX,
    };
    const ESP_Brookesia_PhoneManagerData_t *data = nullptr;

    ESP_BROOKESIA_CHECK_NULL_EXIT(event, "Invalid event");

    manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");
    recents_screen = manager->home._recents_screen.get();
    ESP_BROOKESIA_CHECK_NULL_EXIT(recents_screen, "Invalid recents_screen");
    gesture_info = (ESP_Brookesia_GestureInfo_t *)lv_event_get_param(event);
    ESP_BROOKESIA_CHECK_NULL_EXIT(gesture_info, "Invalid gesture info");
    event_code = manager->_core.getAppEventCode();
    ESP_BROOKESIA_CHECK_FALSE_EXIT(esp_brookesia_core_utils_check_event_code_valid(event_code), "Invalid event code");

    // Check if the recents_screen is not pressed or the snapshot is moved
    if (!manager->_flags.is_recents_screen_pressed || manager->_flags.is_recents_screen_snapshot_move_hor) {
        return;
    }

    if (manager->_recents_screen_active_app == nullptr) {
        goto process;
    }

    start_point = (lv_point_t) {
        (lv_coord_t)gesture_info->start_x, (lv_coord_t)gesture_info->start_y,
    };
    target_app_id = recents_screen->getSnapshotIdPointIn(start_point);
    if (target_app_id < 0) {
        if (manager->_recents_screen_pause_app != nullptr) {
            target_app_id = manager->_recents_screen_pause_app->getId();
            state |= RECENTS_SCREEN_APP_SHOW | RECENTS_SCREEN_HIDE;
        }
        goto process;
    }

    if (manager->_flags.is_recents_screen_snapshot_move_ver) {
        state |= RECENTS_SCREEN_SNAPSHOT_MOVE_BACK;
    }

    data = &manager->data;
    distance_y = gesture_info->stop_y - gesture_info->start_y;
    distance_move_up_threshold = -1 * data->recents_screen.drag_snapshot_y_step + 1;
    distance_move_down_threshold = -distance_move_up_threshold;
    distance_move_up_exit_threshold = -1 * data->recents_screen.delete_snapshot_y_threshold;
    if ((distance_y > distance_move_up_threshold) && (distance_y < distance_move_down_threshold)) {
        state |= RECENTS_SCREEN_APP_SHOW | RECENTS_SCREEN_HIDE;
    } else if (distance_y <= distance_move_up_exit_threshold) {
        state |= RECENTS_SCREEN_APP_CLOSE;
    }

process:
    ESP_BROOKESIA_LOGD("Recents screen release");

    if (state == RECENTS_SCREEN_NONE) {
        state = RECENTS_SCREEN_HIDE;
    }

    if (state & RECENTS_SCREEN_SNAPSHOT_MOVE_BACK) {
        recents_screen->moveSnapshotY(target_app_id, recents_screen->getSnapshotOriginY(target_app_id));
        ESP_BROOKESIA_LOGD("Recents screen move snapshot back");
    }

    if (state & RECENTS_SCREEN_APP_CLOSE) {
        ESP_BROOKESIA_LOGD("Recents screen close app(%d)", target_app_id);
        app_event_data.id = target_app_id;
        app_event_data.type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP;
    } else if (state & RECENTS_SCREEN_APP_SHOW) {
        ESP_BROOKESIA_LOGD("Recents screen start app(%d)", target_app_id);
        app_event_data.id = target_app_id;
        app_event_data.type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_START;
    }

    if (state & RECENTS_SCREEN_HIDE) {
        ESP_BROOKESIA_LOGD("Hide recents_screen");
        ESP_BROOKESIA_CHECK_FALSE_EXIT(manager->processRecentsScreenHide(), "Hide recents_screen failed");
    }

    manager->_flags.is_recents_screen_pressed = false;
    if (app_event_data.type != ESP_BROOKESIA_CORE_APP_EVENT_TYPE_MAX) {
        // Get the index of the next dragging snapshot before close it
        recents_screen_active_snapshot_index = max(manager->getRunningAppIndexById(target_app_id) - 1, 0);
        // Start or close the dragging app
        ESP_BROOKESIA_CHECK_FALSE_EXIT(manager->_core.sendAppEvent(&app_event_data), "Core send app event failed");
        // Scroll to another running app snapshot if the dragging app is closed
        if (app_event_data.type == ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP) {
            manager->_recents_screen_active_app = manager->getRunningAppByIdenx(recents_screen_active_snapshot_index);
            if (manager->_recents_screen_active_app != nullptr) {
                // If there are active apps, scroll to the previous app snapshot
                ESP_BROOKESIA_LOGD("Recents screen scroll snapshot(%d) to %d", manager->_recents_screen_active_app->getId(),
                                   recents_screen_active_snapshot_index);
                if (!recents_screen->scrollToSnapshotByIndex(recents_screen_active_snapshot_index)) {
                    ESP_BROOKESIA_LOGE("Recents screen scroll snapshot(%d) to %d failed",
                                       manager->_recents_screen_active_app->getId(),
                                       recents_screen_active_snapshot_index);
                }
            } else if (manager->data.flags.enable_recents_screen_hide_when_no_snapshot) {
                // If there are no active apps, hide the recents_screen
                ESP_BROOKESIA_LOGD("No active app, hide recents_screen");
                ESP_BROOKESIA_CHECK_FALSE_EXIT(manager->processRecentsScreenHide(), "Hide recents_screen failed");
            }
        }
    }
}

void ESP_Brookesia_PhoneManager::onRecentsScreenSnapshotDeletedEventCallback(lv_event_t *event)
{
    int app_id = -1;
    ESP_Brookesia_PhoneManager *manager = nullptr;
    ESP_Brookesia_RecentsScreen *recents_screen = nullptr;
    ESP_Brookesia_CoreAppEventData_t app_event_data = {
        .type = ESP_BROOKESIA_CORE_APP_EVENT_TYPE_STOP,
    };

    ESP_BROOKESIA_LOGD("Recents screen snapshot deleted event callback");
    ESP_BROOKESIA_CHECK_NULL_EXIT(event, "Invalid event object");

    manager = static_cast<ESP_Brookesia_PhoneManager *>(lv_event_get_user_data(event));
    ESP_BROOKESIA_CHECK_NULL_EXIT(manager, "Invalid manager");
    recents_screen = manager->home._recents_screen.get();
    ESP_BROOKESIA_CHECK_NULL_EXIT(recents_screen, "Invalid recents_screen");
    app_id = (intptr_t)lv_event_get_param(event);

    if (app_id > 0) {
        app_event_data.id = app_id;
        ESP_BROOKESIA_CHECK_FALSE_EXIT(manager->_core.sendAppEvent(&app_event_data), "Core send app event failed");
    }

    if (recents_screen->getSnapshotCount() == 0) {
        ESP_BROOKESIA_LOGD("No snapshot in the recents_screen");
        manager->_recents_screen_active_app = nullptr;
        if (manager->data.flags.enable_recents_screen_hide_when_no_snapshot) {
            ESP_BROOKESIA_CHECK_FALSE_EXIT(manager->processRecentsScreenHide(), "Manager hide recents_screen failed");
        }
    }
}
