#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "LoRaPinProfile.hpp"

namespace jc4880::lora_mesh {

inline constexpr const char *kBroadcastTargetId = "broadcast";
inline constexpr const char *kCommonConversationId = "common";

enum class PacketKind : uint8_t {
    PublicChat = 1,
    PrivateChat,
    PairRequest,
    PairAccept,
    PairReject,
    PairConfirm,
    Ack,
    Hello,
    Presence,
};

enum class ChatTargetKind : uint8_t {
    Common = 0,
    Peer,
};

enum class PeerPresence : uint8_t {
    Unknown = 0,
    Offline,
    Online,
};

enum class PairingState : uint8_t {
    Idle = 0,
    PairRequestSent,
    WaitingForAccept,
    PairRequestReceived,
    WaitingUserInput,
    AcceptSentWaitingConfirm,
    Paired,
    Rejected,
    Failed,
    Timeout,
};

enum class RadioModule : uint8_t {
    E22_400M22S = 0,
    E22_400T22S,
    E220_400T22D,
};

struct CryptoIdentity {
    std::string device_id;
    std::string display_name;
    std::string public_key_hex;
    std::string private_key_hex;
};

struct PeerInfo {
    std::string device_id;
    std::string display_name;
    std::string public_key_hex;
    bool trusted = false;
    int64_t paired_ms = 0;
    int64_t last_seen_ms = 0;
    int last_rssi = 0;
    int last_snr = 0;
    PeerPresence presence = PeerPresence::Unknown;
};

struct PendingPairRequest {
    std::string pair_id;
    std::string device_id;
    std::string display_name;
    std::string public_key_hex;
    std::string target_public_key_hex;
    PairingState state = PairingState::Idle;
    std::string msg_id;
    std::string nonce_hex;
    int64_t received_ms = 0;
    int64_t expires_ms = 0;
    int last_rssi = 0;
    int last_snr = 0;
};

struct MeshSettings {
    uint32_t frequency_hz = 433125000;
    uint8_t spreading_factor = 9;
    uint8_t bandwidth = 4;
    uint8_t coding_rate = 1;
    bool radio_enabled = true;
    // Shipping default is the UART transparent variant (per release notes
    // v1.3.7). Other variants are runtime-selectable via the `lora.module.set`
    // serial command or the LoRa settings UI.
    RadioModule radio_module = RadioModule::E22_400T22S;
    int8_t spi_miso_gpio = pin_profile::kSpiMisoGpio;
    int8_t spi_mosi_gpio = pin_profile::kSpiMosiGpio;
    int8_t spi_sck_gpio = pin_profile::kSpiSckGpio;
    int8_t spi_nss_gpio = pin_profile::kSpiNssGpio;
    int8_t busy_gpio = pin_profile::kBusyGpio;
    int8_t dio1_gpio = pin_profile::kDio1Gpio;
    int8_t nrst_gpio = -1;
    int8_t txen_gpio = -1;
    int8_t rxen_gpio = -1;
    int8_t uart_tx_gpio = 30;
    int8_t uart_rx_gpio = 31;
    int8_t mode0_gpio = 51;
    int8_t mode1_gpio = 29;
    int8_t aux_gpio = 33;
    uint16_t e22_address = 0;
    uint8_t e22_uart_baud_index = 3;
    uint8_t e22_air_data_rate_index = 2;
    uint8_t e22_sub_packet_index = 0;
    uint8_t e22_tx_power_index = 0;
    bool e22_fixed_transmission = false;
    bool e22_rssi_ambient_noise = false;
    bool public_chat_encryption = false;
    std::string common_chat_name = "Common Mesh Chat";
    uint8_t hop_limit = 4;
    bool forwarding_enabled = true;
    bool antenna_warning_acknowledged = false;
    std::string public_group_key_hex;
};

struct ChatMessage {
    std::string conversation_id;
    std::string sender_id;
    std::string sender_name;
    std::string msg_id;
    std::string text;
    int64_t timestamp_ms = 0;
    int last_rssi = 0;
    int last_snr = 0;
    bool outgoing = false;
    bool encrypted = false;
    PacketKind kind = PacketKind::PublicChat;
};

struct MeshPacket {
    PacketKind kind = PacketKind::PublicChat;
    std::string sender_id;
    std::string sender_name;
    std::string target_id = kBroadcastTargetId;
    std::string pair_id;
    std::string msg_id;
    int64_t timestamp_ms = 0;
    uint8_t ttl = 0;
    bool encrypted = false;
    std::string payload;
    std::string nonce_hex;
    std::string auth_hex;
    std::string public_key_hex;
    std::string target_public_key_hex;
};

struct StoredState {
    CryptoIdentity identity;
    MeshSettings settings;
    std::vector<PeerInfo> peers;
};

struct ChatTargetSummary {
    ChatTargetKind kind = ChatTargetKind::Common;
    std::string conversation_id;
    std::string title;
    std::string subtitle;
    std::string peer_id;
    int64_t last_seen_ms = 0;
    int last_rssi = 0;
    int last_snr = 0;
    PeerPresence presence = PeerPresence::Unknown;
};

const char *packet_kind_name(PacketKind kind);
const char *presence_name(PeerPresence presence);
const char *pairing_state_name(PairingState state);

} // namespace jc4880::lora_mesh