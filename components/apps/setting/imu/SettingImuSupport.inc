#if APP_SETTINGS_FEATURE_IMU
static bool is_imu_enabled()
{
    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);
    return config.enabled && (config.model != jc4880::imu::ImuModel::IMU_NONE);
}

static uint16_t imu_pin_choice_index(int32_t gpio)
{
    static constexpr int32_t kImuPinChoices[] = {-1, 28, 29, 30, 31, 32, 33, 34, 35, 49, 50, 51, 52};
    for (size_t index = 0; index < (sizeof(kImuPinChoices) / sizeof(kImuPinChoices[0])); ++index) {
        if (kImuPinChoices[index] == gpio) {
            return static_cast<uint16_t>(index);
        }
    }
    return 0;
}

static int32_t imu_pin_choice_value(uint16_t index)
{
    static constexpr int32_t kImuPinChoices[] = {-1, 28, 29, 30, 31, 32, 33, 34, 35, 49, 50, 51, 52};
    if (index >= (sizeof(kImuPinChoices) / sizeof(kImuPinChoices[0]))) {
        return kImuPinChoices[0];
    }
    return kImuPinChoices[index];
}

static int32_t sanitizeImuAssignableGpio(int32_t gpio)
{
    if (gpio < 0) {
        return -1;
    }
    if (!jc4880::board_pins::is_jp1_assignable_gpio(gpio)) {
        return -1;
    }
    return gpio;
}

static int32_t imu_model_from_dropdown(uint16_t index)
{
    size_t count = 0;
    const jc4880::imu::ImuModelInfo *catalog = jc4880::imu::get_imu_model_catalog(&count);
    if ((catalog == nullptr) || (index >= count)) {
        return static_cast<int32_t>(jc4880::imu::ImuModel::IMU_NONE);
    }
    return static_cast<int32_t>(catalog[index].model);
}

static uint16_t imu_dropdown_index_from_model(jc4880::imu::ImuModel model)
{
    size_t count = 0;
    const jc4880::imu::ImuModelInfo *catalog = jc4880::imu::get_imu_model_catalog(&count);
    if (catalog == nullptr) {
        return 0;
    }
    for (size_t index = 0; index < count; ++index) {
        if (catalog[index].model == model) {
            return static_cast<uint16_t>(index);
        }
    }
    return 0;
}

std::string describe_imu_attachment()
{
    jc4880::imu::ImuConfig config = {};
    auto &imu_service = jc4880::imu::ImuService::instance();
    if (!imu_service.loadConfig(config)) {
        return "IMU: configuration unavailable";
    }
    if (config.busType != jc4880::imu::ImuBusType::I2C) {
        std::ostringstream stream;
        stream << "IMU: bus " << jc4880::imu::imu_bus_type_label(config.busType)
               << " configured, live auto-detect currently covers I2C modules only";
        return stream.str();
    }

    jc4880::imu::ImuModel detected_model = jc4880::imu::ImuModel::IMU_NONE;
    uint8_t detected_address = 0;
    std::string status;
    if (!imu_service.detectI2cModel(config, detected_model, detected_address, status)) {
        std::ostringstream stream;
        stream << "IMU: not detected on SDA GPIO" << static_cast<int>(config.i2cSda)
               << ", SCL GPIO" << static_cast<int>(config.i2cScl);
        if (!status.empty()) {
            stream << " (" << status << ")";
        }
        return stream.str();
    }

    std::ostringstream stream;
    stream << "IMU: " << jc4880::imu::imu_model_label(detected_model)
           << " - SDA GPIO" << static_cast<int>(config.i2cSda)
           << ", SCL GPIO" << static_cast<int>(config.i2cScl);
    if (config.intPin >= 0) {
        stream << ", INT GPIO" << static_cast<int>(config.intPin);
    }
    if (config.drdyPin >= 0) {
        stream << ", DRDY GPIO" << static_cast<int>(config.drdyPin);
    }
    stream << ", I2C 0x" << std::hex << std::uppercase << static_cast<int>(detected_address) << std::dec;
    return stream.str();
}

static constexpr int32_t kImuBusOptions[] = {
    static_cast<int32_t>(jc4880::imu::ImuBusType::I2C),
    static_cast<int32_t>(jc4880::imu::ImuBusType::SPI),
    static_cast<int32_t>(jc4880::imu::ImuBusType::UART),
};
static constexpr char kImuBusOptionsText[] = "I2C\nSPI\nUART";
static constexpr int32_t kImuPinOptions[] = {-1, 28, 29, 30, 31, 32, 33, 34, 35, 49, 50, 51, 52};
static constexpr char kImuPinOptionsText[] = "Disabled\nGPIO 28\nGPIO 29\nGPIO 30\nGPIO 31\nGPIO 32\nGPIO 33\nGPIO 34\nGPIO 35\nGPIO 49\nGPIO 50\nGPIO 51\nGPIO 52";
static constexpr int32_t kImuAddressOptions[] = {0x68, 0x69};
static constexpr char kImuAddressOptionsText[] = "0x68\n0x69";
static constexpr char kImuModelOptionsText[] =
    "IMU_NONE\nBNO085\nBNO080\nBNO055\nICM20948\nMPU9250\nMPU9255\nGY91_MPU9250_BMP280\nMPU6050\nMPU6500\nMPU6886\nBMI160\nBMI270\nBHI260AP\nLSM6DS3\nLSM6DSL\nLSM6DSOX\nLSM9DS1\nLSM9DS0\nLSM6DS3TRC_LIS3MDL\nBMI270_BMM150\nHMC5883L\nQMC5883L\nLIS3MDL\nBMM150\nADXL345\nADIS16500\nADIS16505\nHW579_COMBO";
#else
static bool is_imu_enabled()
{
    return false;
}

std::string describe_imu_attachment()
{
    return std::string();
}
#endif