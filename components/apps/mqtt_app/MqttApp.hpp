#pragma once

#include <string>

#include "esp_brookesia.hpp"
#include "mqtt_client.h"
#include "lvgl.h"

class MqttApp: public ESP_Brookesia_PhoneApp {
public:
    MqttApp();
    ~MqttApp() override = default;

    bool init() override;
    bool run() override;
    bool pause() override;
    bool resume() override;
    bool back() override;
    bool close() override;

private:
    struct AsyncStatus {
        MqttApp *app = nullptr;
        std::string text;
        bool connected = false;
    };

    bool buildUi();
    bool ensureUiReady();
    bool hasLiveScreen() const;
    bool connectClient();
    void disconnectClient();
    void setStatus(const std::string &status);
    void updateButtonState();
    void resetUiPointers();

    static void onScreenDeleted(lv_event_t *event);
    static void onConnectClicked(lv_event_t *event);
    static void onDisconnectClicked(lv_event_t *event);
    static void onMqttEvent(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
    static void applyAsyncStatus(void *context);

    lv_obj_t *_screen;
    lv_obj_t *_statusLabel;
    lv_obj_t *_uriInput;
    lv_obj_t *_usernameInput;
    lv_obj_t *_passwordInput;
    lv_obj_t *_clientIdInput;
    lv_obj_t *_topicInput;
    lv_obj_t *_connectButton;
    lv_obj_t *_disconnectButton;
    esp_mqtt_client_handle_t _client;
    std::string _brokerUri;
    std::string _username;
    std::string _password;
    std::string _clientId;
    std::string _topic;
    bool _connected;
};