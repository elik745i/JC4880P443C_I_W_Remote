#include "P4Browser.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "esp_log.h"

namespace {

static constexpr const char *TAG = "P4Browser";
static constexpr const char *kSearchUrlPrefix = "https://html.duckduckgo.com/html/?q=";
static constexpr const char *kSdCacheDir = "/sdcard/.jc4880_browser_cache";
static constexpr size_t kSdThresholdBytes = 8192;

struct WorkerContext {
    P4Browser *app = nullptr;
    P4Browser::WorkerAction action = P4Browser::WorkerAction::Search;
    size_t index = 0;
    std::string query;
    std::string title;
    std::string url;
};

std::string trim_copy(const std::string &value)
{
    size_t start = 0;
    while ((start < value.size()) && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    size_t end = value.size();
    while ((end > start) && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string decode_html_entities(const std::string &input)
{
    std::string output;
    output.reserve(input.size());
    for (size_t index = 0; index < input.size(); ++index) {
        if (input.compare(index, 5, "&amp;") == 0) {
            output.push_back('&');
            index += 4;
        } else if (input.compare(index, 4, "&lt;") == 0) {
            output.push_back('<');
            index += 3;
        } else if (input.compare(index, 4, "&gt;") == 0) {
            output.push_back('>');
            index += 3;
        } else if (input.compare(index, 6, "&quot;") == 0) {
            output.push_back('"');
            index += 5;
        } else if (input.compare(index, 5, "&#39;") == 0) {
            output.push_back('\'');
            index += 4;
        } else if (input.compare(index, 6, "&nbsp;") == 0) {
            output.push_back(' ');
            index += 5;
        } else {
            output.push_back(input[index]);
        }
    }
    return output;
}

std::string strip_html(const std::string &html)
{
    std::string output;
    output.reserve(html.size());
    bool in_tag = false;
    for (size_t index = 0; index < html.size(); ++index) {
        const char ch = html[index];
        if (ch == '<') {
            in_tag = true;
            if ((index + 3) < html.size()) {
                const bool block_break = (html.compare(index, 4, "<br>") == 0) || (html.compare(index, 5, "<br/>") == 0) ||
                                         (html.compare(index, 3, "<p>") == 0) || (html.compare(index, 5, "</p>") == 0) ||
                                         (html.compare(index, 4, "<li>") == 0) || (html.compare(index, 5, "</li>") == 0) ||
                                         (html.compare(index, 5, "<div>") == 0) || (html.compare(index, 6, "</div>") == 0);
                if (block_break && !output.empty() && (output.back() != '\n')) {
                    output.push_back('\n');
                }
            }
            continue;
        }
        if (ch == '>') {
            in_tag = false;
            continue;
        }
        if (!in_tag) {
            output.push_back(ch);
        }
    }

    output = decode_html_entities(output);
    std::string normalized;
    normalized.reserve(output.size());
    bool previous_space = false;
    for (char ch: output) {
        if ((ch == '\r') || (ch == '\t')) {
            ch = ' ';
        }
        if (ch == '\n') {
            if (!normalized.empty() && (normalized.back() != '\n')) {
                normalized.push_back('\n');
            }
            previous_space = false;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            if (!previous_space) {
                normalized.push_back(' ');
                previous_space = true;
            }
            continue;
        }
        previous_space = false;
        normalized.push_back(ch);
    }
    return trim_copy(normalized);
}

std::string extract_between(const std::string &text, size_t start_pos, const char *begin_marker, const char *end_marker)
{
    const size_t begin = text.find(begin_marker, start_pos);
    if (begin == std::string::npos) {
        return {};
    }
    const size_t value_start = begin + std::strlen(begin_marker);
    const size_t end = text.find(end_marker, value_start);
    if (end == std::string::npos) {
        return {};
    }
    return text.substr(value_start, end - value_start);
}

std::string decode_duckduckgo_result_url(const std::string &href)
{
    const size_t uddg_pos = href.find("uddg=");
    if (uddg_pos == std::string::npos) {
        return href;
    }
    size_t value_start = uddg_pos + 5;
    size_t value_end = href.find('&', value_start);
    if (value_end == std::string::npos) {
        value_end = href.size();
    }
    return app_network_cache::percent_decode_component(href.substr(value_start, value_end - value_start));
}

P4Browser::SearchResultList parse_results(const std::string &html)
{
    P4Browser::SearchResultList results;
    size_t cursor = 0;
    while (results.size() < 10) {
        const size_t link_pos = html.find("result__a", cursor);
        if (link_pos == std::string::npos) {
            break;
        }

        const size_t href_pos = html.rfind("href=\"", link_pos);
        if (href_pos == std::string::npos) {
            cursor = link_pos + 8;
            continue;
        }

        const size_t href_start = href_pos + 6;
        const size_t href_end = html.find('"', href_start);
        const size_t title_start = html.find('>', link_pos);
        const size_t title_end = html.find("</a>", title_start == std::string::npos ? link_pos : title_start);
        if ((href_end == std::string::npos) || (title_start == std::string::npos) || (title_end == std::string::npos)) {
            cursor = link_pos + 8;
            continue;
        }

        P4Browser::SearchResult result;
        result.url = decode_duckduckgo_result_url(html.substr(href_start, href_end - href_start)).c_str();
        result.title = decode_html_entities(strip_html(html.substr(title_start + 1, title_end - title_start - 1))).c_str();

        const size_t snippet_marker = html.find("result__snippet", title_end);
        if ((snippet_marker != std::string::npos) && (snippet_marker < html.find("result__a", title_end + 4))) {
            const size_t snippet_start = html.find('>', snippet_marker);
            const size_t snippet_end = html.find("</a>", snippet_start == std::string::npos ? snippet_marker : snippet_start);
            if ((snippet_start != std::string::npos) && (snippet_end != std::string::npos)) {
                result.snippet = decode_html_entities(strip_html(html.substr(snippet_start + 1, snippet_end - snippet_start - 1))).c_str();
            }
        }

        if (!result.title.empty() && !result.url.empty()) {
            results.push_back(std::move(result));
        }
        cursor = title_end + 4;
    }
    return results;
}

} // namespace

P4Browser::P4Browser():
    ESP_Brookesia_PhoneApp("Browser", nullptr, true),
    _screen(nullptr),
    _statusLabel(nullptr),
    _searchArea(nullptr),
    _keyboard(nullptr),
    _resultsList(nullptr),
    _detailPanel(nullptr),
    _detailTitle(nullptr),
    _detailMeta(nullptr),
    _detailBody(nullptr),
    _resultsPanel(nullptr),
    _requestInFlight(false)
{
}

bool P4Browser::init()
{
    return true;
}

bool P4Browser::run()
{
    if (!ensureUiReady()) {
        return false;
    }
    showResultList();
    setStatus("Search the web. Heavy pages cache on SD card when available, otherwise in PSRAM.");
    return true;
}

bool P4Browser::pause()
{
    return true;
}

bool P4Browser::resume()
{
    if (!ensureUiReady()) {
        return false;
    }
    return true;
}

bool P4Browser::back()
{
    if ((_detailPanel != nullptr) && !lv_obj_has_flag(_detailPanel, LV_OBJ_FLAG_HIDDEN)) {
        showResultList();
        return true;
    }
    return notifyCoreClosed();
}

bool P4Browser::close()
{
    _results.clear();
    _buttonIndexMap.clear();
    return true;
}

bool P4Browser::buildUi()
{
    _screen = lv_scr_act();
    if (_screen == nullptr) {
        return false;
    }

    lv_obj_set_style_bg_color(_screen, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_grad_color(_screen, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_bg_grad_dir(_screen, LV_GRAD_DIR_VER, 0);

    lv_obj_t *title = lv_label_create(_screen);
    lv_label_set_text(title, "Browser");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0F172A), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 18, 16);

    _statusLabel = lv_label_create(_screen);
    lv_obj_set_width(_statusLabel, 444);
    lv_label_set_long_mode(_statusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_statusLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_statusLabel, lv_color_hex(0x334155), 0);
    lv_obj_align(_statusLabel, LV_ALIGN_TOP_LEFT, 18, 58);

    _searchArea = lv_textarea_create(_screen);
    lv_obj_set_size(_searchArea, 316, 54);
    lv_obj_align(_searchArea, LV_ALIGN_TOP_LEFT, 18, 98);
    lv_textarea_set_one_line(_searchArea, true);
    lv_textarea_set_placeholder_text(_searchArea, "Search the web");
    lv_obj_add_event_cb(_searchArea, onSearchFocus, LV_EVENT_FOCUSED, this);

    lv_obj_t *searchButton = lv_btn_create(_screen);
    lv_obj_set_size(searchButton, 120, 54);
    lv_obj_align(searchButton, LV_ALIGN_TOP_RIGHT, -18, 98);
    lv_obj_set_style_bg_color(searchButton, lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_border_width(searchButton, 0, 0);
    lv_obj_add_event_cb(searchButton, onSearchClicked, LV_EVENT_CLICKED, this);

    lv_obj_t *searchLabel = lv_label_create(searchButton);
    lv_label_set_text(searchLabel, "Search");
    lv_obj_set_style_text_color(searchLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(searchLabel);

    _resultsPanel = lv_obj_create(_screen);
    lv_obj_set_size(_resultsPanel, 444, 370);
    lv_obj_align(_resultsPanel, LV_ALIGN_TOP_LEFT, 18, 164);
    lv_obj_set_style_radius(_resultsPanel, 24, 0);
    lv_obj_set_style_border_width(_resultsPanel, 0, 0);
    lv_obj_set_style_pad_all(_resultsPanel, 12, 0);

    _resultsList = lv_list_create(_resultsPanel);
    lv_obj_set_size(_resultsList, 420, 344);
    lv_obj_center(_resultsList);

    _detailPanel = lv_obj_create(_screen);
    lv_obj_set_size(_detailPanel, 444, 370);
    lv_obj_align(_detailPanel, LV_ALIGN_TOP_LEFT, 18, 164);
    lv_obj_set_style_radius(_detailPanel, 24, 0);
    lv_obj_set_style_border_width(_detailPanel, 0, 0);
    lv_obj_set_style_pad_all(_detailPanel, 14, 0);
    lv_obj_add_flag(_detailPanel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *backButton = lv_btn_create(_detailPanel);
    lv_obj_set_size(backButton, 128, 42);
    lv_obj_align(backButton, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(backButton, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_border_width(backButton, 0, 0);
    lv_obj_add_event_cb(backButton, onBackToResultsClicked, LV_EVENT_CLICKED, this);

    lv_obj_t *backLabel = lv_label_create(backButton);
    lv_label_set_text(backLabel, LV_SYMBOL_LEFT " Results");
    lv_obj_center(backLabel);

    _detailTitle = lv_label_create(_detailPanel);
    lv_obj_set_width(_detailTitle, 292);
    lv_label_set_long_mode(_detailTitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_detailTitle, &lv_font_montserrat_18, 0);
    lv_obj_align(_detailTitle, LV_ALIGN_TOP_LEFT, 136, 0);

    _detailMeta = lv_label_create(_detailPanel);
    lv_obj_set_width(_detailMeta, 416);
    lv_label_set_long_mode(_detailMeta, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_detailMeta, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_detailMeta, lv_color_hex(0x475569), 0);
    lv_obj_align(_detailMeta, LV_ALIGN_TOP_LEFT, 0, 54);

    _detailBody = lv_textarea_create(_detailPanel);
    lv_obj_set_size(_detailBody, 416, 278);
    lv_obj_align(_detailBody, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_textarea_set_one_line(_detailBody, false);
    lv_textarea_set_text(_detailBody, "");
    lv_obj_clear_flag(_detailBody, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    _keyboard = lv_keyboard_create(_screen);
    lv_obj_set_size(_keyboard, 480, 210);
    lv_obj_align(_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(_keyboard, _searchArea);
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_keyboard, onKeyboardReady, LV_EVENT_READY, this);
    lv_obj_add_event_cb(_keyboard, onKeyboardReady, LV_EVENT_CANCEL, this);
    return true;
}

bool P4Browser::ensureUiReady()
{
    if (hasLiveScreen()) {
        return true;
    }
    resetUiPointers();
    return buildUi();
}

bool P4Browser::hasLiveScreen() const
{
    return (_screen != nullptr) && lv_obj_is_valid(_screen) && (_searchArea != nullptr) && lv_obj_is_valid(_searchArea);
}

void P4Browser::startSearch()
{
    if (_requestInFlight.exchange(true)) {
        setStatus("Wait for the current request to finish.");
        return;
    }

    const char *query = lv_textarea_get_text(_searchArea);
    if ((query == nullptr) || (query[0] == '\0')) {
        _requestInFlight.store(false);
        setStatus("Enter a query first.");
        return;
    }

    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    setStatus("Searching DuckDuckGo Lite...");

    WorkerContext *context = new WorkerContext();
    context->app = this;
    context->action = WorkerAction::Search;
    context->query = query;
    if (xTaskCreate(workerTask, "p4_browser", 8192, context, 5, nullptr) != pdPASS) {
        delete context;
        _requestInFlight.store(false);
        setStatus("Failed to start browser request.");
    }
}

void P4Browser::startOpenResult(size_t index)
{
    if (index >= _results.size()) {
        return;
    }
    if (_requestInFlight.exchange(true)) {
        setStatus("Wait for the current request to finish.");
        return;
    }

    setStatus("Loading page text...");
    WorkerContext *context = new WorkerContext();
    context->app = this;
    context->action = WorkerAction::Open;
    context->index = index;
    context->title = _results[index].title.c_str();
    context->url = _results[index].url.c_str();
    if (xTaskCreate(workerTask, "p4_page", 10240, context, 5, nullptr) != pdPASS) {
        delete context;
        _requestInFlight.store(false);
        setStatus("Failed to start page fetch.");
    }
}

void P4Browser::renderResults()
{
    if ((_resultsList == nullptr) || !lv_obj_is_valid(_resultsList)) {
        return;
    }

    _buttonIndexMap.clear();
    lv_obj_clean(_resultsList);
    if (_results.empty()) {
        lv_obj_t *label = lv_label_create(_resultsList);
        lv_label_set_text(label, "No results yet. Search above.");
        return;
    }

    for (size_t index = 0; index < _results.size(); ++index) {
        std::string text = std::to_string(index + 1) + ". " + _results[index].title.c_str();
        if (!_results[index].snippet.empty()) {
            text += "\n";
            text += _results[index].snippet.c_str();
        }
        lv_obj_t *button = lv_list_add_btn(_resultsList, LV_SYMBOL_DIRECTORY, text.c_str());
        lv_obj_add_event_cb(button, onResultClicked, LV_EVENT_CLICKED, this);
        _buttonIndexMap[button] = index;
    }
}

void P4Browser::showResultList()
{
    if (_resultsPanel != nullptr) {
        lv_obj_clear_flag(_resultsPanel, LV_OBJ_FLAG_HIDDEN);
    }
    if (_detailPanel != nullptr) {
        lv_obj_add_flag(_detailPanel, LV_OBJ_FLAG_HIDDEN);
    }
}

void P4Browser::showPage(const std::string &title, const std::string &url, const std::string &body,
                         app_network_cache::PayloadStorage storage)
{
    if ((_detailPanel == nullptr) || (_detailTitle == nullptr) || (_detailBody == nullptr) || (_detailMeta == nullptr)) {
        return;
    }

    lv_label_set_text(_detailTitle, title.c_str());
    std::string meta = url + "\nLoaded from " + app_network_cache::storage_label(storage);
    lv_label_set_text(_detailMeta, meta.c_str());
    lv_textarea_set_text(_detailBody, body.c_str());
    lv_obj_add_flag(_resultsPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_detailPanel, LV_OBJ_FLAG_HIDDEN);
}

void P4Browser::setStatus(const std::string &status)
{
    if ((_statusLabel != nullptr) && lv_obj_is_valid(_statusLabel)) {
        lv_label_set_text(_statusLabel, status.c_str());
    }
}

void P4Browser::resetUiPointers()
{
    _screen = nullptr;
    _statusLabel = nullptr;
    _searchArea = nullptr;
    _keyboard = nullptr;
    _resultsList = nullptr;
    _detailPanel = nullptr;
    _detailTitle = nullptr;
    _detailMeta = nullptr;
    _detailBody = nullptr;
    _resultsPanel = nullptr;
}

void P4Browser::onScreenDeleted(lv_event_t *event)
{
    P4Browser *app = static_cast<P4Browser *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->resetUiPointers();
    }
}

void P4Browser::onSearchClicked(lv_event_t *event)
{
    P4Browser *app = static_cast<P4Browser *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->startSearch();
    }
}

void P4Browser::onSearchFocus(lv_event_t *event)
{
    P4Browser *app = static_cast<P4Browser *>(lv_event_get_user_data(event));
    if ((app != nullptr) && (app->_keyboard != nullptr)) {
        lv_obj_clear_flag(app->_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

void P4Browser::onKeyboardReady(lv_event_t *event)
{
    P4Browser *app = static_cast<P4Browser *>(lv_event_get_user_data(event));
    if ((app != nullptr) && (app->_keyboard != nullptr)) {
        lv_obj_add_flag(app->_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

void P4Browser::onResultClicked(lv_event_t *event)
{
    P4Browser *app = static_cast<P4Browser *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    lv_obj_t *target = lv_event_get_target(event);
    const auto found = app->_buttonIndexMap.find(target);
    if (found != app->_buttonIndexMap.end()) {
        app->startOpenResult(found->second);
    }
}

void P4Browser::onBackToResultsClicked(lv_event_t *event)
{
    P4Browser *app = static_cast<P4Browser *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->showResultList();
    }
}

void P4Browser::workerTask(void *context_ptr)
{
    std::unique_ptr<WorkerContext> context(static_cast<WorkerContext *>(context_ptr));
    std::unique_ptr<WorkerResult> result(new WorkerResult());
    result->app = context->app;
    result->action = context->action;

    if (context->action == WorkerAction::Search) {
        const std::string url = std::string(kSearchUrlPrefix) + app_network_cache::percent_encode_component(context->query);
        app_network_cache::CachedPayload payload;
        if (!app_network_cache::fetch_text_with_sd_fallback(url, kSdCacheDir, url, kSdThresholdBytes, payload,
                                                            result->status, false)) {
            result->success = false;
        } else {
            std::string html;
            result->success = app_network_cache::load_cached_text(payload, html, 64000);
            if (!result->success) {
                result->status = "Failed to load search response";
            } else {
                result->results = parse_results(html);
                result->success = !result->results.empty();
                result->status = result->success ? ("Found " + std::to_string(result->results.size()) + " results")
                                                 : "No results parsed from DuckDuckGo Lite";
            }
        }
    } else {
        app_network_cache::CachedPayload payload;
        if (!app_network_cache::fetch_text_with_sd_fallback(context->url, kSdCacheDir, context->url, kSdThresholdBytes,
                                                            payload, result->status, true, 20000)) {
            result->success = false;
        } else {
            std::string html;
            result->success = app_network_cache::load_cached_text(payload, html, 56000);
            if (!result->success) {
                result->status = "Failed to load page response";
            } else {
                result->title = context->title;
                result->url = context->url;
                result->body = strip_html(html);
                if (result->body.size() > 12000) {
                    result->body.resize(12000);
                    result->body += "\n\n[Truncated for device view]";
                }
                result->payload = std::move(payload);
                result->success = !result->body.empty();
                result->status = result->success ? "Page loaded" : "The fetched page did not contain readable text";
            }
        }
    }

    lv_async_call(applyWorkerResult, result.release());
    vTaskDelete(nullptr);
}

void P4Browser::applyWorkerResult(void *context)
{
    std::unique_ptr<WorkerResult> result(static_cast<WorkerResult *>(context));
    if ((result->app == nullptr) || !result->app->hasLiveScreen()) {
        return;
    }

    result->app->_requestInFlight.store(false);
    result->app->setStatus(result->status);
    if (!result->success) {
        return;
    }

    if (result->action == WorkerAction::Search) {
        result->app->_results = std::move(result->results);
        result->app->renderResults();
        result->app->showResultList();
        return;
    }

    result->app->showPage(result->title, result->url, result->body, result->payload.storage);
}