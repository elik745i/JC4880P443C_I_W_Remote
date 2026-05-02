#include "P4YouTube.hpp"

#include <memory>

#include "cJSON.h"
#include "esp_log.h"

namespace {

static constexpr const char *TAG = "P4YouTube";
static constexpr const char *kYoutubeCacheDir = "/sdcard/.jc4880_youtube_cache";
static constexpr size_t kSdThresholdBytes = 8192;
static constexpr const char *kInvidiousInstances[] = {
    "https://inv.nadeko.net",
    "https://yewtu.be",
    "https://vid.puffyan.us",
};

struct WorkerContext {
    P4YouTube *app = nullptr;
    P4YouTube::WorkerAction action = P4YouTube::WorkerAction::Search;
    size_t index = 0;
    std::string query;
    std::string title;
    std::string video_id;
};

std::string get_json_string(cJSON *object, const char *field)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, field);
    return cJSON_IsString(item) && (item->valuestring != nullptr) ? item->valuestring : "";
}

long long get_json_integer(cJSON *object, const char *field)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, field);
    if (cJSON_IsNumber(item)) {
        return static_cast<long long>(item->valuedouble);
    }
    return 0;
}

bool fetch_invidious(const std::string &path, app_network_cache::CachedPayload &payload, std::string &error,
                    std::string *used_base = nullptr, bool prefer_cached_sd = false)
{
    for (const char *base: kInvidiousInstances) {
        const std::string url = std::string(base) + path;
        if (app_network_cache::fetch_text_with_sd_fallback(url, kYoutubeCacheDir, url, kSdThresholdBytes, payload, error,
                                                           prefer_cached_sd, 18000)) {
            if (used_base != nullptr) {
                *used_base = base;
            }
            return true;
        }
    }
    return false;
}

P4YouTube::VideoResultList parse_video_results(const std::string &json)
{
    P4YouTube::VideoResultList results;
    cJSON *root = cJSON_Parse(json.c_str());
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return results;
    }

    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root)
    {
        if (!cJSON_IsObject(item)) {
            continue;
        }
        const std::string type = get_json_string(item, "type");
        if (type != "video") {
            continue;
        }

        P4YouTube::VideoResult result;
        result.title = get_json_string(item, "title").c_str();
        result.video_id = get_json_string(item, "videoId").c_str();
        result.author = get_json_string(item, "author").c_str();

        std::string detail;
        const long long length_seconds = get_json_integer(item, "lengthSeconds");
        const long long views = get_json_integer(item, "viewCount");
        if (length_seconds > 0) {
            detail += std::to_string(length_seconds / 60);
            detail += "m ";
            detail += std::to_string(length_seconds % 60);
            detail += "s";
        }
        if (views > 0) {
            if (!detail.empty()) {
                detail += " | ";
            }
            detail += std::to_string(views);
            detail += " views";
        }
        result.detail = detail.c_str();
        if (!result.title.empty() && !result.video_id.empty()) {
            results.push_back(std::move(result));
        }
        if (results.size() >= 12) {
            break;
        }
    }

    cJSON_Delete(root);
    return results;
}

std::string parse_video_detail_body(const std::string &json, const std::string &title)
{
    cJSON *root = cJSON_Parse(json.c_str());
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return {};
    }

    std::string body;
    const std::string author = get_json_string(root, "author");
    const std::string description = get_json_string(root, "description");
    const long long views = get_json_integer(root, "viewCount");
    const long long likes = get_json_integer(root, "likeCount");

    body += title;
    body += "\n\n";
    if (!author.empty()) {
        body += "Channel: ";
        body += author;
        body += "\n";
    }
    if (views > 0) {
        body += "Views: ";
        body += std::to_string(views);
        body += "\n";
    }
    if (likes > 0) {
        body += "Likes: ";
        body += std::to_string(likes);
        body += "\n";
    }
    body += "\n";
    body += description.empty() ? "No description returned by the Invidious API." : description;
    if (body.size() > 12000) {
        body.resize(12000);
        body += "\n\n[Truncated for device view]";
    }

    cJSON_Delete(root);
    return body;
}

} // namespace

P4YouTube::P4YouTube():
    ESP_Brookesia_PhoneApp("YouTube", nullptr, true),
    _screen(nullptr),
    _statusLabel(nullptr),
    _searchArea(nullptr),
    _keyboard(nullptr),
    _resultsPanel(nullptr),
    _resultsList(nullptr),
    _detailPanel(nullptr),
    _detailTitle(nullptr),
    _detailMeta(nullptr),
    _detailBody(nullptr),
    _requestInFlight(false)
{
}

bool P4YouTube::init()
{
    return true;
}

bool P4YouTube::run()
{
    if (!ensureUiReady()) {
        return false;
    }
    showResultList();
    setStatus("Search YouTube metadata through public Invidious mirrors. Large responses cache on SD card when mounted.");
    return true;
}

bool P4YouTube::pause()
{
    return true;
}

bool P4YouTube::resume()
{
    return ensureUiReady();
}

bool P4YouTube::back()
{
    if ((_detailPanel != nullptr) && !lv_obj_has_flag(_detailPanel, LV_OBJ_FLAG_HIDDEN)) {
        showResultList();
        return true;
    }
    return notifyCoreClosed();
}

bool P4YouTube::close()
{
    _results.clear();
    _buttonIndexMap.clear();
    return true;
}

bool P4YouTube::buildUi()
{
    _screen = lv_scr_act();
    if (_screen == nullptr) {
        return false;
    }

    lv_obj_set_style_bg_color(_screen, lv_color_hex(0xFEF2F2), 0);
    lv_obj_set_style_bg_grad_color(_screen, lv_color_hex(0xFFF7ED), 0);
    lv_obj_set_style_bg_grad_dir(_screen, LV_GRAD_DIR_VER, 0);

    lv_obj_t *title = lv_label_create(_screen);
    lv_label_set_text(title, "YouTube");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x7F1D1D), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 18, 16);

    _statusLabel = lv_label_create(_screen);
    lv_obj_set_width(_statusLabel, 444);
    lv_label_set_long_mode(_statusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_statusLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_statusLabel, lv_color_hex(0x7C2D12), 0);
    lv_obj_align(_statusLabel, LV_ALIGN_TOP_LEFT, 18, 58);

    _searchArea = lv_textarea_create(_screen);
    lv_obj_set_size(_searchArea, 316, 54);
    lv_obj_align(_searchArea, LV_ALIGN_TOP_LEFT, 18, 98);
    lv_textarea_set_one_line(_searchArea, true);
    lv_textarea_set_placeholder_text(_searchArea, "Search YouTube");
    lv_obj_add_event_cb(_searchArea, onSearchFocus, LV_EVENT_FOCUSED, this);

    lv_obj_t *searchButton = lv_btn_create(_screen);
    lv_obj_set_size(searchButton, 120, 54);
    lv_obj_align(searchButton, LV_ALIGN_TOP_RIGHT, -18, 98);
    lv_obj_set_style_bg_color(searchButton, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_border_width(searchButton, 0, 0);
    lv_obj_add_event_cb(searchButton, onSearchClicked, LV_EVENT_CLICKED, this);

    lv_obj_t *searchLabel = lv_label_create(searchButton);
    lv_label_set_text(searchLabel, LV_SYMBOL_VIDEO " Search");
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
    lv_obj_set_style_bg_color(backButton, lv_color_hex(0xFECACA), 0);
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
    lv_obj_set_style_text_color(_detailMeta, lv_color_hex(0x7F1D1D), 0);
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

bool P4YouTube::ensureUiReady()
{
    if (hasLiveScreen()) {
        return true;
    }
    resetUiPointers();
    return buildUi();
}

bool P4YouTube::hasLiveScreen() const
{
    return (_screen != nullptr) && lv_obj_is_valid(_screen) && (_searchArea != nullptr) && lv_obj_is_valid(_searchArea);
}

void P4YouTube::startSearch()
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
    setStatus("Searching public YouTube mirrors...");

    WorkerContext *context = new WorkerContext();
    context->app = this;
    context->action = WorkerAction::Search;
    context->query = query;
    if (xTaskCreate(workerTask, "p4_yt_search", 10240, context, 5, nullptr) != pdPASS) {
        delete context;
        _requestInFlight.store(false);
        setStatus("Failed to start YouTube search.");
    }
}

void P4YouTube::startOpenVideo(size_t index)
{
    if (index >= _results.size()) {
        return;
    }
    if (_requestInFlight.exchange(true)) {
        setStatus("Wait for the current request to finish.");
        return;
    }

    setStatus("Loading video detail...");
    WorkerContext *context = new WorkerContext();
    context->app = this;
    context->action = WorkerAction::Detail;
    context->index = index;
    context->title = _results[index].title.c_str();
    context->video_id = _results[index].video_id.c_str();
    if (xTaskCreate(workerTask, "p4_yt_video", 10240, context, 5, nullptr) != pdPASS) {
        delete context;
        _requestInFlight.store(false);
        setStatus("Failed to start video detail fetch.");
    }
}

void P4YouTube::renderResults()
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
        std::string text = _results[index].title.c_str();
        if (!_results[index].author.empty()) {
            text += "\n";
            text += _results[index].author.c_str();
        }
        if (!_results[index].detail.empty()) {
            text += "\n";
            text += _results[index].detail.c_str();
        }
        lv_obj_t *button = lv_list_add_btn(_resultsList, LV_SYMBOL_VIDEO, text.c_str());
        lv_obj_add_event_cb(button, onResultClicked, LV_EVENT_CLICKED, this);
        _buttonIndexMap[button] = index;
    }
}

void P4YouTube::showResultList()
{
    if (_resultsPanel != nullptr) {
        lv_obj_clear_flag(_resultsPanel, LV_OBJ_FLAG_HIDDEN);
    }
    if (_detailPanel != nullptr) {
        lv_obj_add_flag(_detailPanel, LV_OBJ_FLAG_HIDDEN);
    }
}

void P4YouTube::showVideo(const std::string &title, const std::string &video_id, const std::string &body,
                          app_network_cache::PayloadStorage storage)
{
    lv_label_set_text(_detailTitle, title.c_str());
    std::string meta = std::string("https://youtube.com/watch?v=") + video_id + "\nLoaded from " +
                       app_network_cache::storage_label(storage);
    lv_label_set_text(_detailMeta, meta.c_str());
    lv_textarea_set_text(_detailBody, body.c_str());
    lv_obj_add_flag(_resultsPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_detailPanel, LV_OBJ_FLAG_HIDDEN);
}

void P4YouTube::setStatus(const std::string &status)
{
    if ((_statusLabel != nullptr) && lv_obj_is_valid(_statusLabel)) {
        lv_label_set_text(_statusLabel, status.c_str());
    }
}

void P4YouTube::resetUiPointers()
{
    _screen = nullptr;
    _statusLabel = nullptr;
    _searchArea = nullptr;
    _keyboard = nullptr;
    _resultsPanel = nullptr;
    _resultsList = nullptr;
    _detailPanel = nullptr;
    _detailTitle = nullptr;
    _detailMeta = nullptr;
    _detailBody = nullptr;
}

void P4YouTube::onSearchClicked(lv_event_t *event)
{
    P4YouTube *app = static_cast<P4YouTube *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->startSearch();
    }
}

void P4YouTube::onSearchFocus(lv_event_t *event)
{
    P4YouTube *app = static_cast<P4YouTube *>(lv_event_get_user_data(event));
    if ((app != nullptr) && (app->_keyboard != nullptr)) {
        lv_obj_clear_flag(app->_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

void P4YouTube::onKeyboardReady(lv_event_t *event)
{
    P4YouTube *app = static_cast<P4YouTube *>(lv_event_get_user_data(event));
    if ((app != nullptr) && (app->_keyboard != nullptr)) {
        lv_obj_add_flag(app->_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

void P4YouTube::onResultClicked(lv_event_t *event)
{
    P4YouTube *app = static_cast<P4YouTube *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    const auto found = app->_buttonIndexMap.find(lv_event_get_target(event));
    if (found != app->_buttonIndexMap.end()) {
        app->startOpenVideo(found->second);
    }
}

void P4YouTube::onBackToResultsClicked(lv_event_t *event)
{
    P4YouTube *app = static_cast<P4YouTube *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->showResultList();
    }
}

void P4YouTube::workerTask(void *context_ptr)
{
    std::unique_ptr<WorkerContext> context(static_cast<WorkerContext *>(context_ptr));
    std::unique_ptr<WorkerResult> result(new WorkerResult());
    result->app = context->app;
    result->action = context->action;

    if (context->action == WorkerAction::Search) {
        const std::string path = "/api/v1/search?q=" + app_network_cache::percent_encode_component(context->query) +
                                 "&type=video&page=1&sort_by=relevance";
        app_network_cache::CachedPayload payload;
        if (!fetch_invidious(path, payload, result->status)) {
            result->success = false;
        } else {
            std::string json;
            result->success = app_network_cache::load_cached_text(payload, json, 64000);
            if (!result->success) {
                result->status = "Failed to read YouTube search response";
            } else {
                result->results = parse_video_results(json);
                result->success = !result->results.empty();
                result->status = result->success ? ("Found " + std::to_string(result->results.size()) + " videos")
                                                 : "No video results returned by the public mirrors";
            }
        }
    } else {
        const std::string path = "/api/v1/videos/" + context->video_id;
        app_network_cache::CachedPayload payload;
        if (!fetch_invidious(path, payload, result->status, nullptr, true)) {
            result->success = false;
        } else {
            std::string json;
            result->success = app_network_cache::load_cached_text(payload, json, 56000);
            if (!result->success) {
                result->status = "Failed to read video detail";
            } else {
                result->title = context->title;
                result->video_id = context->video_id;
                result->body = parse_video_detail_body(json, context->title);
                result->payload = std::move(payload);
                result->success = !result->body.empty();
                result->status = result->success ? "Video detail loaded" : "Video detail response was empty";
            }
        }
    }

    lv_async_call(applyWorkerResult, result.release());
    vTaskDelete(nullptr);
}

void P4YouTube::applyWorkerResult(void *context)
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

    result->app->showVideo(result->title, result->video_id, result->body, result->payload.storage);
}