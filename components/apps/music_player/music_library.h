#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool music_library_refresh(void);
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

#ifdef __cplusplus
}
#endif