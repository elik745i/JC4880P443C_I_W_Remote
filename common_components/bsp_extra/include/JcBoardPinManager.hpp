#pragma once

#include <cstddef>
#include <cstdint>

#include "sdkconfig.h"

#define USE_LORA_E22_400M22S 1
#define USE_ETHERNET_RMII 0
#define USE_CSI_CAMERA 0

#if USE_LORA_E22_400M22S && USE_ETHERNET_RMII
#error "E22-400M22S LoRa cannot share GPIO ownership with Ethernet RMII"
#endif

#if USE_LORA_E22_400M22S && USE_CSI_CAMERA
#error "E22-400M22S LoRa cannot share GPIO ownership with CSI camera"
#endif

#if USE_LORA_E22_400M22S && defined(CONFIG_ETH_ENABLED) && CONFIG_ETH_ENABLED
#error "sdkconfig still enables Ethernet while the E22-400M22S LoRa profile reserves those GPIOs"
#endif

#if USE_LORA_E22_400M22S && defined(CONFIG_ETH_USE_ESP32_EMAC) && CONFIG_ETH_USE_ESP32_EMAC
#error "sdkconfig still enables ESP32 EMAC while the E22-400M22S LoRa profile reserves those GPIOs"
#endif

#if USE_LORA_E22_400M22S && defined(CONFIG_ETH_PHY_INTERFACE_RMII) && CONFIG_ETH_PHY_INTERFACE_RMII
#error "sdkconfig still enables RMII while the E22-400M22S LoRa profile reserves those GPIOs"
#endif

#if USE_LORA_E22_400M22S && defined(CONFIG_EXAMPLE_ENABLE_MIPI_CSI_CAM_SENSOR) && CONFIG_EXAMPLE_ENABLE_MIPI_CSI_CAM_SENSOR
#error "sdkconfig still enables the MIPI CSI camera example while the E22-400M22S LoRa profile reserves those GPIOs"
#endif

#if USE_LORA_E22_400M22S && defined(CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE) && CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE
#error "sdkconfig still enables MIPI CSI video while the E22-400M22S LoRa profile reserves those GPIOs"
#endif

namespace jc4880::board_pins {

enum class GpioOwner : uint8_t {
    None = 0,
    SharedI2c,
    LoRaE22,
    Imu,
    Haptics,
    Neopixel,
    DisplayAutorotate,
};

enum class BoardPinRole : uint8_t {
    Unknown = 0,
    SharedI2cSda,
    SharedI2cScl,
    LoRaNss,
    LoRaSck,
    LoRaMosi,
    LoRaMiso,
    LoRaRxEnable,
    LoRaTxEnable,
    LoRaDio1,
    LoRaBusy,
    LoRaReset,
    ImuI2cSda,
    ImuI2cScl,
    ImuSpiSck,
    ImuSpiMosi,
    ImuSpiMiso,
    ImuSpiCs,
    ImuInterrupt,
    ImuReset,
    ImuDataReady,
    HapticOutput,
    NeopixelData,
    DisplayAutorotateSda,
    DisplayAutorotateScl,
};

struct GpioReservation {
    int8_t gpio;
    GpioOwner owner;
    BoardPinRole role;
    bool active;
    bool fixed;
};

inline constexpr int8_t kJp1AssignableGpios[] = {28, 29, 30, 31, 32, 33, 34, 35, 49, 50, 51, 52};
inline constexpr size_t kJp1AssignableGpioCount = sizeof(kJp1AssignableGpios) / sizeof(kJp1AssignableGpios[0]);

inline constexpr int8_t kSharedI2cSdaGpio = 7;
inline constexpr int8_t kSharedI2cSclGpio = 8;
inline constexpr int8_t kDefaultImuI2cSdaGpio = kSharedI2cSdaGpio;
inline constexpr int8_t kDefaultImuI2cSclGpio = kSharedI2cSclGpio;

inline constexpr int8_t kLoRaSpiNssGpio = 29;
inline constexpr int8_t kLoRaSpiSckGpio = 30;
inline constexpr int8_t kLoRaSpiMosiGpio = 31;
inline constexpr int8_t kLoRaSpiMisoGpio = 33;
inline constexpr int8_t kLoRaRxEnableGpio = 34;
inline constexpr int8_t kLoRaTxEnableGpio = 35;
inline constexpr int8_t kLoRaDio1Gpio = 50;
inline constexpr int8_t kLoRaBusyGpio = 51;
inline constexpr int8_t kLoRaResetGpio = 52;

inline constexpr GpioReservation kDefaultReservations[] = {
    {kSharedI2cSdaGpio, GpioOwner::SharedI2c, BoardPinRole::SharedI2cSda, true, true},
    {kSharedI2cSclGpio, GpioOwner::SharedI2c, BoardPinRole::SharedI2cScl, true, true},
    {kLoRaSpiNssGpio, GpioOwner::LoRaE22, BoardPinRole::LoRaNss, USE_LORA_E22_400M22S != 0, true},
    {kLoRaSpiSckGpio, GpioOwner::LoRaE22, BoardPinRole::LoRaSck, USE_LORA_E22_400M22S != 0, true},
    {kLoRaSpiMosiGpio, GpioOwner::LoRaE22, BoardPinRole::LoRaMosi, USE_LORA_E22_400M22S != 0, true},
    {kLoRaSpiMisoGpio, GpioOwner::LoRaE22, BoardPinRole::LoRaMiso, USE_LORA_E22_400M22S != 0, true},
    {kLoRaRxEnableGpio, GpioOwner::LoRaE22, BoardPinRole::LoRaRxEnable, USE_LORA_E22_400M22S != 0, true},
    {kLoRaTxEnableGpio, GpioOwner::LoRaE22, BoardPinRole::LoRaTxEnable, USE_LORA_E22_400M22S != 0, true},
    {kLoRaDio1Gpio, GpioOwner::LoRaE22, BoardPinRole::LoRaDio1, USE_LORA_E22_400M22S != 0, true},
    {kLoRaBusyGpio, GpioOwner::LoRaE22, BoardPinRole::LoRaBusy, USE_LORA_E22_400M22S != 0, true},
    {kLoRaResetGpio, GpioOwner::LoRaE22, BoardPinRole::LoRaReset, USE_LORA_E22_400M22S != 0, true},
};

inline constexpr size_t kDefaultReservationCount = sizeof(kDefaultReservations) / sizeof(kDefaultReservations[0]);

bool is_jp1_assignable_gpio(int gpio);
bool is_lora_e22_reserved_gpio(int gpio);
bool is_gpio_reserved_for_active_owner(int gpio);
GpioReservation describe_gpio_owner(int gpio);
const char *gpio_owner_name(GpioOwner owner);
const char *board_pin_role_name(BoardPinRole role);
void format_gpio_owner_diagnostics(char *buffer, size_t buffer_size);

} // namespace jc4880::board_pins