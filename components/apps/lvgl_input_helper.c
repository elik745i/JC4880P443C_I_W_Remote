#include "lvgl_input_helper.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define JC4880_KEYBOARD_BEHAVIOR_FLAG LV_OBJ_FLAG_USER_1
#define JC4880_PASSWORD_TOGGLE_FLAG   LV_OBJ_FLAG_USER_2

typedef enum {
    JC4880_KEYBOARD_PRESSED_NONE = 0,
    JC4880_KEYBOARD_PRESSED_SHIFT_FROM_LOWER,
    JC4880_KEYBOARD_PRESSED_SHIFT_FROM_UPPER,
    JC4880_KEYBOARD_PRESSED_RESET_STATE,
    JC4880_KEYBOARD_PRESSED_ALPHA,
    JC4880_KEYBOARD_PRESSED_OTHER,
} jc4880_keyboard_pressed_action_t;

typedef struct {
    bool caps_lock_enabled;
    bool single_shift_pending;
    jc4880_keyboard_pressed_action_t pressed_action;
    lv_obj_t *keyboard;
    lv_obj_t *screen;
} jc4880_keyboard_state_t;

typedef struct {
    lv_obj_t *textarea;
    lv_obj_t *button;
    lv_obj_t *label;
    lv_coord_t base_pad_right;
    bool layout_queued;
    bool visible;
} jc4880_password_toggle_state_t;

static void jc4880_password_toggle_layout_async(void *user_data);

static bool jc4880_obj_is_descendant_of(const lv_obj_t *obj, const lv_obj_t *ancestor)
{
    const lv_obj_t *current = obj;
    while (current != NULL) {
        if (current == ancestor) {
            return true;
        }
        current = lv_obj_get_parent(current);
    }

    return false;
}

static void jc4880_keyboard_screen_event_cb(lv_event_t *e)
{
    jc4880_keyboard_state_t *state = (jc4880_keyboard_state_t *)lv_event_get_user_data(e);
    if ((state == NULL) || (lv_event_get_code(e) != LV_EVENT_CLICKED) || (state->keyboard == NULL) ||
        lv_obj_has_flag(state->keyboard, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    lv_obj_t *textarea = lv_keyboard_get_textarea(state->keyboard);
    lv_obj_t *target = lv_event_get_target(e);
    if ((textarea == NULL) || (target == NULL)) {
        return;
    }

    if (jc4880_obj_is_descendant_of(target, textarea) || jc4880_obj_is_descendant_of(target, state->keyboard)) {
        return;
    }

    lv_obj_clear_state(textarea, LV_STATE_FOCUSED);
}

static void jc4880_password_toggle_queue_layout(jc4880_password_toggle_state_t *state)
{
    if ((state == NULL) || state->layout_queued) {
        return;
    }

    state->layout_queued = true;
    lv_async_call(jc4880_password_toggle_layout_async, state);
}

static void jc4880_password_toggle_layout(jc4880_password_toggle_state_t *state)
{
    if ((state == NULL) || (state->textarea == NULL) || (state->button == NULL)) {
        return;
    }

    const lv_coord_t textarea_x = lv_obj_get_x(state->textarea);
    const lv_coord_t textarea_y = lv_obj_get_y(state->textarea);
    const lv_coord_t textarea_width = lv_obj_get_width(state->textarea);
    const lv_coord_t textarea_height = lv_obj_get_height(state->textarea);
    const lv_coord_t button_size = LV_MAX(32, textarea_height - 12);
    const lv_coord_t button_x = textarea_x + LV_MAX(0, textarea_width - button_size - 2);
    const lv_coord_t button_y = textarea_y + LV_MAX(0, (textarea_height - button_size) / 2);
    const lv_coord_t desired_pad_right = state->base_pad_right + button_size + 4;

    lv_obj_set_size(state->button, button_size, button_size);
    lv_obj_set_pos(state->button, button_x, button_y);
    lv_obj_set_style_radius(state->button, button_size / 2, 0);
    if (lv_obj_get_style_pad_right(state->textarea, LV_PART_MAIN) != desired_pad_right) {
        lv_obj_set_style_pad_right(state->textarea, desired_pad_right, 0);
    }
    if (!lv_obj_has_flag(state->button, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_move_foreground(state->button);
    }
}

static void jc4880_password_toggle_layout_async(void *user_data)
{
    jc4880_password_toggle_state_t *state = (jc4880_password_toggle_state_t *)user_data;
    if (state == NULL) {
        return;
    }

    state->layout_queued = false;

    if ((state->textarea == NULL) || (state->button == NULL) || !lv_obj_is_valid(state->textarea) ||
        !lv_obj_is_valid(state->button)) {
        return;
    }

    jc4880_password_toggle_layout(state);
}

static void jc4880_keyboard_case_event_cb(lv_event_t *e)
{
    jc4880_keyboard_state_t *state = (jc4880_keyboard_state_t *)lv_event_get_user_data(e);
    lv_obj_t *keyboard = lv_event_get_target(e);
    const lv_event_code_t code = lv_event_get_code(e);

    if (state == NULL) {
        return;
    }

    if (code == LV_EVENT_DELETE) {
        if ((state->screen != NULL) && lv_obj_is_valid(state->screen)) {
            lv_obj_remove_event_cb_with_user_data(state->screen, jc4880_keyboard_screen_event_cb, state);
        }
        free(state);
        return;
    }

    if ((code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
        lv_obj_t *textarea = lv_keyboard_get_textarea(keyboard);
        state->caps_lock_enabled = false;
        state->single_shift_pending = false;
        state->pressed_action = JC4880_KEYBOARD_PRESSED_NONE;
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        if ((textarea != NULL) && lv_obj_is_valid(textarea)) {
            lv_obj_clear_state(textarea, LV_STATE_FOCUSED);
        }
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        state->pressed_action = JC4880_KEYBOARD_PRESSED_NONE;

        const uint16_t btn_id = lv_keyboard_get_selected_btn(keyboard);
        const char *btn_text = (btn_id != LV_BTNMATRIX_BTN_NONE) ? lv_keyboard_get_btn_text(keyboard, btn_id) : NULL;
        if (btn_text != NULL) {
            const lv_keyboard_mode_t mode = lv_keyboard_get_mode(keyboard);
            if ((mode == LV_KEYBOARD_MODE_TEXT_LOWER) && (strcmp(btn_text, "ABC") == 0)) {
                state->pressed_action = JC4880_KEYBOARD_PRESSED_SHIFT_FROM_LOWER;
            } else if ((mode == LV_KEYBOARD_MODE_TEXT_UPPER) && (strcmp(btn_text, "abc") == 0)) {
                state->pressed_action = JC4880_KEYBOARD_PRESSED_SHIFT_FROM_UPPER;
            } else if ((strcmp(btn_text, "1#") == 0) || (strcmp(btn_text, LV_SYMBOL_KEYBOARD) == 0)) {
                state->pressed_action = JC4880_KEYBOARD_PRESSED_RESET_STATE;
            } else {
                const bool is_single_character = (btn_text[0] != '\0') && (btn_text[1] == '\0');
                const bool is_alpha = is_single_character && isalpha((unsigned char)btn_text[0]);
                state->pressed_action = is_alpha ? JC4880_KEYBOARD_PRESSED_ALPHA : JC4880_KEYBOARD_PRESSED_OTHER;
            }
        }
        return;
    }

    if (code != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    switch (state->pressed_action) {
        case JC4880_KEYBOARD_PRESSED_SHIFT_FROM_LOWER:
            state->caps_lock_enabled = false;
            state->single_shift_pending = true;
            lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER);
            break;
        case JC4880_KEYBOARD_PRESSED_SHIFT_FROM_UPPER:
            if (state->single_shift_pending && !state->caps_lock_enabled) {
                state->caps_lock_enabled = true;
                state->single_shift_pending = false;
                lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER);
            } else {
                state->caps_lock_enabled = false;
                state->single_shift_pending = false;
                lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
            }
            break;
        case JC4880_KEYBOARD_PRESSED_RESET_STATE:
            state->caps_lock_enabled = false;
            state->single_shift_pending = false;
            break;
        case JC4880_KEYBOARD_PRESSED_ALPHA:
            if (state->single_shift_pending && !state->caps_lock_enabled) {
                state->single_shift_pending = false;
                lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
            }
            break;
        default:
            break;
    }

    state->pressed_action = JC4880_KEYBOARD_PRESSED_NONE;
}

void jc4880_keyboard_install_case_behavior(lv_obj_t *keyboard)
{
    if ((keyboard == NULL) || lv_obj_has_flag(keyboard, JC4880_KEYBOARD_BEHAVIOR_FLAG)) {
        return;
    }

    jc4880_keyboard_state_t *state = (jc4880_keyboard_state_t *)calloc(1, sizeof(jc4880_keyboard_state_t));
    if (state == NULL) {
        return;
    }

    state->keyboard = keyboard;
    state->screen = lv_obj_get_parent(keyboard);

    lv_obj_add_flag(keyboard, JC4880_KEYBOARD_BEHAVIOR_FLAG);
    lv_obj_add_event_cb(keyboard, jc4880_keyboard_case_event_cb, LV_EVENT_ALL, state);
    if (state->screen != NULL) {
        lv_obj_add_event_cb(state->screen, jc4880_keyboard_screen_event_cb, LV_EVENT_CLICKED, state);
    }
}

static void jc4880_password_toggle_sync(jc4880_password_toggle_state_t *state)
{
    if ((state == NULL) || (state->textarea == NULL) || (state->label == NULL)) {
        return;
    }

    lv_textarea_set_password_mode(state->textarea, !state->visible);
    lv_label_set_text(state->label, state->visible ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static void jc4880_password_toggle_button_event_cb(lv_event_t *e)
{
    jc4880_password_toggle_state_t *state = (jc4880_password_toggle_state_t *)lv_event_get_user_data(e);
    if ((state == NULL) || (lv_event_get_code(e) != LV_EVENT_CLICKED)) {
        return;
    }

    state->visible = !state->visible;
    jc4880_password_toggle_sync(state);
}

static void jc4880_password_toggle_textarea_event_cb(lv_event_t *e)
{
    jc4880_password_toggle_state_t *state = (jc4880_password_toggle_state_t *)lv_event_get_user_data(e);
    if (state == NULL) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) {
        lv_async_call_cancel(jc4880_password_toggle_layout_async, state);
        if (state->button != NULL) {
            lv_obj_del(state->button);
        }
        free(state);
        return;
    }

    if ((code == LV_EVENT_SIZE_CHANGED) || (code == LV_EVENT_FOCUSED) || (code == LV_EVENT_CLICKED)) {
        jc4880_password_toggle_layout(state);
        jc4880_password_toggle_queue_layout(state);
    }
}

void jc4880_password_textarea_install_toggle(lv_obj_t *textarea)
{
    if ((textarea == NULL) || lv_obj_has_flag(textarea, JC4880_PASSWORD_TOGGLE_FLAG)) {
        return;
    }

    jc4880_password_toggle_state_t *state = (jc4880_password_toggle_state_t *)calloc(1, sizeof(jc4880_password_toggle_state_t));
    if (state == NULL) {
        return;
    }

    state->textarea = textarea;
    state->visible = false;
    state->layout_queued = false;
    state->base_pad_right = lv_obj_get_style_pad_right(textarea, LV_PART_MAIN);

    lv_obj_t *parent = lv_obj_get_parent(textarea);
    if (parent == NULL) {
        free(state);
        return;
    }

    state->button = lv_btn_create(parent);
    lv_obj_set_style_bg_opa(state->button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state->button, 0, 0);
    lv_obj_set_style_shadow_width(state->button, 0, 0);
    lv_obj_set_style_pad_all(state->button, 0, 0);
    lv_obj_clear_flag(state->button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(state->button, LV_OBJ_FLAG_FLOATING);

    state->label = lv_label_create(state->button);
    lv_obj_set_style_text_font(state->label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(state->label, lv_color_hex(0x64748B), 0);
    lv_obj_center(state->label);

    lv_obj_add_flag(textarea, JC4880_PASSWORD_TOGGLE_FLAG);
    lv_obj_set_user_data(textarea, state);
    lv_obj_add_event_cb(state->button, jc4880_password_toggle_button_event_cb, LV_EVENT_CLICKED, state);
    lv_obj_add_event_cb(textarea, jc4880_password_toggle_textarea_event_cb, LV_EVENT_DELETE, state);
    lv_obj_add_event_cb(textarea, jc4880_password_toggle_textarea_event_cb, LV_EVENT_SIZE_CHANGED, state);
    lv_obj_add_event_cb(textarea, jc4880_password_toggle_textarea_event_cb, LV_EVENT_FOCUSED, state);
    lv_obj_add_event_cb(textarea, jc4880_password_toggle_textarea_event_cb, LV_EVENT_CLICKED, state);
    jc4880_password_toggle_layout(state);
    jc4880_password_toggle_queue_layout(state);
    jc4880_password_toggle_sync(state);
}

void jc4880_password_textarea_set_visibility(lv_obj_t *textarea, bool visible)
{
    if ((textarea == NULL) || !lv_obj_has_flag(textarea, JC4880_PASSWORD_TOGGLE_FLAG)) {
        return;
    }

    jc4880_password_toggle_state_t *state = (jc4880_password_toggle_state_t *)lv_obj_get_user_data(textarea);
    if (state == NULL) {
        return;
    }

    state->visible = visible;
    jc4880_password_toggle_sync(state);
}
