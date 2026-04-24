#pragma once

#include "esp_brookesia.hpp"
#include "lvgl.h"

class WebServerApp: public ESP_Brookesia_PhoneApp {
public:
    WebServerApp();
    ~WebServerApp() override = default;

    bool init() override;
    bool run() override;
    bool pause() override;
    bool resume() override;
    bool back() override;
    bool close() override;

    std::vector<ESP_Brookesia_PhoneQuickAccessActionData_t> getQuickAccessActions() const override;
    ESP_Brookesia_PhoneQuickAccessDetailData_t getQuickAccessDetail() const override;
    bool handleQuickAccessAction(int action_id) override;

private:
    bool buildUi();
    bool ensureUiReady();
    void refreshUi();
    void resetUiPointers();

    static void onToggleClicked(lv_event_t *event);
    static void onRefreshClicked(lv_event_t *event);
    static void onScreenDeleted(lv_event_t *event);

    lv_obj_t *_screen;
    lv_obj_t *_statusBadge;
    lv_obj_t *_statusTitle;
    lv_obj_t *_statusDetail;
    lv_obj_t *_sourceLabel;
    lv_obj_t *_urlLabel;
    lv_obj_t *_recoveryLabel;
    lv_obj_t *_mdnsLabel;
    lv_obj_t *_toggleButton;
    lv_obj_t *_toggleButtonLabel;
    lv_obj_t *_hintLabel;
};