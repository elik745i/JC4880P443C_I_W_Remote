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

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t * ctrl;

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

    _music_player_list_ui_create(parent);
    ctrl = _music_player_main_ui_create(parent);
}

void music_player_ui_close(void)
{
    _music_player_list_ui_close();
    _music_player_main_ui_close();

    lv_obj_set_style_bg_color(lv_scr_act(), original_screen_bg_color, 0);
    ctrl = NULL;
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

#endif /*APP_MUSIC_PLAYER_ENABLE*/
