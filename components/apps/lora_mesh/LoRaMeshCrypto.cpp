#include "LoRaMeshCrypto.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

#include "esp_random.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"

namespace jc4880::lora_mesh {
namespace {

constexpr mbedtls_ecp_group_id kIdentityCurve = MBEDTLS_ECP_DP_CURVE25519;
constexpr size_t kCurve25519ScalarBytes = 32U;
constexpr size_t kHmacBlockBytes = 64U;
constexpr size_t kSha256Bytes = 32U;

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

std::vector<uint8_t> random_bytes(size_t size)
{
    std::vector<uint8_t> bytes(size, 0);
    if (!bytes.empty()) {
        esp_fill_random(bytes.data(), bytes.size());
    }
    return bytes;
}

std::string random_hex(size_t size)
{
    const std::vector<uint8_t> bytes = random_bytes(size);
    return bytes_to_hex(bytes.data(), bytes.size());
}

int random_callback(void *, unsigned char *output, size_t output_len)
{
    if ((output == nullptr) || (output_len == 0U)) {
        return 0;
    }
    esp_fill_random(output, output_len);
    return 0;
}

bool hmac_sha256_bytes(const std::vector<uint8_t> &key,
                       const uint8_t *data,
                       size_t size,
                       std::array<uint8_t, kSha256Bytes> &digest)
{
    std::array<uint8_t, kHmacBlockBytes> normalized_key = {};
    if (key.size() > kHmacBlockBytes) {
        const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        if (info == nullptr) {
            return false;
        }
        std::array<uint8_t, kSha256Bytes> key_digest = {};
        if (mbedtls_md(info, key.data(), key.size(), key_digest.data()) != 0) {
            return false;
        }
        std::copy(key_digest.begin(), key_digest.end(), normalized_key.begin());
    } else if (!key.empty()) {
        std::copy(key.begin(), key.end(), normalized_key.begin());
    }

    std::array<uint8_t, kHmacBlockBytes> inner_pad = {};
    std::array<uint8_t, kHmacBlockBytes> outer_pad = {};
    for (size_t index = 0; index < kHmacBlockBytes; ++index) {
        inner_pad[index] = static_cast<uint8_t>(normalized_key[index] ^ 0x36U);
        outer_pad[index] = static_cast<uint8_t>(normalized_key[index] ^ 0x5CU);
    }

    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr) {
        return false;
    }

    std::vector<uint8_t> inner(inner_pad.begin(), inner_pad.end());
    if ((data != nullptr) && (size != 0U)) {
        inner.insert(inner.end(), data, data + size);
    }

    std::array<uint8_t, kSha256Bytes> inner_digest = {};
    if (mbedtls_md(info, inner.data(), inner.size(), inner_digest.data()) != 0) {
        return false;
    }

    std::vector<uint8_t> outer(outer_pad.begin(), outer_pad.end());
    outer.insert(outer.end(), inner_digest.begin(), inner_digest.end());
    return mbedtls_md(info, outer.data(), outer.size(), digest.data()) == 0;
}

bool hmac_sha256_text(const std::string &key_material,
                      const std::string &data,
                      std::array<uint8_t, kSha256Bytes> &digest)
{
    const std::vector<uint8_t> key(key_material.begin(), key_material.end());
    return hmac_sha256_bytes(key,
                             reinterpret_cast<const uint8_t *>(data.data()),
                             data.size(),
                             digest);
}

bool derive_shared_secret_bytes(const CryptoIdentity &local_identity,
                                const std::string &peer_public_key_hex,
                                std::vector<uint8_t> &shared_secret)
{
    std::vector<uint8_t> private_key_bytes;
    std::vector<uint8_t> peer_public_key_bytes;
    if (!hex_to_bytes(local_identity.private_key_hex, private_key_bytes) ||
        !hex_to_bytes(peer_public_key_hex, peer_public_key_bytes) ||
        (private_key_bytes.size() != kCurve25519ScalarBytes) ||
        peer_public_key_bytes.empty()) {
        return false;
    }

    mbedtls_ecp_group group;
    mbedtls_ecp_group_init(&group);
    mbedtls_mpi private_key;
    mbedtls_mpi_init(&private_key);
    mbedtls_mpi shared;
    mbedtls_mpi_init(&shared);
    mbedtls_ecp_point peer_public_key;
    mbedtls_ecp_point_init(&peer_public_key);

    bool ok = false;
    do {
        if (mbedtls_ecp_group_load(&group, kIdentityCurve) != 0) {
            break;
        }
        if (mbedtls_mpi_read_binary(&private_key, private_key_bytes.data(), private_key_bytes.size()) != 0) {
            break;
        }
        if (mbedtls_ecp_point_read_binary(&group,
                                          &peer_public_key,
                                          peer_public_key_bytes.data(),
                                          peer_public_key_bytes.size()) != 0) {
            break;
        }
        if ((mbedtls_ecp_check_privkey(&group, &private_key) != 0) ||
            (mbedtls_ecp_check_pubkey(&group, &peer_public_key) != 0)) {
            break;
        }
        if (mbedtls_ecdh_compute_shared(&group,
                                        &shared,
                                        &peer_public_key,
                                        &private_key,
                                        random_callback,
                                        nullptr) != 0) {
            break;
        }

        shared_secret.assign(kCurve25519ScalarBytes, 0U);
        if (mbedtls_mpi_write_binary(&shared, shared_secret.data(), shared_secret.size()) != 0) {
            shared_secret.clear();
            break;
        }
        ok = true;
    } while (false);

    mbedtls_ecp_point_free(&peer_public_key);
    mbedtls_mpi_free(&shared);
    mbedtls_mpi_free(&private_key);
    mbedtls_ecp_group_free(&group);
    return ok;
}

bool derive_key_bytes(const std::vector<uint8_t> &shared_secret,
                      const std::string &label,
                      std::array<uint8_t, kSha256Bytes> &derived_key)
{
    return hmac_sha256_bytes(shared_secret,
                             reinterpret_cast<const uint8_t *>(label.data()),
                             label.size(),
                             derived_key);
}

bool derive_keystream(const std::array<uint8_t, kSha256Bytes> &stream_key,
                      const std::string &nonce_hex,
                      size_t size,
                      std::vector<uint8_t> &stream)
{
    stream.clear();
    stream.reserve(size);
    uint32_t counter = 0;
    while (stream.size() < size) {
        std::ostringstream material;
        material << "LORA-MSG|" << nonce_hex << '|' << counter++;
        std::array<uint8_t, kSha256Bytes> block = {};
        const std::vector<uint8_t> key(stream_key.begin(), stream_key.end());
        if (!hmac_sha256_bytes(key,
                               reinterpret_cast<const uint8_t *>(material.str().data()),
                               material.str().size(),
                               block)) {
            stream.clear();
            return false;
        }
        const size_t remaining = size - stream.size();
        stream.insert(stream.end(), block.begin(), block.begin() + std::min(block.size(), remaining));
    }
    return true;
}

bool bytes_to_array32(const std::vector<uint8_t> &bytes, std::array<uint8_t, kSha256Bytes> &out)
{
    if (bytes.size() != out.size()) {
        return false;
    }
    std::copy(bytes.begin(), bytes.end(), out.begin());
    return true;
}

std::vector<uint8_t> array32_to_vector(const std::array<uint8_t, kSha256Bytes> &bytes)
{
    return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

bool sha256_text(const std::string &text, std::array<uint8_t, kSha256Bytes> &digest)
{
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr) {
        return false;
    }
    return mbedtls_md(info,
                      reinterpret_cast<const unsigned char *>(text.data()),
                      text.size(),
                      digest.data()) == 0;
}

bool derive_pairing_secret_from_raw_keys(const std::vector<uint8_t> &identity_private_key,
                                         const std::vector<uint8_t> &peer_public_key,
                                         std::vector<uint8_t> &shared_secret)
{
    if ((identity_private_key.size() != kCurve25519ScalarBytes) || peer_public_key.empty()) {
        return false;
    }

    mbedtls_ecp_group group;
    mbedtls_ecp_group_init(&group);
    mbedtls_mpi private_key;
    mbedtls_mpi_init(&private_key);
    mbedtls_mpi shared;
    mbedtls_mpi_init(&shared);
    mbedtls_ecp_point peer_point;
    mbedtls_ecp_point_init(&peer_point);

    bool ok = false;
    do {
        if (mbedtls_ecp_group_load(&group, kIdentityCurve) != 0) {
            break;
        }
        if (mbedtls_mpi_read_binary(&private_key, identity_private_key.data(), identity_private_key.size()) != 0) {
            break;
        }
        if (mbedtls_ecp_point_read_binary(&group,
                                          &peer_point,
                                          peer_public_key.data(),
                                          peer_public_key.size()) != 0) {
            break;
        }
        if (mbedtls_ecdh_compute_shared(&group,
                                        &shared,
                                        &peer_point,
                                        &private_key,
                                        random_callback,
                                        nullptr) != 0) {
            break;
        }
        shared_secret.assign(kCurve25519ScalarBytes, 0U);
        if (mbedtls_mpi_write_binary(&shared, shared_secret.data(), shared_secret.size()) != 0) {
            shared_secret.clear();
            break;
        }
        ok = true;
    } while (false);

    mbedtls_ecp_point_free(&peer_point);
    mbedtls_mpi_free(&shared);
    mbedtls_mpi_free(&private_key);
    mbedtls_ecp_group_free(&group);
    return ok;
}

bool advance_chain_key(std::array<uint8_t, kSha256Bytes> &chain_key,
                       const char *label,
                       std::array<uint8_t, kSha256Bytes> &derived_key)
{
    const std::vector<uint8_t> current_key(chain_key.begin(), chain_key.end());
    if (!derive_key_bytes(current_key, label, derived_key)) {
        return false;
    }
    return derive_key_bytes(current_key, "next", chain_key);
}

bool compute_message_auth_hex(const std::array<uint8_t, kSha256Bytes> &auth_key,
                              const std::string &sender_public_key_hex,
                              const std::string &peer_public_key_hex,
                              const std::string &nonce_hex,
                              const std::string &cipher_hex,
                              std::string &auth_hex)
{
    std::ostringstream material;
    material << "LORA-AUTH|" << sender_public_key_hex << '|' << peer_public_key_hex << '|' << nonce_hex << '|' << cipher_hex;
    std::array<uint8_t, kSha256Bytes> digest = {};
    const std::vector<uint8_t> key(auth_key.begin(), auth_key.end());
    if (!hmac_sha256_bytes(key,
                           reinterpret_cast<const uint8_t *>(material.str().data()),
                           material.str().size(),
                           digest)) {
        return false;
    }
    auth_hex = bytes_to_hex(digest.data(), digest.size());
    return true;
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
    case PacketKind::Unpair:
        return "unpair";
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

const char *delivery_state_name(DeliveryState state)
{
    switch (state) {
    case DeliveryState::None:
        return "none";
    case DeliveryState::PendingTx:
        return "pending_tx";
    case DeliveryState::SentToRadio:
        return "sent";
    case DeliveryState::Delivered:
        return "delivered";
    case DeliveryState::Failed:
        return "failed";
    }
    return "unknown";
}

const char *pairing_state_name(PairingState state)
{
    switch (state) {
    case PairingState::Idle:
        return "idle";
    case PairingState::PairRequestSent:
        return "pair_request_sent";
    case PairingState::WaitingForAccept:
        return "waiting_for_accept";
    case PairingState::PairRequestReceived:
        return "pair_request_received";
    case PairingState::WaitingUserInput:
        return "waiting_user_input";
    case PairingState::AcceptSentWaitingConfirm:
        return "accept_sent_waiting_confirm";
    case PairingState::Paired:
        return "paired";
    case PairingState::Rejected:
        return "rejected";
    case PairingState::Failed:
        return "failed";
    case PairingState::Timeout:
        return "timeout";
    }
    return "unknown";
}

CryptoIdentity cryptoGenerateIdentity()
{
    mbedtls_ecp_group group;
    mbedtls_ecp_group_init(&group);
    mbedtls_mpi private_key;
    mbedtls_mpi_init(&private_key);
    mbedtls_ecp_point public_key;
    mbedtls_ecp_point_init(&public_key);

    CryptoIdentity identity = {};
    size_t public_key_len = 0U;
    std::array<uint8_t, kCurve25519ScalarBytes> private_key_bytes = {};
    std::array<unsigned char, kCurve25519ScalarBytes + 1U> public_key_bytes = {};

    if ((mbedtls_ecp_group_load(&group, kIdentityCurve) == 0) &&
        (mbedtls_ecdh_gen_public(&group, &private_key, &public_key, random_callback, nullptr) == 0) &&
        (mbedtls_mpi_write_binary(&private_key, private_key_bytes.data(), private_key_bytes.size()) == 0) &&
        (mbedtls_ecp_point_write_binary(&group,
                                        &public_key,
                                        MBEDTLS_ECP_PF_UNCOMPRESSED,
                                        &public_key_len,
                                        public_key_bytes.data(),
                                        public_key_bytes.size()) == 0)) {
        identity.private_key_hex = bytes_to_hex(private_key_bytes.data(), private_key_bytes.size());
        identity.public_key_hex = bytes_to_hex(public_key_bytes.data(), public_key_len);
    }

    mbedtls_ecp_point_free(&public_key);
    mbedtls_mpi_free(&private_key);
    mbedtls_ecp_group_free(&group);

    if (identity.public_key_hex.empty() || identity.private_key_hex.empty()) {
        identity.public_key_hex = random_hex(kCurve25519ScalarBytes);
        identity.private_key_hex = random_hex(kCurve25519ScalarBytes);
    }
    identity.device_id = make_device_id(identity.public_key_hex);
    identity.display_name = std::string("P4-") + identity.device_id.substr(0, 4);
    return identity;
}

bool cryptoLoadOrCreateIdentityKeypair(CryptoIdentity &identity)
{
    if (cryptoIdentityLooksValid(identity)) {
        return true;
    }
    identity = cryptoGenerateIdentity();
    return cryptoIdentityLooksValid(identity);
}

std::string cryptoGenerateSharedSecretHex()
{
    return random_hex(kSha256Bytes);
}

std::string cryptoGenerateNonceHex(size_t byte_count)
{
    return random_hex(byte_count == 0U ? 16U : byte_count);
}

std::string cryptoGetPublicKeyHex(const CryptoIdentity &identity)
{
    return identity.public_key_hex;
}

bool cryptoIdentityLooksValid(const CryptoIdentity &identity)
{
    std::vector<uint8_t> private_key_bytes;
    std::vector<uint8_t> public_key_bytes;
    return !identity.device_id.empty() &&
           !identity.display_name.empty() &&
           hex_to_bytes(identity.private_key_hex, private_key_bytes) &&
           hex_to_bytes(identity.public_key_hex, public_key_bytes) &&
           (private_key_bytes.size() == kCurve25519ScalarBytes) &&
           !public_key_bytes.empty();
}

bool cryptoGenerateEphemeralKeypair(std::string &public_key_hex, std::string &private_key_hex)
{
    CryptoIdentity identity = cryptoGenerateIdentity();
    if (!cryptoIdentityLooksValid(identity)) {
        return false;
    }
    public_key_hex = identity.public_key_hex;
    private_key_hex = identity.private_key_hex;
    return !public_key_hex.empty() && !private_key_hex.empty();
}

bool cryptoDerivePairingSharedSecret(const CryptoIdentity &local_identity,
                                     const std::string &local_ephemeral_private_key_hex,
                                     const std::string &peer_identity_public_key_hex,
                                     const std::string &peer_ephemeral_public_key_hex,
                                     bool initiator,
                                     std::array<uint8_t, kRatchetKeySize> &shared_secret)
{
    std::vector<uint8_t> identity_private_key;
    std::vector<uint8_t> local_ephemeral_private_key;
    std::vector<uint8_t> peer_identity_public_key;
    std::vector<uint8_t> peer_ephemeral_public_key;
    if (!hex_to_bytes(local_identity.private_key_hex, identity_private_key) ||
        !hex_to_bytes(local_ephemeral_private_key_hex, local_ephemeral_private_key) ||
        !hex_to_bytes(peer_identity_public_key_hex, peer_identity_public_key) ||
        !hex_to_bytes(peer_ephemeral_public_key_hex, peer_ephemeral_public_key)) {
        return false;
    }

    std::vector<uint8_t> dh1;
    std::vector<uint8_t> dh2;
    std::vector<uint8_t> dh3;
    const bool ok = initiator
                        ? derive_pairing_secret_from_raw_keys(identity_private_key, peer_ephemeral_public_key, dh1) &&
                              derive_pairing_secret_from_raw_keys(local_ephemeral_private_key, peer_identity_public_key, dh2) &&
                              derive_pairing_secret_from_raw_keys(local_ephemeral_private_key, peer_ephemeral_public_key, dh3)
                        : derive_pairing_secret_from_raw_keys(local_ephemeral_private_key, peer_identity_public_key, dh1) &&
                              derive_pairing_secret_from_raw_keys(identity_private_key, peer_ephemeral_public_key, dh2) &&
                              derive_pairing_secret_from_raw_keys(local_ephemeral_private_key, peer_ephemeral_public_key, dh3);
    if (!ok) {
        return false;
    }

    std::vector<uint8_t> combined;
    combined.reserve(dh1.size() + dh2.size() + dh3.size());
    combined.insert(combined.end(), dh1.begin(), dh1.end());
    combined.insert(combined.end(), dh2.begin(), dh2.end());
    combined.insert(combined.end(), dh3.begin(), dh3.end());

    std::array<uint8_t, kSha256Bytes> digest = {};
    if (!hmac_sha256_bytes(combined,
                           reinterpret_cast<const uint8_t *>("LoRaPrivateChatPairingV1"),
                           std::strlen("LoRaPrivateChatPairingV1"),
                           digest)) {
        return false;
    }
    shared_secret = digest;
    return true;
}

bool cryptoDeriveChatId(const std::string &local_identity_public_key_hex,
                        const std::string &peer_identity_public_key_hex,
                        std::string &chat_id)
{
    const std::string lower = std::min(local_identity_public_key_hex, peer_identity_public_key_hex);
    const std::string upper = std::max(local_identity_public_key_hex, peer_identity_public_key_hex);
    std::array<uint8_t, kSha256Bytes> digest = {};
    if (!sha256_text(lower + upper, digest)) {
        return false;
    }
    chat_id = bytes_to_hex(digest.data(), digest.size());
    return true;
}

bool cryptoBuildPairAcceptAuthTag(const std::array<uint8_t, kRatchetKeySize> &shared_secret,
                                  const std::string &request_id,
                                  const std::string &sender_device_id,
                                  const std::string &target_device_id,
                                  const std::string &sender_identity_public_key_hex,
                                  const std::string &sender_ephemeral_public_key_hex,
                                  std::string &auth_tag_hex)
{
    std::ostringstream material;
    material << "PAIR_ACCEPT|" << request_id << '|' << sender_device_id << '|' << target_device_id << '|'
             << sender_identity_public_key_hex << '|' << sender_ephemeral_public_key_hex;
    std::array<uint8_t, kSha256Bytes> digest = {};
    if (!hmac_sha256_bytes(array32_to_vector(shared_secret),
                           reinterpret_cast<const uint8_t *>(material.str().data()),
                           material.str().size(),
                           digest)) {
        return false;
    }
    auth_tag_hex = bytes_to_hex(digest.data(), digest.size());
    return true;
}

bool cryptoInitializeMiniRatchet(const std::array<uint8_t, kRatchetKeySize> &shared_secret,
                                 const std::string &local_device_id,
                                 const std::string &local_name,
                                 const std::string &local_identity_public_key_hex,
                                 const std::string &peer_device_id,
                                 const std::string &peer_name,
                                 const std::string &peer_identity_public_key_hex,
                                 RatchetState &state)
{
    std::string chat_id;
    if (!cryptoDeriveChatId(local_identity_public_key_hex, peer_identity_public_key_hex, chat_id)) {
        return false;
    }

    std::array<uint8_t, kSha256Bytes> root_key = {};
    if (!derive_key_bytes(array32_to_vector(shared_secret), "root", root_key)) {
        return false;
    }

    const std::string lower = std::min(local_identity_public_key_hex, peer_identity_public_key_hex);
    const std::string upper = std::max(local_identity_public_key_hex, peer_identity_public_key_hex);
    std::array<uint8_t, kSha256Bytes> lower_to_higher = {};
    std::array<uint8_t, kSha256Bytes> higher_to_lower = {};
    if (!derive_key_bytes(array32_to_vector(shared_secret), std::string("chain:") + lower + "->" + upper, lower_to_higher) ||
        !derive_key_bytes(array32_to_vector(shared_secret), std::string("chain:") + upper + "->" + lower, higher_to_lower)) {
        return false;
    }

    state = RatchetState{};
    state.chat_id = chat_id;
    state.peer_device_id = peer_device_id;
    state.peer_name = peer_name.empty() ? peer_device_id : peer_name;
    state.peer_identity_pub_hex = peer_identity_public_key_hex;
    state.root_key = root_key;
    const bool local_is_lower = local_identity_public_key_hex <= peer_identity_public_key_hex;
    state.send_chain_key = local_is_lower ? lower_to_higher : higher_to_lower;
    state.recv_chain_key = local_is_lower ? higher_to_lower : lower_to_higher;
    state.send_msg_no = 0;
    state.recv_msg_no = 0;
    state.initialized = !local_device_id.empty() || !local_name.empty();
    return state.initialized;
}

bool cryptoRatchetEncrypt(RatchetState &state,
                          const std::string &plaintext,
                          std::string &nonce_hex,
                          std::string &cipher_hex,
                          std::string &auth_hex,
                          uint32_t &message_number)
{
    if (!state.initialized) {
        return false;
    }
    std::array<uint8_t, kSha256Bytes> message_key = {};
    if (!advance_chain_key(state.send_chain_key, "msg", message_key)) {
        return false;
    }
    nonce_hex = cryptoGenerateNonceHex(24U);
    message_number = state.send_msg_no;
    ++state.send_msg_no;
    return cryptoEncryptSymmetric(bytes_to_hex(message_key.data(), message_key.size()), nonce_hex, plaintext, cipher_hex, auth_hex);
}

bool cryptoRatchetDecrypt(RatchetState &state,
                          uint32_t message_number,
                          const std::string &nonce_hex,
                          const std::string &cipher_hex,
                          const std::string &auth_hex,
                          std::string &plaintext)
{
    if (!state.initialized || (message_number != state.recv_msg_no)) {
        return false;
    }
    std::array<uint8_t, kSha256Bytes> message_key = {};
    if (!advance_chain_key(state.recv_chain_key, "msg", message_key)) {
        return false;
    }
    ++state.recv_msg_no;
    return cryptoDecryptSymmetric(bytes_to_hex(message_key.data(), message_key.size()), nonce_hex, cipher_hex, auth_hex, plaintext);
}

bool cryptoComputeHmacHex(const std::string &key_material,
                          const std::string &data,
                          std::string &digest_hex)
{
    std::array<uint8_t, kSha256Bytes> digest = {};
    if (!hmac_sha256_text(key_material, data, digest)) {
        return false;
    }
    digest_hex = bytes_to_hex(digest.data(), digest.size());
    return true;
}

bool cryptoEncryptSymmetric(const std::string &key_material,
                            const std::string &nonce_hex,
                            const std::string &plaintext,
                            std::string &cipher_hex,
                            std::string &auth_hex)
{
    std::array<uint8_t, kSha256Bytes> stream_key = {};
    std::array<uint8_t, kSha256Bytes> auth_key = {};
    if (!hmac_sha256_text(key_material, "LORA-GROUP-ENC", stream_key) ||
        !hmac_sha256_text(key_material, "LORA-GROUP-AUTH", auth_key)) {
        return false;
    }

    std::vector<uint8_t> keystream;
    if (!derive_keystream(stream_key, nonce_hex, plaintext.size(), keystream)) {
        return false;
    }

    std::vector<uint8_t> cipher_bytes(plaintext.size(), 0);
    for (size_t index = 0; index < plaintext.size(); ++index) {
        cipher_bytes[index] = static_cast<uint8_t>(static_cast<uint8_t>(plaintext[index]) ^ keystream[index]);
    }
    cipher_hex = bytes_to_hex(cipher_bytes.data(), cipher_bytes.size());
    return compute_message_auth_hex(auth_key, "GROUP", "GROUP", nonce_hex, cipher_hex, auth_hex);
}

bool cryptoDecryptSymmetric(const std::string &key_material,
                            const std::string &nonce_hex,
                            const std::string &cipher_hex,
                            const std::string &auth_hex,
                            std::string &plaintext)
{
    std::array<uint8_t, kSha256Bytes> stream_key = {};
    std::array<uint8_t, kSha256Bytes> auth_key = {};
    if (!hmac_sha256_text(key_material, "LORA-GROUP-ENC", stream_key) ||
        !hmac_sha256_text(key_material, "LORA-GROUP-AUTH", auth_key)) {
        return false;
    }

    std::string expected_auth_hex;
    if (!compute_message_auth_hex(auth_key, "GROUP", "GROUP", nonce_hex, cipher_hex, expected_auth_hex) ||
        (expected_auth_hex != auth_hex)) {
        return false;
    }

    std::vector<uint8_t> cipher_bytes;
    if (!hex_to_bytes(cipher_hex, cipher_bytes)) {
        return false;
    }

    std::vector<uint8_t> keystream;
    if (!derive_keystream(stream_key, nonce_hex, cipher_bytes.size(), keystream)) {
        return false;
    }

    plaintext.assign(cipher_bytes.size(), '\0');
    for (size_t index = 0; index < cipher_bytes.size(); ++index) {
        plaintext[index] = static_cast<char>(cipher_bytes[index] ^ keystream[index]);
    }
    return true;
}

bool cryptoEncryptForPeer(const CryptoIdentity &local_identity,
                          const std::string &peer_public_key_hex,
                          const std::string &nonce_hex,
                          const std::string &plaintext,
                          std::string &cipher_hex,
                          std::string &auth_hex)
{
    std::vector<uint8_t> shared_secret;
    if (!derive_shared_secret_bytes(local_identity, peer_public_key_hex, shared_secret)) {
        return false;
    }

    std::array<uint8_t, kSha256Bytes> stream_key = {};
    std::array<uint8_t, kSha256Bytes> auth_key = {};
    if (!derive_key_bytes(shared_secret, "LORA-ENC", stream_key) ||
        !derive_key_bytes(shared_secret, "LORA-AUTH", auth_key)) {
        return false;
    }

    std::vector<uint8_t> keystream;
    if (!derive_keystream(stream_key, nonce_hex, plaintext.size(), keystream)) {
        return false;
    }

    std::vector<uint8_t> cipher_bytes(plaintext.size(), 0);
    for (size_t index = 0; index < plaintext.size(); ++index) {
        cipher_bytes[index] = static_cast<uint8_t>(static_cast<uint8_t>(plaintext[index]) ^ keystream[index]);
    }
    cipher_hex = bytes_to_hex(cipher_bytes.data(), cipher_bytes.size());
    return compute_message_auth_hex(auth_key,
                                    local_identity.public_key_hex,
                                    peer_public_key_hex,
                                    nonce_hex,
                                    cipher_hex,
                                    auth_hex);
}

bool cryptoDecryptFromPeer(const CryptoIdentity &local_identity,
                          const std::string &peer_public_key_hex,
                          const std::string &nonce_hex,
                          const std::string &cipher_hex,
                          const std::string &auth_hex,
                          std::string &plaintext)
{
    std::vector<uint8_t> cipher_bytes;
    if (!hex_to_bytes(cipher_hex, cipher_bytes)) {
        return false;
    }

    std::vector<uint8_t> shared_secret;
    if (!derive_shared_secret_bytes(local_identity, peer_public_key_hex, shared_secret)) {
        return false;
    }

    std::array<uint8_t, kSha256Bytes> stream_key = {};
    std::array<uint8_t, kSha256Bytes> auth_key = {};
    if (!derive_key_bytes(shared_secret, "LORA-ENC", stream_key) ||
        !derive_key_bytes(shared_secret, "LORA-AUTH", auth_key)) {
        return false;
    }

    std::string expected_auth_hex;
    if (!compute_message_auth_hex(auth_key,
                                  peer_public_key_hex,
                                  local_identity.public_key_hex,
                                  nonce_hex,
                                  cipher_hex,
                                  expected_auth_hex) ||
        (expected_auth_hex != auth_hex)) {
        return false;
    }

    std::vector<uint8_t> keystream;
    if (!derive_keystream(stream_key, nonce_hex, cipher_bytes.size(), keystream)) {
        return false;
    }

    plaintext.assign(cipher_bytes.size(), '\0');
    for (size_t index = 0; index < cipher_bytes.size(); ++index) {
        plaintext[index] = static_cast<char>(cipher_bytes[index] ^ keystream[index]);
    }
    return true;
}

} // namespace jc4880::lora_mesh