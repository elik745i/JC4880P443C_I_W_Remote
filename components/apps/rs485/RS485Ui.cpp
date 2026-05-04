#include "RS485Internal.hpp"

using namespace rs485app;

namespace {

lv_obj_t *make_card(lv_obj_t *parent, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x171B2B), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x232A40), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    return card;
}

lv_obj_t *make_action_button(lv_obj_t *parent, const char *text, lv_coord_t width)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, 46);
    lv_obj_set_style_radius(button, 16, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2D6CDF), 0);
    lv_obj_set_style_bg_grad_color(button, lv_color_hex(0x1749A8), 0);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_VER, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF7FAFF), 0);
    lv_obj_center(label);
    return button;
}

lv_obj_t *make_scroll_label_panel(lv_obj_t *parent, lv_coord_t width, lv_coord_t height, lv_obj_t **outLabel)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0F1320), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    lv_obj_t *label = lv_label_create(panel);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xDEE7FF), 0);
    lv_label_set_text(label, "");
    *outLabel = label;
    return panel;
}

} // namespace

bool RS485App::buildUi()
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);

    _root = lv_obj_create(screen);
    lv_obj_set_pos(_root, 0, 0);
    lv_obj_set_size(_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(_root, 0, 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_bg_color(_root, lv_color_hex(0x0D1020), 0);
    lv_obj_set_style_bg_grad_color(_root, lv_color_hex(0x1C2036), 0);
    lv_obj_set_style_bg_grad_dir(_root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_pad_all(_root, 0, 0);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    const lv_area_t area = getVisualArea();
    const lv_coord_t width = area.x2 - area.x1 + 1;
    const lv_coord_t height = area.y2 - area.y1 + 1;

    lv_obj_t *content = lv_obj_create(_root);
    lv_obj_set_pos(content, area.x1, area.y1);
    lv_obj_set_size(content, width, height);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = make_card(content, width - 24, kHeaderHeight);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 4);

    _titleLabel = lv_label_create(header);
    lv_obj_set_style_text_font(_titleLabel, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(_titleLabel, lv_color_hex(0xF8FAFF), 0);
    lv_label_set_text(_titleLabel, "RS-485 Service Panel");
    lv_obj_align(_titleLabel, LV_ALIGN_TOP_LEFT, 0, 0);

    _statusLabel = lv_label_create(header);
    lv_obj_set_width(_statusLabel, width - 90);
    lv_obj_set_style_text_font(_statusLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_statusLabel, lv_color_hex(0x8FD3FF), 0);
    lv_label_set_long_mode(_statusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_align(_statusLabel, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *navPanel = make_card(content, width - 24, 92);
    lv_obj_align_to(navPanel, header, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    lv_obj_set_flex_flow(navPanel, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(navPanel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(navPanel, 8, 0);
    lv_obj_set_style_pad_column(navPanel, 8, 0);

    const char *navLabels[RS485_SCREEN_COUNT] = {"Home", "Scan", "Terminal", "Modbus", "Devices", "Dash", "Logs", "Settings"};
    const int navActions[RS485_SCREEN_COUNT] = {
        ACTION_SHOW_HOME,
        ACTION_SHOW_SCAN,
        ACTION_SHOW_TERMINAL,
        ACTION_SHOW_MODBUS,
        ACTION_SHOW_DEVICES,
        ACTION_SHOW_DASHBOARD,
        ACTION_SHOW_LOGS,
        ACTION_SHOW_SETTINGS,
    };

    auto register_action = [this](lv_obj_t *button, int id) {
        if (_actionBindingCount < _actionBindings.size()) {
            _actionBindings[_actionBindingCount++] = {button, id};
        }
        lv_obj_add_event_cb(button, onActionButtonEvent, LV_EVENT_CLICKED, this);
    };

    for (int index = 0; index < RS485_SCREEN_COUNT; ++index) {
        _navButtons[index] = make_action_button(navPanel, navLabels[index], 106);
        register_action(_navButtons[index], navActions[index]);
    }

    const lv_coord_t panelTop = kHeaderHeight + 110;
    const lv_coord_t panelHeight = height - panelTop - 8;
    for (size_t index = 0; index < _screenPanels.size(); ++index) {
        _screenPanels[index] = make_card(content, width - 24, panelHeight);
        lv_obj_align(_screenPanels[index], LV_ALIGN_TOP_MID, 0, panelTop);
        lv_obj_add_flag(_screenPanels[index], LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *homePanel = _screenPanels[RS485_SCREEN_HOME];
    _homeSummaryLabel = lv_label_create(homePanel);
    lv_obj_set_width(_homeSummaryLabel, lv_pct(100));
    lv_label_set_long_mode(_homeSummaryLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_homeSummaryLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_homeSummaryLabel, lv_color_hex(0xEAF1FF), 0);
    lv_obj_align(_homeSummaryLabel, LV_ALIGN_TOP_LEFT, 0, 0);
    for (int index = 1; index <= 6; ++index) {
        lv_obj_t *button = make_action_button(homePanel, navLabels[index], 142);
        const int column = (index - 1) % 3;
        const int row = (index - 1) / 3;
        lv_obj_align(button, LV_ALIGN_TOP_LEFT, column * 154, 110 + row * 58);
        register_action(button, navActions[index]);
    }

    lv_obj_t *scanPanel = _screenPanels[RS485_SCREEN_SCAN];
    lv_obj_t *scanStart = make_action_button(scanPanel, "Start Scan", 140);
    lv_obj_align(scanStart, LV_ALIGN_TOP_LEFT, 0, 0);
    register_action(scanStart, ACTION_SCAN_START);
    lv_obj_t *scanStop = make_action_button(scanPanel, "Stop", 100);
    lv_obj_align(scanStop, LV_ALIGN_TOP_LEFT, 150, 0);
    register_action(scanStop, ACTION_SCAN_STOP);
    _scanProgressBar = lv_bar_create(scanPanel);
    lv_obj_set_size(_scanProgressBar, width - 90, 22);
    lv_obj_align(_scanProgressBar, LV_ALIGN_TOP_LEFT, 0, 58);
    lv_bar_set_range(_scanProgressBar, 0, 100);
    make_scroll_label_panel(scanPanel, width - 90, 222, &_scanResultsLabel);
    lv_obj_align(lv_obj_get_parent(_scanResultsLabel), LV_ALIGN_TOP_LEFT, 0, 94);
    make_scroll_label_panel(scanPanel, width - 90, 140, &_scanDetailLabel);
    lv_obj_align(lv_obj_get_parent(_scanDetailLabel), LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *terminalPanel = _screenPanels[RS485_SCREEN_TERMINAL];
    _terminalInput = lv_textarea_create(terminalPanel);
    lv_obj_set_size(_terminalInput, width - 90, 86);
    lv_obj_align(_terminalInput, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_textarea_set_placeholder_text(_terminalInput, "ASCII text or spaced HEX bytes");
    lv_obj_t *sendAscii = make_action_button(terminalPanel, "Send ASCII", 138);
    lv_obj_align(sendAscii, LV_ALIGN_TOP_LEFT, 0, 96);
    register_action(sendAscii, ACTION_TERMINAL_SEND_ASCII);
    lv_obj_t *sendHex = make_action_button(terminalPanel, "Send HEX", 138);
    lv_obj_align(sendHex, LV_ALIGN_TOP_LEFT, 148, 96);
    register_action(sendHex, ACTION_TERMINAL_SEND_HEX);
    lv_obj_t *clearTerminal = make_action_button(terminalPanel, "Clear", 100);
    lv_obj_align(clearTerminal, LV_ALIGN_TOP_LEFT, 296, 96);
    register_action(clearTerminal, ACTION_TERMINAL_CLEAR);
    lv_obj_t *exportTerminal = make_action_button(terminalPanel, "Export", 110);
    lv_obj_align(exportTerminal, LV_ALIGN_TOP_LEFT, 406, 96);
    register_action(exportTerminal, ACTION_TERMINAL_EXPORT);
    lv_obj_t *templateButton = make_action_button(terminalPanel, "Use Template", 148);
    lv_obj_align(templateButton, LV_ALIGN_TOP_LEFT, 526, 96);
    register_action(templateButton, ACTION_TERMINAL_TEMPLATE);
    _terminalTemplatesLabel = lv_label_create(terminalPanel);
    lv_obj_set_width(_terminalTemplatesLabel, width - 90);
    lv_label_set_long_mode(_terminalTemplatesLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_terminalTemplatesLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_terminalTemplatesLabel, lv_color_hex(0x9CC2FF), 0);
    lv_obj_align(_terminalTemplatesLabel, LV_ALIGN_TOP_LEFT, 0, 154);
    make_scroll_label_panel(terminalPanel, width - 90, panelHeight - 210, &_terminalLogLabel);
    lv_obj_align(lv_obj_get_parent(_terminalLogLabel), LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *modbusPanel = _screenPanels[RS485_SCREEN_MODBUS];
    lv_obj_t *readHolding = make_action_button(modbusPanel, "Read HR", 128);
    lv_obj_align(readHolding, LV_ALIGN_TOP_LEFT, 0, 0);
    register_action(readHolding, ACTION_MODBUS_READ_HOLDING);
    lv_obj_t *readInput = make_action_button(modbusPanel, "Read IR", 128);
    lv_obj_align(readInput, LV_ALIGN_TOP_LEFT, 138, 0);
    register_action(readInput, ACTION_MODBUS_READ_INPUT);
    lv_obj_t *readCoils = make_action_button(modbusPanel, "Read Coils", 138);
    lv_obj_align(readCoils, LV_ALIGN_TOP_LEFT, 276, 0);
    register_action(readCoils, ACTION_MODBUS_READ_COILS);
    lv_obj_t *writeSingle = make_action_button(modbusPanel, "Write +1", 128);
    lv_obj_align(writeSingle, LV_ALIGN_TOP_LEFT, 424, 0);
    register_action(writeSingle, ACTION_MODBUS_WRITE_SINGLE);
    make_scroll_label_panel(modbusPanel, width - 90, panelHeight - 66, &_browserResultLabel);
    lv_obj_align(lv_obj_get_parent(_browserResultLabel), LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *devicesPanel = _screenPanels[RS485_SCREEN_DEVICES];
    lv_obj_t *saveProfile = make_action_button(devicesPanel, "Save Device", 150);
    lv_obj_align(saveProfile, LV_ALIGN_TOP_LEFT, 0, 0);
    register_action(saveProfile, ACTION_PROFILE_SAVE_SELECTED);
    lv_obj_t *loadDefaults = make_action_button(devicesPanel, "Reset Defaults", 150);
    lv_obj_align(loadDefaults, LV_ALIGN_TOP_LEFT, 160, 0);
    register_action(loadDefaults, ACTION_PROFILE_LOAD_DEFAULT);
    make_scroll_label_panel(devicesPanel, width - 90, panelHeight - 66, &_profilesLabel);
    lv_obj_align(lv_obj_get_parent(_profilesLabel), LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *dashboardPanel = _screenPanels[RS485_SCREEN_DASHBOARD];
    lv_obj_t *dashRefresh = make_action_button(dashboardPanel, "Refresh", 120);
    lv_obj_align(dashRefresh, LV_ALIGN_TOP_LEFT, 0, 0);
    register_action(dashRefresh, ACTION_DASHBOARD_REFRESH);
    _dashboardPrimaryLabel = lv_label_create(dashboardPanel);
    lv_obj_set_width(_dashboardPrimaryLabel, width - 90);
    lv_obj_set_style_text_font(_dashboardPrimaryLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_dashboardPrimaryLabel, lv_color_hex(0xF4FBFF), 0);
    lv_obj_align(_dashboardPrimaryLabel, LV_ALIGN_TOP_LEFT, 0, 64);
    _dashboardSecondaryLabel = lv_label_create(dashboardPanel);
    lv_obj_set_width(_dashboardSecondaryLabel, width - 90);
    lv_label_set_long_mode(_dashboardSecondaryLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_dashboardSecondaryLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_dashboardSecondaryLabel, lv_color_hex(0x97C5FF), 0);
    lv_obj_align(_dashboardSecondaryLabel, LV_ALIGN_TOP_LEFT, 0, 106);
    _dashboardChart = lv_chart_create(dashboardPanel);
    lv_obj_set_size(_dashboardChart, width - 90, 190);
    lv_obj_align(_dashboardChart, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_chart_set_type(_dashboardChart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(_dashboardChart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    lv_chart_set_point_count(_dashboardChart, _dashboardTrend.size());
    _dashboardSeries = lv_chart_add_series(_dashboardChart, lv_color_hex(0x54D1FF), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *logsPanel = _screenPanels[RS485_SCREEN_LOGS];
    lv_obj_t *logsClear = make_action_button(logsPanel, "Clear Logs", 138);
    lv_obj_align(logsClear, LV_ALIGN_TOP_LEFT, 0, 0);
    register_action(logsClear, ACTION_LOGS_CLEAR);
    lv_obj_t *logsExport = make_action_button(logsPanel, "Export Logs", 138);
    lv_obj_align(logsExport, LV_ALIGN_TOP_LEFT, 148, 0);
    register_action(logsExport, ACTION_LOGS_EXPORT);
    make_scroll_label_panel(logsPanel, width - 90, panelHeight - 66, &_logsLabel);
    lv_obj_align(lv_obj_get_parent(_logsLabel), LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *settingsPanel = _screenPanels[RS485_SCREEN_SETTINGS];
    lv_obj_t *baudButton = make_action_button(settingsPanel, "Baud", 120);
    lv_obj_align(baudButton, LV_ALIGN_TOP_LEFT, 0, 0);
    register_action(baudButton, ACTION_SETTINGS_BAUD);
    lv_obj_t *parityButton = make_action_button(settingsPanel, "Parity", 120);
    lv_obj_align(parityButton, LV_ALIGN_TOP_LEFT, 130, 0);
    register_action(parityButton, ACTION_SETTINGS_PARITY);
    lv_obj_t *stopButton = make_action_button(settingsPanel, "Stop Bits", 130);
    lv_obj_align(stopButton, LV_ALIGN_TOP_LEFT, 260, 0);
    register_action(stopButton, ACTION_SETTINGS_STOP_BITS);
    lv_obj_t *rangeButton = make_action_button(settingsPanel, "Scan Range", 136);
    lv_obj_align(rangeButton, LV_ALIGN_TOP_LEFT, 400, 0);
    register_action(rangeButton, ACTION_SETTINGS_RANGE);
    lv_obj_t *persistButton = make_action_button(settingsPanel, "Persist Logs", 140);
    lv_obj_align(persistButton, LV_ALIGN_TOP_LEFT, 546, 0);
    register_action(persistButton, ACTION_SETTINGS_PERSIST);
    lv_obj_t *retryButton = make_action_button(settingsPanel, "Retries", 120);
    lv_obj_align(retryButton, LV_ALIGN_TOP_LEFT, 0, 56);
    register_action(retryButton, ACTION_SETTINGS_RETRIES);
    make_scroll_label_panel(settingsPanel, width - 90, panelHeight - 120, &_settingsLabel);
    lv_obj_align(lv_obj_get_parent(_settingsLabel), LV_ALIGN_BOTTOM_LEFT, 0, 0);

    switchScreen(_activeScreen);
    return true;
}

void RS485App::switchScreen(int screenId)
{
    if ((screenId < 0) || (screenId >= RS485_SCREEN_COUNT)) {
        return;
    }

    _activeScreen = static_cast<uint8_t>(screenId);
    s_settings.selected_screen = _activeScreen;
    for (size_t index = 0; index < _screenPanels.size(); ++index) {
        if (_screenPanels[index] == nullptr) {
            continue;
        }
        if ((int)index == screenId) {
            lv_obj_clear_flag(_screenPanels[index], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_screenPanels[index], LV_OBJ_FLAG_HIDDEN);
        }

        if (_navButtons[index] != nullptr) {
            lv_obj_set_style_bg_color(_navButtons[index], (int)index == screenId ? lv_color_hex(0x2FBF71) : lv_color_hex(0x2D6CDF), 0);
            lv_obj_set_style_bg_grad_color(_navButtons[index], (int)index == screenId ? lv_color_hex(0x1B8050) : lv_color_hex(0x1749A8), 0);
        }
    }
    rs485_app_state_save_settings(&s_settings);
}

void RS485App::refreshStatusText()
{
    if (_statusLabel != nullptr) {
        lv_label_set_text(_statusLabel, _statusMessage.c_str());
    }
}

void RS485App::refreshHomeView()
{
    if (_homeSummaryLabel == nullptr) {
        return;
    }

    std::ostringstream stream;
    stream << "Professional RS-485 / Modbus RTU field terminal\n\n"
           << "Transport: " << (s_transport.installed ? "Ready" : "Idle") << "\n"
           << "Serial: " << s_settings.serial.baud_rate << " baud, "
           << rs485_app_state_parity_to_string(s_settings.serial.parity) << ", "
           << static_cast<unsigned>(s_settings.serial.stop_bits) << " stop\n"
           << "Scan: " << (_scanRunning ? "running" : "idle") << "\n"
           << "Discovered devices: " << _discoveredDevices.size() << "\n"
           << "Profiles: " << rs485_profile_store_profile_count(&s_profileStore) << "\n"
           << "Logs buffered: " << rs485_log_store_count(&s_logStore) << "\n\n"
           << "Pins: TX=" << s_settings.serial.tx_pin << " RX=" << s_settings.serial.rx_pin
           << " EN=" << s_settings.serial.en_pin << "\n"
           << "If pins are unset, the app stays safe and idle until you configure them.\n";
    lv_label_set_text(_homeSummaryLabel, stream.str().c_str());
}

void RS485App::refreshScanView()
{
    if ((_scanProgressBar == nullptr) || (_scanResultsLabel == nullptr) || (_scanDetailLabel == nullptr)) {
        return;
    }

    const int percent = (_scanProgressTotal == 0) ? 0 : (100 * _scanProgressCurrent) / _scanProgressTotal;
    lv_bar_set_value(_scanProgressBar, percent, LV_ANIM_OFF);

    std::ostringstream results;
    if (_discoveredDevices.empty()) {
        results << "No responding devices yet.";
    } else {
        for (size_t index = 0; index < _discoveredDevices.size(); ++index) {
            const auto &device = _discoveredDevices[index];
            results << index << ". Addr " << static_cast<unsigned>(device.slave_address)
                    << " | " << device.summary
                    << " | functions=" << format_function_mask(device.supported_functions_mask) << "\n";
        }
    }
    lv_label_set_text(_scanResultsLabel, results.str().c_str());

    std::ostringstream detail;
    detail << "Range " << s_settings.scan_start_address << ".." << s_settings.scan_end_address
           << "\nProbe mask: " << format_function_mask(s_settings.scan_probe_mask)
           << "\nRetries: " << static_cast<unsigned>(s_settings.serial.retry_count)
           << "\nStatus: " << _scanSummaryText;
    lv_label_set_text(_scanDetailLabel, detail.str().c_str());
}

void RS485App::refreshTerminalView()
{
    if ((_terminalTemplatesLabel == nullptr) || (_terminalLogLabel == nullptr)) {
        return;
    }

    std::ostringstream templ;
    templ << "Mode: " << rs485_app_state_terminal_format_to_string(s_settings.terminal_format)
          << " | CR=" << (s_settings.terminal_append_cr ? "on" : "off")
          << " | LF=" << (s_settings.terminal_append_lf ? "on" : "off")
          << " | Auto-repeat=" << s_settings.terminal_auto_repeat_ms << " ms\nTemplates:";
    rs485_terminal_template_t item = {};
    for (size_t index = 0; index < rs485_profile_store_template_count(&s_profileStore); ++index) {
        if (rs485_profile_store_get_template(&s_profileStore, index, &item)) {
            templ << "\n- " << item.name;
        }
    }
    lv_label_set_text(_terminalTemplatesLabel, templ.str().c_str());

    std::ostringstream logs;
    rs485_log_entry_t entry = {};
    const size_t total = rs485_log_store_count(&s_logStore);
    const size_t start = total > 18 ? total - 18 : 0;
    for (size_t index = start; index < total; ++index) {
        if (!rs485_log_store_get_entry(&s_logStore, index, &entry)) {
            continue;
        }
        logs << '[' << entry.timestamp_ms << "] "
             << (entry.direction == RS485_LOG_DIRECTION_TX ? "TX" : entry.direction == RS485_LOG_DIRECTION_RX ? "RX" : "EV")
             << ' ' << entry.summary;
        if (entry.payload[0] != '\0') {
            logs << "\n  " << entry.payload;
        }
        logs << "\n";
    }
    lv_label_set_text(_terminalLogLabel, logs.str().c_str());
}

void RS485App::refreshBrowserView()
{
    if (_browserResultLabel == nullptr) {
        return;
    }
    std::ostringstream stream;
    stream << _browserSummaryText << "\n\n"
           << "Manual slave: " << static_cast<unsigned>(s_settings.manual_slave_address) << "\n"
           << "Start address: " << s_settings.manual_start_address << "\n"
           << "Quantity: " << s_settings.manual_quantity << "\n"
           << "Retries: " << static_cast<unsigned>(s_settings.serial.retry_count) << "\n"
           << "Timeout: " << s_settings.serial.request_timeout_ms << " ms\n"
           << "Write +1 increments the last primary value and writes it to the selected register.";
    lv_label_set_text(_browserResultLabel, stream.str().c_str());
}

void RS485App::refreshProfilesView()
{
    if (_profilesLabel == nullptr) {
        return;
    }
    std::ostringstream stream;
    rs485_device_profile_t profile = {};
    if (rs485_profile_store_profile_count(&s_profileStore) == 0) {
        stream << "No saved device profiles yet. Save one from Scan or Modbus Browser.";
    } else {
        for (size_t index = 0; index < rs485_profile_store_profile_count(&s_profileStore); ++index) {
            if (rs485_profile_store_get_profile(&s_profileStore, index, &profile)) {
                stream << index << ". " << profile.name
                       << " | slave=" << static_cast<unsigned>(profile.slave_address)
                       << " | poll=" << profile.polling_interval_ms << " ms"
                       << " | fav=" << static_cast<unsigned>(profile.favorite_count) << "\n"
                       << "    " << profile.notes << "\n";
            }
        }
    }
    lv_label_set_text(_profilesLabel, stream.str().c_str());
}

void RS485App::refreshDashboardView()
{
    if ((_dashboardPrimaryLabel == nullptr) || (_dashboardSecondaryLabel == nullptr) || (_dashboardChart == nullptr) || (_dashboardSeries == nullptr)) {
        return;
    }

    std::ostringstream primary;
    primary << (_lastPollHealthy ? "Live value: " : "Last value unavailable") << _lastPrimaryValue;
    lv_label_set_text(_dashboardPrimaryLabel, primary.str().c_str());

    std::ostringstream secondary;
    secondary << "Slave " << static_cast<unsigned>(_lastPolledSlaveAddress)
              << " | register " << _lastStartAddress
              << " | last seen " << _lastPollTimestampMs << " ms"
              << " | health=" << (_lastPollHealthy ? "OK" : "stale")
              << "\nDashboard mode is designed as a lightweight operator/HMI surface fed by the Modbus browser and poller.";
    lv_label_set_text(_dashboardSecondaryLabel, secondary.str().c_str());

    for (size_t index = 0; index < _dashboardTrend.size(); ++index) {
        _dashboardSeries->y_points[index] = _dashboardTrend[index];
    }
    lv_chart_refresh(_dashboardChart);
}

void RS485App::refreshLogsView()
{
    if (_logsLabel == nullptr) {
        return;
    }
    std::ostringstream stream;
    rs485_log_entry_t entry = {};
    const size_t total = rs485_log_store_count(&s_logStore);
    for (size_t index = 0; index < total; ++index) {
        if (rs485_log_store_get_entry(&s_logStore, index, &entry)) {
            stream << '[' << entry.timestamp_ms << "] kind=" << static_cast<unsigned>(entry.kind)
                   << " dir=" << static_cast<unsigned>(entry.direction)
                   << " slave=" << static_cast<unsigned>(entry.slave_address)
                   << " fc=" << static_cast<unsigned>(entry.function_code)
                   << " : " << entry.summary;
            if (entry.payload[0] != '\0') {
                stream << "\n  payload=" << entry.payload;
            }
            stream << "\n";
        }
    }
    if (total == 0) {
        stream << "Log buffer empty.";
    }
    lv_label_set_text(_logsLabel, stream.str().c_str());
}

void RS485App::refreshSettingsView()
{
    if (_settingsLabel == nullptr) {
        return;
    }

    std::ostringstream stream;
    stream << "UART port: " << static_cast<int>(s_settings.serial.uart_port) << "\n"
           << "Baud: " << s_settings.serial.baud_rate << "\n"
           << "Parity: " << rs485_app_state_parity_to_string(s_settings.serial.parity) << "\n"
           << "Stop bits: " << static_cast<unsigned>(s_settings.serial.stop_bits) << "\n"
           << "Inter-frame timeout: " << s_settings.serial.inter_frame_timeout_ms << " ms\n"
           << "Request timeout: " << s_settings.serial.request_timeout_ms << " ms\n"
           << "Retries: " << static_cast<unsigned>(s_settings.serial.retry_count) << "\n"
           << "Scan range: " << s_settings.scan_start_address << ".." << s_settings.scan_end_address << "\n"
           << "Persist logs: " << (s_settings.persist_logs_to_file ? "enabled" : "disabled") << "\n"
           << "TX pin: " << s_settings.serial.tx_pin << "\n"
           << "RX pin: " << s_settings.serial.rx_pin << "\n"
           << "EN pin: " << s_settings.serial.en_pin << "\n\n"
           << kTodoPinHint << "\n"
           << "This first version keeps transport/protocol layers reusable and safe on a shared half-duplex bus.";
    lv_label_set_text(_settingsLabel, stream.str().c_str());
}

void RS485App::tickUi()
{
    refreshStatusText();
    refreshHomeView();
    refreshScanView();
    refreshTerminalView();
    refreshBrowserView();
    refreshProfilesView();
    refreshDashboardView();
    if (rs485_log_store_count(&s_logStore) != _lastRenderedLogCount) {
        refreshLogsView();
        _lastRenderedLogCount = static_cast<uint8_t>(std::min<size_t>(255, rs485_log_store_count(&s_logStore)));
    }
    refreshSettingsView();
}

void RS485App::onNavButtonEvent(lv_event_t *event)
{
    onActionButtonEvent(event);
}

void RS485App::onActionButtonEvent(lv_event_t *event)
{
    RS485App *app = static_cast<RS485App *>(lv_event_get_user_data(event));
    lv_obj_t *target = lv_event_get_current_target(event);
    if ((app == nullptr) || (target == nullptr)) {
        return;
    }

    int actionId = 0;
    for (size_t index = 0; index < app->_actionBindingCount; ++index) {
        if (app->_actionBindings[index].button == target) {
            actionId = app->_actionBindings[index].id;
            break;
        }
    }
    if (actionId == 0) {
        return;
    }

    switch (actionId) {
    case ACTION_SHOW_HOME:
        app->switchScreen(RS485_SCREEN_HOME);
        break;
    case ACTION_SHOW_SCAN:
        app->switchScreen(RS485_SCREEN_SCAN);
        break;
    case ACTION_SHOW_TERMINAL:
        app->switchScreen(RS485_SCREEN_TERMINAL);
        break;
    case ACTION_SHOW_MODBUS:
        app->switchScreen(RS485_SCREEN_MODBUS);
        break;
    case ACTION_SHOW_DEVICES:
        app->switchScreen(RS485_SCREEN_DEVICES);
        break;
    case ACTION_SHOW_DASHBOARD:
        app->switchScreen(RS485_SCREEN_DASHBOARD);
        break;
    case ACTION_SHOW_LOGS:
        app->switchScreen(RS485_SCREEN_LOGS);
        break;
    case ACTION_SHOW_SETTINGS:
        app->switchScreen(RS485_SCREEN_SETTINGS);
        break;
    case ACTION_SCAN_START:
        app->startScan();
        break;
    case ACTION_SCAN_STOP:
        app->stopScan();
        app->setStatusMessage("Stopping scan...");
        break;
    case ACTION_TERMINAL_SEND_ASCII:
        app->sendTerminalPayload(false);
        break;
    case ACTION_TERMINAL_SEND_HEX:
        app->sendTerminalPayload(true);
        break;
    case ACTION_TERMINAL_CLEAR:
        rs485_log_store_clear(&s_logStore);
        if (app->_terminalInput != nullptr) {
            lv_textarea_set_text(app->_terminalInput, "");
        }
        app->setStatusMessage("Terminal log cleared");
        break;
    case ACTION_TERMINAL_EXPORT:
        app->exportLogsToFile();
        break;
    case ACTION_TERMINAL_TEMPLATE: {
        rs485_terminal_template_t templ = {};
        if (rs485_profile_store_get_template(&s_profileStore, 0, &templ) && (app->_terminalInput != nullptr)) {
            if (templ.format == RS485_TERMINAL_FORMAT_ASCII) {
                lv_textarea_set_text(app->_terminalInput, reinterpret_cast<const char *>(templ.payload));
            } else {
                lv_textarea_set_text(app->_terminalInput, hex_encode(templ.payload, templ.payload_len).c_str());
            }
            app->setStatusMessage(std::string("Loaded template ") + templ.name);
        }
        break;
    }
    case ACTION_MODBUS_READ_HOLDING:
        app->performManualRead(MODBUS_FC_READ_HOLDING_REGISTERS);
        break;
    case ACTION_MODBUS_READ_INPUT:
        app->performManualRead(MODBUS_FC_READ_INPUT_REGISTERS);
        break;
    case ACTION_MODBUS_READ_COILS:
        app->performManualRead(MODBUS_FC_READ_COILS);
        break;
    case ACTION_MODBUS_WRITE_SINGLE:
        app->performManualRead(MODBUS_FC_WRITE_SINGLE_REGISTER);
        break;
    case ACTION_PROFILE_SAVE_SELECTED:
        app->saveSelectedDeviceProfile();
        break;
    case ACTION_PROFILE_LOAD_DEFAULT:
        rs485_app_state_set_defaults(&s_settings);
        app->setStatusMessage("Defaults restored");
        break;
    case ACTION_DASHBOARD_REFRESH:
        app->performManualRead(MODBUS_FC_READ_HOLDING_REGISTERS);
        app->switchScreen(RS485_SCREEN_DASHBOARD);
        break;
    case ACTION_LOGS_CLEAR:
        rs485_log_store_clear(&s_logStore);
        app->setStatusMessage("Logs cleared");
        break;
    case ACTION_LOGS_EXPORT:
        app->exportLogsToFile();
        break;
    case ACTION_SETTINGS_BAUD: {
        static const uint32_t baudValues[] = {9600, 19200, 38400, 57600, 115200};
        size_t next = 0;
        for (size_t index = 0; index < sizeof(baudValues) / sizeof(baudValues[0]); ++index) {
            if (baudValues[index] == s_settings.serial.baud_rate) {
                next = (index + 1) % (sizeof(baudValues) / sizeof(baudValues[0]));
                break;
            }
        }
        s_settings.serial.baud_rate = baudValues[next];
        rs485_app_state_save_settings(&s_settings);
        app->setStatusMessage("Baud updated");
        rs485_transport_deinit(&s_transport);
        break;
    }
    case ACTION_SETTINGS_PARITY:
        s_settings.serial.parity = static_cast<uint8_t>((s_settings.serial.parity + 1U) % 3U);
        rs485_app_state_save_settings(&s_settings);
        app->setStatusMessage("Parity updated");
        rs485_transport_deinit(&s_transport);
        break;
    case ACTION_SETTINGS_STOP_BITS:
        s_settings.serial.stop_bits = s_settings.serial.stop_bits == 1 ? 2 : 1;
        rs485_app_state_save_settings(&s_settings);
        app->setStatusMessage("Stop bits updated");
        rs485_transport_deinit(&s_transport);
        break;
    case ACTION_SETTINGS_RANGE:
        if (s_settings.scan_end_address < 32) {
            s_settings.scan_end_address = 32;
        } else if (s_settings.scan_end_address < 64) {
            s_settings.scan_end_address = 64;
        } else {
            s_settings.scan_end_address = 247;
        }
        rs485_app_state_save_settings(&s_settings);
        app->setStatusMessage("Scan range updated");
        break;
    case ACTION_SETTINGS_PERSIST:
        s_settings.persist_logs_to_file = !s_settings.persist_logs_to_file;
        rs485_app_state_save_settings(&s_settings);
        app->setStatusMessage("Log persistence toggled");
        break;
    case ACTION_SETTINGS_RETRIES:
        s_settings.serial.retry_count = static_cast<uint8_t>((s_settings.serial.retry_count + 1U) % 4U);
        rs485_app_state_save_settings(&s_settings);
        modbus_rtu_master_init(&s_modbusMaster, &s_transport, s_settings.serial.request_timeout_ms, s_settings.serial.retry_count);
        app->setStatusMessage("Retry count updated");
        break;
    default:
        break;
    }

    app->tickUi();
}