#include "MqttApp.hpp"

#include <cstring>

#include "esp_log.h"

LV_IMG_DECLARE(img_app_mqtt);

namespace {

static constexpr const char *TAG = "MqttApp";

lv_obj_t *create_field(lv_obj_t *parent, const char *label_text, lv_coord_t y, lv_obj_t **out_input,
                       bool password_mode = false)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x0F172A), 0);
    lv_obj_set_pos(label, 18, y);

    lv_obj_t *input = lv_textarea_create(parent);
    lv_obj_set_size(input, 444, 44);
    lv_obj_set_pos(input, 18, y + 22);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_password_mode(input, password_mode);
    *out_input = input;
    return input;
}

} // namespace

MqttApp::MqttApp():
    ESP_Brookesia_PhoneApp("MQTT", &img_app_mqtt, true),
    _screen(nullptr),
    _statusLabel(nullptr),
    _uriInput(nullptr),
    _usernameInput(nullptr),
    _passwordInput(nullptr),
    _clientIdInput(nullptr),
    _topicInput(nullptr),
    _connectButton(nullptr),
    _disconnectButton(nullptr),
    _client(nullptr),
    _brokerUri("mqtt://broker.hivemq.com"),
    _clientId("jc4880_remote"),
    _topic("jc4880/demo"),
    _connected(false)
{
}

bool MqttApp::init()
{
    return true;
}

bool MqttApp::run()
{
    if (!ensureUiReady()) {
        return false;
    }
    updateButtonState();
    setStatus("Enter MQTT connection settings and connect.");
    return true;
}

bool MqttApp::pause()
{
    return true;
}

bool MqttApp::resume()
{
    if (!ensureUiReady()) {
        return false;
    }
    updateButtonState();
    return true;
}

bool MqttApp::back()
{
    return notifyCoreClosed();
}

bool MqttApp::close()
{
    disconnectClient();
    return true;
}

bool MqttApp::buildUi()
{
    _screen = lv_scr_act();
    if (_screen == nullptr) {
        return false;
    }

    lv_obj_set_style_bg_color(_screen, lv_color_hex(0xECFEFF), 0);
    lv_obj_set_style_bg_grad_color(_screen, lv_color_hex(0xE0F2FE), 0);
    lv_obj_set_style_bg_grad_dir(_screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_scroll_dir(_screen, LV_DIR_VER);

    lv_obj_t *title = lv_label_create(_screen);
    lv_label_set_text(title, "MQTT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0C4A6E), 0);
    lv_obj_set_pos(title, 18, 16);

    _statusLabel = lv_label_create(_screen);
    lv_obj_set_width(_statusLabel, 444);
    lv_label_set_long_mode(_statusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_statusLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_statusLabel, lv_color_hex(0x155E75), 0);
    lv_obj_set_pos(_statusLabel, 18, 56);

    create_field(_screen, "Broker URI", 96, &_uriInput);
    lv_textarea_set_text(_uriInput, _brokerUri.c_str());

    create_field(_screen, "Username", 166, &_usernameInput);
    create_field(_screen, "Password", 236, &_passwordInput, true);

    create_field(_screen, "Client ID", 306, &_clientIdInput);
    lv_textarea_set_text(_clientIdInput, _clientId.c_str());

    create_field(_screen, "Topic", 376, &_topicInput);
    lv_textarea_set_text(_topicInput, _topic.c_str());

    _connectButton = lv_btn_create(_screen);
    lv_obj_set_size(_connectButton, 204, 52);
    lv_obj_set_pos(_connectButton, 18, 452);
    lv_obj_add_event_cb(_connectButton, onConnectClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *connectLabel = lv_label_create(_connectButton);
    lv_label_set_text(connectLabel, "Connect");
    lv_obj_center(connectLabel);

    _disconnectButton = lv_btn_create(_screen);
    lv_obj_set_size(_disconnectButton, 204, 52);
    lv_obj_set_pos(_disconnectButton, 258, 452);
    lv_obj_add_event_cb(_disconnectButton, onDisconnectClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *disconnectLabel = lv_label_create(_disconnectButton);
    lv_label_set_text(disconnectLabel, "Disconnect");
    lv_obj_center(disconnectLabel);

    lv_obj_t *hint = lv_label_create(_screen);
    lv_obj_set_width(hint, 444);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x0F766E), 0);
    lv_obj_set_pos(hint, 18, 520);
    lv_label_set_text(hint,
                      "This first version focuses on connection settings on the launch page. It can connect, subscribe to one topic, and report broker events.");

    lv_obj_add_event_cb(_screen, onScreenDeleted, LV_EVENT_DELETE, this);
    return true;
}

bool MqttApp::ensureUiReady()
{
    if (hasLiveScreen()) {
        return true;
    }
    resetUiPointers();
    return buildUi();
}

bool MqttApp::hasLiveScreen() const
{
    return (_screen != nullptr) && lv_obj_is_valid(_screen) && (_uriInput != nullptr) && lv_obj_is_valid(_uriInput);
}

bool MqttApp::connectClient()
{
    disconnectClient();

    _brokerUri = lv_textarea_get_text(_uriInput);
    _username = lv_textarea_get_text(_usernameInput);
    _password = lv_textarea_get_text(_passwordInput);
    _clientId = lv_textarea_get_text(_clientIdInput);
    _topic = lv_textarea_get_text(_topicInput);

    if (_brokerUri.empty()) {
        setStatus("Broker URI is required.");
        return false;
    }
    if (_clientId.empty()) {
        _clientId = "jc4880_remote";
        lv_textarea_set_text(_clientIdInput, _clientId.c_str());
    }

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = _brokerUri.c_str();
    mqtt_cfg.credentials.client_id = _clientId.c_str();
    if (!_username.empty()) {
        mqtt_cfg.credentials.username = _username.c_str();
    }
    if (!_password.empty()) {
        mqtt_cfg.credentials.authentication.password = _password.c_str();
    }

    _client = esp_mqtt_client_init(&mqtt_cfg);
    if (_client == nullptr) {
        setStatus("Failed to initialize MQTT client.");
        return false;
    }
    esp_mqtt_client_register_event(_client, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), onMqttEvent, this);
    const esp_err_t err = esp_mqtt_client_start(_client);
    if (err != ESP_OK) {
        setStatus(std::string("Failed to start MQTT client: ") + esp_err_to_name(err));
        esp_mqtt_client_destroy(_client);
        _client = nullptr;
        return false;
    }
    setStatus("Connecting to broker...");
    updateButtonState();
    return true;
}

void MqttApp::disconnectClient()
{
    if (_client == nullptr) {
        _connected = false;
        updateButtonState();
        return;
    }

    esp_mqtt_client_stop(_client);
    esp_mqtt_client_destroy(_client);
    _client = nullptr;
    _connected = false;
    updateButtonState();
}

void MqttApp::setStatus(const std::string &status)
{
    if ((_statusLabel != nullptr) && lv_obj_is_valid(_statusLabel)) {
        lv_label_set_text(_statusLabel, status.c_str());
    }
}

void MqttApp::updateButtonState()
{
    if ((_connectButton != nullptr) && (_disconnectButton != nullptr)) {
        if (_client == nullptr) {
            lv_obj_clear_state(_connectButton, LV_STATE_DISABLED);
            lv_obj_add_state(_disconnectButton, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(_connectButton, LV_STATE_DISABLED);
            lv_obj_clear_state(_disconnectButton, LV_STATE_DISABLED);
        }
    }
}

void MqttApp::resetUiPointers()
{
    _screen = nullptr;
    _statusLabel = nullptr;
    _uriInput = nullptr;
    _usernameInput = nullptr;
    _passwordInput = nullptr;
    _clientIdInput = nullptr;
    _topicInput = nullptr;
    _connectButton = nullptr;
    _disconnectButton = nullptr;
}

void MqttApp::onScreenDeleted(lv_event_t *event)
{
    auto *app = static_cast<MqttApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->resetUiPointers();
    }
}

void MqttApp::onConnectClicked(lv_event_t *event)
{
    auto *app = static_cast<MqttApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->connectClient();
    }
}

void MqttApp::onDisconnectClicked(lv_event_t *event)
{
    auto *app = static_cast<MqttApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->disconnectClient();
        app->setStatus("Disconnected.");
    }
}

void MqttApp::onMqttEvent(void *handler_args, esp_event_base_t, int32_t event_id, void *event_data)
{
    auto *app = static_cast<MqttApp *>(handler_args);
    if (app == nullptr) {
        return;
    }

    auto *async_status = new AsyncStatus();
    async_status->app = app;
    auto *event = static_cast<esp_mqtt_event_handle_t>(event_data);
    switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
    case MQTT_EVENT_CONNECTED:
        async_status->connected = true;
        async_status->text = "Connected to broker.";
        if (!app->_topic.empty()) {
            esp_mqtt_client_subscribe(app->_client, app->_topic.c_str(), 0);
            async_status->text += " Subscribed to ";
            async_status->text += app->_topic;
            async_status->text += ".";
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        async_status->connected = false;
        async_status->text = "Broker disconnected.";
        break;
    case MQTT_EVENT_DATA: {
        async_status->connected = true;
        std::string topic(event->topic, event->topic_len);
        std::string payload(event->data, event->data_len);
        if (payload.size() > 96) {
            payload.resize(96);
            payload += "...";
        }
        async_status->text = "Message on ";
        async_status->text += topic;
        async_status->text += ": ";
        async_status->text += payload;
        break;
    }
    case MQTT_EVENT_ERROR:
        async_status->connected = false;
        async_status->text = "MQTT transport error.";
        break;
    default:
        delete async_status;
        return;
    }

    lv_async_call(applyAsyncStatus, async_status);
}

void MqttApp::applyAsyncStatus(void *context)
{
    std::unique_ptr<AsyncStatus> async_status(static_cast<AsyncStatus *>(context));
    if ((async_status->app == nullptr) || !async_status->app->hasLiveScreen()) {
        return;
    }
    async_status->app->_connected = async_status->connected;
    async_status->app->setStatus(async_status->text);
    async_status->app->updateButtonState();
}