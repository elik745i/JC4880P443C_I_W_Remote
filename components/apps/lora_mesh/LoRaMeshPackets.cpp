#include "LoRaMeshPackets.hpp"

#include <vector>

#include "cJSON.h"
#include "mbedtls/base64.h"

namespace jc4880::lora_mesh {
namespace {

bool is_pair_control_packet(PacketKind kind)
{
    switch (kind) {
        case PacketKind::PairRequest:
        case PacketKind::PairAccept:
        case PacketKind::PairReject:
        case PacketKind::Unpair:
        case PacketKind::PairConfirm:
            return true;
        default:
            return false;
    }
}

int hex_nibble_value(char value)
{
    if ((value >= '0') && (value <= '9')) {
        return value - '0';
    }
    if ((value >= 'a') && (value <= 'f')) {
        return 10 + (value - 'a');
    }
    if ((value >= 'A') && (value <= 'F')) {
        return 10 + (value - 'A');
    }
    return -1;
}

bool hex_to_bytes(const std::string &hex, std::vector<uint8_t> &bytes)
{
    if ((hex.size() % 2U) != 0U) {
        return false;
    }

    bytes.clear();
    bytes.reserve(hex.size() / 2U);
    for (size_t index = 0; index < hex.size(); index += 2U) {
        const int high = hex_nibble_value(hex[index]);
        const int low = hex_nibble_value(hex[index + 1U]);
        if ((high < 0) || (low < 0)) {
            bytes.clear();
            return false;
        }
        bytes.push_back(static_cast<uint8_t>((high << 4) | low));
    }
    return true;
}

std::string bytes_to_hex(const uint8_t *data, size_t size)
{
    static constexpr char kHexDigits[] = "0123456789abcdef";

    std::string hex;
    hex.reserve(size * 2U);
    for (size_t index = 0; index < size; ++index) {
        const uint8_t value = data[index];
        hex.push_back(kHexDigits[(value >> 4) & 0x0F]);
        hex.push_back(kHexDigits[value & 0x0F]);
    }
    return hex;
}

std::string hex_to_base64(const std::string &hex)
{
    std::vector<uint8_t> bytes;
    if (!hex_to_bytes(hex, bytes)) {
        return std::string();
    }

    std::string encoded;
    encoded.resize(((bytes.size() + 2U) / 3U) * 4U);
    size_t output_length = 0;
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char *>(encoded.data()),
                              encoded.size(),
                              &output_length,
                              bytes.data(),
                              bytes.size()) != 0) {
        return std::string();
    }
    encoded.resize(output_length);
    return encoded;
}

std::string base64_to_hex(const std::string &encoded)
{
    if (encoded.empty()) {
        return std::string();
    }

    std::vector<uint8_t> decoded(encoded.size());
    size_t output_length = 0;
    if (mbedtls_base64_decode(decoded.data(),
                              decoded.size(),
                              &output_length,
                              reinterpret_cast<const unsigned char *>(encoded.data()),
                              encoded.size()) != 0) {
        return std::string();
    }
    decoded.resize(output_length);
    return bytes_to_hex(decoded.data(), decoded.size());
}

void add_string(cJSON *object, const char *key, const std::string &value)
{
    if (!value.empty()) {
        cJSON_AddStringToObject(object, key, value.c_str());
    }
}

std::string read_string(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && (item->valuestring != nullptr) ? std::string(item->valuestring) : std::string();
}

std::string read_string_alias(const cJSON *object, const char *short_key, const char *legacy_key)
{
    std::string value = read_string(object, short_key);
    if (value.empty() && (legacy_key != nullptr)) {
        value = read_string(object, legacy_key);
    }
    return value;
}

const cJSON *get_item_alias(const cJSON *object, const char *short_key, const char *legacy_key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, short_key);
    if ((item == nullptr) && (legacy_key != nullptr)) {
        item = cJSON_GetObjectItemCaseSensitive(object, legacy_key);
    }
    return item;
}

} // namespace

bool encode_mesh_packet(const MeshPacket &packet, std::string &encoded)
{
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) {
        return false;
    }

    const bool pair_control_packet = is_pair_control_packet(packet.kind);
    const bool compact_private_packet = packet.kind == PacketKind::PrivateChat;
    const bool compact_encrypted_packet = packet.encrypted && !pair_control_packet;

    add_string(root, "k", packet_kind_name(packet.kind));
    add_string(root, "sid", packet.sender_id);
    if (!pair_control_packet && !compact_private_packet) {
        add_string(root, "sn", packet.sender_name);
    }
    if ((packet.kind != PacketKind::PairRequest) || !packet.target_id.empty()) {
        add_string(root, "tid", packet.target_id);
    }
    add_string(root, "pid", packet.pair_id);
    add_string(root, "cid", packet.chat_id);
    if (!pair_control_packet) {
        add_string(root, "m", packet.msg_id);
        cJSON_AddNumberToObject(root, "ts", static_cast<double>(packet.timestamp_ms));
        cJSON_AddNumberToObject(root, "t", packet.ttl);
        cJSON_AddBoolToObject(root, "e", packet.encrypted);
    }
    if ((packet.kind == PacketKind::PrivateChat) && (packet.message_number != 0U)) {
        cJSON_AddNumberToObject(root, "mn", static_cast<double>(packet.message_number));
    }
    add_string(root, "rt", packet.receipt_type);
    add_string(root, "rm", packet.receipt_message_id);
    if (pair_control_packet) {
        add_string(root, "p", packet.payload);
        add_string(root, "n", packet.nonce_hex);
        const std::string auth_base64 = hex_to_base64(packet.auth_hex);
        const std::string public_key_base64 = hex_to_base64(packet.public_key_hex);
        const std::string ephemeral_public_key_base64 = hex_to_base64(packet.ephemeral_public_key_hex);
        const std::string target_public_key_base64 = hex_to_base64(packet.target_public_key_hex);
        add_string(root, auth_base64.empty() ? "a" : "a64", auth_base64.empty() ? packet.auth_hex : auth_base64);
        add_string(root, public_key_base64.empty() ? "pk" : "pk64", public_key_base64.empty() ? packet.public_key_hex : public_key_base64);
        add_string(root, ephemeral_public_key_base64.empty() ? "epk" : "epk64",
                   ephemeral_public_key_base64.empty() ? packet.ephemeral_public_key_hex : ephemeral_public_key_base64);
        add_string(root, target_public_key_base64.empty() ? "tpk" : "tpk64",
                   target_public_key_base64.empty() ? packet.target_public_key_hex : target_public_key_base64);
    } else {
        if (compact_encrypted_packet) {
            const std::string payload_base64 = hex_to_base64(packet.payload);
            const std::string nonce_base64 = hex_to_base64(packet.nonce_hex);
            const std::string auth_base64 = hex_to_base64(packet.auth_hex);
            add_string(root, payload_base64.empty() ? "p" : "p64", payload_base64.empty() ? packet.payload : payload_base64);
            add_string(root, nonce_base64.empty() ? "n" : "n64", nonce_base64.empty() ? packet.nonce_hex : nonce_base64);
            add_string(root, auth_base64.empty() ? "a" : "a64", auth_base64.empty() ? packet.auth_hex : auth_base64);
        } else {
            add_string(root, "p", packet.payload);
            add_string(root, "n", packet.nonce_hex);
            add_string(root, "a", packet.auth_hex);
        }
        if (!compact_private_packet) {
            add_string(root, "pk", packet.public_key_hex);
            add_string(root, "epk", packet.ephemeral_public_key_hex);
            add_string(root, "tpk", packet.target_public_key_hex);
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == nullptr) {
        return false;
    }

    encoded.assign(json);
    cJSON_free(json);
    return true;
}

bool decode_mesh_packet(const std::string &encoded, MeshPacket &packet)
{
    cJSON *root = cJSON_Parse(encoded.c_str());
    if (root == nullptr) {
        return false;
    }

    const std::string kind = read_string_alias(root, "k", "kind");
    if (kind == "public_chat") {
        packet.kind = PacketKind::PublicChat;
    } else if (kind == "private_chat") {
        packet.kind = PacketKind::PrivateChat;
    } else if (kind == "pair_request") {
        packet.kind = PacketKind::PairRequest;
    } else if (kind == "pair_accept") {
        packet.kind = PacketKind::PairAccept;
    } else if (kind == "pair_reject") {
        packet.kind = PacketKind::PairReject;
    } else if (kind == "unpair") {
        packet.kind = PacketKind::Unpair;
    } else if (kind == "pair_confirm") {
        packet.kind = PacketKind::PairConfirm;
    } else if (kind == "ack") {
        packet.kind = PacketKind::Ack;
    } else if (kind == "hello") {
        packet.kind = PacketKind::Hello;
    } else if (kind == "presence") {
        packet.kind = PacketKind::Presence;
    } else {
        cJSON_Delete(root);
        return false;
    }

    packet.sender_id = read_string_alias(root, "sid", "sender_id");
    packet.sender_name = read_string_alias(root, "sn", "sender_name");
    packet.target_id = read_string_alias(root, "tid", "target_id");
    packet.pair_id = read_string_alias(root, "pid", "pair_id");
    packet.chat_id = read_string_alias(root, "cid", "chat_id");
    packet.msg_id = read_string_alias(root, "m", "msg_id");
    packet.receipt_type = read_string_alias(root, "rt", "receipt_type");
    packet.receipt_message_id = read_string_alias(root, "rm", "receipt_message_id");
    const std::string payload_base64 = read_string(root, "p64");
    packet.payload = payload_base64.empty() ? read_string_alias(root, "p", "payload") : base64_to_hex(payload_base64);
    const std::string nonce_base64 = read_string(root, "n64");
    packet.nonce_hex = nonce_base64.empty() ? read_string_alias(root, "n", "nonce") : base64_to_hex(nonce_base64);
    if (is_pair_control_packet(packet.kind)) {
        const std::string auth_base64 = read_string(root, "a64");
        const std::string public_key_base64 = read_string(root, "pk64");
        const std::string ephemeral_public_key_base64 = read_string(root, "epk64");
        const std::string target_public_key_base64 = read_string(root, "tpk64");
        packet.auth_hex = auth_base64.empty() ? read_string_alias(root, "a", "auth") : base64_to_hex(auth_base64);
        packet.public_key_hex = public_key_base64.empty() ? read_string_alias(root, "pk", "public_key") : base64_to_hex(public_key_base64);
        packet.ephemeral_public_key_hex = ephemeral_public_key_base64.empty() ? read_string_alias(root, "epk", "ephemeral_public_key") : base64_to_hex(ephemeral_public_key_base64);
        packet.target_public_key_hex = target_public_key_base64.empty() ? read_string_alias(root, "tpk", "target_public_key") : base64_to_hex(target_public_key_base64);
    } else {
        const std::string auth_base64 = read_string(root, "a64");
        packet.auth_hex = auth_base64.empty() ? read_string_alias(root, "a", "auth") : base64_to_hex(auth_base64);
        packet.public_key_hex = read_string_alias(root, "pk", "public_key");
        packet.ephemeral_public_key_hex = read_string_alias(root, "epk", "ephemeral_public_key");
        packet.target_public_key_hex = read_string_alias(root, "tpk", "target_public_key");
    }

    if (packet.msg_id.empty() && !packet.pair_id.empty() && is_pair_control_packet(packet.kind)) {
        packet.msg_id = packet.pair_id;
    }

    const cJSON *timestamp = get_item_alias(root, "ts", "timestamp");
    if (cJSON_IsNumber(timestamp)) {
        packet.timestamp_ms = static_cast<int64_t>(timestamp->valuedouble);
    }

    const cJSON *ttl = get_item_alias(root, "t", "ttl");
    if (cJSON_IsNumber(ttl)) {
        packet.ttl = static_cast<uint8_t>(ttl->valueint);
    }

    const cJSON *message_number = get_item_alias(root, "mn", "message_number");
    if (cJSON_IsNumber(message_number)) {
        packet.message_number = static_cast<uint32_t>(message_number->valueint);
    }

    const cJSON *encrypted = get_item_alias(root, "e", "encrypted");
    packet.encrypted = cJSON_IsTrue(encrypted);

    cJSON_Delete(root);
    return !packet.sender_id.empty() && (!packet.msg_id.empty() || !packet.pair_id.empty());
}

} // namespace jc4880::lora_mesh