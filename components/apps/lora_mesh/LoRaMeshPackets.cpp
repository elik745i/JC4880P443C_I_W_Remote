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

    add_string(root, "k", packet_kind_name(packet.kind));
    add_string(root, "sid", packet.sender_id);
    add_string(root, "sn", packet.sender_name);
    add_string(root, "tid", packet.target_id);
    add_string(root, "pid", packet.pair_id);
    add_string(root, "m", packet.msg_id);
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(packet.timestamp_ms));
    cJSON_AddNumberToObject(root, "t", packet.ttl);
    cJSON_AddBoolToObject(root, "e", packet.encrypted);
    add_string(root, "p", packet.payload);
    add_string(root, "n", packet.nonce_hex);
    add_string(root, "a", packet.auth_hex);
    add_string(root, "pk", packet.public_key_hex);
    add_string(root, "tpk", packet.target_public_key_hex);

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
    packet.msg_id = read_string_alias(root, "m", "msg_id");
    packet.payload = read_string_alias(root, "p", "payload");
    packet.nonce_hex = read_string_alias(root, "n", "nonce");
    packet.auth_hex = read_string_alias(root, "a", "auth");
    packet.public_key_hex = read_string_alias(root, "pk", "public_key");
    packet.target_public_key_hex = read_string_alias(root, "tpk", "target_public_key");

    const cJSON *timestamp = get_item_alias(root, "ts", "timestamp");
    if (cJSON_IsNumber(timestamp)) {
        packet.timestamp_ms = static_cast<int64_t>(timestamp->valuedouble);
    }

    const cJSON *ttl = get_item_alias(root, "t", "ttl");
    if (cJSON_IsNumber(ttl)) {
        packet.ttl = static_cast<uint8_t>(ttl->valueint);
    }

    const cJSON *encrypted = get_item_alias(root, "e", "encrypted");
    packet.encrypted = cJSON_IsTrue(encrypted);

    cJSON_Delete(root);
    return !packet.sender_id.empty() && !packet.msg_id.empty();
}

} // namespace jc4880::lora_mesh