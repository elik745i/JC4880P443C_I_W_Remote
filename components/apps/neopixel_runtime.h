#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void jc4880_neopixel_init(void);
void jc4880_neopixel_apply_config(bool enabled, int gpio, int brightness_percent, int palette_index, int effect_index);

#ifdef __cplusplus
}
#endif