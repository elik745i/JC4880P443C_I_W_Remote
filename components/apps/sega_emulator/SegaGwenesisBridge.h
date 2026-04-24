#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEGA_GWENESIS_FRAME_STRIDE 320
#define SEGA_GWENESIS_FRAME_HEIGHT 240
#define SEGA_GWENESIS_FRAME_BYTES_PER_PIXEL 2
#define SEGA_GWENESIS_FRAME_OFFSET 64
#define SEGA_GWENESIS_REFRESH_RATE 60
#define SEGA_GWENESIS_AUDIO_BUFFER_LENGTH 1056

bool sega_gwenesis_load_rom(const char *path, uint8_t *framebuffer, size_t framebuffer_size);
bool sega_gwenesis_load_state_file(const char *path);
bool sega_gwenesis_save_state_file(const char *path);
bool sega_gwenesis_validate_state_file(const char *path);
void sega_gwenesis_set_input_mask(uint32_t input_mask);

typedef struct {
	uint64_t cpu_time_us;
	uint64_t render_time_us;
	uint64_t synth_time_us;
	uint32_t frame_count;
	uint32_t cpu_max_us;
	uint32_t render_max_us;
	uint32_t synth_max_us;
} sega_gwenesis_perf_stats_t;

void sega_gwenesis_reset_perf_stats(void);
void sega_gwenesis_get_perf_stats(sega_gwenesis_perf_stats_t *stats);
void sega_gwenesis_set_perf_stats_enabled(bool enabled);

void sega_gwenesis_run_frame(bool draw_frame);
size_t sega_gwenesis_mix_audio_stereo(int16_t *destination, size_t frame_capacity);
const uint16_t *sega_gwenesis_get_palette(void);
int sega_gwenesis_get_screen_width(void);
int sega_gwenesis_get_screen_height(void);
int sega_gwenesis_get_audio_sample_rate(void);
int sega_gwenesis_get_refresh_rate(void);
void sega_gwenesis_shutdown(void);

#ifdef __cplusplus
}
#endif