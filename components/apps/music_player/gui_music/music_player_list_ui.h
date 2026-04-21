/**
 * @file music_player_list_ui.h
 *
 */

#ifndef APP_MUSIC_PLAYER_LIST_H
#define APP_MUSIC_PLAYER_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "music_player_ui.h"
#if APP_MUSIC_PLAYER_ENABLE

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
lv_obj_t * _music_player_list_ui_create(lv_obj_t * parent);
void _music_player_list_ui_close(void);
void _music_player_list_ui_reload(void);
bool _music_player_list_ui_needs_reload(void);
void _music_player_list_ui_open_add_file_browser(void);
void _music_player_list_ui_open_download_modal(void);

void _music_player_list_ui_btn_check(uint32_t track_id, bool state);

/**********************
 *      MACROS
 **********************/

#endif /*APP_MUSIC_PLAYER_ENABLE*/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*APP_MUSIC_PLAYER_LIST_H*/
