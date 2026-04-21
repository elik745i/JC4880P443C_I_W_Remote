/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "lvgl.h"
#include "esp_brookesia.hpp"

class MusicPlayer: public ESP_Brookesia_PhoneApp {
public:
    enum QuickAccessAction {
        QUICK_ACCESS_ACTION_PREVIOUS = 1,
        QUICK_ACCESS_ACTION_TOGGLE_PLAYBACK,
        QUICK_ACCESS_ACTION_NEXT,
    };

    MusicPlayer();
    ~MusicPlayer();

    bool run(void)override;
    bool back(void)override;
    bool close(void)override;

    bool init(void) override;
    bool pause(void) override;
    std::vector<ESP_Brookesia_PhoneQuickAccessActionData_t> getQuickAccessActions(void) const override;
    ESP_Brookesia_PhoneQuickAccessDetailData_t getQuickAccessDetail(void) const override;
    bool handleQuickAccessAction(int action_id) override;

private:
    void stop_audio_fully(void);
};
