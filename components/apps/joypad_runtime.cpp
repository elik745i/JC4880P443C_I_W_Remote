#include "joypad_runtime.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

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
constexpr const char *kManualButtonsKey = "joy_man_btn";
constexpr const char *kBleCalibrationSlotsKey = "joy_ble_cal";
constexpr const char *kBleCalibrationCounterKey = "joy_ble_caln";
constexpr size_t kBleCalibrationSlotCount = 20;
constexpr int32_t kAxisNormalizeRange = 1024;
constexpr int32_t kAxisNormalizeHalfRange = kAxisNormalizeRange / 2;

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
std::array<int8_t, JC4880_JOYPAD_SPI_CONTROL_COUNT + 2 + JC4880_JOYPAD_BUTTON_CONTROL_COUNT> s_lastConfiguredPins = {};

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
    for (size_t index = 0; index < 2; ++index) {
        config.manual_resistive_gpio[index] = -1;
    }
    for (size_t index = 0; index < JC4880_JOYPAD_BUTTON_CONTROL_COUNT; ++index) {
        config.manual_button_gpio[index] = -1;
    }
    config.ble_device_addr[0] = '\0';
    return config;
}

jc4880_joypad_config_t s_config = makeDefaultConfig();

bool isAllowedPin(int8_t pin)
{
    return std::find(kAllowedPins.begin(), kAllowedPins.end(), pin) != kAllowedPins.end();
}

void sanitizeConfig(jc4880_joypad_config_t &config)
{
    if ((config.backend != JC4880_JOYPAD_BACKEND_DISABLED) &&
        (config.backend != JC4880_JOYPAD_BACKEND_BLE) &&
        (config.backend != JC4880_JOYPAD_BACKEND_MANUAL)) {
        config.backend = JC4880_JOYPAD_BACKEND_DISABLED;
    }

    if ((config.manual_mode != JC4880_JOYPAD_MANUAL_MODE_SPI) &&
        (config.manual_mode != JC4880_JOYPAD_MANUAL_MODE_RESISTIVE)) {
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
        if (!isAllowedPin(config.manual_resistive_gpio[index])) {
            config.manual_resistive_gpio[index] = -1;
        }
    }
    for (size_t index = 0; index < JC4880_JOYPAD_BUTTON_CONTROL_COUNT; ++index) {
        if (!isAllowedPin(config.manual_button_gpio[index])) {
            config.manual_button_gpio[index] = -1;
        }
    }

    config.ble_device_addr[sizeof(config.ble_device_addr) - 1] = '\0';
}

bool loadBlob(nvs_handle_t handle, const char *key, void *buffer, size_t size)
{
    size_t actualSize = size;
    return (nvs_get_blob(handle, key, buffer, &actualSize) == ESP_OK) && (actualSize == size);
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

void configureManualPinsLocked()
{
    std::array<int8_t, JC4880_JOYPAD_SPI_CONTROL_COUNT + 2 + JC4880_JOYPAD_BUTTON_CONTROL_COUNT> currentPins = {};
    size_t nextIndex = 0;
    for (int8_t pin : s_config.manual_spi_gpio) {
        currentPins[nextIndex++] = pin;
    }
    for (int8_t pin : s_config.manual_resistive_gpio) {
        currentPins[nextIndex++] = pin;
    }
    for (int8_t pin : s_config.manual_button_gpio) {
        currentPins[nextIndex++] = pin;
    }

    if (currentPins == s_lastConfiguredPins) {
        return;
    }

    for (int8_t pin : currentPins) {
        configurePinInput(pin);
    }
    s_lastConfiguredPins = currentPins;
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

    if (isPressed(s_config.manual_button_gpio[JC4880_JOYPAD_BUTTON_CONTROL_START])) {
        gameplayMask |= JC4880_JOYPAD_MASK_START;
    }
    if (isPressed(s_config.manual_button_gpio[JC4880_JOYPAD_BUTTON_CONTROL_BUTTON_A])) {
        gameplayMask |= JC4880_JOYPAD_MASK_BUTTON_A;
    }
    if (isPressed(s_config.manual_button_gpio[JC4880_JOYPAD_BUTTON_CONTROL_BUTTON_B])) {
        gameplayMask |= JC4880_JOYPAD_MASK_BUTTON_B;
    }
    if (isPressed(s_config.manual_button_gpio[JC4880_JOYPAD_BUTTON_CONTROL_BUTTON_C])) {
        gameplayMask |= JC4880_JOYPAD_MASK_BUTTON_C;
    }
    if (isPressed(s_config.manual_button_gpio[JC4880_JOYPAD_BUTTON_CONTROL_EXIT])) {
        actionMask |= JC4880_JOYPAD_ACTION_EXIT;
    }
    if (isPressed(s_config.manual_button_gpio[JC4880_JOYPAD_BUTTON_CONTROL_SAVE])) {
        actionMask |= JC4880_JOYPAD_ACTION_SAVE;
    }
    if (isPressed(s_config.manual_button_gpio[JC4880_JOYPAD_BUTTON_CONTROL_LOAD])) {
        actionMask |= JC4880_JOYPAD_ACTION_LOAD;
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
    loadBlob(handle, kManualSpiKey, s_config.manual_spi_gpio, sizeof(s_config.manual_spi_gpio));
    loadBlob(handle, kManualResistiveKey, s_config.manual_resistive_gpio, sizeof(s_config.manual_resistive_gpio));
    loadBlob(handle, kManualButtonsKey, s_config.manual_button_gpio, sizeof(s_config.manual_button_gpio));
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
    ok = ok && (nvs_set_blob(handle, kManualButtonsKey, s_config.manual_button_gpio, sizeof(s_config.manual_button_gpio)) == ESP_OK);
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

    uint32_t manualGameplayMask = 0;
    uint32_t manualActionMask = 0;
    sampleManualStateLocked(manualGameplayMask, manualActionMask);

    uint32_t gameplayMask = manualGameplayMask;
    if (s_config.ble_enabled && (s_config.backend == JC4880_JOYPAD_BACKEND_BLE) && s_bleConnected) {
        gameplayMask |= s_bleGameplayMask;
    }
    return gameplayMask;
}

extern "C" uint32_t jc4880_joypad_consume_action_mask(void)
{
    jc4880_joypad_init();
    std::lock_guard<std::mutex> guard(s_mutex);

    uint32_t manualGameplayMask = 0;
    uint32_t manualActionMask = 0;
    sampleManualStateLocked(manualGameplayMask, manualActionMask);

    uint32_t currentActionState = manualActionMask;
    if (s_config.ble_enabled && (s_config.backend == JC4880_JOYPAD_BACKEND_BLE) && s_bleConnected) {
        currentActionState |= s_bleActionMask;
    }

    const uint32_t triggeredActions = currentActionState & ~s_previousActionState;
    s_previousActionState = currentActionState;
    return triggeredActions;
}