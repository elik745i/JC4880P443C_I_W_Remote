#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEGA_GWENESIS_FRAME_STRIDE 320
#define SEGA_GWENESIS_FRAME_HEIGHT 240
#define SEGA_GWENESIS_FRAME_OFFSET 32
#define SEGA_GWENESIS_REFRESH_RATE 60
#define SEGA_GWENESIS_AUDIO_BUFFER_LENGTH 1056

bool sega_gwenesis_load_rom(const char *path, uint8_t *framebuffer, size_t framebuffer_size);
void sega_gwenesis_set_input_mask(uint32_t input_mask);
void sega_gwenesis_run_frame(void);
size_t sega_gwenesis_mix_audio_stereo(int16_t *destination, size_t frame_capacity);
const uint16_t *sega_gwenesis_get_palette(void);
int sega_gwenesis_get_screen_width(void);
int sega_gwenesis_get_screen_height(void);
int sega_gwenesis_get_audio_sample_rate(void);
void sega_gwenesis_shutdown(void);

#ifdef __cplusplus
}
#endif