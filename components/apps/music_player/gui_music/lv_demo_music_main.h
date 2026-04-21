/**
 * @file lv_demo_music_main.h
 *
 */

#ifndef APP_DEMO_MUSIC_MAIN_H
#define APP_DEMO_MUSIC_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lv_demo_music.h"
#if APP_DEMO_MUSIC_ENABLE

/*********************
 *      DEFINES
 *********************/
/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
lv_obj_t * _lv_demo_music_main_create(lv_obj_t * parent);
void _lv_demo_music_main_close(void);

void _lv_demo_music_play(uint32_t id);
void _lv_demo_music_resume(void);
void _lv_demo_music_pause(void);
void _lv_demo_music_exit_pause(void);
void _lv_demo_music_album_next(bool next);
void _lv_demo_music_main_sync_state(void);

/**********************
 *      MACROS
 **********************/
#endif /*APP_DEMO_MUSIC_ENABLE*/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*APP_DEMO_MUSIC_MAIN_H*/
