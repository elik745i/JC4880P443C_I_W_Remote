#pragma once

#include <array>
#include <memory>
#include <string>

#include "driver/i2c_master.h"
#include "ImuDriver.hpp"

namespace jc4880::imu {

class ImuService {
public:
    static ImuService &instance();

    bool loadConfig(ImuConfig &config);
    bool saveConfig(const ImuConfig &config);
    bool begin(const ImuConfig *config = nullptr);
    void stop();
    bool read(ImuSample &sample);
    bool scanI2c(const ImuConfig &config, std::array<uint8_t, 32> &addresses, size_t &count, std::string &status);
    bool detectI2cModel(const ImuConfig &config, ImuModel &model, uint8_t &address, std::string &status);
    bool test(const ImuConfig &config, ImuSample &sample, std::string &status);
    bool calibrateStep(std::string &status);
    bool saveCalibration(std::string &status);
    bool clearCalibration(std::string &status);
    bool setCurrentOrientationAsZero(std::string &status);
    std::string buildPinDiagnostics(const ImuConfig &config) const;
    bool validateConfig(const ImuConfig &config, std::string &error) const;
    bool getLastSample(ImuSample &sample) const;
    bool configureI2cPort(const ImuConfig &config, bool *driver_installed_out = nullptr);
    int selectI2cPort(const ImuConfig &config) const;
    bool writeI2cRegister8(int port, uint8_t address, uint8_t reg, uint8_t value) const;
    bool readI2cRegisters(int port, uint8_t address, uint8_t reg, uint8_t *buffer, size_t length) const;
    bool saveCalibrationBlob(const char *blob_key, const void *data, size_t length);
    bool loadCalibrationBlob(const char *blob_key, void *data, size_t length) const;

private:
    ImuService();

    ImuConfig _config;
    ImuSample _lastSample;
    std::unique_ptr<ImuDriver> _driver;
    bool _active;
    i2c_master_bus_handle_t _i2cBusHandle;
    std::array<int8_t, 2> _ownedI2cPins;
    bool _ownsI2cBus;
};

} // namespace jc4880::imu