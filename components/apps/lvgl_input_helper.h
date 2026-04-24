#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void jc4880_keyboard_install_case_behavior(lv_obj_t *keyboard);
void jc4880_password_textarea_install_toggle(lv_obj_t *textarea);
void jc4880_password_textarea_set_visibility(lv_obj_t *textarea, bool visible);

#ifdef __cplusplus
}
#endif
