#pragma once

#include <string>

#include "LoRaMeshTypes.hpp"

namespace jc4880::lora_mesh {

CryptoIdentity cryptoGenerateIdentity();
std::string cryptoGenerateSharedSecretHex();
std::string cryptoGenerateNonceHex();
std::string cryptoGetPublicKeyHex(const CryptoIdentity &identity);

bool cryptoEncryptForPeer(const CryptoIdentity &local_identity,
                          const std::string &peer_public_key_hex,
                          const std::string &pair_secret_hex,
                          const std::string &nonce_hex,
                          const std::string &plaintext,
                          std::string &cipher_hex,
                          std::string &auth_hex);

bool cryptoDecryptFromPeer(const CryptoIdentity &local_identity,
                          const std::string &peer_public_key_hex,
                          const std::string &pair_secret_hex,
                          const std::string &nonce_hex,
                          const std::string &cipher_hex,
                          const std::string &auth_hex,
                          std::string &plaintext);

} // namespace jc4880::lora_mesh