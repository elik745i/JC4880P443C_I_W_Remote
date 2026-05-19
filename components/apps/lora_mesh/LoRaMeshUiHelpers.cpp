#include "LoRaMeshUiHelpers.hpp"

#include <cstring>

namespace jc4880::lora_mesh {

bool extract_peer_id_from_target_button(lv_obj_t *button, std::string &peer_id)
{
    peer_id.clear();
    if (button == nullptr) {
        return false;
    }

    lv_obj_t *col = lv_obj_get_child(button, 0);
    lv_obj_t *meta_label = nullptr;
    if (col != nullptr) {
        const uint32_t child_count = lv_obj_get_child_cnt(col);
        meta_label = (child_count >= 3U) ? lv_obj_get_child(col, 2) : lv_obj_get_child(col, child_count > 1U ? 1U : 0U);
    }

    const char *meta_text = (meta_label == nullptr) ? nullptr : lv_label_get_text(meta_label);
    if (meta_text == nullptr) {
        return false;
    }

    const char *id_marker = std::strstr(meta_text, "ID ");
    if (id_marker == nullptr) {
        return false;
    }

    id_marker += 3;
    const char *id_end = std::strchr(id_marker, ' ');
    peer_id = (id_end == nullptr) ? std::string(id_marker) : std::string(id_marker, id_end - id_marker);
    return !peer_id.empty();
}

void scroll_chat_to_latest(lv_obj_t *message_list, lv_obj_t *composer, bool keep_composer_visible)
{
    if ((message_list != nullptr) && (lv_obj_get_child_cnt(message_list) > 0)) {
        lv_obj_t *last_row = lv_obj_get_child(message_list, lv_obj_get_child_cnt(message_list) - 1);
        if (last_row != nullptr) {
            lv_obj_scroll_to_view(last_row, LV_ANIM_OFF);
        }
    }

    if (keep_composer_visible && (composer != nullptr)) {
        lv_obj_scroll_to_view(composer, LV_ANIM_OFF);
    }
}

} // namespace jc4880::lora_mesh