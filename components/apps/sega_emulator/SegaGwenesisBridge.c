#include "SegaGwenesisBridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "psram_alloc.h"
#include "sega_emulator/gwenesis/gwenesis.h"

extern unsigned char *VRAM;
extern unsigned char *ROM_DATA;
extern int zclk;
int system_clock = 0;
int scan_line = 0;

EXT_RAM_BSS_ATTR int16_t gwenesis_sn76489_buffer[GWENESIS_AUDIO_BUFFER_LENGTH_PAL] = {0};
int sn76489_index = 0;
int sn76489_clock = 0;
EXT_RAM_BSS_ATTR int16_t gwenesis_ym2612_buffer[GWENESIS_AUDIO_BUFFER_LENGTH_PAL] = {0};
int ym2612_index = 0;
int ym2612_clock = 0;

static uint32_t s_input_mask = 0;
static FILE *s_savestate_file = NULL;
static sega_gwenesis_perf_stats_t s_perf_stats = {0};
static bool s_perf_stats_enabled = false;

static void *sega_hot_malloc(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ptr != NULL) {
        return ptr;
    }

    return sega_psram_malloc(size);
}

typedef struct {
    char key[28];
    uint32_t length;
} gwenesis_save_var_t;

static void perf_update_max(uint32_t *slot, uint32_t value)
{
    if (value > *slot) {
        *slot = value;
    }
}

static void update_button_state(uint32_t mask, int button)
{
    if ((s_input_mask & mask) != 0) {
        gwenesis_io_pad_press_button(0, button);
    } else {
        gwenesis_io_pad_release_button(0, button);
    }
}

static bool savestate_has_tag(FILE *file, const char *tag_name)
{
    if ((file == NULL) || (tag_name == NULL)) {
        return false;
    }

    const long initial_pos = ftell(file);
    if (initial_pos < 0) {
        return false;
    }

    rewind(file);
    gwenesis_save_var_t var = {0};
    while (fread(&var, sizeof(var), 1, file) == 1) {
        if (strncmp(var.key, tag_name, sizeof(var.key)) == 0) {
            fseek(file, initial_pos, SEEK_SET);
            return true;
        }
        fseek(file, (long)var.length, SEEK_CUR);
    }

    fseek(file, initial_pos, SEEK_SET);
    return false;
}

SaveState *saveGwenesisStateOpenForRead(const char *fileName)
{
    (void)fileName;
    return (SaveState *)1;
}

SaveState *saveGwenesisStateOpenForWrite(const char *fileName)
{
    (void)fileName;
    return (SaveState *)1;
}

int saveGwenesisStateGet(SaveState *state, const char *tagName)
{
    int value = 0;
    saveGwenesisStateGetBuffer(state, tagName, &value, sizeof(value));
    return value;
}

void saveGwenesisStateSet(SaveState *state, const char *tagName, int value)
{
    saveGwenesisStateSetBuffer(state, tagName, &value, sizeof(value));
}

void saveGwenesisStateGetBuffer(SaveState *state, const char *tagName, void *buffer, int length)
{
    (void)state;
    if (s_savestate_file == NULL) {
        memset(buffer, 0, (size_t)length);
        return;
    }

    const long initial_pos = ftell(s_savestate_file);
    bool wrapped = false;
    gwenesis_save_var_t var = {0};

    while (!wrapped || (ftell(s_savestate_file) < initial_pos)) {
        if (fread(&var, sizeof(var), 1, s_savestate_file) != 1) {
            if (!wrapped) {
                fseek(s_savestate_file, 0, SEEK_SET);
                wrapped = true;
                continue;
            }
            break;
        }

        if (strncmp(var.key, tagName, sizeof(var.key)) == 0) {
            size_t bytes_to_read = (size_t)length;
            if (var.length < bytes_to_read) {
                bytes_to_read = var.length;
            }
            fread(buffer, bytes_to_read, 1, s_savestate_file);
            if (bytes_to_read < (size_t)length) {
                memset(((uint8_t *)buffer) + bytes_to_read, 0, (size_t)length - bytes_to_read);
            }
            if (var.length > bytes_to_read) {
                fseek(s_savestate_file, (long)(var.length - bytes_to_read), SEEK_CUR);
            }
            return;
        }

        fseek(s_savestate_file, (long)var.length, SEEK_CUR);
    }

    memset(buffer, 0, (size_t)length);
}

void saveGwenesisStateSetBuffer(SaveState *state, const char *tagName, void *buffer, int length)
{
    (void)state;
    if (s_savestate_file == NULL) {
        return;
    }

    gwenesis_save_var_t var = {0};
    strncpy(var.key, tagName, sizeof(var.key) - 1);
    var.length = (uint32_t)length;
    fwrite(&var, sizeof(var), 1, s_savestate_file);
    fwrite(buffer, (size_t)length, 1, s_savestate_file);
}

void gwenesis_io_get_buttons(void)
{
    update_button_state(1u << 0, PAD_UP);
    update_button_state(1u << 1, PAD_DOWN);
    update_button_state(1u << 2, PAD_LEFT);
    update_button_state(1u << 3, PAD_RIGHT);
    update_button_state(1u << 4, PAD_A);
    update_button_state(1u << 5, PAD_B);
    update_button_state(1u << 6, PAD_C);
    update_button_state(1u << 7, PAD_S);
}

bool sega_gwenesis_load_rom(const char *path, uint8_t *framebuffer, size_t framebuffer_size)
{
    FILE *file = fopen(path, "rb");
    long rom_size_long;
    uint8_t *rom_buffer;

    sega_gwenesis_shutdown();

    if ((file == NULL) || (framebuffer == NULL) ||
        (framebuffer_size < (SEGA_GWENESIS_FRAME_OFFSET + (SEGA_GWENESIS_FRAME_STRIDE * SEGA_GWENESIS_FRAME_HEIGHT)))) {
        if (file != NULL) {
            fclose(file);
        }
        return false;
    }

    fseek(file, 0, SEEK_END);
    rom_size_long = ftell(file);
    rewind(file);
    if (rom_size_long <= 0) {
        fclose(file);
        return false;
    }

    rom_buffer = sega_psram_malloc((size_t)rom_size_long);
    if (rom_buffer == NULL) {
        fclose(file);
        return false;
    }

    if (fread(rom_buffer, 1, (size_t)rom_size_long, file) != (size_t)rom_size_long) {
        sega_psram_free(rom_buffer);
        fclose(file);
        return false;
    }
    fclose(file);

    VRAM = sega_hot_malloc(VRAM_MAX_SIZE);
    if (VRAM == NULL) {
        sega_psram_free(rom_buffer);
        return false;
    }

    memset(framebuffer, 0, framebuffer_size);
    load_cartridge(rom_buffer, (size_t)rom_size_long);
    sega_psram_free(rom_buffer);

    power_on();
    reset_emulation();
    gwenesis_vdp_set_buffer((unsigned short *)(framebuffer + SEGA_GWENESIS_FRAME_OFFSET));
    return true;
}

bool sega_gwenesis_load_state_file(const char *path)
{
    FILE *file;

    if (path == NULL) {
        return false;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    if (!savestate_has_tag(file, "REG_A")) {
        fclose(file);
        return false;
    }

    s_savestate_file = file;
    gwenesis_load_state();
    fclose(file);
    s_savestate_file = NULL;
    return true;
}

bool sega_gwenesis_save_state_file(const char *path)
{
    FILE *file;

    if (path == NULL) {
        return false;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }

    s_savestate_file = file;
    gwenesis_save_state();
    fclose(file);
    s_savestate_file = NULL;
    return true;
}

void sega_gwenesis_set_input_mask(uint32_t input_mask)
{
    s_input_mask = input_mask;
}

void sega_gwenesis_reset_perf_stats(void)
{
    memset(&s_perf_stats, 0, sizeof(s_perf_stats));
}

void sega_gwenesis_get_perf_stats(sega_gwenesis_perf_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    *stats = s_perf_stats;
}

void sega_gwenesis_set_perf_stats_enabled(bool enabled)
{
    s_perf_stats_enabled = enabled;
    if (!enabled) {
        sega_gwenesis_reset_perf_stats();
    }
}

void sega_gwenesis_run_frame(bool draw_frame)
{
    extern unsigned char gwenesis_vdp_regs[0x20];
    extern unsigned int gwenesis_vdp_status;
    extern int screen_width;
    extern int screen_height;
    extern int hint_pending;

    const int lines_per_frame = REG1_PAL ? LINES_PER_FRAME_PAL : LINES_PER_FRAME_NTSC;
    int hint_counter = gwenesis_vdp_regs[10];

    screen_width = REG12_MODE_H40 ? 320 : 256;
    screen_height = REG1_PAL ? 240 : 224;
    gwenesis_vdp_render_config();

    system_clock = 0;
    zclk = 0;
    ym2612_clock = 0;
    ym2612_index = 0;
    sn76489_clock = 0;
    sn76489_index = 0;
    scan_line = 0;

    uint64_t cpuTimeUs = 0;
    uint64_t renderTimeUs = 0;
    uint64_t synthTimeUs = 0;
    uint32_t cpuMaxUs = 0;
    uint32_t renderMaxUs = 0;
    uint32_t synthMaxUs = 0;

    while (scan_line < lines_per_frame) {
        if (s_perf_stats_enabled) {
            int64_t sectionStartUs = esp_timer_get_time();
            m68k_run(system_clock + VDP_CYCLES_PER_LINE);
            z80_run(system_clock + VDP_CYCLES_PER_LINE);
            uint32_t sectionUs = (uint32_t)(esp_timer_get_time() - sectionStartUs);
            cpuTimeUs += sectionUs;
            perf_update_max(&cpuMaxUs, sectionUs);

            if (GWENESIS_AUDIO_ACCURATE == 0) {
                sectionStartUs = esp_timer_get_time();
                gwenesis_SN76489_run(system_clock + VDP_CYCLES_PER_LINE);
                ym2612_run(system_clock + VDP_CYCLES_PER_LINE);
                sectionUs = (uint32_t)(esp_timer_get_time() - sectionStartUs);
                synthTimeUs += sectionUs;
                perf_update_max(&synthMaxUs, sectionUs);
            }

            if (draw_frame && (scan_line < screen_height)) {
                sectionStartUs = esp_timer_get_time();
                gwenesis_vdp_render_line(scan_line);
                sectionUs = (uint32_t)(esp_timer_get_time() - sectionStartUs);
                renderTimeUs += sectionUs;
                perf_update_max(&renderMaxUs, sectionUs);
            }
        } else {
            m68k_run(system_clock + VDP_CYCLES_PER_LINE);
            z80_run(system_clock + VDP_CYCLES_PER_LINE);

            if (GWENESIS_AUDIO_ACCURATE == 0) {
                gwenesis_SN76489_run(system_clock + VDP_CYCLES_PER_LINE);
                ym2612_run(system_clock + VDP_CYCLES_PER_LINE);
            }

            if (draw_frame && (scan_line < screen_height)) {
                gwenesis_vdp_render_line(scan_line);
            }
        }

        if ((scan_line == 0) || (scan_line > screen_height)) {
            hint_counter = REG10_LINE_COUNTER;
        }

        if (--hint_counter < 0) {
            if ((REG0_LINE_INTERRUPT != 0) && (scan_line <= screen_height)) {
                hint_pending = 1;
                if ((gwenesis_vdp_status & STATUS_VIRQPENDING) == 0) {
                    m68k_update_irq(4);
                }
            }
            hint_counter = REG10_LINE_COUNTER;
        }

        scan_line++;

        if (scan_line == screen_height) {
            if (REG1_VBLANK_INTERRUPT != 0) {
                gwenesis_vdp_status |= STATUS_VIRQPENDING;
                m68k_set_irq(6);
            }
            z80_irq_line(1);
        }
        if (scan_line == (screen_height + 1)) {
            z80_irq_line(0);
        }

        system_clock += VDP_CYCLES_PER_LINE;
    }

    if (GWENESIS_AUDIO_ACCURATE == 1) {
        if (s_perf_stats_enabled) {
            const int64_t synthStartUs = esp_timer_get_time();
            gwenesis_SN76489_run(system_clock);
            ym2612_run(system_clock);
            const uint32_t synthUs = (uint32_t)(esp_timer_get_time() - synthStartUs);
            synthTimeUs += synthUs;
            perf_update_max(&synthMaxUs, synthUs);
        } else {
            gwenesis_SN76489_run(system_clock);
            ym2612_run(system_clock);
        }
    }

    m68k.cycles -= system_clock;

    if (s_perf_stats_enabled) {
        s_perf_stats.cpu_time_us += cpuTimeUs;
        s_perf_stats.render_time_us += renderTimeUs;
        s_perf_stats.synth_time_us += synthTimeUs;
        s_perf_stats.frame_count += 1;
        perf_update_max(&s_perf_stats.cpu_max_us, cpuMaxUs);
        perf_update_max(&s_perf_stats.render_max_us, renderMaxUs);
        perf_update_max(&s_perf_stats.synth_max_us, synthMaxUs);
    }
}

size_t sega_gwenesis_mix_audio_stereo(int16_t *destination, size_t frame_capacity)
{
    size_t frame_count = (size_t)((ym2612_index > sn76489_index) ? ym2612_index : sn76489_index);
    size_t i;

    if ((destination == NULL) || (frame_capacity == 0)) {
        return 0;
    }
    if (frame_count > frame_capacity) {
        frame_count = frame_capacity;
    }

    for (i = 0; i < frame_count; ++i) {
        int mixed = 0;
        if (i < (size_t)ym2612_index) {
            mixed += gwenesis_ym2612_buffer[i];
        }
        if (i < (size_t)sn76489_index) {
            mixed += gwenesis_sn76489_buffer[i];
        }
        if (mixed > 32767) {
            mixed = 32767;
        }
        if (mixed < -32768) {
            mixed = -32768;
        }
        destination[i * 2] = (int16_t)mixed;
        destination[i * 2 + 1] = (int16_t)mixed;
    }

    return frame_count;
}

const uint16_t *sega_gwenesis_get_palette(void)
{
    extern unsigned short CRAM565[];
    return CRAM565;
}

int sega_gwenesis_get_screen_width(void)
{
    extern int screen_width;
    return (screen_width > 0) ? screen_width : 320;
}

int sega_gwenesis_get_screen_height(void)
{
    extern int screen_height;
    return (screen_height > 0) ? screen_height : 224;
}

int sega_gwenesis_get_audio_sample_rate(void)
{
    return gwenesis_runtime_mode_pal ? GWENESIS_AUDIO_FREQ_PAL : GWENESIS_AUDIO_FREQ_NTSC;
}

int sega_gwenesis_get_refresh_rate(void)
{
    return gwenesis_runtime_mode_pal ? GWENESIS_REFRESH_RATE_PAL : GWENESIS_REFRESH_RATE_NTSC;
}

void sega_gwenesis_shutdown(void)
{
    if (ROM_DATA != NULL) {
        sega_psram_free(ROM_DATA);
        ROM_DATA = NULL;
    }
    if (VRAM != NULL) {
        sega_psram_free(VRAM);
        VRAM = NULL;
    }
    s_input_mask = 0;
    s_savestate_file = NULL;
}