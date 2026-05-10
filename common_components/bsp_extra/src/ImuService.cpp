#include "ImuService.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include "bsp/esp32_p4_function_ev_board.h"

namespace jc4880::imu {

namespace {

static constexpr char kTag[] = "imu_service";
static constexpr char kNvsNamespace[] = "imu";
static constexpr char kConfigBlobKey[] = "cfg";
static constexpr char kMpuCalBlobKey[] = "mpu_cal";
static constexpr uint32_t kI2cFrequencyHz = 400000;
static constexpr uint8_t kMpu6050WhoAmIRegister = 0x75;
static constexpr uint8_t kMpu6050WhoAmIValue = 0x68;
static constexpr uint8_t kMpu6050PwrMgmt1Register = 0x6B;
static constexpr uint8_t kMpu6050ConfigRegister = 0x1A;
static constexpr uint8_t kMpu6050GyroConfigRegister = 0x1B;
static constexpr uint8_t kMpu6050AccelConfigRegister = 0x1C;
static constexpr uint8_t kMpu6050DataRegister = 0x3B;
static constexpr uint8_t kBmi160ChipIdRegister = 0x00;
static constexpr uint8_t kBmi160ChipIdValue = 0xD1;
static constexpr uint8_t kBmi160GyroDataRegister = 0x0C;
static constexpr uint8_t kBmi160AccelDataRegister = 0x12;
static constexpr uint8_t kBmi160TemperatureRegister = 0x20;
static constexpr uint8_t kBmi160AccelConfigRegister = 0x40;
static constexpr uint8_t kBmi160AccelRangeRegister = 0x41;
static constexpr uint8_t kBmi160GyroConfigRegister = 0x42;
static constexpr uint8_t kBmi160GyroRangeRegister = 0x43;
static constexpr uint8_t kBmi160CommandRegister = 0x7E;
static constexpr uint8_t kBmi160SoftResetCommand = 0xB6;
static constexpr uint8_t kBmi160AccelNormalModeCommand = 0x11;
static constexpr uint8_t kBmi160GyroNormalModeCommand = 0x15;
static constexpr uint8_t kBmi160AccelConfig100Hz = 0x28;
static constexpr uint8_t kBmi160GyroConfig100Hz = 0x28;
static constexpr uint8_t kBmi160AccelRange2g = 0x03;
static constexpr uint8_t kBmi160GyroRange250dps = 0x03;
static constexpr uint32_t kI2cTimeoutMs = 50;
static constexpr float kDegreesPerRadian = 57.2957795f;

struct StoredImuConfig {
    uint8_t version;
    uint8_t enabled;
    uint8_t model;
    uint8_t busType;
    int8_t i2cSda;
    int8_t i2cScl;
    uint8_t i2cAddress;
    int8_t spiSck;
    int8_t spiMosi;
    int8_t spiMiso;
    int8_t spiCs;
    int8_t intPin;
    int8_t rstPin;
    int8_t drdyPin;
    uint8_t axisSwap[3];
    uint8_t invertX;
    uint8_t invertY;
    uint8_t invertZ;
    uint8_t mountingOrientation;
    float headingOffset;
    uint8_t useFusion;
    uint16_t sampleRateHz;
};

struct Mpu6050Calibration {
    float accelBias[3];
    float gyroBias[3];
    float zeroRoll;
    float zeroPitch;
    float zeroYaw;
};

static constexpr StoredImuConfig kDefaultStoredConfig = {
    1,
    0,
    static_cast<uint8_t>(ImuModel::BMI160),
    static_cast<uint8_t>(ImuBusType::I2C),
    31,
    30,
    0x68,
    -1,
    -1,
    -1,
    -1,
    50,
    -1,
    51,
    {0, 1, 2},
    0,
    0,
    0,
    static_cast<uint8_t>(MountingOrientation::PortraitUp),
    0.0f,
    1,
    100,
};

constexpr ImuModelInfo kModelCatalog[] = {
    {ImuModel::IMU_NONE, "IMU_NONE", true, false, false, "VCC3V3", false, false, false, false},
    {ImuModel::BNO085, "BNO085", true, true, false, "VCC3V3", true, false, true, true},
    {ImuModel::BNO080, "BNO080", true, true, false, "VCC3V3", true, false, true, true},
    {ImuModel::BNO055, "BNO055", true, false, true, "VCC3V3", true, false, true, true},
    {ImuModel::ICM20948, "ICM20948", true, true, false, "VCC3V3", true, false, false, true},
    {ImuModel::MPU9250, "MPU9250", true, true, false, "VCC3V3", true, false, false, true},
    {ImuModel::MPU9255, "MPU9255", true, true, false, "VCC3V3", true, false, false, true},
    {ImuModel::GY91_MPU9250_BMP280, "GY91_MPU9250_BMP280", true, false, false, "VCC3V3", true, true, false, true},
    {ImuModel::MPU6050, "MPU6050", true, false, false, "VCC3V3", false, false, false, false},
    {ImuModel::MPU6500, "MPU6500", true, true, false, "VCC3V3", false, false, false, true},
    {ImuModel::MPU6886, "MPU6886", true, false, false, "VCC3V3", false, false, false, true},
    {ImuModel::BMI160, "BMI160", true, true, false, "VCC3V3", false, false, false, false},
    {ImuModel::BMI270, "BMI270", true, true, false, "VCC3V3", false, false, false, true},
    {ImuModel::BHI260AP, "BHI260AP", true, true, false, "VCC3V3", true, false, true, true},
    {ImuModel::LSM6DS3, "LSM6DS3", true, true, false, "VCC3V3", false, false, false, true},
    {ImuModel::LSM6DSL, "LSM6DSL", true, true, false, "VCC3V3", false, false, false, true},
    {ImuModel::LSM6DSOX, "LSM6DSOX", true, true, false, "VCC3V3", false, false, false, true},
    {ImuModel::LSM9DS1, "LSM9DS1", true, true, false, "VCC3V3", true, false, false, true},
    {ImuModel::LSM9DS0, "LSM9DS0", true, true, false, "VCC3V3", true, false, false, true},
    {ImuModel::LSM6DS3TRC_LIS3MDL, "LSM6DS3TRC_LIS3MDL", true, true, false, "VCC3V3", true, false, false, true},
    {ImuModel::BMI270_BMM150, "BMI270_BMM150", true, true, false, "VCC3V3", true, false, false, true},
    {ImuModel::HMC5883L, "HMC5883L", true, false, false, "VCC3V3", true, false, false, true},
    {ImuModel::QMC5883L, "QMC5883L", true, false, false, "VCC3V3", true, false, false, true},
    {ImuModel::LIS3MDL, "LIS3MDL", true, true, false, "VCC3V3", true, false, false, true},
    {ImuModel::BMM150, "BMM150", true, false, false, "VCC3V3", true, false, false, true},
    {ImuModel::ADXL345, "ADXL345", true, true, false, "VCC3V3", false, false, false, true},
    {ImuModel::ADIS16500, "ADIS16500", false, true, false, "VCC3V3", false, false, false, true},
    {ImuModel::ADIS16505, "ADIS16505", false, true, false, "VCC3V3", false, false, false, true},
    {ImuModel::HW579_COMBO, "HW579_COMBO", true, true, false, "VCC3V3", true, true, false, true},
};

struct I2cIdentityProbe {
    ImuModel model;
    uint8_t reg;
    std::array<uint8_t, 3> bytes;
    uint8_t length;
};

constexpr I2cIdentityProbe kI2cIdentityProbes[] = {
    {ImuModel::BNO055, 0x00, {0xA0, 0x00, 0x00}, 1},
    {ImuModel::ICM20948, 0x00, {0xEA, 0x00, 0x00}, 1},
    {ImuModel::MPU6050, 0x75, {0x68, 0x00, 0x00}, 1},
    {ImuModel::MPU6500, 0x75, {0x70, 0x00, 0x00}, 1},
    {ImuModel::MPU9250, 0x75, {0x71, 0x00, 0x00}, 1},
    {ImuModel::MPU9255, 0x75, {0x73, 0x00, 0x00}, 1},
    {ImuModel::BMI160, 0x00, {0xD1, 0x00, 0x00}, 1},
    {ImuModel::BMI270, 0x00, {0x24, 0x00, 0x00}, 1},
    {ImuModel::LSM6DS3, 0x0F, {0x69, 0x00, 0x00}, 1},
    {ImuModel::LSM6DSL, 0x0F, {0x6A, 0x00, 0x00}, 1},
    {ImuModel::LSM6DSOX, 0x0F, {0x6C, 0x00, 0x00}, 1},
    {ImuModel::LIS3MDL, 0x0F, {0x3D, 0x00, 0x00}, 1},
    {ImuModel::BMM150, 0x40, {0x32, 0x00, 0x00}, 1},
    {ImuModel::ADXL345, 0x00, {0xE5, 0x00, 0x00}, 1},
    {ImuModel::HMC5883L, 0x0A, {'H', '4', '3'}, 3},
};

bool model_has_concrete_driver(ImuModel model)
{
    switch (model) {
        case ImuModel::BMI160:
        case ImuModel::MPU6050:
        case ImuModel::MPU6500:
        case ImuModel::MPU6886:
        case ImuModel::MPU9250:
        case ImuModel::GY91_MPU9250_BMP280:
            return true;
        default:
            return false;
    }
}

bool mpu_family_who_am_i_matches(ImuModel model, uint8_t who_am_i)
{
    switch (model) {
        case ImuModel::MPU6050:
            return who_am_i == 0x68;
        case ImuModel::MPU6500:
        case ImuModel::MPU6886:
            return who_am_i == 0x70;
        case ImuModel::MPU9250:
        case ImuModel::GY91_MPU9250_BMP280:
            return who_am_i == 0x71;
        default:
            return (who_am_i == 0x68) || (who_am_i == 0x70) || (who_am_i == 0x71);
    }
}

float wrap_degrees(float angle)
{
    while (angle >= 180.0f) {
        angle -= 360.0f;
    }
    while (angle < -180.0f) {
        angle += 360.0f;
    }
    return angle;
}

ImuSample transformed_sample(const ImuConfig &cfg, const ImuSample &in)
{
    ImuSample out = in;

    auto apply_vector = [&cfg](float &x, float &y, float &z) {
        const std::array<float, 3> original = {x, y, z};
        x = original[std::min<size_t>(cfg.axisSwap[0], 2)];
        y = original[std::min<size_t>(cfg.axisSwap[1], 2)];
        z = original[std::min<size_t>(cfg.axisSwap[2], 2)];
        if (cfg.invertX) {
            x = -x;
        }
        if (cfg.invertY) {
            y = -y;
        }
        if (cfg.invertZ) {
            z = -z;
        }
    };

    if (out.hasAccel) {
        apply_vector(out.ax, out.ay, out.az);
    }
    if (out.hasGyro) {
        apply_vector(out.gx, out.gy, out.gz);
    }
    if (out.hasMag) {
        apply_vector(out.mx, out.my, out.mz);
    }

    out.yaw += cfg.headingOffset;
    while (out.yaw >= 180.0f) {
        out.yaw -= 360.0f;
    }
    while (out.yaw < -180.0f) {
        out.yaw += 360.0f;
    }
    return out;
}

class PlaceholderImuDriver : public ImuDriver {
public:
    explicit PlaceholderImuDriver(const char *label): _label(label) {}

    bool begin(const ImuConfig &cfg) override
    {
        (void)cfg;
        return false;
    }

    bool read(ImuSample &out) override
    {
        out = {};
        return false;
    }

    bool calibrateStep() override
    {
        return false;
    }

    bool saveCalibration() override
    {
        return false;
    }

    bool loadCalibration() override
    {
        return false;
    }

    const char *name() override
    {
        return _label;
    }

private:
    const char *_label;
};

class Mpu6050Driver : public ImuDriver {
public:
    explicit Mpu6050Driver(ImuService &service): _service(service)
    {
        std::memset(&_calibration, 0, sizeof(_calibration));
    }

    bool begin(const ImuConfig &cfg) override
    {
        _cfg = cfg;
        _port = _service.selectI2cPort(cfg);
        if (!_service.configureI2cPort(cfg, nullptr)) {
            return false;
        }

        uint8_t who_am_i = 0;
        if (!_service.readI2cRegisters(_port, _cfg.i2cAddress, kMpu6050WhoAmIRegister, &who_am_i, 1)) {
            ESP_LOGW(kTag, "MPU6050 WHO_AM_I read failed on GPIO%d/GPIO%d addr 0x%02X",
                     static_cast<int>(_cfg.i2cSda),
                     static_cast<int>(_cfg.i2cScl),
                     static_cast<unsigned>(_cfg.i2cAddress));
            return false;
        }
        if (!mpu_family_who_am_i_matches(_cfg.model, who_am_i)) {
            ESP_LOGW(kTag, "Unexpected MPU-family WHO_AM_I value 0x%02X for %s",
                     static_cast<unsigned>(who_am_i),
                     imu_model_label(_cfg.model));
            return false;
        }

        if (!_service.writeI2cRegister8(_port, _cfg.i2cAddress, kMpu6050PwrMgmt1Register, 0x01) ||
            !_service.writeI2cRegister8(_port, _cfg.i2cAddress, kMpu6050ConfigRegister, 0x03) ||
            !_service.writeI2cRegister8(_port, _cfg.i2cAddress, kMpu6050GyroConfigRegister, 0x00) ||
            !_service.writeI2cRegister8(_port, _cfg.i2cAddress, kMpu6050AccelConfigRegister, 0x00)) {
            return false;
        }

        (void)loadCalibration();
        return true;
    }

    bool read(ImuSample &out) override
    {
        std::array<uint8_t, 14> raw = {};
        if (!_service.readI2cRegisters(_port, _cfg.i2cAddress, kMpu6050DataRegister, raw.data(), raw.size())) {
            out = {};
            return false;
        }

        auto read_be16 = [&raw](size_t offset) -> int16_t {
            return static_cast<int16_t>((static_cast<uint16_t>(raw[offset]) << 8) | raw[offset + 1]);
        };

        const float ax = static_cast<float>(read_be16(0)) / 16384.0f - _calibration.accelBias[0];
        const float ay = static_cast<float>(read_be16(2)) / 16384.0f - _calibration.accelBias[1];
        const float az = static_cast<float>(read_be16(4)) / 16384.0f - _calibration.accelBias[2];
        const float temperature = static_cast<float>(read_be16(6)) / 340.0f + 36.53f;
        const float gx = static_cast<float>(read_be16(8)) / 131.0f - _calibration.gyroBias[0];
        const float gy = static_cast<float>(read_be16(10)) / 131.0f - _calibration.gyroBias[1];
        const float gz = static_cast<float>(read_be16(12)) / 131.0f - _calibration.gyroBias[2];

        out = {};
        out.valid = true;
        out.hasAccel = true;
        out.hasGyro = true;
        out.ax = ax;
        out.ay = ay;
        out.az = az;
        out.gx = gx;
        out.gy = gy;
        out.gz = gz;
        out.temperature = temperature;
        out.roll = std::atan2(ay, az) * 57.2957795f - _calibration.zeroRoll;
        out.pitch = std::atan2(-ax, std::sqrt((ay * ay) + (az * az))) * 57.2957795f - _calibration.zeroPitch;
        out.yaw = -_calibration.zeroYaw;
        out.calibrationSys = 1;
        out.calibrationAccel = 1;
        out.calibrationGyro = 1;
        return true;
    }

    bool calibrateStep() override
    {
        ImuSample sample = {};
        if (!read(sample)) {
            return false;
        }

        _calibration.accelBias[0] = sample.ax;
        _calibration.accelBias[1] = sample.ay;
        _calibration.accelBias[2] = sample.az - 1.0f;
        _calibration.gyroBias[0] = sample.gx;
        _calibration.gyroBias[1] = sample.gy;
        _calibration.gyroBias[2] = sample.gz;
        return true;
    }

    bool saveCalibration() override
    {
        return _service.saveCalibrationBlob(kMpuCalBlobKey, &_calibration, sizeof(_calibration));
    }

    bool loadCalibration() override
    {
        return _service.loadCalibrationBlob(kMpuCalBlobKey, &_calibration, sizeof(_calibration));
    }

    const char *name() override
    {
        return imu_model_label(_cfg.model);
    }

    bool zeroCurrentOrientation() override
    {
        ImuSample sample = {};
        if (!read(sample)) {
            return false;
        }
        _calibration.zeroRoll = sample.roll + _calibration.zeroRoll;
        _calibration.zeroPitch = sample.pitch + _calibration.zeroPitch;
        _calibration.zeroYaw = sample.yaw + _calibration.zeroYaw;
        return true;
    }

private:
    ImuService &_service;
    ImuConfig _cfg;
    int _port = I2C_NUM_0;
    Mpu6050Calibration _calibration;
};

class Bmi160Driver : public ImuDriver {
public:
    explicit Bmi160Driver(ImuService &service): _service(service)
    {
        std::memset(&_calibration, 0, sizeof(_calibration));
    }

    bool begin(const ImuConfig &cfg) override
    {
        _cfg = cfg;
        _port = _service.selectI2cPort(cfg);
        if (!_service.configureI2cPort(cfg, nullptr)) {
            return false;
        }

        uint8_t chip_id = 0;
        if (!_service.readI2cRegisters(_port, _cfg.i2cAddress, kBmi160ChipIdRegister, &chip_id, 1)) {
            ESP_LOGW(kTag, "BMI160 CHIP_ID read failed on GPIO%d/GPIO%d addr 0x%02X",
                     static_cast<int>(_cfg.i2cSda),
                     static_cast<int>(_cfg.i2cScl),
                     static_cast<unsigned>(_cfg.i2cAddress));
            return false;
        }
        if (chip_id != kBmi160ChipIdValue) {
            ESP_LOGW(kTag, "Unexpected BMI160 CHIP_ID value 0x%02X", static_cast<unsigned>(chip_id));
            return false;
        }

        if (!_service.writeI2cRegister8(_port, _cfg.i2cAddress, kBmi160CommandRegister, kBmi160SoftResetCommand)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));

        if (!_service.writeI2cRegister8(_port, _cfg.i2cAddress, kBmi160CommandRegister, kBmi160AccelNormalModeCommand)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));

        if (!_service.writeI2cRegister8(_port, _cfg.i2cAddress, kBmi160CommandRegister, kBmi160GyroNormalModeCommand)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(85));

        if (!_service.writeI2cRegister8(_port, _cfg.i2cAddress, kBmi160AccelConfigRegister, kBmi160AccelConfig100Hz) ||
            !_service.writeI2cRegister8(_port, _cfg.i2cAddress, kBmi160AccelRangeRegister, kBmi160AccelRange2g) ||
            !_service.writeI2cRegister8(_port, _cfg.i2cAddress, kBmi160GyroConfigRegister, kBmi160GyroConfig100Hz) ||
            !_service.writeI2cRegister8(_port, _cfg.i2cAddress, kBmi160GyroRangeRegister, kBmi160GyroRange250dps)) {
            return false;
        }

        _lastSampleUs = 0;
        _yawIntegrator = 0.0f;
        (void)loadCalibration();
        _yawIntegrator = _calibration.zeroYaw;
        return true;
    }

    bool read(ImuSample &out) override
    {
        std::array<uint8_t, 6> gyro_raw = {};
        std::array<uint8_t, 6> accel_raw = {};
        std::array<uint8_t, 2> temp_raw = {};
        if (!_service.readI2cRegisters(_port, _cfg.i2cAddress, kBmi160GyroDataRegister, gyro_raw.data(), gyro_raw.size()) ||
            !_service.readI2cRegisters(_port, _cfg.i2cAddress, kBmi160AccelDataRegister, accel_raw.data(), accel_raw.size()) ||
            !_service.readI2cRegisters(_port, _cfg.i2cAddress, kBmi160TemperatureRegister, temp_raw.data(), temp_raw.size())) {
            out = {};
            return false;
        }

        auto read_le16 = [](const auto &raw, size_t offset) -> int16_t {
            return static_cast<int16_t>(static_cast<uint16_t>(raw[offset]) |
                                        (static_cast<uint16_t>(raw[offset + 1]) << 8));
        };

        const float gx = static_cast<float>(read_le16(gyro_raw, 0)) / 131.2f - _calibration.gyroBias[0];
        const float gy = static_cast<float>(read_le16(gyro_raw, 2)) / 131.2f - _calibration.gyroBias[1];
        const float gz = static_cast<float>(read_le16(gyro_raw, 4)) / 131.2f - _calibration.gyroBias[2];
        const float ax = static_cast<float>(read_le16(accel_raw, 0)) / 16384.0f - _calibration.accelBias[0];
        const float ay = static_cast<float>(read_le16(accel_raw, 2)) / 16384.0f - _calibration.accelBias[1];
        const float az = static_cast<float>(read_le16(accel_raw, 4)) / 16384.0f - _calibration.accelBias[2];
        const int16_t temp_raw_value = read_le16(temp_raw, 0);
        const float temperature = static_cast<float>(temp_raw_value) / 512.0f + 23.0f;
        const int64_t now_us = esp_timer_get_time();
        if (_lastSampleUs > 0) {
            const float delta_seconds = std::clamp(static_cast<float>(now_us - _lastSampleUs) / 1000000.0f, 0.0f, 0.25f);
            _yawIntegrator = wrap_degrees(_yawIntegrator + (gz * delta_seconds));
        }
        _lastSampleUs = now_us;

        out = {};
        out.valid = true;
        out.hasAccel = true;
        out.hasGyro = true;
        out.ax = ax;
        out.ay = ay;
        out.az = az;
        out.gx = gx;
        out.gy = gy;
        out.gz = gz;
        out.temperature = temperature;
        out.roll = std::atan2(ay, az) * kDegreesPerRadian - _calibration.zeroRoll;
        out.pitch = std::atan2(-ax, std::sqrt((ay * ay) + (az * az))) * kDegreesPerRadian - _calibration.zeroPitch;
        out.yaw = wrap_degrees(_yawIntegrator - _calibration.zeroYaw);
        out.calibrationSys = 1;
        out.calibrationAccel = 1;
        out.calibrationGyro = 1;
        return true;
    }

    bool calibrateStep() override
    {
        ImuSample sample = {};
        if (!read(sample)) {
            return false;
        }

        _calibration.accelBias[0] = sample.ax;
        _calibration.accelBias[1] = sample.ay;
        _calibration.accelBias[2] = sample.az - 1.0f;
        _calibration.gyroBias[0] = sample.gx;
        _calibration.gyroBias[1] = sample.gy;
        _calibration.gyroBias[2] = sample.gz;
        return true;
    }

    bool saveCalibration() override
    {
        return _service.saveCalibrationBlob(kMpuCalBlobKey, &_calibration, sizeof(_calibration));
    }

    bool loadCalibration() override
    {
        return _service.loadCalibrationBlob(kMpuCalBlobKey, &_calibration, sizeof(_calibration));
    }

    bool zeroCurrentOrientation() override
    {
        ImuSample sample = {};
        if (!read(sample)) {
            return false;
        }

        _calibration.zeroRoll += sample.roll;
        _calibration.zeroPitch += sample.pitch;
        _calibration.zeroYaw += sample.yaw;
        return true;
    }

    const char *name() override
    {
        return "BMI160";
    }

private:
    ImuService &_service;
    ImuConfig _cfg;
    int _port = I2C_NUM_0;
    Mpu6050Calibration _calibration;
    int64_t _lastSampleUs = 0;
    float _yawIntegrator = 0.0f;
};

std::unique_ptr<ImuDriver> create_driver(ImuService &service, ImuModel model)
{
    switch (model) {
        case ImuModel::BMI160:
            return std::make_unique<Bmi160Driver>(service);
        case ImuModel::MPU6050:
        case ImuModel::MPU6500:
        case ImuModel::MPU6886:
        case ImuModel::MPU9250:
        case ImuModel::GY91_MPU9250_BMP280:
            return std::make_unique<Mpu6050Driver>(service);
        case ImuModel::IMU_NONE:
            return std::make_unique<PlaceholderImuDriver>("IMU_NONE");
        default:
            return std::make_unique<PlaceholderImuDriver>(imu_model_label(model));
    }
}

StoredImuConfig to_stored_config(const ImuConfig &config)
{
    StoredImuConfig stored = kDefaultStoredConfig;
    stored.enabled = config.enabled ? 1U : 0U;
    stored.model = static_cast<uint8_t>(config.model);
    stored.busType = static_cast<uint8_t>(config.busType);
    stored.i2cSda = config.i2cSda;
    stored.i2cScl = config.i2cScl;
    stored.i2cAddress = config.i2cAddress;
    stored.spiSck = config.spiSck;
    stored.spiMosi = config.spiMosi;
    stored.spiMiso = config.spiMiso;
    stored.spiCs = config.spiCs;
    stored.intPin = config.intPin;
    stored.rstPin = config.rstPin;
    stored.drdyPin = config.drdyPin;
    stored.axisSwap[0] = config.axisSwap[0];
    stored.axisSwap[1] = config.axisSwap[1];
    stored.axisSwap[2] = config.axisSwap[2];
    stored.invertX = config.invertX ? 1U : 0U;
    stored.invertY = config.invertY ? 1U : 0U;
    stored.invertZ = config.invertZ ? 1U : 0U;
    stored.mountingOrientation = static_cast<uint8_t>(config.mountingOrientation);
    stored.headingOffset = config.headingOffset;
    stored.useFusion = config.useFusion ? 1U : 0U;
    stored.sampleRateHz = config.sampleRateHz;
    return stored;
}

ImuConfig from_stored_config(const StoredImuConfig &stored)
{
    ImuConfig config = make_default_imu_config();
    config.enabled = stored.enabled != 0;
    config.model = static_cast<ImuModel>(stored.model);
    config.busType = static_cast<ImuBusType>(stored.busType);
    config.i2cSda = stored.i2cSda;
    config.i2cScl = stored.i2cScl;
    config.i2cAddress = stored.i2cAddress;
    config.spiSck = stored.spiSck;
    config.spiMosi = stored.spiMosi;
    config.spiMiso = stored.spiMiso;
    config.spiCs = stored.spiCs;
    config.intPin = stored.intPin;
    config.rstPin = stored.rstPin;
    config.drdyPin = stored.drdyPin;
    config.axisSwap = {stored.axisSwap[0], stored.axisSwap[1], stored.axisSwap[2]};
    config.invertX = stored.invertX != 0;
    config.invertY = stored.invertY != 0;
    config.invertZ = stored.invertZ != 0;
    config.mountingOrientation = static_cast<MountingOrientation>(stored.mountingOrientation);
    config.headingOffset = stored.headingOffset;
    config.useFusion = stored.useFusion != 0;
    config.sampleRateHz = stored.sampleRateHz;
    return config;
}

bool assignable_or_shared_i2c_gpio(int gpio)
{
    return (gpio == jc4880::board_pins::kSharedI2cSdaGpio) ||
           (gpio == jc4880::board_pins::kSharedI2cSclGpio) ||
           jc4880::board_pins::is_jp1_assignable_gpio(gpio);
}

} // namespace

const ImuModelInfo *get_imu_model_catalog(size_t *count_out)
{
    if (count_out != nullptr) {
        *count_out = sizeof(kModelCatalog) / sizeof(kModelCatalog[0]);
    }
    return kModelCatalog;
}

const ImuModelInfo *find_imu_model_info(ImuModel model)
{
    for (const ImuModelInfo &info : kModelCatalog) {
        if (info.model == model) {
            return &info;
        }
    }
    return &kModelCatalog[0];
}

bool imu_model_supports_bus(ImuModel model, ImuBusType bus_type)
{
    const ImuModelInfo *info = find_imu_model_info(model);
    if (info == nullptr) {
        return false;
    }
    switch (bus_type) {
        case ImuBusType::I2C:
            return info->supportsI2c;
        case ImuBusType::SPI:
            return info->supportsSpi;
        case ImuBusType::UART:
            return info->supportsUart;
        default:
            return false;
    }
}

const char *imu_bus_type_label(ImuBusType bus_type)
{
    switch (bus_type) {
        case ImuBusType::I2C:
            return "I2C";
        case ImuBusType::SPI:
            return "SPI";
        case ImuBusType::UART:
            return "UART";
        default:
            return "Unknown";
    }
}

const char *imu_mounting_orientation_label(MountingOrientation orientation)
{
    switch (orientation) {
        case MountingOrientation::PortraitUp:
            return "Portrait Up";
        case MountingOrientation::PortraitDown:
            return "Portrait Down";
        case MountingOrientation::LandscapeLeft:
            return "Landscape Left";
        case MountingOrientation::LandscapeRight:
            return "Landscape Right";
        case MountingOrientation::FlatFaceUp:
            return "Flat Face Up";
        case MountingOrientation::FlatFaceDown:
            return "Flat Face Down";
        default:
            return "Portrait Up";
    }
}

const char *imu_model_label(ImuModel model)
{
    const ImuModelInfo *info = find_imu_model_info(model);
    return info != nullptr ? info->label : "IMU_NONE";
}

ImuConfig make_default_imu_config()
{
    ImuConfig config = {};
    config.enabled = kDefaultStoredConfig.enabled != 0;
    config.model = static_cast<ImuModel>(kDefaultStoredConfig.model);
    config.busType = static_cast<ImuBusType>(kDefaultStoredConfig.busType);
    config.i2cSda = kDefaultStoredConfig.i2cSda;
    config.i2cScl = kDefaultStoredConfig.i2cScl;
    config.i2cAddress = kDefaultStoredConfig.i2cAddress;
    config.spiSck = kDefaultStoredConfig.spiSck;
    config.spiMosi = kDefaultStoredConfig.spiMosi;
    config.spiMiso = kDefaultStoredConfig.spiMiso;
    config.spiCs = kDefaultStoredConfig.spiCs;
    config.intPin = kDefaultStoredConfig.intPin;
    config.rstPin = kDefaultStoredConfig.rstPin;
    config.drdyPin = kDefaultStoredConfig.drdyPin;
    config.axisSwap = {kDefaultStoredConfig.axisSwap[0],
                       kDefaultStoredConfig.axisSwap[1],
                       kDefaultStoredConfig.axisSwap[2]};
    config.invertX = kDefaultStoredConfig.invertX != 0;
    config.invertY = kDefaultStoredConfig.invertY != 0;
    config.invertZ = kDefaultStoredConfig.invertZ != 0;
    config.mountingOrientation = static_cast<MountingOrientation>(kDefaultStoredConfig.mountingOrientation);
    config.headingOffset = kDefaultStoredConfig.headingOffset;
    config.useFusion = kDefaultStoredConfig.useFusion != 0;
    config.sampleRateHz = kDefaultStoredConfig.sampleRateHz;
    return config;
}

void apply_axis_transform(const ImuConfig &cfg, ImuSample &sample)
{
    sample = transformed_sample(cfg, sample);
}

ImuService &ImuService::instance()
{
    static ImuService service;
    return service;
}

ImuService::ImuService():
    _config(make_default_imu_config()),
    _lastSample(),
    _driver(),
    _active(false),
    _i2cBusHandle(nullptr),
    _ownedI2cPins({-1, -1}),
    _ownsI2cBus(false)
{
}

bool ImuService::loadConfig(ImuConfig &config)
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
        config = make_default_imu_config();
        return false;
    }

    StoredImuConfig stored = {};
    size_t size = sizeof(stored);
    const esp_err_t result = nvs_get_blob(handle, kConfigBlobKey, &stored, &size);
    nvs_close(handle);
    if ((result != ESP_OK) || (size != sizeof(stored)) || (stored.version != kDefaultStoredConfig.version)) {
        config = make_default_imu_config();
        return false;
    }

    config = from_stored_config(stored);
    _config = config;
    return true;
}

bool ImuService::saveConfig(const ImuConfig &config)
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    const StoredImuConfig stored = to_stored_config(config);
    const esp_err_t result = nvs_set_blob(handle, kConfigBlobKey, &stored, sizeof(stored));
    if (result == ESP_OK) {
        (void)nvs_commit(handle);
        _config = config;
    }
    nvs_close(handle);
    return result == ESP_OK;
}

bool ImuService::begin(const ImuConfig *config)
{
    if (config != nullptr) {
        _config = *config;
    } else {
        ImuConfig loaded = {};
        if (loadConfig(loaded)) {
            _config = loaded;
        }
    }

    std::string error;
    if (!validateConfig(_config, error) || !_config.enabled || (_config.model == ImuModel::IMU_NONE)) {
        ESP_LOGW(kTag, "IMU begin skipped: %s", error.empty() ? "disabled or unconfigured" : error.c_str());
        _active = false;
        _driver.reset();
        return false;
    }

    _driver = create_driver(*this, _config.model);
    if ((_driver == nullptr) || !_driver->begin(_config)) {
        ESP_LOGW(kTag, "IMU driver begin failed for %s on %s", imu_model_label(_config.model), imu_bus_type_label(_config.busType));
        _active = false;
        return false;
    }

    char pin_diag[256] = {};
    jc4880::board_pins::format_gpio_owner_diagnostics(pin_diag, sizeof(pin_diag));
    ESP_LOGI(kTag,
             "IMU begin model=%s bus=%s power=%s pins=%s board=%s",
             _driver->name(),
             imu_bus_type_label(_config.busType),
             find_imu_model_info(_config.model)->powerHint,
             buildPinDiagnostics(_config).c_str(),
             pin_diag);
    _active = true;
    return true;
}

void ImuService::stop()
{
    _driver.reset();
    _active = false;
    if (_ownsI2cBus && (_i2cBusHandle != nullptr)) {
        (void)i2c_del_master_bus(_i2cBusHandle);
        _i2cBusHandle = nullptr;
        _ownedI2cPins = {-1, -1};
        _ownsI2cBus = false;
    }
}

bool ImuService::read(ImuSample &sample)
{
    if (!_active || (_driver == nullptr)) {
        sample = {};
        return false;
    }

    if (!_driver->read(sample)) {
        sample = {};
        return false;
    }

    apply_axis_transform(_config, sample);
    _lastSample = sample;
    return sample.valid;
}

bool ImuService::scanI2c(const ImuConfig &config, std::array<uint8_t, 32> &addresses, size_t &count, std::string &status)
{
    count = 0;
    addresses.fill(0);
    std::string error;
    if ((config.busType != ImuBusType::I2C) || !validateConfig(config, error)) {
        status = error.empty() ? "I2C scan requires a valid I2C IMU configuration." : error;
        return false;
    }
    if (!configureI2cPort(config, nullptr)) {
        status = "Failed to configure the IMU I2C bus.";
        return false;
    }

    for (uint8_t address = 0x08; address < 0x78; ++address) {
        i2c_device_config_t device_config = {};
        device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device_config.device_address = address;
        device_config.scl_speed_hz = kI2cFrequencyHz;

        i2c_master_dev_handle_t device = nullptr;
        const esp_err_t add_result = i2c_master_bus_add_device(_i2cBusHandle, &device_config, &device);
        if (add_result != ESP_OK) {
            continue;
        }

        uint8_t probe = 0;
        const esp_err_t result = i2c_master_receive(device, &probe, 1, kI2cTimeoutMs);
        (void)i2c_master_bus_rm_device(device);
        if (result == ESP_OK) {
            if (count < addresses.size()) {
                addresses[count++] = address;
            }
        }
    }

    char summary[96] = {};
    std::snprintf(summary,
                  sizeof(summary),
                  "I2C scan on GPIO%d/GPIO%d found %lu device(s).",
                  static_cast<int>(config.i2cSda),
                  static_cast<int>(config.i2cScl),
                  static_cast<unsigned long>(count));
    status = summary;
    return true;
}

bool ImuService::detectI2cModel(const ImuConfig &config, ImuModel &model, uint8_t &address, std::string &status)
{
    model = ImuModel::IMU_NONE;
    address = 0;

    std::array<uint8_t, 32> addresses = {};
    size_t count = 0;
    std::string scan_status;
    if (!scanI2c(config, addresses, count, scan_status)) {
        status = scan_status;
        return false;
    }

    const bool has_bmp280 = [&addresses, count]() {
        for (size_t index = 0; index < count; ++index) {
            if ((addresses[index] == 0x76) || (addresses[index] == 0x77)) {
                return true;
            }
        }
        return false;
    }();

    for (size_t address_index = 0; address_index < count; ++address_index) {
        const uint8_t candidate_address = addresses[address_index];
        for (const I2cIdentityProbe &probe : kI2cIdentityProbes) {
            uint8_t buffer[3] = {};
            if (!readI2cRegisters(selectI2cPort(config), candidate_address, probe.reg, buffer, probe.length)) {
                continue;
            }
            bool match = true;
            for (uint8_t byte_index = 0; byte_index < probe.length; ++byte_index) {
                if (buffer[byte_index] != probe.bytes[byte_index]) {
                    match = false;
                    break;
                }
            }
            if (!match) {
                continue;
            }

            model = probe.model;
            address = candidate_address;
            if ((model == ImuModel::MPU9250) && has_bmp280) {
                model = ImuModel::GY91_MPU9250_BMP280;
            }

            char summary[224] = {};
            std::snprintf(summary,
                          sizeof(summary),
                          "Detected %s at 0x%02X on GPIO%d/GPIO%d.%s Use the IMU Model dropdown to force a different driver if needed.",
                          imu_model_label(model),
                          static_cast<unsigned>(address),
                          static_cast<int>(config.i2cSda),
                          static_cast<int>(config.i2cScl),
                          model_has_concrete_driver(model)
                              ? " A concrete transport backend is available for this model family."
                              : " This model is identified, but its dedicated sensor backend is still staged.");
            status = summary;
            return true;
        }
    }

    status = scan_status + " No known IMU identity register matched the detected addresses.";
    return false;
}

bool ImuService::test(const ImuConfig &config, ImuSample &sample, std::string &status)
{
    if (!begin(&config)) {
        const ImuModelInfo *info = find_imu_model_info(config.model);
        if ((info != nullptr) && info->placeholderOnly) {
            status = std::string(info->label) + " is staged in the framework but its transport driver is still a placeholder.";
        } else {
            status = "Failed to initialize the selected IMU with the current wiring and bus settings.";
        }
        return false;
    }

    if (!read(sample)) {
        status = "IMU initialized, but no valid sample was read.";
        return false;
    }

    char summary[160] = {};
    std::snprintf(summary,
                  sizeof(summary),
                  "%s online. Accel=%.2fg,%.2fg,%.2fg Gyro=%.2f,%.2f,%.2f dps",
                  _driver != nullptr ? _driver->name() : imu_model_label(config.model),
                  sample.ax,
                  sample.ay,
                  sample.az,
                  sample.gx,
                  sample.gy,
                  sample.gz);
    status = summary;
    return true;
}

bool ImuService::calibrateStep(std::string &status)
{
    if ((_driver == nullptr) || !_active) {
        status = "IMU is not active.";
        return false;
    }
    if (!_driver->calibrateStep()) {
        status = "Calibration step failed or is not implemented for this module.";
        return false;
    }
    if (!_driver->saveCalibration()) {
        status = "Calibration sample captured, but it could not be saved.";
        return false;
    }
    status = "Calibration sample captured and saved.";
    return true;
}

bool ImuService::saveCalibration(std::string &status)
{
    if ((_driver == nullptr) || !_active) {
        status = "IMU is not active.";
        return false;
    }
    if (!_driver->saveCalibration()) {
        status = "Calibration save is not available for this driver yet.";
        return false;
    }
    status = "Calibration saved.";
    return true;
}

bool ImuService::clearCalibration(std::string &status)
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        status = "Failed to open IMU storage.";
        return false;
    }
    (void)nvs_erase_key(handle, kMpuCalBlobKey);
    (void)nvs_commit(handle);
    nvs_close(handle);
    if (_driver != nullptr) {
        (void)_driver->loadCalibration();
    }
    status = "Calibration cleared.";
    return true;
}

bool ImuService::setCurrentOrientationAsZero(std::string &status)
{
    if (!_active || (_driver == nullptr)) {
        status = "IMU is not active.";
        return false;
    }
    if (!_driver->zeroCurrentOrientation()) {
        status = "Failed to sample the current orientation or this IMU driver does not support zeroing yet.";
        return false;
    }
    if (!_driver->saveCalibration()) {
        status = "Current orientation was sampled, but the zero calibration could not be saved.";
        return false;
    }
    status = "Current orientation captured as zero.";
    return true;
}

std::string ImuService::buildPinDiagnostics(const ImuConfig &config) const
{
    char buffer[192] = {};
    if (config.busType == ImuBusType::I2C) {
        std::snprintf(buffer,
                      sizeof(buffer),
                      "SDA=%d SCL=%d ADDR=0x%02X INT=%d RST=%d DRDY=%d",
                      static_cast<int>(config.i2cSda),
                      static_cast<int>(config.i2cScl),
                      static_cast<unsigned>(config.i2cAddress),
                      static_cast<int>(config.intPin),
                      static_cast<int>(config.rstPin),
                      static_cast<int>(config.drdyPin));
    } else if (config.busType == ImuBusType::SPI) {
        std::snprintf(buffer,
                      sizeof(buffer),
                      "SCK=%d MOSI=%d MISO=%d CS=%d INT=%d RST=%d DRDY=%d",
                      static_cast<int>(config.spiSck),
                      static_cast<int>(config.spiMosi),
                      static_cast<int>(config.spiMiso),
                      static_cast<int>(config.spiCs),
                      static_cast<int>(config.intPin),
                      static_cast<int>(config.rstPin),
                      static_cast<int>(config.drdyPin));
    } else {
        std::snprintf(buffer, sizeof(buffer), "UART is selected; staged driver support depends on the chosen module.");
    }
    return std::string(buffer);
}

bool ImuService::validateConfig(const ImuConfig &config, std::string &error) const
{
    error.clear();
    if (!imu_model_supports_bus(config.model, config.busType)) {
        error = "Selected IMU model does not support the chosen bus type.";
        return false;
    }

    auto validate_optional_pin = [&error](int pin, const char *label) -> bool {
        if (pin < 0) {
            return true;
        }
        if (!jc4880::board_pins::is_jp1_assignable_gpio(pin)) {
            error = std::string(label) + " must use a JP1 GPIO or stay disabled.";
            return false;
        }
        return true;
    };

    if (config.busType == ImuBusType::I2C) {
        if (!assignable_or_shared_i2c_gpio(config.i2cSda) || !assignable_or_shared_i2c_gpio(config.i2cScl)) {
            error = "IMU I2C pins must use ES_I2C_SDA/ES_I2C_SCL or JP1 GPIOs.";
            return false;
        }
        if (config.i2cSda == config.i2cScl) {
            error = "IMU I2C SDA/SCL must be unique.";
            return false;
        }
        if ((config.i2cAddress < 0x08) || (config.i2cAddress > 0x77)) {
            error = "IMU I2C address must be between 0x08 and 0x77.";
            return false;
        }
    }

    if (config.busType == ImuBusType::SPI) {
        const int pins[] = {config.spiSck, config.spiMosi, config.spiMiso, config.spiCs};
        for (int pin : pins) {
            if (!validate_optional_pin(pin, "IMU SPI pin")) {
                return false;
            }
        }
        if ((config.spiSck < 0) || (config.spiMosi < 0) || (config.spiMiso < 0) || (config.spiCs < 0)) {
            error = "IMU SPI requires SCK, MOSI, MISO, and CS pins.";
            return false;
        }
        if ((config.spiSck == config.spiMosi) || (config.spiSck == config.spiMiso) || (config.spiSck == config.spiCs) ||
            (config.spiMosi == config.spiMiso) || (config.spiMosi == config.spiCs) || (config.spiMiso == config.spiCs)) {
            error = "IMU SPI pins must be unique.";
            return false;
        }
    }

    if (!validate_optional_pin(config.intPin, "IMU INT") ||
        !validate_optional_pin(config.rstPin, "IMU RST") ||
        !validate_optional_pin(config.drdyPin, "IMU DRDY")) {
        return false;
    }

    if ((config.sampleRateHz < 1U) || (config.sampleRateHz > 1000U)) {
        error = "IMU sample rate must be between 1 Hz and 1000 Hz.";
        return false;
    }

    return true;
}

bool ImuService::getLastSample(ImuSample &sample) const
{
    sample = _lastSample;
    return sample.valid;
}

bool ImuService::configureI2cPort(const ImuConfig &config, bool *driver_installed_out)
{
    if (driver_installed_out != nullptr) {
        *driver_installed_out = false;
    }

    const bool uses_board_shared_bus =
        (config.i2cSda == jc4880::board_pins::kDefaultImuI2cSdaGpio) &&
        (config.i2cScl == jc4880::board_pins::kDefaultImuI2cSclGpio);

    if (uses_board_shared_bus) {
        if (_ownsI2cBus && (_i2cBusHandle != nullptr)) {
            (void)i2c_del_master_bus(_i2cBusHandle);
            _i2cBusHandle = nullptr;
            _ownsI2cBus = false;
            _ownedI2cPins = {-1, -1};
        }

        if (bsp_i2c_get_handle() == nullptr) {
            if (bsp_i2c_init() != ESP_OK) {
                return false;
            }
        }

        _i2cBusHandle = bsp_i2c_get_handle();
        return _i2cBusHandle != nullptr;
    }

    const std::array<int8_t, 2> requested_pins = {config.i2cSda, config.i2cScl};
    if (_ownsI2cBus && (_i2cBusHandle != nullptr) && (_ownedI2cPins == requested_pins)) {
        return true;
    }

    if (_ownsI2cBus && (_i2cBusHandle != nullptr)) {
        (void)i2c_del_master_bus(_i2cBusHandle);
        _i2cBusHandle = nullptr;
        _ownsI2cBus = false;
        _ownedI2cPins = {-1, -1};
    }

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = static_cast<gpio_num_t>(config.i2cSda);
    bus_config.scl_io_num = static_cast<gpio_num_t>(config.i2cScl);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bus_config, &_i2cBusHandle) != ESP_OK) {
        _i2cBusHandle = nullptr;
        return false;
    }

    _ownsI2cBus = true;
    _ownedI2cPins = requested_pins;
    if (driver_installed_out != nullptr) {
        *driver_installed_out = true;
    }
    return true;
}

int ImuService::selectI2cPort(const ImuConfig &config) const
{
    const bool uses_board_shared_bus =
        (config.i2cSda == jc4880::board_pins::kDefaultImuI2cSdaGpio) &&
        (config.i2cScl == jc4880::board_pins::kDefaultImuI2cSclGpio);
    return uses_board_shared_bus ? BSP_I2C_NUM : I2C_NUM_0;
}

bool ImuService::writeI2cRegister8(int port, uint8_t address, uint8_t reg, uint8_t value) const
{
    (void)port;
    if (_i2cBusHandle == nullptr) {
        return false;
    }

    uint8_t payload[2] = {reg, value};
    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = address;
    device_config.scl_speed_hz = kI2cFrequencyHz;

    i2c_master_dev_handle_t device = nullptr;
    if (i2c_master_bus_add_device(_i2cBusHandle, &device_config, &device) != ESP_OK) {
        return false;
    }
    const esp_err_t result = i2c_master_transmit(device, payload, sizeof(payload), kI2cTimeoutMs);
    (void)i2c_master_bus_rm_device(device);
    return result == ESP_OK;
}

bool ImuService::readI2cRegisters(int port, uint8_t address, uint8_t reg, uint8_t *buffer, size_t length) const
{
    (void)port;
    if ((_i2cBusHandle == nullptr) || (buffer == nullptr) || (length == 0)) {
        return false;
    }

    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = address;
    device_config.scl_speed_hz = kI2cFrequencyHz;

    i2c_master_dev_handle_t device = nullptr;
    if (i2c_master_bus_add_device(_i2cBusHandle, &device_config, &device) != ESP_OK) {
        return false;
    }
    const esp_err_t result = i2c_master_transmit_receive(device, &reg, 1, buffer, length, kI2cTimeoutMs);
    (void)i2c_master_bus_rm_device(device);
    return result == ESP_OK;
}

bool ImuService::saveCalibrationBlob(const char *blob_key, const void *data, size_t length)
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    const esp_err_t result = nvs_set_blob(handle, blob_key, data, length);
    if (result == ESP_OK) {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
    return result == ESP_OK;
}

bool ImuService::loadCalibrationBlob(const char *blob_key, void *data, size_t length) const
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
        std::memset(data, 0, length);
        return false;
    }
    size_t actual_length = length;
    const esp_err_t result = nvs_get_blob(handle, blob_key, data, &actual_length);
    nvs_close(handle);
    if ((result != ESP_OK) || (actual_length != length)) {
        std::memset(data, 0, length);
        return false;
    }
    return true;
}

} // namespace jc4880::imu