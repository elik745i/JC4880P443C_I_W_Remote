#include "JcBoardPinManager.hpp"

#include <cstdio>
#include <cstring>

namespace jc4880::board_pins {

namespace {

constexpr GpioReservation kUnownedReservation = {-1, GpioOwner::None, BoardPinRole::Unknown, false, false};

} // namespace

bool is_jp1_assignable_gpio(int gpio)
{
    for (size_t index = 0; index < kJp1AssignableGpioCount; ++index) {
        if (kJp1AssignableGpios[index] == gpio) {
            return true;
        }
    }
    return false;
}

bool is_lora_e22_reserved_gpio(int gpio)
{
    for (size_t index = 0; index < kDefaultReservationCount; ++index) {
        const GpioReservation &reservation = kDefaultReservations[index];
        if (reservation.active && (reservation.owner == GpioOwner::LoRaE22) && (reservation.gpio == gpio)) {
            return true;
        }
    }
    return false;
}

bool is_gpio_reserved_for_active_owner(int gpio)
{
    for (size_t index = 0; index < kDefaultReservationCount; ++index) {
        const GpioReservation &reservation = kDefaultReservations[index];
        if (reservation.active && (reservation.gpio == gpio)) {
            return true;
        }
    }
    return false;
}

GpioReservation describe_gpio_owner(int gpio)
{
    for (size_t index = 0; index < kDefaultReservationCount; ++index) {
        const GpioReservation &reservation = kDefaultReservations[index];
        if (reservation.active && (reservation.gpio == gpio)) {
            return reservation;
        }
    }
    return kUnownedReservation;
}

const char *gpio_owner_name(GpioOwner owner)
{
    switch (owner) {
        case GpioOwner::SharedI2c:
            return "Shared I2C";
        case GpioOwner::LoRaE22:
            return "LoRa E22";
        case GpioOwner::Imu:
            return "IMU";
        case GpioOwner::Haptics:
            return "Haptics";
        case GpioOwner::Neopixel:
            return "NeoPixel";
        case GpioOwner::DisplayAutorotate:
            return "Display Auto-Rotate";
        case GpioOwner::None:
        default:
            return "Unowned";
    }
}

const char *board_pin_role_name(BoardPinRole role)
{
    switch (role) {
        case BoardPinRole::SharedI2cSda:
            return "ES_I2C_SDA";
        case BoardPinRole::SharedI2cScl:
            return "ES_I2C_SCL";
        case BoardPinRole::LoRaNss:
            return "LORA_NSS";
        case BoardPinRole::LoRaSck:
            return "LORA_SCK";
        case BoardPinRole::LoRaMosi:
            return "LORA_MOSI";
        case BoardPinRole::LoRaMiso:
            return "LORA_MISO";
        case BoardPinRole::LoRaRxEnable:
            return "LORA_RXEN";
        case BoardPinRole::LoRaTxEnable:
            return "LORA_TXEN";
        case BoardPinRole::LoRaDio1:
            return "LORA_DIO1";
        case BoardPinRole::LoRaBusy:
            return "LORA_BUSY";
        case BoardPinRole::LoRaReset:
            return "LORA_NRST";
        case BoardPinRole::ImuI2cSda:
            return "IMU_I2C_SDA";
        case BoardPinRole::ImuI2cScl:
            return "IMU_I2C_SCL";
        case BoardPinRole::ImuSpiSck:
            return "IMU_SPI_SCK";
        case BoardPinRole::ImuSpiMosi:
            return "IMU_SPI_MOSI";
        case BoardPinRole::ImuSpiMiso:
            return "IMU_SPI_MISO";
        case BoardPinRole::ImuSpiCs:
            return "IMU_SPI_CS";
        case BoardPinRole::ImuInterrupt:
            return "IMU_INT";
        case BoardPinRole::ImuReset:
            return "IMU_RST";
        case BoardPinRole::ImuDataReady:
            return "IMU_DRDY";
        case BoardPinRole::HapticOutput:
            return "HAPTIC";
        case BoardPinRole::NeopixelData:
            return "NEOPIXEL";
        case BoardPinRole::DisplayAutorotateSda:
            return "DISP_AUTO_SDA";
        case BoardPinRole::DisplayAutorotateScl:
            return "DISP_AUTO_SCL";
        case BoardPinRole::Unknown:
        default:
            return "UNKNOWN";
    }
}

void format_gpio_owner_diagnostics(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }

    buffer[0] = '\0';
    size_t offset = 0;
    for (size_t index = 0; index < kDefaultReservationCount; ++index) {
        const GpioReservation &reservation = kDefaultReservations[index];
        if (!reservation.active) {
            continue;
        }

        const int written = std::snprintf(buffer + offset,
                                          buffer_size - offset,
                                          "%sGPIO%d=%s/%s",
                                          offset == 0 ? "" : "; ",
                                          static_cast<int>(reservation.gpio),
                                          gpio_owner_name(reservation.owner),
                                          board_pin_role_name(reservation.role));
        if (written <= 0) {
            break;
        }
        const size_t written_size = static_cast<size_t>(written);
        if (written_size >= (buffer_size - offset)) {
            offset = buffer_size - 1;
            break;
        }
        offset += written_size;
    }

    buffer[buffer_size - 1] = '\0';
}

} // namespace jc4880::board_pins