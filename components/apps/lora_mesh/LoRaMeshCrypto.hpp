#pragma once

#include <string>

#include "LoRaMeshTypes.hpp"

namespace jc4880::lora_mesh {

CryptoIdentity cryptoGenerateIdentity();
bool cryptoLoadOrCreateIdentityKeypair(CryptoIdentity &identity);
std::string cryptoGenerateSharedSecretHex();
std::string cryptoGenerateNonceHex(size_t byte_count = 16U);
std::string cryptoGetPublicKeyHex(const CryptoIdentity &identity);
bool cryptoIdentityLooksValid(const CryptoIdentity &identity);
bool cryptoGenerateEphemeralKeypair(std::string &public_key_hex, std::string &private_key_hex);
bool cryptoDerivePairingSharedSecret(const CryptoIdentity &local_identity,
                                     const std::string &local_ephemeral_private_key_hex,
                                     const std::string &peer_identity_public_key_hex,
                                     const std::string &peer_ephemeral_public_key_hex,
                                     bool initiator,
                                     std::array<uint8_t, kRatchetKeySize> &shared_secret);
bool cryptoDeriveChatId(const std::string &local_identity_public_key_hex,
                        const std::string &peer_identity_public_key_hex,
                        std::string &chat_id);
bool cryptoBuildPairAcceptAuthTag(const std::array<uint8_t, kRatchetKeySize> &shared_secret,
                                  const std::string &request_id,
                                  const std::string &sender_device_id,
                                  const std::string &target_device_id,
                                  const std::string &sender_identity_public_key_hex,
                                  const std::string &sender_ephemeral_public_key_hex,
                                  std::string &auth_tag_hex);
bool cryptoInitializeMiniRatchet(const std::array<uint8_t, kRatchetKeySize> &shared_secret,
                                 const std::string &local_device_id,
                                 const std::string &local_name,
                                 const std::string &local_identity_public_key_hex,
                                 const std::string &peer_device_id,
                                 const std::string &peer_name,
                                 const std::string &peer_identity_public_key_hex,
                                 RatchetState &state);
bool cryptoRatchetEncrypt(RatchetState &state,
                          const std::string &plaintext,
                          std::string &nonce_hex,
                          std::string &cipher_hex,
                          std::string &auth_hex,
                          uint32_t &message_number);
bool cryptoRatchetDecrypt(RatchetState &state,
                          uint32_t message_number,
                          const std::string &nonce_hex,
                          const std::string &cipher_hex,
                          const std::string &auth_hex,
                          std::string &plaintext);

bool cryptoComputeHmacHex(const std::string &key_material,
                          const std::string &data,
                          std::string &digest_hex);

bool cryptoEncryptSymmetric(const std::string &key_material,
                            const std::string &nonce_hex,
                            const std::string &plaintext,
                            std::string &cipher_hex,
                            std::string &auth_hex);

bool cryptoDecryptSymmetric(const std::string &key_material,
                            const std::string &nonce_hex,
                            const std::string &cipher_hex,
                            const std::string &auth_hex,
                            std::string &plaintext);

bool cryptoEncryptForPeer(const CryptoIdentity &local_identity,
                          const std::string &peer_public_key_hex,
                          const std::string &nonce_hex,
                          const std::string &plaintext,
                          std::string &cipher_hex,
                          std::string &auth_hex);

bool cryptoDecryptFromPeer(const CryptoIdentity &local_identity,
                          const std::string &peer_public_key_hex,
                          const std::string &nonce_hex,
                          const std::string &cipher_hex,
                          const std::string &auth_hex,
                          std::string &plaintext);

} // namespace jc4880::lora_mesh