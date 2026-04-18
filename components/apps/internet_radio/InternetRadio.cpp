#include "InternetRadio.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <iomanip>
#include <sstream>

#include "audio_player.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

LV_IMG_DECLARE(img_app_music_player);

namespace {

static const char *TAG = "InternetRadio";
static constexpr const char *kApiBaseUrl = "http://de1.api.radio-browser.info/json";
static constexpr int kListLimit = 40;
static constexpr uint32_t kPreviewTaskStackSize = 32 * 1024;
static constexpr UBaseType_t kPreviewTaskPriority = 3;
static constexpr TickType_t kPreviewStopWait = pdMS_TO_TICKS(1500);
static constexpr int kStreamTimeoutMs = 500;

struct PreviewTaskContext {
    InternetRadio *app = nullptr;
    std::string title;
    std::string url;
    bool can_preview = false;
};

struct AsyncStatusContext {
    InternetRadio *app = nullptr;
    std::string text;
};

struct HttpMp3StreamContext {
    esp_http_client_handle_t client = nullptr;
};

BaseType_t create_preview_task(TaskFunction_t task, void *context, TaskHandle_t *handle)
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
    BaseType_t result = xTaskCreatePinnedToCoreWithCaps(
        task,
        "radio_preview",
        kPreviewTaskStackSize,
        context,
        kPreviewTaskPriority,
        handle,
        1,
        MALLOC_CAP_SPIRAM);
    if (result == pdPASS) {
        ESP_LOGI(TAG, "Created radio preview task with PSRAM-backed stack");
        return result;
    }

    ESP_LOGW(TAG, "Falling back to internal RAM for radio preview task stack");
#endif

    return xTaskCreatePinnedToCore(task, "radio_preview", kPreviewTaskStackSize, context,
                                   kPreviewTaskPriority, handle, 1);
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

    const int bytes_read = esp_http_client_read(context->client, reinterpret_cast<char *>(buffer), static_cast<int>(len));
    if (bytes_read > 0) {
        if (is_eof != nullptr) {
            *is_eof = false;
        }
        return bytes_read;
    }

    const bool complete = esp_http_client_is_complete_data_received(context->client);
    if (is_eof != nullptr) {
        *is_eof = complete;
    }

    if ((bytes_read < 0) && !complete) {
        ESP_LOGW(TAG, "HTTP stream read yielded %d, retrying", bytes_read);
    }

    return 0;
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

    delete context;
}

bool wait_for_audio_player_state(audio_player_state_t expected_state, TickType_t timeout)
{
    TickType_t start = xTaskGetTickCount();
    while (audio_player_get_state() != expected_state) {
        if ((xTaskGetTickCount() - start) >= timeout) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

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
    : ESP_Brookesia_PhoneApp("Radio Browser", &img_app_music_player, true),
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
    _previewStartInProgress(false)
{
}

bool InternetRadio::init(void)
{
    if (!buildUi()) {
        return false;
    }

    showRootMenu();
    return true;
}

bool InternetRadio::run(void)
{
    if (_screen == nullptr) {
        return false;
    }

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
    stopPreviewPlayback();
    return notifyCoreClosed();
}

bool InternetRadio::pause(void)
{
    stopPreviewPlayback();
    return true;
}

bool InternetRadio::resume(void)
{
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
    if (_statusLabel != nullptr) {
        lv_label_set_text(_statusLabel, text.c_str());
    }
}

void InternetRadio::setStatusFromTask(const std::string &text)
{
    auto *context = new AsyncStatusContext{this, text};
    if (context == nullptr) {
        ESP_LOGW(TAG, "Failed to allocate async status context");
        return;
    }

    if (lv_async_call(applyAsyncStatus, context) != LV_RES_OK) {
        delete context;
        ESP_LOGW(TAG, "Failed to queue async radio status update");
    }
}

void InternetRadio::applyAsyncStatus(void *context)
{
    auto *status_context = static_cast<AsyncStatusContext *>(context);
    if (status_context != nullptr) {
        if (status_context->app != nullptr) {
            status_context->app->setStatus(status_context->text);
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

void InternetRadio::playStationPreview(size_t index)
{
    if (index >= _entries.size()) {
        return;
    }

    const ListEntry &entry = _entries[index];
    if (entry.value.empty()) {
        setStatus("Station does not provide a playable stream URL.");
        return;
    }

    bool expected = false;
    if (!_previewStartInProgress.compare_exchange_strong(expected, true)) {
        setStatus("A station preview is already starting. Please wait.");
        return;
    }

    auto *task_context = new PreviewTaskContext{this, entry.title, entry.value, entry.canPreview};
    if (create_preview_task(previewPlaybackTask, task_context, &_previewTaskHandle) != pdPASS) {
        delete task_context;
        _previewTaskHandle = nullptr;
        _previewStartInProgress.store(false);
        setStatus("Failed to start station preview task.");
        return;
    }

    setStatus(std::string("Opening stream: ") + entry.title);
}

bool InternetRadio::startPreviewPlayback(const ListEntry &entry)
{
    if (entry.value.empty()) {
        setStatus("Station does not provide a stream URL.");
        return false;
    }

    if (audio_player_get_state() != AUDIO_PLAYER_STATE_IDLE) {
        audio_player_stop();
        if (!wait_for_audio_player_state(AUDIO_PLAYER_STATE_IDLE, kPreviewStopWait)) {
            setStatus("Audio player is still stopping. Please try again.");
            return false;
        }
    }

    auto *stream_context = new HttpMp3StreamContext();
    if (stream_context == nullptr) {
        setStatus("Unable to allocate stream context.");
        return false;
    }

    esp_http_client_config_t config = {};
    config.url = entry.value.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = kStreamTimeoutMs;
    config.disable_auto_redirect = false;
    configure_http_client_security(entry.value, config);

    stream_context->client = esp_http_client_init(&config);
    if (stream_context->client == nullptr) {
        delete stream_context;
        setStatus("Failed to open station stream.");
        return false;
    }

    if (esp_http_client_open(stream_context->client, 0) != ESP_OK) {
        http_mp3_stream_close(stream_context);
        setStatus("Failed to connect to station stream.");
        return false;
    }

    audio_player_stream_t stream = {
        .read_fn = http_mp3_stream_read,
        .close_fn = http_mp3_stream_close,
        .user_ctx = stream_context,
    };

    if (audio_player_play_mp3_stream(stream) != ESP_OK) {
        http_mp3_stream_close(stream_context);
        setStatus("Stream playback failed to start.");
        return false;
    }

    return true;
}

void InternetRadio::previewPlaybackTask(void *context)
{
    auto *task_context = static_cast<PreviewTaskContext *>(context);
    InternetRadio *app = (task_context != nullptr) ? task_context->app : nullptr;

    if (app == nullptr || task_context == nullptr) {
        delete task_context;
        vTaskDelete(nullptr);
        return;
    }

    ListEntry entry = {};
    entry.title = std::move(task_context->title);
    entry.value = std::move(task_context->url);
    entry.canPreview = task_context->can_preview;
    delete task_context;

    if (entry.canPreview) {
        app->setStatusFromTask(std::string("Starting stream: ") + entry.title);
    } else {
        app->setStatusFromTask(std::string("Trying stream: ") + entry.title + " (MP3 streams work best)");
    }

    const bool started = app->startPreviewPlayback(entry);
    if (started) {
        app->setStatusFromTask(std::string("Playing: ") + entry.title);
    } else if (!entry.canPreview) {
        app->setStatusFromTask(std::string("Playback failed for ") + entry.title + ". This stream may use an unsupported codec.");
    } else {
        app->setStatusFromTask(std::string("Failed to start streaming ") + entry.title + ". Try another station.");
    }

    app->_previewTaskHandle = nullptr;
    app->_previewStartInProgress.store(false);
    vTaskDelete(nullptr);
}

void InternetRadio::stopPreviewPlayback(void)
{
    _previewStartInProgress.store(false);

    if (audio_player_get_state() != AUDIO_PLAYER_STATE_IDLE) {
        if (audio_player_stop() != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop audio player before releasing preview buffer");
            return;
        }

        if (!wait_for_audio_player_state(AUDIO_PLAYER_STATE_IDLE, kPreviewStopWait)) {
            ESP_LOGW(TAG, "Timed out waiting for audio player to go idle");
            return;
        }
    }
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
        playStationPreview(index);
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