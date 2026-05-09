#include "LoRaMeshCrypto.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

#include "esp_random.h"

namespace jc4880::lora_mesh {
namespace {

std::string bytes_to_hex(const uint8_t *data, size_t size)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(size * 2);
    for (size_t index = 0; index < size; ++index) {
        out.push_back(kHex[(data[index] >> 4) & 0x0F]);
        out.push_back(kHex[data[index] & 0x0F]);
    }
    return out;
}

bool hex_to_bytes(const std::string &hex, std::vector<uint8_t> &out)
{
    if ((hex.size() % 2U) != 0U) {
        return false;
    }

    auto hex_value = [](char value) -> int {
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
    };

    out.clear();
    out.reserve(hex.size() / 2U);
    for (size_t index = 0; index < hex.size(); index += 2U) {
        const int high = hex_value(hex[index]);
        const int low = hex_value(hex[index + 1U]);
        if ((high < 0) || (low < 0)) {
            out.clear();
            return false;
        }
        out.push_back(static_cast<uint8_t>((high << 4) | low));
    }
    return true;
}

uint32_t fnv1a_32(const std::string &text)
{
    uint32_t hash = 2166136261U;
    for (const unsigned char value : text) {
        hash ^= value;
        hash *= 16777619U;
    }
    return hash;
}

std::string make_device_id(const std::string &public_key_hex)
{
    char buffer[9] = {};
    std::snprintf(buffer, sizeof(buffer), "%08lX", static_cast<unsigned long>(fnv1a_32(public_key_hex)));
    return std::string(buffer);
}

std::vector<uint8_t> derive_keystream(const std::string &seed_material, size_t size)
{
    std::vector<uint8_t> stream(size, 0);
    uint32_t state = fnv1a_32(seed_material);
    for (size_t index = 0; index < size; ++index) {
        state ^= static_cast<uint32_t>(index * 2654435761U);
        state *= 1664525U;
        state += 1013904223U;
        stream[index] = static_cast<uint8_t>((state >> ((index % 4U) * 8U)) & 0xFFU);
    }
    return stream;
}

std::string derive_auth_hex(const std::string &seed_material, const std::vector<uint8_t> &cipher_bytes)
{
    std::string auth_input = seed_material;
    auth_input.append(reinterpret_cast<const char *>(cipher_bytes.data()), cipher_bytes.size());
    const uint32_t hash = fnv1a_32(auth_input);
    uint8_t bytes[4] = {
        static_cast<uint8_t>((hash >> 24) & 0xFFU),
        static_cast<uint8_t>((hash >> 16) & 0xFFU),
        static_cast<uint8_t>((hash >> 8) & 0xFFU),
        static_cast<uint8_t>(hash & 0xFFU),
    };
    return bytes_to_hex(bytes, sizeof(bytes));
}

std::string symmetric_seed(const std::string &left_public_key_hex,
                           const std::string &right_public_key_hex,
                           const std::string &pair_secret_hex,
                           const std::string &nonce_hex)
{
    std::array<std::string, 2> ordered_keys = {left_public_key_hex, right_public_key_hex};
    std::sort(ordered_keys.begin(), ordered_keys.end());
    return ordered_keys[0] + ":" + ordered_keys[1] + ":" + pair_secret_hex + ":" + nonce_hex;
}

std::string random_hex(size_t size)
{
    std::vector<uint8_t> bytes(size, 0);
    esp_fill_random(bytes.data(), bytes.size());
    return bytes_to_hex(bytes.data(), bytes.size());
}

} // namespace

const char *packet_kind_name(PacketKind kind)
{
    switch (kind) {
    case PacketKind::PublicChat:
        return "public_chat";
    case PacketKind::PrivateChat:
        return "private_chat";
    case PacketKind::PairRequest:
        return "pair_request";
    case PacketKind::PairAccept:
        return "pair_accept";
    case PacketKind::PairReject:
        return "pair_reject";
    case PacketKind::PairConfirm:
        return "pair_confirm";
    case PacketKind::Ack:
        return "ack";
    case PacketKind::Hello:
        return "hello";
    case PacketKind::Presence:
        return "presence";
    }
    return "unknown";
}

const char *presence_name(PeerPresence presence)
{
    switch (presence) {
    case PeerPresence::Unknown:
        return "unknown";
    case PeerPresence::Offline:
        return "offline";
    case PeerPresence::Online:
        return "online";
    }
    return "unknown";
}

CryptoIdentity cryptoGenerateIdentity()
{
    CryptoIdentity identity = {};
    identity.public_key_hex = random_hex(16);
    identity.private_key_hex = random_hex(16);
    identity.device_id = make_device_id(identity.public_key_hex);
    identity.display_name = std::string("P4-") + identity.device_id.substr(0, 4);
    return identity;
}

std::string cryptoGenerateSharedSecretHex()
{
    return random_hex(16);
}

std::string cryptoGenerateNonceHex()
{
    return random_hex(8);
}

std::string cryptoGetPublicKeyHex(const CryptoIdentity &identity)
{
    return identity.public_key_hex;
}

bool cryptoEncryptForPeer(const CryptoIdentity &local_identity,
                          const std::string &peer_public_key_hex,
                          const std::string &pair_secret_hex,
                          const std::string &nonce_hex,
                          const std::string &plaintext,
                          std::string &cipher_hex,
                          std::string &auth_hex)
{
    const std::string seed = symmetric_seed(local_identity.public_key_hex, peer_public_key_hex, pair_secret_hex, nonce_hex);
    const std::vector<uint8_t> keystream = derive_keystream(seed, plaintext.size());
    std::vector<uint8_t> cipher_bytes(plaintext.size(), 0);
    for (size_t index = 0; index < plaintext.size(); ++index) {
        cipher_bytes[index] = static_cast<uint8_t>(static_cast<uint8_t>(plaintext[index]) ^ keystream[index]);
    }
    cipher_hex = bytes_to_hex(cipher_bytes.data(), cipher_bytes.size());
    auth_hex = derive_auth_hex(seed, cipher_bytes);
    return true;
}

bool cryptoDecryptFromPeer(const CryptoIdentity &local_identity,
                          const std::string &peer_public_key_hex,
                          const std::string &pair_secret_hex,
                          const std::string &nonce_hex,
                          const std::string &cipher_hex,
                          const std::string &auth_hex,
                          std::string &plaintext)
{
    std::vector<uint8_t> cipher_bytes;
    if (!hex_to_bytes(cipher_hex, cipher_bytes)) {
        return false;
    }

    const std::string seed = symmetric_seed(local_identity.public_key_hex, peer_public_key_hex, pair_secret_hex, nonce_hex);
    if (derive_auth_hex(seed, cipher_bytes) != auth_hex) {
        return false;
    }

    const std::vector<uint8_t> keystream = derive_keystream(seed, cipher_bytes.size());
    plaintext.assign(cipher_bytes.size(), '\0');
    for (size_t index = 0; index < cipher_bytes.size(); ++index) {
        plaintext[index] = static_cast<char>(cipher_bytes[index] ^ keystream[index]);
    }
    return true;
}

} // namespace jc4880::lora_mesh