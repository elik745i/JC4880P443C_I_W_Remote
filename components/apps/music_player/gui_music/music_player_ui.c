/**
 * @file music_player_ui.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "music_player_ui.h"

#if APP_MUSIC_PLAYER_ENABLE

#include "music_player_main_ui.h"
#include "music_player_list_ui.h"
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
#if APP_MUSIC_PLAYER_AUTO_PLAY
    static void auto_step_cb(lv_timer_t * timer);
#endif

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t * ctrl;
static lv_obj_t * list;

#if APP_MUSIC_PLAYER_AUTO_PLAY
    static lv_timer_t * auto_step_timer;
#endif

static lv_color_t original_screen_bg_color;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void music_player_ui_create(lv_obj_t *parent)
{
    original_screen_bg_color = lv_obj_get_style_bg_color(parent, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x343247), 0);

    list = _music_player_list_ui_create(parent);
    ctrl = _music_player_main_ui_create(parent);

#if APP_MUSIC_PLAYER_AUTO_PLAY
    auto_step_timer = lv_timer_create(auto_step_cb, 1000, NULL);
#endif
}

void music_player_ui_close(void)
{
#if APP_MUSIC_PLAYER_AUTO_PLAY
    if (auto_step_timer != NULL) {
        lv_timer_del(auto_step_timer);
        auto_step_timer = NULL;
    }
#endif
    _music_player_list_ui_close();
    _music_player_main_ui_close();

    lv_obj_set_style_bg_color(lv_scr_act(), original_screen_bg_color, 0);
    ctrl = NULL;
    list = NULL;
}

uint32_t _music_player_ui_get_track_count(void)
{
    return music_library_get_count();
}

const char * _music_player_ui_get_title(uint32_t track_id)
{
    return music_library_get_title(track_id);
}

const char * _music_player_ui_get_artist(uint32_t track_id)
{
    return music_library_get_artist(track_id);
}

const char * _music_player_ui_get_genre(uint32_t track_id)
{
    return music_library_get_genre(track_id);
}

const char * _music_player_ui_get_info(uint32_t track_id)
{
    return music_library_get_info(track_id);
}

uint32_t _music_player_ui_get_track_length(uint32_t track_id)
{
    return music_library_get_track_length(track_id);
}

void _music_player_ui_open_browser(void)
{
    if (_music_player_list_ui_needs_reload()) {
        _music_player_list_ui_reload();
    }

    if (ctrl != NULL) {
        lv_obj_scroll_by(ctrl, 0, -LV_VER_RES, LV_ANIM_ON);
    }
}

void _music_player_ui_close_browser(void)
{
    if (ctrl != NULL) {
        lv_obj_scroll_by(ctrl, 0, LV_VER_RES, LV_ANIM_ON);
    }
}

void _music_player_ui_reload_browser(void)
{
    _music_player_list_ui_reload();
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#if APP_MUSIC_PLAYER_AUTO_PLAY
static void auto_step_cb(lv_timer_t * t)
{
    LV_UNUSED(t);
    static uint32_t state = 0;

#if APP_MUSIC_PLAYER_LARGE
    const lv_font_t * font_small = &lv_font_montserrat_22;
    const lv_font_t * font_large = &lv_font_montserrat_32;
#else
    const lv_font_t * font_small = &lv_font_montserrat_12;
    const lv_font_t * font_large = &lv_font_montserrat_16;
#endif

    switch(state) {
        case 5:
            _music_player_ui_album_next(true);
            break;

        case 6:
            _music_player_ui_album_next(true);
            break;
        case 7:
            _music_player_ui_album_next(true);
            break;
        case 8:
            _music_player_ui_play(0);
            break;
#if APP_MUSIC_PLAYER_SQUARE || APP_MUSIC_PLAYER_ROUND
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
            _music_player_ui_play(1);
            break;
        case 19:
            lv_obj_scroll_by(ctrl, 0, LV_VER_RES, LV_ANIM_ON);
            break;
#if APP_MUSIC_PLAYER_SQUARE || APP_MUSIC_PLAYER_ROUND
        case 20:
            lv_obj_scroll_by(ctrl, 0, LV_VER_RES, LV_ANIM_ON);
            break;
#endif
        case 30:
            _music_player_ui_play(2);
            break;
        case 40: {
                lv_obj_t * bg = lv_layer_top();
                lv_obj_set_style_bg_color(bg, lv_color_hex(0x6f8af6), 0);
                lv_obj_set_style_text_color(bg, lv_color_white(), 0);
                lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
                lv_obj_fade_in(bg, 400, 0);
                lv_obj_t * dsc = lv_label_create(bg);
                lv_obj_set_style_text_font(dsc, font_small, 0);
                lv_label_set_text(dsc, "Average FPS");
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
#if APP_MUSIC_PLAYER_SQUARE || APP_MUSIC_PLAYER_ROUND
                lv_label_set_text(attr, "Music Player\nPerformance Monitor");
#else
                lv_label_set_text(attr, "Music Player | Performance Monitor");
#endif
                lv_obj_align(attr, LV_ALIGN_BOTTOM_MID, 0, -10);
                break;
            }
        case 41:
            lv_scr_load(lv_obj_create(NULL));
            _music_player_ui_pause();
            break;
    }
    state++;
}

#endif /*APP_MUSIC_PLAYER_AUTO_PLAY*/

#endif /*APP_MUSIC_PLAYER_ENABLE*/
