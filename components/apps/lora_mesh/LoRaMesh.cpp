#include "LoRaMesh.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "LoRaMeshCrypto.hpp"
#include "LoRaMeshPackets.hpp"
#include "LoRaPinProfile.hpp"
#include "LoRaMeshStorage.hpp"
#include "LoRaMeshTypes.hpp"
#include "assets/esp_brookesia_assets.h"
#include "bsp/esp-bsp.h"
#include "neopixel_runtime.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lvgl_input_helper.h"

LV_IMG_DECLARE(loramesh_png);

namespace {

constexpr char kTag[] = "LoRaMesh";

constexpr spi_host_device_t kLoRaSpiHost = SPI3_HOST;
constexpr uart_port_t kLoRaUartPort = UART_NUM_1;
constexpr int kSpiClockHz = 1 * 1000 * 1000;
constexpr int kUartBaudRate = 9600;
constexpr uint8_t kRadioSyncWordMsb = 0x14;
constexpr uint8_t kRadioSyncWordLsb = 0x24;
constexpr uint8_t kDefaultTtl = 4;
constexpr uint32_t kUiTickMs = 250;
constexpr uint32_t kRxPollMs = 25;
constexpr uint32_t kRxTaskStack = 6144;
constexpr UBaseType_t kRxTaskPriority = 4;
constexpr size_t kMaxLogBytes = 4096;
constexpr size_t kMaxConversationMessages = 64;
constexpr uint32_t kTxTimeoutSteps = 0x09C400; // 10 s at 15.625 us per step
constexpr int64_t kTxWaitDeadlineUs = 12000000;
constexpr int64_t kInvalidIrqAssumeTxUs = 700000;
constexpr size_t kMaxPayloadBytes = 220;
constexpr size_t kRecentFrameCount = 24;
constexpr uint32_t kUartAuxTimeoutMs = 250;
constexpr uint32_t kUartFrameGapMs = 20;
constexpr uint32_t kUartCommandTimeoutMs = 500;
constexpr uint32_t kUartTxCompleteTimeoutMs = 1500;

constexpr uint8_t kE22CommandWritePersistent = 0xC0;
constexpr uint8_t kE22CommandReadRegisters = 0xC1;
constexpr uint8_t kE22RegisterStart = 0x00;
constexpr uint8_t kE22RegisterCount = 0x09;
constexpr uint32_t kE22BaseFrequencyHz = 410125000;
constexpr uint32_t kE22ChannelStepHz = 1000000;
constexpr uint8_t kE22Reg0Default = 0x62;
constexpr uint8_t kE22Reg1Default = 0x00;
constexpr uint8_t kE22OptionTransparentDefault = 0x03;

constexpr uint8_t SX126X_CMD_SET_STANDBY = 0x80;
constexpr uint8_t SX126X_CMD_SET_RX = 0x82;
constexpr uint8_t SX126X_CMD_SET_TX = 0x83;
constexpr uint8_t SX126X_CMD_SET_CALIBRATION = 0x89;
constexpr uint8_t SX126X_CMD_SET_PACKET_TYPE = 0x8A;
constexpr uint8_t SX126X_CMD_SET_RF_FREQUENCY = 0x86;
constexpr uint8_t SX126X_CMD_SET_TX_PARAMS = 0x8E;
constexpr uint8_t SX126X_CMD_GET_STATUS = 0xC0;
constexpr uint8_t SX126X_CMD_READ_REGISTER = 0x1D;
constexpr uint8_t SX126X_CMD_SET_BUFFER_BASE_ADDRESS = 0x8F;
constexpr uint8_t SX126X_CMD_SET_MODULATION_PARAMS = 0x8B;
constexpr uint8_t SX126X_CMD_SET_PACKET_PARAMS = 0x8C;
constexpr uint8_t SX126X_CMD_SET_DIO_IRQ_PARAMS = 0x08;
constexpr uint8_t SX126X_CMD_CLEAR_DEVICE_ERRORS = 0x07;
constexpr uint8_t SX126X_CMD_GET_DEVICE_ERRORS = 0x17;
constexpr uint8_t SX126X_CMD_GET_IRQ_STATUS = 0x12;
constexpr uint8_t SX126X_CMD_CLEAR_IRQ_STATUS = 0x02;
constexpr uint8_t SX126X_CMD_GET_RX_BUFFER_STATUS = 0x13;
constexpr uint8_t SX126X_CMD_GET_PACKET_STATUS = 0x14;
constexpr uint8_t SX126X_CMD_WRITE_BUFFER = 0x0E;
constexpr uint8_t SX126X_CMD_READ_BUFFER = 0x1E;
constexpr uint8_t SX126X_CMD_WRITE_REGISTER = 0x0D;
constexpr uint8_t SX126X_CMD_SET_REGULATOR_MODE = 0x96;
constexpr uint8_t SX126X_CMD_SET_TCXO_MODE = 0x97;
constexpr uint8_t SX126X_CMD_CALIBRATE_IMAGE = 0x98;
constexpr uint8_t SX126X_CMD_SET_PA_CONFIG = 0x95;
constexpr uint8_t SX126X_STANDBY_RC = 0x00;
constexpr uint8_t SX126X_STANDBY_XOSC = 0x01;
constexpr uint8_t SX126X_TCXO_CTRL_1P7V = 0x01;
constexpr uint8_t SX126X_CALIBRATION_ALL = 0x7F;
constexpr uint32_t kTcxoSetupSteps = 5U << 6;

constexpr uint16_t SX126X_IRQ_TX_DONE = 0x0001;
constexpr uint16_t SX126X_IRQ_RX_DONE = 0x0002;
constexpr uint16_t SX126X_IRQ_HEADER_ERR = 0x0020;
constexpr uint16_t SX126X_IRQ_CRC_ERR = 0x0040;
constexpr uint16_t SX126X_IRQ_TIMEOUT = 0x0200;

constexpr uint8_t kMeshMagic0 = 'M';
constexpr uint8_t kMeshMagic1 = 'H';
constexpr uint8_t kMeshVersion = 1;
constexpr uint8_t kMeshFlagBeacon = 0x01;
constexpr uint16_t kSyncWordRegister = 0x0740;

struct MeshFrame {
    uint8_t ttl = 0;
    uint8_t flags = 0;
    uint32_t origin = 0;
    uint32_t sender = 0;
    uint32_t sequence = 0;
    std::string payload;
};

struct RecentFrame {
    uint32_t origin = 0;
    uint32_t sequence = 0;
};

struct ConversationEntry {
    std::string conversation_id;
    std::string text;
    std::string meta;
    bool outgoing = false;
};

enum class ViewMode : uint8_t {
    Targets = 0,
    Chat,
};

enum class DebugUiAction : uint8_t {
    ShowTargets = 0,
    ShowCommonChat,
    ShowPeerChat,
    StartSelfTest,
    StopSelfTest,
    SendCommonMessage,
};

using HeapBuffer = std::unique_ptr<uint8_t, decltype(&heap_caps_free)>;
using jc4880::lora_mesh::CryptoIdentity;
using jc4880::lora_mesh::MeshPacket;
using jc4880::lora_mesh::PacketKind;
using jc4880::lora_mesh::PeerInfo;
using jc4880::lora_mesh::PendingPairRequest;
using jc4880::lora_mesh::RadioModule;
using jc4880::lora_mesh::StoredState;

enum class RadioPinRole : uint8_t {
    SpiMiso = 0,
    SpiMosi,
    SpiSck,
    SpiNss,
    Busy,
    Dio1,
    Reset,
    TxEnable,
    RxEnable,
    UartTx,
    UartRx,
    Mode0,
    Mode1,
    Aux,
};

constexpr size_t kRadioPinRoleCount = 14;
constexpr int8_t kRadioGpioChoices[] = {29, 30, 31, 33, 34, 35, 50, 51, 52};
constexpr char kRadioModuleOptions[] = "Ebyte E22-400M22S\nEbyte E22-400T22S\nEbyte E220-400T22D";
constexpr char kRadioGpioDropdownOptions[] = "GPIO29\nGPIO30\nGPIO31\nGPIO33\nGPIO34\nGPIO35\nGPIO50\nGPIO51\nGPIO52";

RadioModule radio_module_from_dropdown(uint16_t selected)
{
    switch (selected) {
        case 1:
            return RadioModule::E22_400T22S;
        case 2:
            return RadioModule::E220_400T22D;
        default:
            return RadioModule::E22_400M22S;
    }
}

uint16_t dropdown_index_from_radio_module(RadioModule module)
{
    switch (module) {
        case RadioModule::E22_400T22S:
            return 1;
        case RadioModule::E220_400T22D:
            return 2;
        case RadioModule::E22_400M22S:
        default:
            return 0;
    }
}

const char *radio_module_name(RadioModule module)
{
    switch (module) {
        case RadioModule::E22_400T22S:
            return "Ebyte E22-400T22S";
        case RadioModule::E220_400T22D:
            return "Ebyte E220-400T22D";
        case RadioModule::E22_400M22S:
        default:
            return "Ebyte E22-400M22S";
    }
}

bool radio_module_uses_spi(RadioModule module)
{
    return module == RadioModule::E22_400M22S;
}

const char *radio_pin_label(RadioPinRole role)
{
    switch (role) {
        case RadioPinRole::SpiMiso:
            return "MISO";
        case RadioPinRole::SpiMosi:
            return "MOSI";
        case RadioPinRole::SpiSck:
            return "SCK";
        case RadioPinRole::SpiNss:
            return "NSS";
        case RadioPinRole::Busy:
            return "BUSY";
        case RadioPinRole::Dio1:
            return "DIO1";
        case RadioPinRole::Reset:
            return "NRST";
        case RadioPinRole::TxEnable:
            return "TXEN";
        case RadioPinRole::RxEnable:
            return "RXEN";
        case RadioPinRole::UartTx:
            return "RXD";
        case RadioPinRole::UartRx:
            return "TXD";
        case RadioPinRole::Mode0:
            return "M0";
        case RadioPinRole::Mode1:
            return "M1";
        case RadioPinRole::Aux:
            return "AUX";
        default:
            return "GPIO";
    }
}

HeapBuffer allocate_byte_buffer(size_t size)
{
    if (size == 0) {
        return HeapBuffer(nullptr, &heap_caps_free);
    }

    void *memory = heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (memory == nullptr) {
        memory = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (memory == nullptr) {
        memory = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }

    return HeapBuffer(static_cast<uint8_t *>(memory), &heap_caps_free);
}

std::string format_hex_bytes(const uint8_t *data, size_t length)
{
    if ((data == nullptr) || (length == 0)) {
        return "-";
    }

    std::ostringstream stream;
    stream << std::hex << std::uppercase;
    for (size_t index = 0; index < length; ++index) {
        if (index != 0) {
            stream << ' ';
        }
        stream.width(2);
        stream.fill('0');
        stream << static_cast<unsigned>(data[index]);
    }
    return stream.str();
}

bool all_bytes_equal(const uint8_t *data, size_t length, uint8_t value)
{
    if ((data == nullptr) || (length == 0)) {
        return false;
    }

    for (size_t index = 0; index < length; ++index) {
        if (data[index] != value) {
            return false;
        }
    }

    return true;
}

bool is_invalid_uniform_spi_signature(const uint8_t *data, size_t length)
{
    return all_bytes_equal(data, length, 0xAA) || all_bytes_equal(data, length, 0xFF);
}

bool is_uniform_spi_signature_value(const uint8_t *data, size_t length, uint8_t value)
{
    return all_bytes_equal(data, length, value);
}

bool create_task_prefer_psram(TaskFunction_t task,
                              const char *name,
                              uint32_t stack_depth,
                              void *arg,
                              UBaseType_t priority,
                              TaskHandle_t *task_handle)
{
    if (xTaskCreatePinnedToCoreWithCaps(task,
                                        name,
                                        stack_depth,
                                        arg,
                                        priority,
                                        task_handle,
                                        tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        return true;
    }
    return xTaskCreatePinnedToCore(task, name, stack_depth, arg, priority, task_handle, tskNO_AFFINITY) == pdPASS;
}

std::string format_node_id(uint32_t node_id)
{
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%08" PRIX32, node_id);
    return std::string(buffer);
}

void append_line_capped(std::string &target, const std::string &line)
{
    if (!target.empty()) {
        target.push_back('\n');
    }
    target += line;
    if (target.size() > kMaxLogBytes) {
        target.erase(0, target.size() - kMaxLogBytes);
    }
}

uint32_t read_u32_be(const uint8_t *data)
{
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

void write_u32_be(uint8_t *data, uint32_t value)
{
    data[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    data[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[3] = static_cast<uint8_t>(value & 0xFF);
}

uint16_t read_u16_be(const uint8_t *data)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

void write_u16_be(uint8_t *data, uint16_t value)
{
    data[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[1] = static_cast<uint8_t>(value & 0xFF);
}

const char *view_mode_name(ViewMode mode)
{
    switch (mode) {
    case ViewMode::Targets:
        return "targets";
    case ViewMode::Chat:
        return "chat";
    }
    return "unknown";
}

struct DebugUiActionContext {
    LoRaMeshApp *app = nullptr;
    DebugUiAction action = DebugUiAction::ShowTargets;
    std::string peer_id;
};

bool is_printable_text(const std::string &text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char character) {
        return std::isprint(character) || std::isspace(character);
    });
}

} // namespace

struct LoRaMeshApp::Impl {
    spi_device_handle_t spi = nullptr;
    bool spi_bus_ready = false;
    bool uart_ready = false;
    bool radio_ready = false;
    bool rx_task_stop = false;
    TaskHandle_t rx_task = nullptr;
    TaskHandle_t startup_task = nullptr;
    TaskHandle_t send_task = nullptr;
    TaskHandle_t self_test_task = nullptr;
    SemaphoreHandle_t mutex = nullptr;
    lv_timer_t *ui_timer = nullptr;
    lv_obj_t *root = nullptr;
    lv_obj_t *header_title = nullptr;
    lv_obj_t *back_button = nullptr;
    lv_obj_t *menu_button = nullptr;
    lv_obj_t *target_list = nullptr;
    lv_obj_t *chat_panel = nullptr;
    lv_obj_t *settings_panel = nullptr;
    lv_obj_t *chat_title_label = nullptr;
    lv_obj_t *chat_status_label = nullptr;
    lv_obj_t *message_list = nullptr;
    lv_obj_t *composer = nullptr;
    lv_obj_t *input = nullptr;
    lv_obj_t *send_button = nullptr;
    lv_obj_t *keyboard = nullptr;
    lv_obj_t *settings_name_input = nullptr;
    lv_obj_t *settings_common_name_input = nullptr;
    lv_obj_t *settings_module_dropdown = nullptr;
    lv_obj_t *settings_module_info_label = nullptr;
    lv_obj_t *settings_frequency_input = nullptr;
    lv_obj_t *settings_sf_input = nullptr;
    lv_obj_t *settings_bw_input = nullptr;
    lv_obj_t *settings_cr_input = nullptr;
    lv_obj_t *settings_hop_input = nullptr;
    std::array<lv_obj_t *, kRadioPinRoleCount> settings_pin_rows = {};
    std::array<lv_obj_t *, kRadioPinRoleCount> settings_pin_labels = {};
    std::array<lv_obj_t *, kRadioPinRoleCount> settings_pin_dropdowns = {};
    lv_obj_t *settings_self_test_button = nullptr;
    lv_obj_t *settings_self_test_button_label = nullptr;
    lv_obj_t *settings_self_test_label = nullptr;
    lv_obj_t *settings_forward_switch = nullptr;
    lv_obj_t *settings_encrypt_switch = nullptr;
    uint32_t node_id = 0;
    uint32_t next_sequence = 1;
    uint32_t tx_count = 0;
    uint32_t rx_count = 0;
    uint32_t relay_count = 0;
    uint32_t drop_count = 0;
    uint16_t last_tx_irq_status = 0;
    std::string last_tx_diagnostic;
    int last_rssi = 0;
    int last_snr = 0;
    std::string status_text = "LoRa radio idle";
    std::string self_test_summary = "Mode: idle";
    std::string log_text;
    bool self_test_ran = false;
    bool self_test_ok = false;
    bool self_test_running = false;
    volatile bool self_test_stop_requested = false;
    bool conversation_dirty = true;
    bool target_list_dirty = true;
    bool state_dirty = false;
    bool hello_sent = false;
    bool startup_started = false;
    bool startup_complete = false;
    bool startup_ok = false;
    bool pending_self_test_on_next_open = false;
    int suspended_neopixel_gpio = -1;
    std::vector<uint8_t> uart_rx_bytes;
    ViewMode view_mode = ViewMode::Targets;
    std::string selected_conversation_id = jc4880::lora_mesh::kCommonConversationId;
    std::string selected_peer_id;
    std::array<RecentFrame, kRecentFrameCount> recent_frames = {};
    size_t recent_index = 0;
    StoredState stored_state = {};
    std::vector<PendingPairRequest> pending_pair_requests;
    std::vector<ConversationEntry> conversation;

    struct PendingSendContext {
        Impl *impl = nullptr;
        std::string payload;
        bool beacon = false;
        bool clear_input_on_success = false;
    };

    ~Impl()
    {
        if (mutex != nullptr) {
            vSemaphoreDelete(mutex);
            mutex = nullptr;
        }
    }

    void lock()
    {
        if (mutex != nullptr) {
            xSemaphoreTake(mutex, portMAX_DELAY);
        }
    }

    void unlock()
    {
        if (mutex != nullptr) {
            xSemaphoreGive(mutex);
        }
    }

    void set_status(const std::string &text)
    {
        lock();
        status_text = text;
        unlock();
    }

    void append_log(const std::string &line)
    {
        lock();
        append_line_capped(log_text, line);
        unlock();
    }

    void trace_event_locked(const std::string &message)
    {
        append_line_capped(log_text, message);
        ESP_LOGI(kTag, "%s", message.c_str());
    }

    void trace_event(const std::string &message)
    {
        lock();
        trace_event_locked(message);
        unlock();
    }

    void set_self_test_mode_locked(const std::string &mode)
    {
        self_test_summary = std::string("Mode: ") + mode;
        status_text = std::string("LoRa self-test: ") + mode;
        ESP_LOGI(kTag, "Self-test mode: %s", mode.c_str());
    }

    void request_self_test_stop_locked(const char *reason)
    {
        if (!self_test_running) {
            return;
        }

        self_test_stop_requested = true;
        self_test_summary = std::string("Mode: stopping (") + reason + ")";
        status_text = "Stopping LoRa self-test...";
        trace_event_locked(std::string("Self-test stop requested: ") + reason);
    }

    static std::string peer_label(const PeerInfo &peer)
    {
        return peer.display_name.empty() ? peer.device_id : peer.display_name;
    }

    PeerInfo *find_peer_locked(const std::string &device_id)
    {
        auto iterator = std::find_if(stored_state.peers.begin(), stored_state.peers.end(), [&device_id](const PeerInfo &peer) {
            return peer.device_id == device_id;
        });
        return iterator == stored_state.peers.end() ? nullptr : &(*iterator);
    }

    PendingPairRequest *find_pending_request_locked(const std::string &device_id)
    {
        auto iterator = std::find_if(pending_pair_requests.begin(), pending_pair_requests.end(), [&device_id](const PendingPairRequest &request) {
            return request.device_id == device_id;
        });
        return iterator == pending_pair_requests.end() ? nullptr : &(*iterator);
    }

    void save_state_if_needed_locked()
    {
        if (!state_dirty) {
            return;
        }
        if (jc4880::lora_mesh::save_stored_state(stored_state)) {
            state_dirty = false;
            target_list_dirty = true;
        } else {
            append_line_capped(log_text, "Failed to persist LoRa mesh settings");
        }
    }

    std::string current_chat_title_locked() const
    {
        if (selected_conversation_id == jc4880::lora_mesh::kCommonConversationId) {
            return stored_state.settings.common_chat_name.empty() ? "Common Mesh Chat" : stored_state.settings.common_chat_name;
        }
        const auto iterator = std::find_if(stored_state.peers.begin(), stored_state.peers.end(), [this](const PeerInfo &peer) {
            return peer.device_id == selected_peer_id;
        });
        return iterator == stored_state.peers.end() ? std::string("Private Chat") : peer_label(*iterator);
    }

    void show_targets_locked()
    {
        view_mode = ViewMode::Targets;
        target_list_dirty = true;
        conversation_dirty = true;
    }

    void show_common_chat_locked()
    {
        view_mode = ViewMode::Chat;
        selected_conversation_id = jc4880::lora_mesh::kCommonConversationId;
        selected_peer_id.clear();
        conversation_dirty = true;
    }

    void show_peer_chat_locked(const std::string &peer_id)
    {
        view_mode = ViewMode::Chat;
        selected_conversation_id = peer_id;
        selected_peer_id = peer_id;
        conversation_dirty = true;
    }

    bool parse_uint32_text(lv_obj_t *textarea, uint32_t &value) const
    {
        if (textarea == nullptr) {
            return false;
        }
        const char *text = lv_textarea_get_text(textarea);
        if ((text == nullptr) || (text[0] == '\0')) {
            return false;
        }
        char *end = nullptr;
        const unsigned long parsed = std::strtoul(text, &end, 10);
        if ((end == nullptr) || (*end != '\0')) {
            return false;
        }
        value = static_cast<uint32_t>(parsed);
        return true;
    }

    bool parse_uint8_text(lv_obj_t *textarea, uint8_t &value) const
    {
        uint32_t parsed = 0;
        if (!parse_uint32_text(textarea, parsed) || (parsed > 255U)) {
            return false;
        }
        value = static_cast<uint8_t>(parsed);
        return true;
    }

    static void set_textarea_text(lv_obj_t *textarea, const std::string &text)
    {
        if (textarea != nullptr) {
            lv_textarea_set_text(textarea, text.c_str());
        }
    }

    static void set_textarea_uint(lv_obj_t *textarea, uint32_t value)
    {
        if (textarea == nullptr) {
            return;
        }
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(value));
        lv_textarea_set_text(textarea, buffer);
    }

    int8_t pin_value_for_role(const jc4880::lora_mesh::MeshSettings &settings, RadioPinRole role) const
    {
        switch (role) {
            case RadioPinRole::SpiMiso:
                return settings.spi_miso_gpio;
            case RadioPinRole::SpiMosi:
                return settings.spi_mosi_gpio;
            case RadioPinRole::SpiSck:
                return settings.spi_sck_gpio;
            case RadioPinRole::SpiNss:
                return settings.spi_nss_gpio;
            case RadioPinRole::Busy:
                return settings.busy_gpio;
            case RadioPinRole::Dio1:
                return settings.dio1_gpio;
            case RadioPinRole::Reset:
                return settings.nrst_gpio;
            case RadioPinRole::TxEnable:
                return settings.txen_gpio;
            case RadioPinRole::RxEnable:
                return settings.rxen_gpio;
            case RadioPinRole::UartTx:
                return settings.uart_tx_gpio;
            case RadioPinRole::UartRx:
                return settings.uart_rx_gpio;
            case RadioPinRole::Mode0:
                return settings.mode0_gpio;
            case RadioPinRole::Mode1:
                return settings.mode1_gpio;
            case RadioPinRole::Aux:
                return settings.aux_gpio;
            default:
                return -1;
        }
    }

    void set_pin_value_for_role(jc4880::lora_mesh::MeshSettings &settings, RadioPinRole role, int8_t value) const
    {
        switch (role) {
            case RadioPinRole::SpiMiso:
                settings.spi_miso_gpio = value;
                break;
            case RadioPinRole::SpiMosi:
                settings.spi_mosi_gpio = value;
                break;
            case RadioPinRole::SpiSck:
                settings.spi_sck_gpio = value;
                break;
            case RadioPinRole::SpiNss:
                settings.spi_nss_gpio = value;
                break;
            case RadioPinRole::Busy:
                settings.busy_gpio = value;
                break;
            case RadioPinRole::Dio1:
                settings.dio1_gpio = value;
                break;
            case RadioPinRole::Reset:
                settings.nrst_gpio = value;
                break;
            case RadioPinRole::TxEnable:
                settings.txen_gpio = value;
                break;
            case RadioPinRole::RxEnable:
                settings.rxen_gpio = value;
                break;
            case RadioPinRole::UartTx:
                settings.uart_tx_gpio = value;
                break;
            case RadioPinRole::UartRx:
                settings.uart_rx_gpio = value;
                break;
            case RadioPinRole::Mode0:
                settings.mode0_gpio = value;
                break;
            case RadioPinRole::Mode1:
                settings.mode1_gpio = value;
                break;
            case RadioPinRole::Aux:
                settings.aux_gpio = value;
                break;
        }
    }

    static int gpio_choice_index(int8_t value)
    {
        for (size_t index = 0; index < sizeof(kRadioGpioChoices) / sizeof(kRadioGpioChoices[0]); ++index) {
            if (kRadioGpioChoices[index] == value) {
                return static_cast<int>(index);
            }
        }
        return 0;
    }

    static int8_t gpio_choice_value(uint16_t index)
    {
        if (index >= (sizeof(kRadioGpioChoices) / sizeof(kRadioGpioChoices[0]))) {
            return kRadioGpioChoices[0];
        }
        return kRadioGpioChoices[index];
    }

    static bool module_uses_role(RadioModule module, RadioPinRole role)
    {
        switch (module) {
            case RadioModule::E22_400M22S:
                return role <= RadioPinRole::RxEnable;
            case RadioModule::E22_400T22S:
                return (role == RadioPinRole::UartTx) || (role == RadioPinRole::UartRx) || (role == RadioPinRole::Mode0) ||
                       (role == RadioPinRole::Mode1) || (role == RadioPinRole::Aux);
            case RadioModule::E220_400T22D:
                return (role == RadioPinRole::UartTx) || (role == RadioPinRole::UartRx) || (role == RadioPinRole::Mode0) ||
                       (role == RadioPinRole::Mode1) || (role == RadioPinRole::Aux);
            default:
                return false;
        }
    }

    void apply_module_defaults(jc4880::lora_mesh::MeshSettings &settings) const
    {
        switch (settings.radio_module) {
            case RadioModule::E22_400M22S:
                settings.spi_miso_gpio = jc4880::lora_mesh::pin_profile::kSpiMisoGpio;
                settings.spi_mosi_gpio = jc4880::lora_mesh::pin_profile::kSpiMosiGpio;
                settings.spi_sck_gpio = jc4880::lora_mesh::pin_profile::kSpiSckGpio;
                settings.spi_nss_gpio = jc4880::lora_mesh::pin_profile::kSpiNssGpio;
                settings.busy_gpio = jc4880::lora_mesh::pin_profile::kBusyGpio;
                settings.dio1_gpio = jc4880::lora_mesh::pin_profile::kDio1Gpio;
                settings.nrst_gpio = jc4880::lora_mesh::pin_profile::kNrstGpio;
                settings.txen_gpio = jc4880::lora_mesh::pin_profile::kTxEnableGpio;
                settings.rxen_gpio = jc4880::lora_mesh::pin_profile::kRxEnableGpio;
                break;
            case RadioModule::E22_400T22S:
                settings.uart_tx_gpio = 31;
                settings.uart_rx_gpio = 30;
                settings.mode0_gpio = 51;
                settings.mode1_gpio = 29;
                settings.aux_gpio = 33;
                settings.nrst_gpio = -1;
                break;
            case RadioModule::E220_400T22D:
                settings.uart_tx_gpio = 31;
                settings.uart_rx_gpio = 33;
                settings.mode0_gpio = 29;
                settings.mode1_gpio = 30;
                settings.aux_gpio = 50;
                break;
        }
    }

    gpio_num_t gpio_for_role(RadioPinRole role) const
    {
        return static_cast<gpio_num_t>(pin_value_for_role(stored_state.settings, role));
    }

    void reset_active_radio_gpios()
    {
        for (size_t role_index = 0; role_index < kRadioPinRoleCount; ++role_index) {
            auto role = static_cast<RadioPinRole>(role_index);
            if (!module_uses_role(stored_state.settings.radio_module, role)) {
                continue;
            }

            const int8_t value = pin_value_for_role(stored_state.settings, role);
            if (value < 0) {
                continue;
            }

            gpio_reset_pin(static_cast<gpio_num_t>(value));
        }
    }

    void suspend_conflicting_neopixel_if_needed()
    {
        if (suspended_neopixel_gpio >= 0) {
            return;
        }

        for (size_t role_index = 0; role_index < kRadioPinRoleCount; ++role_index) {
            auto role = static_cast<RadioPinRole>(role_index);
            if (!module_uses_role(stored_state.settings.radio_module, role)) {
                continue;
            }

            const int8_t value = pin_value_for_role(stored_state.settings, role);
            if (value < 0) {
                continue;
            }

            if (jc4880_neopixel_suspend_gpio(value)) {
                suspended_neopixel_gpio = value;
                ESP_LOGI(kTag, "Suspended NeoPixel ownership on GPIO %d for LoRa", value);
                break;
            }
        }
    }

    void resume_suspended_neopixel_if_needed()
    {
        if (suspended_neopixel_gpio < 0) {
            return;
        }

        jc4880_neopixel_resume_gpio(suspended_neopixel_gpio);
        suspended_neopixel_gpio = -1;
    }

    bool has_pin(RadioPinRole role) const
    {
        return pin_value_for_role(stored_state.settings, role) >= 0;
    }

    void start_startup_task()
    {
        if (startup_task != nullptr) {
            return;
        }

        startup_started = true;
        startup_complete = false;
        startup_ok = false;
        status_text = "Opening LoRa mesh...";
        trace_event_locked("Startup task requested");
        if (!create_task_prefer_psram(&Impl::startup_task_entry, "lora_mesh_boot", 6144, this, kRxTaskPriority, &startup_task)) {
            status_text = "Failed to start LoRa init task";
            startup_complete = true;
            trace_event_locked("Failed to start LoRa init task");
        }
    }

    static void startup_task_entry(void *context)
    {
        auto *impl = static_cast<Impl *>(context);
        if (impl != nullptr) {
            const bool ok = impl->ensure_radio_ready();
            impl->lock();
            impl->startup_ok = ok;
            impl->startup_complete = true;
            impl->startup_task = nullptr;
            if (ok) {
                impl->status_text = "LoRa mesh ready";
                impl->trace_event_locked("Startup task completed successfully");
            } else {
                impl->trace_event_locked("Startup task failed");
            }
            impl->unlock();
        }
        vTaskDelete(nullptr);
    }

    void start_self_test_task()
    {
        if (self_test_task != nullptr) {
            return;
        }
        self_test_stop_requested = false;
        self_test_running = true;
        set_self_test_mode_locked("starting");
        trace_event_locked("Self-test task start requested");
        if (!create_task_prefer_psram(&Impl::self_test_task_entry, "lora_mesh_test", 6144, this, kRxTaskPriority, &self_test_task)) {
            self_test_running = false;
            self_test_summary = "Mode: failed to start";
            append_line_capped(log_text, "Failed to start LoRa self-test task");
            ESP_LOGW(kTag, "Failed to start LoRa self-test task");
        }
    }

    static void finish_send_ui(void *context)
    {
        auto *impl = static_cast<Impl *>(context);
        if (impl == nullptr) {
            return;
        }

        if (impl->input != nullptr) {
            lv_textarea_set_text(impl->input, "");
            lv_obj_clear_state(impl->input, LV_STATE_FOCUSED);
        }
        impl->set_keyboard_visible(false);
        impl->refresh_ui();
    }

    static void refresh_ui_async(void *context)
    {
        auto *impl = static_cast<Impl *>(context);
        if (impl != nullptr) {
            impl->refresh_ui();
        }
    }

    static void send_task_entry(void *context)
    {
        std::unique_ptr<PendingSendContext> request(static_cast<PendingSendContext *>(context));
        if ((request == nullptr) || (request->impl == nullptr)) {
            vTaskDelete(nullptr);
            return;
        }

        Impl *impl = request->impl;
        const bool ok = impl->send_user_message(request->payload, request->beacon);

        bsp_display_lock(0);
        if (ok && request->clear_input_on_success) {
            (void)lv_async_call(&Impl::finish_send_ui, impl);
        } else {
            (void)lv_async_call(&Impl::refresh_ui_async, impl);
        }
        bsp_display_unlock();

        impl->lock();
        if (impl->send_task == xTaskGetCurrentTaskHandle()) {
            impl->send_task = nullptr;
        }
        impl->unlock();
        vTaskDelete(nullptr);
    }

    bool start_send_task(const std::string &payload, bool beacon, bool clear_input_on_success)
    {
        auto *context = new PendingSendContext{.impl = this,
                                               .payload = payload,
                                               .beacon = beacon,
                                               .clear_input_on_success = clear_input_on_success};
        if (context == nullptr) {
            set_status("Failed to allocate LoRa send task");
            return false;
        }

        lock();
        if (send_task != nullptr) {
            delete context;
            status_text = "LoRa transmit already in progress";
            unlock();
            return false;
        }
        unlock();

        TaskHandle_t task = nullptr;
        if (!create_task_prefer_psram(&Impl::send_task_entry, "lora_mesh_send", 6144, context, kRxTaskPriority, &task)) {
            delete context;
            set_status("Failed to start LoRa send task");
            return false;
        }

        lock();
        send_task = task;
        unlock();
        return true;
    }

    static void self_test_task_entry(void *context)
    {
        auto *impl = static_cast<Impl *>(context);
        if (impl != nullptr) {
            impl->lock();
            impl->set_self_test_mode_locked("waiting for receive task to stop");
            impl->rx_task_stop = true;
            impl->unlock();

            while (true) {
                impl->lock();
                const bool rx_active = impl->rx_task != nullptr;
                const bool stop_requested = impl->self_test_stop_requested;
                if (rx_active && stop_requested) {
                    impl->self_test_summary = "Mode: stopping after receive task exits";
                    impl->status_text = "Stopping LoRa self-test...";
                }
                impl->unlock();
                if (!rx_active) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(20));
            }

            impl->lock();
            const bool stop_requested = impl->self_test_stop_requested;
            if (stop_requested) {
                impl->self_test_summary = "Mode: stopped before radio probe";
                impl->status_text = "LoRa self-test stopped";
            } else {
                impl->set_self_test_mode_locked("preparing radio");
            }
            impl->unlock();

            if (!stop_requested) {
                impl->run_self_test(false);
            }

            impl->lock();
            impl->self_test_task = nullptr;
            impl->self_test_running = false;
            impl->self_test_stop_requested = false;
            const bool restart_rx_task = impl->radio_ready;
            impl->rx_task_stop = false;
            impl->unlock();

            if (restart_rx_task) {
                TaskHandle_t rx_task = nullptr;
                if (create_task_prefer_psram(&Impl::rx_task_entry, "lora_mesh_rx", kRxTaskStack, impl, kRxTaskPriority, &rx_task)) {
                    impl->lock();
                    impl->rx_task = rx_task;
                    impl->unlock();
                } else {
                    impl->set_status("Failed to restart LoRa receive task after self-test");
                }
            }
        }
        vTaskDelete(nullptr);
    }

    std::string make_message_meta_locked(const std::string &sender_name) const
    {
        std::ostringstream stream;
        stream << sender_name << "  RSSI " << last_rssi << "  SNR " << last_snr;
        return stream.str();
    }

    void remember_peer_activity_locked(const std::string &device_id,
                                       const std::string &display_name,
                                       const std::string &public_key_hex)
    {
        if (device_id.empty() || (device_id == stored_state.identity.device_id)) {
            return;
        }

        PeerInfo *peer = find_peer_locked(device_id);
        if (peer == nullptr) {
            stored_state.peers.push_back(PeerInfo{.device_id = device_id,
                                                  .display_name = display_name.empty() ? device_id : display_name,
                                                  .public_key_hex = public_key_hex,
                                                  .last_seen_ms = esp_timer_get_time() / 1000,
                                                  .last_rssi = last_rssi,
                                                  .last_snr = last_snr,
                                                  .presence = jc4880::lora_mesh::PeerPresence::Online});
            state_dirty = true;
            target_list_dirty = true;
            return;
        }

        if (!display_name.empty()) {
            peer->display_name = display_name;
        }
        if (!public_key_hex.empty()) {
            peer->public_key_hex = public_key_hex;
        }
        peer->last_seen_ms = esp_timer_get_time() / 1000;
        peer->last_rssi = last_rssi;
        peer->last_snr = last_snr;
        peer->presence = jc4880::lora_mesh::PeerPresence::Online;
        state_dirty = true;
        target_list_dirty = true;
    }

    bool send_packet_locked(MeshPacket &packet, const std::string *bubble_text, const std::string &bubble_meta)
    {
        std::string encoded;
        if (!jc4880::lora_mesh::encode_mesh_packet(packet, encoded)) {
            return false;
        }

        MeshFrame frame = {};
        frame.ttl = packet.ttl;
        frame.flags = 0;
        frame.origin = node_id;
        frame.sender = node_id;
        frame.sequence = next_sequence++;
        frame.payload = encoded;
        packet.msg_id = packet.sender_id + "-" + std::to_string(frame.sequence);
        if (!jc4880::lora_mesh::encode_mesh_packet(packet, frame.payload)) {
            return false;
        }

        const bool ok = transmit_frame_locked(frame);
        if (!ok) {
            ++drop_count;
            std::ostringstream stream;
            stream << "TX packet failed irq=0x" << std::hex << std::uppercase << last_tx_irq_status;
            if (!last_tx_diagnostic.empty()) {
                stream << ' ' << last_tx_diagnostic;
            }
            append_line_capped(log_text, stream.str());
            status_text = stream.str();
            return false;
        }

        ++tx_count;
        remember_frame(frame.origin, frame.sequence);
        if (bubble_text != nullptr) {
            push_conversation_locked(*bubble_text, bubble_meta, true);
        }
        std::ostringstream stream;
        stream << "TX packet kind=" << jc4880::lora_mesh::packet_kind_name(packet.kind)
               << " msg=" << packet.msg_id
               << " target=" << packet.target_id;
        append_line_capped(log_text, stream.str());
        status_text = "Mesh packet sent";
        return true;
    }

    void send_presence_locked(bool hello)
    {
        MeshPacket packet = {};
        packet.kind = hello ? PacketKind::Hello : PacketKind::Presence;
        packet.sender_id = stored_state.identity.device_id;
        packet.sender_name = stored_state.identity.display_name;
        packet.target_id = jc4880::lora_mesh::kBroadcastTargetId;
        packet.timestamp_ms = esp_timer_get_time() / 1000;
        packet.ttl = stored_state.settings.hop_limit;
        packet.public_key_hex = stored_state.identity.public_key_hex;
        (void)send_packet_locked(packet, nullptr, std::string());
    }

    bool send_presence_without_mutex(bool hello)
    {
        MeshPacket packet = {};
        MeshFrame frame = {};

        lock();
        packet.kind = hello ? PacketKind::Hello : PacketKind::Presence;
        packet.sender_id = stored_state.identity.device_id;
        packet.sender_name = stored_state.identity.display_name;
        packet.target_id = jc4880::lora_mesh::kBroadcastTargetId;
        packet.timestamp_ms = esp_timer_get_time() / 1000;
        packet.ttl = stored_state.settings.hop_limit;
        packet.public_key_hex = stored_state.identity.public_key_hex;

        frame.ttl = packet.ttl;
        frame.flags = 0;
        frame.origin = node_id;
        frame.sender = node_id;
        frame.sequence = next_sequence++;
        packet.msg_id = packet.sender_id + "-" + std::to_string(frame.sequence);
        const bool encoded = jc4880::lora_mesh::encode_mesh_packet(packet, frame.payload);
        unlock();

        if (!encoded) {
            return false;
        }

        const bool ok = transmit_frame_locked(frame);

        lock();
        if (!ok) {
            ++drop_count;
            std::ostringstream stream;
            stream << "TX packet failed irq=0x" << std::hex << std::uppercase << last_tx_irq_status;
            if (!last_tx_diagnostic.empty()) {
                stream << ' ' << last_tx_diagnostic;
            }
            append_line_capped(log_text, stream.str());
            status_text = stream.str();
            unlock();
            return false;
        }

        ++tx_count;
        remember_frame(frame.origin, frame.sequence);
        std::ostringstream stream;
        stream << "TX packet kind=" << jc4880::lora_mesh::packet_kind_name(packet.kind)
               << " msg=" << packet.msg_id
               << " target=" << packet.target_id;
        append_line_capped(log_text, stream.str());
        unlock();
        return true;
    }

    bool wait_while_busy(uint32_t timeout_ms) const
    {
        if (!radio_module_uses_spi(stored_state.settings.radio_module)) {
            return true;
        }
        const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
        while (gpio_get_level(gpio_for_role(RadioPinRole::Busy)) != 0) {
            if (self_test_running && self_test_stop_requested) {
                return false;
            }
            if (esp_timer_get_time() >= deadline) {
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        return true;
    }

    void set_rf_path(bool tx) const
    {
        if (has_pin(RadioPinRole::TxEnable)) {
            gpio_set_level(gpio_for_role(RadioPinRole::TxEnable), tx ? 1 : 0);
        }
        if (has_pin(RadioPinRole::RxEnable)) {
            gpio_set_level(gpio_for_role(RadioPinRole::RxEnable), tx ? 0 : 1);
        }
    }

    void set_rf_idle() const
    {
        if (has_pin(RadioPinRole::TxEnable)) {
            gpio_set_level(gpio_for_role(RadioPinRole::TxEnable), 0);
        }
        if (has_pin(RadioPinRole::RxEnable)) {
            gpio_set_level(gpio_for_role(RadioPinRole::RxEnable), 0);
        }
    }

    bool wait_for_uart_aux_ready(uint32_t timeout_ms) const
    {
        if (!has_pin(RadioPinRole::Aux)) {
            return true;
        }

        const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
        while (gpio_get_level(gpio_for_role(RadioPinRole::Aux)) == 0) {
            if (esp_timer_get_time() >= deadline) {
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        return true;
    }

    void set_uart_module_mode(bool normal_mode) const
    {
        if (has_pin(RadioPinRole::Mode0)) {
            gpio_set_level(gpio_for_role(RadioPinRole::Mode0), normal_mode ? 0 : 1);
        }
        if (has_pin(RadioPinRole::Mode1)) {
            gpio_set_level(gpio_for_role(RadioPinRole::Mode1), normal_mode ? 0 : 1);
        }
    }

    uint8_t e22_channel_for_frequency(uint32_t frequency_hz) const
    {
        if (frequency_hz <= kE22BaseFrequencyHz) {
            return 0;
        }

        const uint32_t delta_hz = frequency_hz - kE22BaseFrequencyHz;
        const uint32_t rounded_channel = (delta_hz + (kE22ChannelStepHz / 2U)) / kE22ChannelStepHz;
        return static_cast<uint8_t>(std::min<uint32_t>(rounded_channel, 0xFF));
    }

    esp_err_t uart_read_exact(uint8_t *buffer, size_t length, uint32_t timeout_ms)
    {
        if (buffer == nullptr) {
            return ESP_ERR_INVALID_ARG;
        }

        size_t offset = 0;
        const int64_t deadline_us = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
        while (offset < length) {
            const int64_t remaining_us = deadline_us - esp_timer_get_time();
            if (remaining_us <= 0) {
                return ESP_ERR_TIMEOUT;
            }

            const uint32_t chunk_timeout_ms = static_cast<uint32_t>(std::max<int64_t>(1, remaining_us / 1000));
            const int read = uart_read_bytes(kLoRaUartPort,
                                             buffer + offset,
                                             length - offset,
                                             pdMS_TO_TICKS(std::min<uint32_t>(chunk_timeout_ms, 50)));
            if (read < 0) {
                return ESP_FAIL;
            }
            offset += static_cast<size_t>(read);
        }

        return ESP_OK;
    }

    esp_err_t sync_e22_400t22s_config_locked()
    {
        if (stored_state.settings.radio_module != RadioModule::E22_400T22S) {
            return ESP_OK;
        }

        const uint8_t channel = e22_channel_for_frequency(stored_state.settings.frequency_hz);
        const std::array<uint8_t, 12> write_command = {
            kE22CommandWritePersistent,
            kE22RegisterStart,
            kE22RegisterCount,
            0x00,
            0x00,
            0x00,
            kE22Reg0Default,
            kE22Reg1Default,
            channel,
            kE22OptionTransparentDefault,
            0x00,
            0x00,
        };
        const std::array<uint8_t, 3> read_command = {
            kE22CommandReadRegisters,
            kE22RegisterStart,
            kE22RegisterCount,
        };

        set_uart_module_mode(false);
        if (!wait_for_uart_aux_ready(kUartCommandTimeoutMs)) {
            return ESP_ERR_TIMEOUT;
        }

        uart_flush_input(kLoRaUartPort);
        ESP_RETURN_ON_FALSE(uart_write_bytes(kLoRaUartPort,
                                             reinterpret_cast<const char *>(write_command.data()),
                                             write_command.size()) == static_cast<int>(write_command.size()),
                            ESP_FAIL,
                            kTag,
                            "e22 config write failed");
        ESP_RETURN_ON_ERROR(uart_wait_tx_done(kLoRaUartPort, pdMS_TO_TICKS(kUartCommandTimeoutMs)),
                            kTag,
                            "e22 config tx wait failed");
        ESP_RETURN_ON_FALSE(wait_for_uart_aux_ready(kUartCommandTimeoutMs),
                            ESP_ERR_TIMEOUT,
                            kTag,
                            "e22 config aux wait failed");

        std::array<uint8_t, 12> write_response = {};
        ESP_RETURN_ON_ERROR(uart_read_exact(write_response.data(), write_response.size(), kUartCommandTimeoutMs),
                            kTag,
                            "e22 config response timeout");

        std::array<uint8_t, 12> expected_write_response = write_command;
        expected_write_response[0] = kE22CommandReadRegisters;
        if (write_response != expected_write_response) {
            ESP_LOGW(kTag,
                     "E22 config write response differed expected=[%s] actual=[%s]",
                     format_hex_bytes(expected_write_response.data(), expected_write_response.size()).c_str(),
                     format_hex_bytes(write_response.data(), write_response.size()).c_str());
        }

        uart_flush_input(kLoRaUartPort);
        ESP_RETURN_ON_FALSE(uart_write_bytes(kLoRaUartPort,
                                             reinterpret_cast<const char *>(read_command.data()),
                                             read_command.size()) == static_cast<int>(read_command.size()),
                            ESP_FAIL,
                            kTag,
                            "e22 config readback request failed");
        ESP_RETURN_ON_ERROR(uart_wait_tx_done(kLoRaUartPort, pdMS_TO_TICKS(kUartCommandTimeoutMs)),
                            kTag,
                            "e22 config readback tx wait failed");

        std::array<uint8_t, 12> read_response = {};
        const esp_err_t readback_err = uart_read_exact(read_response.data(), read_response.size(), kUartCommandTimeoutMs);
        if (readback_err == ESP_OK) {
            if (read_response[0] != kE22CommandReadRegisters ||
                read_response[1] != kE22RegisterStart ||
                read_response[2] != kE22RegisterCount) {
                ESP_LOGW(kTag,
                         "E22 config readback header differed actual=[%s]",
                         format_hex_bytes(read_response.data(), read_response.size()).c_str());
            } else if (read_response != expected_write_response) {
                ESP_LOGW(kTag,
                         "E22 config readback differed desired=[%s] actual=[%s]",
                         format_hex_bytes(expected_write_response.data(), expected_write_response.size()).c_str(),
                         format_hex_bytes(read_response.data(), read_response.size()).c_str());
            }
        } else {
            ESP_LOGW(kTag, "E22 config readback unavailable err=%s", esp_err_to_name(readback_err));
        }

        ESP_LOGI(kTag,
                 "Synchronized E22-400T22S config freq=%" PRIu32 "Hz channel=%u reg0=0x%02X reg1=0x%02X option=0x%02X",
                 stored_state.settings.frequency_hz,
                 channel,
                 kE22Reg0Default,
                 kE22Reg1Default,
                 kE22OptionTransparentDefault);
        return ESP_OK;
    }

    esp_err_t spi_transfer(const uint8_t *tx_data, uint8_t *rx_data, size_t length)
    {
        if (spi == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }

        spi_transaction_t transaction = {};
        transaction.length = length * 8;
        transaction.tx_buffer = tx_data;
        transaction.rx_buffer = rx_data;
        return spi_device_transmit(spi, &transaction);
    }

    esp_err_t write_command(uint8_t command, const uint8_t *data, size_t length)
    {
        if (!wait_while_busy(100)) {
            return ESP_ERR_TIMEOUT;
        }

        HeapBuffer buffer = allocate_byte_buffer(length + 1);
        if (buffer == nullptr) {
            return ESP_ERR_NO_MEM;
        }
        std::memset(buffer.get(), 0, length + 1);
        buffer.get()[0] = command;
        if ((data != nullptr) && (length > 0)) {
            std::memcpy(buffer.get() + 1, data, length);
        }

        const esp_err_t err = spi_transfer(buffer.get(), nullptr, length + 1);
        if (err != ESP_OK) {
            return err;
        }
        return wait_while_busy(100) ? ESP_OK : ESP_ERR_TIMEOUT;
    }

    esp_err_t read_command(uint8_t command, uint8_t *data, size_t length)
    {
        if (!wait_while_busy(100)) {
            return ESP_ERR_TIMEOUT;
        }

        HeapBuffer tx = allocate_byte_buffer(length + 2);
        HeapBuffer rx = allocate_byte_buffer(length + 2);
        if ((tx == nullptr) || (rx == nullptr)) {
            return ESP_ERR_NO_MEM;
        }
        std::memset(tx.get(), 0, length + 2);
        std::memset(rx.get(), 0, length + 2);
        tx.get()[0] = command;
        const esp_err_t err = spi_transfer(tx.get(), rx.get(), length + 2);
        if (err != ESP_OK) {
            return err;
        }
        if (length > 0) {
            std::memcpy(data, rx.get() + 2, length);
        }
        return wait_while_busy(100) ? ESP_OK : ESP_ERR_TIMEOUT;
    }

    esp_err_t write_register(uint16_t address, const uint8_t *data, size_t length)
    {
        if (!wait_while_busy(100)) {
            return ESP_ERR_TIMEOUT;
        }

        HeapBuffer buffer = allocate_byte_buffer(length + 3);
        if (buffer == nullptr) {
            return ESP_ERR_NO_MEM;
        }
        std::memset(buffer.get(), 0, length + 3);
        buffer.get()[0] = SX126X_CMD_WRITE_REGISTER;
        buffer.get()[1] = static_cast<uint8_t>((address >> 8) & 0xFF);
        buffer.get()[2] = static_cast<uint8_t>(address & 0xFF);
        if ((data != nullptr) && (length > 0)) {
            std::memcpy(buffer.get() + 3, data, length);
        }

        const esp_err_t err = spi_transfer(buffer.get(), nullptr, length + 3);
        if (err != ESP_OK) {
            return err;
        }
        return wait_while_busy(100) ? ESP_OK : ESP_ERR_TIMEOUT;
    }

    esp_err_t write_buffer(const uint8_t *data, size_t length)
    {
        if (!wait_while_busy(100)) {
            return ESP_ERR_TIMEOUT;
        }

        HeapBuffer buffer = allocate_byte_buffer(length + 2);
        if (buffer == nullptr) {
            return ESP_ERR_NO_MEM;
        }
        std::memset(buffer.get(), 0, length + 2);
        buffer.get()[0] = SX126X_CMD_WRITE_BUFFER;
        buffer.get()[1] = 0x00;
        if ((data != nullptr) && (length > 0)) {
            std::memcpy(buffer.get() + 2, data, length);
        }

        const esp_err_t err = spi_transfer(buffer.get(), nullptr, length + 2);
        if (err != ESP_OK) {
            return err;
        }
        return wait_while_busy(100) ? ESP_OK : ESP_ERR_TIMEOUT;
    }

    esp_err_t read_register(uint16_t address, uint8_t *data, size_t length)
    {
        if (!wait_while_busy(100)) {
            return ESP_ERR_TIMEOUT;
        }

        HeapBuffer tx = allocate_byte_buffer(length + 4);
        HeapBuffer rx = allocate_byte_buffer(length + 4);
        if ((tx == nullptr) || (rx == nullptr)) {
            return ESP_ERR_NO_MEM;
        }
        std::memset(tx.get(), 0, length + 4);
        std::memset(rx.get(), 0, length + 4);
        tx.get()[0] = SX126X_CMD_READ_REGISTER;
        tx.get()[1] = static_cast<uint8_t>((address >> 8) & 0xFF);
        tx.get()[2] = static_cast<uint8_t>(address & 0xFF);
        const esp_err_t err = spi_transfer(tx.get(), rx.get(), length + 4);
        if (err != ESP_OK) {
            return err;
        }
        if (length > 0) {
            std::memcpy(data, rx.get() + 4, length);
        }
        return wait_while_busy(100) ? ESP_OK : ESP_ERR_TIMEOUT;
    }

    esp_err_t get_status(uint8_t &status)
    {
        if (!wait_while_busy(100)) {
            return ESP_ERR_TIMEOUT;
        }

        const uint8_t tx[2] = {SX126X_CMD_GET_STATUS, 0x00};
        uint8_t rx[2] = {};
        const esp_err_t err = spi_transfer(tx, rx, sizeof(tx));
        if (err != ESP_OK) {
            return err;
        }

        status = rx[1];
        return wait_while_busy(100) ? ESP_OK : ESP_ERR_TIMEOUT;
    }

    esp_err_t get_device_errors(uint16_t &errors)
    {
        if (!wait_while_busy(100)) {
            return ESP_ERR_TIMEOUT;
        }

        const uint8_t tx[4] = {SX126X_CMD_GET_DEVICE_ERRORS, 0x00, 0x00, 0x00};
        uint8_t rx[4] = {};
        const esp_err_t err = spi_transfer(tx, rx, sizeof(tx));
        if (err != ESP_OK) {
            return err;
        }

        errors = read_u16_be(rx + 2);
        return wait_while_busy(100) ? ESP_OK : ESP_ERR_TIMEOUT;
    }

    std::string describe_radio_errors(uint16_t errors) const
    {
        if (errors == 0U) {
            return "none";
        }

        std::ostringstream stream;
        bool first = true;
        auto append_flag = [&](const char *name) {
            if (!first) {
                stream << ',';
            }
            stream << name;
            first = false;
        };

        if ((errors & (1U << 0)) != 0U) {
            append_flag("RC64K");
        }
        if ((errors & (1U << 1)) != 0U) {
            append_flag("RC13M");
        }
        if ((errors & (1U << 2)) != 0U) {
            append_flag("PLL_CAL");
        }
        if ((errors & (1U << 3)) != 0U) {
            append_flag("ADC_CAL");
        }
        if ((errors & (1U << 4)) != 0U) {
            append_flag("IMG_CAL");
        }
        if ((errors & (1U << 5)) != 0U) {
            append_flag("XOSC_START");
        }
        if ((errors & (1U << 6)) != 0U) {
            append_flag("PLL_LOCK");
        }
        if ((errors & (1U << 8)) != 0U) {
            append_flag("PA_RAMP");
        }

        return first ? "unknown" : stream.str();
    }

    std::string capture_tx_diagnostics_locked()
    {
        uint8_t status = 0xFF;
        uint16_t errors = 0xFFFF;
        std::ostringstream stream;

        stream << "status=";
        if (get_status(status) == ESP_OK) {
            stream << "0x" << std::hex << std::uppercase << static_cast<unsigned>(status);
        } else {
            stream << "unavailable";
        }

        stream << " errors=";
        if (get_device_errors(errors) == ESP_OK) {
            stream << "0x" << std::hex << std::uppercase << errors << std::dec;
            stream << " [" << describe_radio_errors(errors) << "]";
            if (errors != 0U) {
                const uint8_t clear_errors[2] = {0x00, 0x00};
                (void)write_command(SX126X_CMD_CLEAR_DEVICE_ERRORS, clear_errors, sizeof(clear_errors));
            }
        } else {
            stream << "unavailable";
        }

        return stream.str();
    }

    void trim_conversation_locked()
    {
        if (conversation.size() > kMaxConversationMessages) {
            conversation.erase(conversation.begin(), conversation.begin() + (conversation.size() - kMaxConversationMessages));
        }
    }

    void push_conversation_locked(const std::string &text, const std::string &meta, bool outgoing)
    {
        conversation.push_back(ConversationEntry{.conversation_id = selected_conversation_id,
                                                 .text = text,
                                                 .meta = meta,
                                                 .outgoing = outgoing});
        trim_conversation_locked();
        conversation_dirty = true;
        target_list_dirty = true;
    }

    void push_conversation_locked(const std::string &conversation_id,
                                  const std::string &text,
                                  const std::string &meta,
                                  bool outgoing)
    {
        conversation.push_back(ConversationEntry{.conversation_id = conversation_id,
                                                 .text = text,
                                                 .meta = meta,
                                                 .outgoing = outgoing});
        trim_conversation_locked();
        conversation_dirty = true;
        target_list_dirty = true;
    }

    bool send_uart_self_test_probe_locked(std::string &failure_reason)
    {
        MeshPacket packet = {};
        packet.kind = PacketKind::Presence;
        packet.sender_id = stored_state.identity.device_id;
        packet.sender_name = stored_state.identity.display_name;
        packet.target_id = jc4880::lora_mesh::kBroadcastTargetId;
        packet.timestamp_ms = esp_timer_get_time() / 1000;
        packet.ttl = stored_state.settings.hop_limit;
        packet.public_key_hex = stored_state.identity.public_key_hex;
        if (send_packet_locked(packet, nullptr, std::string())) {
            return true;
        }

        std::ostringstream detail;
        detail << "send_probe_failed";
        if (!last_tx_diagnostic.empty()) {
            detail << ' ' << last_tx_diagnostic;
        } else {
            detail << " irq=0x" << std::hex << std::uppercase << last_tx_irq_status;
        }
        failure_reason = detail.str();
        return false;
    }

    bool try_self_test_swap_uart_pins_locked(std::string &detail)
    {
        if ((stored_state.settings.radio_module != RadioModule::E22_400T22S) ||
            (stored_state.settings.uart_tx_gpio == stored_state.settings.uart_rx_gpio)) {
            return false;
        }

        const int8_t original_tx_gpio = stored_state.settings.uart_tx_gpio;
        const int8_t original_rx_gpio = stored_state.settings.uart_rx_gpio;
        const int8_t swapped_tx_gpio = original_rx_gpio;
        const int8_t swapped_rx_gpio = original_tx_gpio;

        deinit_radio_locked();
        stored_state.settings.uart_tx_gpio = swapped_tx_gpio;
        stored_state.settings.uart_rx_gpio = swapped_rx_gpio;

        const esp_err_t init_err = init_radio_locked();
        if ((init_err == ESP_OK) && send_uart_self_test_probe_locked(detail)) {
            state_dirty = true;
            trace_event_locked("Self-test auto-corrected E22 UART pin map by swapping TX/RX");
            save_state_if_needed_locked();
            detail = std::string("recovered_by_swap tx=") + std::to_string(swapped_tx_gpio) +
                     " rx=" + std::to_string(swapped_rx_gpio);
            return true;
        }

        std::string recovery_detail = detail;
        if (recovery_detail.empty()) {
            recovery_detail = std::string("swap_init_err=") + esp_err_to_name(init_err);
        }

        deinit_radio_locked();
        stored_state.settings.uart_tx_gpio = original_tx_gpio;
        stored_state.settings.uart_rx_gpio = original_rx_gpio;
        const esp_err_t restore_err = init_radio_locked();
        if (restore_err != ESP_OK) {
            detail = recovery_detail + " restore_err=" + esp_err_to_name(restore_err);
        } else {
            detail = recovery_detail;
        }
        return false;
    }

    bool perform_self_test_locked(bool verbose)
    {
        const bool first_run = !self_test_ran;
        if (!radio_module_uses_spi(stored_state.settings.radio_module)) {
            const bool aux_ready = wait_for_uart_aux_ready(kUartAuxTimeoutMs);
            const bool uart_ok = uart_ready;
            std::string probe_detail;
            bool passed = aux_ready && uart_ok;
            if (passed) {
                passed = send_uart_self_test_probe_locked(probe_detail);
                if (!passed) {
                    std::string swap_detail;
                    if (try_self_test_swap_uart_pins_locked(swap_detail)) {
                        passed = true;
                        probe_detail = swap_detail;
                    } else if (!swap_detail.empty()) {
                        probe_detail += probe_detail.empty() ? swap_detail : std::string(" ") + swap_detail;
                    }
                }
            }
            std::ostringstream detail;
            detail << (passed ? "PASS" : "FAIL")
                   << " module=" << radio_module_name(stored_state.settings.radio_module)
                   << " uart=" << (uart_ok ? "ready" : "down")
                   << " aux=" << (has_pin(RadioPinRole::Aux) ? gpio_get_level(gpio_for_role(RadioPinRole::Aux)) : 1)
                   << " tx=" << static_cast<int>(stored_state.settings.uart_tx_gpio)
                   << " rx=" << static_cast<int>(stored_state.settings.uart_rx_gpio);
            if (!passed) {
                detail << " transparent UART module not ready";
            }
            if (!probe_detail.empty()) {
                detail << ' ' << probe_detail;
            }

            self_test_summary = detail.str();
            self_test_ran = true;
            self_test_ok = passed;
            status_text = passed ? "LoRa self-test passed" : "LoRa self-test failed";
            if (first_run || verbose) {
                push_conversation_locked(jc4880::lora_mesh::kCommonConversationId,
                                         passed ? "Self-test passed" : "Self-test failed",
                                         detail.str(),
                                         true);
            }
            if (verbose || !passed) {
                append_line_capped(log_text, std::string("[self-test] ") + detail.str());
            }
            return passed;
        }

        bool passed = true;
        bool cancelled = false;
        std::ostringstream summary;
        summary << "Self-test: ";

        auto fail = [&](const char *reason) {
            passed = false;
            summary << reason;
        };

        auto stop_if_requested = [&](const char *mode) {
            if (!self_test_stop_requested) {
                return false;
            }

            cancelled = true;
            passed = false;
            summary << "stopped during " << mode;
            return true;
        };

        uint8_t standby = 0x00;
        uint8_t status = 0;
        const uint8_t clear_errors[2] = {0x00, 0x00};
        uint16_t device_errors = 0xFFFF;
        const uint8_t expected_sync_word[] = {kRadioSyncWordMsb, kRadioSyncWordLsb};
        uint8_t sync_word[2] = {};
        const std::array<uint8_t, 8> pattern = {0x4C, 0x4F, 0x52, 0x41, 0xAA, 0x55, 0x5A, 0xC3};
        std::array<uint8_t, pattern.size()> echo = {};

        auto append_spi_diagnostics = [&]() {
            std::ostringstream diag;
            diag << "[self-test] spi status=0x" << std::hex << std::uppercase << static_cast<unsigned>(status)
                 << " errors=0x" << device_errors
                 << " sync=[" << format_hex_bytes(sync_word, sizeof(sync_word)) << "]"
                 << " echo=[" << format_hex_bytes(echo.data(), echo.size()) << "]"
                 << " expected=[" << format_hex_bytes(pattern.data(), pattern.size()) << "]"
                 << std::dec;
            if (all_bytes_equal(sync_word, sizeof(sync_word), 0xAA) && all_bytes_equal(echo.data(), echo.size(), 0xAA)) {
                diag << " signature=all-0xAA";
            } else if (all_bytes_equal(sync_word, sizeof(sync_word), 0xFF) && all_bytes_equal(echo.data(), echo.size(), 0xFF)) {
                diag << " signature=all-0xFF";
            }
            append_line_capped(log_text, diag.str());
            ESP_LOGI(kTag, "%s", diag.str().c_str());
        };

        set_self_test_mode_locked("checking BUSY line");
        if (!stop_if_requested("startup") && !wait_while_busy(200)) {
            if (self_test_stop_requested) {
                cancelled = true;
                summary << "stopped while waiting for BUSY";
            } else {
                fail("BUSY stuck high");
            }
        }

        if (passed) {
            set_self_test_mode_locked("sending standby command");
        }
        if (passed && (write_command(SX126X_CMD_SET_STANDBY, &standby, 1) != ESP_OK)) {
            fail("standby command failed");
        }

        if (passed) {
            set_self_test_mode_locked("reading radio status");
        }
        if (passed && (get_status(status) != ESP_OK)) {
            fail("status read failed");
        }

        if (passed) {
            set_self_test_mode_locked("clearing device errors");
        }
        if (passed && (write_command(SX126X_CMD_CLEAR_DEVICE_ERRORS, clear_errors, sizeof(clear_errors)) != ESP_OK)) {
            fail("clear errors failed");
        }

        if (passed) {
            set_self_test_mode_locked("reading device errors");
        }
        if (passed && (get_device_errors(device_errors) != ESP_OK)) {
            fail("device error read failed");
        }

        if (passed) {
            set_self_test_mode_locked("verifying sync word register");
        }
        if (passed && (write_register(kSyncWordRegister, expected_sync_word, sizeof(expected_sync_word)) != ESP_OK)) {
            fail("sync word write failed");
        }
        if (passed && (read_register(kSyncWordRegister, sync_word, sizeof(sync_word)) != ESP_OK)) {
            fail("sync word readback failed");
        }

        if (passed) {
            set_self_test_mode_locked("checking radio buffer echo");
        }
        if (passed && (write_buffer(pattern.data(), pattern.size()) != ESP_OK)) {
            fail("buffer write failed");
        }
        if (passed && (read_buffer(0x00, echo.data(), echo.size()) != ESP_OK)) {
            fail("buffer readback failed");
        }
        if (passed && (echo != pattern)) {
            fail("buffer mismatch");
        }

        if (passed && ((sync_word[0] != expected_sync_word[0]) || (sync_word[1] != expected_sync_word[1]))) {
            fail("sync word mismatch");
        }

        if (passed && (device_errors != 0x0000U)) {
            fail("radio reported device errors");
        }

        if (passed && ((status == 0x00U) || (status == 0xFFU))) {
            fail("invalid status byte");
        }

        if (verbose || !passed) {
            append_spi_diagnostics();
        }

        if (passed) {
            set_self_test_mode_locked("restoring receive mode");
        }
        if (start_receive_locked() != ESP_OK) {
            passed = false;
            if (summary.tellp() <= static_cast<std::streampos>(11)) {
                if (self_test_stop_requested) {
                    cancelled = true;
                    summary << "stopped while restoring RX";
                } else {
                    summary << "failed to restore RX";
                }
            }
        }

        if (cancelled) {
            self_test_summary = "Mode: stopped";
            self_test_ran = false;
            self_test_ok = false;
            status_text = "LoRa self-test stopped";
            append_line_capped(log_text, std::string("[self-test] STOPPED ") + summary.str().substr(11));
            ESP_LOGI(kTag, "Self-test stopped: %s", summary.str().c_str());
            return false;
        }

        std::ostringstream detail;
        detail << (passed ? "PASS" : "FAIL")
               << " status=0x" << std::hex << std::uppercase << static_cast<unsigned>(status)
               << " errors=0x" << device_errors
               << " sync=0x" << static_cast<unsigned>(sync_word[0]) << static_cast<unsigned>(sync_word[1])
             << std::dec
                         << " busy=" << gpio_get_level(gpio_for_role(RadioPinRole::Busy))
                         << " dio1=" << gpio_get_level(gpio_for_role(RadioPinRole::Dio1));
        if (!passed) {
            detail << " " << summary.str().substr(11);
        }

        self_test_summary = detail.str();
        self_test_ran = true;
        self_test_ok = passed;
        status_text = passed ? "LoRa self-test passed" : "LoRa self-test failed";
        if (first_run || verbose) {
            push_conversation_locked(jc4880::lora_mesh::kCommonConversationId,
                                     passed ? "Self-test passed" : "Self-test failed",
                                     detail.str(),
                                     true);
        }
        if (verbose || !passed) {
            append_line_capped(log_text, std::string("[self-test] ") + detail.str());
        }
        ESP_LOGI(kTag, "Self-test result: %s", detail.str().c_str());

        return passed;
    }

    bool run_self_test(bool verbose)
    {
        lock();
        if (self_test_stop_requested) {
            self_test_summary = "Mode: stopped before start";
            status_text = "LoRa self-test stopped";
            unlock();
            return false;
        }
        set_self_test_mode_locked("ensuring radio is ready");
        unlock();

        if (!ensure_radio_ready()) {
            lock();
            if (self_test_stop_requested) {
                self_test_summary = "Mode: stopped while preparing radio";
                status_text = "LoRa self-test stopped";
            }
            unlock();
            return false;
        }

        lock();
        const bool passed = perform_self_test_locked(verbose);
        unlock();
        return passed;
    }

    esp_err_t read_buffer(uint8_t offset, uint8_t *data, size_t length)
    {
        if (!wait_while_busy(100)) {
            return ESP_ERR_TIMEOUT;
        }

        HeapBuffer tx = allocate_byte_buffer(length + 3);
        HeapBuffer rx = allocate_byte_buffer(length + 3);
        if ((tx == nullptr) || (rx == nullptr)) {
            return ESP_ERR_NO_MEM;
        }
        std::memset(tx.get(), 0, length + 3);
        std::memset(rx.get(), 0, length + 3);
        tx.get()[0] = SX126X_CMD_READ_BUFFER;
        tx.get()[1] = offset;
        const esp_err_t err = spi_transfer(tx.get(), rx.get(), length + 3);
        if (err != ESP_OK) {
            return err;
        }
        if (length > 0) {
            std::memcpy(data, rx.get() + 3, length);
        }
        return wait_while_busy(100) ? ESP_OK : ESP_ERR_TIMEOUT;
    }

    esp_err_t reset_radio()
    {
        if (has_pin(RadioPinRole::Reset)) {
            gpio_set_level(gpio_for_role(RadioPinRole::Reset), 0);
            vTaskDelay(pdMS_TO_TICKS(10));
            gpio_set_level(gpio_for_role(RadioPinRole::Reset), 1);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (radio_module_uses_spi(stored_state.settings.radio_module)) {
            return wait_while_busy(100) ? ESP_OK : ESP_ERR_TIMEOUT;
        }
        return wait_for_uart_aux_ready(kUartAuxTimeoutMs) ? ESP_OK : ESP_ERR_TIMEOUT;
    }

    esp_err_t set_frequency(uint32_t frequency_hz)
    {
        const uint32_t frf = static_cast<uint32_t>((static_cast<uint64_t>(frequency_hz) << 25) / 32000000ULL);
        uint8_t args[4] = {
            static_cast<uint8_t>((frf >> 24) & 0xFF),
            static_cast<uint8_t>((frf >> 16) & 0xFF),
            static_cast<uint8_t>((frf >> 8) & 0xFF),
            static_cast<uint8_t>(frf & 0xFF),
        };
        return write_command(SX126X_CMD_SET_RF_FREQUENCY, args, sizeof(args));
    }

    esp_err_t start_receive_locked()
    {
        if (!radio_module_uses_spi(stored_state.settings.radio_module)) {
            set_uart_module_mode(true);
            return wait_for_uart_aux_ready(kUartAuxTimeoutMs) ? ESP_OK : ESP_ERR_TIMEOUT;
        }
        const uint8_t standby = SX126X_STANDBY_XOSC;
        ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_STANDBY, &standby, 1), kTag, "rx standby failed");
        esp_rom_delay_us(500);
        set_rf_path(false);
        const uint8_t packet_params[] = {0x00, 0x08, 0x00, 0xFF, 0x01, 0x00};
        ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_PACKET_PARAMS, packet_params, sizeof(packet_params)), kTag, "rx packet params failed");
        uint8_t clear_irq[2] = {0xFF, 0xFF};
        ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_CLEAR_IRQ_STATUS, clear_irq, sizeof(clear_irq)), kTag, "clear irq failed");
        const uint8_t rx_timeout[3] = {0xFF, 0xFF, 0xFF};
        return write_command(SX126X_CMD_SET_RX, rx_timeout, sizeof(rx_timeout));
    }

    esp_err_t stop_receive_locked()
    {
        if (!radio_module_uses_spi(stored_state.settings.radio_module)) {
            set_uart_module_mode(true);
            return wait_for_uart_aux_ready(kUartAuxTimeoutMs) ? ESP_OK : ESP_ERR_TIMEOUT;
        }

        const uint8_t standby = SX126X_STANDBY_XOSC;
        ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_STANDBY, &standby, 1), kTag, "stop rx standby failed");
        const uint8_t clear_irq[2] = {0xFF, 0xFF};
        return write_command(SX126X_CMD_CLEAR_IRQ_STATUS, clear_irq, sizeof(clear_irq));
    }

    esp_err_t init_radio_locked()
    {
        if (radio_module_uses_spi(stored_state.settings.radio_module)) {
            suspend_conflicting_neopixel_if_needed();
            reset_active_radio_gpios();

            ESP_LOGI(kTag, "LoRa pin owner guard: USE_ETHERNET_RMII=%d USE_CSI_CAMERA=%d", USE_ETHERNET_RMII, USE_CSI_CAMERA);

            ESP_LOGI(kTag,
                     "LoRa SPI pin map MISO=%d MOSI=%d SCK=%d NSS=%d DIO1=%d BUSY=%d NRST=%d TXEN=%d RXEN=%d expected_e22=%s",
                     gpio_for_role(RadioPinRole::SpiMiso),
                     gpio_for_role(RadioPinRole::SpiMosi),
                     gpio_for_role(RadioPinRole::SpiSck),
                     gpio_for_role(RadioPinRole::SpiNss),
                     gpio_for_role(RadioPinRole::Dio1),
                     gpio_for_role(RadioPinRole::Busy),
                     gpio_for_role(RadioPinRole::Reset),
                     gpio_for_role(RadioPinRole::TxEnable),
                     gpio_for_role(RadioPinRole::RxEnable),
                     stored_state.settings.radio_module == RadioModule::E22_400M22S ? "yes" : "no");

            uint64_t output_mask = 0;
            output_mask |= (1ULL << gpio_for_role(RadioPinRole::SpiNss));
            output_mask |= (1ULL << gpio_for_role(RadioPinRole::SpiMosi));
            output_mask |= (1ULL << gpio_for_role(RadioPinRole::SpiSck));
            output_mask |= (1ULL << gpio_for_role(RadioPinRole::Reset));
            output_mask |= (1ULL << gpio_for_role(RadioPinRole::TxEnable));
            output_mask |= (1ULL << gpio_for_role(RadioPinRole::RxEnable));

            gpio_config_t output_config = {};
            output_config.pin_bit_mask = output_mask;
            output_config.mode = GPIO_MODE_OUTPUT;
            output_config.pull_up_en = GPIO_PULLUP_DISABLE;
            output_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
            output_config.intr_type = GPIO_INTR_DISABLE;
            ESP_RETURN_ON_ERROR(gpio_config(&output_config), kTag, "output gpio config failed");

            gpio_set_level(gpio_for_role(RadioPinRole::SpiNss), 1);
            gpio_set_level(gpio_for_role(RadioPinRole::SpiMosi), 0);
            gpio_set_level(gpio_for_role(RadioPinRole::SpiSck), 0);

            gpio_config_t input_config = {};
            input_config.pin_bit_mask = (1ULL << gpio_for_role(RadioPinRole::SpiMiso)) |
                                        (1ULL << gpio_for_role(RadioPinRole::Busy)) |
                                        (1ULL << gpio_for_role(RadioPinRole::Dio1));
            input_config.mode = GPIO_MODE_INPUT;
            input_config.pull_up_en = GPIO_PULLUP_DISABLE;
            input_config.pull_down_en = GPIO_PULLDOWN_ENABLE;
            input_config.intr_type = GPIO_INTR_DISABLE;
            ESP_RETURN_ON_ERROR(gpio_config(&input_config), kTag, "input gpio config failed");

            ESP_LOGI(kTag,
                     "LoRa pre-init GPIO snapshot NSS=%d MOSI=%d SCK=%d NRST=%d TXEN=%d RXEN=%d MISO=%d BUSY=%d DIO1=%d",
                     gpio_get_level(gpio_for_role(RadioPinRole::SpiNss)),
                     gpio_get_level(gpio_for_role(RadioPinRole::SpiMosi)),
                     gpio_get_level(gpio_for_role(RadioPinRole::SpiSck)),
                     gpio_get_level(gpio_for_role(RadioPinRole::Reset)),
                     gpio_get_level(gpio_for_role(RadioPinRole::TxEnable)),
                     gpio_get_level(gpio_for_role(RadioPinRole::RxEnable)),
                     gpio_get_level(gpio_for_role(RadioPinRole::SpiMiso)),
                     gpio_get_level(gpio_for_role(RadioPinRole::Busy)),
                     gpio_get_level(gpio_for_role(RadioPinRole::Dio1)));

            set_rf_idle();
            gpio_set_level(gpio_for_role(RadioPinRole::Reset), 1);

            if (!spi_bus_ready) {
                spi_bus_config_t bus_config = {};
                bus_config.miso_io_num = gpio_for_role(RadioPinRole::SpiMiso);
                bus_config.mosi_io_num = gpio_for_role(RadioPinRole::SpiMosi);
                bus_config.sclk_io_num = gpio_for_role(RadioPinRole::SpiSck);
                bus_config.quadwp_io_num = -1;
                bus_config.quadhd_io_num = -1;
                bus_config.max_transfer_sz = 512;
                ESP_RETURN_ON_ERROR(spi_bus_initialize(kLoRaSpiHost, &bus_config, SPI_DMA_CH_AUTO), kTag, "spi bus init failed");
                spi_bus_ready = true;
            }

            if (spi == nullptr) {
                spi_device_interface_config_t device_config = {};
                device_config.clock_speed_hz = kSpiClockHz;
                device_config.mode = 0;
                device_config.spics_io_num = gpio_for_role(RadioPinRole::SpiNss);
                device_config.queue_size = 1;
                ESP_RETURN_ON_ERROR(spi_bus_add_device(kLoRaSpiHost, &device_config, &spi), kTag, "spi add device failed");
            }

            ESP_RETURN_ON_ERROR(reset_radio(), kTag, "radio reset failed");

            uint8_t arg = SX126X_STANDBY_RC;
            ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_STANDBY, &arg, 1), kTag, "standby failed");

            if (stored_state.settings.radio_module == RadioModule::E22_400M22S) {
                const uint8_t tcxo_mode[] = {
                    SX126X_TCXO_CTRL_1P7V,
                    static_cast<uint8_t>((kTcxoSetupSteps >> 16) & 0xFF),
                    static_cast<uint8_t>((kTcxoSetupSteps >> 8) & 0xFF),
                    static_cast<uint8_t>(kTcxoSetupSteps & 0xFF),
                };
                ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_TCXO_MODE, tcxo_mode, sizeof(tcxo_mode)), kTag, "tcxo setup failed");

                const uint8_t calibration = SX126X_CALIBRATION_ALL;
                ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_CALIBRATION, &calibration, 1), kTag, "calibration failed");
            }

            arg = 0x00;
            ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_REGULATOR_MODE, &arg, 1), kTag, "regulator setup failed");

            const uint8_t calib[] = {0x6B, 0x6F};
            ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_CALIBRATE_IMAGE, calib, sizeof(calib)), kTag, "image calibration failed");

            arg = 0x01;
            ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_PACKET_TYPE, &arg, 1), kTag, "packet type failed");
            ESP_RETURN_ON_ERROR(set_frequency(stored_state.settings.frequency_hz), kTag, "frequency setup failed");

            const uint8_t pa_config[] = {0x04, 0x07, 0x00, 0x01};
            ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_PA_CONFIG, pa_config, sizeof(pa_config)), kTag, "pa config failed");

            const uint8_t tx_params[] = {22, 0x04};
            ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_TX_PARAMS, tx_params, sizeof(tx_params)), kTag, "tx params failed");

            const uint8_t modulation_params[] = {
                stored_state.settings.spreading_factor,
                stored_state.settings.bandwidth,
                stored_state.settings.coding_rate,
                0x00,
            };
            ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_MODULATION_PARAMS, modulation_params, sizeof(modulation_params)), kTag, "modulation failed");

            const uint8_t packet_params[] = {0x00, 0x08, 0x00, 0xFF, 0x01, 0x00};
            ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_PACKET_PARAMS, packet_params, sizeof(packet_params)), kTag, "packet params failed");

            const uint8_t base_address[] = {0x00, 0x00};
            ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_BUFFER_BASE_ADDRESS, base_address, sizeof(base_address)), kTag, "buffer base failed");

            const uint8_t irq_params[] = {
                static_cast<uint8_t>(((SX126X_IRQ_TX_DONE | SX126X_IRQ_RX_DONE | SX126X_IRQ_HEADER_ERR | SX126X_IRQ_CRC_ERR | SX126X_IRQ_TIMEOUT) >> 8) & 0xFF),
                static_cast<uint8_t>((SX126X_IRQ_TX_DONE | SX126X_IRQ_RX_DONE | SX126X_IRQ_HEADER_ERR | SX126X_IRQ_CRC_ERR | SX126X_IRQ_TIMEOUT) & 0xFF),
                static_cast<uint8_t>(((SX126X_IRQ_TX_DONE | SX126X_IRQ_RX_DONE | SX126X_IRQ_HEADER_ERR | SX126X_IRQ_CRC_ERR | SX126X_IRQ_TIMEOUT) >> 8) & 0xFF),
                static_cast<uint8_t>((SX126X_IRQ_TX_DONE | SX126X_IRQ_RX_DONE | SX126X_IRQ_HEADER_ERR | SX126X_IRQ_CRC_ERR | SX126X_IRQ_TIMEOUT) & 0xFF),
                0x00,
                0x00,
                0x00,
                0x00,
            };
            ESP_RETURN_ON_ERROR(write_command(SX126X_CMD_SET_DIO_IRQ_PARAMS, irq_params, sizeof(irq_params)), kTag, "irq config failed");

            const uint8_t sync_word[] = {kRadioSyncWordMsb, kRadioSyncWordLsb};
            ESP_RETURN_ON_ERROR(write_register(0x0740, sync_word, sizeof(sync_word)), kTag, "sync word failed");

            ESP_RETURN_ON_ERROR(start_receive_locked(), kTag, "start receive failed");
            radio_ready = true;
            return ESP_OK;
        }

        uint64_t output_mask = 0;
        output_mask |= (1ULL << gpio_for_role(RadioPinRole::Mode0));
        output_mask |= (1ULL << gpio_for_role(RadioPinRole::Mode1));
        if (has_pin(RadioPinRole::Reset)) {
            output_mask |= (1ULL << gpio_for_role(RadioPinRole::Reset));
        }
        gpio_config_t output_config = {};
        output_config.pin_bit_mask = output_mask;
        output_config.mode = GPIO_MODE_OUTPUT;
        output_config.pull_up_en = GPIO_PULLUP_DISABLE;
        output_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        output_config.intr_type = GPIO_INTR_DISABLE;
        ESP_RETURN_ON_ERROR(gpio_config(&output_config), kTag, "uart output gpio config failed");

        if (has_pin(RadioPinRole::Aux)) {
            gpio_config_t input_config = {};
            input_config.pin_bit_mask = (1ULL << gpio_for_role(RadioPinRole::Aux));
            input_config.mode = GPIO_MODE_INPUT;
            input_config.pull_up_en = GPIO_PULLUP_DISABLE;
            input_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
            input_config.intr_type = GPIO_INTR_DISABLE;
            ESP_RETURN_ON_ERROR(gpio_config(&input_config), kTag, "uart aux gpio config failed");
        }

        if (!uart_ready) {
            uart_config_t config = {};
            config.baud_rate = kUartBaudRate;
            config.data_bits = UART_DATA_8_BITS;
            config.parity = UART_PARITY_DISABLE;
            config.stop_bits = UART_STOP_BITS_1;
            config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
            config.source_clk = UART_SCLK_DEFAULT;
            ESP_RETURN_ON_ERROR(uart_driver_install(kLoRaUartPort, 1024, 0, 0, nullptr, 0), kTag, "uart driver install failed");
            uart_ready = true;
            ESP_RETURN_ON_ERROR(uart_param_config(kLoRaUartPort, &config), kTag, "uart param config failed");
        }

        const int8_t configured_tx_gpio = stored_state.settings.uart_tx_gpio;
        const int8_t configured_rx_gpio = stored_state.settings.uart_rx_gpio;
        auto try_uart_pin_pair = [&](int8_t tx_gpio, int8_t rx_gpio) -> esp_err_t {
            ESP_RETURN_ON_ERROR(uart_set_pin(kLoRaUartPort,
                                             tx_gpio,
                                             rx_gpio,
                                             UART_PIN_NO_CHANGE,
                                             UART_PIN_NO_CHANGE),
                                kTag,
                                "uart pin config failed");

            ESP_LOGI(kTag,
                     "LoRa UART pin map TX=%d RX=%d AUX=%d M0=%d M1=%d NRST=%d module=%s",
                     tx_gpio,
                     rx_gpio,
                     has_pin(RadioPinRole::Aux) ? gpio_for_role(RadioPinRole::Aux) : -1,
                     has_pin(RadioPinRole::Mode0) ? gpio_for_role(RadioPinRole::Mode0) : -1,
                     has_pin(RadioPinRole::Mode1) ? gpio_for_role(RadioPinRole::Mode1) : -1,
                     has_pin(RadioPinRole::Reset) ? gpio_for_role(RadioPinRole::Reset) : -1,
                     radio_module_name(stored_state.settings.radio_module));

            set_uart_module_mode(true);
            if (has_pin(RadioPinRole::Reset)) {
                gpio_set_level(gpio_for_role(RadioPinRole::Reset), 1);
            }
            ESP_RETURN_ON_ERROR(reset_radio(), kTag, "uart radio reset failed");
            return sync_e22_400t22s_config_locked();
        };

        esp_err_t uart_sync_err = try_uart_pin_pair(configured_tx_gpio, configured_rx_gpio);
        if ((uart_sync_err != ESP_OK) &&
            (stored_state.settings.radio_module == RadioModule::E22_400T22S) &&
            (configured_tx_gpio != configured_rx_gpio)) {
            ESP_LOGW(kTag,
                     "E22 config sync failed on configured UART pair TX=%d RX=%d err=%s; retrying swapped pair",
                     configured_tx_gpio,
                     configured_rx_gpio,
                     esp_err_to_name(uart_sync_err));
            const esp_err_t swapped_err = try_uart_pin_pair(configured_rx_gpio, configured_tx_gpio);
            if (swapped_err == ESP_OK) {
                stored_state.settings.uart_tx_gpio = configured_rx_gpio;
                stored_state.settings.uart_rx_gpio = configured_tx_gpio;
                state_dirty = true;
                trace_event("Auto-corrected E22 UART pin map by swapping TX/RX");
                uart_sync_err = ESP_OK;
            } else {
                ESP_LOGW(kTag,
                         "E22 swapped UART pair TX=%d RX=%d also failed err=%s",
                         configured_rx_gpio,
                         configured_tx_gpio,
                         esp_err_to_name(swapped_err));
            }
        }
        if ((uart_sync_err == ESP_ERR_TIMEOUT) &&
            (stored_state.settings.radio_module == RadioModule::E22_400T22S)) {
            ESP_LOGW(kTag,
                     "E22 config sync unavailable on UART; continuing with existing pin map TX=%d RX=%d",
                     stored_state.settings.uart_tx_gpio,
                     stored_state.settings.uart_rx_gpio);
            trace_event("E22 config sync unavailable; using existing UART settings");
            uart_sync_err = ESP_OK;
        }
        ESP_RETURN_ON_ERROR(uart_sync_err, kTag, "e22 config sync failed");
        set_uart_module_mode(true);
        ESP_RETURN_ON_FALSE(wait_for_uart_aux_ready(kUartCommandTimeoutMs), ESP_ERR_TIMEOUT, kTag, "uart normal mode wait failed");
        uart_flush_input(kLoRaUartPort);
        uart_rx_bytes.clear();
        ESP_RETURN_ON_ERROR(start_receive_locked(), kTag, "uart receive prep failed");
        radio_ready = true;
        return ESP_OK;
    }

    void deinit_radio_locked()
    {
        radio_ready = false;
        set_rf_idle();
        uart_rx_bytes.clear();
        if (spi != nullptr) {
            spi_bus_remove_device(spi);
            spi = nullptr;
        }
        if (spi_bus_ready) {
            spi_bus_free(kLoRaSpiHost);
            spi_bus_ready = false;
        }
        if (uart_ready) {
            uart_driver_delete(kLoRaUartPort);
            uart_ready = false;
        }
        resume_suspended_neopixel_if_needed();
    }

    bool ensure_radio_ready()
    {
        lock();
        if (!stored_state.settings.radio_enabled) {
            status_text = "Device disabled. Please enable radio in Device Settings.";
            append_line_capped(log_text, "Device disabled. Please enable radio in Device Settings.");
            unlock();
            return false;
        }
        if (radio_ready) {
            unlock();
            return true;
        }
        unlock();

        const esp_err_t err = init_radio_locked();
        if (err != ESP_OK) {
            lock();
            if (self_test_running && self_test_stop_requested) {
                self_test_summary = "Mode: stopped while preparing radio";
                status_text = "LoRa self-test stopped";
                unlock();
                return false;
            }
            const std::string error_text = std::string("LoRa init failed: ") + esp_err_to_name(err);
            status_text = error_text;
            append_line_capped(log_text, error_text);
            unlock();
            return false;
        }

        bool send_hello = false;
        lock();
        status_text = "LoRa radio ready on configured mesh channel";
        append_line_capped(log_text, "Radio ready; listening for mesh traffic");
        if (!hello_sent) {
            hello_sent = true;
            send_hello = true;
        }
        save_state_if_needed_locked();
        unlock();

        if (send_hello) {
            (void)send_presence_without_mutex(true);
        }

        if (rx_task == nullptr) {
            rx_task_stop = false;
            if (!create_task_prefer_psram(&Impl::rx_task_entry, "lora_mesh_rx", kRxTaskStack, this, kRxTaskPriority, &rx_task)) {
                set_status("Failed to start LoRa receive task");
                return false;
            }
        }

        return true;
    }

    static void rx_task_entry(void *context)
    {
        auto *impl = static_cast<Impl *>(context);
        if (impl != nullptr) {
            impl->rx_task_loop();
        }
        vTaskDelete(nullptr);
    }

    void remember_frame(uint32_t origin, uint32_t sequence)
    {
        recent_frames[recent_index] = RecentFrame{origin, sequence};
        recent_index = (recent_index + 1) % recent_frames.size();
    }

    bool has_seen_frame(uint32_t origin, uint32_t sequence) const
    {
        return std::any_of(recent_frames.begin(), recent_frames.end(), [origin, sequence](const RecentFrame &entry) {
            return (entry.origin == origin) && (entry.sequence == sequence);
        });
    }

    bool encode_frame(const MeshFrame &frame, std::vector<uint8_t> &bytes) const
    {
        if (frame.payload.empty() || (frame.payload.size() > kMaxPayloadBytes)) {
            return false;
        }

        bytes.assign(18 + frame.payload.size(), 0);
        bytes[0] = kMeshMagic0;
        bytes[1] = kMeshMagic1;
        bytes[2] = kMeshVersion;
        bytes[3] = frame.ttl;
        bytes[4] = frame.flags;
        bytes[5] = 0x00;
        write_u32_be(bytes.data() + 6, frame.origin);
        write_u32_be(bytes.data() + 10, frame.sender);
        write_u32_be(bytes.data() + 14, frame.sequence);
        write_u16_be(bytes.data() + 18 - 2, static_cast<uint16_t>(frame.payload.size()));
        std::memcpy(bytes.data() + 18, frame.payload.data(), frame.payload.size());
        return true;
    }

    bool decode_frame(const uint8_t *data, size_t length, MeshFrame &frame) const
    {
        if ((data == nullptr) || (length < 18) || (data[0] != kMeshMagic0) || (data[1] != kMeshMagic1) || (data[2] != kMeshVersion)) {
            return false;
        }

        const uint16_t payload_length = read_u16_be(data + 16);
        if ((18U + payload_length) != length || (payload_length == 0) || (payload_length > kMaxPayloadBytes)) {
            return false;
        }

        frame.ttl = data[3];
        frame.flags = data[4];
        frame.origin = read_u32_be(data + 6);
        frame.sender = read_u32_be(data + 10);
        frame.sequence = read_u32_be(data + 14);
        frame.payload.assign(reinterpret_cast<const char *>(data + 18), payload_length);
        return true;
    }

    void process_uart_rx_bytes_locked()
    {
        while (uart_rx_bytes.size() >= 18) {
            auto magic = uart_rx_bytes.begin();
            while ((magic != uart_rx_bytes.end()) &&
                   ((*magic != kMeshMagic0) || ((magic + 1) == uart_rx_bytes.end()) || (*(magic + 1) != kMeshMagic1))) {
                ++magic;
            }
            if ((magic == uart_rx_bytes.end()) || ((magic + 1) == uart_rx_bytes.end())) {
                uart_rx_bytes.clear();
                return;
            }
            if (magic != uart_rx_bytes.begin()) {
                uart_rx_bytes.erase(uart_rx_bytes.begin(), magic);
            }
            if (uart_rx_bytes.size() < 18) {
                return;
            }

            const uint16_t payload_length = read_u16_be(uart_rx_bytes.data() + 16);
            const size_t frame_length = 18U + payload_length;
            if ((payload_length == 0U) || (payload_length > kMaxPayloadBytes)) {
                uart_rx_bytes.erase(uart_rx_bytes.begin());
                ++drop_count;
                continue;
            }
            if (uart_rx_bytes.size() < frame_length) {
                return;
            }

            MeshFrame frame = {};
            if (decode_frame(uart_rx_bytes.data(), frame_length, frame) && is_printable_text(frame.payload)) {
                last_rssi = 0;
                last_snr = 0;
                handle_incoming_frame_locked(frame);
            } else {
                ++drop_count;
                append_line_capped(log_text, "Dropped invalid UART mesh payload");
            }
            uart_rx_bytes.erase(uart_rx_bytes.begin(), uart_rx_bytes.begin() + static_cast<std::ptrdiff_t>(frame_length));
        }
    }

    bool transmit_frame_locked(const MeshFrame &frame)
    {
        last_tx_irq_status = 0;
        last_tx_diagnostic.clear();
        std::vector<uint8_t> bytes;
        if (!encode_frame(frame, bytes)) {
            last_tx_diagnostic = std::string("payload_len=") + std::to_string(frame.payload.size()) +
                                 " max_payload=" + std::to_string(kMaxPayloadBytes);
            return false;
        }
        if (bytes.size() > 0xFF) {
            last_tx_diagnostic = std::string("frame_len=") + std::to_string(bytes.size()) + " max_frame=255";
            return false;
        }

        if (!radio_module_uses_spi(stored_state.settings.radio_module)) {
            set_uart_module_mode(true);
            if (!wait_for_uart_aux_ready(kUartAuxTimeoutMs)) {
                last_tx_diagnostic = std::string("uart_aux_busy_before_tx len=") + std::to_string(bytes.size());
                return false;
            }
            const int written = uart_write_bytes(kLoRaUartPort,
                                                 reinterpret_cast<const char *>(bytes.data()),
                                                 static_cast<uint32_t>(bytes.size()));
            if (written != static_cast<int>(bytes.size())) {
                last_tx_diagnostic = std::string("uart_write_short written=") + std::to_string(written) +
                                     " expected=" + std::to_string(bytes.size());
                return false;
            }
            esp_rom_delay_us(kUartFrameGapMs * 1000U);
            if (!wait_for_uart_aux_ready(kUartTxCompleteTimeoutMs)) {
                last_tx_diagnostic = std::string("uart_aux_timeout_after_tx len=") + std::to_string(bytes.size()) +
                                     " timeout_ms=" + std::to_string(kUartTxCompleteTimeoutMs);
                return false;
            }
            last_tx_irq_status = 0xFFFF;
            return true;
        }

        const uint8_t standby = SX126X_STANDBY_XOSC;
        if (write_command(SX126X_CMD_SET_STANDBY, &standby, 1) != ESP_OK) {
            last_tx_diagnostic = capture_tx_diagnostics_locked();
            set_rf_idle();
            return false;
        }
        esp_rom_delay_us(500);

        set_rf_path(true);
        esp_rom_delay_us(250);
        const uint8_t packet_params[] = {0x00, 0x08, 0x00, static_cast<uint8_t>(bytes.size()), 0x01, 0x00};
        if (write_command(SX126X_CMD_SET_PACKET_PARAMS, packet_params, sizeof(packet_params)) != ESP_OK) {
            last_tx_diagnostic = capture_tx_diagnostics_locked();
            set_rf_idle();
            return false;
        }
        const uint8_t clear_irq[] = {0xFF, 0xFF};
        if (write_command(SX126X_CMD_CLEAR_IRQ_STATUS, clear_irq, sizeof(clear_irq)) != ESP_OK) {
            last_tx_diagnostic = capture_tx_diagnostics_locked();
            set_rf_idle();
            return false;
        }
        if (write_buffer(bytes.data(), bytes.size()) != ESP_OK) {
            last_tx_diagnostic = capture_tx_diagnostics_locked();
            set_rf_idle();
            return false;
        }

        const uint8_t tx_timeout[] = {
            static_cast<uint8_t>((kTxTimeoutSteps >> 16) & 0xFF),
            static_cast<uint8_t>((kTxTimeoutSteps >> 8) & 0xFF),
            static_cast<uint8_t>(kTxTimeoutSteps & 0xFF),
        };
        if (write_command(SX126X_CMD_SET_TX, tx_timeout, sizeof(tx_timeout)) != ESP_OK) {
            last_tx_diagnostic = capture_tx_diagnostics_locked();
            set_rf_idle();
            return false;
        }

        const int64_t deadline = esp_timer_get_time() + kTxWaitDeadlineUs;
        uint16_t irq_status = 0;
        bool saw_uniform_invalid_irq = false;
        std::string uniform_invalid_irq_diagnostic;
        int64_t invalid_irq_deadline = 0;
        while (irq_status == 0) {
            if (esp_timer_get_time() >= deadline) {
                if (saw_uniform_invalid_irq) {
                    last_tx_irq_status = 0;
                    last_tx_diagnostic = uniform_invalid_irq_diagnostic;
                    if (start_receive_locked() != ESP_OK) {
                        set_rf_idle();
                        return false;
                    }
                    return true;
                }
                last_tx_diagnostic = capture_tx_diagnostics_locked();
                set_rf_idle();
                return false;
            }

            uint8_t irq_bytes[2] = {};
            if (read_command(SX126X_CMD_GET_IRQ_STATUS, irq_bytes, sizeof(irq_bytes)) != ESP_OK) {
                last_tx_diagnostic = capture_tx_diagnostics_locked();
                set_rf_idle();
                return false;
            }
            if (is_invalid_uniform_spi_signature(irq_bytes, sizeof(irq_bytes))) {
                const int64_t now = esp_timer_get_time();
                saw_uniform_invalid_irq = true;
                last_tx_irq_status = read_u16_be(irq_bytes);
                if (invalid_irq_deadline == 0) {
                    invalid_irq_deadline = now + kInvalidIrqAssumeTxUs;
                }
                std::ostringstream stream;
                stream << "invalid_irq_signature=" << format_hex_bytes(irq_bytes, sizeof(irq_bytes));
                if (is_uniform_spi_signature_value(irq_bytes, sizeof(irq_bytes), 0xAA)) {
                    stream << " assuming_tx_after_timeout=all-0xAA";
                } else if (is_uniform_spi_signature_value(irq_bytes, sizeof(irq_bytes), 0xFF)) {
                    stream << " assuming_tx_after_timeout=all-0xFF";
                } else {
                    stream << " assuming_tx_after_timeout=uniform";
                }
                const std::string capture = capture_tx_diagnostics_locked();
                if (!capture.empty()) {
                    stream << ' ' << capture;
                }
                uniform_invalid_irq_diagnostic = stream.str();
                if (now >= invalid_irq_deadline) {
                    last_tx_irq_status = 0;
                    last_tx_diagnostic = uniform_invalid_irq_diagnostic;
                    if (start_receive_locked() != ESP_OK) {
                        set_rf_idle();
                        return false;
                    }
                    return true;
                }
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            irq_status = read_u16_be(irq_bytes);
            if (irq_status == 0) {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }

        last_tx_irq_status = irq_status;

        const uint8_t irq_bytes[2] = {
            static_cast<uint8_t>((irq_status >> 8) & 0xFF),
            static_cast<uint8_t>(irq_status & 0xFF),
        };
        if (write_command(SX126X_CMD_CLEAR_IRQ_STATUS, irq_bytes, sizeof(irq_bytes)) != ESP_OK) {
            set_rf_idle();
            return false;
        }

        if (start_receive_locked() != ESP_OK) {
            set_rf_idle();
            return false;
        }

        return (irq_status & SX126X_IRQ_TX_DONE) != 0;
    }

    bool send_user_message(const std::string &payload, bool beacon)
    {
        if (payload.empty()) {
            set_status("Enter a message before sending");
            return false;
        }
        if (!startup_complete) {
            set_status("Opening LoRa mesh...");
        }
        if (!ensure_radio_ready()) {
            return false;
        }

        lock();
        startup_started = true;
        startup_complete = true;
        startup_ok = true;
        unlock();

        bool restart_rx_task = false;
        while (true) {
            lock();
            const bool rx_active = rx_task != nullptr;
            if (rx_active) {
                rx_task_stop = true;
                restart_rx_task = true;
            }
            unlock();
            if (!rx_active) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        lock();
        if (stop_receive_locked() != ESP_OK) {
            unlock();
            set_status("Failed to stop LoRa receive before transmit");
            return false;
        }

        MeshPacket packet = {};
        packet.sender_id = stored_state.identity.device_id;
        packet.sender_name = stored_state.identity.display_name;
        packet.timestamp_ms = esp_timer_get_time() / 1000;
        packet.ttl = stored_state.settings.hop_limit == 0U ? kDefaultTtl : stored_state.settings.hop_limit;
        std::string bubble_text = payload;
        std::string bubble_meta = stored_state.identity.display_name;
        if (selected_conversation_id == jc4880::lora_mesh::kCommonConversationId) {
            packet.kind = PacketKind::PublicChat;
            packet.target_id = jc4880::lora_mesh::kBroadcastTargetId;
        } else {
            PeerInfo *peer = find_peer_locked(selected_peer_id);
            if ((peer == nullptr) || peer->pair_secret_hex.empty() || peer->public_key_hex.empty()) {
                unlock();
                set_status("Selected peer is not fully paired yet");
                return false;
            }
            packet.kind = PacketKind::PrivateChat;
            packet.target_id = peer->device_id;
            packet.encrypted = true;
            packet.nonce_hex = jc4880::lora_mesh::cryptoGenerateNonceHex();
            if (!jc4880::lora_mesh::cryptoEncryptForPeer(stored_state.identity,
                                                        peer->public_key_hex,
                                                        peer->pair_secret_hex,
                                                        packet.nonce_hex,
                                                        payload,
                                                        packet.payload,
                                                        packet.auth_hex)) {
                unlock();
                set_status("Private chat encryption failed");
                return false;
            }
            bubble_text = std::string("[private] ") + payload;
        }
        if ((packet.kind == PacketKind::PublicChat) && stored_state.settings.public_chat_encryption) {
            packet.encrypted = true;
            packet.nonce_hex = jc4880::lora_mesh::cryptoGenerateNonceHex();
            if (!jc4880::lora_mesh::cryptoEncryptForPeer(stored_state.identity,
                                                        "GROUP",
                                                        stored_state.settings.public_group_key_hex,
                                                        packet.nonce_hex,
                                                        payload,
                                                        packet.payload,
                                                        packet.auth_hex)) {
                unlock();
                set_status("Public chat encryption failed");
                return false;
            }
        } else {
            packet.payload = payload;
        }
        const bool ok = send_packet_locked(packet, &bubble_text, bubble_meta);
        save_state_if_needed_locked();
        unlock();

        if (restart_rx_task) {
            rx_task_stop = false;
            TaskHandle_t restarted_rx_task = nullptr;
            if (create_task_prefer_psram(&Impl::rx_task_entry, "lora_mesh_rx", kRxTaskStack, this, kRxTaskPriority, &restarted_rx_task)) {
                lock();
                rx_task = restarted_rx_task;
                unlock();
            } else {
                set_status("Failed to restart LoRa receive task after transmit");
            }
        }

        return ok;
    }

    void handle_incoming_frame_locked(const MeshFrame &frame)
    {
        if (frame.sender == node_id) {
            return;
        }

        if (has_seen_frame(frame.origin, frame.sequence)) {
            return;
        }

        remember_frame(frame.origin, frame.sequence);
        ++rx_count;

        MeshPacket packet = {};
         if (!jc4880::lora_mesh::decode_mesh_packet(frame.payload, packet)) {
             std::ostringstream stream;
             stream << "RX legacy " << format_node_id(frame.origin)
                 << " via " << format_node_id(frame.sender)
                 << " ttl=" << static_cast<int>(frame.ttl)
                 << " rssi=" << last_rssi
                 << " snr=" << last_snr
                 << " \"" << frame.payload << "\"";
             append_line_capped(log_text, stream.str());
             push_conversation_locked(jc4880::lora_mesh::kCommonConversationId,
                          frame.payload,
                          make_message_meta_locked(format_node_id(frame.origin)),
                          false);
         } else {
            remember_peer_activity_locked(packet.sender_id, packet.sender_name, packet.public_key_hex);

            std::ostringstream stream;
            stream << "RX packet kind=" << jc4880::lora_mesh::packet_kind_name(packet.kind)
                   << " from=" << packet.sender_id
                   << " target=" << packet.target_id
                   << " ttl=" << static_cast<int>(frame.ttl)
                   << " rssi=" << last_rssi
                   << " snr=" << last_snr;
            append_line_capped(log_text, stream.str());

            switch (packet.kind) {
            case PacketKind::PublicChat: {
                std::string text = packet.payload;
                if (packet.encrypted) {
                    if (!jc4880::lora_mesh::cryptoDecryptFromPeer(stored_state.identity,
                                                                 "GROUP",
                                                                 stored_state.settings.public_group_key_hex,
                                                                 packet.nonce_hex,
                                                                 packet.payload,
                                                                 packet.auth_hex,
                                                                 text)) {
                        ++drop_count;
                        append_line_capped(log_text, "Dropped encrypted public packet: decrypt failed");
                        break;
                    }
                }
                push_conversation_locked(jc4880::lora_mesh::kCommonConversationId,
                                         text,
                                         make_message_meta_locked(packet.sender_name.empty() ? packet.sender_id : packet.sender_name),
                                         false);
                status_text = "Mesh message received";
                break;
            }
            case PacketKind::PairRequest: {
                if ((packet.sender_id != stored_state.identity.device_id) && (find_pending_request_locked(packet.sender_id) == nullptr)) {
                    pending_pair_requests.push_back(PendingPairRequest{.device_id = packet.sender_id,
                                                                       .display_name = packet.sender_name,
                                                                       .public_key_hex = packet.public_key_hex,
                                                                       .msg_id = packet.msg_id,
                                                                       .received_ms = esp_timer_get_time() / 1000,
                                                                       .last_rssi = last_rssi,
                                                                       .last_snr = last_snr});
                    push_conversation_locked(jc4880::lora_mesh::kCommonConversationId,
                                             std::string("Pair request from ") +
                                                 (packet.sender_name.empty() ? packet.sender_id : packet.sender_name),
                                             make_message_meta_locked(packet.sender_id),
                                             false);
                }
                status_text = "Pair request received";
                break;
            }
            case PacketKind::PairAccept: {
                if (packet.target_id == stored_state.identity.device_id) {
                    PeerInfo *peer = find_peer_locked(packet.sender_id);
                    if (peer == nullptr) {
                        stored_state.peers.push_back(PeerInfo{.device_id = packet.sender_id,
                                                              .display_name = packet.sender_name.empty() ? packet.sender_id : packet.sender_name,
                                                              .public_key_hex = packet.public_key_hex,
                                                              .pair_secret_hex = packet.pair_secret_hex,
                                                              .last_seen_ms = esp_timer_get_time() / 1000,
                                                              .last_rssi = last_rssi,
                                                              .last_snr = last_snr,
                                                              .presence = jc4880::lora_mesh::PeerPresence::Online});
                    } else {
                        peer->display_name = packet.sender_name.empty() ? peer->display_name : packet.sender_name;
                        peer->public_key_hex = packet.public_key_hex;
                        peer->pair_secret_hex = packet.pair_secret_hex;
                    }
                    pending_pair_requests.erase(std::remove_if(pending_pair_requests.begin(),
                                                               pending_pair_requests.end(),
                                                               [&packet](const PendingPairRequest &request) {
                                                                   return request.device_id == packet.sender_id;
                                                               }),
                                               pending_pair_requests.end());
                    state_dirty = true;
                    target_list_dirty = true;
                    push_conversation_locked(packet.sender_id,
                                             std::string("Paired with ") +
                                                 (packet.sender_name.empty() ? packet.sender_id : packet.sender_name),
                                             make_message_meta_locked(packet.sender_id),
                                             false);
                    status_text = "Pairing accepted";
                }
                break;
            }
            case PacketKind::PairReject:
                if (packet.target_id == stored_state.identity.device_id) {
                    push_conversation_locked(packet.sender_id,
                                             std::string("Pairing rejected by ") +
                                                 (packet.sender_name.empty() ? packet.sender_id : packet.sender_name),
                                             make_message_meta_locked(packet.sender_id),
                                             false);
                    status_text = "Pairing rejected";
                }
                break;
            case PacketKind::PairConfirm:
                if (packet.target_id == stored_state.identity.device_id) {
                    status_text = "Pairing confirmed";
                }
            case PacketKind::PrivateChat: {
                if (packet.target_id != stored_state.identity.device_id) {
                    break;
                }
                PeerInfo *peer = find_peer_locked(packet.sender_id);
                if ((peer == nullptr) || peer->pair_secret_hex.empty()) {
                    ++drop_count;
                    append_line_capped(log_text, "Dropped private packet from unpaired sender");
                    break;
                }
                std::string text;
                if (!jc4880::lora_mesh::cryptoDecryptFromPeer(stored_state.identity,
                                                             peer->public_key_hex,
                                                             peer->pair_secret_hex,
                                                             packet.nonce_hex,
                                                             packet.payload,
                                                             packet.auth_hex,
                                                             text)) {
                    ++drop_count;
                    append_line_capped(log_text, "Dropped private packet: decrypt failed");
                    break;
                }
                push_conversation_locked(packet.sender_id,
                                         std::string("[private] ") + text,
                                         make_message_meta_locked(peer->display_name.empty() ? peer->device_id : peer->display_name),
                                         false);
                status_text = "Private message received";
                break;
            }
            case PacketKind::Hello:
            case PacketKind::Presence:
                status_text = "Peer status updated";
                break;
            case PacketKind::Ack:
                break;
            }
        }

        save_state_if_needed_locked();

        if (stored_state.settings.forwarding_enabled && (frame.ttl > 1) && (frame.origin != node_id)) {
            MeshFrame relay = frame;
            relay.ttl = static_cast<uint8_t>(relay.ttl - 1);
            relay.sender = node_id;
            vTaskDelay(pdMS_TO_TICKS(40 + (esp_random() % 80)));
            if (transmit_frame_locked(relay)) {
                ++relay_count;
                append_line_capped(log_text, "Relay forwarded mesh packet");
                status_text = "Relayed mesh packet";
            } else {
                ++drop_count;
                append_line_capped(log_text, "Relay failed");
                status_text = "Relay transmit failed";
            }
        } else {
            status_text = (frame.flags & kMeshFlagBeacon) != 0 ? "Mesh beacon received" : "Mesh message received";
        }
    }

    void rx_task_loop()
    {
        while (!rx_task_stop) {
            if (!ensure_radio_ready()) {
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            if (!radio_module_uses_spi(stored_state.settings.radio_module)) {
                uint8_t buffer[128] = {};
                const int read = uart_read_bytes(kLoRaUartPort, buffer, sizeof(buffer), pdMS_TO_TICKS(kRxPollMs));
                if (read > 0) {
                    lock();
                    uart_rx_bytes.insert(uart_rx_bytes.end(), buffer, buffer + read);
                    process_uart_rx_bytes_locked();
                    unlock();
                }
                continue;
            }

            if (gpio_get_level(gpio_for_role(RadioPinRole::Dio1)) == 0) {
                vTaskDelay(pdMS_TO_TICKS(kRxPollMs));
                continue;
            }

            lock();
            uint8_t irq_bytes[2] = {};
            if (read_command(SX126X_CMD_GET_IRQ_STATUS, irq_bytes, sizeof(irq_bytes)) != ESP_OK) {
                append_line_capped(log_text, "Failed to read IRQ status");
                unlock();
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            const uint16_t irq_status = read_u16_be(irq_bytes);
            (void)write_command(SX126X_CMD_CLEAR_IRQ_STATUS, irq_bytes, sizeof(irq_bytes));

            if ((irq_status & SX126X_IRQ_RX_DONE) != 0U) {
                uint8_t buffer_status[2] = {};
                uint8_t packet_status[3] = {};
                if ((read_command(SX126X_CMD_GET_RX_BUFFER_STATUS, buffer_status, sizeof(buffer_status)) == ESP_OK) &&
                    (read_command(SX126X_CMD_GET_PACKET_STATUS, packet_status, sizeof(packet_status)) == ESP_OK)) {
                    const uint8_t payload_length = buffer_status[0];
                    const uint8_t rx_offset = buffer_status[1];
                    if (payload_length > 0U) {
                        std::vector<uint8_t> payload(payload_length, 0);
                        if (read_buffer(rx_offset, payload.data(), payload.size()) == ESP_OK) {
                            last_rssi = -static_cast<int>(packet_status[0]) / 2;
                            last_snr = static_cast<int>(static_cast<int8_t>(packet_status[1])) / 4;

                            MeshFrame frame = {};
                            if (decode_frame(payload.data(), payload.size(), frame) && is_printable_text(frame.payload)) {
                                handle_incoming_frame_locked(frame);
                            } else {
                                ++drop_count;
                                append_line_capped(log_text, "Dropped non-mesh or invalid payload");
                            }
                        }
                    }
                }
            } else if ((irq_status & (SX126X_IRQ_TIMEOUT | SX126X_IRQ_CRC_ERR | SX126X_IRQ_HEADER_ERR)) != 0U) {
                ++drop_count;
                append_line_capped(log_text, "Radio receive error");
                status_text = "LoRa receive error";
            }

            (void)start_receive_locked();
            unlock();
        }

        lock();
        (void)stop_receive_locked();
        set_rf_idle();
        unlock();
        rx_task = nullptr;
    }

    void refresh_ui()
    {
        std::string status_copy;
        uint32_t tx = 0;
        uint32_t rx = 0;
        uint32_t relays = 0;
        uint32_t drops = 0;
        int rssi = 0;
        int snr = 0;
        std::string self_test_copy;
        bool self_test_ok_copy = false;
        bool self_test_ran_copy = false;
        bool self_test_running_copy = false;
        bool startup_complete_copy = false;
        bool radio_enabled_copy = true;
        ViewMode view_mode_copy = ViewMode::Targets;
        std::string current_chat_title_copy;
        std::vector<ConversationEntry> conversation_copy;
        std::vector<PeerInfo> peers_copy;
        size_t pending_pair_count = 0;
        bool conversation_dirty_copy = false;
        bool target_list_dirty_copy = false;

        lock();
        status_copy = status_text;
        self_test_copy = self_test_summary;
        self_test_ok_copy = self_test_ok;
        self_test_ran_copy = self_test_ran;
        self_test_running_copy = self_test_running;
        startup_complete_copy = startup_complete;
        radio_enabled_copy = stored_state.settings.radio_enabled;
        view_mode_copy = view_mode;
        current_chat_title_copy = current_chat_title_locked();
        conversation_dirty_copy = conversation_dirty;
        if (conversation_dirty_copy) {
            conversation_copy = conversation;
            conversation_dirty = false;
        }
        target_list_dirty_copy = target_list_dirty;
        if (target_list_dirty_copy) {
            peers_copy = stored_state.peers;
            target_list_dirty = false;
        }
        pending_pair_count = pending_pair_requests.size();
        tx = tx_count;
        rx = rx_count;
        relays = relay_count;
        drops = drop_count;
        rssi = last_rssi;
        snr = last_snr;
        unlock();

        if (header_title != nullptr) {
            lv_label_set_text(header_title, view_mode_copy == ViewMode::Targets ? "LoRa" : current_chat_title_copy.c_str());
        }
        if (chat_title_label != nullptr) {
            lv_label_set_text(chat_title_label, current_chat_title_copy.c_str());
        }
        if (chat_status_label != nullptr) {
            lv_label_set_text(chat_status_label, status_copy.c_str());
        }
        if (back_button != nullptr) {
            if (view_mode_copy == ViewMode::Targets) {
                lv_obj_add_flag(back_button, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(back_button, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (target_list != nullptr) {
            if (view_mode_copy == ViewMode::Targets) {
                lv_obj_clear_flag(target_list, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(target_list, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (chat_panel != nullptr) {
            if (view_mode_copy == ViewMode::Chat) {
                lv_obj_clear_flag(chat_panel, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(chat_panel, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (input != nullptr) {
            if ((view_mode_copy != ViewMode::Chat) || !startup_complete_copy) {
                lv_obj_add_state(input, LV_STATE_DISABLED);
            } else {
                lv_obj_clear_state(input, LV_STATE_DISABLED);
            }
        }
        if (target_list_dirty_copy && (target_list != nullptr)) {
            lv_obj_clean(target_list);

            lv_obj_t *status_card = lv_obj_create(target_list);
            lv_obj_set_width(status_card, LV_PCT(100));
            lv_obj_set_height(status_card, LV_SIZE_CONTENT);
            lv_obj_set_style_radius(status_card, 18, 0);
            lv_obj_set_style_pad_all(status_card, 14, 0);
            lv_obj_set_style_border_width(status_card, 0, 0);
            lv_obj_set_style_shadow_width(status_card, 0, 0);
            lv_obj_set_style_bg_color(status_card, lv_color_hex(0xFFFFFF), 0);

            lv_obj_t *status_title = lv_label_create(status_card);
            lv_obj_set_style_text_font(status_title, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(status_title, lv_color_hex(0x0F172A), 0);
            lv_label_set_text(status_title, "Status");

            lv_obj_t *status_label = lv_label_create(status_card);
            lv_obj_set_width(status_label, LV_PCT(100));
            lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x475569), 0);
            lv_obj_align_to(status_label, status_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
            lv_label_set_text(status_label, status_copy.c_str());

            if (!radio_enabled_copy) {
                lv_obj_set_style_bg_color(status_card, lv_color_hex(0xFEE2E2), 0);
                lv_obj_set_style_text_color(status_title, lv_color_hex(0x991B1B), 0);
                lv_obj_set_style_text_color(status_label, lv_color_hex(0x7F1D1D), 0);
                return;
            }

            lv_obj_t *common_button = lv_btn_create(target_list);
            lv_obj_set_width(common_button, LV_PCT(100));
            lv_obj_set_height(common_button, 46);
            lv_obj_set_style_radius(common_button, 18, 0);
            lv_obj_set_style_pad_left(common_button, 16, 0);
            lv_obj_set_style_pad_right(common_button, 16, 0);
            lv_obj_set_style_pad_top(common_button, 0, 0);
            lv_obj_set_style_pad_bottom(common_button, 0, 0);
            lv_obj_set_style_border_width(common_button, 0, 0);
            lv_obj_set_style_shadow_width(common_button, 0, 0);
            lv_obj_set_style_bg_color(common_button, lv_color_hex(0x00A884), 0);
            lv_obj_set_flex_flow(common_button, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(common_button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_add_event_cb(common_button, on_open_common_chat, LV_EVENT_CLICKED, this);

            lv_obj_t *common_title = lv_label_create(common_button);
            lv_obj_set_width(common_title, LV_PCT(100));
            lv_obj_set_style_text_color(common_title, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(common_title, &lv_font_montserrat_18, 0);
            lv_label_set_long_mode(common_title, LV_LABEL_LONG_DOT);
            lv_label_set_text(common_title, (stored_state.settings.common_chat_name.empty() ? "Common Mesh Chat" : stored_state.settings.common_chat_name).c_str());

            for (size_t index = 0; index < peers_copy.size(); ++index) {
                const PeerInfo &peer = peers_copy[index];
                lv_obj_t *button = lv_btn_create(target_list);
                lv_obj_set_width(button, LV_PCT(100));
                lv_obj_set_height(button, LV_SIZE_CONTENT);
                lv_obj_set_style_radius(button, 18, 0);
                lv_obj_set_style_pad_all(button, 14, 0);
                lv_obj_set_style_border_width(button, 0, 0);
                lv_obj_add_event_cb(button, on_open_peer_chat, LV_EVENT_CLICKED, this);

                lv_obj_t *col = lv_obj_create(button);
                lv_obj_set_width(col, LV_PCT(100));
                lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(col, 0, 0);
                lv_obj_set_style_pad_all(col, 0, 0);
                lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
                lv_obj_set_style_pad_row(col, 4, 0);
                lv_label_set_text(lv_label_create(col), peer_label(peer).c_str());
                lv_obj_t *subtitle = lv_label_create(col);
                lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, 0);
                std::ostringstream peer_info;
                peer_info << jc4880::lora_mesh::presence_name(peer.presence)
                          << "  ID " << peer.device_id
                          << "  RSSI " << peer.last_rssi
                          << "  SNR " << peer.last_snr;
                lv_label_set_text(subtitle, peer_info.str().c_str());
            }
        }
        if (conversation_dirty_copy && (message_list != nullptr)) {
            lv_obj_clean(message_list);

            bool has_visible_messages = false;

            for (const ConversationEntry &entry : conversation_copy) {
                if (entry.conversation_id != selected_conversation_id) {
                    continue;
                }
                has_visible_messages = true;
                lv_obj_t *row = lv_obj_create(message_list);
                lv_obj_set_width(row, LV_PCT(100));
                lv_obj_set_height(row, LV_SIZE_CONTENT);
                lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_set_style_pad_all(row, 0, 0);

                lv_obj_t *bubble = lv_obj_create(row);
                lv_obj_set_width(bubble, LV_PCT(78));
                lv_obj_set_height(bubble, LV_SIZE_CONTENT);
                lv_obj_set_style_border_width(bubble, 0, 0);
                lv_obj_set_style_radius(bubble, 18, 0);
                lv_obj_set_style_pad_left(bubble, 14, 0);
                lv_obj_set_style_pad_right(bubble, 14, 0);
                lv_obj_set_style_pad_top(bubble, 10, 0);
                lv_obj_set_style_pad_bottom(bubble, 10, 0);
                lv_obj_set_style_pad_row(bubble, 6, 0);
                lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
                lv_obj_set_style_bg_color(bubble,
                                          entry.outgoing ? lv_color_hex(0xD9FDD3) : lv_color_hex(0xFFFFFF),
                                          0);
                lv_obj_set_align(bubble, entry.outgoing ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT);

                lv_obj_t *message_label = lv_label_create(bubble);
                lv_obj_set_width(message_label, LV_PCT(100));
                lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
                lv_obj_set_style_text_color(message_label, lv_color_hex(0x111B21), 0);
                lv_label_set_text(message_label, entry.text.c_str());

                lv_obj_t *meta_label = lv_label_create(bubble);
                lv_obj_set_width(meta_label, LV_PCT(100));
                lv_obj_set_style_text_font(meta_label, &lv_font_montserrat_12, 0);
                lv_obj_set_style_text_color(meta_label, lv_color_hex(0x667781), 0);
                lv_obj_set_style_text_align(meta_label, entry.outgoing ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
                lv_label_set_text(meta_label, entry.meta.c_str());
            }

            if (lv_obj_get_child_cnt(message_list) > 0) {
                lv_obj_t *last_row = lv_obj_get_child(message_list, lv_obj_get_child_cnt(message_list) - 1);
                if (last_row != nullptr) {
                    lv_obj_scroll_to_view(last_row, LV_ANIM_OFF);
                }
            }

            if (!has_visible_messages) {
                lv_obj_t *empty_row = lv_obj_create(message_list);
                lv_obj_set_width(empty_row, LV_PCT(100));
                lv_obj_set_height(empty_row, LV_SIZE_CONTENT);
                lv_obj_set_style_bg_opa(empty_row, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(empty_row, 0, 0);
                lv_obj_set_style_pad_top(empty_row, 18, 0);
                lv_obj_set_style_pad_bottom(empty_row, 18, 0);
                lv_obj_set_style_pad_left(empty_row, 10, 0);
                lv_obj_set_style_pad_right(empty_row, 10, 0);

                lv_obj_t *empty_label = lv_label_create(empty_row);
                lv_obj_set_width(empty_label, LV_PCT(100));
                lv_label_set_long_mode(empty_label, LV_LABEL_LONG_WRAP);
                lv_obj_set_style_text_align(empty_label, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_set_style_text_color(empty_label, lv_color_hex(0x667781), 0);
                lv_label_set_text(empty_label, "No messages yet. Incoming mesh messages will appear here.");
            }
        }
    }

    void set_keyboard_visible(bool visible)
    {
        if (keyboard == nullptr) {
            return;
        }

        if (visible) {
            lv_keyboard_set_textarea(keyboard, input);
            lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
            if (composer != nullptr) {
                lv_obj_scroll_to_view(composer, LV_ANIM_OFF);
            }
        } else {
            lv_keyboard_set_textarea(keyboard, nullptr);
            lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void focus_input()
    {
        if (input == nullptr) {
            return;
        }

        set_keyboard_visible(true);
        lv_obj_add_state(input, LV_STATE_FOCUSED);
        lv_event_send(input, LV_EVENT_FOCUSED, nullptr);
        if (composer != nullptr) {
            lv_obj_scroll_to_view(composer, LV_ANIM_OFF);
        }
    }

    static void on_ui_timer(lv_timer_t *timer)
    {
        auto *impl = static_cast<Impl *>(timer->user_data);
        if (impl != nullptr) {
            if (!impl->startup_started) {
                impl->start_startup_task();
            }
            impl->lock();
            if (impl->pending_self_test_on_next_open && impl->startup_complete && impl->startup_ok && !impl->self_test_running) {
                impl->pending_self_test_on_next_open = false;
                impl->trace_event_locked("Pending self-test started after app open");
                impl->status_text = "LoRa self-test: starting";
                impl->start_self_test_task();
            }
            impl->unlock();
            impl->refresh_ui();
        }
    }

    static void on_open_common_chat(lv_event_t *event)
    {
        auto *impl = static_cast<Impl *>(lv_event_get_user_data(event));
        if (impl == nullptr) {
            return;
        }
        impl->lock();
        impl->trace_event_locked("UI opened common chat");
        impl->show_common_chat_locked();
        impl->unlock();
        impl->refresh_ui();
        if (impl->startup_complete) {
            impl->focus_input();
        }
    }

    static void on_open_peer_chat(lv_event_t *event)
    {
        auto *impl = static_cast<Impl *>(lv_event_get_user_data(event));
        if (impl == nullptr) {
            return;
        }
        lv_obj_t *button = lv_event_get_target(event);
        if (button == nullptr) {
            return;
        }
        lv_obj_t *col = lv_obj_get_child(button, 0);
        lv_obj_t *subtitle = (col == nullptr) ? nullptr : lv_obj_get_child(col, 1);
        const char *subtitle_text = (subtitle == nullptr) ? nullptr : lv_label_get_text(subtitle);
        if (subtitle_text == nullptr) {
            return;
        }
        const char *id_marker = std::strstr(subtitle_text, "ID ");
        if (id_marker == nullptr) {
            return;
        }
        id_marker += 3;
        const char *id_end = std::strchr(id_marker, ' ');
        const std::string peer_id = id_end == nullptr ? std::string(id_marker) : std::string(id_marker, id_end - id_marker);
        impl->lock();
        impl->trace_event_locked(std::string("UI opened peer chat ") + peer_id);
        impl->show_peer_chat_locked(peer_id);
        impl->unlock();
        impl->refresh_ui();
        if (impl->startup_complete) {
            impl->focus_input();
        }
    }

    static void on_back_clicked(lv_event_t *event)
    {
        auto *impl = static_cast<Impl *>(lv_event_get_user_data(event));
        if (impl == nullptr) {
            return;
        }
        impl->lock();
        impl->trace_event_locked("UI returned to target list");
        impl->show_targets_locked();
        impl->unlock();
        impl->set_keyboard_visible(false);
        impl->refresh_ui();
    }

    static void on_run_self_test(lv_event_t *event)
    {
        auto *impl = static_cast<Impl *>(lv_event_get_user_data(event));
        if (impl == nullptr) {
            return;
        }

        impl->lock();
        if (impl->self_test_running) {
            impl->trace_event_locked("UI requested self-test stop");
            impl->request_self_test_stop_locked("user request");
            impl->unlock();
            impl->refresh_ui();
            return;
        }
        impl->unlock();

        if (!impl->startup_complete) {
            impl->set_status("LoRa radio is still opening");
            impl->trace_event("Self-test start rejected because startup is incomplete");
            return;
        }

        impl->set_status("LoRa self-test: starting");
        impl->trace_event("UI requested self-test start");
        impl->start_self_test_task();
        impl->refresh_ui();
    }

    static void on_send_clicked(lv_event_t *event)
    {
        auto *impl = static_cast<Impl *>(lv_event_get_user_data(event));
        if ((impl == nullptr) || (impl->input == nullptr)) {
            return;
        }

        const char *text = lv_textarea_get_text(impl->input);
        if ((text == nullptr) || (text[0] == '\0')) {
            impl->set_status("Enter a message before sending");
            return;
        }
        impl->set_status("Sending mesh message...");
        if (!impl->start_send_task(text, false, true)) {
            impl->refresh_ui();
        }
    }

    static void on_input_ready(lv_event_t *event)
    {
        on_send_clicked(event);
    }

    static lv_obj_t *create_setting_field(lv_obj_t *parent, const char *label_text, lv_obj_t **out_input, const char *placeholder)
    {
        lv_obj_t *wrap = lv_obj_create(parent);
        lv_obj_set_width(wrap, LV_PCT(100));
        lv_obj_set_height(wrap, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(wrap, 0, 0);
        lv_obj_set_style_pad_all(wrap, 0, 0);
        lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(wrap, 4, 0);
        lv_label_set_text(lv_label_create(wrap), label_text);
        lv_obj_t *input_field = lv_textarea_create(wrap);
        lv_obj_set_width(input_field, LV_PCT(100));
        lv_obj_set_height(input_field, 44);
        lv_textarea_set_one_line(input_field, true);
        lv_textarea_set_placeholder_text(input_field, placeholder);
        *out_input = input_field;
        return wrap;
    }

    static lv_obj_t *create_setting_dropdown_field(lv_obj_t *parent, const char *label_text, lv_obj_t **out_dropdown, const char *options)
    {
        lv_obj_t *wrap = lv_obj_create(parent);
        lv_obj_set_width(wrap, LV_PCT(100));
        lv_obj_set_height(wrap, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(wrap, 0, 0);
        lv_obj_set_style_pad_all(wrap, 0, 0);
        lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(wrap, 4, 0);
        lv_label_set_text(lv_label_create(wrap), label_text);
        lv_obj_t *dropdown = lv_dropdown_create(wrap);
        lv_obj_set_width(dropdown, LV_PCT(100));
        lv_dropdown_set_options(dropdown, options);
        *out_dropdown = dropdown;
        return wrap;
    }

    void refresh_settings_module_ui(bool apply_defaults)
    {
        const RadioModule module = (settings_module_dropdown != nullptr)
                                     ? radio_module_from_dropdown(lv_dropdown_get_selected(settings_module_dropdown))
                                     : stored_state.settings.radio_module;

        if (apply_defaults) {
            jc4880::lora_mesh::MeshSettings preview = stored_state.settings;
            preview.radio_module = module;
            apply_module_defaults(preview);
            for (size_t role_index = 0; role_index < kRadioPinRoleCount; ++role_index) {
                auto role = static_cast<RadioPinRole>(role_index);
                if (settings_pin_dropdowns[role_index] != nullptr) {
                    lv_dropdown_set_selected(settings_pin_dropdowns[role_index],
                                             gpio_choice_index(pin_value_for_role(preview, role)));
                }
            }
        }

        if (settings_module_info_label != nullptr) {
            std::string info;
            if (module == RadioModule::E22_400M22S) {
                info = "SPI SX126x module. Power VCC from 3.3V regulator output and connect GND to GND.";
            } else {
                info = "UART transparent module. Power from 3.3V or 5V per module guidance; RXD/TXD map to ESP32-P4 UART pins.";
            }
            lv_label_set_text(settings_module_info_label, info.c_str());
        }

        for (size_t role_index = 0; role_index < kRadioPinRoleCount; ++role_index) {
            auto role = static_cast<RadioPinRole>(role_index);
            const bool visible = module_uses_role(module, role);
            if (settings_pin_rows[role_index] != nullptr) {
                if (visible) {
                    lv_obj_clear_flag(settings_pin_rows[role_index], LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(settings_pin_rows[role_index], LV_OBJ_FLAG_HIDDEN);
                }
            }
            if (settings_pin_labels[role_index] != nullptr) {
                lv_label_set_text(settings_pin_labels[role_index], radio_pin_label(role));
            }
        }
    }

    static void on_radio_module_changed(lv_event_t *event)
    {
        auto *impl = static_cast<Impl *>(lv_event_get_user_data(event));
        if (impl == nullptr) {
            return;
        }
        impl->refresh_settings_module_ui(true);
    }

    static void on_settings_save(lv_event_t *event)
    {
        auto *impl = static_cast<Impl *>(lv_event_get_user_data(event));
        if (impl == nullptr) {
            return;
        }
        impl->lock();
        const auto previous_settings = impl->stored_state.settings;
        impl->stored_state.identity.display_name = lv_textarea_get_text(impl->settings_name_input);
        impl->stored_state.settings.common_chat_name = lv_textarea_get_text(impl->settings_common_name_input);
        if (impl->settings_module_dropdown != nullptr) {
            impl->stored_state.settings.radio_module = radio_module_from_dropdown(lv_dropdown_get_selected(impl->settings_module_dropdown));
        }

        uint32_t frequency_hz = 0;
        if (impl->parse_uint32_text(impl->settings_frequency_input, frequency_hz)) {
            impl->stored_state.settings.frequency_hz = frequency_hz;
        }

        uint8_t value8 = 0;
        if (impl->parse_uint8_text(impl->settings_sf_input, value8)) {
            impl->stored_state.settings.spreading_factor = value8;
        }
        if (impl->parse_uint8_text(impl->settings_bw_input, value8)) {
            impl->stored_state.settings.bandwidth = value8;
        }
        if (impl->parse_uint8_text(impl->settings_cr_input, value8)) {
            impl->stored_state.settings.coding_rate = value8;
        }
        if (impl->parse_uint8_text(impl->settings_hop_input, value8)) {
            impl->stored_state.settings.hop_limit = value8;
        }

        if (impl->settings_forward_switch != nullptr) {
            impl->stored_state.settings.forwarding_enabled = lv_obj_has_state(impl->settings_forward_switch, LV_STATE_CHECKED);
        }
        if (impl->settings_encrypt_switch != nullptr) {
            impl->stored_state.settings.public_chat_encryption = lv_obj_has_state(impl->settings_encrypt_switch, LV_STATE_CHECKED);
        }

        for (size_t role_index = 0; role_index < kRadioPinRoleCount; ++role_index) {
            lv_obj_t *dropdown = impl->settings_pin_dropdowns[role_index];
            if (dropdown == nullptr) {
                continue;
            }
            impl->set_pin_value_for_role(impl->stored_state.settings,
                                         static_cast<RadioPinRole>(role_index),
                                         gpio_choice_value(lv_dropdown_get_selected(dropdown)));
        }

        if (impl->stored_state.identity.display_name.empty()) {
            impl->stored_state.identity.display_name = std::string("P4-") + impl->stored_state.identity.device_id.substr(0, 4);
        }
        if (impl->stored_state.settings.common_chat_name.empty()) {
            impl->stored_state.settings.common_chat_name = "Common Mesh Chat";
        }
        const bool radio_settings_changed =
            (impl->stored_state.settings.radio_module != previous_settings.radio_module) ||
            (impl->stored_state.settings.frequency_hz != previous_settings.frequency_hz) ||
            (impl->stored_state.settings.spreading_factor != previous_settings.spreading_factor) ||
            (impl->stored_state.settings.bandwidth != previous_settings.bandwidth) ||
            (impl->stored_state.settings.coding_rate != previous_settings.coding_rate) ||
            (impl->stored_state.settings.hop_limit != previous_settings.hop_limit) ||
            (impl->stored_state.settings.forwarding_enabled != previous_settings.forwarding_enabled) ||
            (impl->stored_state.settings.public_chat_encryption != previous_settings.public_chat_encryption) ||
            (impl->stored_state.settings.spi_miso_gpio != previous_settings.spi_miso_gpio) ||
            (impl->stored_state.settings.spi_mosi_gpio != previous_settings.spi_mosi_gpio) ||
            (impl->stored_state.settings.spi_sck_gpio != previous_settings.spi_sck_gpio) ||
            (impl->stored_state.settings.spi_nss_gpio != previous_settings.spi_nss_gpio) ||
            (impl->stored_state.settings.busy_gpio != previous_settings.busy_gpio) ||
            (impl->stored_state.settings.dio1_gpio != previous_settings.dio1_gpio) ||
            (impl->stored_state.settings.nrst_gpio != previous_settings.nrst_gpio) ||
            (impl->stored_state.settings.txen_gpio != previous_settings.txen_gpio) ||
            (impl->stored_state.settings.rxen_gpio != previous_settings.rxen_gpio) ||
            (impl->stored_state.settings.uart_tx_gpio != previous_settings.uart_tx_gpio) ||
            (impl->stored_state.settings.uart_rx_gpio != previous_settings.uart_rx_gpio) ||
            (impl->stored_state.settings.mode0_gpio != previous_settings.mode0_gpio) ||
            (impl->stored_state.settings.mode1_gpio != previous_settings.mode1_gpio) ||
            (impl->stored_state.settings.aux_gpio != previous_settings.aux_gpio);
        impl->state_dirty = true;
        impl->target_list_dirty = true;
        impl->conversation_dirty = true;
        if (radio_settings_changed) {
            append_line_capped(impl->log_text, "Radio settings saved; close and reopen LoRa to apply hardware changes");
            impl->trace_event_locked("Radio settings changed from UI save");
        }
        impl->trace_event_locked("Settings saved from UI");
        impl->show_targets_locked();
        impl->save_state_if_needed_locked();
        impl->unlock();
        impl->set_status(radio_settings_changed ? "Settings saved. Reopen LoRa to apply radio changes" : "Settings saved");
        impl->refresh_ui();
    }

    static void on_input_focus_event(lv_event_t *event)
    {
        auto *impl = static_cast<Impl *>(lv_event_get_user_data(event));
        if (impl == nullptr) {
            return;
        }

        const lv_event_code_t code = lv_event_get_code(event);
        if ((code == LV_EVENT_FOCUSED) || (code == LV_EVENT_CLICKED)) {
            impl->set_keyboard_visible(true);
        } else if (code == LV_EVENT_DEFOCUSED) {
            impl->set_keyboard_visible(false);
        }
    }

    static void on_keyboard_event(lv_event_t *event)
    {
        auto *impl = static_cast<Impl *>(lv_event_get_user_data(event));
        if (impl == nullptr) {
            return;
        }

        const lv_event_code_t code = lv_event_get_code(event);
        if (code == LV_EVENT_READY) {
            on_send_clicked(event);
        } else if (code == LV_EVENT_CANCEL) {
            if (impl->input != nullptr) {
                lv_obj_clear_state(impl->input, LV_STATE_FOCUSED);
            }
            impl->set_keyboard_visible(false);
        }
    }

    bool build_ui()
    {
        root = lv_obj_create(lv_scr_act());
        if (root == nullptr) {
            return false;
        }

        lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(root, 12, 0);
        lv_obj_set_style_pad_row(root, 10, 0);
        lv_obj_set_style_border_width(root, 0, 0);
        lv_obj_set_style_radius(root, 0, 0);
        lv_obj_set_style_bg_color(root, lv_color_hex(0xEFEAE2), 0);
        lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);

        lv_obj_t *header = lv_obj_create(root);
        lv_obj_set_width(header, LV_PCT(100));
        lv_obj_set_height(header, LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_set_style_radius(header, 0, 0);
        lv_obj_set_style_pad_left(header, 12, 0);
        lv_obj_set_style_pad_right(header, 12, 0);
        lv_obj_set_style_pad_top(header, 10, 0);
        lv_obj_set_style_pad_bottom(header, 10, 0);
        lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);

        lv_obj_t *header_row = lv_obj_create(header);
        lv_obj_set_width(header_row, LV_PCT(100));
        lv_obj_set_height(header_row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(header_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(header_row, 0, 0);
        lv_obj_set_style_pad_all(header_row, 0, 0);
        lv_obj_set_flex_flow(header_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(header_row, 8, 0);

        back_button = lv_btn_create(header_row);
        lv_obj_set_size(back_button, 42, 42);
        lv_obj_set_style_radius(back_button, 21, 0);
        lv_obj_add_event_cb(back_button, on_back_clicked, LV_EVENT_CLICKED, this);
        lv_obj_add_flag(back_button, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lv_label_create(back_button), LV_SYMBOL_LEFT);

        header_title = lv_label_create(header_row);
        lv_obj_set_style_text_font(header_title, &lv_font_montserrat_28, 0);
        lv_label_set_text(header_title, "LoRa");
        lv_obj_set_flex_grow(header_title, 1);
        lv_obj_set_style_text_align(header_title, LV_TEXT_ALIGN_CENTER, 0);

        target_list = lv_obj_create(root);
        lv_obj_set_width(target_list, LV_PCT(100));
        lv_obj_set_flex_grow(target_list, 1);
        lv_obj_set_style_border_width(target_list, 0, 0);
        lv_obj_set_style_radius(target_list, 18, 0);
        lv_obj_set_style_pad_all(target_list, 10, 0);
        lv_obj_set_style_pad_row(target_list, 8, 0);
        lv_obj_set_style_bg_color(target_list, lv_color_hex(0xE5DDD5), 0);
        lv_obj_set_flex_flow(target_list, LV_FLEX_FLOW_COLUMN);

        chat_panel = lv_obj_create(root);
        lv_obj_set_width(chat_panel, LV_PCT(100));
        lv_obj_set_flex_grow(chat_panel, 1);
        lv_obj_set_style_bg_opa(chat_panel, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(chat_panel, 0, 0);
        lv_obj_set_style_pad_all(chat_panel, 0, 0);
        lv_obj_set_style_pad_row(chat_panel, 8, 0);
        lv_obj_set_flex_flow(chat_panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_flag(chat_panel, LV_OBJ_FLAG_HIDDEN);

        chat_title_label = lv_label_create(chat_panel);
        lv_obj_set_width(chat_title_label, LV_PCT(100));
        lv_obj_set_style_text_font(chat_title_label, &lv_font_montserrat_18, 0);

        chat_status_label = lv_label_create(chat_panel);
        lv_obj_set_width(chat_status_label, LV_PCT(100));
        lv_label_set_long_mode(chat_status_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(chat_status_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(chat_status_label, lv_color_hex(0x667781), 0);

        message_list = lv_obj_create(chat_panel);
        lv_obj_set_width(message_list, LV_PCT(100));
        lv_obj_set_flex_grow(message_list, 1);
        lv_obj_set_style_border_width(message_list, 0, 0);
        lv_obj_set_style_radius(message_list, 18, 0);
        lv_obj_set_style_pad_all(message_list, 10, 0);
        lv_obj_set_style_pad_row(message_list, 8, 0);
        lv_obj_set_style_bg_color(message_list, lv_color_hex(0xE5DDD5), 0);
        lv_obj_set_flex_flow(message_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scrollbar_mode(message_list, LV_SCROLLBAR_MODE_OFF);

        composer = lv_obj_create(chat_panel);
        lv_obj_set_width(composer, LV_PCT(100));
        chat_status_label = nullptr;
        lv_obj_set_height(composer, LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(composer, 0, 0);
        lv_obj_set_style_radius(composer, 18, 0);
        lv_obj_set_style_bg_color(composer, lv_color_hex(0xF0F2F5), 0);
        lv_obj_set_style_pad_all(composer, 8, 0);
        lv_obj_set_style_pad_column(composer, 8, 0);
        lv_obj_set_flex_flow(composer, LV_FLEX_FLOW_ROW);

        input = lv_textarea_create(composer);
        lv_obj_set_flex_grow(input, 1);
        lv_obj_set_height(input, 48);
        lv_textarea_set_one_line(input, true);
        lv_textarea_set_placeholder_text(input, "Type a mesh message");
        lv_obj_add_event_cb(input, on_input_ready, LV_EVENT_READY, this);
        lv_obj_add_event_cb(input, on_input_focus_event, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(input, on_input_focus_event, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(input, on_input_focus_event, LV_EVENT_DEFOCUSED, this);

        send_button = lv_btn_create(composer);
        lv_obj_set_size(send_button, 48, 48);
        lv_obj_set_style_radius(send_button, 24, 0);
        lv_obj_set_style_bg_color(send_button, lv_color_hex(0x00A884), 0);
        lv_obj_add_event_cb(send_button, on_send_clicked, LV_EVENT_CLICKED, this);
        lv_obj_t *send_label = lv_label_create(send_button);
        lv_obj_set_style_text_font(send_label, &lv_font_montserrat_22, 0);
        lv_label_set_text(send_label, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(send_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(send_label);

        keyboard = lv_keyboard_create(chat_panel);
        lv_obj_set_width(keyboard, LV_PCT(100));
        lv_obj_set_height(keyboard, 240);
        lv_keyboard_set_textarea(keyboard, input);
        jc4880_keyboard_install_case_behavior(keyboard);
        lv_obj_add_event_cb(keyboard, on_keyboard_event, LV_EVENT_READY, this);
        lv_obj_add_event_cb(keyboard, on_keyboard_event, LV_EVENT_CANCEL, this);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

        conversation_dirty = true;
        target_list_dirty = true;

        refresh_ui();
        return true;
    }

    void release_ui()
    {
        if (root != nullptr) {
            lv_obj_del(root);
        }
        root = nullptr;
        header_title = nullptr;
        back_button = nullptr;
        menu_button = nullptr;
        target_list = nullptr;
        chat_panel = nullptr;
        settings_panel = nullptr;
        chat_title_label = nullptr;
        message_list = nullptr;
        composer = nullptr;
        input = nullptr;
        send_button = nullptr;
        keyboard = nullptr;
        settings_name_input = nullptr;
        settings_common_name_input = nullptr;
        settings_module_dropdown = nullptr;
        settings_module_info_label = nullptr;
        settings_frequency_input = nullptr;
        settings_sf_input = nullptr;
        settings_bw_input = nullptr;
        settings_cr_input = nullptr;
        settings_hop_input = nullptr;
        settings_pin_rows.fill(nullptr);
        settings_pin_labels.fill(nullptr);
        settings_pin_dropdowns.fill(nullptr);
        settings_self_test_button = nullptr;
        settings_self_test_button_label = nullptr;
        settings_self_test_label = nullptr;
        settings_forward_switch = nullptr;
        settings_encrypt_switch = nullptr;
    }
};

LoRaMeshApp::LoRaMeshApp()
        : ESP_Brookesia_PhoneApp("LoRa Mesh", &loramesh_png, true),
            _impl(new Impl())
{
}

LoRaMeshApp::~LoRaMeshApp()
{
    delete _impl;
    _impl = nullptr;
}

bool LoRaMeshApp::init()
{
    if (_impl == nullptr) {
        return false;
    }
    if (_impl->mutex == nullptr) {
        _impl->mutex = xSemaphoreCreateMutex();
        if (_impl->mutex == nullptr) {
            return false;
        }
    }

    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        _impl->node_id = (static_cast<uint32_t>(mac[2]) << 24) |
                         (static_cast<uint32_t>(mac[3]) << 16) |
                         (static_cast<uint32_t>(mac[4]) << 8) |
                         static_cast<uint32_t>(mac[5]);
    } else {
        _impl->node_id = esp_random();
    }

    _impl->append_log(std::string("LoRa mesh app prepared for node ") + format_node_id(_impl->node_id));
    ESP_LOGI(kTag, "Prepared LoRa mesh app for node %s", format_node_id(_impl->node_id).c_str());
    if (!jc4880::lora_mesh::load_stored_state(_impl->stored_state)) {
        _impl->append_log("Failed to load persisted LoRa mesh state; using defaults");
        ESP_LOGW(kTag, "Failed to load persisted LoRa mesh state; using defaults");
    }
    for (PeerInfo &peer : _impl->stored_state.peers) {
        peer.presence = jc4880::lora_mesh::PeerPresence::Unknown;
    }
    _impl->append_log(std::string("Identity ") + _impl->stored_state.identity.display_name + " (" + _impl->stored_state.identity.device_id + ")");
    ESP_LOGI(kTag,
             "Identity %s (%s)",
             _impl->stored_state.identity.display_name.c_str(),
             _impl->stored_state.identity.device_id.c_str());
    return true;
}

bool LoRaMeshApp::run()
{
    if (_impl == nullptr) {
        return false;
    }

    if (!jc4880::lora_mesh::load_stored_state(_impl->stored_state)) {
        _impl->append_log("Failed to reload persisted LoRa mesh state on app open; using cached state");
    }
    for (PeerInfo &peer : _impl->stored_state.peers) {
        peer.presence = jc4880::lora_mesh::PeerPresence::Unknown;
    }
    _impl->lock();
    _impl->trace_event_locked("App run requested");
    _impl->show_targets_locked();
    const bool radio_enabled = _impl->stored_state.settings.radio_enabled;
    _impl->startup_started = !radio_enabled;
    _impl->startup_complete = !radio_enabled;
    _impl->startup_ok = false;
    _impl->status_text = radio_enabled
                             ? "Opening LoRa mesh..."
                             : "Device disabled. Please enable radio in Device Settings.";
    _impl->unlock();

    const bool ui_ok = _impl->build_ui();
    _impl->refresh_ui();

    if (_impl->ui_timer != nullptr) {
        lv_timer_del(_impl->ui_timer);
    }
    _impl->ui_timer = lv_timer_create(Impl::on_ui_timer, kUiTickMs, _impl);
    if (radio_enabled) {
        _impl->start_startup_task();
    }
    return ui_ok && (_impl->ui_timer != nullptr);
}

void LoRaMeshApp::requestSelfTestOnNextOpen()
{
    if (_impl == nullptr) {
        return;
    }

    _impl->lock();
    _impl->pending_self_test_on_next_open = true;
    _impl->trace_event_locked("Self-test scheduled for next app open");
    _impl->unlock();
}

bool LoRaMeshApp::startSelfTestFromSettings()
{
    if (_impl == nullptr) {
        return false;
    }

    _impl->lock();
    if (_impl->self_test_running) {
        _impl->unlock();
        return true;
    }

    _impl->pending_self_test_on_next_open = false;
    _impl->status_text = "LoRa self-test: starting";
    _impl->trace_event_locked("Settings requested background self-test");
    _impl->start_self_test_task();
    const bool started = _impl->self_test_running;
    _impl->unlock();
    return started;
}

std::string LoRaMeshApp::getSelfTestStatus(bool *is_running, bool *has_result) const
{
    if (_impl == nullptr) {
        if (is_running != nullptr) {
            *is_running = false;
        }
        if (has_result != nullptr) {
            *has_result = false;
        }
        return "LoRa self-test unavailable";
    }

    std::string summary;
    bool running = false;
    bool ran = false;
    _impl->lock();
    summary = _impl->self_test_summary;
    running = _impl->self_test_running;
    ran = _impl->self_test_ran;
    _impl->unlock();

    if (is_running != nullptr) {
        *is_running = running;
    }
    if (has_result != nullptr) {
        *has_result = ran;
    }
    return summary;
}

bool LoRaMeshApp::pause()
{
    if (_impl != nullptr) {
        _impl->lock();
        _impl->trace_event_locked("App pause requested");
        _impl->request_self_test_stop_locked("app paused");
        _impl->unlock();
        while (_impl->self_test_task != nullptr) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if ((_impl != nullptr) && (_impl->ui_timer != nullptr)) {
        lv_timer_pause(_impl->ui_timer);
    }
    return true;
}

bool LoRaMeshApp::resume()
{
    if ((_impl != nullptr) && (_impl->ui_timer != nullptr)) {
        _impl->trace_event("App resume requested");
        lv_timer_resume(_impl->ui_timer);
        _impl->refresh_ui();
    }
    return true;
}

bool LoRaMeshApp::back()
{
    if ((_impl != nullptr) && (_impl->view_mode != ViewMode::Targets)) {
        _impl->lock();
        _impl->trace_event_locked("Back requested: returning to target list");
        _impl->show_targets_locked();
        _impl->unlock();
        _impl->set_keyboard_visible(false);
        _impl->refresh_ui();
        return true;
    }
    if (_impl != nullptr) {
        _impl->trace_event("Back requested: closing app");
    }
    return notifyCoreClosed();
}

bool LoRaMeshApp::close()
{
    if (_impl == nullptr) {
        return true;
    }

    _impl->lock();
    _impl->trace_event_locked("App close requested");
    _impl->request_self_test_stop_locked("app closing");
    _impl->unlock();
    _impl->rx_task_stop = true;
    while (_impl->rx_task != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    while (_impl->startup_task != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    while (_impl->self_test_task != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (_impl->ui_timer != nullptr) {
        lv_timer_del(_impl->ui_timer);
        _impl->ui_timer = nullptr;
    }

    _impl->lock();
    _impl->deinit_radio_locked();
    _impl->conversation.clear();
    _impl->log_text.clear();
    _impl->conversation_dirty = true;
    _impl->target_list_dirty = true;
    _impl->self_test_ran = false;
    _impl->self_test_ok = false;
    _impl->self_test_running = false;
    _impl->self_test_stop_requested = false;
    _impl->self_test_summary = "Mode: idle";
    _impl->startup_started = false;
    _impl->startup_complete = false;
    _impl->startup_ok = false;
    _impl->self_test_task = nullptr;
    _impl->view_mode = ViewMode::Targets;
    _impl->selected_conversation_id = jc4880::lora_mesh::kCommonConversationId;
    _impl->selected_peer_id.clear();
    _impl->release_ui();
    _impl->unlock();
    return true;
}

bool LoRaMeshApp::queueDebugUiAction(int action, const std::string &peer_id)
{
    if ((_impl == nullptr) || (_impl->root == nullptr)) {
        return false;
    }

    auto *context = new (std::nothrow) DebugUiActionContext{
        .app = this,
        .action = static_cast<DebugUiAction>(action),
        .peer_id = peer_id,
    };
    if (context == nullptr) {
        return false;
    }

    const auto apply = [](void *raw_context) {
        std::unique_ptr<DebugUiActionContext> context(static_cast<DebugUiActionContext *>(raw_context));
        if ((context == nullptr) || (context->app == nullptr) || (context->app->_impl == nullptr)) {
            return;
        }

        LoRaMeshApp::Impl *impl = context->app->_impl;
        impl->lock();
        switch (context->action) {
        case DebugUiAction::ShowTargets:
            impl->status_text = "Serial debug opened target list";
            impl->trace_event_locked("Serial debug opened target list");
            impl->show_targets_locked();
            break;
        case DebugUiAction::ShowCommonChat:
            impl->status_text = "Serial debug opened common chat";
            impl->trace_event_locked("Serial debug opened common chat");
            impl->show_common_chat_locked();
            break;
        case DebugUiAction::ShowPeerChat:
            if (impl->find_peer_locked(context->peer_id) == nullptr) {
                impl->status_text = "Serial debug peer not found";
                impl->trace_event_locked(std::string("Serial debug failed to open peer chat: ") + context->peer_id);
            } else {
                impl->status_text = std::string("Serial debug opened peer chat ") + context->peer_id;
                impl->trace_event_locked(std::string("Serial debug opened peer chat ") + context->peer_id);
                impl->show_peer_chat_locked(context->peer_id);
            }
            break;
        case DebugUiAction::StartSelfTest:
            if (!impl->startup_complete) {
                impl->status_text = "LoRa radio is still opening";
                impl->trace_event_locked("Serial debug self-test start rejected because startup is incomplete");
            } else if (impl->self_test_running) {
                impl->trace_event_locked("Serial debug self-test start ignored because self-test is already running");
            } else {
                impl->trace_event_locked("Serial debug requested self-test start");
                impl->status_text = "LoRa self-test: starting";
                impl->start_self_test_task();
            }
            break;
        case DebugUiAction::StopSelfTest:
            if (impl->self_test_running) {
                impl->trace_event_locked("Serial debug requested self-test stop");
                impl->request_self_test_stop_locked("serial debug");
            } else {
                impl->trace_event_locked("Serial debug self-test stop ignored because no self-test is running");
                impl->status_text = "LoRa self-test is not running";
            }
            break;
        case DebugUiAction::SendCommonMessage:
            impl->selected_conversation_id = jc4880::lora_mesh::kCommonConversationId;
            impl->selected_peer_id.clear();
            impl->show_common_chat_locked();
            impl->trace_event_locked(std::string("Serial debug sending common message: ") + context->peer_id);
            impl->unlock();
            (void)impl->start_send_task(context->peer_id, false, false);
            impl->refresh_ui();
            return;
        }
        impl->unlock();
        impl->refresh_ui();
    };

    bsp_display_lock(0);
    const lv_res_t result = lv_async_call(apply, context);
    bsp_display_unlock();
    if (result != LV_RES_OK) {
        delete context;
        return false;
    }

    return true;
}

bool LoRaMeshApp::debugShowTargetsVisible()
{
    return queueDebugUiAction(static_cast<int>(DebugUiAction::ShowTargets), std::string());
}

bool LoRaMeshApp::debugShowCommonChatVisible()
{
    return queueDebugUiAction(static_cast<int>(DebugUiAction::ShowCommonChat), std::string());
}

bool LoRaMeshApp::debugShowSettingsVisible()
{
    return queueDebugUiAction(static_cast<int>(DebugUiAction::ShowTargets), std::string());
}

bool LoRaMeshApp::debugShowPeerChatVisible(const std::string &peer_id)
{
    return queueDebugUiAction(static_cast<int>(DebugUiAction::ShowPeerChat), peer_id);
}

bool LoRaMeshApp::debugRunSelfTestVisible()
{
    return queueDebugUiAction(static_cast<int>(DebugUiAction::StartSelfTest), std::string());
}

bool LoRaMeshApp::debugStopSelfTestVisible()
{
    return queueDebugUiAction(static_cast<int>(DebugUiAction::StopSelfTest), std::string());
}

bool LoRaMeshApp::debugSendCommonMessageVisible(const std::string &message)
{
    return queueDebugUiAction(static_cast<int>(DebugUiAction::SendCommonMessage), message);
}

std::string LoRaMeshApp::debugDescribeState() const
{
    if (_impl == nullptr) {
        return "app=null";
    }

    std::string status;
    std::string self_test;
    std::string selected_chat;
    std::string selected_peer;
    ViewMode view_mode = ViewMode::Targets;
    bool radio_ready = false;
    bool startup_started = false;
    bool startup_complete = false;
    bool startup_ok = false;
    bool self_test_running = false;
    bool self_test_ran = false;
    bool self_test_ok = false;
    size_t peer_count = 0;
    size_t pending_pair_count = 0;
    size_t conversation_count = 0;
    bool root_ready = false;
    bool rx_task_ready = false;

    _impl->lock();
    status = _impl->status_text;
    self_test = _impl->self_test_summary;
    selected_chat = _impl->selected_conversation_id;
    selected_peer = _impl->selected_peer_id;
    view_mode = _impl->view_mode;
    radio_ready = _impl->radio_ready;
    startup_started = _impl->startup_started;
    startup_complete = _impl->startup_complete;
    startup_ok = _impl->startup_ok;
    self_test_running = _impl->self_test_running;
    self_test_ran = _impl->self_test_ran;
    self_test_ok = _impl->self_test_ok;
    peer_count = _impl->stored_state.peers.size();
    pending_pair_count = _impl->pending_pair_requests.size();
    conversation_count = _impl->conversation.size();
    root_ready = _impl->root != nullptr;
    rx_task_ready = _impl->rx_task != nullptr;
    _impl->unlock();

    std::ostringstream stream;
    stream << "view=" << view_mode_name(view_mode)
           << " root=" << (root_ready ? "yes" : "no")
           << " startup_started=" << (startup_started ? "yes" : "no")
           << " startup_complete=" << (startup_complete ? "yes" : "no")
           << " startup_ok=" << (startup_ok ? "yes" : "no")
           << " radio_ready=" << (radio_ready ? "yes" : "no")
           << " rx_task=" << (rx_task_ready ? "set" : "null")
           << " self_test_running=" << (self_test_running ? "yes" : "no")
           << " self_test_ran=" << (self_test_ran ? "yes" : "no")
           << " self_test_ok=" << (self_test_ok ? "yes" : "no")
           << " peers=" << peer_count
           << " pending_pairs=" << pending_pair_count
           << " conversation=" << conversation_count
           << " selected_chat=\"" << selected_chat << "\""
           << " selected_peer=\"" << selected_peer << "\""
           << " status=\"" << status << "\""
           << " self_test=\"" << self_test << "\"";
    return stream.str();
}

std::vector<std::string> LoRaMeshApp::debugListPeerSummaries() const
{
    std::vector<std::string> lines;
    if (_impl == nullptr) {
        return lines;
    }

    std::vector<PeerInfo> peers;
    _impl->lock();
    peers = _impl->stored_state.peers;
    _impl->unlock();

    lines.reserve(peers.size());
    for (const PeerInfo &peer : peers) {
        std::ostringstream stream;
        stream << peer.device_id << " name=\"" << (peer.display_name.empty() ? peer.device_id : peer.display_name)
               << "\" presence=" << jc4880::lora_mesh::presence_name(peer.presence)
               << " rssi=" << peer.last_rssi
               << " snr=" << peer.last_snr;
        lines.push_back(stream.str());
    }
    return lines;
}

std::vector<std::string> LoRaMeshApp::debugRecentLogLines(size_t max_lines) const
{
    std::vector<std::string> lines;
    if ((_impl == nullptr) || (max_lines == 0)) {
        return lines;
    }

    std::string log_copy;
    _impl->lock();
    log_copy = _impl->log_text;
    _impl->unlock();

    std::istringstream stream(log_copy);
    for (std::string line; std::getline(stream, line);) {
        lines.push_back(line);
    }
    if (lines.size() > max_lines) {
        lines.erase(lines.begin(), lines.end() - static_cast<std::ptrdiff_t>(max_lines));
    }
    return lines;
}