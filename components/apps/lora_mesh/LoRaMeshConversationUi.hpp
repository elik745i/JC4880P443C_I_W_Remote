#pragma once

#include "lvgl.h"

namespace jc4880::lora_mesh {

struct ConversationRowConfig {
    lv_obj_t *parent = nullptr;
    const char *text = nullptr;
    const char *meta = nullptr;
    bool outgoing = false;
    bool transmit_pending = false;
    bool transmit_failed = false;
};

lv_obj_t *create_conversation_row(const ConversationRowConfig &config);
lv_obj_t *create_empty_conversation_row(lv_obj_t *parent, const char *text);

} // namespace jc4880::lora_mesh