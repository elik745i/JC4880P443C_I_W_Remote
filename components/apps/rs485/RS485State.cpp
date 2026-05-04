#include "RS485Internal.hpp"

using namespace rs485app;

namespace {

void on_scan_progress(void *user_ctx, uint8_t current, uint8_t total)
{
    RS485App *app = static_cast<RS485App *>(user_ctx);
    if (app != nullptr) {
        app->handleScanProgress(current, total);
    }
}

void on_scan_device(void *user_ctx, const rs485_discovered_device_t *device)
{
    RS485App *app = static_cast<RS485App *>(user_ctx);
    if ((app != nullptr) && (device != nullptr)) {
        app->handleScanDevice(device);
    }
}

void on_scan_log(void *user_ctx, const char *message)
{
    RS485App *app = static_cast<RS485App *>(user_ctx);
    if ((app != nullptr) && (message != nullptr)) {
        app->handleScanLog(message);
    }
}

std::string make_export_path(void)
{
    return kLogExportPath;
}

} // namespace

void RS485App::handleScanProgress(uint8_t current, uint8_t total)
{
    if ((_stateMutex == nullptr) || (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) != pdTRUE)) {
        return;
    }
    _scanProgressCurrent = current;
    _scanProgressTotal = total;
    xSemaphoreGive(_stateMutex);
}

void RS485App::handleScanDevice(const rs485_discovered_device_t *device)
{
    if ((device == nullptr) || (_stateMutex == nullptr)) {
        return;
    }
    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _discoveredDevices.push_back(*device);
        _scanSummaryText = std::string("Found ") + std::to_string(_discoveredDevices.size()) + " responding slave(s)";
        xSemaphoreGive(_stateMutex);
    }
    appendLogStatus(device->summary, format_function_mask(device->supported_functions_mask).c_str());
}

void RS485App::handleScanLog(const char *message)
{
    if (message != nullptr) {
        appendLogStatus(message);
    }
}

bool RS485App::ensureTransportReady()
{
    if (s_transport.installed) {
        modbus_rtu_master_init(&s_modbusMaster, &s_transport, s_settings.serial.request_timeout_ms, s_settings.serial.retry_count);
        return true;
    }

    if ((s_settings.serial.tx_pin < 0) || (s_settings.serial.rx_pin < 0) || (s_settings.serial.en_pin < 0)) {
        setStatusMessage(kTodoPinHint);
        appendLogStatus("Transport not started", kTodoPinHint);
        return false;
    }

    const rs485_transport_config_t transport_config = make_transport_config(s_settings);
    if (!rs485_transport_init(&s_transport, &transport_config)) {
        setStatusMessage("Failed to initialize RS-485 transport");
        appendLogStatus("Transport init failed", rs485_transport_status_to_string(RS485_TRANSPORT_INVALID_STATE));
        return false;
    }

    modbus_rtu_master_init(&s_modbusMaster, &s_transport, s_settings.serial.request_timeout_ms, s_settings.serial.retry_count);
    setStatusMessage("RS-485 transport ready");
    appendLogStatus("Transport initialized");
    return true;
}

void RS485App::appendLogStatus(const char *summary, const char *payload)
{
    rs485_log_store_append(&s_logStore,
                           RS485_LOG_DIRECTION_EVENT,
                           RS485_LOG_KIND_STATUS,
                           0,
                           0,
                           0,
                           summary == nullptr ? "status" : summary,
                           payload == nullptr ? "" : payload);
}

void RS485App::setStatusMessage(const std::string &message)
{
    if ((_stateMutex != nullptr) && (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE)) {
        _statusMessage = message;
        xSemaphoreGive(_stateMutex);
    }
}

bool RS485App::startScan()
{
    if (_scanRunning || (_scanTaskHandle != nullptr)) {
        return false;
    }
    if (!ensureTransportReady()) {
        return false;
    }

    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        _discoveredDevices.clear();
        _scanProgressCurrent = 0;
        _scanProgressTotal = 0;
        _scanCancelRequested = false;
        _scanRunning = true;
        _scanSummaryText = "Scanning bus...";
        xSemaphoreGive(_stateMutex);
    }

    if (create_task_prefer_psram(scanTaskEntry,
                                 "rs485_scan",
                                 kScanTaskStack,
                                 this,
                                 kScanTaskPriority,
                                 &_scanTaskHandle,
                                 1) != pdPASS) {
        _scanRunning = false;
        _scanTaskHandle = nullptr;
        setStatusMessage("Failed to start scan task");
        return false;
    }

    switchScreen(RS485_SCREEN_SCAN);
    setStatusMessage("Bus scan started");
    appendLogStatus("Bus scan started");
    return true;
}

void RS485App::stopScan()
{
    if (_stateMutex != nullptr) {
        if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            _scanCancelRequested = true;
            xSemaphoreGive(_stateMutex);
        }
    }
}

void RS485App::scanTask()
{
    rs485_scan_config_t scan_config = {
        .start_address = static_cast<uint8_t>(std::clamp<int>(s_settings.scan_start_address, 1, 247)),
        .end_address = static_cast<uint8_t>(std::clamp<int>(s_settings.scan_end_address, 1, 247)),
        .probe_mask = s_settings.scan_probe_mask,
        .retries = s_settings.serial.retry_count,
        .probe_spacing_ms = 30,
    };

    rs485_scan_callbacks_t callbacks = {
        .cancel_flag = &_scanCancelRequested,
        .user_ctx = this,
        .on_progress = on_scan_progress,
        .on_device = on_scan_device,
        .on_log = on_scan_log,
    };

    const bool finished = rs485_device_scan_run(&s_modbusMaster, &scan_config, &callbacks);
    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _scanRunning = false;
        _scanTaskHandle = nullptr;
        if (_scanCancelRequested) {
            _scanSummaryText = "Scan canceled";
        } else if (finished) {
            _scanSummaryText = std::string("Scan done: ") + std::to_string(_discoveredDevices.size()) + " device(s)";
        } else {
            _scanSummaryText = "Scan stopped with errors";
        }
        _scanCancelRequested = false;
        xSemaphoreGive(_stateMutex);
    }
    setStatusMessage(_scanSummaryText);
}

bool RS485App::exportLogsToFile()
{
    if (!app_storage_ensure_sdcard_available()) {
        setStatusMessage("SD card unavailable for log export");
        return false;
    }
    if (!ensure_directory(kStorageRoot)) {
        setStatusMessage("Failed to create RS-485 storage directory");
        return false;
    }

    const std::string path = make_export_path();
    const bool ok = rs485_log_store_export_file(&s_logStore, path.c_str());
    setStatusMessage(ok ? "Exported log to SD card" : "Failed to export log");
    return ok;
}

bool RS485App::saveSelectedDeviceProfile()
{
    rs485_device_profile_t profile = {};
    const rs485_discovered_device_t *source = nullptr;

    if (!_discoveredDevices.empty()) {
        source = &_discoveredDevices.front();
    }

    if (source != nullptr) {
        snprintf(profile.name, sizeof(profile.name), "%s", source->user_name);
        profile.slave_address = source->slave_address;
        snprintf(profile.notes, sizeof(profile.notes), "%s", source->summary);
    } else {
        snprintf(profile.name, sizeof(profile.name), "Manual %u", (unsigned)s_settings.manual_slave_address);
        profile.slave_address = s_settings.manual_slave_address;
        snprintf(profile.notes, sizeof(profile.notes), "%s", "Saved from manual browser");
    }

    profile.protocol_mode = RS485_PROTOCOL_MODE_MODBUS_RTU;
    profile.serial = s_settings.serial;
    profile.polling_interval_ms = 1000;
    profile.scale = 1.0f;
    profile.decimals = 1;
    profile.favorite_count = 1;
    profile.favorite_registers[0] = s_settings.manual_start_address;

    if (!rs485_profile_store_upsert_profile(&s_profileStore, &profile) || !rs485_profile_store_save(&s_profileStore)) {
        setStatusMessage("Failed to save profile");
        return false;
    }

    setStatusMessage(std::string("Saved profile ") + profile.name);
    appendLogStatus("Profile saved", profile.name);
    return true;
}

bool RS485App::performManualRead(uint8_t functionCode)
{
    if (!ensureTransportReady()) {
        return false;
    }

    modbus_response_t response = {};
    modbus_status_t status = MODBUS_STATUS_INVALID_RESPONSE;
    switch (functionCode) {
    case MODBUS_FC_READ_HOLDING_REGISTERS:
        status = modbus_rtu_read_holding_registers(&s_modbusMaster,
                                                   s_settings.manual_slave_address,
                                                   s_settings.manual_start_address,
                                                   s_settings.manual_quantity,
                                                   &response);
        break;
    case MODBUS_FC_READ_INPUT_REGISTERS:
        status = modbus_rtu_read_input_registers(&s_modbusMaster,
                                                 s_settings.manual_slave_address,
                                                 s_settings.manual_start_address,
                                                 s_settings.manual_quantity,
                                                 &response);
        break;
    case MODBUS_FC_READ_COILS:
        status = modbus_rtu_read_coils(&s_modbusMaster,
                                       s_settings.manual_slave_address,
                                       s_settings.manual_start_address,
                                       s_settings.manual_quantity,
                                       &response);
        break;
    case MODBUS_FC_WRITE_SINGLE_REGISTER:
        status = modbus_rtu_write_single_register(&s_modbusMaster,
                                                  s_settings.manual_slave_address,
                                                  s_settings.manual_start_address,
                                                  _lastPrimaryValue + 1U,
                                                  &response);
        break;
    default:
        break;
    }

    std::ostringstream summary;
    summary << "Slave " << static_cast<unsigned>(s_settings.manual_slave_address)
            << " FC " << static_cast<unsigned>(functionCode) << ": " << modbus_rtu_status_to_string(status);

    if (status == MODBUS_STATUS_OK) {
        _lastPolledSlaveAddress = s_settings.manual_slave_address;
        _lastStartAddress = s_settings.manual_start_address;
        _lastPollTimestampMs = rs485_app_state_now_ms();
        _lastPollHealthy = true;
        if (response.register_count > 0) {
            _lastPrimaryValue = response.registers[0];
            summary << " value=" << response.registers[0];
        } else if (response.coil_count > 0) {
            _lastPrimaryValue = response.coils[0];
            summary << " coil=" << static_cast<unsigned>(response.coils[0]);
        }
        std::rotate(_dashboardTrend.begin(), _dashboardTrend.begin() + 1, _dashboardTrend.end());
        _dashboardTrend.back() = static_cast<int16_t>(std::min<uint16_t>(_lastPrimaryValue, 1000U));
        rs485_log_store_append(&s_logStore,
                               RS485_LOG_DIRECTION_RX,
                               RS485_LOG_KIND_MODBUS,
                               response.slave_address,
                               response.function_code,
                               0,
                               summary.str().c_str(),
                               hex_encode(response.raw_frame, response.raw_length).c_str());
    } else {
        _lastPollHealthy = false;
        if (status == MODBUS_STATUS_EXCEPTION) {
            summary << " " << modbus_rtu_exception_to_string(response.exception_code);
        } else {
            summary << " transport=" << rs485_transport_status_to_string(response.transport_status);
        }
        rs485_log_store_append(&s_logStore,
                               RS485_LOG_DIRECTION_EVENT,
                               RS485_LOG_KIND_ERROR,
                               s_settings.manual_slave_address,
                               functionCode,
                               status,
                               summary.str().c_str(),
                               hex_encode(response.raw_frame, response.raw_length).c_str());
    }

    _browserSummaryText = summary.str();
    setStatusMessage(_browserSummaryText);
    switchScreen(RS485_SCREEN_MODBUS);
    return status == MODBUS_STATUS_OK;
}

bool RS485App::sendTerminalPayload(bool hexMode)
{
    if (!ensureTransportReady()) {
        return false;
    }

    const char *text = (_terminalInput != nullptr) ? lv_textarea_get_text(_terminalInput) : nullptr;
    if ((text == nullptr) || (text[0] == '\0')) {
        setStatusMessage("Enter terminal payload first");
        return false;
    }

    std::vector<uint8_t> payload;
    if (hexMode) {
        if (!parse_hex_string(text, payload)) {
            setStatusMessage("Invalid HEX payload");
            return false;
        }
    } else {
        payload.assign(text, text + strlen(text));
        if (s_settings.terminal_append_cr) {
            payload.push_back('\r');
        }
        if (s_settings.terminal_append_lf) {
            payload.push_back('\n');
        }
    }

    const rs485_transport_status_t tx_status = rs485_transport_send_frame(&s_transport, payload.data(), payload.size());
    rs485_log_store_append(&s_logStore,
                           RS485_LOG_DIRECTION_TX,
                           RS485_LOG_KIND_RAW,
                           0,
                           0,
                           tx_status,
                           hexMode ? "HEX send" : "ASCII send",
                           hex_encode(payload.data(), payload.size()).c_str());

    if (tx_status != RS485_TRANSPORT_OK) {
        setStatusMessage(std::string("Send failed: ") + rs485_transport_status_to_string(tx_status));
        return false;
    }

    uint8_t rx_frame[256] = {0};
    size_t rx_length = 0;
    const rs485_transport_status_t rx_status = rs485_transport_receive_frame(&s_transport,
                                                                             rx_frame,
                                                                             sizeof(rx_frame),
                                                                             &rx_length,
                                                                             s_settings.serial.request_timeout_ms);
    if ((rx_status == RS485_TRANSPORT_OK) && (rx_length > 0)) {
        rs485_log_store_append(&s_logStore,
                               RS485_LOG_DIRECTION_RX,
                               RS485_LOG_KIND_RAW,
                               0,
                               0,
                               0,
                               "Terminal RX",
                               hex_encode(rx_frame, rx_length).c_str());
        setStatusMessage("Terminal TX/RX complete");
    } else {
        setStatusMessage(std::string("Sent; RX=") + rs485_transport_status_to_string(rx_status));
    }
    switchScreen(RS485_SCREEN_TERMINAL);
    return true;
}

bool RS485App::debugStartScan()
{
    return startScan();
}

bool RS485App::debugStopScan()
{
    stopScan();
    return true;
}

bool RS485App::debugSendAscii(const std::string &text)
{
    if (_terminalInput != nullptr) {
        lv_textarea_set_text(_terminalInput, text.c_str());
    }
    return sendTerminalPayload(false);
}

bool RS485App::debugSendHex(const std::string &hexText)
{
    if (_terminalInput != nullptr) {
        lv_textarea_set_text(_terminalInput, hexText.c_str());
    }
    return sendTerminalPayload(true);
}

bool RS485App::debugExportLog()
{
    return exportLogsToFile();
}

bool RS485App::debugCloseApp()
{
    bsp_display_lock(0);
    if (!bsp_display_lock(0)) {
        return false;
    }
    const bool ok = notifyCoreClosed();
    bsp_display_unlock();
    return ok;
}

std::string RS485App::debugDescribeState() const
{
    std::ostringstream stream;
    stream << "screen=" << static_cast<unsigned>(_activeScreen)
           << " scan_running=" << (_scanRunning ? "yes" : "no")
           << " found=" << _discoveredDevices.size()
           << " logs=" << rs485_log_store_count(&s_logStore)
           << " profiles=" << rs485_profile_store_profile_count(&s_profileStore)
           << " transport=" << (s_transport.installed ? "ready" : "down")
           << " baud=" << s_settings.serial.baud_rate
           << " tx=" << s_settings.serial.tx_pin
           << " rx=" << s_settings.serial.rx_pin
           << " en=" << s_settings.serial.en_pin
           << " status=\"" << _statusMessage << "\"";
    return stream.str();
}