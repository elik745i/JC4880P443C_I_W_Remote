#include "SettingWifiPrivate.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <vector>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lvgl_input_helper.h"
#include "nvs.h"
#include "../app_sntp.h"
#include "../../system_ui_service.h"
#include "../ui/ui.h"

static const char TAG[] = "EUI_Setting";
static constexpr const char *kNvsStorageNamespace = "storage";
static constexpr const char *kWifiApDefaultSsid = "JC4880P443C Remote";
static constexpr uint8_t kWifiApDefaultChannel = 1;
static constexpr uint8_t kWifiApMaxConnections = 4;

static bool is_ignorable_wifi_transition_error(esp_err_t err)
{
    return (err == ESP_OK) || (err == ESP_ERR_WIFI_NOT_INIT) || (err == ESP_ERR_WIFI_NOT_STARTED) ||
           (err == ESP_ERR_WIFI_STATE) || (err == ESP_ERR_WIFI_CONN);
}

static std::string safe_json_string(cJSON *object, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && (item->valuestring != nullptr) ? item->valuestring : "";
}

static bool read_nvs_string_value(const char *key, std::string &value)
{
    if (key == nullptr) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(kNvsStorageNamespace, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, key, nullptr, &required_size);
    if ((err != ESP_OK) || (required_size == 0)) {
        nvs_close(nvs_handle);
        return false;
    }

    std::string buffer(required_size, '\0');
    err = nvs_get_str(nvs_handle, key, buffer.data(), &required_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error (%s) reading %s", esp_err_to_name(err), key);
        return false;
    }

    value.assign(buffer.c_str());
    return true;
}

EventGroupHandle_t s_wifi_event_group = nullptr;
bool s_wifi_restore_in_progress = false;
bool s_wifi_runtime_ready = false;

char st_wifi_ssid[WIFI_SSID_STORAGE_SIZE] = {0};
char st_wifi_password[WIFI_PASSWORD_STORAGE_SIZE] = {0};
char st_wifi_ap_ssid[WIFI_AP_SSID_STORAGE_SIZE] = {0};
char st_wifi_ap_password[WIFI_AP_PASSWORD_STORAGE_SIZE] = {0};

lv_obj_t *panel_wifi_btn[SCAN_LIST_SIZE] = {nullptr};
lv_obj_t *label_wifi_ssid[SCAN_LIST_SIZE] = {nullptr};
lv_obj_t *img_img_wifi_lock[SCAN_LIST_SIZE] = {nullptr};
lv_obj_t *wifi_image[SCAN_LIST_SIZE] = {nullptr};
lv_obj_t *wifi_connect[SCAN_LIST_SIZE] = {nullptr};

AppSettings::SavedWifiCredential AppSettings::sanitizeWifiCredential(const char *ssid, const char *password) const
{
    SavedWifiCredential credential;
    credential.ssid = (ssid != nullptr) ? ssid : "";
    credential.password = (password != nullptr) ? password : "";

    if (credential.ssid.size() >= WIFI_SSID_STORAGE_SIZE) {
        credential.ssid.resize(WIFI_SSID_STORAGE_SIZE - 1);
    }
    if (credential.password.size() >= WIFI_PASSWORD_STORAGE_SIZE) {
        credential.password.resize(WIFI_PASSWORD_STORAGE_SIZE - 1);
    }

    return credential;
}

AppSettings::SavedWifiCredential AppSettings::sanitizeWifiApCredential(const char *ssid, const char *password) const
{
    SavedWifiCredential credential;
    credential.ssid = trim_copy((ssid != nullptr) ? ssid : "");
    credential.password = (password != nullptr) ? password : "";

    if (credential.ssid.size() >= WIFI_AP_SSID_STORAGE_SIZE) {
        credential.ssid.resize(WIFI_AP_SSID_STORAGE_SIZE - 1);
    }
    if (credential.password.size() >= WIFI_AP_PASSWORD_STORAGE_SIZE) {
        credential.password.resize(WIFI_AP_PASSWORD_STORAGE_SIZE - 1);
    }

    return credential;
}

void AppSettings::populateWifiStaConfig(wifi_config_t &wifi_config, const SavedWifiCredential &credential) const
{
    wifi_config = {};
    memcpy(wifi_config.sta.ssid,
           credential.ssid.c_str(),
           std::min(credential.ssid.size(), sizeof(wifi_config.sta.ssid) - 1));
    memcpy(wifi_config.sta.password,
           credential.password.c_str(),
           std::min(credential.password.size(), sizeof(wifi_config.sta.password) - 1));
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_config.sta.bssid_set = false;
    wifi_config.sta.channel = 0;
}

void AppSettings::populateWifiApConfig(wifi_config_t &wifi_config, const SavedWifiCredential &credential) const
{
    wifi_config = {};

    const std::string ssid = credential.ssid.empty() ? std::string(kWifiApDefaultSsid) : credential.ssid;
    std::string password = credential.password;
    const bool use_open_auth = password.empty() || (password.size() < 8);
    if (use_open_auth) {
        password.clear();
    }

    memcpy(wifi_config.ap.ssid,
           ssid.c_str(),
           std::min(ssid.size(), sizeof(wifi_config.ap.ssid) - 1));
    memcpy(wifi_config.ap.password,
           password.c_str(),
           std::min(password.size(), sizeof(wifi_config.ap.password) - 1));
    wifi_config.ap.ssid_len = ssid.size();
    wifi_config.ap.channel = kWifiApDefaultChannel;
    wifi_config.ap.max_connection = kWifiApMaxConnections;
    wifi_config.ap.authmode = use_open_auth ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
}

std::vector<AppSettings::SavedWifiCredential> AppSettings::loadSavedWifiCredentials(void) const
{
    std::vector<SavedWifiCredential> credentials;
    std::string serialized;
    if (!read_nvs_string_value(NVS_KEY_WIFI_NETWORKS, serialized) || serialized.empty()) {
        return credentials;
    }

    cJSON *root = cJSON_Parse(serialized.c_str());
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return credentials;
    }

    cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, root) {
        if (!cJSON_IsObject(entry)) {
            continue;
        }

        SavedWifiCredential credential = sanitizeWifiCredential(
            safe_json_string(entry, "ssid").c_str(),
            safe_json_string(entry, "password").c_str());
        if (credential.ssid.empty()) {
            continue;
        }

        credentials.erase(std::remove_if(credentials.begin(), credentials.end(),
                                         [&credential](const SavedWifiCredential &existing) {
                                             return existing.ssid == credential.ssid;
                                         }),
                          credentials.end());
        credentials.push_back(credential);
        if (credentials.size() > WIFI_SAVED_NETWORK_LIMIT) {
            credentials.erase(credentials.begin(), credentials.begin() + (credentials.size() - WIFI_SAVED_NETWORK_LIMIT));
        }
    }

    cJSON_Delete(root);
    return credentials;
}

bool AppSettings::saveSavedWifiCredentials(const std::vector<SavedWifiCredential> &credentials)
{
    std::vector<SavedWifiCredential> normalized;
    normalized.reserve(credentials.size());

    for (const SavedWifiCredential &entry : credentials) {
        SavedWifiCredential credential = sanitizeWifiCredential(entry.ssid.c_str(), entry.password.c_str());
        if (credential.ssid.empty()) {
            continue;
        }

        normalized.erase(std::remove_if(normalized.begin(), normalized.end(),
                                        [&credential](const SavedWifiCredential &existing) {
                                            return existing.ssid == credential.ssid;
                                        }),
                         normalized.end());
        normalized.push_back(credential);
    }

    if (normalized.size() > WIFI_SAVED_NETWORK_LIMIT) {
        normalized.erase(normalized.begin(), normalized.begin() + (normalized.size() - WIFI_SAVED_NETWORK_LIMIT));
    }

    cJSON *root = cJSON_CreateArray();
    if (root == nullptr) {
        return false;
    }

    for (const SavedWifiCredential &credential : normalized) {
        cJSON *item = cJSON_CreateObject();
        if (item == nullptr) {
            cJSON_Delete(root);
            return false;
        }

        cJSON_AddStringToObject(item, "ssid", credential.ssid.c_str());
        cJSON_AddStringToObject(item, "password", credential.password.c_str());
        cJSON_AddItemToArray(root, item);
    }

    char *serialized = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (serialized == nullptr) {
        return false;
    }

    const bool ok = setNvsStringParam(NVS_KEY_WIFI_NETWORKS, serialized);
    cJSON_free(serialized);
    return ok;
}

bool AppSettings::persistLatestSavedWifiCredential(const SavedWifiCredential *credential)
{
    if (credential == nullptr) {
        return setNvsStringParam(NVS_KEY_WIFI_SSID, "") &&
               setNvsStringParam(NVS_KEY_WIFI_PASSWORD, "");
    }

    return setNvsStringParam(NVS_KEY_WIFI_SSID, credential->ssid.c_str()) &&
           setNvsStringParam(NVS_KEY_WIFI_PASSWORD, credential->password.c_str());
}

bool AppSettings::loadLatestSavedWifiCredential(SavedWifiCredential &credential)
{
    std::vector<SavedWifiCredential> credentials = loadSavedWifiCredentials();
    if (!credentials.empty()) {
        credential = credentials.back();
        persistLatestSavedWifiCredential(&credential);
        return true;
    }

    char legacy_ssid[WIFI_SSID_STORAGE_SIZE] = {0};
    char legacy_password[WIFI_PASSWORD_STORAGE_SIZE] = {0};
    loadNvsStringParam(NVS_KEY_WIFI_SSID, legacy_ssid, sizeof(legacy_ssid));
    loadNvsStringParam(NVS_KEY_WIFI_PASSWORD, legacy_password, sizeof(legacy_password));

    credential = sanitizeWifiCredential(legacy_ssid, legacy_password);
    if (credential.ssid.empty()) {
        return false;
    }

    credentials.push_back(credential);
    saveSavedWifiCredentials(credentials);
    persistLatestSavedWifiCredential(&credential);
    return true;
}

bool AppSettings::selectAutoConnectWifiCredential(SavedWifiCredential &credential)
{
    std::vector<SavedWifiCredential> credentials = loadSavedWifiCredentials();
    if (credentials.empty()) {
        return loadLatestSavedWifiCredential(credential);
    }

    const SavedWifiCredential latest = credentials.back();

    esp_err_t err = esp_wifi_scan_start(nullptr, true);
    if (err != ESP_OK) {
        credential = latest;
        persistLatestSavedWifiCredential(&credential);
        ESP_LOGW(TAG, "Startup Wi-Fi scan failed, falling back to latest saved SSID:%s (%s)",
                 credential.ssid.c_str(), esp_err_to_name(err));
        return true;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if ((err != ESP_OK) || (ap_count == 0)) {
        credential = latest;
        persistLatestSavedWifiCredential(&credential);
        return true;
    }

    uint16_t record_count = ap_count;
    std::vector<wifi_ap_record_t> ap_records(record_count);
    err = esp_wifi_scan_get_ap_records(&record_count, ap_records.data());
    if (err != ESP_OK) {
        credential = latest;
        persistLatestSavedWifiCredential(&credential);
        return true;
    }

    std::map<std::string, int> best_rssi_by_ssid;
    for (uint16_t index = 0; index < record_count; ++index) {
        const char *ssid = reinterpret_cast<const char *>(ap_records[index].ssid);
        if ((ssid == nullptr) || (ssid[0] == '\0')) {
            continue;
        }

        auto existing = best_rssi_by_ssid.find(ssid);
        if ((existing == best_rssi_by_ssid.end()) || (ap_records[index].rssi > existing->second)) {
            best_rssi_by_ssid[ssid] = ap_records[index].rssi;
        }
    }

    if (best_rssi_by_ssid.find(latest.ssid) != best_rssi_by_ssid.end()) {
        credential = latest;
        persistLatestSavedWifiCredential(&credential);
        ESP_LOGI(TAG, "Selected latest saved Wi-Fi SSID:%s because it is currently available", credential.ssid.c_str());
        return true;
    }

    int best_index = -1;
    int best_rssi = std::numeric_limits<int>::min();
    for (size_t index = 0; index < credentials.size(); ++index) {
        const auto match = best_rssi_by_ssid.find(credentials[index].ssid);
        if (match == best_rssi_by_ssid.end()) {
            continue;
        }

        if ((best_index < 0) || (match->second > best_rssi) ||
            ((match->second == best_rssi) && (static_cast<int>(index) > best_index))) {
            best_index = static_cast<int>(index);
            best_rssi = match->second;
        }
    }

    credential = (best_index >= 0) ? credentials[best_index] : latest;
    persistLatestSavedWifiCredential(&credential);
    ESP_LOGI(TAG, "Selected startup Wi-Fi SSID:%s from saved history", credential.ssid.c_str());
    return true;
}

bool AppSettings::rememberWifiCredential(const SavedWifiCredential &credential)
{
    SavedWifiCredential normalized = sanitizeWifiCredential(credential.ssid.c_str(), credential.password.c_str());
    if (normalized.ssid.empty()) {
        return false;
    }

    std::vector<SavedWifiCredential> credentials = loadSavedWifiCredentials();
    credentials.erase(std::remove_if(credentials.begin(), credentials.end(),
                                     [&normalized](const SavedWifiCredential &existing) {
                                         return existing.ssid == normalized.ssid;
                                     }),
                      credentials.end());
    credentials.push_back(normalized);

    if (credentials.size() > WIFI_SAVED_NETWORK_LIMIT) {
        credentials.erase(credentials.begin(), credentials.begin() + (credentials.size() - WIFI_SAVED_NETWORK_LIMIT));
    }

    return saveSavedWifiCredentials(credentials) && persistLatestSavedWifiCredential(&normalized);
}

bool AppSettings::clearSavedWifiCredentials(void)
{
    bool ok = true;
    ok &= setNvsStringParam(NVS_KEY_WIFI_NETWORKS, "[]");
    ok &= setNvsStringParam(NVS_KEY_WIFI_SSID, "");
    ok &= setNvsStringParam(NVS_KEY_WIFI_PASSWORD, "");

    st_wifi_ssid[0] = '\0';
    st_wifi_password[0] = '\0';

    wifi_config_t wifi_config = {};
    _suppressDisconnectRecovery = true;
    esp_err_t err = esp_wifi_disconnect();
    if ((err != ESP_OK) && (err != ESP_ERR_WIFI_NOT_INIT) && (err != ESP_ERR_WIFI_NOT_STARTED)) {
        ESP_LOGW(TAG, "Failed to disconnect Wi-Fi while forgetting network: %s", esp_err_to_name(err));
        ok = false;
        _suppressDisconnectRecovery = false;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if ((err != ESP_OK) && (err != ESP_ERR_WIFI_NOT_INIT)) {
        ESP_LOGW(TAG, "Failed to clear Wi-Fi runtime config: %s", esp_err_to_name(err));
        ok = false;
    }

    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_CONNECTED);
    system_ui_service::set_wifi_connected(false);

    refreshSavedWifiUi();
    return ok;
}

bool AppSettings::forgetSavedWifiCredential(const std::string &ssid)
{
    const std::string trimmed_ssid = trim_copy(ssid);
    if (trimmed_ssid.empty()) {
        return false;
    }

    std::vector<SavedWifiCredential> credentials = loadSavedWifiCredentials();
    credentials.erase(std::remove_if(credentials.begin(), credentials.end(),
                                     [&trimmed_ssid](const SavedWifiCredential &credential) {
                                         return credential.ssid == trimmed_ssid;
                                     }),
                      credentials.end());

    bool ok = saveSavedWifiCredentials(credentials);
    if (credentials.empty()) {
        ok &= persistLatestSavedWifiCredential(nullptr);
        st_wifi_ssid[0] = '\0';
        st_wifi_password[0] = '\0';
    } else {
        ok &= persistLatestSavedWifiCredential(&credentials.back());
        snprintf(st_wifi_ssid, sizeof(st_wifi_ssid), "%s", credentials.back().ssid.c_str());
        snprintf(st_wifi_password, sizeof(st_wifi_password), "%s", credentials.back().password.c_str());
    }

    refreshSavedWifiUi();
    return ok;
}

std::map<std::string, int> AppSettings::getScannedWifiRssiBySsid(void) const
{
    std::map<std::string, int> best_rssi_by_ssid;

    uint16_t ap_count = 0;
    esp_err_t err = esp_wifi_scan_get_ap_num(&ap_count);
    if ((err != ESP_OK) || (ap_count == 0)) {
        return best_rssi_by_ssid;
    }

    const uint16_t record_limit = std::min<uint16_t>(ap_count, SCAN_LIST_SIZE);
    std::vector<wifi_ap_record_t> ap_records(record_limit);
    uint16_t record_count = record_limit;
    err = esp_wifi_scan_get_ap_records(&record_count, ap_records.data());
    if (err != ESP_OK) {
        return best_rssi_by_ssid;
    }

    for (uint16_t index = 0; index < record_count; ++index) {
        const char *ssid = reinterpret_cast<const char *>(ap_records[index].ssid);
        if ((ssid == nullptr) || (ssid[0] == '\0')) {
            continue;
        }

        auto existing = best_rssi_by_ssid.find(ssid);
        if ((existing == best_rssi_by_ssid.end()) || (ap_records[index].rssi > existing->second)) {
            best_rssi_by_ssid[ssid] = ap_records[index].rssi;
        }
    }

    return best_rssi_by_ssid;
}

void AppSettings::updateSavedWifiPanelLayout(bool list_visible, size_t row_count)
{
    if (!lv_obj_ready(_savedWifiPanel) || !lv_obj_ready(_savedWifiListContainer) ||
        !lv_obj_ready(_wifiScanButton) || !lv_obj_ready(ui_PanelScreenSettingWiFiList)) {
        return;
    }

    if (list_visible) {
        lv_obj_clear_flag(_savedWifiListContainer, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_savedWifiListContainer, LV_OBJ_FLAG_HIDDEN);
    }

    const size_t visible_rows = list_visible ? std::max<size_t>(row_count, 1) : 0;
    const int32_t list_height = static_cast<int32_t>(visible_rows) * 56 + static_cast<int32_t>(visible_rows > 0 ? (visible_rows - 1) * 8 : 0);
    const int32_t panel_height = std::max<int32_t>(72, 34 + (list_visible ? (10 + list_height) : 0) + 10);
    lv_obj_set_height(_savedWifiPanel, panel_height);
    lv_obj_align_to(_wifiScanButton, _savedWifiPanel, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_align_to(ui_PanelScreenSettingWiFiList, _wifiScanButton, LV_ALIGN_OUT_BOTTOM_MID, 0, UI_WIFI_LIST_UP_OFFSET);
}

size_t AppSettings::getSavedWifiRenderedIndexFromEventTarget(lv_obj_t *target) const
{
    if (!lv_obj_ready(_savedWifiListContainer) || (target == nullptr)) {
        return _savedWifiRenderedCredentials.size();
    }

    lv_obj_t *row = lv_obj_get_parent(target);
    if ((row == nullptr) || (lv_obj_get_parent(row) != _savedWifiListContainer)) {
        return _savedWifiRenderedCredentials.size();
    }

    const uint32_t index = lv_obj_get_index(row);
    if (index >= _savedWifiRenderedCredentials.size()) {
        return _savedWifiRenderedCredentials.size();
    }

    return index;
}

void AppSettings::refreshSavedWifiUi(void)
{
    if (!isUiActive() || !lv_obj_ready(_savedWifiPanel) || !lv_obj_ready(_savedWifiTitleLabel) ||
        !lv_obj_ready(_savedWifiListContainer) || !lv_obj_ready(_savedWifiExpandButton) ||
        !lv_obj_ready(_savedWifiExpandLabel)) {
        return;
    }

    std::vector<SavedWifiCredential> credentials = loadSavedWifiCredentials();
    if (credentials.empty()) {
        SavedWifiCredential legacy_credential;
        if (loadLatestSavedWifiCredential(legacy_credential)) {
            credentials.push_back(legacy_credential);
        }
    }

    const bool show_dropdown = credentials.size() > 1;
    if (!show_dropdown) {
        _savedWifiListExpanded = false;
    }

    wifi_ap_record_t current_ap = {};
    std::string current_ssid;
    if ((xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_CONNECTED) && (esp_wifi_sta_get_ap_info(&current_ap) == ESP_OK)) {
        current_ssid = reinterpret_cast<const char *>(current_ap.ssid);
    }

    std::string saved_wifi_key = show_dropdown ? "multi|" : "single|";
    saved_wifi_key += _savedWifiListExpanded ? "expanded|" : "collapsed|";
    saved_wifi_key += current_ssid;
    saved_wifi_key += '|';
    for (const SavedWifiCredential &credential : credentials) {
        const bool is_current = !current_ssid.empty() && (credential.ssid == current_ssid);
        saved_wifi_key += credential.ssid;
        saved_wifi_key += '|';
        saved_wifi_key += std::to_string(static_cast<int>(is_current));
        saved_wifi_key += ';';
    }

    if (_savedWifiUiStateKey == saved_wifi_key) {
        return;
    }

    _savedWifiUiStateKey = saved_wifi_key;

    _savedWifiRenderedCredentials = credentials;
    lv_obj_clean(_savedWifiListContainer);

    if (credentials.empty()) {
        lv_label_set_text(_savedWifiTitleLabel, "Saved Network");
        lv_obj_add_flag(_savedWifiExpandButton, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *emptyLabel = lv_label_create(_savedWifiListContainer);
        lv_label_set_text(emptyLabel, "None");
        lv_obj_set_style_text_font(emptyLabel, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(emptyLabel, lv_color_hex(0x111827), 0);

        updateSavedWifiPanelLayout(true, 1);
        return;
    }
    lv_label_set_text(_savedWifiTitleLabel, show_dropdown ?
        (std::string("Saved Networks (") + std::to_string(credentials.size()) + ")").c_str() :
        "Saved Network");

    if (show_dropdown) {
        lv_obj_clear_flag(_savedWifiExpandButton, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_savedWifiExpandLabel, _savedWifiListExpanded ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
    } else {
        lv_obj_add_flag(_savedWifiExpandButton, LV_OBJ_FLAG_HIDDEN);
    }

    const bool list_visible = !show_dropdown || _savedWifiListExpanded;

    if (list_visible) {
        for (const SavedWifiCredential &credential : credentials) {
            const bool is_current = !current_ssid.empty() && (credential.ssid == current_ssid);

            lv_obj_t *row = lv_obj_create(_savedWifiListContainer);
            lv_obj_set_size(row, lv_pct(100), 48);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(row, 14, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_bg_color(row, lv_color_hex(0xF8FAFC), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_left(row, 14, 0);
            lv_obj_set_style_pad_right(row, 10, 0);
            lv_obj_set_style_pad_top(row, 6, 0);
            lv_obj_set_style_pad_bottom(row, 6, 0);
            lv_obj_set_style_pad_column(row, 8, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *ssidLabel = lv_label_create(row);
            lv_label_set_text(ssidLabel, credential.ssid.c_str());
            lv_label_set_long_mode(ssidLabel, LV_LABEL_LONG_DOT);
            lv_obj_set_width(ssidLabel, 0);
            lv_obj_set_flex_grow(ssidLabel, 1);
            lv_obj_set_style_text_font(ssidLabel, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(ssidLabel, lv_color_hex(0x0F172A), 0);

            if (!is_current) {
                lv_obj_t *connectButton = lv_btn_create(row);
                lv_obj_set_size(connectButton, 86, 36);
                lv_obj_set_style_radius(connectButton, 14, 0);
                lv_obj_set_style_border_width(connectButton, 0, 0);
                lv_obj_set_style_bg_color(connectButton, lv_color_hex(0x2563EB), 0);
                lv_obj_set_style_bg_opa(connectButton, LV_OPA_COVER, 0);
                lv_obj_add_event_cb(connectButton, onConnectSavedWifiClickedEventCallback, LV_EVENT_CLICKED, this);

                lv_obj_t *connectLabel = lv_label_create(connectButton);
                lv_label_set_text(connectLabel, "Connect");
                lv_obj_set_style_text_color(connectLabel, lv_color_hex(0xFFFFFF), 0);
                lv_obj_center(connectLabel);
            }

            lv_obj_t *forgetButton = lv_btn_create(row);
            lv_obj_set_size(forgetButton, 82, 36);
            lv_obj_set_style_radius(forgetButton, 14, 0);
            lv_obj_set_style_border_width(forgetButton, 0, 0);
            lv_obj_set_style_bg_color(forgetButton, lv_color_hex(0xE53E3E), 0);
            lv_obj_set_style_bg_opa(forgetButton, LV_OPA_COVER, 0);
            lv_obj_add_event_cb(forgetButton, onForgetSavedWifiClickedEventCallback, LV_EVENT_CLICKED, this);

            lv_obj_t *forgetLabel = lv_label_create(forgetButton);
            lv_label_set_text(forgetLabel, "Forget");
            lv_obj_set_style_text_color(forgetLabel, lv_color_hex(0xFFFFFF), 0);
            lv_obj_center(forgetLabel);
        }
    }

    updateSavedWifiPanelLayout(list_visible, list_visible ? credentials.size() : 0);
}

void AppSettings::refreshAboutWifiUi(void)
{
    if (!isUiActive() || !lv_obj_ready(_aboutWifiValueLabel)) {
        return;
    }

    wifi_ap_record_t ap_info = {};
    const bool wifi_connected = (xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_CONNECTED) &&
                                (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    if (!wifi_connected) {
        lv_label_set_text(_aboutWifiValueLabel, "Disconnected");
        return;
    }

    char ip_address[16] = "Unavailable";
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif != nullptr) {
        esp_netif_ip_info_t ip_info = {};
        if ((esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) &&
            (ip_info.ip.addr != 0)) {
            if (esp_ip4addr_ntoa(&ip_info.ip, ip_address, sizeof(ip_address)) == nullptr) {
                snprintf(ip_address, sizeof(ip_address), "Unavailable");
            }
        }
    }

    char detail[160];
    snprintf(detail,
             sizeof(detail),
             "%s\nIP %s\nRSSI %d dBm, channel %u",
             reinterpret_cast<const char *>(ap_info.ssid),
             ip_address,
             static_cast<int>(ap_info.rssi),
             static_cast<unsigned>(ap_info.primary));
    lv_label_set_text(_aboutWifiValueLabel, detail);
}

bool AppSettings::launchWifiConnection(const SavedWifiCredential &credential, bool dismiss_keyboard, bool navigate_back_on_success)
{
    const SavedWifiCredential normalized = sanitizeWifiCredential(credential.ssid.c_str(), credential.password.c_str());
    if (normalized.ssid.empty()) {
        return false;
    }

    SavedWifiCredential previous_credential;
    bool has_previous_connection = false;
    wifi_ap_record_t current_ap = {};
    if ((xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_CONNECTED) &&
        (esp_wifi_sta_get_ap_info(&current_ap) == ESP_OK)) {
        const SavedWifiCredential current_credential = sanitizeWifiCredential(
            reinterpret_cast<const char *>(current_ap.ssid), st_wifi_password);
        if (!current_credential.ssid.empty() && (current_credential.ssid != normalized.ssid)) {
            previous_credential = current_credential;
            has_previous_connection = true;
        }
    }

    if (lv_obj_ready(ui_LabelScreenSettingVerificationSSID)) {
        lv_label_set_text(ui_LabelScreenSettingVerificationSSID, normalized.ssid.c_str());
    }
    if (lv_obj_ready(ui_TextAreaScreenSettingVerificationPassword)) {
        lv_textarea_set_text(ui_TextAreaScreenSettingVerificationPassword, normalized.password.c_str());
    }

    processWifiConnect(WIFI_CONNECT_RUNNING);
    stopWifiScan();

    WifiConnectTaskContext *context = new WifiConnectTaskContext{
        this,
        normalized,
        previous_credential,
        has_previous_connection,
        dismiss_keyboard,
        navigate_back_on_success,
    };

    if (create_background_task_prefer_psram(wifiConnectTask, "wifi Connect", WIFI_CONNECT_TASK_STACK_SIZE,
                                            context, WIFI_CONNECT_TASK_PRIORITY, nullptr, WIFI_CONNECT_TASK_STACK_CORE) != pdPASS) {
        delete context;
        processWifiConnect(WIFI_CONNECT_FAIL);
        return false;
    }

    return true;
}

bool AppSettings::restoreWifiCredentials(void)
{
    if (!_nvs_param_map[NVS_KEY_WIFI_ENABLE]) {
        return false;
    }

    SavedWifiCredential credential;
    if (!selectAutoConnectWifiCredential(credential)) {
        return false;
    }

    snprintf(st_wifi_ssid, sizeof(st_wifi_ssid), "%s", credential.ssid.c_str());
    snprintf(st_wifi_password, sizeof(st_wifi_password), "%s", credential.password.c_str());

    wifi_config_t wifi_config = {};
    populateWifiStaConfig(wifi_config, credential);

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restore Wi-Fi config: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Restored Wi-Fi credentials for SSID: %s", st_wifi_ssid);
    return true;
}

esp_err_t AppSettings::applyWifiOperatingMode(bool reconnect_sta, const char *reason)
{
    if (s_wifi_event_group == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const bool wifi_enabled = _nvs_param_map[NVS_KEY_WIFI_ENABLE] != 0;
    const bool ap_enabled = wifi_enabled && (_nvs_param_map[NVS_KEY_WIFI_AP_ENABLE] != 0);

    if (st_wifi_ap_ssid[0] == '\0') {
        loadNvsStringParam(NVS_KEY_WIFI_AP_SSID, st_wifi_ap_ssid, sizeof(st_wifi_ap_ssid));
    }
    if (st_wifi_ap_password[0] == '\0') {
        loadNvsStringParam(NVS_KEY_WIFI_AP_PASSWORD, st_wifi_ap_password, sizeof(st_wifi_ap_password));
    }
    if ((st_wifi_ssid[0] == '\0') && wifi_enabled) {
        SavedWifiCredential credential;
        if (loadLatestSavedWifiCredential(credential)) {
            snprintf(st_wifi_ssid, sizeof(st_wifi_ssid), "%s", credential.ssid.c_str());
            snprintf(st_wifi_password, sizeof(st_wifi_password), "%s", credential.password.c_str());
        }
    }

    stopWifiScan();
    _suppressDisconnectRecovery = true;
    esp_err_t err = esp_wifi_disconnect();
    if (!is_ignorable_wifi_transition_error(err)) {
        ESP_LOGW(TAG, "Wi-Fi disconnect before mode switch failed: %s", esp_err_to_name(err));
    }

    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_CONNECTED | WIFI_EVENT_CONNECTING);
    system_ui_service::set_wifi_connected(false);

    err = esp_wifi_stop();
    if (!is_ignorable_wifi_transition_error(err)) {
        ESP_LOGE(TAG, "Wi-Fi stop before mode switch failed: %s", esp_err_to_name(err));
        _suppressDisconnectRecovery = false;
        return err;
    }

    if (!wifi_enabled) {
        err = esp_wifi_set_mode(WIFI_MODE_NULL);
        if ((err != ESP_OK) && (err != ESP_ERR_WIFI_NOT_INIT)) {
            ESP_LOGW(TAG, "Failed to set Wi-Fi NULL mode while disabled: %s", esp_err_to_name(err));
        }

        refreshSavedWifiUi();
        refreshAboutWifiUi();
        refreshWifiApUi();
        return ESP_OK;
    }

    s_wifi_restore_in_progress = true;

    const wifi_mode_t mode = ap_enabled ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    err = esp_wifi_set_mode(mode);
    if (err != ESP_OK) {
        s_wifi_restore_in_progress = false;
        _suppressDisconnectRecovery = false;
        ESP_LOGE(TAG, "Failed to set Wi-Fi mode %d: %s", static_cast<int>(mode), esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err != ESP_OK) {
        s_wifi_restore_in_progress = false;
        _suppressDisconnectRecovery = false;
        ESP_LOGE(TAG, "Failed to set Wi-Fi storage before mode switch: %s", esp_err_to_name(err));
        return err;
    }

    if (st_wifi_ssid[0] != '\0') {
        wifi_config_t sta_config = {};
        populateWifiStaConfig(sta_config, sanitizeWifiCredential(st_wifi_ssid, st_wifi_password));
        err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        if (err != ESP_OK) {
            s_wifi_restore_in_progress = false;
            _suppressDisconnectRecovery = false;
            ESP_LOGE(TAG, "Failed to set station config: %s", esp_err_to_name(err));
            return err;
        }
    }

    if (ap_enabled) {
        wifi_config_t ap_config = {};
        populateWifiApConfig(ap_config, sanitizeWifiApCredential(st_wifi_ap_ssid, st_wifi_ap_password));
        err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        if (err != ESP_OK) {
            s_wifi_restore_in_progress = false;
            _suppressDisconnectRecovery = false;
            ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(err));
            return err;
        }
    }

    err = esp_wifi_start();
    s_wifi_restore_in_progress = false;
    if (err != ESP_OK) {
        _suppressDisconnectRecovery = false;
        ESP_LOGE(TAG, "Failed to restart Wi-Fi after mode switch: %s", esp_err_to_name(err));
        return err;
    }

    _suppressDisconnectRecovery = false;

    if (reconnect_sta && (st_wifi_ssid[0] != '\0')) {
        requestWifiConnect(reason);
    }

    refreshSavedWifiUi();
    refreshAboutWifiUi();
    refreshWifiApUi();
    return ESP_OK;
}

esp_err_t AppSettings::initWifi()
{
    s_wifi_runtime_ready = false;
    s_wifi_event_group = xEventGroupCreate();
    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_CONNECTED);
    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_INIT_DONE);
    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_SCANING);
    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_CONNECTING);
    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_SCAN_RUNNING);
    if (!(xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_UI_INIT_DONE)) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_UI_INIT_DONE);
    }

    esp_err_t err = esp_netif_init();
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        ESP_LOGE(TAG, "Failed to initialize esp_netif: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == nullptr) {
        ESP_LOGE(TAG, "Failed to create default station netif");
        return ESP_FAIL;
    }

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == nullptr) {
        ESP_LOGE(TAG, "Failed to create default SoftAP netif");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize hosted Wi-Fi: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Wi-Fi storage after init: %s", esp_err_to_name(err));
        return err;
    }

    esp_event_handler_instance_t instance_any_id;
    err = esp_event_handler_instance_register(WIFI_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              &wifiEventHandler,
                                              this,
                                              &instance_any_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register Wi-Fi event handler: %s", esp_err_to_name(err));
        return err;
    }

    esp_event_handler_instance_t instance_got_ip;
    err = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              &wifiEventHandler,
                                              this,
                                              &instance_got_ip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(err));
        return err;
    }

    loadNvsStringParam(NVS_KEY_WIFI_AP_SSID, st_wifi_ap_ssid, sizeof(st_wifi_ap_ssid));
    loadNvsStringParam(NVS_KEY_WIFI_AP_PASSWORD, st_wifi_ap_password, sizeof(st_wifi_ap_password));
    const bool has_saved_wifi = restoreWifiCredentials();
    err = applyWifiOperatingMode(has_saved_wifi, "saved credentials");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply initial Wi-Fi operating mode: %s", esp_err_to_name(err));
        return err;
    }

    s_wifi_runtime_ready = true;

    return ESP_OK;
}

void AppSettings::requestWifiConnect(const char *reason)
{
    if ((s_wifi_event_group == nullptr) || !_nvs_param_map[NVS_KEY_WIFI_ENABLE] || (st_wifi_ssid[0] == '\0')) {
        return;
    }

    const EventBits_t wifi_bits = xEventGroupGetBits(s_wifi_event_group);
    if ((wifi_bits & WIFI_EVENT_CONNECTED) || (wifi_bits & WIFI_EVENT_CONNECTING)) {
        return;
    }

    esp_err_t err = esp_wifi_connect();
    if ((err == ESP_OK) || (err == ESP_ERR_WIFI_CONN)) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_EVENT_CONNECTING);
        ESP_LOGI(TAG, "Queued Wi-Fi connect (%s) for SSID:%s", (reason != nullptr) ? reason : "unknown", st_wifi_ssid);
        return;
    }

    ESP_LOGW(TAG, "Wi-Fi connect request (%s) failed to start: %s",
             (reason != nullptr) ? reason : "unknown", esp_err_to_name(err));
}

void AppSettings::startWifiScan(void)
{
    ESP_LOGI(TAG, "Start Wi-Fi scan");
    xEventGroupSetBits(s_wifi_event_group, WIFI_EVENT_SCANING);
    lv_obj_clear_flag(ui_SpinnerScreenSettingWiFi, LV_OBJ_FLAG_HIDDEN);
    if (lv_obj_ready(ui_SwitchPanelScreenSettingWiFiSwitch)) {
        lv_obj_add_flag(ui_SwitchPanelScreenSettingWiFiSwitch, LV_OBJ_FLAG_CLICKABLE);
    }
}

AppSettings::WifiSignalStrengthLevel_t AppSettings::wifiSignalStrengthFromRssi(int rssi) const
{
    if (rssi > -100 && rssi <= -80) {
        return WIFI_SIGNAL_STRENGTH_WEAK;
    }

    if (rssi > -80 && rssi <= -60) {
        return WIFI_SIGNAL_STRENGTH_MODERATE;
    }

    if (rssi > -60) {
        return WIFI_SIGNAL_STRENGTH_GOOD;
    }

    return WIFI_SIGNAL_STRENGTH_NONE;
}

void AppSettings::stopWifiScan(void)
{
    if (s_wifi_event_group == nullptr) {
        return;
    }

    ESP_LOGI(TAG, "Stop Wi-Fi scan");
    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_SCANING | WIFI_EVENT_SCAN_RUNNING);
    _wifiScanUiStateKey.clear();
    esp_err_t err = esp_wifi_scan_stop();
    if ((err != ESP_OK) && (err != ESP_ERR_WIFI_STATE) && (err != ESP_ERR_WIFI_NOT_INIT) &&
        (err != ESP_ERR_WIFI_NOT_STARTED)) {
        ESP_LOGW(TAG, "Stop Wi-Fi scan request failed: %s", esp_err_to_name(err));
    }
    if (lv_obj_ready(ui_PanelScreenSettingWiFiList)) {
        lv_obj_add_flag(ui_PanelScreenSettingWiFiList, LV_OBJ_FLAG_HIDDEN);
    }
    if (lv_obj_ready(ui_SpinnerScreenSettingWiFi)) {
        lv_obj_add_flag(ui_SpinnerScreenSettingWiFi, LV_OBJ_FLAG_HIDDEN);
    }
    if (lv_obj_ready(ui_SwitchPanelScreenSettingWiFiSwitch)) {
        lv_obj_add_flag(ui_SwitchPanelScreenSettingWiFiSwitch, LV_OBJ_FLAG_CLICKABLE);
    }
    deinitWifiListButton();
}

void AppSettings::scanWifiAndUpdateUi(void)
{
    uint16_t number = SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[SCAN_LIST_SIZE];
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    esp_err_t err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to fetch Wi-Fi scan count: %s", esp_err_to_name(err));
        return;
    }

    err = esp_wifi_scan_get_ap_records(&number, ap_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to fetch Wi-Fi scan records: %s", esp_err_to_name(err));
        return;
    }
#if ENABLE_DEBUG_LOG
    ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);
#endif

    std::string scan_ui_key;
    int displayed_count = 0;
    for (int i = 0; (i < SCAN_LIST_SIZE) && (i < ap_count) && (displayed_count < SCAN_LIST_SIZE); i++) {
        if (ap_info[i].ssid[0] == '\0') {
            continue;
        }

        const bool psk_flag = (ap_info[i].authmode != WIFI_AUTH_OPEN) && (ap_info[i].authmode != WIFI_AUTH_OWE);
        const WifiSignalStrengthLevel_t signal_strength = wifiSignalStrengthFromRssi(ap_info[i].rssi);
        scan_ui_key += reinterpret_cast<const char *>(ap_info[i].ssid);
        scan_ui_key += '|';
        scan_ui_key += std::to_string(static_cast<int>(psk_flag));
        scan_ui_key += '|';
        scan_ui_key += std::to_string(static_cast<int>(signal_strength));
        scan_ui_key += '|';
        scan_ui_key += std::to_string(strcmp(reinterpret_cast<const char *>(ap_info[i].ssid), st_wifi_ssid) == 0 ? 1 : 0);
        scan_ui_key += ';';
        displayed_count++;
    }

    if (_wifiScanUiStateKey == scan_ui_key) {
        refreshSavedWifiUi();
        return;
    }

    _wifiScanUiStateKey = scan_ui_key;

    const bool can_render_scan_list = isUiActive() && (_screen_index == UI_WIFI_SCAN_INDEX);

    if (can_render_scan_list) {
        bsp_display_lock(0);
        deinitWifiListButton();
        bsp_display_unlock();
    }

    displayed_count = 0;
    for (int i = 0; (i < SCAN_LIST_SIZE) && (i < ap_count) && (displayed_count < SCAN_LIST_SIZE); i++) {
#if ENABLE_DEBUG_LOG
        ESP_LOGI(TAG, "SSID \t\t%s", ap_info[i].ssid);
        ESP_LOGI(TAG, "RSSI \t\t%d", ap_info[i].rssi);
        ESP_LOGI(TAG, "Channel \t\t%d", ap_info[i].primary);
#endif

        if (ap_info[i].ssid[0] == '\0') {
            continue;
        }

        const bool psk_flag = (ap_info[i].authmode != WIFI_AUTH_OPEN) && (ap_info[i].authmode != WIFI_AUTH_OWE);
#if ENABLE_DEBUG_LOG
        ESP_LOGI(TAG, "psk_flag: %d", psk_flag);
#endif

        _wifi_signal_strength_level = wifiSignalStrengthFromRssi(ap_info[i].rssi);
#if ENABLE_DEBUG_LOG
        ESP_LOGI(TAG, "signal_strength: %d", _wifi_signal_strength_level);
#endif

        if (can_render_scan_list) {
            bsp_display_lock(0);
            initWifiListButton(label_wifi_ssid[displayed_count], img_img_wifi_lock[displayed_count],
                               wifi_image[displayed_count], wifi_connect[displayed_count],
                               ap_info[i].ssid, psk_flag, _wifi_signal_strength_level);
            displayed_count++;
            bsp_display_unlock();
        }
    }

    refreshSavedWifiUi();
}

void AppSettings::initWifiListButton(lv_obj_t *lv_label_ssid, lv_obj_t *lv_img_wifi_lock, lv_obj_t *lv_wifi_img,
                                     lv_obj_t *lv_wifi_connect, uint8_t *ssid, bool psk, WifiSignalStrengthLevel_t signal_strength)
{
    if (lv_obj_ready(lv_img_wifi_lock)) {
        lv_obj_add_flag(lv_img_wifi_lock, LV_OBJ_FLAG_HIDDEN);
    }
    if (lv_obj_ready(lv_wifi_connect)) {
        lv_obj_add_flag(lv_wifi_connect, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text_fmt(lv_label_ssid, "%s", (const char *)ssid);
    if (lv_label_ssid != nullptr) {
        lv_obj_clear_flag(lv_obj_get_parent(lv_label_ssid), LV_OBJ_FLAG_HIDDEN);
    }

    if (strcmp((const char *)ssid, (const char *)st_wifi_ssid) == 0) {
        lv_obj_clear_flag(lv_wifi_connect, LV_OBJ_FLAG_HIDDEN);
    }

    if (psk) {
        lv_img_set_src(lv_img_wifi_lock, &img_wifi_lock);
        lv_obj_clear_flag(lv_img_wifi_lock, LV_OBJ_FLAG_HIDDEN);
    }

    if (signal_strength == WIFI_SIGNAL_STRENGTH_GOOD) {
        lv_img_set_src(lv_wifi_img, &img_wifisignal_good);
    } else if (signal_strength == WIFI_SIGNAL_STRENGTH_MODERATE) {
        lv_img_set_src(lv_wifi_img, &img_wifisignal_moderate);
    } else if (signal_strength == WIFI_SIGNAL_STRENGTH_WEAK) {
        lv_img_set_src(lv_wifi_img, &img_wifisignal_wake);
    } else {
        lv_img_set_src(lv_wifi_img, &img_wifisignal_absent);
    }
}

void AppSettings::deinitWifiListButton(void)
{
    for (int i = 0; i < SCAN_LIST_SIZE; i++) {
        if (lv_obj_ready(panel_wifi_btn[i])) {
            lv_obj_add_flag(panel_wifi_btn[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (lv_obj_ready(label_wifi_ssid[i])) {
            lv_label_set_text(label_wifi_ssid[i], "");
        }
        if (lv_obj_ready(img_img_wifi_lock[i])) {
            lv_obj_add_flag(img_img_wifi_lock[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (lv_obj_ready(wifi_connect[i])) {
            lv_obj_add_flag(wifi_connect[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void AppSettings::wifiScanTask(void *arg)
{
    AppSettings *app = (AppSettings *)arg;
    esp_err_t ret = ESP_OK;

    if (app == NULL) {
        ESP_LOGE(TAG, "App instance is NULL");
        goto err;
    }

    ret = app->initWifi();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init Wi-Fi failed");
        goto err;
    }

    if (ret == ESP_OK) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_EVENT_INIT_DONE);
        ESP_LOGI(TAG, "wifi_init done");
    } else {
        ESP_LOGE(TAG, "wifi_init failed");
    }

    while (true) {
        if ((xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_INIT_DONE) &&
            (xEventGroupGetBits(s_wifi_event_group) & WIFI_EVENT_UI_INIT_DONE)) {
            if (app->isUiActive() && lv_obj_ready(ui_SwitchPanelScreenSettingWiFiSwitch)) {
                lv_obj_add_flag(ui_SwitchPanelScreenSettingWiFiSwitch, LV_OBJ_FLAG_CLICKABLE);
            }
            xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_INIT_DONE);
            xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_UI_INIT_DONE);
        }

        const EventBits_t wifi_bits = xEventGroupGetBits(s_wifi_event_group);
        if ((wifi_bits & WIFI_EVENT_SCANING) && !(wifi_bits & WIFI_EVENT_SCAN_RUNNING)) {
            esp_err_t scan_err = esp_wifi_scan_start(nullptr, false);
            if (scan_err == ESP_OK) {
                xEventGroupSetBits(s_wifi_event_group, WIFI_EVENT_SCAN_RUNNING);
            } else if ((scan_err != ESP_ERR_WIFI_STATE) && (scan_err != ESP_ERR_WIFI_NOT_INIT) &&
                       (scan_err != ESP_ERR_WIFI_NOT_STARTED)) {
                ESP_LOGW(TAG, "Failed to start async Wi-Fi scan: %s", esp_err_to_name(scan_err));
            }
            vTaskDelay(pdMS_TO_TICKS(WIFI_SCAN_TASK_PERIOD_MS));
        }

        const EventBits_t connected_bits = xEventGroupGetBits(s_wifi_event_group);
        if ((connected_bits & WIFI_EVENT_CONNECTED) && app->_autoTimezoneRefreshPending) {
            app->_autoTimezoneRefreshPending = false;
            if (app->_nvs_param_map[NVS_KEY_DISPLAY_TZ_AUTO]) {
                app->syncAutoTimezoneFromInternet();
            } else {
                app->applyManualTimezonePreference();
            }
            app_sntp_init();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

err:
    vTaskDelete(NULL);
}

void AppSettings::wifiConnectTask(void *arg)
{
    WifiConnectTaskContext *context = static_cast<WifiConnectTaskContext *>(arg);
    if ((context == nullptr) || (context->app == nullptr)) {
        delete context;
        vTaskDelete(NULL);
        return;
    }

    AppSettings *app = context->app;
    const SavedWifiCredential credential = app->sanitizeWifiCredential(context->credential.ssid.c_str(),
                                                                      context->credential.password.c_str());
    const SavedWifiCredential previous_credential = app->sanitizeWifiCredential(context->previous_credential.ssid.c_str(),
                                                                               context->previous_credential.password.c_str());
    const bool has_previous_connection = context->has_previous_connection && !previous_credential.ssid.empty();
    const bool dismiss_keyboard = context->dismiss_keyboard;
    const bool navigate_back_on_success = context->navigate_back_on_success;
    delete context;

    wifi_config_t wifi_config = {0};

    app->_suppressDisconnectRecovery = true;
    esp_wifi_disconnect();
    system_ui_service::set_wifi_connected(false);

    snprintf(st_wifi_ssid, sizeof(st_wifi_ssid), "%s", credential.ssid.c_str());
    snprintf(st_wifi_password, sizeof(st_wifi_password), "%s", credential.password.c_str());

    app->populateWifiStaConfig(wifi_config, credential);

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_CONNECTED | WIFI_EVENT_CONNECTING);

    app->setNvsParam(NVS_KEY_WIFI_ENABLE, 1);
    app->_nvs_param_map[NVS_KEY_WIFI_ENABLE] = true;
    ESP_LOGI(TAG, "Starting Wi-Fi connection for SSID: %s", st_wifi_ssid);
    app->requestWifiConnect("manual connection");

    bool connection_succeeded = false;
    const TickType_t wait_step = pdMS_TO_TICKS(250);
    TickType_t waited_ticks = 0;
    const TickType_t max_wait_ticks = pdMS_TO_TICKS(WIFI_CONNECT_RET_WAIT_TIME_MS + 8000);
    while (waited_ticks < max_wait_ticks) {
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                WIFI_EVENT_CONNECTED,
                pdFALSE,
                pdFALSE,
                wait_step);
        if (bits & WIFI_EVENT_CONNECTED) {
            connection_succeeded = true;
            break;
        }

        wifi_ap_record_t current_ap = {};
        if (esp_wifi_sta_get_ap_info(&current_ap) == ESP_OK) {
            const char *current_ssid = reinterpret_cast<const char *>(current_ap.ssid);
            if ((current_ssid != nullptr) && (strcmp(current_ssid, credential.ssid.c_str()) == 0)) {
                connection_succeeded = true;
                break;
            }
        }

        waited_ticks += wait_step;
    }

    if (connection_succeeded) {
        ESP_LOGI(TAG, "Connected successfully");
        if (!app->rememberWifiCredential(credential)) {
            ESP_LOGW(TAG, "Failed to persist Wi-Fi credentials for SSID:%s", credential.ssid.c_str());
        }

        if (!app->_is_ui_del) {
            bsp_display_lock(0);
            app->processWifiConnect(WIFI_CONNECT_SUCCESS);
            bsp_display_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_UI_WAIT_TIME_MS));

        if (!app->_is_ui_del) {
            bsp_display_lock(0);
            app->processWifiConnect(WIFI_CONNECT_HIDE);
            if (dismiss_keyboard) {
                app->setWifiKeyboardVisible(false);
                jc4880_password_textarea_set_visibility(ui_TextAreaScreenSettingVerificationPassword, false);
                lv_textarea_set_text(ui_TextAreaScreenSettingVerificationPassword, "");
            }
            app->refreshSavedWifiUi();
            app->refreshAboutWifiUi();
            if (navigate_back_on_success) {
                app->back();
            }
            bsp_display_unlock();
        }
    } else {
        ESP_LOGI(TAG, "Connect failed");

        if (has_previous_connection) {
            wifi_config_t previous_wifi_config = {0};
            app->populateWifiStaConfig(previous_wifi_config, previous_credential);

            snprintf(st_wifi_ssid, sizeof(st_wifi_ssid), "%s", previous_credential.ssid.c_str());
            snprintf(st_wifi_password, sizeof(st_wifi_password), "%s", previous_credential.password.c_str());

            esp_err_t start_err = esp_wifi_start();
            if ((start_err == ESP_OK) || (start_err == ESP_ERR_WIFI_CONN)) {
                esp_err_t storage_err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
                esp_err_t config_err = esp_wifi_set_config(WIFI_IF_STA, &previous_wifi_config);
                if ((storage_err == ESP_OK) && (config_err == ESP_OK)) {
                    xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_CONNECTED | WIFI_EVENT_CONNECTING);
                    app->requestWifiConnect("restore previous connection after failed switch");
                    ESP_LOGI(TAG, "Restoring previous Wi-Fi connection for SSID:%s", previous_credential.ssid.c_str());
                } else {
                    ESP_LOGW(TAG, "Failed to restore previous Wi-Fi config after failed switch: storage=%s config=%s",
                             esp_err_to_name(storage_err), esp_err_to_name(config_err));
                }
            } else {
                ESP_LOGW(TAG, "Failed to restart Wi-Fi while restoring previous connection: %s", esp_err_to_name(start_err));
            }
        }

        if (!app->_is_ui_del) {
            bsp_display_lock(0);
            app->processWifiConnect(WIFI_CONNECT_FAIL);
            bsp_display_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_UI_WAIT_TIME_MS));

        if (!app->_is_ui_del) {
            bsp_display_lock(0);
            app->processWifiConnect(WIFI_CONNECT_HIDE);
            if (dismiss_keyboard) {
                app->setWifiKeyboardVisible(false);
                jc4880_password_textarea_set_visibility(ui_TextAreaScreenSettingVerificationPassword, false);
                lv_textarea_set_text(ui_TextAreaScreenSettingVerificationPassword, "");
            }
            if (!navigate_back_on_success && (app->_screen_index == UI_WIFI_SCAN_INDEX)) {
                app->refreshSavedWifiUi();
            }
            bsp_display_unlock();
        }
    }

    vTaskDelete(NULL);
}

void AppSettings::wifiEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    AppSettings *app = (AppSettings *)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Connected to AP SSID:%s", st_wifi_ssid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "SoftAP started with SSID:%s", (st_wifi_ap_ssid[0] != '\0') ? st_wifi_ap_ssid : kWifiApDefaultSsid);
        if (app != nullptr) {
            app->refreshWifiApUi();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STOP) {
        ESP_LOGI(TAG, "SoftAP stopped");
        if (app != nullptr) {
            app->refreshWifiApUi();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if ((app != nullptr) && !s_wifi_restore_in_progress) {
            app->requestWifiConnect("station start");
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_CONNECTED | WIFI_EVENT_CONNECTING);
        ESP_LOGI(TAG, "Disconnected from AP SSID:%s", st_wifi_ssid);
        system_ui_service::set_wifi_connected(false);
        if (app != nullptr) {
            app->refreshSavedWifiUi();
            app->refreshAboutWifiUi();
            if (app->_suppressDisconnectRecovery) {
                app->_suppressDisconnectRecovery = false;
            } else {
                app->requestWifiConnect("disconnect recovery");
            }
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        const EventBits_t scan_bits = xEventGroupGetBits(s_wifi_event_group);
        const bool should_render_scan = (scan_bits & WIFI_EVENT_SCANING) != 0;
        xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_SCAN_RUNNING | WIFI_EVENT_SCANING);
        if ((app != nullptr) && should_render_scan) {
            app->scanWifiAndUpdateUi();
        }

        if ((app != nullptr) && app->isUiActive() && lv_obj_ready(ui_PanelScreenSettingWiFiList) &&
           lv_obj_ready(ui_SpinnerScreenSettingWiFi) && lv_obj_ready(ui_SwitchPanelScreenSettingWiFiSwitch) &&
           lv_obj_has_flag(ui_PanelScreenSettingWiFiList, LV_OBJ_FLAG_HIDDEN)) {
            bsp_display_lock(0);
            lv_obj_clear_flag(ui_PanelScreenSettingWiFiList, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_SpinnerScreenSettingWiFi, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_SwitchPanelScreenSettingWiFiSwitch, LV_OBJ_FLAG_CLICKABLE);
            bsp_display_unlock();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_EVENT_CONNECTING);
        xEventGroupSetBits(s_wifi_event_group, WIFI_EVENT_CONNECTED);
        system_ui_service::refresh_wifi_from_driver();
        if (app != nullptr) {
            app->_autoTimezoneRefreshPending = true;
            app->refreshSavedWifiUi();
            app->refreshAboutWifiUi();
        }
    }
}