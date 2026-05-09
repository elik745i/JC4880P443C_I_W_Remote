#include "LoRaMeshPackets.hpp"

#include "cJSON.h"

namespace jc4880::lora_mesh {
namespace {

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

} // namespace

bool encode_mesh_packet(const MeshPacket &packet, std::string &encoded)
{
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) {
        return false;
    }

    add_string(root, "kind", packet_kind_name(packet.kind));
    add_string(root, "sender_id", packet.sender_id);
    add_string(root, "sender_name", packet.sender_name);
    add_string(root, "target_id", packet.target_id);
    add_string(root, "msg_id", packet.msg_id);
    cJSON_AddNumberToObject(root, "timestamp", static_cast<double>(packet.timestamp_ms));
    cJSON_AddNumberToObject(root, "ttl", packet.ttl);
    cJSON_AddBoolToObject(root, "encrypted", packet.encrypted);
    add_string(root, "payload", packet.payload);
    add_string(root, "nonce", packet.nonce_hex);
    add_string(root, "auth", packet.auth_hex);
    add_string(root, "public_key", packet.public_key_hex);
    add_string(root, "pair_secret", packet.pair_secret_hex);

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

    const std::string kind = read_string(root, "kind");
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

    packet.sender_id = read_string(root, "sender_id");
    packet.sender_name = read_string(root, "sender_name");
    packet.target_id = read_string(root, "target_id");
    packet.msg_id = read_string(root, "msg_id");
    packet.payload = read_string(root, "payload");
    packet.nonce_hex = read_string(root, "nonce");
    packet.auth_hex = read_string(root, "auth");
    packet.public_key_hex = read_string(root, "public_key");
    packet.pair_secret_hex = read_string(root, "pair_secret");

    const cJSON *timestamp = cJSON_GetObjectItemCaseSensitive(root, "timestamp");
    if (cJSON_IsNumber(timestamp)) {
        packet.timestamp_ms = static_cast<int64_t>(timestamp->valuedouble);
    }

    const cJSON *ttl = cJSON_GetObjectItemCaseSensitive(root, "ttl");
    if (cJSON_IsNumber(ttl)) {
        packet.ttl = static_cast<uint8_t>(ttl->valueint);
    }

    const cJSON *encrypted = cJSON_GetObjectItemCaseSensitive(root, "encrypted");
    packet.encrypted = cJSON_IsTrue(encrypted);

    cJSON_Delete(root);
    return !packet.sender_id.empty() && !packet.msg_id.empty();
}

} // namespace jc4880::lora_mesh