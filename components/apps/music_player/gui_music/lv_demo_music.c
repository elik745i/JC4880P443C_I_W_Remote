/**
 * @file lv_demo_music.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_demo_music.h"

#if APP_DEMO_MUSIC_ENABLE

#include "lv_demo_music_main.h"
#include "lv_demo_music_list.h"
#include "music_library.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
#if APP_DEMO_MUSIC_AUTO_PLAY
    static void auto_step_cb(lv_timer_t * timer);
#endif

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t * ctrl;
static lv_obj_t * list;

#if APP_DEMO_MUSIC_AUTO_PLAY
    static lv_timer_t * auto_step_timer;
#endif

static lv_color_t original_screen_bg_color;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_demo_music(lv_obj_t *parent)
{
    original_screen_bg_color = lv_obj_get_style_bg_color(parent, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x343247), 0);

    list = _lv_demo_music_list_create(parent);
    ctrl = _lv_demo_music_main_create(parent);

#if APP_DEMO_MUSIC_AUTO_PLAY
    auto_step_timer = lv_timer_create(auto_step_cb, 1000, NULL);
#endif
}

void lv_demo_music_close(void)
{
    /*Delete all aniamtions*/
    lv_anim_del(NULL, NULL);

#if APP_DEMO_MUSIC_AUTO_PLAY
    lv_timer_del(auto_step_timer);
#endif
    _lv_demo_music_list_close();
    _lv_demo_music_main_close();

    lv_obj_clean(lv_scr_act());

    lv_obj_set_style_bg_color(lv_scr_act(), original_screen_bg_color, 0);
}

uint32_t _lv_demo_music_get_track_count(void)
{
    return music_library_get_count();
}

const char * _lv_demo_music_get_title(uint32_t track_id)
{
    return music_library_get_title(track_id);
}

const char * _lv_demo_music_get_artist(uint32_t track_id)
{
    return music_library_get_artist(track_id);
}

const char * _lv_demo_music_get_genre(uint32_t track_id)
{
    return music_library_get_genre(track_id);
}

uint32_t _lv_demo_music_get_track_length(uint32_t track_id)
{
    return music_library_get_track_length(track_id);
}

void _lv_demo_music_open_browser(void)
{
    if (ctrl != NULL) {
        lv_obj_scroll_by(ctrl, 0, -LV_VER_RES, LV_ANIM_ON);
    }
}

void _lv_demo_music_close_browser(void)
{
    if (ctrl != NULL) {
        lv_obj_scroll_by(ctrl, 0, LV_VER_RES, LV_ANIM_ON);
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#if APP_DEMO_MUSIC_AUTO_PLAY
static void auto_step_cb(lv_timer_t * t)
{
    LV_UNUSED(t);
    static uint32_t state = 0;

#if APP_DEMO_MUSIC_LARGE
    const lv_font_t * font_small = &lv_font_montserrat_22;
    const lv_font_t * font_large = &lv_font_montserrat_32;
#else
    const lv_font_t * font_small = &lv_font_montserrat_12;
    const lv_font_t * font_large = &lv_font_montserrat_16;
#endif

    switch(state) {
        case 5:
            _lv_demo_music_album_next(true);
            break;

        case 6:
            _lv_demo_music_album_next(true);
            break;
        case 7:
            _lv_demo_music_album_next(true);
            break;
        case 8:
            _lv_demo_music_play(0);
            break;
#if APP_DEMO_MUSIC_SQUARE || APP_DEMO_MUSIC_ROUND
        case 11:
            lv_obj_scroll_by(ctrl, 0, -LV_VER_RES, LV_ANIM_ON);
            break;
        case 13:
            lv_obj_scroll_by(ctrl, 0, -LV_VER_RES, LV_ANIM_ON);
            break;
#else
        case 12:
            lv_obj_scroll_by(ctrl, 0, -LV_VER_RES, LV_ANIM_ON);
            break;
#endif
        case 15:
            lv_obj_scroll_by(list, 0, -300, LV_ANIM_ON);
            break;
        case 16:
            lv_obj_scroll_by(list, 0, 300, LV_ANIM_ON);
            break;
        case 18:
            _lv_demo_music_play(1);
            break;
        case 19:
            lv_obj_scroll_by(ctrl, 0, LV_VER_RES, LV_ANIM_ON);
            break;
#if APP_DEMO_MUSIC_SQUARE || APP_DEMO_MUSIC_ROUND
        case 20:
            lv_obj_scroll_by(ctrl, 0, LV_VER_RES, LV_ANIM_ON);
            break;
#endif
        case 30:
            _lv_demo_music_play(2);
            break;
        case 40: {
                lv_obj_t * bg = lv_layer_top();
                lv_obj_set_style_bg_color(bg, lv_color_hex(0x6f8af6), 0);
                lv_obj_set_style_text_color(bg, lv_color_white(), 0);
                lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
                lv_obj_fade_in(bg, 400, 0);
                lv_obj_t * dsc = lv_label_create(bg);
                lv_obj_set_style_text_font(dsc, font_small, 0);
                lv_label_set_text(dsc, "The average FPS is");
                lv_obj_align(dsc, LV_ALIGN_TOP_MID, 0, 90);

                lv_obj_t * num = lv_label_create(bg);
                lv_obj_set_style_text_font(num, font_large, 0);
#if LV_USE_PERF_MONITOR
                lv_label_set_text_fmt(num, "%d", lv_refr_get_fps_avg());
#endif
                lv_obj_align(num, LV_ALIGN_TOP_MID, 0, 120);

                lv_obj_t * attr = lv_label_create(bg);
                lv_obj_set_style_text_align(attr, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_set_style_text_font(attr, font_small, 0);
#if APP_DEMO_MUSIC_SQUARE || APP_DEMO_MUSIC_ROUND
                lv_label_set_text(attr, "Copyright 2020 LVGL Kft.\nwww.lvgl.io | lvgl@lvgl.io");
#else
                lv_label_set_text(attr, "Copyright 2020 LVGL Kft. | www.lvgl.io | lvgl@lvgl.io");
#endif
                lv_obj_align(attr, LV_ALIGN_BOTTOM_MID, 0, -10);
                break;
            }
        case 41:
            lv_scr_load(lv_obj_create(NULL));
            _lv_demo_music_pause();
            break;
    }
    state++;
}

#endif /*APP_DEMO_MUSIC_AUTO_PLAY*/

#endif /*APP_DEMO_MUSIC_ENABLE*/
