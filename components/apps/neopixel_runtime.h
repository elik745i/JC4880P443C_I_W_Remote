#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void jc4880_neopixel_init(void);
void jc4880_neopixel_apply_config(bool enabled, int gpio, int brightness_percent, int palette_index, int effect_index);
bool jc4880_neopixel_suspend_gpio(int gpio);
void jc4880_neopixel_resume_gpio(int gpio);

#ifdef __cplusplus
}
#endif