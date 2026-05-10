#pragma once

#include <cstdint>

#include "JcBoardPinManager.hpp"

namespace jc4880::lora_mesh::pin_profile {

inline constexpr int8_t kSpiMisoGpio = jc4880::board_pins::kLoRaSpiMisoGpio;
inline constexpr int8_t kSpiMosiGpio = jc4880::board_pins::kLoRaSpiMosiGpio;
inline constexpr int8_t kSpiSckGpio = jc4880::board_pins::kLoRaSpiSckGpio;
inline constexpr int8_t kSpiNssGpio = jc4880::board_pins::kLoRaSpiNssGpio;
inline constexpr int8_t kDio1Gpio = jc4880::board_pins::kLoRaDio1Gpio;
inline constexpr int8_t kBusyGpio = jc4880::board_pins::kLoRaBusyGpio;
inline constexpr int8_t kNrstGpio = jc4880::board_pins::kLoRaResetGpio;
inline constexpr int8_t kTxEnableGpio = jc4880::board_pins::kLoRaTxEnableGpio;
inline constexpr int8_t kRxEnableGpio = jc4880::board_pins::kLoRaRxEnableGpio;

constexpr bool is_reserved_gpio(int gpio)
{
    return jc4880::board_pins::is_lora_e22_reserved_gpio(gpio);
}

} // namespace jc4880::lora_mesh::pin_profile