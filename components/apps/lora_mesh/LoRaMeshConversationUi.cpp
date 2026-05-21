#include "LoRaMeshConversationUi.hpp"

namespace jc4880::lora_mesh {
namespace {

const char *safe_text(const char *text)
{
    return text == nullptr ? "" : text;
}

bool has_text(const char *text)
{
    return (text != nullptr) && (text[0] != '\0');
}

const char *delivery_status_symbol(const ConversationRowConfig &config)
{
    if (!config.outgoing) {
        return "";
    }
    if (config.transmit_pending) {
        return "...";
    }
    if (config.transmit_failed) {
        return "!";
    }
    if (config.delivered) {
        return "\xEF\x80\x8C\xEF\x80\x8C";
    }
    return LV_SYMBOL_OK;
}

lv_color_t delivery_status_color(const ConversationRowConfig &config)
{
    if (config.transmit_failed) {
        return lv_color_hex(0xB42318);
    }
    if (config.transmit_pending) {
        return lv_color_hex(0xB54708);
    }
    return lv_color_hex(0x027A48);
}

} // namespace

lv_obj_t *create_conversation_row(const ConversationRowConfig &config)
{
    lv_obj_t *row = lv_obj_create(config.parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bubble = lv_obj_create(row);
    lv_obj_set_width(bubble, LV_PCT(78));
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_style_radius(bubble, 18, 0);
    lv_obj_set_style_pad_left(bubble, 14, 0);
    lv_obj_set_style_pad_right(bubble, 14, 0);
    lv_obj_set_style_pad_top(bubble, 10, 0);
    lv_obj_set_style_pad_bottom(bubble, 10, 0);
    lv_obj_set_style_pad_row(bubble, 6, 0);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(bubble,
                              config.outgoing ? lv_color_hex(0xD9FDD3) : lv_color_hex(0xFFFFFF),
                              0);
    lv_obj_set_align(bubble, config.outgoing ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT);
    if ((config.click_cb != nullptr) || (config.cleanup_cb != nullptr)) {
        lv_obj_add_flag(bubble, LV_OBJ_FLAG_CLICKABLE);
        if (config.click_cb != nullptr) {
            lv_obj_add_event_cb(bubble, config.click_cb, LV_EVENT_CLICKED, config.click_user_data);
        }
        if (config.cleanup_cb != nullptr) {
            lv_obj_add_event_cb(bubble, config.cleanup_cb, LV_EVENT_DELETE, config.cleanup_user_data);
        }
    }

    if (has_text(config.quoted_text)) {
        lv_obj_t *quote_box = lv_obj_create(bubble);
        lv_obj_set_width(quote_box, LV_PCT(100));
        lv_obj_set_height(quote_box, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(quote_box, config.outgoing ? lv_color_hex(0xC4EAD0) : lv_color_hex(0xF2F4F7), 0);
        lv_obj_set_style_bg_opa(quote_box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(quote_box, 0, 0);
        lv_obj_set_style_radius(quote_box, 12, 0);
        lv_obj_set_style_pad_left(quote_box, 10, 0);
        lv_obj_set_style_pad_right(quote_box, 10, 0);
        lv_obj_set_style_pad_top(quote_box, 8, 0);
        lv_obj_set_style_pad_bottom(quote_box, 8, 0);
        lv_obj_clear_flag(quote_box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(quote_box, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *quote_label = lv_label_create(quote_box);
        lv_obj_set_width(quote_label, LV_PCT(100));
        lv_label_set_long_mode(quote_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(quote_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(quote_label, lv_color_hex(0x475467), 0);
        lv_label_set_text(quote_label, safe_text(config.quoted_text));
    }

    lv_obj_t *message_label = lv_label_create(bubble);
    lv_obj_set_width(message_label, LV_PCT(100));
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(message_label, lv_color_hex(0x111B21), 0);
    lv_label_set_text(message_label, safe_text(config.text));

    lv_obj_t *meta_row = lv_obj_create(bubble);
    lv_obj_set_width(meta_row, LV_PCT(100));
    lv_obj_set_height(meta_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(meta_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meta_row, 0, 0);
    lv_obj_set_style_pad_all(meta_row, 0, 0);
    lv_obj_set_style_pad_column(meta_row, 6, 0);
    lv_obj_set_flex_flow(meta_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(meta_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(meta_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(meta_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *meta_label = lv_label_create(meta_row);
    lv_obj_set_flex_grow(meta_label, 1);
    lv_obj_set_style_text_font(meta_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(meta_label, lv_color_hex(0x667781), 0);
    lv_obj_set_style_text_align(meta_label, config.outgoing ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(meta_label, safe_text(config.meta));

    if (config.outgoing) {
        lv_obj_t *status_label = lv_label_create(meta_row);
        lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(status_label, delivery_status_color(config), 0);
        lv_label_set_text(status_label, delivery_status_symbol(config));
    }

    return row;
}

lv_obj_t *create_empty_conversation_row(lv_obj_t *parent, const char *text)
{
    lv_obj_t *empty_row = lv_obj_create(parent);
    lv_obj_set_width(empty_row, LV_PCT(100));
    lv_obj_set_height(empty_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(empty_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(empty_row, 0, 0);
    lv_obj_set_style_pad_top(empty_row, 18, 0);
    lv_obj_set_style_pad_bottom(empty_row, 18, 0);
    lv_obj_set_style_pad_left(empty_row, 10, 0);
    lv_obj_set_style_pad_right(empty_row, 10, 0);
    lv_obj_clear_flag(empty_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *empty_label = lv_label_create(empty_row);
    lv_obj_set_width(empty_label, LV_PCT(100));
    lv_label_set_long_mode(empty_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(empty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(empty_label, lv_color_hex(0x667781), 0);
    lv_label_set_text(empty_label, safe_text(text));

    return empty_row;
}

} // namespace jc4880::lora_mesh