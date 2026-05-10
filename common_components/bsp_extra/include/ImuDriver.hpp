#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "JcBoardPinManager.hpp"

namespace jc4880::imu {

enum class ImuModel : uint8_t {
    IMU_NONE = 0,
    BNO085,
    BNO080,
    BNO055,
    ICM20948,
    MPU9250,
    MPU9255,
    GY91_MPU9250_BMP280,
    MPU6050,
    MPU6500,
    MPU6886,
    BMI160,
    BMI270,
    BHI260AP,
    LSM6DS3,
    LSM6DSL,
    LSM6DSOX,
    LSM9DS1,
    LSM9DS0,
    LSM6DS3TRC_LIS3MDL,
    BMI270_BMM150,
    HMC5883L,
    QMC5883L,
    LIS3MDL,
    BMM150,
    ADXL345,
    ADIS16500,
    ADIS16505,
    HW579_COMBO,
};

enum class ImuBusType : uint8_t {
    I2C = 0,
    SPI,
    UART,
};

enum class MountingOrientation : uint8_t {
    PortraitUp = 0,
    PortraitDown,
    LandscapeLeft,
    LandscapeRight,
    FlatFaceUp,
    FlatFaceDown,
};

struct ImuConfig {
    bool enabled = false;
    ImuModel model = ImuModel::IMU_NONE;
    ImuBusType busType = ImuBusType::I2C;
    int8_t i2cSda = jc4880::board_pins::kDefaultImuI2cSdaGpio;
    int8_t i2cScl = jc4880::board_pins::kDefaultImuI2cSclGpio;
    uint8_t i2cAddress = 0x68;
    int8_t spiSck = -1;
    int8_t spiMosi = -1;
    int8_t spiMiso = -1;
    int8_t spiCs = -1;
    int8_t intPin = -1;
    int8_t rstPin = -1;
    int8_t drdyPin = -1;
    std::array<uint8_t, 3> axisSwap = {0, 1, 2};
    bool invertX = false;
    bool invertY = false;
    bool invertZ = false;
    MountingOrientation mountingOrientation = MountingOrientation::PortraitUp;
    float headingOffset = 0.0f;
    bool useFusion = true;
    uint16_t sampleRateHz = 100;
};

struct ImuSample {
    bool valid = false;
    bool hasAccel = false;
    bool hasGyro = false;
    bool hasMag = false;
    bool hasFusion = false;
    bool hasBarometer = false;
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    float gx = 0.0f;
    float gy = 0.0f;
    float gz = 0.0f;
    float mx = 0.0f;
    float my = 0.0f;
    float mz = 0.0f;
    float roll = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
    float qw = 1.0f;
    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    float temperature = 0.0f;
    float pressure = 0.0f;
    float altitude = 0.0f;
    uint8_t calibrationSys = 0;
    uint8_t calibrationAccel = 0;
    uint8_t calibrationGyro = 0;
    uint8_t calibrationMag = 0;
};

struct ImuModelInfo {
    ImuModel model;
    const char *label;
    bool supportsI2c;
    bool supportsSpi;
    bool supportsUart;
    const char *powerHint;
    bool hasMagnetometer;
    bool hasBarometer;
    bool hasFusion;
    bool placeholderOnly;
};

class ImuDriver {
public:
    virtual ~ImuDriver() = default;
    virtual bool begin(const ImuConfig &cfg) = 0;
    virtual bool read(ImuSample &out) = 0;
    virtual bool calibrateStep() = 0;
    virtual bool saveCalibration() = 0;
    virtual bool loadCalibration() = 0;
    virtual const char *name() = 0;
};

const ImuModelInfo *get_imu_model_catalog(size_t *count_out = nullptr);
const ImuModelInfo *find_imu_model_info(ImuModel model);
bool imu_model_supports_bus(ImuModel model, ImuBusType bus_type);
const char *imu_bus_type_label(ImuBusType bus_type);
const char *imu_mounting_orientation_label(MountingOrientation orientation);
const char *imu_model_label(ImuModel model);
ImuConfig make_default_imu_config();
void apply_axis_transform(const ImuConfig &cfg, ImuSample &sample);

} // namespace jc4880::imu