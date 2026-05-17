#pragma once

#include <string>

#include "LoRaMeshTypes.hpp"

namespace jc4880::lora_mesh {

CryptoIdentity cryptoGenerateIdentity();
std::string cryptoGenerateSharedSecretHex();
std::string cryptoGenerateNonceHex(size_t byte_count = 16U);
std::string cryptoGetPublicKeyHex(const CryptoIdentity &identity);
bool cryptoIdentityLooksValid(const CryptoIdentity &identity);

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