#pragma once

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

namespace jc4880::lora_mesh::pin_profile {

inline constexpr int8_t kSpiMisoGpio = 33;
inline constexpr int8_t kSpiMosiGpio = 31;
inline constexpr int8_t kSpiSckGpio = 30;
inline constexpr int8_t kSpiNssGpio = 29;
inline constexpr int8_t kDio1Gpio = 50;
inline constexpr int8_t kBusyGpio = 51;
inline constexpr int8_t kNrstGpio = 52;
inline constexpr int8_t kTxEnableGpio = 35;
inline constexpr int8_t kRxEnableGpio = 34;

constexpr bool is_reserved_gpio(int gpio)
{
    switch (gpio) {
        case kSpiMisoGpio:
        case kSpiMosiGpio:
        case kSpiSckGpio:
        case kSpiNssGpio:
        case kDio1Gpio:
        case kBusyGpio:
        case kNrstGpio:
        case kTxEnableGpio:
        case kRxEnableGpio:
            return true;
        default:
            return false;
    }
}

} // namespace jc4880::lora_mesh::pin_profile