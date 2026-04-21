/**
 * @file music_player_ui.h
 *
 */

#ifndef APP_MUSIC_PLAYER_H
#define APP_MUSIC_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lvgl.h"
#include "bsp_board_extra.h"

#define APP_MUSIC_PLAYER_ENABLE       1
#define APP_MUSIC_PLAYER_LARGE        0

#if APP_MUSIC_PLAYER_ENABLE

/*********************
 *      DEFINES
 *********************/

#if APP_MUSIC_PLAYER_LARGE
#  define APP_MUSIC_PLAYER_HANDLE_SIZE  40
#else
#  define APP_MUSIC_PLAYER_HANDLE_SIZE  20
#endif

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

void music_player_ui_create(lv_obj_t *parent);
void music_player_ui_close(void);

uint32_t _music_player_ui_get_track_count(void);
const char * _music_player_ui_get_title(uint32_t track_id);
const char * _music_player_ui_get_artist(uint32_t track_id);
const char * _music_player_ui_get_genre(uint32_t track_id);
const char * _music_player_ui_get_info(uint32_t track_id);
uint32_t _music_player_ui_get_track_length(uint32_t track_id);
void _music_player_ui_open_browser(void);
void _music_player_ui_close_browser(void);
void _music_player_ui_reload_browser(void);

/**********************
 *      MACROS
 **********************/

#endif /*APP_MUSIC_PLAYER_ENABLE*/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*APP_MUSIC_PLAYER_H*/
