#pragma once

#include "RS485.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "app_state.h"
#include "device_scan.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "log_store.h"
#include "modbus_rtu.h"
#include "profile_store.h"
#include "rs485_transport.h"
}
#include "bsp/esp32_p4_function_ev_board.h"

#include "device_scan.h"
#include "storage_access.h"

LV_IMG_DECLARE(rs485_png);

extern rs485_log_store_t s_logStore;
extern rs485_profile_store_t s_profileStore;
extern rs485_transport_t s_transport;
extern modbus_rtu_master_t s_modbusMaster;
extern rs485_app_settings_t s_settings;

namespace rs485app {

inline constexpr char kTag[] = "RS485App";
inline constexpr char kStorageRoot[] = "/sdcard/rs485";
inline constexpr char kLogExportPath[] = "/sdcard/rs485/rs485_log.csv";
inline constexpr uint32_t kUiTickMs = 250;
inline constexpr uint32_t kScanTaskStack = 8192;
inline constexpr UBaseType_t kScanTaskPriority = 4;
inline constexpr lv_coord_t kHeaderHeight = 88;
inline constexpr lv_coord_t kSectionGap = 10;
inline constexpr size_t kLogCapacity = 192;
inline constexpr const char *kTodoPinHint = "TODO: assign UART TX/RX/EN GPIO pins in Settings or defaults";

enum ScreenActionId {
    ACTION_SHOW_HOME = 1,
    ACTION_SHOW_SCAN,
    ACTION_SHOW_TERMINAL,
    ACTION_SHOW_MODBUS,
    ACTION_SHOW_DEVICES,
    ACTION_SHOW_DASHBOARD,
    ACTION_SHOW_LOGS,
    ACTION_SHOW_SETTINGS,
    ACTION_SCAN_START,
    ACTION_SCAN_STOP,
    ACTION_TERMINAL_SEND_ASCII,
    ACTION_TERMINAL_SEND_HEX,
    ACTION_TERMINAL_CLEAR,
    ACTION_TERMINAL_EXPORT,
    ACTION_TERMINAL_TEMPLATE,
    ACTION_MODBUS_READ_HOLDING,
    ACTION_MODBUS_READ_INPUT,
    ACTION_MODBUS_READ_COILS,
    ACTION_MODBUS_WRITE_SINGLE,
    ACTION_PROFILE_SAVE_SELECTED,
    ACTION_PROFILE_LOAD_DEFAULT,
    ACTION_DASHBOARD_REFRESH,
    ACTION_LOGS_CLEAR,
    ACTION_LOGS_EXPORT,
    ACTION_SETTINGS_BAUD,
    ACTION_SETTINGS_PARITY,
    ACTION_SETTINGS_STOP_BITS,
    ACTION_SETTINGS_RANGE,
    ACTION_SETTINGS_PERSIST,
    ACTION_SETTINGS_RETRIES,
};

inline bool ensure_directory(const char *path)
{
    struct stat info = {};
    if (stat(path, &info) == 0) {
        return S_ISDIR(info.st_mode);
    }
    return mkdir(path, 0755) == 0;
}

inline std::string hex_encode(const uint8_t *data, size_t length)
{
    std::ostringstream stream;
    stream.setf(std::ios::uppercase);
    for (size_t index = 0; index < length; ++index) {
        if (index != 0) {
            stream << ' ';
        }
        stream.width(2);
        stream.fill('0');
        stream << std::hex << static_cast<unsigned>(data[index]);
    }
    return stream.str();
}

inline bool parse_hex_string(const std::string &text, std::vector<uint8_t> &output)
{
    output.clear();
    std::istringstream stream(text);
    std::string token;
    while (stream >> token) {
        char *end = nullptr;
        const long value = std::strtol(token.c_str(), &end, 16);
        if ((end == token.c_str()) || (*end != '\0') || (value < 0) || (value > 255)) {
            return false;
        }
        output.push_back(static_cast<uint8_t>(value));
    }
    return !output.empty();
}

inline std::string format_function_mask(uint8_t mask)
{
    std::string text;
    if ((mask & RS485_SCAN_PROBE_READ_HOLDING) != 0U) {
        text += "HR ";
    }
    if ((mask & RS485_SCAN_PROBE_READ_INPUT) != 0U) {
        text += "IR ";
    }
    if ((mask & RS485_SCAN_PROBE_READ_COILS) != 0U) {
        text += "COIL ";
    }
    if (text.empty()) {
        text = "None";
    }
    return text;
}

inline BaseType_t create_task_prefer_psram(TaskFunction_t task,
                                           const char *name,
                                           uint32_t stackDepth,
                                           void *arg,
                                           UBaseType_t priority,
                                           TaskHandle_t *taskHandle,
                                           BaseType_t coreId)
{
    if (xTaskCreatePinnedToCoreWithCaps(task,
                                        name,
                                        stackDepth,
                                        arg,
                                        priority,
                                        taskHandle,
                                        coreId,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        return pdPASS;
    }
    return xTaskCreatePinnedToCore(task, name, stackDepth, arg, priority, taskHandle, coreId);
}

inline rs485_transport_config_t make_transport_config(const rs485_app_settings_t &settings)
{
    rs485_transport_config_t config = {};
    config.uart_port = static_cast<uart_port_t>(settings.serial.uart_port);
    config.tx_pin = settings.serial.tx_pin;
    config.rx_pin = settings.serial.rx_pin;
    config.en_pin = settings.serial.en_pin;
    config.baud_rate = settings.serial.baud_rate;
    config.data_bits = settings.serial.data_bits >= 8 ? UART_DATA_8_BITS : UART_DATA_7_BITS;
    config.parity = settings.serial.parity == RS485_PARITY_EVEN ? UART_PARITY_EVEN :
                    (settings.serial.parity == RS485_PARITY_ODD ? UART_PARITY_ODD : UART_PARITY_DISABLE);
    config.stop_bits = settings.serial.stop_bits >= 2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1;
    config.inter_frame_timeout_ms = settings.serial.inter_frame_timeout_ms;
    config.request_timeout_ms = settings.serial.request_timeout_ms;
    config.tx_pre_delay_us = settings.serial.tx_pre_delay_us;
    config.tx_post_delay_us = settings.serial.tx_post_delay_us;
    return config;
}

} // namespace rs485app