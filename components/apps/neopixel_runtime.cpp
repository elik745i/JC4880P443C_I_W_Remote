#include "neopixel_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "led_strip.h"
#include "esp_log.h"

namespace {
constexpr const char *kTag = "NeoPixel";
constexpr int kPixelCount = 4;
constexpr int kRefreshPeriodMs = 40;
constexpr int kEffectCount = 21;
constexpr std::array<int, 12> kAllowedGpios = {28, 29, 30, 31, 32, 33, 34, 35, 49, 50, 51, 52};

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct NeoPixelConfig {
    bool enabled = false;
    int gpio = -1;
    int brightness_percent = 60;
    int palette_index = 0;
    int effect_index = 0;
};

std::mutex s_mutex;
TaskHandle_t s_task = nullptr;
bool s_initialized = false;
NeoPixelConfig s_config = {};
led_strip_handle_t s_strip = nullptr;
int s_strip_gpio = -1;
uint32_t s_frame = 0;
bool s_failed_gpio_logged = false;

constexpr std::array<Rgb, 12> kPalette = {{
    {255, 32, 32},
    {255, 128, 0},
    {255, 220, 32},
    {48, 220, 72},
    {0, 200, 120},
    {0, 180, 255},
    {32, 96, 255},
    {128, 64, 255},
    {255, 64, 180},
    {255, 255, 255},
    {255, 96, 24},
    {64, 255, 220},
}};

bool isAllowedGpio(int gpio)
{
    return std::find(kAllowedGpios.begin(), kAllowedGpios.end(), gpio) != kAllowedGpios.end();
}

uint8_t clampByte(int value)
{
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

Rgb scaleColor(const Rgb &color, int brightness_percent)
{
    const int clamped = std::clamp(brightness_percent, 0, 100);
    return {
        clampByte((static_cast<int>(color.r) * clamped) / 100),
        clampByte((static_cast<int>(color.g) * clamped) / 100),
        clampByte((static_cast<int>(color.b) * clamped) / 100),
    };
}

Rgb blendColor(const Rgb &lhs, const Rgb &rhs, uint8_t amount)
{
    const uint16_t inverse = static_cast<uint16_t>(255 - amount);
    return {
        clampByte((lhs.r * inverse + rhs.r * amount) / 255),
        clampByte((lhs.g * inverse + rhs.g * amount) / 255),
        clampByte((lhs.b * inverse + rhs.b * amount) / 255),
    };
}

Rgb wheelColor(uint8_t position)
{
    if (position < 85) {
        return {clampByte(255 - position * 3), clampByte(position * 3), 0};
    }
    if (position < 170) {
        position = static_cast<uint8_t>(position - 85);
        return {0, clampByte(255 - position * 3), clampByte(position * 3)};
    }

    position = static_cast<uint8_t>(position - 170);
    return {clampByte(position * 3), 0, clampByte(255 - position * 3)};
}

uint8_t wave8(uint32_t frame, uint32_t phase)
{
    const float angle = static_cast<float>((frame + phase) % 360) * 3.14159265f / 180.0f;
    return clampByte(static_cast<int>((std::sin(angle) * 127.0f) + 128.0f));
}

uint8_t tri8(uint32_t value)
{
    const uint32_t folded = value % 510;
    return (folded <= 255) ? static_cast<uint8_t>(folded) : static_cast<uint8_t>(510 - folded);
}

Rgb paletteColor(int palette_index)
{
    const int clamped = std::clamp(palette_index, 0, static_cast<int>(kPalette.size()) - 1);
    return kPalette[static_cast<size_t>(clamped)];
}

void clearStripLocked()
{
    if (s_strip == nullptr) {
        return;
    }

    led_strip_clear(s_strip);
    led_strip_refresh(s_strip);
}

void releaseStripLocked()
{
    if (s_strip != nullptr) {
        clearStripLocked();
        led_strip_del(s_strip);
        s_strip = nullptr;
    }
    s_strip_gpio = -1;
}

bool ensureStripLocked()
{
    if (!isAllowedGpio(s_config.gpio)) {
        releaseStripLocked();
        return false;
    }

    if ((s_strip != nullptr) && (s_strip_gpio == s_config.gpio)) {
        return true;
    }

    releaseStripLocked();

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = s_config.gpio;
    strip_config.max_leds = kPixelCount;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip_config.flags.invert_out = false;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000;
    rmt_config.mem_block_symbols = 64;
    rmt_config.flags.with_dma = false;

    const esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        if (!s_failed_gpio_logged || (s_strip_gpio != s_config.gpio)) {
            ESP_LOGW(kTag, "Failed to initialize WS2812 strip on GPIO %d: %s", s_config.gpio, esp_err_to_name(err));
            s_failed_gpio_logged = true;
        }
        s_strip = nullptr;
        s_strip_gpio = -1;
        return false;
    }

    s_failed_gpio_logged = false;
    s_strip_gpio = s_config.gpio;
    ESP_LOGI(kTag, "WS2812 strip ready on GPIO %d (%d pixels)", s_config.gpio, kPixelCount);
    return true;
}

std::array<Rgb, kPixelCount> renderFrame(const NeoPixelConfig &config, uint32_t frame)
{
    std::array<Rgb, kPixelCount> pixels = {};
    const Rgb base = paletteColor(config.palette_index);
    const Rgb dim_base = scaleColor(base, std::max(8, config.brightness_percent / 4));
    const uint32_t chase = (frame / 3) % kPixelCount;
    const uint32_t bounce = (frame / 4) % (2 * (kPixelCount - 1));
    const int scanner = (bounce < static_cast<uint32_t>(kPixelCount)) ? static_cast<int>(bounce)
                                                                      : static_cast<int>(2 * (kPixelCount - 1) - bounce);

    switch (std::clamp(config.effect_index, 0, kEffectCount - 1)) {
        case 0:
            pixels.fill(base);
            break;
        case 1:
            pixels.fill(((frame / 10) % 2) == 0 ? base : Rgb{0, 0, 0});
            break;
        case 2: {
            const Rgb breathed = scaleColor(base, 10 + (wave8(frame * 3, 0) * 90) / 255);
            pixels.fill(breathed);
            break;
        }
        case 3:
            for (int index = 0; index < kPixelCount; ++index) {
                pixels[static_cast<size_t>(index)] = (index <= static_cast<int>(chase)) ? base : Rgb{0, 0, 0};
            }
            break;
        case 4:
            for (int index = 0; index < kPixelCount; ++index) {
                pixels[static_cast<size_t>(index)] = (((index + frame / 2) % 3) == 0) ? base : Rgb{0, 0, 0};
            }
            break;
        case 5:
            for (int index = 0; index < kPixelCount; ++index) {
                pixels[static_cast<size_t>(index)] = wheelColor(static_cast<uint8_t>((frame * 7) + (index * 64)));
            }
            break;
        case 6:
            pixels[static_cast<size_t>(scanner)] = base;
            break;
        case 7:
            for (int index = 0; index < kPixelCount; ++index) {
                const bool lit = (((frame * 17) + static_cast<uint32_t>(index * 31)) % 11) < 2;
                pixels[static_cast<size_t>(index)] = lit ? base : dim_base;
            }
            break;
        case 8:
            for (int index = 0; index < kPixelCount; ++index) {
                const uint8_t twinkle = wave8(frame * 5, static_cast<uint32_t>(index * 70));
                pixels[static_cast<size_t>(index)] = scaleColor(base, (twinkle * 100) / 255);
            }
            break;
        case 9:
            for (int index = 0; index < kPixelCount; ++index) {
                const int distance = std::abs(index - scanner);
                const int falloff = std::max(0, 100 - (distance * 38));
                pixels[static_cast<size_t>(index)] = scaleColor(base, falloff);
            }
            break;
        case 10:
            for (int index = 0; index < kPixelCount; ++index) {
                pixels[static_cast<size_t>(index)] = scaleColor(base, 15 + (wave8(frame * 4, static_cast<uint32_t>(index * 40)) * 85) / 255);
            }
            break;
        case 11:
            pixels[static_cast<size_t>(chase)] = base;
            pixels[static_cast<size_t>(kPixelCount - 1 - chase)] = wheelColor(static_cast<uint8_t>(frame * 11));
            break;
        case 12:
            pixels.fill(dim_base);
            pixels[static_cast<size_t>((frame / 2) % kPixelCount)] = {255, 255, 255};
            break;
        case 13:
            pixels[static_cast<size_t>(scanner)] = base;
            pixels[static_cast<size_t>(kPixelCount - 1 - scanner)] = blendColor(base, {255, 255, 255}, 96);
            break;
        case 14:
            for (int index = 0; index < kPixelCount; ++index) {
                const Rgb warm = {255, clampByte(120 + index * 24), 20};
                pixels[static_cast<size_t>(index)] = scaleColor(warm, 10 + (wave8(frame * 6, static_cast<uint32_t>(index * 29)) * 90) / 255);
            }
            break;
        case 15:
            for (int index = 0; index < kPixelCount; ++index) {
                const int distance = (scanner >= index) ? (scanner - index) : (kPixelCount + scanner - index);
                const int falloff = std::max(0, 100 - (distance * 28));
                pixels[static_cast<size_t>(index)] = scaleColor(base, falloff);
            }
            break;
        case 16:
            for (int index = 0; index < kPixelCount; ++index) {
                const bool red = (((index + frame / 2) % 2) == 0);
                pixels[static_cast<size_t>(index)] = red ? Rgb{255, 0, 0} : Rgb{255, 255, 255};
            }
            break;
        case 17:
            for (int index = 0; index < kPixelCount; ++index) {
                const bool left = index < (kPixelCount / 2);
                const bool flash = ((frame / 6) % 2) == 0;
                pixels[static_cast<size_t>(index)] = (left == flash) ? Rgb{255, 0, 0} : Rgb{0, 64, 255};
            }
            break;
        case 18:
            for (int index = 0; index < kPixelCount; ++index) {
                const Rgb sunset = blendColor({255, 90, 24}, {128, 32, 255}, wave8(frame * 2, static_cast<uint32_t>(index * 45)));
                pixels[static_cast<size_t>(index)] = sunset;
            }
            break;
        case 19:
            for (int index = 0; index < kPixelCount; ++index) {
                const Rgb aurora = blendColor({0, 255, 96}, {0, 128, 255}, wave8(frame * 3, static_cast<uint32_t>(index * 60)));
                pixels[static_cast<size_t>(index)] = scaleColor(aurora, 35 + (tri8(frame * 5 + static_cast<uint32_t>(index * 40)) * 65) / 255);
            }
            break;
        case 20:
            for (int index = 0; index < kPixelCount; ++index) {
                const int distance = std::abs(index - scanner);
                const int falloff = std::max(0, 100 - (distance * 30));
                pixels[static_cast<size_t>(index)] = scaleColor(base, falloff);
            }
            break;
        default:
            pixels.fill(base);
            break;
    }

    for (Rgb &pixel : pixels) {
        pixel = scaleColor(pixel, config.brightness_percent);
    }

    return pixels;
}

void writeFrameLocked(const std::array<Rgb, kPixelCount> &pixels)
{
    if (s_strip == nullptr) {
        return;
    }

    for (int index = 0; index < kPixelCount; ++index) {
        const Rgb &pixel = pixels[static_cast<size_t>(index)];
        led_strip_set_pixel(s_strip, index, pixel.r, pixel.g, pixel.b);
    }
    led_strip_refresh(s_strip);
}

void neopixelTask(void *context)
{
    (void)context;

    while (true) {
        {
            std::lock_guard<std::mutex> guard(s_mutex);

            if (!s_config.enabled || !isAllowedGpio(s_config.gpio)) {
                clearStripLocked();
            } else if (ensureStripLocked()) {
                writeFrameLocked(renderFrame(s_config, s_frame));
                ++s_frame;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(kRefreshPeriodMs));
    }
}
} // namespace

extern "C" void jc4880_neopixel_init(void)
{
    std::lock_guard<std::mutex> guard(s_mutex);
    if (s_initialized) {
        return;
    }

    BaseType_t task_created = xTaskCreateWithCaps(neopixelTask,
                                                  "NeoPixel",
                                                  4096,
                                                  nullptr,
                                                  1,
                                                  &s_task,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (task_created != pdPASS) {
        task_created = xTaskCreate(neopixelTask, "NeoPixel", 4096, nullptr, 1, &s_task);
    }
    if (task_created != pdPASS) {
        ESP_LOGW(kTag, "Failed to start Neopixel task");
        s_task = nullptr;
        return;
    }

    s_initialized = true;
}

extern "C" void jc4880_neopixel_apply_config(bool enabled, int gpio, int brightness_percent, int palette_index, int effect_index)
{
    jc4880_neopixel_init();

    std::lock_guard<std::mutex> guard(s_mutex);
    s_config.enabled = enabled;
    s_config.gpio = gpio;
    s_config.brightness_percent = std::clamp(brightness_percent, 0, 100);
    s_config.palette_index = std::clamp(palette_index, 0, static_cast<int>(kPalette.size()) - 1);
    s_config.effect_index = std::clamp(effect_index, 0, kEffectCount - 1);
    if (!enabled) {
        clearStripLocked();
    }
}