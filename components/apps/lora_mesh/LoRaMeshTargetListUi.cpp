#include "LoRaMeshTargetListUi.hpp"

namespace jc4880::lora_mesh {
namespace {

lv_obj_t *create_passive_layout_container(lv_obj_t *parent)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_shadow_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    return container;
}

void style_target_list_button(lv_obj_t *button,
                              lv_color_t background,
                              int height,
                              int pad_left,
                              int pad_right,
                              int pad_top,
                              int pad_bottom)
{
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, height);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_pad_left(button, pad_left, 0);
    lv_obj_set_style_pad_right(button, pad_right, 0);
    lv_obj_set_style_pad_top(button, pad_top, 0);
    lv_obj_set_style_pad_bottom(button, pad_bottom, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, background, 0);
}

const char *safe_text(const char *text)
{
    return text == nullptr ? "" : text;
}

} // namespace

void add_target_list_section_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *section_label = lv_label_create(parent);
    lv_obj_set_style_text_color(section_label, lv_color_hex(0x475467), 0);
    lv_label_set_text(section_label, safe_text(text));
}

lv_obj_t *create_common_chat_row(const CommonChatRowConfig &config)
{
    lv_obj_t *button = lv_btn_create(config.parent);
    style_target_list_button(button, lv_color_hex(0xFFFFFF), 52, 16, 16, 0, 0);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (config.open_cb != nullptr) {
        lv_obj_add_event_cb(button, config.open_cb, LV_EVENT_CLICKED, config.open_user_data);
    }

    lv_obj_t *row = create_passive_layout_container(button);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(row);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_color(title, lv_color_hex(0x111B21), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_label_set_text(title, safe_text(config.title));

    if (config.unread) {
        lv_obj_t *unread = lv_label_create(row);
        lv_obj_set_style_text_font(unread, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(unread, lv_color_hex(0x0C8A6A), 0);
        lv_label_set_text(unread, LV_SYMBOL_ENVELOPE);
    }

    return button;
}

lv_obj_t *create_pending_pair_request_row(const PendingPairRequestRowConfig &config)
{
    lv_obj_t *button = lv_btn_create(config.parent);
    style_target_list_button(button, lv_color_hex(0xECFDF3), 52, 16, 12, 0, 0);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (config.cleanup_cb != nullptr) {
        lv_obj_add_event_cb(button, config.cleanup_cb, LV_EVENT_DELETE, config.cleanup_user_data);
    }

    lv_obj_t *col = create_passive_layout_container(button);
    lv_obj_set_width(col, 0);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_right(col, 8, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 2, 0);

    lv_obj_t *title = lv_label_create(col);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x027A48), 0);
    lv_label_set_text(title, safe_text(config.title));

    lv_obj_t *meta = lv_label_create(col);
    lv_obj_set_style_text_font(meta, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(meta, lv_color_hex(0x027A48), 0);
    lv_label_set_text(meta, safe_text(config.meta));

    auto create_action_button = [button](lv_color_t background,
                                         lv_color_t text_color,
                                         const char *text,
                                         lv_event_cb_t click_cb,
                                         void *user_data) {
        lv_obj_t *action_button = lv_btn_create(button);
        lv_obj_set_size(action_button, 36, 36);
        lv_obj_set_style_radius(action_button, 18, 0);
        lv_obj_set_style_pad_all(action_button, 0, 0);
        lv_obj_set_style_border_width(action_button, 0, 0);
        lv_obj_set_style_shadow_width(action_button, 0, 0);
        lv_obj_set_style_bg_color(action_button, background, 0);
        if (click_cb != nullptr) {
            lv_obj_add_event_cb(action_button, click_cb, LV_EVENT_CLICKED, user_data);
        }

        lv_obj_t *label = lv_label_create(action_button);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(label, text_color, 0);
        lv_label_set_text(label, text);
        lv_obj_center(label);
    };

    create_action_button(lv_color_hex(0xD1FADF), lv_color_hex(0x027A48), LV_SYMBOL_OK, config.accept_click_cb, config.accept_click_user_data);
    create_action_button(lv_color_hex(0xFEE4E2), lv_color_hex(0xB42318), LV_SYMBOL_CLOSE, config.reject_click_cb, config.reject_click_user_data);

    return button;
}

lv_obj_t *create_peer_row(const PeerRowConfig &config)
{
    lv_obj_t *button = lv_btn_create(config.parent);
    style_target_list_button(button, lv_color_hex(0xFFFFFF), 52, 16, 12, 0, 0);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (config.open_cb != nullptr) {
        lv_obj_add_event_cb(button, config.open_cb, LV_EVENT_CLICKED, config.open_user_data);
    }

    lv_obj_t *col = create_passive_layout_container(button);
    lv_obj_set_width(col, 0);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_right(col, 8, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title_row = create_passive_layout_container(col);
    lv_obj_set_width(title_row, 0);
    lv_obj_set_flex_grow(title_row, 1);
    lv_obj_set_height(title_row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(title_row, 8, 0);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(title_row);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x111B21), 0);
    lv_label_set_text(title, safe_text(config.title));

    if (config.unread) {
        lv_obj_t *unread = lv_label_create(title_row);
        lv_obj_set_style_text_font(unread, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(unread, lv_color_hex(0x0C8A6A), 0);
        lv_label_set_text(unread, LV_SYMBOL_ENVELOPE);
    }

    lv_obj_t *meta_label = lv_label_create(col);
    lv_label_set_text(meta_label, safe_text(config.peer_id));
    lv_obj_add_flag(meta_label, LV_OBJ_FLAG_HIDDEN);

    if ((config.delete_click_cb != nullptr) || (config.delete_cleanup_cb != nullptr)) {
        lv_obj_t *delete_button = lv_btn_create(button);
        lv_obj_set_size(delete_button, 36, 36);
        lv_obj_set_style_radius(delete_button, 18, 0);
        lv_obj_set_style_pad_all(delete_button, 0, 0);
        lv_obj_set_style_border_width(delete_button, 0, 0);
        lv_obj_set_style_shadow_width(delete_button, 0, 0);
        lv_obj_set_style_bg_color(delete_button, lv_color_hex(0xFFF1F1), 0);
        if (config.delete_click_cb != nullptr) {
            lv_obj_add_event_cb(delete_button, config.delete_click_cb, LV_EVENT_CLICKED, config.delete_click_user_data);
        }
        if (config.delete_cleanup_cb != nullptr) {
            lv_obj_add_event_cb(delete_button, config.delete_cleanup_cb, LV_EVENT_DELETE, config.delete_cleanup_user_data);
        }

        lv_obj_t *delete_icon = lv_label_create(delete_button);
        lv_obj_set_style_text_font(delete_icon, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(delete_icon, lv_color_hex(0xD92D20), 0);
        lv_label_set_text(delete_icon, LV_SYMBOL_TRASH);
        lv_obj_center(delete_icon);
    }

    return button;
}

} // namespace jc4880::lora_mesh