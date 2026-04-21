#include "InternetRadio.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "audio_player.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

LV_IMG_DECLARE(img_app_radio_browser);

namespace {

static const char *TAG = "InternetRadio";
static constexpr const char *kApiBaseUrl = "http://de1.api.radio-browser.info/json";
static constexpr int kListLimit = 40;
static constexpr uint32_t kPreviewTaskStackSize = 32 * 1024;
static constexpr UBaseType_t kPreviewTaskPriority = 3;
static constexpr size_t kPreviewCommandQueueLength = 1;
static constexpr TickType_t kPreviewStopWait = pdMS_TO_TICKS(5000);
static constexpr TickType_t kCodecResetDelay = pdMS_TO_TICKS(50);
static constexpr TickType_t kCodecResumeDelay = pdMS_TO_TICKS(100);
static constexpr int kStreamTimeoutMs = 3000;
static constexpr int kMaxHttpRedirects = 5;
static constexpr size_t kStreamBufferPreferredSize = 96 * 1024;
static constexpr size_t kStreamBufferMinimumSize = 32 * 1024;
static constexpr size_t kStreamBufferStepSize = 16 * 1024;
static constexpr size_t kStreamScratchSize = 4096;
static constexpr size_t kStreamNetworkReadChunk = 8 * 1024;
static constexpr size_t kStreamInitialPrefillBytes = 24 * 1024;
static constexpr int kStreamReadRetryCount = 6;
static constexpr TickType_t kStreamReadRetryDelay = pdMS_TO_TICKS(25);
static constexpr size_t kAsyncStatusTextCapacity = 192;
static constexpr const char *kRadioHttpUserAgent = "JC4880P443C-IW-Remote";

struct PreviewTaskContext {
    InternetRadio *app = nullptr;
    std::string title;
    std::string url;
    bool can_preview = false;
};

struct AsyncStatusContext {
    InternetRadio *app = nullptr;
    char text[kAsyncStatusTextCapacity] = {};
};

struct HttpMp3StreamContext {
    esp_http_client_handle_t client = nullptr;
    uint8_t *buffer = nullptr;
    uint8_t *scratch = nullptr;
    size_t capacity = 0;
    size_t read_offset = 0;
    size_t bytes_buffered = 0;
    size_t icy_meta_interval = 0;
    size_t bytes_until_metadata = 0;
    size_t metadata_bytes_remaining = 0;
    bool eof_reached = false;
    bool using_psram = false;
};

uint8_t *allocate_stream_buffer(size_t size, bool &using_psram)
{
    uint8_t *buffer = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer != nullptr) {
        using_psram = true;
        return buffer;
    }

    buffer = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
    using_psram = false;
    return buffer;
}

uint8_t *allocate_scratch_buffer(size_t size)
{
    uint8_t *buffer = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer != nullptr) {
        return buffer;
    }

    return static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
}

void log_radio_memory_snapshot(const char *stage)
{
    ESP_LOGI(TAG,
             "%s: internal free=%u largest=%u psram free=%u largest=%u",
             stage,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}

uint8_t *allocate_stream_buffer_adaptive(size_t &size, bool &using_psram)
{
    for (size_t attempt_size = kStreamBufferPreferredSize; attempt_size >= kStreamBufferMinimumSize; attempt_size -= kStreamBufferStepSize) {
        uint8_t *buffer = allocate_stream_buffer(attempt_size, using_psram);
        if (buffer != nullptr) {
            size = attempt_size;
            return buffer;
        }

        if (attempt_size == kStreamBufferMinimumSize) {
            break;
        }
    }

    size = 0;
    using_psram = false;
    return nullptr;
}

int resilient_http_read(HttpMp3StreamContext *context, uint8_t *dest, size_t len)
{
    int last_result = 0;

    for (int attempt = 0; attempt < kStreamReadRetryCount; ++attempt) {
        last_result = esp_http_client_read(context->client, reinterpret_cast<char *>(dest), static_cast<int>(len));
        if (last_result > 0) {
            return last_result;
        }

        const bool eof = esp_http_client_is_complete_data_received(context->client);
        if (eof) {
            context->eof_reached = true;
            return 0;
        }

        if (attempt == 0) {
            ESP_LOGW(TAG, "Radio stream read stalled while waiting for %u bytes", static_cast<unsigned>(len));
        }

        vTaskDelay(kStreamReadRetryDelay);
    }

    if (last_result < 0) {
        ESP_LOGW(TAG, "Radio stream read failed after retries: %d", last_result);
    }

    return 0;
}

int read_http_audio_bytes(HttpMp3StreamContext *context, uint8_t *dest, size_t len)
{
    if ((context == nullptr) || (context->client == nullptr) || (dest == nullptr) || (len == 0)) {
        return 0;
    }

    size_t total_audio = 0;
    while (total_audio < len) {
        if (context->metadata_bytes_remaining > 0) {
            const size_t skip_chunk = std::min(context->metadata_bytes_remaining, kStreamScratchSize);
            const int skipped = resilient_http_read(context,
                                                    context->scratch,
                                                    skip_chunk);
            if (skipped <= 0) {
                return static_cast<int>(total_audio);
            }

            context->metadata_bytes_remaining -= static_cast<size_t>(skipped);
            if (context->metadata_bytes_remaining == 0) {
                context->bytes_until_metadata = context->icy_meta_interval;
            }
            continue;
        }

        if ((context->icy_meta_interval > 0) && (context->bytes_until_metadata == 0)) {
            uint8_t metadata_length = 0;
            const int length_read = resilient_http_read(context,
                                                        &metadata_length,
                                                        1);
            if (length_read <= 0) {
                return static_cast<int>(total_audio);
            }

            context->metadata_bytes_remaining = static_cast<size_t>(metadata_length) * 16;
            if (context->metadata_bytes_remaining == 0) {
                context->bytes_until_metadata = context->icy_meta_interval;
            }
            continue;
        }

        const size_t audio_chunk = (context->icy_meta_interval > 0)
                                       ? std::min(len - total_audio, context->bytes_until_metadata)
                                       : (len - total_audio);
        const int read_bytes = resilient_http_read(context,
                                                   dest + total_audio,
                                                   audio_chunk);
        if (read_bytes <= 0) {
            return static_cast<int>(total_audio);
        }

        total_audio += static_cast<size_t>(read_bytes);
        if (context->icy_meta_interval > 0) {
            context->bytes_until_metadata -= static_cast<size_t>(read_bytes);
        }
    }

    return static_cast<int>(total_audio);
}

int append_http_mp3_stream_buffer(HttpMp3StreamContext *context, size_t target_bytes)
{
    if ((context == nullptr) || (context->client == nullptr) || (context->buffer == nullptr) || (context->capacity == 0) ||
        context->eof_reached) {
        return 0;
    }

    if (context->bytes_buffered >= target_bytes) {
        return 0;
    }

    int total_appended = 0;
    while ((context->bytes_buffered < target_bytes) && (context->bytes_buffered < context->capacity)) {
        const size_t remaining_capacity = context->capacity - context->bytes_buffered;
        const size_t remaining_target = target_bytes - context->bytes_buffered;
        const size_t read_size = std::min(remaining_capacity, std::min(remaining_target, kStreamNetworkReadChunk));
        const int bytes_read = read_http_audio_bytes(context, context->buffer + context->bytes_buffered, read_size);
        if (bytes_read <= 0) {
            context->eof_reached = esp_http_client_is_complete_data_received(context->client);
            break;
        }

        context->bytes_buffered += static_cast<size_t>(bytes_read);
        total_appended += bytes_read;

        if (static_cast<size_t>(bytes_read) < read_size) {
            break;
        }
    }

    return total_appended;
}

int refill_http_mp3_stream_buffer(HttpMp3StreamContext *context)
{
    if ((context == nullptr) || (context->client == nullptr) || (context->buffer == nullptr) || (context->capacity == 0) || context->eof_reached) {
        return 0;
    }

    context->read_offset = 0;
    context->bytes_buffered = 0;
    const int bytes_read = append_http_mp3_stream_buffer(context, kStreamNetworkReadChunk);
    if (bytes_read > 0) {
        ESP_LOGD(TAG, "HTTP stream refill produced %d bytes", bytes_read);
        return bytes_read;
    }

    context->read_offset = 0;
    context->bytes_buffered = 0;
    context->eof_reached = esp_http_client_is_complete_data_received(context->client);
    if ((bytes_read < 0) && !context->eof_reached) {
        ESP_LOGW(TAG, "HTTP stream read yielded %d while refilling radio buffer", bytes_read);
    }

    return 0;
}

int http_mp3_stream_read(void *user_ctx, uint8_t *buffer, size_t len, bool *is_eof)
{
    auto *context = static_cast<HttpMp3StreamContext *>(user_ctx);
    if ((context == nullptr) || (context->client == nullptr) || (buffer == nullptr) || (len == 0)) {
        if (is_eof != nullptr) {
            *is_eof = true;
        }
        return 0;
    }

    size_t total_copied = 0;
    while (total_copied < len) {
        if (context->read_offset >= context->bytes_buffered) {
            if (refill_http_mp3_stream_buffer(context) <= 0) {
                break;
            }
        }

        const size_t available = context->bytes_buffered - context->read_offset;
        const size_t chunk = std::min(len - total_copied, available);
        memcpy(buffer + total_copied, context->buffer + context->read_offset, chunk);
        context->read_offset += chunk;
        total_copied += chunk;
    }

    if (is_eof != nullptr) {
        *is_eof = context->eof_reached && (context->read_offset >= context->bytes_buffered);
    }

    return static_cast<int>(total_copied);
}

void http_mp3_stream_close(void *user_ctx)
{
    auto *context = static_cast<HttpMp3StreamContext *>(user_ctx);
    if (context == nullptr) {
        return;
    }

    if (context->client != nullptr) {
        esp_http_client_close(context->client);
        esp_http_client_cleanup(context->client);
        context->client = nullptr;
    }

    if (context->buffer != nullptr) {
        heap_caps_free(context->buffer);
        context->buffer = nullptr;
    }

    if (context->scratch != nullptr) {
        heap_caps_free(context->scratch);
        context->scratch = nullptr;
    }

    delete context;
}

bool ensure_audio_player_idle(TickType_t timeout)
{
    TickType_t start = xTaskGetTickCount();
    bool stop_requested = false;

    while (audio_player_get_state() != AUDIO_PLAYER_STATE_IDLE) {
        const esp_err_t stop_result = audio_player_stop();
        if (stop_result == ESP_OK) {
            stop_requested = true;
        } else if (stop_result != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "audio_player_stop failed while waiting for idle: %s", esp_err_to_name(stop_result));
        }

        if ((xTaskGetTickCount() - start) >= timeout) {
            ESP_LOGW(TAG,
                     "Audio player failed to reach idle. state=%d stop_requested=%d",
                     static_cast<int>(audio_player_get_state()),
                     stop_requested ? 1 : 0);
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    return true;
}

bool ensure_radio_audio_output_ready()
{
    ESP_RETURN_ON_ERROR(bsp_extra_codec_dev_stop(), TAG, "Failed to stop codec output path before radio restart");
    vTaskDelay(kCodecResetDelay);

    ESP_RETURN_ON_ERROR(bsp_extra_codec_init(), TAG, "Failed to initialize audio codec path");
    ESP_RETURN_ON_ERROR(bsp_extra_player_init(), TAG, "Failed to initialize shared audio player");
    ESP_RETURN_ON_ERROR(bsp_extra_codec_dev_resume(), TAG, "Failed to reopen codec output path");
    vTaskDelay(kCodecResumeDelay);

    const int current_volume = bsp_extra_codec_volume_get();
    ESP_RETURN_ON_ERROR(bsp_extra_codec_volume_set(current_volume, nullptr), TAG, "Failed to restore codec volume");
    ESP_RETURN_ON_ERROR(bsp_extra_codec_mute_set(false), TAG, "Failed to unmute codec output");

    return true;
}

bool decode_utf8_codepoint(const std::string &text, size_t &offset, uint32_t &codepoint)
{
    if (offset >= text.size()) {
        return false;
    }

    const unsigned char lead = static_cast<unsigned char>(text[offset++]);
    if (lead < 0x80) {
        codepoint = lead;
        return true;
    }

    size_t remaining = 0;
    if ((lead & 0xE0) == 0xC0) {
        codepoint = lead & 0x1F;
        remaining = 1;
    } else if ((lead & 0xF0) == 0xE0) {
        codepoint = lead & 0x0F;
        remaining = 2;
    } else if ((lead & 0xF8) == 0xF0) {
        codepoint = lead & 0x07;
        remaining = 3;
    } else {
        codepoint = '?';
        return true;
    }

    if ((offset + remaining) > text.size()) {
        codepoint = '?';
        offset = text.size();
        return true;
    }

    for (size_t index = 0; index < remaining; ++index) {
        const unsigned char next = static_cast<unsigned char>(text[offset++]);
        if ((next & 0xC0) != 0x80) {
            codepoint = '?';
            return true;
        }
        codepoint = (codepoint << 6) | (next & 0x3F);
    }

    return true;
}

std::string transliterate_codepoint(uint32_t codepoint)
{
    if (codepoint < 0x80) {
        return std::string(1, static_cast<char>(codepoint));
    }

    switch (codepoint) {
    case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
    case 0x0100: case 0x0102: case 0x0104: return "A";
    case 0x00C6: return "AE";
    case 0x00C7: case 0x0106: case 0x0108: case 0x010A: case 0x010C: return "C";
    case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB: case 0x0112: case 0x0116: case 0x0118: return "E";
    case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF: case 0x012A: case 0x0130: return "I";
    case 0x00D1: case 0x0143: case 0x0147: return "N";
    case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D8:
    case 0x014C: case 0x0150: return "O";
    case 0x0152: return "OE";
    case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC: case 0x016A: case 0x016E: case 0x0170: return "U";
    case 0x00DD: case 0x0178: return "Y";
    case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
    case 0x0101: case 0x0103: case 0x0105: return "a";
    case 0x00E6: return "ae";
    case 0x00E7: case 0x0107: case 0x0109: case 0x010B: case 0x010D: return "c";
    case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: case 0x0113: case 0x0117: case 0x0119: return "e";
    case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF: case 0x012B: case 0x0131: return "i";
    case 0x00F1: case 0x0144: case 0x0148: return "n";
    case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F8:
    case 0x014D: case 0x0151: return "o";
    case 0x0153: return "oe";
    case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC: case 0x016B: case 0x016F: case 0x0171: return "u";
    case 0x00FD: case 0x00FF: return "y";
    case 0x00DF: return "ss";
    case 0x2018: case 0x2019: case 0x2032: return "'";
    case 0x201C: case 0x201D: return "\"";
    case 0x2013: case 0x2014: return "-";
    case 0x2026: return "...";
    case 0x00A0: return " ";
    default:
        return "";
    }
}

std::string normalize_text(const std::string &input)
{
    std::string output;
    output.reserve(input.size());

    bool last_was_space = false;
    for (size_t offset = 0; offset < input.size();) {
        uint32_t codepoint = 0;
        if (!decode_utf8_codepoint(input, offset, codepoint)) {
            break;
        }

        std::string fragment = transliterate_codepoint(codepoint);
        for (char ch : fragment) {
            const bool is_space = std::isspace(static_cast<unsigned char>(ch)) != 0;
            if (is_space) {
                if (!last_was_space && !output.empty()) {
                    output.push_back(' ');
                }
                last_was_space = true;
            } else if ((static_cast<unsigned char>(ch) >= 32) && (static_cast<unsigned char>(ch) != 127)) {
                output.push_back(ch);
                last_was_space = false;
            }
        }
    }

    while (!output.empty() && output.back() == ' ') {
        output.pop_back();
    }

    return output.empty() ? std::string("Unknown") : output;
}

std::string lowercase_copy(const std::string &text)
{
    std::string copy = text;
    std::transform(copy.begin(), copy.end(), copy.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return copy;
}

bool is_mp3_preview_candidate(const std::string &codec, const std::string &url)
{
    const std::string codec_lower = lowercase_copy(codec);
    const std::string url_lower = lowercase_copy(url);
    return (codec_lower.find("mp3") != std::string::npos) ||
           (codec_lower.find("mpeg") != std::string::npos) ||
           (url_lower.find(".mp3") != std::string::npos);
}

bool url_uses_https(const std::string &url)
{
    return url.rfind("https://", 0) == 0;
}

void configure_http_client_security(const std::string &url, esp_http_client_config_t &config)
{
    if (url_uses_https(url)) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }
}

std::string safe_string(cJSON *object, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || (item->valuestring == nullptr)) {
        return {};
    }

    return item->valuestring;
}

int safe_int(cJSON *object, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) ? item->valueint : 0;
}

} // namespace

InternetRadio::InternetRadio()
    : ESP_Brookesia_PhoneApp("Radio Browser", &img_app_radio_browser, true),
      _screen(nullptr),
      _titleLabel(nullptr),
      _subtitleLabel(nullptr),
      _statusLabel(nullptr),
      _list(nullptr),
      _entries(),
      _buttonIndexMap(),
      _viewMode(ViewMode::Root),
      _stationSource(StationSource::None),
      _activeFilterDisplay(),
    _activeFilterValue(),
    _previewTaskHandle(nullptr),
    _previewCommandQueue(nullptr),
    _previewTaskUsesCapsStack(false),
        _previewStartInProgress(false),
        _playerDialog(nullptr),
        _playerDialogTitleLabel(nullptr),
        _playerDialogDetailLabel(nullptr),
        _playerDialogStatusLabel(nullptr),
        _playerDialogPlayButton(nullptr),
        _playerDialogStopButton(nullptr),
        _selectedStationIndex(0),
        _activeStationTitle()
{
}

bool InternetRadio::init(void)
{
    return true;
}

bool InternetRadio::run(void)
{
    if (_screen == nullptr) {
        if (!buildUi()) {
            return false;
        }
        showRootMenu();
    }

    audio_player_callback_register(radioAudioCallback, this);
    lv_scr_load(_screen);
    return true;
}

bool InternetRadio::back(void)
{
    switch (_viewMode) {
    case ViewMode::Stations:
        switch (_stationSource) {
        case StationSource::Country:
            loadCountries();
            return true;
        case StationSource::Language:
            loadLanguages();
            return true;
        case StationSource::Tag:
            loadTags();
            return true;
        case StationSource::Popular:
        case StationSource::None:
            showRootMenu();
            return true;
        }
        break;
    case ViewMode::Countries:
    case ViewMode::Languages:
    case ViewMode::Tags:
        showRootMenu();
        return true;
    case ViewMode::Root:
        return notifyCoreClosed();
    }

    return notifyCoreClosed();
}

bool InternetRadio::close(void)
{
    closePlayerDialog();
    stopPreviewPlayback();
    bsp_extra_codec_dev_stop();
    return notifyCoreClosed();
}

bool InternetRadio::pause(void)
{
    closePlayerDialog();
    stopPreviewPlayback();
    bsp_extra_codec_dev_stop();
    return true;
}

bool InternetRadio::resume(void)
{
    audio_player_callback_register(radioAudioCallback, this);
    return true;
}

bool InternetRadio::buildUi(void)
{
    _screen = lv_obj_create(nullptr);
    if (_screen == nullptr) {
        return false;
    }

    lv_obj_set_style_bg_color(_screen, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_screen, 0, 0);
    lv_obj_set_style_pad_all(_screen, 24, 0);

    _titleLabel = lv_label_create(_screen);
    lv_label_set_text(_titleLabel, "Internet Radio");
    lv_obj_set_style_text_font(_titleLabel, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(_titleLabel, lv_color_hex(0xF7FAFC), 0);
    lv_obj_align(_titleLabel, LV_ALIGN_TOP_LEFT, 0, 4);

    _subtitleLabel = lv_label_create(_screen);
    lv_label_set_text(_subtitleLabel, "Radio Browser catalog inspired by Home Assistant");
    lv_obj_set_style_text_font(_subtitleLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_subtitleLabel, lv_color_hex(0x91A4BF), 0);
    lv_obj_align_to(_subtitleLabel, _titleLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    _statusLabel = lv_label_create(_screen);
    lv_label_set_text(_statusLabel, "Ready");
    lv_obj_set_style_text_font(_statusLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_statusLabel, lv_color_hex(0xFFD166), 0);
    lv_obj_align_to(_statusLabel, _subtitleLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    _list = lv_list_create(_screen);
    if (_list == nullptr) {
        return false;
    }

    lv_obj_set_size(_list, 432, 610);
    lv_obj_align(_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(_list, 28, 0);
    lv_obj_set_style_bg_color(_list, lv_color_hex(0x121D31), 0);
    lv_obj_set_style_bg_opa(_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_style_pad_all(_list, 10, 0);
    lv_obj_set_style_pad_row(_list, 10, 0);
    lv_obj_set_scrollbar_mode(_list, LV_SCROLLBAR_MODE_OFF);

    return true;
}

void InternetRadio::setStatus(const std::string &text)
{
    bsp_display_lock(0);
    applyStatusText(text.c_str());
    bsp_display_unlock();
}

void InternetRadio::applyStatusText(const char *text)
{
    if (text == nullptr) {
        text = "";
    }

    if (_statusLabel != nullptr) {
        lv_label_set_text(_statusLabel, text);
    }

    if (_playerDialogStatusLabel != nullptr) {
        lv_label_set_text(_playerDialogStatusLabel, text);
    }
}

void InternetRadio::setStatusFromTask(const char *text)
{
    auto *context = new AsyncStatusContext{};
    if (context == nullptr) {
        ESP_LOGW(TAG, "Failed to allocate async status context");
        return;
    }

    context->app = this;
    snprintf(context->text, sizeof(context->text), "%s", (text != nullptr) ? text : "");

    bsp_display_lock(0);
    if (lv_async_call(applyAsyncStatus, context) != LV_RES_OK) {
        bsp_display_unlock();
        delete context;
        ESP_LOGW(TAG, "Failed to queue async radio status update");
        return;
    }
    bsp_display_unlock();
}

void InternetRadio::setStatusForContext(const std::string &text, bool from_task)
{
    if (from_task) {
        setStatusFromTask(text.c_str());
    } else {
        setStatus(text);
    }
}

void InternetRadio::applyAsyncStatus(void *context)
{
    auto *status_context = static_cast<AsyncStatusContext *>(context);
    if (status_context != nullptr) {
        if (status_context->app != nullptr) {
            status_context->app->applyStatusText(status_context->text);
        }
        delete status_context;
    }
}

void InternetRadio::showRootMenu(void)
{
    _viewMode = ViewMode::Root;
    _stationSource = StationSource::None;
    _activeFilterDisplay.clear();
    _activeFilterValue.clear();
    _entries = {
        {"Popular", "Top-voted stations", "popular", "Browse the most popular stations.", LV_SYMBOL_AUDIO, "", false},
        {"By Country", "Alphabetical country list", "countries", "Browse by country.", LV_SYMBOL_DIRECTORY, "", false},
        {"By Language", "Browse stations by language", "languages", "Browse by language.", LV_SYMBOL_EDIT, "", false},
        {"By Category", "Browse stations by tag/category", "tags", "Browse by tag.", LV_SYMBOL_SETTINGS, "", false},
    };
    setStatus("Browse live stations. MP3 stations can start a buffered preview.");
    renderEntries();
}

void InternetRadio::loadCountries(void)
{
    std::vector<ListEntry> entries;
    const std::string url = std::string(kApiBaseUrl) + "/countries?hidebroken=true&order=name&reverse=false&limit=" + std::to_string(kListLimit);
    if (!fetchEntries(url, ViewMode::Countries, StationSource::Country, "Loading countries...", entries)) {
        return;
    }

    _entries = std::move(entries);
    std::sort(_entries.begin(), _entries.end(), [](const ListEntry &left, const ListEntry &right) {
        return lowercase_copy(left.title) < lowercase_copy(right.title);
    });
    _viewMode = ViewMode::Countries;
    _stationSource = StationSource::Country;
    setStatus("Choose a country.");
    renderEntries();
}

void InternetRadio::loadLanguages(void)
{
    std::vector<ListEntry> entries;
    const std::string url = std::string(kApiBaseUrl) + "/languages?hidebroken=true&order=stationcount&reverse=true&limit=" + std::to_string(kListLimit);
    if (!fetchEntries(url, ViewMode::Languages, StationSource::Language, "Loading languages...", entries)) {
        return;
    }

    _entries = std::move(entries);
    _viewMode = ViewMode::Languages;
    _stationSource = StationSource::Language;
    setStatus("Choose a language.");
    renderEntries();
}

void InternetRadio::loadTags(void)
{
    std::vector<ListEntry> entries;
    const std::string url = std::string(kApiBaseUrl) + "/tags?hidebroken=true&order=stationcount&reverse=true&limit=" + std::to_string(kListLimit);
    if (!fetchEntries(url, ViewMode::Tags, StationSource::Tag, "Loading categories...", entries)) {
        return;
    }

    _entries = std::move(entries);
    _viewMode = ViewMode::Tags;
    _stationSource = StationSource::Tag;
    setStatus("Choose a category.");
    renderEntries();
}

void InternetRadio::loadPopularStations(void)
{
    loadStationsForFilter(StationSource::Popular, "Popular", "topvote");
}

void InternetRadio::loadStationsForFilter(StationSource source, const std::string &display_name, const std::string &value)
{
    std::vector<ListEntry> entries;
    std::string url;

    switch (source) {
    case StationSource::Popular:
        url = std::string(kApiBaseUrl) + "/stations/topvote/" + std::to_string(kListLimit);
        break;
    case StationSource::Country:
        url = std::string(kApiBaseUrl) + "/stations/bycountryexact/" + percentEncode(value) + "?hidebroken=true&order=votes&reverse=true&limit=" + std::to_string(kListLimit);
        break;
    case StationSource::Language:
        url = std::string(kApiBaseUrl) + "/stations/bylanguageexact/" + percentEncode(value) + "?hidebroken=true&order=votes&reverse=true&limit=" + std::to_string(kListLimit);
        break;
    case StationSource::Tag:
        url = std::string(kApiBaseUrl) + "/stations/bytagexact/" + percentEncode(value) + "?hidebroken=true&order=votes&reverse=true&limit=" + std::to_string(kListLimit);
        break;
    case StationSource::None:
        return;
    }

    if (!fetchEntries(url, ViewMode::Stations, source, "Loading stations...", entries)) {
        return;
    }

    _entries = std::move(entries);
    std::stable_sort(_entries.begin(), _entries.end(), [](const ListEntry &left, const ListEntry &right) {
        if (left.canPreview != right.canPreview) {
            return left.canPreview > right.canPreview;
        }
        return lowercase_copy(left.title) < lowercase_copy(right.title);
    });
    _viewMode = ViewMode::Stations;
    _stationSource = source;
    _activeFilterDisplay = display_name;
    _activeFilterValue = value;
    setStatus("Select a station to start an MP3 preview or inspect details.");
    renderEntries();
}

void InternetRadio::renderEntries(void)
{
    if (_list == nullptr) {
        return;
    }

    _buttonIndexMap.clear();
    lv_obj_clean(_list);

    if (_entries.empty()) {
        lv_obj_t *label = lv_label_create(_list);
        lv_label_set_text(label, "No entries available.");
        lv_obj_set_style_text_color(label, lv_color_hex(0xD9E2EC), 0);
        return;
    }

    for (size_t i = 0; i < _entries.size(); ++i) {
        std::string button_text = _entries[i].subtitle.empty() ? _entries[i].title : (_entries[i].title + "\n" + _entries[i].subtitle);
        lv_obj_t *button = lv_list_add_btn(_list, entrySymbol(_entries[i]), button_text.c_str());
        lv_obj_set_height(button, 76);
        lv_obj_set_style_radius(button, 18, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x19263D), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_text_color(button, lv_color_hex(0xF7FAFC), 0);
        lv_obj_add_event_cb(button, onListButtonClicked, LV_EVENT_CLICKED, this);
        _buttonIndexMap[button] = i;

        lv_obj_t *label = lv_obj_get_child(button, 1);
        if (label != nullptr) {
            lv_obj_set_width(label, 330);
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(0xF7FAFC), 0);
        }
    }
}

void InternetRadio::showEntryDetails(size_t index)
{
    if (index >= _entries.size()) {
        return;
    }

    const ListEntry &entry = _entries[index];
    std::string message = entry.subtitle;
    if (!entry.detail.empty()) {
        if (!message.empty()) {
            message += "\n\n";
        }
        message += entry.detail;
    }

    if (!message.empty()) {
        message += "\n\n";
    }
    message += entry.canPreview ?
        "Continuous MP3 playback is available for this station." :
        "Playback is currently limited to MP3 stations in this firmware.";

    lv_obj_t *box = lv_msgbox_create(nullptr, entry.title.c_str(), message.c_str(), nullptr, true);
    lv_obj_set_width(box, 420);
    lv_obj_center(box);
}

void InternetRadio::showPlayerDialog(size_t index)
{
    if ((_screen == nullptr) || (index >= _entries.size())) {
        return;
    }

    _selectedStationIndex = index;
    const ListEntry &entry = _entries[index];

    if (_playerDialog != nullptr) {
        lv_obj_del(_playerDialog);
        _playerDialog = nullptr;
        _playerDialogTitleLabel = nullptr;
        _playerDialogDetailLabel = nullptr;
        _playerDialogStatusLabel = nullptr;
        _playerDialogPlayButton = nullptr;
        _playerDialogStopButton = nullptr;
    }

    _playerDialog = lv_obj_create(_screen);
    if (_playerDialog == nullptr) {
        return;
    }

    lv_obj_set_size(_playerDialog, 420, 360);
    lv_obj_center(_playerDialog);
    lv_obj_set_style_radius(_playerDialog, 24, 0);
    lv_obj_set_style_bg_color(_playerDialog, lv_color_hex(0x101A2D), 0);
    lv_obj_set_style_bg_opa(_playerDialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_playerDialog, 2, 0);
    lv_obj_set_style_border_color(_playerDialog, lv_color_hex(0x2D425F), 0);
    lv_obj_set_style_pad_all(_playerDialog, 18, 0);
    lv_obj_clear_flag(_playerDialog, LV_OBJ_FLAG_SCROLLABLE);

    _playerDialogTitleLabel = lv_label_create(_playerDialog);
    lv_label_set_text(_playerDialogTitleLabel, entry.title.c_str());
    lv_obj_set_width(_playerDialogTitleLabel, 320);
    lv_label_set_long_mode(_playerDialogTitleLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_playerDialogTitleLabel, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(_playerDialogTitleLabel, lv_color_hex(0xF7FAFC), 0);
    lv_obj_align(_playerDialogTitleLabel, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *close_button = lv_btn_create(_playerDialog);
    lv_obj_set_size(close_button, 44, 44);
    lv_obj_align(close_button, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_radius(close_button, 22, 0);
    lv_obj_set_style_bg_color(close_button, lv_color_hex(0x25354F), 0);
    lv_obj_add_event_cb(close_button, onPlayerDialogButtonClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *close_label = lv_label_create(close_button);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_center(close_label);

    lv_obj_t *content = lv_obj_create(_playerDialog);
    lv_obj_set_size(content, 384, 210);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, 78);
    lv_obj_set_style_radius(content, 16, 0);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x162338), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 1, 0);
    lv_obj_set_style_border_color(content, lv_color_hex(0x2D425F), 0);
    lv_obj_set_style_pad_all(content, 14, 0);
    lv_obj_set_style_pad_row(content, 10, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);

    _playerDialogDetailLabel = lv_label_create(content);
    std::string detail = entry.subtitle.empty() ? entry.detail : entry.subtitle;
    if (!entry.detail.empty() && (detail != entry.detail)) {
        detail += "\n" + entry.detail;
    }
    if (!entry.codec.empty()) {
        if (!detail.empty()) {
            detail += "\n";
        }
        detail += std::string("Codec: ") + entry.codec;
    }
    if (detail.empty()) {
        detail = entry.canPreview ? "MP3 stream candidate." : "Stream format may be unsupported.";
    }
    lv_label_set_text(_playerDialogDetailLabel, detail.c_str());
    lv_obj_set_width(_playerDialogDetailLabel, lv_pct(100));
    lv_label_set_long_mode(_playerDialogDetailLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_playerDialogDetailLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_playerDialogDetailLabel, lv_color_hex(0x9BB0CC), 0);

    lv_obj_t *status_header = lv_label_create(content);
    lv_label_set_text(status_header, "Status");
    lv_obj_set_style_text_font(status_header, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_header, lv_color_hex(0xFFD166), 0);

    _playerDialogStatusLabel = lv_label_create(content);
    lv_label_set_text(_playerDialogStatusLabel, "Ready to start stream");
    lv_obj_set_width(_playerDialogStatusLabel, lv_pct(100));
    lv_label_set_long_mode(_playerDialogStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_playerDialogStatusLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_playerDialogStatusLabel, lv_color_hex(0xF7FAFC), 0);

    _playerDialogPlayButton = lv_btn_create(_playerDialog);
    lv_obj_set_size(_playerDialogPlayButton, 150, 50);
    lv_obj_align(_playerDialogPlayButton, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_radius(_playerDialogPlayButton, 20, 0);
    lv_obj_set_style_bg_color(_playerDialogPlayButton, lv_color_hex(0x0E9F6E), 0);
    lv_obj_add_event_cb(_playerDialogPlayButton, onPlayerDialogButtonClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *play_label = lv_label_create(_playerDialogPlayButton);
    lv_label_set_text(play_label, "Play");
    lv_obj_center(play_label);

    _playerDialogStopButton = lv_btn_create(_playerDialog);
    lv_obj_set_size(_playerDialogStopButton, 150, 50);
    lv_obj_align(_playerDialogStopButton, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_radius(_playerDialogStopButton, 20, 0);
    lv_obj_set_style_bg_color(_playerDialogStopButton, lv_color_hex(0xD64545), 0);
    lv_obj_add_event_cb(_playerDialogStopButton, onPlayerDialogButtonClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *stop_label = lv_label_create(_playerDialogStopButton);
    lv_label_set_text(stop_label, "Stop");
    lv_obj_center(stop_label);

    if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
        setStatus(std::string("Playing: ") + _activeStationTitle);
    } else if (_previewStartInProgress.load()) {
        setStatus(std::string("Opening stream: ") + entry.title);
    } else {
        setStatus(std::string("Selected station: ") + entry.title);
    }
}

void InternetRadio::closePlayerDialog(void)
{
    if (_playerDialog != nullptr) {
        lv_obj_del(_playerDialog);
        _playerDialog = nullptr;
    }

    _playerDialogTitleLabel = nullptr;
    _playerDialogDetailLabel = nullptr;
    _playerDialogStatusLabel = nullptr;
    _playerDialogPlayButton = nullptr;
    _playerDialogStopButton = nullptr;
}

void InternetRadio::playStationPreview(size_t index)
{
    if (index >= _entries.size()) {
        return;
    }

    queuePreviewPlayback(_entries[index], false);
}

bool InternetRadio::ensurePreviewWorkerReady(void)
{
    if (_previewTaskHandle != nullptr && _previewCommandQueue != nullptr) {
        return true;
    }

    if (_previewCommandQueue == nullptr) {
        _previewCommandQueue = xQueueCreate(kPreviewCommandQueueLength, sizeof(PreviewTaskContext *));
        if (_previewCommandQueue == nullptr) {
            ESP_LOGE(TAG,
                     "Failed to allocate radio preview command queue. Internal RAM free=%u largest=%u",
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                     static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
            return false;
        }
    }

    if (_previewTaskHandle != nullptr) {
        return true;
    }

    _previewTaskUsesCapsStack = false;
    if (xTaskCreatePinnedToCoreWithCaps(previewPlaybackTask,
                                        "radio_preview",
                                        kPreviewTaskStackSize,
                                        this,
                                        kPreviewTaskPriority,
                                        &_previewTaskHandle,
                                        1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        _previewTaskUsesCapsStack = true;
        ESP_LOGI(TAG, "Radio preview worker created with PSRAM-backed stack");
        return true;
    }

    if (xTaskCreatePinnedToCore(previewPlaybackTask,
                                "radio_preview",
                                kPreviewTaskStackSize,
                                this,
                                kPreviewTaskPriority,
                                &_previewTaskHandle,
                                1) != pdPASS) {
        const unsigned internal_free = static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        const unsigned internal_largest = static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        const unsigned psram_free = static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

        ESP_LOGE(TAG,
                 "Failed to create radio preview worker in both PSRAM and internal RAM. Internal RAM free=%u largest=%u PSRAM free=%u",
                 internal_free,
                 internal_largest,
                 psram_free);
        return false;
    }

    ESP_LOGI(TAG, "Radio preview worker created with internal RAM stack");
    return true;
}

void InternetRadio::destroyPreviewWorker(void)
{
    if ((_previewTaskHandle != nullptr) && (_previewTaskHandle != xTaskGetCurrentTaskHandle())) {
        vTaskDelete(_previewTaskHandle);
    }
    _previewTaskHandle = nullptr;
    _previewTaskUsesCapsStack = false;

    if (_previewCommandQueue != nullptr) {
        PreviewTaskContext *stale_context = nullptr;
        while (xQueueReceive(_previewCommandQueue, &stale_context, 0) == pdTRUE) {
            delete stale_context;
            stale_context = nullptr;
        }
        vQueueDelete(_previewCommandQueue);
        _previewCommandQueue = nullptr;
    }
}

bool InternetRadio::queuePreviewPlayback(const ListEntry &entry, bool from_task)
{
    _activeStationTitle = entry.title;
    if (entry.value.empty()) {
        setStatusForContext("Station does not provide a playable stream URL.", from_task);
        return false;
    }

    if (!ensurePreviewWorkerReady()) {
        setStatusForContext("Failed to start station preview task.", from_task);
        return false;
    }

    bool expected = false;
    if (!_previewStartInProgress.compare_exchange_strong(expected, true)) {
        setStatusForContext("A station preview is already starting. Please wait.", from_task);
        return false;
    }

    auto *task_context = new PreviewTaskContext{this, entry.title, entry.value, entry.canPreview};
    if (task_context == nullptr) {
        _previewStartInProgress.store(false);
        setStatusForContext("Unable to allocate station preview context.", from_task);
        return false;
    }

    ESP_LOGI(TAG, "Queueing station preview task for '%s' codec='%s' url='%s' preview=%d",
             entry.title.c_str(), entry.codec.c_str(), entry.value.c_str(), entry.canPreview ? 1 : 0);

    if (uxQueueMessagesWaiting(_previewCommandQueue) != 0) {
        PreviewTaskContext *stale_context = nullptr;
        if (xQueueReceive(_previewCommandQueue, &stale_context, 0) == pdTRUE) {
            delete stale_context;
        }
    }

    if (xQueueSend(_previewCommandQueue, &task_context, 0) != pdPASS) {
        delete task_context;
        _previewStartInProgress.store(false);
        ESP_LOGE(TAG,
                 "Failed to queue radio preview task. Internal RAM free=%u largest=%u PSRAM free=%u",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        setStatusForContext("Failed to start station preview task.", from_task);
        return false;
    }

    setStatusForContext(std::string("Opening stream: ") + entry.title, from_task);
    return true;
}

bool InternetRadio::startPreviewPlayback(const ListEntry &entry)
{
    if (entry.value.empty()) {
        setStatusFromTask("Station does not provide a stream URL.");
        return false;
    }

    if (audio_player_get_state() != AUDIO_PLAYER_STATE_IDLE) {
        if (!ensure_audio_player_idle(kPreviewStopWait)) {
            setStatusFromTask("Audio player is still stopping. Please try again.");
            return false;
        }
    }

    auto *stream_context = new HttpMp3StreamContext();
    if (stream_context == nullptr) {
        setStatusFromTask("Unable to allocate stream context.");
        return false;
    }

    log_radio_memory_snapshot("radio_start");

    esp_http_client_config_t config = {};
    config.url = entry.value.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = kStreamTimeoutMs;
    config.disable_auto_redirect = false;
    config.buffer_size = 1024;
    config.buffer_size_tx = 1024;
    config.user_data = stream_context;
    config.event_handler = [](esp_http_client_event_t *event) {
        if ((event == nullptr) || (event->user_data == nullptr) || (event->event_id != HTTP_EVENT_ON_HEADER) ||
            (event->header_key == nullptr) || (event->header_value == nullptr)) {
            return ESP_OK;
        }

        std::string header_key = event->header_key;
        std::transform(header_key.begin(), header_key.end(), header_key.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        if (header_key != "icy-metaint") {
            return ESP_OK;
        }

        auto *context = static_cast<HttpMp3StreamContext *>(event->user_data);
        const long meta_interval = std::strtol(event->header_value, nullptr, 10);
        if (meta_interval > 0) {
            context->icy_meta_interval = static_cast<size_t>(meta_interval);
            context->bytes_until_metadata = static_cast<size_t>(meta_interval);
            context->metadata_bytes_remaining = 0;
            ESP_LOGI(TAG, "Detected ICY metadata interval: %ld", meta_interval);
        }
        return ESP_OK;
    };
#if CONFIG_MBEDTLS_DYNAMIC_BUFFER
    config.tls_dyn_buf_strategy = HTTP_TLS_DYN_BUF_RX_STATIC;
#endif
    configure_http_client_security(entry.value, config);

    stream_context->client = esp_http_client_init(&config);
    if (stream_context->client == nullptr) {
        delete stream_context;
        setStatusFromTask("Failed to open station stream.");
        return false;
    }

    esp_http_client_set_header(stream_context->client, "Accept", "audio/mpeg,*/*");
    esp_http_client_set_header(stream_context->client, "User-Agent", kRadioHttpUserAgent);
    esp_http_client_set_header(stream_context->client, "Icy-MetaData", "0");

    int64_t content_length = 0;
    int status_code = 0;
    bool stream_ready = false;
    for (int redirect_index = 0; redirect_index <= kMaxHttpRedirects; ++redirect_index) {
        stream_context->icy_meta_interval = 0;
        stream_context->bytes_until_metadata = 0;
        stream_context->metadata_bytes_remaining = 0;
        stream_context->read_offset = 0;
        stream_context->bytes_buffered = 0;
        stream_context->eof_reached = false;

        const esp_err_t open_result = esp_http_client_open(stream_context->client, 0);
        if (open_result != ESP_OK) {
            http_mp3_stream_close(stream_context);
            setStatusFromTask("Failed to connect to station stream.");
            return false;
        }

        content_length = esp_http_client_fetch_headers(stream_context->client);
        status_code = esp_http_client_get_status_code(stream_context->client);
        if ((status_code >= 300) && (status_code < 400) && (redirect_index < kMaxHttpRedirects)) {
            ESP_LOGI(TAG, "Following radio stream redirect, status=%d", status_code);
            esp_http_client_set_redirection(stream_context->client);
            esp_http_client_close(stream_context->client);
            continue;
        }

        stream_ready = true;
        break;
    }

    ESP_LOGI(TAG,
             "Radio stream response status=%d content_length=%lld",
             status_code,
             static_cast<long long>(content_length));

    if (!stream_ready || (status_code < 200) || (status_code >= 300)) {
        http_mp3_stream_close(stream_context);
        setStatusFromTask("Station returned an unexpected HTTP status.");
        return false;
    }

    stream_context->buffer = allocate_stream_buffer_adaptive(stream_context->capacity, stream_context->using_psram);
    if (stream_context->buffer == nullptr) {
        log_radio_memory_snapshot("radio_buffer_alloc_failed");
        http_mp3_stream_close(stream_context);
        setStatusFromTask("Unable to allocate radio stream buffer.");
        return false;
    }

    stream_context->scratch = allocate_scratch_buffer(kStreamScratchSize);
    if (stream_context->scratch == nullptr) {
        http_mp3_stream_close(stream_context);
        setStatusFromTask("Unable to allocate radio stream scratch buffer.");
        return false;
    }

    ESP_LOGI(TAG, "Radio stream buffer allocated in %s (%u bytes)",
             stream_context->using_psram ? "PSRAM" : "internal RAM fallback",
             static_cast<unsigned>(stream_context->capacity));

    const size_t prefill_target = std::min(kStreamInitialPrefillBytes, stream_context->capacity / 2);
    const int prefilled_bytes = append_http_mp3_stream_buffer(stream_context, prefill_target);
    ESP_LOGI(TAG,
             "Radio stream prefilled %d/%u bytes before playback",
             prefilled_bytes,
             static_cast<unsigned>(prefill_target));
    if (prefilled_bytes <= 0) {
        http_mp3_stream_close(stream_context);
        setStatusFromTask("Station stream did not deliver audio data.");
        return false;
    }

    if (!ensure_radio_audio_output_ready()) {
        http_mp3_stream_close(stream_context);
        setStatusFromTask("Audio output is unavailable.");
        return false;
    }

    audio_player_stream_t stream = {
        .read_fn = http_mp3_stream_read,
        .close_fn = http_mp3_stream_close,
        .user_ctx = stream_context,
    };

    if (audio_player_play_mp3_stream(stream) != ESP_OK) {
        http_mp3_stream_close(stream_context);
        setStatusFromTask("Stream playback failed to start.");
        return false;
    }

    return true;
}

void InternetRadio::previewPlaybackTask(void *context)
{
    auto *app = static_cast<InternetRadio *>(context);
    if (app == nullptr || app->_previewCommandQueue == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    for (;;) {
        PreviewTaskContext *task_context = nullptr;
        if (xQueueReceive(app->_previewCommandQueue, &task_context, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (task_context == nullptr) {
            continue;
        }

        ListEntry entry = {};
        entry.title = std::move(task_context->title);
        entry.value = std::move(task_context->url);
        entry.canPreview = task_context->can_preview;
        delete task_context;

        char status_text[kAsyncStatusTextCapacity] = {};
        if (entry.canPreview) {
            snprintf(status_text, sizeof(status_text), "Starting stream: %s", entry.title.c_str());
        } else {
            snprintf(status_text, sizeof(status_text), "Trying stream: %s (MP3 streams work best)", entry.title.c_str());
        }
        app->setStatusFromTask(status_text);

        const bool started = app->startPreviewPlayback(entry);
        if (started) {
            snprintf(status_text, sizeof(status_text), "Playing: %s", entry.title.c_str());
            app->setStatusFromTask(status_text);
        } else if (!entry.canPreview) {
            snprintf(status_text, sizeof(status_text), "Playback failed for %s. This stream may use an unsupported codec.", entry.title.c_str());
            app->setStatusFromTask(status_text);
        } else {
            snprintf(status_text, sizeof(status_text), "Failed to start streaming %s. Try another station.", entry.title.c_str());
            app->setStatusFromTask(status_text);
        }

        app->_previewStartInProgress.store(false);
    }
}

void InternetRadio::stopPreviewPlayback(void)
{
    stopPreviewPlaybackInternal(false);
}

bool InternetRadio::stopPreviewPlaybackInternal(bool from_task)
{
    _previewStartInProgress.store(false);
    if (_activeStationTitle.empty()) {
        setStatusForContext("Playback stopped.", from_task);
    } else {
        setStatusForContext(std::string("Stopping: ") + _activeStationTitle, from_task);
    }

    if (audio_player_get_state() != AUDIO_PLAYER_STATE_IDLE) {
        if (!ensure_audio_player_idle(kPreviewStopWait)) {
            setStatusForContext("Timed out while stopping playback.", from_task);
            ESP_LOGW(TAG, "Timed out waiting for audio player to go idle");
            return false;
        }
    }

    _activeStationTitle.clear();
    bsp_extra_codec_dev_stop();
    setStatusForContext("Playback stopped.", from_task);
    return true;
}

bool InternetRadio::debugPlayStation(const std::string &country, const std::string &station_name)
{
    const std::string normalized_country = normalize_text(country);
    const std::string normalized_station = normalize_text(station_name);
    if (normalized_country.empty() || normalized_station.empty()) {
        ESP_LOGW(TAG, "Debug play rejected because country or station name is empty");
        setStatusFromTask("radio.play requires both a country and station name.");
        return false;
    }

    const std::string url = std::string(kApiBaseUrl) + "/stations/bycountryexact/" + percentEncode(normalized_country) +
                            "?hidebroken=true&order=votes&reverse=true&limit=200";
    std::vector<ListEntry> entries;
    ESP_LOGI(TAG, "Debug station lookup: country='%s' station='%s'", normalized_country.c_str(), normalized_station.c_str());
    if (!fetchJsonArray(url, entries, ViewMode::Stations)) {
        setStatusFromTask("Debug station lookup failed. Check Wi-Fi.");
        return false;
    }

    std::stable_sort(entries.begin(), entries.end(), [](const ListEntry &left, const ListEntry &right) {
        if (left.canPreview != right.canPreview) {
            return left.canPreview > right.canPreview;
        }
        return lowercase_copy(left.title) < lowercase_copy(right.title);
    });

    const std::string target = lowercase_copy(normalized_station);
    auto exact_match = std::find_if(entries.begin(), entries.end(), [&target](const ListEntry &entry) {
        return lowercase_copy(normalize_text(entry.title)) == target;
    });

    auto match = exact_match;
    if (match == entries.end()) {
        match = std::find_if(entries.begin(), entries.end(), [&target](const ListEntry &entry) {
            return lowercase_copy(normalize_text(entry.title)).find(target) != std::string::npos;
        });
    }

    if (match == entries.end()) {
        ESP_LOGW(TAG, "Debug station lookup found no match for '%s' in '%s' (%u candidates)",
                 normalized_station.c_str(), normalized_country.c_str(), static_cast<unsigned>(entries.size()));
        setStatusFromTask("Requested station was not found in Radio Browser results.");
        return false;
    }

    ESP_LOGI(TAG, "Debug station match: title='%s' codec='%s' url='%s' preview=%d",
             match->title.c_str(), match->codec.c_str(), match->value.c_str(), match->canPreview ? 1 : 0);
    return queuePreviewPlayback(*match, true);
}

bool InternetRadio::debugStopPlayback(void)
{
    ESP_LOGI(TAG, "Debug stop requested. %s", debugDescribeState().c_str());
    return stopPreviewPlaybackInternal(true);
}

std::string InternetRadio::debugDescribeState(void) const
{
    std::ostringstream stream;
    stream << "player_state=" << static_cast<int>(audio_player_get_state())
           << " preview_start=" << (_previewStartInProgress.load() ? 1 : 0)
           << " preview_task=" << (_previewTaskHandle != nullptr ? "set" : "null")
           << " active_station=";

    if (_activeStationTitle.empty()) {
        stream << "<none>";
    } else {
        stream << '"' << _activeStationTitle << '"';
    }

    return stream.str();
}

void InternetRadio::handleEntrySelection(size_t index)
{
    if (index >= _entries.size()) {
        return;
    }

    const ListEntry &entry = _entries[index];

    switch (_viewMode) {
    case ViewMode::Root:
        if (entry.value == "popular") {
            loadPopularStations();
        } else if (entry.value == "countries") {
            loadCountries();
        } else if (entry.value == "languages") {
            loadLanguages();
        } else if (entry.value == "tags") {
            loadTags();
        }
        break;
    case ViewMode::Countries:
        loadStationsForFilter(StationSource::Country, entry.title, entry.value);
        break;
    case ViewMode::Languages:
        loadStationsForFilter(StationSource::Language, entry.title, entry.value);
        break;
    case ViewMode::Tags:
        loadStationsForFilter(StationSource::Tag, entry.title, entry.value);
        break;
    case ViewMode::Stations:
        showPlayerDialog(index);
        break;
    }
}

void InternetRadio::onPlayerDialogButtonClicked(lv_event_t *event)
{
    if (event == nullptr) {
        return;
    }

    auto *self = static_cast<InternetRadio *>(lv_event_get_user_data(event));
    if (self == nullptr) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(event);
    if (target == self->_playerDialogPlayButton) {
        self->playStationPreview(self->_selectedStationIndex);
        return;
    }

    if (target == self->_playerDialogStopButton) {
        self->stopPreviewPlayback();
        return;
    }

    self->closePlayerDialog();
}

void InternetRadio::radioAudioCallback(audio_player_cb_ctx_t *ctx)
{
    if ((ctx == nullptr) || (ctx->user_ctx == nullptr)) {
        return;
    }

    auto *self = static_cast<InternetRadio *>(ctx->user_ctx);
    char status_text[kAsyncStatusTextCapacity] = {};
    switch (ctx->audio_event) {
    case AUDIO_PLAYER_CALLBACK_EVENT_PLAYING:
        if (!self->_activeStationTitle.empty()) {
            snprintf(status_text, sizeof(status_text), "Playing: %s", self->_activeStationTitle.c_str());
            self->setStatusFromTask(status_text);
        } else {
            self->setStatusFromTask("Playing stream");
        }
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PAUSE:
        self->setStatusFromTask("Playback paused");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_IDLE:
        if (!self->_activeStationTitle.empty()) {
            snprintf(status_text, sizeof(status_text), "Stopped: %s", self->_activeStationTitle.c_str());
            self->setStatusFromTask(status_text);
            self->_activeStationTitle.clear();
        } else {
            self->setStatusFromTask("Playback stopped");
        }
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_ERROR:
        self->setStatusFromTask("Playback failed");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE:
        self->setStatusFromTask("Stream codec is unsupported");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_COMPLETED_PLAYING_NEXT:
    case AUDIO_PLAYER_CALLBACK_EVENT_SHUTDOWN:
    case AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN:
        break;
    }
}

const char *InternetRadio::entrySymbol(void) const
{
    switch (_viewMode) {
    case ViewMode::Root:
        return LV_SYMBOL_LIST;
    case ViewMode::Countries:
        return LV_SYMBOL_DIRECTORY;
    case ViewMode::Languages:
        return LV_SYMBOL_EDIT;
    case ViewMode::Tags:
        return LV_SYMBOL_SETTINGS;
    case ViewMode::Stations:
        return LV_SYMBOL_AUDIO;
    }

    return LV_SYMBOL_LIST;
}

const char *InternetRadio::entrySymbol(const ListEntry &entry) const
{
    if (!entry.leadingSymbol.empty()) {
        return entry.leadingSymbol.c_str();
    }

    return entrySymbol();
}

std::string InternetRadio::percentEncode(const std::string &value)
{
    std::ostringstream stream;
    stream.fill('0');
    stream << std::hex << std::uppercase;

    for (unsigned char ch : value) {
        if (std::isalnum(ch) || (ch == '-') || (ch == '_') || (ch == '.') || (ch == '~')) {
            stream << static_cast<char>(ch);
        } else {
            stream << '%' << std::setw(2) << static_cast<int>(ch);
        }
    }

    return stream.str();
}

bool InternetRadio::fetchEntries(const std::string &url, ViewMode next_mode, StationSource next_source,
                                 const std::string &status_text, std::vector<ListEntry> &out_entries)
{
    (void)next_mode;
    (void)next_source;

    setStatus(status_text);
    if (!fetchJsonArray(url, out_entries, next_mode)) {
        setStatus("Request failed. Check Wi-Fi and try again.");
        return false;
    }

    if (out_entries.empty()) {
        setStatus("No results returned by Radio Browser.");
        return false;
    }

    return true;
}

bool InternetRadio::fetchJsonArray(const std::string &url, std::vector<ListEntry> &out_entries, ViewMode mode)
{
    std::string response;
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000;
    config.event_handler = httpEventHandler;
    config.user_data = &response;
    config.disable_auto_redirect = false;
    configure_http_client_security(url, config);

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }

    bool ok = false;
    if (esp_http_client_perform(client) == ESP_OK) {
        const int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            cJSON *root = cJSON_Parse(response.c_str());
            if (cJSON_IsArray(root)) {
                cJSON *item = nullptr;
                cJSON_ArrayForEach(item, root) {
                    if (!cJSON_IsObject(item)) {
                        continue;
                    }

                    if (mode == ViewMode::Countries) {
                        const std::string name = normalize_text(safe_string(item, "name"));
                        const std::string country_code = normalize_text(safe_string(item, "iso_3166_1"));
                        const int station_count = safe_int(item, "stationcount");
                        if (!name.empty()) {
                            out_entries.push_back({name, std::to_string(station_count) + " stations", name, {}, country_code.empty() ? LV_SYMBOL_DIRECTORY : country_code, {}, false});
                        }
                    } else if (mode == ViewMode::Languages) {
                        const std::string name = normalize_text(safe_string(item, "name"));
                        const int station_count = safe_int(item, "stationcount");
                        if (!name.empty()) {
                            out_entries.push_back({name, std::to_string(station_count) + " stations", name, {}, LV_SYMBOL_EDIT, {}, false});
                        }
                    } else if (mode == ViewMode::Tags) {
                        const std::string name = normalize_text(safe_string(item, "name"));
                        const int station_count = safe_int(item, "stationcount");
                        if (!name.empty()) {
                            out_entries.push_back({name, std::to_string(station_count) + " stations", name, {}, LV_SYMBOL_SETTINGS, {}, false});
                        }
                    } else if (mode == ViewMode::Stations) {
                        const std::string name = normalize_text(safe_string(item, "name"));
                        const std::string country = normalize_text(safe_string(item, "country"));
                        const std::string codec = normalize_text(safe_string(item, "codec"));
                        const int bitrate = safe_int(item, "bitrate");
                        const std::string favicon = safe_string(item, "favicon");
                        std::string stream_url = safe_string(item, "url_resolved");
                        if (stream_url.empty()) {
                            stream_url = safe_string(item, "url");
                        }

                        if (!name.empty()) {
                            std::string subtitle = country;
                            if (!codec.empty()) {
                                if (!subtitle.empty()) {
                                    subtitle += " | ";
                                }
                                subtitle += codec;
                            }
                            if (bitrate > 0) {
                                if (!subtitle.empty()) {
                                    subtitle += " ";
                                }
                                subtitle += std::to_string(bitrate) + " kbps";
                            }

                            std::string detail = "Stream URL: " + stream_url;
                            const std::string homepage = safe_string(item, "homepage");
                            if (!homepage.empty()) {
                                detail += "\nHomepage: " + homepage;
                            }
                            if (!favicon.empty()) {
                                detail += "\nArtwork URL: " + favicon;
                            }
                            detail += is_mp3_preview_candidate(codec, stream_url)
                                ? "\nPreview: Buffered MP3 playback supported"
                                : "\nPreview: Only MP3 stations are playable in the current firmware";

                            out_entries.push_back({name, subtitle, stream_url, detail, favicon.empty() ? LV_SYMBOL_AUDIO : LV_SYMBOL_IMAGE, codec, is_mp3_preview_candidate(codec, stream_url)});
                        }
                    }
                }
                ok = true;
            }
            cJSON_Delete(root);
        } else {
            ESP_LOGW(TAG, "Unexpected Radio Browser status: %d", status_code);
        }
    } else {
        ESP_LOGW(TAG, "HTTP request failed for %s", url.c_str());
    }

    esp_http_client_cleanup(client);
    return ok;
}

void InternetRadio::onListButtonClicked(lv_event_t *event)
{
    auto *self = static_cast<InternetRadio *>(lv_event_get_user_data(event));
    if (self == nullptr) {
        return;
    }

    lv_obj_t *button = lv_event_get_target(event);
    auto entry = self->_buttonIndexMap.find(button);
    if (entry == self->_buttonIndexMap.end()) {
        return;
    }

    self->handleEntrySelection(entry->second);
}

esp_err_t InternetRadio::httpEventHandler(esp_http_client_event_t *event)
{
    if ((event == nullptr) || (event->user_data == nullptr)) {
        return ESP_OK;
    }

    if ((event->event_id == HTTP_EVENT_ON_DATA) && (event->data != nullptr) && (event->data_len > 0)) {
        auto *response = static_cast<std::string *>(event->user_data);
        response->append(static_cast<const char *>(event->data), event->data_len);
    }

    return ESP_OK;
}