#pragma once

#include "LoRaMeshTypes.hpp"

namespace jc4880::lora_mesh {

bool load_stored_state(StoredState &state);
bool save_stored_state(const StoredState &state);
// Resets all pin GPIOs in `settings` to the canonical defaults for the currently
// selected `settings.radio_module`. Other fields (frequency, keys, peers...) are
// preserved. Useful when switching module variant at runtime so the previous
// variant's pin mapping does not leak through.
void reset_module_pin_defaults(MeshSettings &settings);

} // namespace jc4880::lora_mesh