#pragma once

#include <string>

#include "LoRaMeshTypes.hpp"

namespace jc4880::lora_mesh {

bool encode_mesh_packet(const MeshPacket &packet, std::string &encoded);
bool decode_mesh_packet(const std::string &encoded, MeshPacket &packet);

} // namespace jc4880::lora_mesh