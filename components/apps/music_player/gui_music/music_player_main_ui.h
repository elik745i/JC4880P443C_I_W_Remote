/**
 * @file music_player_main_ui.h
 *
 */

#ifndef APP_MUSIC_PLAYER_MAIN_H
#define APP_MUSIC_PLAYER_MAIN_H

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
lv_obj_t * _music_player_main_ui_create(lv_obj_t * parent);
void _music_player_main_ui_close(void);

void _music_player_ui_play(uint32_t id);
void _music_player_ui_resume(void);
void _music_player_ui_pause(void);
void _music_player_ui_exit_pause(void);
void _music_player_ui_album_next(bool next);
bool _music_player_ui_has_active_session(void);
uint32_t _music_player_ui_get_elapsed_time(void);
void _music_player_main_ui_sync_state(void);

/**********************
 *      MACROS
 **********************/
#endif /*APP_MUSIC_PLAYER_ENABLE*/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*APP_MUSIC_PLAYER_MAIN_H*/
