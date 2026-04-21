#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	MUSIC_LIBRARY_INDEX_STATE_IDLE = 0,
	MUSIC_LIBRARY_INDEX_STATE_RUNNING,
	MUSIC_LIBRARY_INDEX_STATE_COMPLETED,
	MUSIC_LIBRARY_INDEX_STATE_FAILED,
} music_library_index_state_t;

typedef enum {
	MUSIC_LIBRARY_STORAGE_SD = 0,
	MUSIC_LIBRARY_STORAGE_SPIFFS,
} music_library_storage_root_t;

typedef enum {
	MUSIC_LIBRARY_BROWSER_MODE_FILE = 0,
	MUSIC_LIBRARY_BROWSER_MODE_FOLDER,
} music_library_browser_mode_t;

typedef enum {
	MUSIC_LIBRARY_DOWNLOAD_STATE_IDLE = 0,
	MUSIC_LIBRARY_DOWNLOAD_STATE_RUNNING,
	MUSIC_LIBRARY_DOWNLOAD_STATE_COMPLETED,
	MUSIC_LIBRARY_DOWNLOAD_STATE_FAILED,
} music_library_download_state_t;

typedef enum {
	MUSIC_LIBRARY_BROWSER_ADD_STATE_IDLE = 0,
	MUSIC_LIBRARY_BROWSER_ADD_STATE_RUNNING,
	MUSIC_LIBRARY_BROWSER_ADD_STATE_COMPLETED,
	MUSIC_LIBRARY_BROWSER_ADD_STATE_FAILED,
} music_library_browser_add_state_t;

bool music_library_init(void);
void music_library_deinit(void);
bool music_library_refresh(void);
bool music_library_start_index(void);
bool music_library_finalize_index(void);
music_library_index_state_t music_library_get_index_state(void);
uint32_t music_library_get_index_scanned_files(void);
uint32_t music_library_get_indexed_track_count(void);
bool music_library_has_cached_index(void);

uint32_t music_library_get_count(void);
const char *music_library_get_title(uint32_t track_id);
const char *music_library_get_artist(uint32_t track_id);
const char *music_library_get_genre(uint32_t track_id);
uint32_t music_library_get_track_length(uint32_t track_id);
const char *music_library_get_path(uint32_t track_id);
bool music_library_play(uint32_t track_id);
bool music_library_is_playing(uint32_t track_id);
uint32_t music_library_get_current_index(void);
bool music_library_set_current_index(uint32_t track_id);

bool music_library_playlist_clear(void);
const char *music_library_get_last_message(void);

bool music_library_browser_open(music_library_browser_mode_t mode);
music_library_browser_mode_t music_library_browser_get_mode(void);
uint32_t music_library_browser_get_count(music_library_storage_root_t root);
const char *music_library_browser_get_path(music_library_storage_root_t root);
const char *music_library_browser_get_name(music_library_storage_root_t root, uint32_t entry_id);
const char *music_library_browser_get_meta(music_library_storage_root_t root, uint32_t entry_id);
bool music_library_browser_entry_is_directory(music_library_storage_root_t root, uint32_t entry_id);
bool music_library_browser_entry_can_add(music_library_storage_root_t root, uint32_t entry_id);
bool music_library_browser_entry_is_available(music_library_storage_root_t root, uint32_t entry_id);
bool music_library_browser_root_available(music_library_storage_root_t root);
bool music_library_browser_navigate_up(music_library_storage_root_t root);
bool music_library_browser_enter_directory(music_library_storage_root_t root, uint32_t entry_id);
bool music_library_browser_add_entry(music_library_storage_root_t root, uint32_t entry_id);
bool music_library_browser_add_entry_async(music_library_storage_root_t root, uint32_t entry_id);
music_library_browser_add_state_t music_library_browser_add_get_state(void);
const char *music_library_browser_add_get_status(void);
void music_library_browser_add_reset(void);

bool music_library_download_start(const char *url);
music_library_download_state_t music_library_download_get_state(void);
int32_t music_library_download_get_progress(void);
const char *music_library_download_get_status(void);
void music_library_download_reset(void);

#ifdef __cplusplus
}
#endif