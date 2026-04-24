#include "WebServerApp.hpp"

#include <string>
#include <vector>

#include "WebServerService.hpp"

LV_IMG_DECLARE(img_app_web_server);

namespace {

static constexpr int kQuickAccessActionToggleServer = 0x57454231;

lv_obj_t *create_card(lv_obj_t *parent, lv_coord_t width, lv_coord_t height, lv_coord_t x, lv_coord_t y, lv_color_t color)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, width, height);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_radius(card, 26, 0);
    lv_obj_set_style_bg_color(card, color, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 28, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

} // namespace

WebServerApp::WebServerApp()
    : ESP_Brookesia_PhoneApp("Web Server", &img_app_web_server, true),
      _screen(nullptr),
      _statusBadge(nullptr),
      _statusTitle(nullptr),
      _statusDetail(nullptr),
      _sourceLabel(nullptr),
      _urlLabel(nullptr),
      _recoveryLabel(nullptr),
      _mdnsLabel(nullptr),
      _toggleButton(nullptr),
      _toggleButtonLabel(nullptr),
      _hintLabel(nullptr)
{
}

bool WebServerApp::init()
{
    return true;
}

bool WebServerApp::run()
{
    if (!ensureUiReady()) {
        return false;
    }
    refreshUi();
    lv_scr_load(_screen);
    return true;
}

bool WebServerApp::pause()
{
    return true;
}

bool WebServerApp::resume()
{
    if (!ensureUiReady()) {
        return false;
    }
    refreshUi();
    return true;
}

bool WebServerApp::back()
{
    return notifyCoreClosed();
}

bool WebServerApp::close()
{
    return true;
}

std::vector<ESP_Brookesia_PhoneQuickAccessActionData_t> WebServerApp::getQuickAccessActions() const
{
    return {{kQuickAccessActionToggleServer, LV_SYMBOL_POWER, true}};
}

ESP_Brookesia_PhoneQuickAccessDetailData_t WebServerApp::getQuickAccessDetail() const
{
    ESP_Brookesia_PhoneQuickAccessDetailData_t detail;
    detail.text = WebServerService::instance().statusText();
    detail.scroll_text = detail.text.size() > 34;
    return detail;
}

bool WebServerApp::handleQuickAccessAction(int action_id)
{
    if (action_id != kQuickAccessActionToggleServer) {
        return ESP_Brookesia_PhoneApp::handleQuickAccessAction(action_id);
    }

    const bool ok = WebServerService::instance().toggle();
    refreshUi();
    return ok;
}

bool WebServerApp::buildUi()
{
    _screen = lv_obj_create(nullptr);
    if (_screen == nullptr) {
        return false;
    }

    lv_obj_add_event_cb(_screen, onScreenDeleted, LV_EVENT_DELETE, this);
    lv_obj_set_style_bg_color(_screen, lv_color_hex(0xE0F2FE), 0);
    lv_obj_set_style_bg_grad_color(_screen, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_grad_dir(_screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hero = create_card(_screen, 440, 220, 20, 20, lv_color_hex(0x0F172A));
    lv_obj_t *title = lv_label_create(hero);
    lv_label_set_text(title, "Web Server");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 20);

    _statusBadge = lv_obj_create(hero);
    lv_obj_set_size(_statusBadge, 132, 36);
    lv_obj_align(_statusBadge, LV_ALIGN_TOP_RIGHT, -18, 18);
    lv_obj_set_style_radius(_statusBadge, 18, 0);
    lv_obj_set_style_border_width(_statusBadge, 0, 0);
    lv_obj_set_style_pad_all(_statusBadge, 0, 0);
    lv_obj_clear_flag(_statusBadge, LV_OBJ_FLAG_SCROLLABLE);

    _statusTitle = lv_label_create(_statusBadge);
    lv_obj_set_style_text_font(_statusTitle, &lv_font_montserrat_16, 0);
    lv_obj_center(_statusTitle);

    _statusDetail = lv_label_create(hero);
    lv_obj_set_width(_statusDetail, 392);
    lv_label_set_long_mode(_statusDetail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_statusDetail, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_statusDetail, lv_color_hex(0xBFDBFE), 0);
    lv_obj_align(_statusDetail, LV_ALIGN_TOP_LEFT, 20, 72);

    _sourceLabel = lv_label_create(hero);
    lv_obj_set_width(_sourceLabel, 392);
    lv_label_set_long_mode(_sourceLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_sourceLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_sourceLabel, lv_color_hex(0x93C5FD), 0);
    lv_obj_align(_sourceLabel, LV_ALIGN_TOP_LEFT, 20, 128);

    _urlLabel = lv_label_create(hero);
    lv_obj_set_width(_urlLabel, 392);
    lv_label_set_long_mode(_urlLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_urlLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_urlLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_urlLabel, LV_ALIGN_TOP_LEFT, 20, 158);

    lv_obj_t *actions = create_card(_screen, 440, 180, 20, 258, lv_color_hex(0xFFFFFF));
    lv_obj_t *actionsTitle = lv_label_create(actions);
    lv_label_set_text(actionsTitle, "Controls");
    lv_obj_set_style_text_font(actionsTitle, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(actionsTitle, lv_color_hex(0x102A43), 0);
    lv_obj_align(actionsTitle, LV_ALIGN_TOP_LEFT, 18, 16);

    _toggleButton = lv_btn_create(actions);
    lv_obj_set_size(_toggleButton, 196, 60);
    lv_obj_align(_toggleButton, LV_ALIGN_TOP_LEFT, 18, 62);
    lv_obj_set_style_radius(_toggleButton, 18, 0);
    lv_obj_set_style_border_width(_toggleButton, 0, 0);
    lv_obj_add_event_cb(_toggleButton, onToggleClicked, LV_EVENT_CLICKED, this);

    _toggleButtonLabel = lv_label_create(_toggleButton);
    lv_obj_set_style_text_font(_toggleButtonLabel, &lv_font_montserrat_18, 0);
    lv_obj_center(_toggleButtonLabel);

    lv_obj_t *refreshButton = lv_btn_create(actions);
    lv_obj_set_size(refreshButton, 196, 60);
    lv_obj_align(refreshButton, LV_ALIGN_TOP_RIGHT, -18, 62);
    lv_obj_set_style_radius(refreshButton, 18, 0);
    lv_obj_set_style_bg_color(refreshButton, lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_bg_opa(refreshButton, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(refreshButton, 0, 0);
    lv_obj_add_event_cb(refreshButton, onRefreshClicked, LV_EVENT_CLICKED, this);

    lv_obj_t *refreshLabel = lv_label_create(refreshButton);
    lv_label_set_text(refreshLabel, "Refresh Status");
    lv_obj_set_style_text_font(refreshLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(refreshLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(refreshLabel);

    _recoveryLabel = lv_label_create(actions);
    lv_obj_set_width(_recoveryLabel, 402);
    lv_label_set_long_mode(_recoveryLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_recoveryLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_recoveryLabel, lv_color_hex(0x334155), 0);
    lv_obj_align(_recoveryLabel, LV_ALIGN_TOP_LEFT, 18, 132);

    _mdnsLabel = lv_label_create(_screen);
    lv_obj_set_width(_mdnsLabel, 440);
    lv_label_set_long_mode(_mdnsLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_mdnsLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_mdnsLabel, lv_color_hex(0x0F172A), 0);
    lv_obj_align(_mdnsLabel, LV_ALIGN_TOP_LEFT, 20, 454);

    _hintLabel = lv_label_create(_screen);
    lv_obj_set_width(_hintLabel, 440);
    lv_label_set_long_mode(_hintLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_hintLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_hintLabel, lv_color_hex(0x334155), 0);
    lv_obj_align(_hintLabel, LV_ALIGN_TOP_LEFT, 20, 510);

    return true;
}

bool WebServerApp::ensureUiReady()
{
    if ((_screen != nullptr) && lv_obj_is_valid(_screen)) {
        return true;
    }

    resetUiPointers();
    return buildUi();
}

void WebServerApp::refreshUi()
{
    if ((_screen == nullptr) || !lv_obj_is_valid(_screen)) {
        return;
    }

    WebServerService &service = WebServerService::instance();
    const bool running = service.isRunning();

    lv_obj_set_style_bg_color(_statusBadge, running ? lv_color_hex(0x16A34A) : lv_color_hex(0x475569), 0);
    lv_obj_set_style_bg_opa(_statusBadge, LV_OPA_COVER, 0);
    lv_label_set_text(_statusTitle, running ? "Running" : "Stopped");
    lv_obj_set_style_text_color(_statusTitle, lv_color_hex(0xFFFFFF), 0);

    lv_label_set_text(_statusDetail, service.statusText().c_str());
    lv_label_set_text_fmt(_sourceLabel, "Content source: %s", service.sourceSummary().c_str());

    const std::string primary = service.primaryUrl();
    lv_label_set_text(_urlLabel,
                      primary.empty() ? "Reachable URL: waiting for Wi-Fi or AP mode IP assignment"
                                      : (std::string("Reachable URL: ") + primary).c_str());

    lv_obj_set_style_bg_color(_toggleButton, running ? lv_color_hex(0xDC2626) : lv_color_hex(0x22C55E), 0);
    lv_obj_set_style_bg_opa(_toggleButton, LV_OPA_COVER, 0);
    lv_label_set_text(_toggleButtonLabel, running ? "Stop Server" : "Start Server");
    lv_obj_set_style_text_color(_toggleButtonLabel, lv_color_hex(0xFFFFFF), 0);

    const std::string recovery = service.recoveryUrl();
    lv_label_set_text(_recoveryLabel,
                      recovery.empty() ?
                          "Recovery: /recovery will be available once the device has an IP address. Uploads target /sdcard/web."
                                          : (std::string("Recovery: ") + recovery + " uploads files into /sdcard/web").c_str());

    const std::string mdns = service.mdnsUrl();
    lv_label_set_text(_mdnsLabel,
                      mdns.empty() ? "mDNS hostname becomes available after the server registers on the network."
                                   : (std::string("mDNS: ") + mdns).c_str());

    lv_label_set_text(_hintLabel,
                      service.isApModeActive() ?
                          "AP mode is active, so common captive-portal probe requests will be redirected into the device web UI."
                                                : "Enable AP mode in Settings if you want clients to discover this through a hotspot and captive-portal probes.");
}

void WebServerApp::resetUiPointers()
{
    _screen = nullptr;
    _statusBadge = nullptr;
    _statusTitle = nullptr;
    _statusDetail = nullptr;
    _sourceLabel = nullptr;
    _urlLabel = nullptr;
    _recoveryLabel = nullptr;
    _mdnsLabel = nullptr;
    _toggleButton = nullptr;
    _toggleButtonLabel = nullptr;
    _hintLabel = nullptr;
}

void WebServerApp::onToggleClicked(lv_event_t *event)
{
    auto *app = static_cast<WebServerApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    WebServerService::instance().toggle();
    app->refreshUi();
}

void WebServerApp::onRefreshClicked(lv_event_t *event)
{
    auto *app = static_cast<WebServerApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    app->refreshUi();
}

void WebServerApp::onScreenDeleted(lv_event_t *event)
{
    auto *app = static_cast<WebServerApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    app->resetUiPointers();
}