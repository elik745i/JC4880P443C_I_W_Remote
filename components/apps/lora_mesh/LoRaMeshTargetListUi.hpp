#pragma once

#include "lvgl.h"

namespace jc4880::lora_mesh {

struct CommonChatRowConfig {
    lv_obj_t *parent = nullptr;
    const char *title = nullptr;
    bool unread = false;
    lv_event_cb_t open_cb = nullptr;
    void *open_user_data = nullptr;
};

struct PendingPairRequestRowConfig {
    lv_obj_t *parent = nullptr;
    const char *title = nullptr;
    const char *meta = nullptr;
    lv_event_cb_t click_cb = nullptr;
    void *click_user_data = nullptr;
    lv_event_cb_t cleanup_cb = nullptr;
    void *cleanup_user_data = nullptr;
};

struct PeerRowConfig {
    lv_obj_t *parent = nullptr;
    const char *title = nullptr;
    const char *peer_id = nullptr;
    bool unread = false;
    lv_event_cb_t open_cb = nullptr;
    void *open_user_data = nullptr;
    lv_event_cb_t delete_click_cb = nullptr;
    void *delete_click_user_data = nullptr;
    lv_event_cb_t delete_cleanup_cb = nullptr;
    void *delete_cleanup_user_data = nullptr;
};

void add_target_list_section_label(lv_obj_t *parent, const char *text);
lv_obj_t *create_common_chat_row(const CommonChatRowConfig &config);
lv_obj_t *create_pending_pair_request_row(const PendingPairRequestRowConfig &config);
lv_obj_t *create_peer_row(const PeerRowConfig &config);

} // namespace jc4880::lora_mesh