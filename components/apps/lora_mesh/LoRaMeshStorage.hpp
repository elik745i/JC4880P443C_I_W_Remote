#pragma once

#include "LoRaMeshTypes.hpp"

namespace jc4880::lora_mesh {

bool load_stored_state(StoredState &state);
bool save_stored_state(const StoredState &state);

} // namespace jc4880::lora_mesh