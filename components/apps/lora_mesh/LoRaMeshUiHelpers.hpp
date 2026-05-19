#pragma once

#include <string>

#include "lvgl.h"

namespace jc4880::lora_mesh {

bool extract_peer_id_from_target_button(lv_obj_t *button, std::string &peer_id);
void scroll_chat_to_latest(lv_obj_t *message_list, lv_obj_t *composer, bool keep_composer_visible);

} // namespace jc4880::lora_mesh