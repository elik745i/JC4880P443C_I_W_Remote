#include "RS485Internal.hpp"

using namespace rs485app;

rs485_log_store_t s_logStore;
rs485_profile_store_t s_profileStore;
rs485_transport_t s_transport;
modbus_rtu_master_t s_modbusMaster;
rs485_app_settings_t s_settings;

RS485App::RS485App()
    : ESP_Brookesia_PhoneApp("RS-485", &rs485_png, true),
      _root(nullptr),
      _statusLabel(nullptr),
      _titleLabel(nullptr),
      _screenPanels{},
      _navButtons{},
      _actionBindings{},
      _actionBindingCount(0),
      _homeSummaryLabel(nullptr),
      _scanProgressBar(nullptr),
      _scanResultsLabel(nullptr),
      _scanDetailLabel(nullptr),
      _terminalInput(nullptr),
      _terminalTemplatesLabel(nullptr),
      _terminalLogLabel(nullptr),
      _browserResultLabel(nullptr),
      _profilesLabel(nullptr),
      _dashboardPrimaryLabel(nullptr),
      _dashboardSecondaryLabel(nullptr),
      _dashboardChart(nullptr),
      _dashboardSeries(nullptr),
      _logsLabel(nullptr),
      _settingsLabel(nullptr),
      _uiTimer(nullptr),
      _stateMutex(nullptr),
      _scanTaskHandle(nullptr),
      _scanCancelRequested(false),
      _scanRunning(false),
      _scanProgressCurrent(0),
      _scanProgressTotal(0),
      _activeScreen(RS485_SCREEN_HOME),
      _lastRenderedLogCount(0),
    _lastPolledSlaveAddress(0),
      _dashboardTrend{},
    _lastPrimaryValue(0),
    _lastStartAddress(0),
    _lastPollTimestampMs(0),
    _lastPollHealthy(false),
      _statusMessage("RS-485 app ready"),
      _scanSummaryText("No scan run yet"),
      _browserSummaryText("Select a read action to query a device"),
      _discoveredDevices{}
{
}

RS485App::~RS485App()
{
    if (_stateMutex != nullptr) {
        vSemaphoreDelete(_stateMutex);
        _stateMutex = nullptr;
    }
}

bool RS485App::init()
{
    if (_stateMutex == nullptr) {
        _stateMutex = xSemaphoreCreateMutex();
        if (_stateMutex == nullptr) {
            return false;
        }
    }

    rs485_app_state_load_settings(&s_settings);
    if (!rs485_log_store_init(&s_logStore, kLogCapacity)) {
        return false;
    }
    if (!rs485_profile_store_init(&s_profileStore)) {
        rs485_log_store_deinit(&s_logStore);
        return false;
    }

    modbus_rtu_master_init(&s_modbusMaster, &s_transport, s_settings.serial.request_timeout_ms, s_settings.serial.retry_count);
    _activeScreen = s_settings.selected_screen;
    if (_activeScreen >= static_cast<uint8_t>(RS485_SCREEN_COUNT)) {
        _activeScreen = static_cast<uint8_t>(RS485_SCREEN_HOME);
    }
    _dashboardTrend.fill(0);
    return true;
}

bool RS485App::run()
{
    if (!buildUi()) {
        return false;
    }

    tickUi();
    if (_uiTimer != nullptr) {
        lv_timer_del(_uiTimer);
    }
    _uiTimer = lv_timer_create(onUiTimer, kUiTickMs, this);
    return _uiTimer != nullptr;
}

bool RS485App::pause()
{
    if (_uiTimer != nullptr) {
        lv_timer_pause(_uiTimer);
    }
    return true;
}

bool RS485App::resume()
{
    if (_uiTimer != nullptr) {
        lv_timer_resume(_uiTimer);
    }
    tickUi();
    return true;
}

bool RS485App::back()
{
    if (_activeScreen != RS485_SCREEN_HOME) {
        switchScreen(RS485_SCREEN_HOME);
        return true;
    }
    return notifyCoreClosed();
}

bool RS485App::close()
{
    stopScan();
    while (_scanTaskHandle != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    rs485_app_state_save_settings(&s_settings);
    rs485_profile_store_save(&s_profileStore);
    rs485_transport_deinit(&s_transport);
    rs485_profile_store_deinit(&s_profileStore);
    rs485_log_store_deinit(&s_logStore);

    if (_uiTimer != nullptr) {
        lv_timer_del(_uiTimer);
        _uiTimer = nullptr;
    }

    releaseRuntimeResources();
    return true;
}

void RS485App::releaseRuntimeResources()
{
    _root = nullptr;
    _statusLabel = nullptr;
    _titleLabel = nullptr;
    _screenPanels.fill(nullptr);
    _navButtons.fill(nullptr);
    _actionBindingCount = 0;
    _homeSummaryLabel = nullptr;
    _scanProgressBar = nullptr;
    _scanResultsLabel = nullptr;
    _scanDetailLabel = nullptr;
    _terminalInput = nullptr;
    _terminalTemplatesLabel = nullptr;
    _terminalLogLabel = nullptr;
    _browserResultLabel = nullptr;
    _profilesLabel = nullptr;
    _dashboardPrimaryLabel = nullptr;
    _dashboardSecondaryLabel = nullptr;
    _dashboardChart = nullptr;
    _dashboardSeries = nullptr;
    _logsLabel = nullptr;
    _settingsLabel = nullptr;
    _discoveredDevices.clear();
    _discoveredDevices.shrink_to_fit();
    _lastRenderedLogCount = 0;
    _lastPolledSlaveAddress = 0;
    _lastPrimaryValue = 0;
    _lastStartAddress = 0;
    _lastPollTimestampMs = 0;
    _lastPollHealthy = false;
}

void RS485App::onUiTimer(lv_timer_t *timer)
{
    RS485App *app = static_cast<RS485App *>(timer->user_data);
    if (app != nullptr) {
        app->tickUi();
    }
}

void RS485App::scanTaskEntry(void *context)
{
    RS485App *app = static_cast<RS485App *>(context);
    if (app != nullptr) {
        app->scanTask();
    }
    vTaskDelete(nullptr);
}