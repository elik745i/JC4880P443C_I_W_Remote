#include "../ui.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#define TPX 0x00, 0x00, 0x00
#define WPX 0xFF, 0xFF, 0xFF

const LV_ATTRIBUTE_MEM_ALIGN uint8_t ui_img_ota_status_png_data[] = {
    TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX,
    TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX,
    TPX, TPX, TPX, TPX, TPX, TPX, WPX, WPX, WPX, WPX, TPX, TPX, TPX, TPX, TPX, TPX,
    TPX, TPX, TPX, TPX, WPX, WPX, WPX, WPX, WPX, WPX, WPX, WPX, TPX, TPX, TPX, TPX,
    TPX, TPX, TPX, TPX, WPX, WPX, WPX, TPX, TPX, WPX, WPX, WPX, WPX, TPX, TPX, TPX,
    TPX, TPX, TPX, WPX, WPX, TPX, TPX, TPX, TPX, TPX, TPX, WPX, WPX, TPX, TPX, TPX,
    TPX, TPX, WPX, WPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, WPX, WPX, TPX, TPX,
    TPX, WPX, WPX, WPX, WPX, TPX, TPX, TPX, TPX, TPX, WPX, WPX, WPX, WPX, WPX, WPX,
    TPX, WPX, WPX, WPX, WPX, WPX, TPX, TPX, TPX, TPX, TPX, WPX, WPX, WPX, WPX, TPX,
    TPX, TPX, WPX, WPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, WPX, WPX, TPX, TPX,
    TPX, TPX, TPX, WPX, WPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, WPX, WPX, TPX, TPX,
    TPX, TPX, TPX, WPX, WPX, WPX, TPX, TPX, TPX, TPX, WPX, WPX, TPX, TPX, TPX, TPX,
    TPX, TPX, TPX, TPX, WPX, WPX, WPX, WPX, WPX, WPX, WPX, WPX, TPX, TPX, TPX, TPX,
    TPX, TPX, TPX, TPX, TPX, WPX, WPX, WPX, WPX, WPX, WPX, TPX, TPX, TPX, TPX, TPX,
    TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX,
    TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX, TPX,
};

#undef TPX
#undef WPX

const lv_img_dsc_t ui_img_ota_status_png = {
    .header.always_zero = 0,
    .header.w = 16,
    .header.h = 16,
    .data_size = sizeof(ui_img_ota_status_png_data),
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .data = ui_img_ota_status_png_data,
};