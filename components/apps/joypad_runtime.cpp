#include "joypad_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "soc/adc_channel.h"

namespace {
constexpr const char *kTag = "JoypadRuntime";
constexpr const char *kNvsNamespace = "storage";
constexpr const char *kBackendKey = "joy_backend";
constexpr const char *kBleEnabledKey = "joy_ble_en";
constexpr const char *kBleDiscoveryKey = "joy_ble_disc";
constexpr const char *kBleDeviceKey = "joy_ble_addr";
constexpr const char *kBleRemapKey = "joy_ble_map";
constexpr const char *kManualModeKey = "joy_man_mode";
constexpr const char *kManualSpiKey = "joy_man_spi";
constexpr const char *kManualResistiveKey = "joy_man_res";
constexpr const char *kManualResistiveButtonsKey = "joy_man_resb";
constexpr const char *kManualMcpGpioKey = "joy_man_mcpi";
constexpr const char *kManualMcpButtonsKey = "joy_man_mcpb";
constexpr const char *kBleCalibrationSlotsKey = "joy_ble_cal";
constexpr const char *kBleCalibrationCounterKey = "joy_ble_caln";
constexpr size_t kBleCalibrationSlotCount = 20;
constexpr int32_t kAxisNormalizeRange = 1024;
constexpr int32_t kAxisNormalizeHalfRange = kAxisNormalizeRange / 2;
constexpr size_t kManualInterfaceGpioCount = 2;
constexpr size_t kMcpPinCount = 16;
constexpr uint8_t kMcp23017AddressMin = 0x20;
constexpr uint8_t kMcp23017AddressMax = 0x27;
constexpr uint32_t kMcpClockHz = 100000;
constexpr uint32_t kI2cTimeoutMs = 25;
constexpr TickType_t kManualPollPeriod = pdMS_TO_TICKS(10);
constexpr uint32_t kManualPollTaskStack = 4096;
constexpr UBaseType_t kManualPollTaskPriority = 1;
constexpr int kAdcSampleCount = 4;
constexpr int kResistivePullupOhms = 10000;
constexpr int kResistiveIdleThreshold = 3800;

enum : uint8_t {
    kMcpIodirA = 0x00,
    kMcpIodirB = 0x01,
    kMcpGppuA = 0x0C,
    kMcpGppuB = 0x0D,
    kMcpGpioA = 0x12,
};

constexpr std::array<int32_t, 8> kResistiveButtonOhms = {330, 680, 1000, 2200, 3300, 4700, 6800, 10000};
constexpr size_t kLegacyMcpMappedButtonCount = 7;
constexpr std::array<size_t, kLegacyMcpMappedButtonCount> kLegacyMcpButtonControlMap = {
    JC4880_JOYPAD_SPI_CONTROL_START,
    JC4880_JOYPAD_SPI_CONTROL_EXIT,
    JC4880_JOYPAD_SPI_CONTROL_SAVE,
    JC4880_JOYPAD_SPI_CONTROL_LOAD,
    JC4880_JOYPAD_SPI_CONTROL_BUTTON_A,
    JC4880_JOYPAD_SPI_CONTROL_BUTTON_B,
    JC4880_JOYPAD_SPI_CONTROL_BUTTON_C,
};
constexpr std::array<const char *, JC4880_JOYPAD_SPI_CONTROL_COUNT> kManualControlLogLabels = {
    "Up",
    "Down",
    "Left",
    "Right",
    "Start",
    "Exit",
    "Save",
    "Load",
    "A",
    "B",
    "C",
    "Key",
};

struct BleCalibrationCapture {
    bool active = false;
    char device_addr[18] = {};
    char device_name[32] = {};
    int16_t axis_center[4] = {};
    int16_t axis_min[4] = {};
    int16_t axis_max[4] = {};
    uint16_t pedal_min[2] = {};
    uint16_t pedal_max[2] = {};
};

constexpr std::array<int8_t, 12> kAllowedPins = {28, 29, 30, 31, 32, 33, 34, 35, 49, 50, 51, 52};

std::mutex s_mutex;
bool s_initialized = false;
bool s_bleConnected = false;
bool s_bleScanning = false;
uint32_t s_bleRawMask = 0;
uint32_t s_bleGameplayMask = 0;
uint32_t s_bleActionMask = 0;
uint32_t s_previousActionState = 0;
int16_t s_bleAxisX = 0;
int16_t s_bleAxisY = 0;
int16_t s_bleAxisRx = 0;
int16_t s_bleAxisRy = 0;
uint16_t s_bleBrake = 0;
uint16_t s_bleThrottle = 0;
int16_t s_bleRawAxisX = 0;
int16_t s_bleRawAxisY = 0;
int16_t s_bleRawAxisRx = 0;
int16_t s_bleRawAxisRy = 0;
uint16_t s_bleRawBrake = 0;
uint16_t s_bleRawThrottle = 0;
char s_bleDeviceName[32] = {};
char s_bleDeviceAddr[18] = {};
std::array<jc4880_joypad_ble_calibration_slot_t, kBleCalibrationSlotCount> s_bleCalibrationSlots = {};
uint32_t s_bleCalibrationCounter = 0;
bool s_bleCalibrationLoaded = false;
int s_bleCalibrationSlotIndex = -1;
BleCalibrationCapture s_bleCalibrationCapture = {};
jc4880_joypad_config_changed_callback_t s_configChangedCallback = nullptr;
void *s_configChangedCallbackContext = nullptr;
std::array<int8_t, JC4880_JOYPAD_SPI_CONTROL_COUNT + kManualInterfaceGpioCount> s_lastConfiguredPins = {};
adc_oneshot_unit_handle_t s_manualAdcHandle = nullptr;
std::array<bool, 8> s_manualAdcChannelConfigured = {};
i2c_master_bus_handle_t s_manualMcpBus = nullptr;
i2c_master_dev_handle_t s_manualMcpDevice = nullptr;
std::array<int8_t, kManualInterfaceGpioCount> s_lastMcpI2cPins = {-1, -1};
uint8_t s_manualMcpAddress = 0;
bool s_manualMcpDetected = false;
bool s_manualMcpMissingLogged = false;
uint16_t s_previousMcpControlMask = 0;
TaskHandle_t s_manualPollTask = nullptr;
uint32_t s_cachedManualGameplayMask = 0;
uint32_t s_cachedManualActionMask = 0;
int s_lastAnalogAxisY = 2048;
int s_lastAnalogAxisX = 2048;
uint32_t s_lastAnalogAxisMask = 0;

constexpr jc4880_joypad_config_t makeDefaultConfig()
{
    jc4880_joypad_config_t config = {};
    config.backend = JC4880_JOYPAD_BACKEND_DISABLED;
    config.manual_mode = JC4880_JOYPAD_MANUAL_MODE_SPI;
    for (size_t index = 0; index < JC4880_JOYPAD_BLE_CONTROL_COUNT; ++index) {
        config.ble_remap[index] = (index < 8) ? static_cast<uint8_t>(index + 1) : static_cast<uint8_t>(JC4880_JOYPAD_MAP_NONE);
    }
    for (size_t index = 0; index < JC4880_JOYPAD_SPI_CONTROL_COUNT; ++index) {
        config.manual_spi_gpio[index] = -1;
    }
    config.manual_resistive_gpio[0] = 50;
    config.manual_resistive_gpio[1] = 51;
    for (size_t index = 0; index < JC4880_JOYPAD_BUTTON_CONTROL_COUNT; ++index) {
        config.manual_resistive_button_binding[index] = -1;
    }
    for (size_t index = 0; index < JC4880_JOYPAD_SPI_CONTROL_COUNT; ++index) {
        config.manual_mcp_button_pin[index] = -1;
    }
    config.manual_mcp_i2c_gpio[0] = 30;
    config.manual_mcp_i2c_gpio[1] = 31;
    config.manual_mcp_button_pin[JC4880_JOYPAD_SPI_CONTROL_START] = 12;
    config.manual_mcp_button_pin[JC4880_JOYPAD_SPI_CONTROL_EXIT] = 11;
    config.manual_mcp_button_pin[JC4880_JOYPAD_SPI_CONTROL_SAVE] = 15;
    config.manual_mcp_button_pin[JC4880_JOYPAD_SPI_CONTROL_LOAD] = 14;
    config.manual_mcp_button_pin[JC4880_JOYPAD_SPI_CONTROL_BUTTON_A] = 10;
    config.manual_mcp_button_pin[JC4880_JOYPAD_SPI_CONTROL_BUTTON_B] = 9;
    config.manual_mcp_button_pin[JC4880_JOYPAD_SPI_CONTROL_BUTTON_C] = 8;
    config.manual_mcp_button_pin[JC4880_JOYPAD_SPI_CONTROL_BUTTON_Y] = 12;
    config.ble_device_addr[0] = '\0';
    return config;
}

constexpr int kAnalogAxisCenter = 2048;
constexpr int kAnalogAxisDeadzone = 600;

jc4880_joypad_config_t s_config = makeDefaultConfig();

bool isAllowedPin(int8_t pin)
{
    return std::find(kAllowedPins.begin(), kAllowedPins.end(), pin) != kAllowedPins.end();
}

bool isAnalogCapablePin(int8_t pin)
{
    return (pin == 50) || (pin == 51);
}

bool isResistiveBindingValid(int8_t binding)
{
    return (binding >= -1) && (binding < static_cast<int8_t>(kResistiveButtonOhms.size() * kManualInterfaceGpioCount));
}

bool isMcpPinValid(int8_t pin)
{
    return (pin >= -1) && (pin < static_cast<int8_t>(kMcpPinCount));
}

int resistiveLadderIndex(int8_t binding)
{
    return binding / static_cast<int8_t>(kResistiveButtonOhms.size());
}

int resistiveResistorIndex(int8_t binding)
{
    return binding % static_cast<int8_t>(kResistiveButtonOhms.size());
}

adc_channel_t adcChannelForPin(int8_t pin)
{
    switch (pin) {
        case 50:
            return static_cast<adc_channel_t>(ADC2_GPIO50_CHANNEL);
        case 51:
            return static_cast<adc_channel_t>(ADC2_GPIO51_CHANNEL);
        default:
            return ADC_CHANNEL_0;
    }
}

uint32_t gameplayMaskForButtonControl(size_t index)
{
    switch (index) {
        case JC4880_JOYPAD_BUTTON_CONTROL_START:
            return JC4880_JOYPAD_MASK_START;
        case JC4880_JOYPAD_BUTTON_CONTROL_BUTTON_A:
            return JC4880_JOYPAD_MASK_BUTTON_A;
        case JC4880_JOYPAD_BUTTON_CONTROL_BUTTON_B:
            return JC4880_JOYPAD_MASK_BUTTON_B;
        case JC4880_JOYPAD_BUTTON_CONTROL_BUTTON_C:
            return JC4880_JOYPAD_MASK_BUTTON_C;
        case JC4880_JOYPAD_BUTTON_CONTROL_BUTTON_Y:
            return JC4880_JOYPAD_MASK_BUTTON_Y;
        default:
            return 0;
    }
}

uint32_t actionMaskForButtonControl(size_t index)
{
    switch (index) {
        case JC4880_JOYPAD_BUTTON_CONTROL_EXIT:
            return JC4880_JOYPAD_ACTION_EXIT;
        case JC4880_JOYPAD_BUTTON_CONTROL_SAVE:
            return JC4880_JOYPAD_ACTION_SAVE;
        case JC4880_JOYPAD_BUTTON_CONTROL_LOAD:
            return JC4880_JOYPAD_ACTION_LOAD;
        default:
            return 0;
    }
}

int expectedResistiveRawForBinding(int8_t binding)
{
    if (!isResistiveBindingValid(binding) || (binding < 0)) {
        return kAxisNormalizeRange;
    }

    const int resistor_index = resistiveResistorIndex(binding);
    const int32_t resistor_ohms = kResistiveButtonOhms[static_cast<size_t>(resistor_index)];
    return static_cast<int>(std::lround((4095.0 * resistor_ohms) / static_cast<double>(kResistivePullupOhms + resistor_ohms)));
}

void sanitizeConfig(jc4880_joypad_config_t &config)
{
    if ((config.backend != JC4880_JOYPAD_BACKEND_DISABLED) &&
        (config.backend != JC4880_JOYPAD_BACKEND_BLE) &&
        (config.backend != JC4880_JOYPAD_BACKEND_MANUAL)) {
        config.backend = JC4880_JOYPAD_BACKEND_DISABLED;
    }

    if ((config.manual_mode != JC4880_JOYPAD_MANUAL_MODE_SPI) &&
        (config.manual_mode != JC4880_JOYPAD_MANUAL_MODE_RESISTIVE) &&
        (config.manual_mode != JC4880_JOYPAD_MANUAL_MODE_MCP23017)) {
        config.manual_mode = JC4880_JOYPAD_MANUAL_MODE_SPI;
    }

    for (size_t index = 0; index < JC4880_JOYPAD_BLE_CONTROL_COUNT; ++index) {
        if (config.ble_remap[index] > JC4880_JOYPAD_MAP_LOAD) {
            config.ble_remap[index] = (index < 8) ? static_cast<uint8_t>(index + 1) : static_cast<uint8_t>(JC4880_JOYPAD_MAP_NONE);
        }
    }

    for (size_t index = 0; index < JC4880_JOYPAD_SPI_CONTROL_COUNT; ++index) {
        if (!isAllowedPin(config.manual_spi_gpio[index])) {
            config.manual_spi_gpio[index] = -1;
        }
    }
    for (size_t index = 0; index < 2; ++index) {
        if (!isAnalogCapablePin(config.manual_resistive_gpio[index]) && (config.manual_resistive_gpio[index] != -1)) {
            config.manual_resistive_gpio[index] = -1;
        }
        if (!isAllowedPin(config.manual_mcp_i2c_gpio[index])) {
            config.manual_mcp_i2c_gpio[index] = -1;
        }
    }
    for (size_t index = 0; index < JC4880_JOYPAD_BUTTON_CONTROL_COUNT; ++index) {
        if (!isResistiveBindingValid(config.manual_resistive_button_binding[index])) {
            config.manual_resistive_button_binding[index] = -1;
        }
    }
    for (size_t index = 0; index < JC4880_JOYPAD_SPI_CONTROL_COUNT; ++index) {
        if (!isMcpPinValid(config.manual_mcp_button_pin[index])) {
            config.manual_mcp_button_pin[index] = -1;
        }
    }

    config.ble_device_addr[sizeof(config.ble_device_addr) - 1] = '\0';
}

bool loadBlob(nvs_handle_t handle, const char *key, void *buffer, size_t size)
{
    size_t actualSize = size;
    return (nvs_get_blob(handle, key, buffer, &actualSize) == ESP_OK) && (actualSize == size);
}

void loadManualSpiPins(nvs_handle_t handle)
{
    size_t actual_size = 0;
    if (nvs_get_blob(handle, kManualSpiKey, nullptr, &actual_size) != ESP_OK) {
        return;
    }

    if (actual_size == sizeof(s_config.manual_spi_gpio)) {
        nvs_get_blob(handle, kManualSpiKey, s_config.manual_spi_gpio, &actual_size);
        return;
    }

    const size_t legacy_size = (JC4880_JOYPAD_SPI_CONTROL_COUNT - 1) * sizeof(int8_t);
    if (actual_size == legacy_size) {
        std::array<int8_t, JC4880_JOYPAD_SPI_CONTROL_COUNT - 1> legacy_pins = {};
        if (nvs_get_blob(handle, kManualSpiKey, legacy_pins.data(), &actual_size) != ESP_OK) {
            return;
        }
        for (size_t index = 0; index < legacy_pins.size(); ++index) {
            s_config.manual_spi_gpio[index] = legacy_pins[index];
        }
    }
}

void loadManualResistiveBindings(nvs_handle_t handle)
{
    size_t actual_size = 0;
    if (nvs_get_blob(handle, kManualResistiveButtonsKey, nullptr, &actual_size) != ESP_OK) {
        return;
    }

    if (actual_size == sizeof(s_config.manual_resistive_button_binding)) {
        nvs_get_blob(handle, kManualResistiveButtonsKey, s_config.manual_resistive_button_binding, &actual_size);
        return;
    }

    const size_t legacy_size = (JC4880_JOYPAD_BUTTON_CONTROL_COUNT - 1) * sizeof(int8_t);
    if (actual_size == legacy_size) {
        std::array<int8_t, JC4880_JOYPAD_BUTTON_CONTROL_COUNT - 1> legacy_bindings = {};
        if (nvs_get_blob(handle, kManualResistiveButtonsKey, legacy_bindings.data(), &actual_size) != ESP_OK) {
            return;
        }
        for (size_t index = 0; index < legacy_bindings.size(); ++index) {
            s_config.manual_resistive_button_binding[index] = legacy_bindings[index];
        }
    }
}

void loadManualMcpPins(nvs_handle_t handle)
{
    size_t actual_size = 0;
    if (nvs_get_blob(handle, kManualMcpButtonsKey, nullptr, &actual_size) != ESP_OK) {
        return;
    }

    if (actual_size == sizeof(s_config.manual_mcp_button_pin)) {
        nvs_get_blob(handle, kManualMcpButtonsKey, s_config.manual_mcp_button_pin, &actual_size);
        return;
    }

    const size_t previous_spi_control_size = (JC4880_JOYPAD_SPI_CONTROL_COUNT - 1) * sizeof(int8_t);
    if (actual_size == previous_spi_control_size) {
        std::array<int8_t, JC4880_JOYPAD_SPI_CONTROL_COUNT - 1> legacy_pins = {};
        if (nvs_get_blob(handle, kManualMcpButtonsKey, legacy_pins.data(), &actual_size) != ESP_OK) {
            return;
        }

        for (size_t index = 0; index < legacy_pins.size(); ++index) {
            s_config.manual_mcp_button_pin[index] = legacy_pins[index];
        }
        return;
    }

    if (actual_size == (kLegacyMcpMappedButtonCount * sizeof(int8_t))) {
        std::array<int8_t, kLegacyMcpMappedButtonCount> legacy_pins = {};
        if (nvs_get_blob(handle, kManualMcpButtonsKey, legacy_pins.data(), &actual_size) != ESP_OK) {
            return;
        }

        for (size_t index = 0; index < legacy_pins.size(); ++index) {
            s_config.manual_mcp_button_pin[kLegacyMcpButtonControlMap[index]] = legacy_pins[index];
        }
    }
}

int findCalibrationSlotLocked(const char *device_addr)
{
    if ((device_addr == nullptr) || (device_addr[0] == '\0')) {
        return -1;
    }

    for (size_t index = 0; index < s_bleCalibrationSlots.size(); ++index) {
        const auto &slot = s_bleCalibrationSlots[index];
        if ((slot.valid == 0) || (slot.device_addr[0] == '\0')) {
            continue;
        }
        if (std::strncmp(slot.device_addr, device_addr, sizeof(slot.device_addr)) == 0) {
            return static_cast<int>(index);
        }
    }

    return -1;
}

int normalizeAxisValue(int32_t raw_value, int16_t center, int16_t min_value, int16_t max_value)
{
    if (raw_value >= center) {
        const int32_t positive_span = std::max<int32_t>(1, static_cast<int32_t>(max_value) - static_cast<int32_t>(center));
        const int32_t scaled = ((raw_value - center) * kAxisNormalizeHalfRange) / positive_span;
        return std::min<int32_t>(kAxisNormalizeHalfRange, std::max<int32_t>(0, scaled));
    }

    const int32_t negative_span = std::max<int32_t>(1, static_cast<int32_t>(center) - static_cast<int32_t>(min_value));
    const int32_t scaled = ((raw_value - center) * kAxisNormalizeHalfRange) / negative_span;
    return std::max<int32_t>(-kAxisNormalizeHalfRange, std::min<int32_t>(0, scaled));
}

uint16_t normalizePedalValue(int32_t raw_value, uint16_t min_value, uint16_t max_value)
{
    const int32_t span = std::max<int32_t>(1, static_cast<int32_t>(max_value) - static_cast<int32_t>(min_value));
    const int32_t scaled = ((raw_value - min_value) * kAxisNormalizeRange) / span;
    return static_cast<uint16_t>(std::max<int32_t>(0, std::min<int32_t>(kAxisNormalizeRange, scaled)));
}

void updateCalibrationLoadedStateLocked(const char *device_addr)
{
    s_bleCalibrationSlotIndex = findCalibrationSlotLocked(device_addr);
    s_bleCalibrationLoaded = s_bleCalibrationSlotIndex >= 0;
}

void loadCalibrationSlotsLocked(nvs_handle_t handle)
{
    s_bleCalibrationSlots = {};
    s_bleCalibrationCounter = 0;
    loadBlob(handle, kBleCalibrationSlotsKey, s_bleCalibrationSlots.data(), sizeof(s_bleCalibrationSlots));
    nvs_get_u32(handle, kBleCalibrationCounterKey, &s_bleCalibrationCounter);
}

bool saveCalibrationSlotsLocked(nvs_handle_t handle)
{
    return (nvs_set_blob(handle, kBleCalibrationSlotsKey, s_bleCalibrationSlots.data(), sizeof(s_bleCalibrationSlots)) == ESP_OK) &&
           (nvs_set_u32(handle, kBleCalibrationCounterKey, s_bleCalibrationCounter) == ESP_OK);
}

int allocateCalibrationSlotLocked(const char *device_addr)
{
    const int existing_slot = findCalibrationSlotLocked(device_addr);
    if (existing_slot >= 0) {
        return existing_slot;
    }

    for (size_t index = 0; index < s_bleCalibrationSlots.size(); ++index) {
        if (s_bleCalibrationSlots[index].valid == 0) {
            return static_cast<int>(index);
        }
    }

    size_t oldest_index = 0;
    uint32_t oldest_counter = s_bleCalibrationSlots[0].last_used_counter;
    for (size_t index = 1; index < s_bleCalibrationSlots.size(); ++index) {
        if (s_bleCalibrationSlots[index].last_used_counter < oldest_counter) {
            oldest_counter = s_bleCalibrationSlots[index].last_used_counter;
            oldest_index = index;
        }
    }

    return static_cast<int>(oldest_index);
}

void updateCalibrationCaptureLocked(int16_t axis_x,
                                    int16_t axis_y,
                                    int16_t axis_rx,
                                    int16_t axis_ry,
                                    uint16_t brake,
                                    uint16_t throttle)
{
    if (!s_bleCalibrationCapture.active) {
        return;
    }

    const int16_t axis_values[4] = {axis_x, axis_y, axis_rx, axis_ry};
    const uint16_t pedal_values[2] = {brake, throttle};
    for (size_t index = 0; index < 4; ++index) {
        s_bleCalibrationCapture.axis_min[index] = std::min<int16_t>(s_bleCalibrationCapture.axis_min[index], axis_values[index]);
        s_bleCalibrationCapture.axis_max[index] = std::max<int16_t>(s_bleCalibrationCapture.axis_max[index], axis_values[index]);
    }
    for (size_t index = 0; index < 2; ++index) {
        s_bleCalibrationCapture.pedal_min[index] = std::min<uint16_t>(s_bleCalibrationCapture.pedal_min[index], pedal_values[index]);
        s_bleCalibrationCapture.pedal_max[index] = std::max<uint16_t>(s_bleCalibrationCapture.pedal_max[index], pedal_values[index]);
    }
}

void applyCalibrationLocked(int16_t &axis_x,
                            int16_t &axis_y,
                            int16_t &axis_rx,
                            int16_t &axis_ry,
                            uint16_t &brake,
                            uint16_t &throttle)
{
    if (!s_bleCalibrationLoaded || (s_bleCalibrationSlotIndex < 0)) {
        return;
    }

    const auto &slot = s_bleCalibrationSlots[static_cast<size_t>(s_bleCalibrationSlotIndex)];
    axis_x = static_cast<int16_t>(normalizeAxisValue(axis_x, slot.axis_center[0], slot.axis_min[0], slot.axis_max[0]));
    axis_y = static_cast<int16_t>(normalizeAxisValue(axis_y, slot.axis_center[1], slot.axis_min[1], slot.axis_max[1]));
    axis_rx = static_cast<int16_t>(normalizeAxisValue(axis_rx, slot.axis_center[2], slot.axis_min[2], slot.axis_max[2]));
    axis_ry = static_cast<int16_t>(normalizeAxisValue(axis_ry, slot.axis_center[3], slot.axis_min[3], slot.axis_max[3]));
    brake = normalizePedalValue(brake, slot.pedal_min[0], slot.pedal_max[0]);
    throttle = normalizePedalValue(throttle, slot.pedal_min[1], slot.pedal_max[1]);
}

void buildCalibrationPreviewLocked(int16_t &axis_x,
                                   int16_t &axis_y,
                                   int16_t &axis_rx,
                                   int16_t &axis_ry,
                                   uint16_t &brake,
                                   uint16_t &throttle)
{
    axis_x = normalizeAxisValue(s_bleRawAxisX,
                                s_bleCalibrationCapture.axis_center[0],
                                s_bleCalibrationCapture.axis_min[0],
                                s_bleCalibrationCapture.axis_max[0]);
    axis_y = normalizeAxisValue(s_bleRawAxisY,
                                s_bleCalibrationCapture.axis_center[1],
                                s_bleCalibrationCapture.axis_min[1],
                                s_bleCalibrationCapture.axis_max[1]);
    axis_rx = normalizeAxisValue(s_bleRawAxisRx,
                                 s_bleCalibrationCapture.axis_center[2],
                                 s_bleCalibrationCapture.axis_min[2],
                                 s_bleCalibrationCapture.axis_max[2]);
    axis_ry = normalizeAxisValue(s_bleRawAxisRy,
                                 s_bleCalibrationCapture.axis_center[3],
                                 s_bleCalibrationCapture.axis_min[3],
                                 s_bleCalibrationCapture.axis_max[3]);
    brake = normalizePedalValue(s_bleRawBrake,
                                s_bleCalibrationCapture.pedal_min[0],
                                s_bleCalibrationCapture.pedal_max[0]);
    throttle = normalizePedalValue(s_bleRawThrottle,
                                   s_bleCalibrationCapture.pedal_min[1],
                                   s_bleCalibrationCapture.pedal_max[1]);
}

void configurePinInput(int8_t pin)
{
    if (!isAllowedPin(pin)) {
        return;
    }

    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << pin;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    const esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to configure GPIO %d for joypad input: %s", static_cast<int>(pin), esp_err_to_name(err));
    }
}

void configureAnalogInput(int8_t pin)
{
    if (!isAnalogCapablePin(pin)) {
        return;
    }

    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << pin;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    const esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to configure GPIO %d for joypad analog input: %s", static_cast<int>(pin), esp_err_to_name(err));
    }
}

void resetManualAdcLocked()
{
    if (s_manualAdcHandle != nullptr) {
        adc_oneshot_del_unit(s_manualAdcHandle);
        s_manualAdcHandle = nullptr;
    }
    s_manualAdcChannelConfigured.fill(false);
}

bool ensureManualAdcLocked()
{
    if (s_manualAdcHandle != nullptr) {
        return true;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {};
    unit_config.unit_id = ADC_UNIT_2;
    const esp_err_t err = adc_oneshot_new_unit(&unit_config, &s_manualAdcHandle);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to init ADC2 for joypad analog inputs (resistive or MCP X/Y axes): %s", esp_err_to_name(err));
        s_manualAdcHandle = nullptr;
        return false;
    }

    return true;
}

bool readAnalogPinLocked(int8_t pin, int &raw_value)
{
    if (!isAnalogCapablePin(pin) || !ensureManualAdcLocked()) {
        return false;
    }

    const adc_channel_t channel = adcChannelForPin(pin);
    const size_t channel_index = static_cast<size_t>(channel);
    if (channel_index >= s_manualAdcChannelConfigured.size()) {
        return false;
    }

    if (!s_manualAdcChannelConfigured[channel_index]) {
        adc_oneshot_chan_cfg_t channel_config = {};
        channel_config.atten = ADC_ATTEN_DB_12;
        channel_config.bitwidth = ADC_BITWIDTH_DEFAULT;
        const esp_err_t err = adc_oneshot_config_channel(s_manualAdcHandle, channel, &channel_config);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "Failed to configure ADC channel %d: %s", static_cast<int>(channel), esp_err_to_name(err));
            return false;
        }
        s_manualAdcChannelConfigured[channel_index] = true;
    }

    int accumulator = 0;
    for (int sample_index = 0; sample_index < kAdcSampleCount; ++sample_index) {
        int sample = 0;
        const esp_err_t err = adc_oneshot_read(s_manualAdcHandle, channel, &sample);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "Failed to read ADC channel %d: %s", static_cast<int>(channel), esp_err_to_name(err));
            return false;
        }
        accumulator += sample;
    }

    raw_value = accumulator / kAdcSampleCount;
    return true;
}

void resetManualMcpLocked()
{
    if (s_manualMcpDevice != nullptr) {
        i2c_master_bus_rm_device(s_manualMcpDevice);
        s_manualMcpDevice = nullptr;
    }
    if (s_manualMcpBus != nullptr) {
        i2c_del_master_bus(s_manualMcpBus);
        s_manualMcpBus = nullptr;
    }
    s_lastMcpI2cPins = {-1, -1};
    s_manualMcpAddress = 0;
    s_manualMcpDetected = false;
    s_previousMcpControlMask = 0;
}

bool writeMcpRegisterLocked(uint8_t reg, uint8_t value, bool log_error = true)
{
    if (s_manualMcpDevice == nullptr) {
        return false;
    }

    const uint8_t payload[] = {reg, value};
    const esp_err_t err = i2c_master_transmit(s_manualMcpDevice, payload, sizeof(payload), kI2cTimeoutMs);
    if (err != ESP_OK) {
        if (log_error) {
            ESP_LOGW(kTag, "Failed to write MCP23017 reg 0x%02X: %s", reg, esp_err_to_name(err));
        }
        return false;
    }

    return true;
}

bool ensureManualMcpLocked()
{
    const std::array<int8_t, kManualInterfaceGpioCount> current_pins = {
        s_config.manual_mcp_i2c_gpio[0],
        s_config.manual_mcp_i2c_gpio[1],
    };
    if ((current_pins[0] < 0) || (current_pins[1] < 0)) {
        return false;
    }

    if ((s_manualMcpDevice != nullptr) && (current_pins == s_lastMcpI2cPins)) {
        return true;
    }

    if (current_pins != s_lastMcpI2cPins) {
        s_manualMcpMissingLogged = false;
    }

    resetManualMcpLocked();

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = 0;
    bus_config.sda_io_num = static_cast<gpio_num_t>(current_pins[0]);
    bus_config.scl_io_num = static_cast<gpio_num_t>(current_pins[1]);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bus_config, &s_manualMcpBus) != ESP_OK) {
        s_manualMcpBus = nullptr;
        return false;
    }

    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.scl_speed_hz = kMcpClockHz;

    for (uint8_t address = kMcp23017AddressMin; address <= kMcp23017AddressMax; ++address) {
        device_config.device_address = address;
        if (i2c_master_bus_add_device(s_manualMcpBus, &device_config, &s_manualMcpDevice) != ESP_OK) {
            continue;
        }

        const bool configured = writeMcpRegisterLocked(kMcpIodirA, 0xFF, false) &&
                                writeMcpRegisterLocked(kMcpIodirB, 0xFF, false) &&
                                writeMcpRegisterLocked(kMcpGppuA, 0xFF, false) &&
                                writeMcpRegisterLocked(kMcpGppuB, 0xFF, false);
        if (configured) {
            s_lastMcpI2cPins = current_pins;
            s_manualMcpAddress = address;
            s_manualMcpDetected = true;
            s_manualMcpMissingLogged = false;
            ESP_LOGI(kTag,
                     "Detected MCP23017 at 0x%02X on SDA GPIO %d / SCL GPIO %d",
                     static_cast<unsigned>(address),
                     static_cast<int>(current_pins[0]),
                     static_cast<int>(current_pins[1]));
            return true;
        }

        i2c_master_bus_rm_device(s_manualMcpDevice);
        s_manualMcpDevice = nullptr;
    }

    if (!s_manualMcpMissingLogged) {
        ESP_LOGW(kTag,
                 "No MCP23017 detected on SDA GPIO %d / SCL GPIO %d after scanning 0x%02X-0x%02X. Floating A0/A1/A2 can leave the address undefined; tie them explicitly to GND or 3V3.",
                 static_cast<int>(current_pins[0]),
                 static_cast<int>(current_pins[1]),
                 static_cast<unsigned>(kMcp23017AddressMin),
                 static_cast<unsigned>(kMcp23017AddressMax));
        s_manualMcpMissingLogged = true;
    }

    resetManualMcpLocked();
    return false;
}

bool readMcpStateLocked(uint16_t &state)
{
    if (!ensureManualMcpLocked()) {
        return false;
    }

    const uint8_t start_reg = kMcpGpioA;
    uint8_t gpio_state[2] = {};
    const esp_err_t err = i2c_master_transmit_receive(s_manualMcpDevice, &start_reg, sizeof(start_reg), gpio_state, sizeof(gpio_state), kI2cTimeoutMs);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to read MCP23017 GPIO state: %s", esp_err_to_name(err));
        resetManualMcpLocked();
        return false;
    }

    state = static_cast<uint16_t>(gpio_state[0]) | (static_cast<uint16_t>(gpio_state[1]) << 8);
    return true;
}

void configureManualPinsLocked()
{
    std::array<int8_t, JC4880_JOYPAD_SPI_CONTROL_COUNT + kManualInterfaceGpioCount> currentPins = {};
    size_t nextIndex = 0;
    for (int8_t pin : s_config.manual_spi_gpio) {
        currentPins[nextIndex++] = pin;
    }
    for (int8_t pin : s_config.manual_resistive_gpio) {
        currentPins[nextIndex++] = pin;
    }

    if (currentPins == s_lastConfiguredPins) {
        return;
    }

    for (size_t index = 0; index < JC4880_JOYPAD_SPI_CONTROL_COUNT; ++index) {
        configurePinInput(currentPins[index]);
    }
    for (size_t index = JC4880_JOYPAD_SPI_CONTROL_COUNT; index < currentPins.size(); ++index) {
        configureAnalogInput(currentPins[index]);
    }
    s_lastConfiguredPins = currentPins;
    resetManualAdcLocked();
    resetManualMcpLocked();
}

uint32_t mapTargetToGameplayMask(uint8_t target)
{
    switch (target) {
        case JC4880_JOYPAD_MAP_UP:
            return JC4880_JOYPAD_MASK_UP;
        case JC4880_JOYPAD_MAP_DOWN:
            return JC4880_JOYPAD_MASK_DOWN;
        case JC4880_JOYPAD_MAP_LEFT:
            return JC4880_JOYPAD_MASK_LEFT;
        case JC4880_JOYPAD_MAP_RIGHT:
            return JC4880_JOYPAD_MASK_RIGHT;
        case JC4880_JOYPAD_MAP_BUTTON_A:
            return JC4880_JOYPAD_MASK_BUTTON_A;
        case JC4880_JOYPAD_MAP_BUTTON_B:
            return JC4880_JOYPAD_MASK_BUTTON_B;
        case JC4880_JOYPAD_MAP_BUTTON_C:
            return JC4880_JOYPAD_MASK_BUTTON_C;
        case JC4880_JOYPAD_MAP_START:
            return JC4880_JOYPAD_MASK_START;
        default:
            return 0;
    }
}

uint32_t mapTargetToActionMask(uint8_t target)
{
    switch (target) {
        case JC4880_JOYPAD_MAP_EXIT:
            return JC4880_JOYPAD_ACTION_EXIT;
        case JC4880_JOYPAD_MAP_SAVE:
            return JC4880_JOYPAD_ACTION_SAVE;
        case JC4880_JOYPAD_MAP_LOAD:
            return JC4880_JOYPAD_ACTION_LOAD;
        default:
            return 0;
    }
}

bool isPressed(int8_t pin)
{
    return isAllowedPin(pin) && (gpio_get_level(static_cast<gpio_num_t>(pin)) == 0);
}

void applyManualButtonControl(size_t control_index, uint32_t &gameplayMask, uint32_t &actionMask)
{
    gameplayMask |= gameplayMaskForButtonControl(control_index);
    actionMask |= actionMaskForButtonControl(control_index);
}

void applyManualSpiControl(size_t control_index, uint32_t &gameplayMask, uint32_t &actionMask)
{
    switch (control_index) {
        case JC4880_JOYPAD_SPI_CONTROL_UP:
            gameplayMask |= JC4880_JOYPAD_MASK_UP;
            break;
        case JC4880_JOYPAD_SPI_CONTROL_DOWN:
            gameplayMask |= JC4880_JOYPAD_MASK_DOWN;
            break;
        case JC4880_JOYPAD_SPI_CONTROL_LEFT:
            gameplayMask |= JC4880_JOYPAD_MASK_LEFT;
            break;
        case JC4880_JOYPAD_SPI_CONTROL_RIGHT:
            gameplayMask |= JC4880_JOYPAD_MASK_RIGHT;
            break;
        case JC4880_JOYPAD_SPI_CONTROL_START:
            gameplayMask |= JC4880_JOYPAD_MASK_START;
            break;
        case JC4880_JOYPAD_SPI_CONTROL_EXIT:
            actionMask |= JC4880_JOYPAD_ACTION_EXIT;
            break;
        case JC4880_JOYPAD_SPI_CONTROL_SAVE:
            actionMask |= JC4880_JOYPAD_ACTION_SAVE;
            break;
        case JC4880_JOYPAD_SPI_CONTROL_LOAD:
            actionMask |= JC4880_JOYPAD_ACTION_LOAD;
            break;
        case JC4880_JOYPAD_SPI_CONTROL_BUTTON_A:
            gameplayMask |= JC4880_JOYPAD_MASK_BUTTON_A;
            break;
        case JC4880_JOYPAD_SPI_CONTROL_BUTTON_B:
            gameplayMask |= JC4880_JOYPAD_MASK_BUTTON_B;
            break;
        case JC4880_JOYPAD_SPI_CONTROL_BUTTON_C:
            gameplayMask |= JC4880_JOYPAD_MASK_BUTTON_C;
            break;
        case JC4880_JOYPAD_SPI_CONTROL_BUTTON_Y:
            gameplayMask |= JC4880_JOYPAD_MASK_BUTTON_Y;
            break;
        default:
            break;
    }
}

void logMcpControlTransitionsLocked(uint16_t control_mask)
{
    const uint16_t changed_mask = static_cast<uint16_t>(control_mask ^ s_previousMcpControlMask);
    if (changed_mask == 0) {
        return;
    }

    for (size_t index = 0; index < kManualControlLogLabels.size(); ++index) {
        const uint16_t bit = static_cast<uint16_t>(1u << index);
        if ((changed_mask & bit) == 0) {
            continue;
        }

        ESP_LOGI(kTag,
                 "MCP23017 %s: %s",
                 ((control_mask & bit) != 0) ? "press" : "release",
                 kManualControlLogLabels[index]);
    }

    s_previousMcpControlMask = control_mask;
}

void sampleResistiveStateLocked(uint32_t &gameplayMask, uint32_t &actionMask)
{
    std::array<int, kManualInterfaceGpioCount> raw_values = {4095, 4095};
    for (size_t ladder_index = 0; ladder_index < kManualInterfaceGpioCount; ++ladder_index) {
        const int8_t pin = s_config.manual_resistive_gpio[ladder_index];
        if (pin < 0) {
            continue;
        }
        readAnalogPinLocked(pin, raw_values[ladder_index]);
    }

    for (size_t ladder_index = 0; ladder_index < kManualInterfaceGpioCount; ++ladder_index) {
        const int raw_value = raw_values[ladder_index];
        if (raw_value >= kResistiveIdleThreshold) {
            continue;
        }

        int best_control = -1;
        int best_delta = std::numeric_limits<int>::max();
        int second_best_delta = std::numeric_limits<int>::max();
        for (size_t control_index = 0; control_index < JC4880_JOYPAD_BUTTON_CONTROL_COUNT; ++control_index) {
            const int8_t binding = s_config.manual_resistive_button_binding[control_index];
            if ((binding < 0) || (resistiveLadderIndex(binding) != static_cast<int>(ladder_index))) {
                continue;
            }

            const int expected_raw = expectedResistiveRawForBinding(binding);
            const int delta = std::abs(raw_value - expected_raw);
            if (delta < best_delta) {
                second_best_delta = best_delta;
                best_delta = delta;
                best_control = static_cast<int>(control_index);
            } else if (delta < second_best_delta) {
                second_best_delta = delta;
            }
        }

        const int acceptance_window = std::max(120, second_best_delta / 2);
        if ((best_control >= 0) && (best_delta <= acceptance_window)) {
            applyManualButtonControl(static_cast<size_t>(best_control), gameplayMask, actionMask);
        }
    }
}

void sampleAnalogAxisStateLocked(uint32_t &gameplayMask)
{
    std::array<int, kManualInterfaceGpioCount> raw_values = {kAnalogAxisCenter, kAnalogAxisCenter};

    for (size_t axis_index = 0; axis_index < kManualInterfaceGpioCount; ++axis_index) {
        const int8_t pin = s_config.manual_resistive_gpio[axis_index];
        if (pin < 0) {
            continue;
        }

        readAnalogPinLocked(pin, raw_values[axis_index]);
    }

    const int y_value = raw_values[0];
    const int x_value = raw_values[1];
    uint32_t axis_mask = 0;

    if (y_value <= (kAnalogAxisCenter - kAnalogAxisDeadzone)) {
        axis_mask |= JC4880_JOYPAD_MASK_UP;
    } else if (y_value >= (kAnalogAxisCenter + kAnalogAxisDeadzone)) {
        axis_mask |= JC4880_JOYPAD_MASK_DOWN;
    }

    if (x_value <= (kAnalogAxisCenter - kAnalogAxisDeadzone)) {
        axis_mask |= JC4880_JOYPAD_MASK_LEFT;
    } else if (x_value >= (kAnalogAxisCenter + kAnalogAxisDeadzone)) {
        axis_mask |= JC4880_JOYPAD_MASK_RIGHT;
    }

    gameplayMask |= axis_mask;

    const bool value_changed = (std::abs(y_value - s_lastAnalogAxisY) >= 120) || (std::abs(x_value - s_lastAnalogAxisX) >= 120);
    const bool direction_changed = axis_mask != s_lastAnalogAxisMask;
    if (value_changed || direction_changed) {
        const char *y_dir = (axis_mask & JC4880_JOYPAD_MASK_UP) ? "UP" : ((axis_mask & JC4880_JOYPAD_MASK_DOWN) ? "DOWN" : "CENTER");
        const char *x_dir = (axis_mask & JC4880_JOYPAD_MASK_LEFT) ? "LEFT" : ((axis_mask & JC4880_JOYPAD_MASK_RIGHT) ? "RIGHT" : "CENTER");
        ESP_LOGI(kTag,
                 "Local analog axes: Y(GPIO %d)=%d [%s] X(GPIO %d)=%d [%s]",
                 static_cast<int>(s_config.manual_resistive_gpio[0]),
                 y_value,
                 y_dir,
                 static_cast<int>(s_config.manual_resistive_gpio[1]),
                 x_value,
                 x_dir);
        s_lastAnalogAxisY = y_value;
        s_lastAnalogAxisX = x_value;
        s_lastAnalogAxisMask = axis_mask;
    }
}

void sampleMcpStateLocked(uint32_t &gameplayMask, uint32_t &actionMask)
{
    sampleAnalogAxisStateLocked(gameplayMask);

    uint16_t mcp_state = 0xFFFF;
    if (!readMcpStateLocked(mcp_state)) {
        return;
    }

    uint16_t control_mask = 0;
    for (size_t control_index = 0; control_index < JC4880_JOYPAD_SPI_CONTROL_COUNT; ++control_index) {
        const int8_t pin = s_config.manual_mcp_button_pin[control_index];
        if ((pin < 0) || (pin >= static_cast<int8_t>(kMcpPinCount))) {
            continue;
        }

        if ((mcp_state & (1u << pin)) == 0) {
            control_mask = static_cast<uint16_t>(control_mask | (1u << control_index));
            applyManualSpiControl(control_index, gameplayMask, actionMask);
        }
    }

    logMcpControlTransitionsLocked(control_mask);
}

void sampleManualStateLocked(uint32_t &gameplayMask, uint32_t &actionMask)
{
    gameplayMask = 0;
    actionMask = 0;

    if (s_config.backend != JC4880_JOYPAD_BACKEND_MANUAL) {
        return;
    }

    if (s_config.manual_mode == JC4880_JOYPAD_MANUAL_MODE_SPI) {
        if (isPressed(s_config.manual_spi_gpio[JC4880_JOYPAD_SPI_CONTROL_UP])) {
            gameplayMask |= JC4880_JOYPAD_MASK_UP;
        }
        if (isPressed(s_config.manual_spi_gpio[JC4880_JOYPAD_SPI_CONTROL_DOWN])) {
            gameplayMask |= JC4880_JOYPAD_MASK_DOWN;
        }
        if (isPressed(s_config.manual_spi_gpio[JC4880_JOYPAD_SPI_CONTROL_LEFT])) {
            gameplayMask |= JC4880_JOYPAD_MASK_LEFT;
        }
        if (isPressed(s_config.manual_spi_gpio[JC4880_JOYPAD_SPI_CONTROL_RIGHT])) {
            gameplayMask |= JC4880_JOYPAD_MASK_RIGHT;
        }
        if (isPressed(s_config.manual_spi_gpio[JC4880_JOYPAD_SPI_CONTROL_START])) {
            gameplayMask |= JC4880_JOYPAD_MASK_START;
        }
        if (isPressed(s_config.manual_spi_gpio[JC4880_JOYPAD_SPI_CONTROL_BUTTON_A])) {
            gameplayMask |= JC4880_JOYPAD_MASK_BUTTON_A;
        }
        if (isPressed(s_config.manual_spi_gpio[JC4880_JOYPAD_SPI_CONTROL_BUTTON_B])) {
            gameplayMask |= JC4880_JOYPAD_MASK_BUTTON_B;
        }
        if (isPressed(s_config.manual_spi_gpio[JC4880_JOYPAD_SPI_CONTROL_BUTTON_C])) {
            gameplayMask |= JC4880_JOYPAD_MASK_BUTTON_C;
        }
        if (isPressed(s_config.manual_spi_gpio[JC4880_JOYPAD_SPI_CONTROL_BUTTON_Y])) {
            gameplayMask |= JC4880_JOYPAD_MASK_BUTTON_Y;
        }
        if (isPressed(s_config.manual_spi_gpio[JC4880_JOYPAD_SPI_CONTROL_EXIT])) {
            actionMask |= JC4880_JOYPAD_ACTION_EXIT;
        }
        if (isPressed(s_config.manual_spi_gpio[JC4880_JOYPAD_SPI_CONTROL_SAVE])) {
            actionMask |= JC4880_JOYPAD_ACTION_SAVE;
        }
        if (isPressed(s_config.manual_spi_gpio[JC4880_JOYPAD_SPI_CONTROL_LOAD])) {
            actionMask |= JC4880_JOYPAD_ACTION_LOAD;
        }
        return;
    }

    if (s_config.manual_mode == JC4880_JOYPAD_MANUAL_MODE_RESISTIVE) {
        sampleResistiveStateLocked(gameplayMask, actionMask);
        return;
    }

    sampleMcpStateLocked(gameplayMask, actionMask);
}

void manualPollTask(void *context)
{
    (void)context;

    for (;;) {
        {
            std::lock_guard<std::mutex> guard(s_mutex);
            sampleManualStateLocked(s_cachedManualGameplayMask, s_cachedManualActionMask);
        }

        vTaskDelay(kManualPollPeriod);
    }
}

void loadConfigLocked()
{
    s_config = makeDefaultConfig();

    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
        sanitizeConfig(s_config);
        configureManualPinsLocked();
        return;
    }

    uint8_t value = 0;
    if (nvs_get_u8(handle, kBackendKey, &value) == ESP_OK) {
        s_config.backend = value;
    }
    if (nvs_get_u8(handle, kBleEnabledKey, &value) == ESP_OK) {
        s_config.ble_enabled = value;
    }
    if (nvs_get_u8(handle, kBleDiscoveryKey, &value) == ESP_OK) {
        s_config.ble_discovery_enabled = value;
    }
    if (nvs_get_u8(handle, kManualModeKey, &value) == ESP_OK) {
        s_config.manual_mode = value;
    }

    size_t length = sizeof(s_config.ble_device_addr);
    nvs_get_str(handle, kBleDeviceKey, s_config.ble_device_addr, &length);
    loadBlob(handle, kBleRemapKey, s_config.ble_remap, sizeof(s_config.ble_remap));
    loadManualSpiPins(handle);
    loadBlob(handle, kManualResistiveKey, s_config.manual_resistive_gpio, sizeof(s_config.manual_resistive_gpio));
    loadManualResistiveBindings(handle);
    loadBlob(handle, kManualMcpGpioKey, s_config.manual_mcp_i2c_gpio, sizeof(s_config.manual_mcp_i2c_gpio));
    loadManualMcpPins(handle);
    loadCalibrationSlotsLocked(handle);

    nvs_close(handle);
    sanitizeConfig(s_config);
    configureManualPinsLocked();
}

bool saveConfigLocked()
{
    sanitizeConfig(s_config);

    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    bool ok = true;
    ok = ok && (nvs_set_u8(handle, kBackendKey, s_config.backend) == ESP_OK);
    ok = ok && (nvs_set_u8(handle, kBleEnabledKey, s_config.ble_enabled) == ESP_OK);
    ok = ok && (nvs_set_u8(handle, kBleDiscoveryKey, s_config.ble_discovery_enabled) == ESP_OK);
    ok = ok && (nvs_set_u8(handle, kManualModeKey, s_config.manual_mode) == ESP_OK);
    ok = ok && (nvs_set_str(handle, kBleDeviceKey, s_config.ble_device_addr) == ESP_OK);
    ok = ok && (nvs_set_blob(handle, kBleRemapKey, s_config.ble_remap, sizeof(s_config.ble_remap)) == ESP_OK);
    ok = ok && (nvs_set_blob(handle, kManualSpiKey, s_config.manual_spi_gpio, sizeof(s_config.manual_spi_gpio)) == ESP_OK);
    ok = ok && (nvs_set_blob(handle, kManualResistiveKey, s_config.manual_resistive_gpio, sizeof(s_config.manual_resistive_gpio)) == ESP_OK);
    ok = ok && (nvs_set_blob(handle, kManualResistiveButtonsKey, s_config.manual_resistive_button_binding, sizeof(s_config.manual_resistive_button_binding)) == ESP_OK);
    ok = ok && (nvs_set_blob(handle, kManualMcpGpioKey, s_config.manual_mcp_i2c_gpio, sizeof(s_config.manual_mcp_i2c_gpio)) == ESP_OK);
    ok = ok && (nvs_set_blob(handle, kManualMcpButtonsKey, s_config.manual_mcp_button_pin, sizeof(s_config.manual_mcp_button_pin)) == ESP_OK);
    ok = ok && saveCalibrationSlotsLocked(handle);
    ok = ok && (nvs_commit(handle) == ESP_OK);

    nvs_close(handle);
    configureManualPinsLocked();
    return ok;
}
} // namespace

extern "C" void jc4880_joypad_init(void)
{
    std::lock_guard<std::mutex> guard(s_mutex);
    if (s_initialized) {
        return;
    }

    s_lastConfiguredPins.fill(-1);
    loadConfigLocked();
    s_cachedManualGameplayMask = 0;
    s_cachedManualActionMask = 0;
    if (xTaskCreate(manualPollTask, "JoypadPoll", kManualPollTaskStack, nullptr, kManualPollTaskPriority, &s_manualPollTask) != pdPASS) {
        ESP_LOGW(kTag, "Failed to start manual joypad poll task");
        s_manualPollTask = nullptr;
    }
    s_initialized = true;
}

extern "C" bool jc4880_joypad_get_config(jc4880_joypad_config_t *out_config)
{
    if (out_config == nullptr) {
        return false;
    }

    jc4880_joypad_init();
    std::lock_guard<std::mutex> guard(s_mutex);
    *out_config = s_config;
    return true;
}

extern "C" bool jc4880_joypad_set_config(const jc4880_joypad_config_t *config)
{
    if (config == nullptr) {
        return false;
    }

    jc4880_joypad_init();
    jc4880_joypad_config_t snapshot = {};
    jc4880_joypad_config_changed_callback_t callback = nullptr;
    void *callbackContext = nullptr;
    bool saved = false;

    {
        std::lock_guard<std::mutex> guard(s_mutex);
        s_config = *config;
        saved = saveConfigLocked();
        if (saved) {
            snapshot = s_config;
            callback = s_configChangedCallback;
            callbackContext = s_configChangedCallbackContext;
        }
    }

    if (saved && (callback != nullptr)) {
        callback(&snapshot, callbackContext);
    }

    return saved;
}

extern "C" void jc4880_joypad_register_config_changed_callback(jc4880_joypad_config_changed_callback_t callback, void *context)
{
    jc4880_joypad_init();
    std::lock_guard<std::mutex> guard(s_mutex);
    s_configChangedCallback = callback;
    s_configChangedCallbackContext = context;
}

extern "C" bool jc4880_joypad_get_ble_status(bool *connected, char *device_name, size_t device_name_size)
{
    jc4880_joypad_init();
    std::lock_guard<std::mutex> guard(s_mutex);

    if (connected != nullptr) {
        *connected = s_bleConnected;
    }
    if ((device_name != nullptr) && (device_name_size > 0)) {
        std::strncpy(device_name, s_bleDeviceName, device_name_size - 1);
        device_name[device_name_size - 1] = '\0';
    }
    return true;
}

extern "C" bool jc4880_joypad_get_ble_report_state(jc4880_joypad_ble_report_state_t *out_report)
{
    if (out_report == nullptr) {
        return false;
    }

    jc4880_joypad_init();
    std::lock_guard<std::mutex> guard(s_mutex);

    jc4880_joypad_ble_report_state_t report = {};
    report.connected = s_bleConnected ? 1 : 0;
    report.scanning = s_bleScanning ? 1 : 0;
    report.calibration_active = s_bleCalibrationCapture.active ? 1 : 0;
    report.calibration_available = s_bleCalibrationLoaded ? 1 : 0;
    report.raw_mask = s_bleRawMask;
    report.raw_axis_x = s_bleRawAxisX;
    report.raw_axis_y = s_bleRawAxisY;
    report.raw_axis_rx = s_bleRawAxisRx;
    report.raw_axis_ry = s_bleRawAxisRy;
    report.raw_brake = s_bleRawBrake;
    report.raw_throttle = s_bleRawThrottle;
    if (s_bleCalibrationCapture.active) {
        buildCalibrationPreviewLocked(report.axis_x,
                                      report.axis_y,
                                      report.axis_rx,
                                      report.axis_ry,
                                      report.brake,
                                      report.throttle);
    } else {
        report.axis_x = s_bleAxisX;
        report.axis_y = s_bleAxisY;
        report.axis_rx = s_bleAxisRx;
        report.axis_ry = s_bleAxisRy;
        report.brake = s_bleBrake;
        report.throttle = s_bleThrottle;
    }
    std::memcpy(report.device_addr, s_bleDeviceAddr, sizeof(report.device_addr));
    std::memcpy(report.device_name, s_bleDeviceName, sizeof(report.device_name));
    *out_report = report;
    return true;
}

extern "C" bool jc4880_joypad_get_manual_report_state(jc4880_joypad_manual_report_state_t *out_report)
{
    if (out_report == nullptr) {
        return false;
    }

    jc4880_joypad_init();
    std::lock_guard<std::mutex> guard(s_mutex);

    jc4880_joypad_manual_report_state_t report = {};
    report.active = (s_config.backend == JC4880_JOYPAD_BACKEND_MANUAL) ? 1 : 0;
    report.manual_mode = s_config.manual_mode;
    report.axis_y_raw = static_cast<uint16_t>(std::max(0, std::min(4095, s_lastAnalogAxisY)));
    report.axis_x_raw = static_cast<uint16_t>(std::max(0, std::min(4095, s_lastAnalogAxisX)));
    if (report.active != 0) {
        report.gameplay_mask = s_cachedManualGameplayMask;
        report.action_mask = s_cachedManualActionMask;
    }
    *out_report = report;
    return true;
}

extern "C" uint32_t jc4880_joypad_apply_ble_remap(uint32_t raw_mask, const jc4880_joypad_config_t *config)
{
    if (config == nullptr) {
        return raw_mask;
    }

    const uint32_t sourceMasks[JC4880_JOYPAD_BLE_CONTROL_COUNT] = {
        JC4880_JOYPAD_MASK_UP,
        JC4880_JOYPAD_MASK_DOWN,
        JC4880_JOYPAD_MASK_LEFT,
        JC4880_JOYPAD_MASK_RIGHT,
        JC4880_JOYPAD_MASK_BUTTON_A,
        JC4880_JOYPAD_MASK_BUTTON_B,
        JC4880_JOYPAD_MASK_BUTTON_C,
        JC4880_JOYPAD_MASK_START,
        JC4880_JOYPAD_MASK_BUTTON_Y,
        JC4880_JOYPAD_MASK_SHOULDER_L,
        JC4880_JOYPAD_MASK_SHOULDER_R,
        JC4880_JOYPAD_MASK_TRIGGER_L,
        JC4880_JOYPAD_MASK_TRIGGER_R,
        JC4880_JOYPAD_MASK_SELECT,
        JC4880_JOYPAD_MASK_SYSTEM,
        JC4880_JOYPAD_MASK_CAPTURE,
        JC4880_JOYPAD_MASK_THUMB_L,
        JC4880_JOYPAD_MASK_THUMB_R,
        JC4880_JOYPAD_MASK_STICK_L_UP,
        JC4880_JOYPAD_MASK_STICK_L_DOWN,
        JC4880_JOYPAD_MASK_STICK_L_LEFT,
        JC4880_JOYPAD_MASK_STICK_L_RIGHT,
        JC4880_JOYPAD_MASK_STICK_R_UP,
        JC4880_JOYPAD_MASK_STICK_R_DOWN,
        JC4880_JOYPAD_MASK_STICK_R_LEFT,
        JC4880_JOYPAD_MASK_STICK_R_RIGHT,
    };

    uint32_t remapped = 0;
    for (size_t index = 0; index < JC4880_JOYPAD_BLE_CONTROL_COUNT; ++index) {
        if ((raw_mask & sourceMasks[index]) == 0) {
            continue;
        }
        remapped |= mapTargetToGameplayMask(config->ble_remap[index]);
    }
    return remapped;
}

extern "C" void jc4880_joypad_set_ble_report(bool connected,
                                              bool scanning,
                                              const char *device_name,
                                              const char *device_addr,
                                              uint32_t raw_mask,
                                              int16_t axis_x,
                                              int16_t axis_y,
                                              int16_t axis_rx,
                                              int16_t axis_ry,
                                              uint16_t brake,
                                              uint16_t throttle)
{
    jc4880_joypad_init();
    std::lock_guard<std::mutex> guard(s_mutex);

    if ((device_addr != nullptr) && (std::strncmp(s_bleDeviceAddr, device_addr, sizeof(s_bleDeviceAddr)) != 0)) {
        updateCalibrationLoadedStateLocked(device_addr);
    }

    s_bleRawAxisX = axis_x;
    s_bleRawAxisY = axis_y;
    s_bleRawAxisRx = axis_rx;
    s_bleRawAxisRy = axis_ry;
    s_bleRawBrake = brake;
    s_bleRawThrottle = throttle;
    updateCalibrationCaptureLocked(axis_x, axis_y, axis_rx, axis_ry, brake, throttle);
    applyCalibrationLocked(axis_x, axis_y, axis_rx, axis_ry, brake, throttle);

    s_bleConnected = connected;
    s_bleScanning = scanning;
    if (device_name != nullptr) {
        std::strncpy(s_bleDeviceName, device_name, sizeof(s_bleDeviceName) - 1);
        s_bleDeviceName[sizeof(s_bleDeviceName) - 1] = '\0';
    } else {
        s_bleDeviceName[0] = '\0';
    }
    if (device_addr != nullptr) {
        std::strncpy(s_bleDeviceAddr, device_addr, sizeof(s_bleDeviceAddr) - 1);
        s_bleDeviceAddr[sizeof(s_bleDeviceAddr) - 1] = '\0';
    } else {
        s_bleDeviceAddr[0] = '\0';
    }

    s_bleRawMask = raw_mask;
    s_bleAxisX = axis_x;
    s_bleAxisY = axis_y;
    s_bleAxisRx = axis_rx;
    s_bleAxisRy = axis_ry;
    s_bleBrake = brake;
    s_bleThrottle = throttle;
    s_bleGameplayMask = jc4880_joypad_apply_ble_remap(raw_mask, &s_config);
    s_bleActionMask = 0;
    const uint32_t sourceMasks[JC4880_JOYPAD_BLE_CONTROL_COUNT] = {
        JC4880_JOYPAD_MASK_UP,
        JC4880_JOYPAD_MASK_DOWN,
        JC4880_JOYPAD_MASK_LEFT,
        JC4880_JOYPAD_MASK_RIGHT,
        JC4880_JOYPAD_MASK_BUTTON_A,
        JC4880_JOYPAD_MASK_BUTTON_B,
        JC4880_JOYPAD_MASK_BUTTON_C,
        JC4880_JOYPAD_MASK_START,
        JC4880_JOYPAD_MASK_BUTTON_Y,
        JC4880_JOYPAD_MASK_SHOULDER_L,
        JC4880_JOYPAD_MASK_SHOULDER_R,
        JC4880_JOYPAD_MASK_TRIGGER_L,
        JC4880_JOYPAD_MASK_TRIGGER_R,
        JC4880_JOYPAD_MASK_SELECT,
        JC4880_JOYPAD_MASK_SYSTEM,
        JC4880_JOYPAD_MASK_CAPTURE,
        JC4880_JOYPAD_MASK_THUMB_L,
        JC4880_JOYPAD_MASK_THUMB_R,
        JC4880_JOYPAD_MASK_STICK_L_UP,
        JC4880_JOYPAD_MASK_STICK_L_DOWN,
        JC4880_JOYPAD_MASK_STICK_L_LEFT,
        JC4880_JOYPAD_MASK_STICK_L_RIGHT,
        JC4880_JOYPAD_MASK_STICK_R_UP,
        JC4880_JOYPAD_MASK_STICK_R_DOWN,
        JC4880_JOYPAD_MASK_STICK_R_LEFT,
        JC4880_JOYPAD_MASK_STICK_R_RIGHT,
    };
    for (size_t index = 0; index < JC4880_JOYPAD_BLE_CONTROL_COUNT; ++index) {
        if ((raw_mask & sourceMasks[index]) == 0) {
            continue;
        }
        s_bleActionMask |= mapTargetToActionMask(s_config.ble_remap[index]);
    }
}

extern "C" bool jc4880_joypad_begin_ble_calibration(void)
{
    jc4880_joypad_init();
    std::lock_guard<std::mutex> guard(s_mutex);
    if (!s_bleConnected || (s_bleDeviceAddr[0] == '\0')) {
        return false;
    }

    s_bleCalibrationCapture = {};
    s_bleCalibrationCapture.active = true;
    snprintf(s_bleCalibrationCapture.device_addr,
             sizeof(s_bleCalibrationCapture.device_addr),
             "%s",
             s_bleDeviceAddr);
    snprintf(s_bleCalibrationCapture.device_name,
             sizeof(s_bleCalibrationCapture.device_name),
             "%s",
             s_bleDeviceName);
    s_bleCalibrationCapture.axis_center[0] = s_bleRawAxisX;
    s_bleCalibrationCapture.axis_center[1] = s_bleRawAxisY;
    s_bleCalibrationCapture.axis_center[2] = s_bleRawAxisRx;
    s_bleCalibrationCapture.axis_center[3] = s_bleRawAxisRy;
    for (size_t index = 0; index < 4; ++index) {
        s_bleCalibrationCapture.axis_min[index] = s_bleCalibrationCapture.axis_center[index];
        s_bleCalibrationCapture.axis_max[index] = s_bleCalibrationCapture.axis_center[index];
    }
    s_bleCalibrationCapture.pedal_min[0] = s_bleRawBrake;
    s_bleCalibrationCapture.pedal_min[1] = s_bleRawThrottle;
    s_bleCalibrationCapture.pedal_max[0] = s_bleRawBrake;
    s_bleCalibrationCapture.pedal_max[1] = s_bleRawThrottle;
    return true;
}

extern "C" bool jc4880_joypad_finish_ble_calibration(void)
{
    jc4880_joypad_init();
    std::lock_guard<std::mutex> guard(s_mutex);
    if (!s_bleCalibrationCapture.active || (s_bleCalibrationCapture.device_addr[0] == '\0')) {
        return false;
    }

    const int slot_index = allocateCalibrationSlotLocked(s_bleCalibrationCapture.device_addr);
    auto &slot = s_bleCalibrationSlots[static_cast<size_t>(slot_index)];
    slot = {};
    slot.valid = 1;
    slot.last_used_counter = ++s_bleCalibrationCounter;
    snprintf(slot.device_addr,
             sizeof(slot.device_addr),
             "%s",
             s_bleCalibrationCapture.device_addr);
    snprintf(slot.device_name,
             sizeof(slot.device_name),
             "%s",
             s_bleCalibrationCapture.device_name);
    for (size_t index = 0; index < 4; ++index) {
        slot.axis_center[index] = s_bleCalibrationCapture.axis_center[index];
        slot.axis_min[index] = s_bleCalibrationCapture.axis_min[index];
        slot.axis_max[index] = s_bleCalibrationCapture.axis_max[index];
        if (slot.axis_min[index] == slot.axis_max[index]) {
            slot.axis_min[index] = static_cast<int16_t>(slot.axis_center[index] - 1);
            slot.axis_max[index] = static_cast<int16_t>(slot.axis_center[index] + 1);
        }
    }
    for (size_t index = 0; index < 2; ++index) {
        slot.pedal_min[index] = s_bleCalibrationCapture.pedal_min[index];
        slot.pedal_max[index] = s_bleCalibrationCapture.pedal_max[index];
        if (slot.pedal_min[index] == slot.pedal_max[index]) {
            slot.pedal_max[index] = static_cast<uint16_t>(std::min<int32_t>(kAxisNormalizeRange, slot.pedal_min[index] + 1));
        }
    }

    nvs_handle_t handle = 0;
    const bool opened = (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) == ESP_OK);
    bool saved = false;
    if (opened) {
        saved = saveCalibrationSlotsLocked(handle) && (nvs_commit(handle) == ESP_OK);
        nvs_close(handle);
    }
    if (saved) {
        updateCalibrationLoadedStateLocked(slot.device_addr);
    }
    s_bleCalibrationCapture.active = false;
    return saved;
}

extern "C" void jc4880_joypad_cancel_ble_calibration(void)
{
    jc4880_joypad_init();
    std::lock_guard<std::mutex> guard(s_mutex);
    s_bleCalibrationCapture.active = false;
}

extern "C" bool jc4880_joypad_is_ble_calibration_active(void)
{
    jc4880_joypad_init();
    std::lock_guard<std::mutex> guard(s_mutex);
    return s_bleCalibrationCapture.active;
}

extern "C" bool jc4880_joypad_get_ble_calibration_slot(const char *device_addr, jc4880_joypad_ble_calibration_slot_t *out_slot)
{
    if ((device_addr == nullptr) || (out_slot == nullptr)) {
        return false;
    }

    jc4880_joypad_init();
    std::lock_guard<std::mutex> guard(s_mutex);
    const int slot_index = findCalibrationSlotLocked(device_addr);
    if (slot_index < 0) {
        return false;
    }

    *out_slot = s_bleCalibrationSlots[static_cast<size_t>(slot_index)];
    return true;
}

extern "C" uint32_t jc4880_joypad_get_input_mask(void)
{
    jc4880_joypad_init();
    std::lock_guard<std::mutex> guard(s_mutex);

    uint32_t gameplayMask = s_cachedManualGameplayMask;
    if (s_config.ble_enabled && (s_config.backend == JC4880_JOYPAD_BACKEND_BLE) && s_bleConnected) {
        gameplayMask |= s_bleGameplayMask;
    }
    return gameplayMask;
}

extern "C" uint32_t jc4880_joypad_consume_action_mask(void)
{
    jc4880_joypad_init();
    std::lock_guard<std::mutex> guard(s_mutex);

    uint32_t currentActionState = s_cachedManualActionMask;
    if (s_config.ble_enabled && (s_config.backend == JC4880_JOYPAD_BACKEND_BLE) && s_bleConnected) {
        currentActionState |= s_bleActionMask;
    }

    const uint32_t triggeredActions = currentActionState & ~s_previousActionState;
    s_previousActionState = currentActionState;
    return triggeredActions;
}