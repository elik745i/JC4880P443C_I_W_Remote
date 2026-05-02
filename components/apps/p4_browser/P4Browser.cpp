#include "P4Browser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>

#include "cJSON.h"
#include "esp_log.h"

LV_IMG_DECLARE(img_app_browser);

namespace {

static constexpr const char *TAG = "P4Browser";
static constexpr const char *kSearchUrlPrefix = "https://html.duckduckgo.com/html/?q=";
static constexpr const char *kSdCacheDir = "/sdcard/.jc4880_browser_cache";
static constexpr const char *kBookmarksPath = "/sdcard/.jc4880_browser_cache/bookmarks.json";
static constexpr size_t kSdThresholdBytes = 8192;
static constexpr const char *kDefaultHomeTitle = "Google";
static constexpr const char *kDefaultHomeUrl = "https://www.google.com/";
static constexpr const char *kMobileBrowserUserAgent =
    "Mozilla/5.0 (Linux; Android 14; JC4880P4Remote) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0.0.0 Mobile Safari/537.36";
static constexpr const char *kHtmlAcceptHeader =
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8";

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

bool is_name_char(char ch)
{
    return (std::isalnum(static_cast<unsigned char>(ch)) != 0) || (ch == '-') || (ch == '_') || (ch == ':');
}

bool starts_with(const std::string &value, const char *prefix)
{
    return value.rfind(prefix, 0) == 0;
}

std::string lower_ascii_copy(const std::string &value)
{
    std::string output = value;
    for (char &ch: output) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return output;
}

bool is_block_tag(const std::string &name)
{
    static constexpr const char *kBlockTags[] = {
        "article", "aside",  "blockquote", "br",    "dd",    "div",   "dl",     "dt",   "figcaption",
        "figure",  "footer", "form",       "h1",    "h2",    "h3",    "h4",     "h5",   "h6",
        "header",  "hr",     "li",         "main",  "nav",   "ol",    "p",      "pre",  "section",
        "table",   "tbody",  "td",         "tfoot", "th",    "thead", "tr",     "ul"};
    return std::find(std::begin(kBlockTags), std::end(kBlockTags), name) != std::end(kBlockTags);
}

void append_spacing(std::string &output, char ch)
{
    if (output.empty()) {
        return;
    }

    if (ch == '\n') {
        if (output.back() != '\n') {
            output.push_back('\n');
        }
        return;
    }

    if ((output.back() != ' ') && (output.back() != '\n')) {
        output.push_back(' ');
    }
}

std::string html_to_reader_text(const std::string &html)
{
    std::string output;
    output.reserve(html.size());
    bool in_tag = false;
    std::string skip_tag_name;
    size_t index = 0;
    while (index < html.size()) {
        const char ch = html[index];
        if (ch == '<') {
            if ((index + 3) < html.size() && (html.compare(index, 4, "<!--") == 0)) {
                const size_t comment_end = html.find("-->", index + 4);
                if (comment_end == std::string::npos) {
                    break;
                }
                index = comment_end + 3;
                continue;
            }

            in_tag = false;
            size_t cursor = index + 1;
            bool closing = false;
            while ((cursor < html.size()) && std::isspace(static_cast<unsigned char>(html[cursor])) != 0) {
                ++cursor;
            }
            if ((cursor < html.size()) && (html[cursor] == '/')) {
                closing = true;
                ++cursor;
            }

            const size_t name_start = cursor;
            while ((cursor < html.size()) && is_name_char(html[cursor])) {
                ++cursor;
            }
            const std::string tag_name = lower_ascii_copy(html.substr(name_start, cursor - name_start));
            const size_t tag_end = html.find('>', cursor);
            if (tag_end == std::string::npos) {
                break;
            }

            if (!closing && ((tag_name == "script") || (tag_name == "style") || (tag_name == "svg") ||
                             (tag_name == "noscript") || (tag_name == "template") || (tag_name == "head"))) {
                skip_tag_name = tag_name;
                const std::string closing_tag = "</" + tag_name;
                const std::string html_lower = lower_ascii_copy(html);
                const size_t close_pos = html_lower.find(closing_tag, tag_end + 1);
                if (close_pos == std::string::npos) {
                    break;
                }
                const size_t close_end = html.find('>', close_pos + closing_tag.size());
                if (close_end == std::string::npos) {
                    break;
                }
                index = close_end + 1;
                skip_tag_name.clear();
                continue;
            }

            if (!tag_name.empty() && is_block_tag(tag_name)) {
                append_spacing(output, '\n');
            }

            index = tag_end + 1;
            continue;
        }
        if (!in_tag) {
            output.push_back(ch);
        }
        ++index;
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

std::string extract_html_title(const std::string &html)
{
    const std::string html_lower = lower_ascii_copy(html);
    const size_t title_start = html_lower.find("<title");
    if (title_start == std::string::npos) {
        return {};
    }

    const size_t title_tag_end = html.find('>', title_start);
    if (title_tag_end == std::string::npos) {
        return {};
    }

    const size_t title_end = html_lower.find("</title>", title_tag_end + 1);
    if (title_end == std::string::npos) {
        return {};
    }

    return trim_copy(decode_html_entities(html_to_reader_text(html.substr(title_tag_end + 1, title_end - title_tag_end - 1))));
}

std::string extract_attribute_value(const std::string &tag, const std::string &attribute_name)
{
    const std::string tag_lower = lower_ascii_copy(tag);
    const std::string needle = lower_ascii_copy(attribute_name);
    size_t pos = 0;
    while ((pos = tag_lower.find(needle, pos)) != std::string::npos) {
        const bool boundary_before = (pos == 0) || !is_name_char(tag_lower[pos - 1]);
        const size_t name_end = pos + needle.size();
        const bool boundary_after = (name_end >= tag_lower.size()) || !is_name_char(tag_lower[name_end]);
        if (!boundary_before || !boundary_after) {
            pos = name_end;
            continue;
        }

        size_t value_pos = name_end;
        while ((value_pos < tag.size()) && std::isspace(static_cast<unsigned char>(tag[value_pos])) != 0) {
            ++value_pos;
        }
        if ((value_pos >= tag.size()) || (tag[value_pos] != '=')) {
            pos = name_end;
            continue;
        }
        ++value_pos;
        while ((value_pos < tag.size()) && std::isspace(static_cast<unsigned char>(tag[value_pos])) != 0) {
            ++value_pos;
        }
        if (value_pos >= tag.size()) {
            return {};
        }

        if ((tag[value_pos] == '"') || (tag[value_pos] == '\'')) {
            const char quote = tag[value_pos++];
            const size_t value_end = tag.find(quote, value_pos);
            return decode_html_entities(tag.substr(value_pos, value_end == std::string::npos ? std::string::npos : (value_end - value_pos)));
        }

        size_t value_end = value_pos;
        while ((value_end < tag.size()) && std::isspace(static_cast<unsigned char>(tag[value_end])) == 0 &&
               (tag[value_end] != '>')) {
            ++value_end;
        }
        return decode_html_entities(tag.substr(value_pos, value_end - value_pos));
    }

    return {};
}

std::string strip_fragment(const std::string &url)
{
    const size_t fragment_pos = url.find('#');
    return (fragment_pos == std::string::npos) ? url : url.substr(0, fragment_pos);
}

std::string base_origin(const std::string &url)
{
    const size_t scheme_pos = url.find("://");
    if (scheme_pos == std::string::npos) {
        return {};
    }
    const size_t host_end = url.find('/', scheme_pos + 3);
    return (host_end == std::string::npos) ? url : url.substr(0, host_end);
}

std::string base_directory(const std::string &url)
{
    const std::string without_fragment = strip_fragment(url);
    const size_t query_pos = without_fragment.find('?');
    const std::string clean_url = (query_pos == std::string::npos) ? without_fragment : without_fragment.substr(0, query_pos);
    const size_t slash_pos = clean_url.rfind('/');
    if (slash_pos == std::string::npos) {
        return clean_url;
    }
    return clean_url.substr(0, slash_pos + 1);
}

std::string normalize_path(const std::string &path)
{
    std::vector<std::string> segments;
    size_t cursor = 0;
    while (cursor <= path.size()) {
        const size_t next = path.find('/', cursor);
        const std::string part = path.substr(cursor, next == std::string::npos ? std::string::npos : (next - cursor));
        if (part == "..") {
            if (!segments.empty()) {
                segments.pop_back();
            }
        } else if (!part.empty() && (part != ".")) {
            segments.push_back(part);
        }
        if (next == std::string::npos) {
            break;
        }
        cursor = next + 1;
    }

    std::string normalized = "/";
    for (size_t index = 0; index < segments.size(); ++index) {
        if (index > 0) {
            normalized.push_back('/');
        }
        normalized += segments[index];
    }
    return normalized;
}

std::string resolve_url(const std::string &base_url, const std::string &reference)
{
    const std::string trimmed = trim_copy(reference);
    if (trimmed.empty()) {
        return {};
    }
    if (starts_with(lower_ascii_copy(trimmed), "javascript:") || starts_with(lower_ascii_copy(trimmed), "mailto:")) {
        return {};
    }
    if (trimmed.find("://") != std::string::npos) {
        return trimmed;
    }
    if (starts_with(trimmed, "//")) {
        const size_t scheme_pos = base_url.find("://");
        return ((scheme_pos == std::string::npos) ? std::string("https:") : base_url.substr(0, scheme_pos + 1)) + trimmed;
    }
    if (trimmed[0] == '#') {
        return strip_fragment(base_url) + trimmed;
    }
    if (trimmed[0] == '?') {
        return strip_fragment(base_url) + trimmed;
    }
    const std::string origin = base_origin(base_url);
    if (trimmed[0] == '/') {
        return origin + normalize_path(trimmed);
    }
    return origin + normalize_path(base_directory(base_url).substr(origin.size()) + trimmed);
}

bool is_duplicate_link(const P4Browser::PageDocument &page, const std::string &url)
{
    return std::any_of(page.links.begin(), page.links.end(), [&url](const P4Browser::PageLink &entry) {
        return entry.url == url.c_str();
    });
}

void append_document_outline(P4Browser::PageDocument &page)
{
    std::string body = page.body.c_str();

    if (!page.links.empty()) {
        body += "\n\nLinks\n";
        for (size_t index = 0; index < page.links.size(); ++index) {
            body += std::to_string(index + 1);
            body += ". ";
            body += page.links[index].label.empty() ? page.links[index].url.c_str() : page.links[index].label.c_str();
            body += '\n';
        }
    }

    if (!page.images.empty()) {
        body += "\nImages\n";
        for (const P4Browser::PageImage &image : page.images) {
            body += "- ";
            body += image.alt.empty() ? "Image" : image.alt.c_str();
            body += '\n';
        }
    }

    if (!page.forms.empty()) {
        body += "\nForms\n";
        for (const P4Browser::PageForm &form : page.forms) {
            body += "- ";
            body += form.label.empty() ? "GET form" : form.label.c_str();
            body += '\n';
        }
    }

    if (body.size() > 12000) {
        body.resize(12000);
        body += "\n\n[Truncated for device view]";
    }
    page.body = body.c_str();
}

P4Browser::PageDocument parse_page_document(const std::string &url, const std::string &fallback_title, const std::string &html)
{
    P4Browser::PageDocument page;
    const std::string parsed_title = extract_html_title(html);
    page.title = (!parsed_title.empty() ? parsed_title : fallback_title).c_str();
    page.url = url.c_str();
    page.body = html_to_reader_text(html).c_str();

    const std::string html_lower = lower_ascii_copy(html);

    size_t cursor = 0;
    while ((cursor = html_lower.find("<a", cursor)) != std::string::npos && page.links.size() < 24) {
        const size_t tag_end = html.find('>', cursor);
        if (tag_end == std::string::npos) {
            break;
        }
        const size_t close_pos = html_lower.find("</a>", tag_end + 1);
        if (close_pos == std::string::npos) {
            break;
        }

        const std::string tag = html.substr(cursor, tag_end - cursor + 1);
        const std::string href = resolve_url(url, extract_attribute_value(tag, "href"));
        const std::string label = trim_copy(html_to_reader_text(html.substr(tag_end + 1, close_pos - tag_end - 1)));
        if (!href.empty() && !is_duplicate_link(page, href)) {
            P4Browser::PageLink link;
            link.url = href.c_str();
            link.label = (!label.empty() ? label : href).c_str();
            page.links.push_back(std::move(link));
        }
        cursor = close_pos + 4;
    }

    cursor = 0;
    while ((cursor = html_lower.find("<img", cursor)) != std::string::npos && page.images.size() < 8) {
        const size_t tag_end = html.find('>', cursor);
        if (tag_end == std::string::npos) {
            break;
        }
        const std::string tag = html.substr(cursor, tag_end - cursor + 1);
        const std::string src = resolve_url(url, extract_attribute_value(tag, "src"));
        if (!src.empty()) {
            P4Browser::PageImage image;
            image.url = src.c_str();
            const std::string alt = trim_copy(extract_attribute_value(tag, "alt"));
            image.alt = (!alt.empty() ? alt : "Inline image").c_str();
            page.images.push_back(std::move(image));
        }
        cursor = tag_end + 1;
    }

    cursor = 0;
    while ((cursor = html_lower.find("<form", cursor)) != std::string::npos && page.forms.size() < 4) {
        const size_t tag_end = html.find('>', cursor);
        if (tag_end == std::string::npos) {
            break;
        }
        const size_t close_pos = html_lower.find("</form>", tag_end + 1);
        if (close_pos == std::string::npos) {
            break;
        }

        const std::string tag = html.substr(cursor, tag_end - cursor + 1);
        const std::string method = lower_ascii_copy(extract_attribute_value(tag, "method"));
        if (!method.empty() && (method != "get")) {
            cursor = close_pos + 7;
            continue;
        }

        P4Browser::PageForm form;
        const std::string action = resolve_url(url, extract_attribute_value(tag, "action"));
        form.action = (!action.empty() ? action : url).c_str();
        form.method = (method.empty() ? "get" : method).c_str();
        const std::string form_body = html.substr(tag_end + 1, close_pos - tag_end - 1);
        const std::string form_body_lower = lower_ascii_copy(form_body);
        form.label = trim_copy(html_to_reader_text(form_body)).c_str();
        if (form.label.empty()) {
            form.label = "Simple form";
        }

        size_t input_cursor = 0;
        while ((input_cursor = form_body_lower.find("<input", input_cursor)) != std::string::npos && form.fields.size() < 6) {
            const size_t input_end = form_body.find('>', input_cursor);
            if (input_end == std::string::npos) {
                break;
            }
            const std::string input_tag = form_body.substr(input_cursor, input_end - input_cursor + 1);
            P4Browser::PageFormField field;
            field.name = trim_copy(extract_attribute_value(input_tag, "name")).c_str();
            field.value = trim_copy(extract_attribute_value(input_tag, "value")).c_str();
            field.type = lower_ascii_copy(trim_copy(extract_attribute_value(input_tag, "type"))).c_str();
            if (field.type.empty()) {
                field.type = "text";
            }
            if (!field.name.empty() && (field.type != "submit") && (field.type != "button")) {
                form.fields.push_back(std::move(field));
            }
            input_cursor = input_end + 1;
        }

        page.forms.push_back(std::move(form));
        cursor = close_pos + 7;
    }

    append_document_outline(page);
    return page;
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

bool looks_like_url(const std::string &text)
{
    if (text.empty()) {
        return false;
    }
    if (text.find("://") != std::string::npos) {
        return true;
    }
    if (text.find(' ') != std::string::npos) {
        return false;
    }
    return (text.find('.') != std::string::npos) || (text.find('/') != std::string::npos);
}

std::string normalize_url(const std::string &text)
{
    if (text.find("://") != std::string::npos) {
        return text;
    }
    return "https://" + text;
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
        result.title = decode_html_entities(html_to_reader_text(html.substr(title_start + 1, title_end - title_start - 1))).c_str();

        const size_t snippet_marker = html.find("result__snippet", title_end);
        if ((snippet_marker != std::string::npos) && (snippet_marker < html.find("result__a", title_end + 4))) {
            const size_t snippet_start = html.find('>', snippet_marker);
            const size_t snippet_end = html.find("</a>", snippet_start == std::string::npos ? snippet_marker : snippet_start);
            if ((snippet_start != std::string::npos) && (snippet_end != std::string::npos)) {
                result.snippet = decode_html_entities(html_to_reader_text(html.substr(snippet_start + 1, snippet_end - snippet_start - 1))).c_str();
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
    ESP_Brookesia_PhoneApp("Browser", &img_app_browser, true),
    _screen(nullptr),
    _statusLabel(nullptr),
    _searchArea(nullptr),
    _keyboard(nullptr),
    _resultsList(nullptr),
    _detailPanel(nullptr),
    _detailTitle(nullptr),
    _detailMeta(nullptr),
    _detailScroll(nullptr),
    _detailDocument(nullptr),
    _bookmarkButton(nullptr),
    _resultsPanel(nullptr),
    _requestInFlight(false),
    _listMode(ListMode::SearchResults),
    _bookmarksLoaded(false),
    _homeLoaded(false)
{
}

bool P4Browser::init()
{
    if (!_bookmarksLoaded) {
        loadBookmarks();
    }
    return true;
}

bool P4Browser::run()
{
    if (!ensureUiReady()) {
        return false;
    }
    if (!_bookmarksLoaded) {
        loadBookmarks();
    }
    showResultList();
    if (!_homeLoaded) {
        _homeLoaded = true;
        lv_textarea_set_text(_searchArea, "google.com");
        startOpenPage(kDefaultHomeTitle, kDefaultHomeUrl, "Opening google.com...");
    } else {
        setStatus("Mini browser ready. Search, open lightweight pages, browse links, and save bookmarks. Large pages cache on SD card first, then PSRAM.");
    }
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
    _bookmarks.clear();
    _currentPage.clear();
    _buttonIndexMap.clear();
    _pageLinkIndexMap.clear();
    _formIndexMap.clear();
    _bookmarksLoaded = false;
    _homeLoaded = false;
    _requestInFlight.store(false);
    return true;
}

bool P4Browser::loadBookmarks()
{
    _bookmarksLoaded = true;
    _bookmarks.clear();

    if (!app_storage_ensure_sdcard_available() || !app_network_cache::file_exists(kBookmarksPath)) {
        return true;
    }

    FILE *file = std::fopen(kBookmarksPath, "rb");
    if (file == nullptr) {
        return false;
    }

    std::fseek(file, 0, SEEK_END);
    const long file_size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (file_size <= 0) {
        std::fclose(file);
        return true;
    }

    std::string json(static_cast<size_t>(file_size), '\0');
    const size_t read_size = std::fread(json.data(), 1, json.size(), file);
    std::fclose(file);
    if (read_size != json.size()) {
        return false;
    }

    cJSON *root = cJSON_Parse(json.c_str());
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return false;
    }

    cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, root) {
        cJSON *title = cJSON_GetObjectItemCaseSensitive(entry, "title");
        cJSON *url = cJSON_GetObjectItemCaseSensitive(entry, "url");
        if (!cJSON_IsString(url) || (url->valuestring == nullptr) || (url->valuestring[0] == '\0')) {
            continue;
        }

        BookmarkEntry bookmark;
        bookmark.url = url->valuestring;
        bookmark.title = (cJSON_IsString(title) && (title->valuestring != nullptr) && (title->valuestring[0] != '\0'))
                             ? title->valuestring
                             : url->valuestring;
        _bookmarks.push_back(std::move(bookmark));
    }

    cJSON_Delete(root);
    return true;
}

bool P4Browser::saveBookmarks() const
{
    if (!app_storage_ensure_sdcard_available() || !app_network_cache::ensure_directory_exists(kSdCacheDir)) {
        return false;
    }

    cJSON *root = cJSON_CreateArray();
    if (root == nullptr) {
        return false;
    }

    for (const BookmarkEntry &bookmark : _bookmarks) {
        cJSON *item = cJSON_CreateObject();
        if (item == nullptr) {
            cJSON_Delete(root);
            return false;
        }
        cJSON_AddStringToObject(item, "title", bookmark.title.c_str());
        cJSON_AddStringToObject(item, "url", bookmark.url.c_str());
        cJSON_AddItemToArray(root, item);
    }

    char *serialized = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (serialized == nullptr) {
        return false;
    }

    FILE *file = std::fopen(kBookmarksPath, "wb");
    if (file == nullptr) {
        cJSON_free(serialized);
        return false;
    }

    const size_t length = std::strlen(serialized);
    const size_t written = std::fwrite(serialized, 1, length, file);
    std::fclose(file);
    cJSON_free(serialized);
    return written == length;
}

bool P4Browser::hasBookmark(const std::string &url) const
{
    return std::any_of(_bookmarks.begin(), _bookmarks.end(), [&url](const BookmarkEntry &entry) {
        return entry.url == url.c_str();
    });
}

bool P4Browser::toggleCurrentBookmark()
{
    const std::string current_url = _currentPage.url.c_str();
    if (current_url.empty()) {
        return false;
    }

    const auto existing = std::find_if(_bookmarks.begin(), _bookmarks.end(), [&current_url](const BookmarkEntry &entry) {
        return entry.url == current_url.c_str();
    });

    if (existing != _bookmarks.end()) {
        _bookmarks.erase(existing);
        updateBookmarkButton();
        saveBookmarks();
        setStatus("Bookmark removed.");
        return true;
    }

    BookmarkEntry bookmark;
    bookmark.url = _currentPage.url;
    bookmark.title = _currentPage.title.empty() ? _currentPage.url : _currentPage.title;
    _bookmarks.push_back(std::move(bookmark));
    updateBookmarkButton();
    if (!saveBookmarks()) {
        setStatus("Saved in memory. Insert SD card to persist bookmarks.");
    } else {
        setStatus("Bookmark saved.");
    }
    return true;
}

void P4Browser::updateBookmarkButton()
{
    if ((_bookmarkButton == nullptr) || !lv_obj_is_valid(_bookmarkButton)) {
        return;
    }

    lv_obj_t *label = lv_obj_get_child(_bookmarkButton, 0);
    if (label == nullptr) {
        return;
    }

    const bool saved = hasBookmark(_currentPage.url.c_str());
    lv_label_set_text(label, saved ? LV_SYMBOL_OK " Saved" : "Save");
}

bool P4Browser::buildUi()
{
    _screen = lv_scr_act();
    if (_screen == nullptr) {
        return false;
    }

    const lv_coord_t screen_width = lv_obj_get_width(_screen);
    const lv_coord_t screen_height = lv_obj_get_height(_screen);
    const lv_coord_t horizontal_margin = 8;
    const lv_coord_t content_width = screen_width - (horizontal_margin * 2);
    const lv_coord_t header_top = 8;
    const lv_coord_t header_height = 62;
    const lv_coord_t status_top = 76;
    const lv_coord_t status_height = 32;
    const lv_coord_t address_top = 112;
    const lv_coord_t address_height = 52;
    const lv_coord_t body_top = 172;
    const lv_coord_t body_height = screen_height - body_top - 8;

    lv_obj_add_event_cb(_screen, onScreenDeleted, LV_EVENT_DELETE, this);

    lv_obj_set_style_bg_color(_screen, lv_color_hex(0xE8EEF9), 0);
    lv_obj_set_style_bg_grad_color(_screen, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_grad_dir(_screen, LV_GRAD_DIR_VER, 0);

    lv_obj_t *header = lv_obj_create(_screen);
    lv_obj_set_size(header, content_width, header_height);
    lv_obj_set_pos(header, horizontal_margin, header_top);
    lv_obj_set_style_radius(header, 22, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 12, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_width(header, 18, 0);
    lv_obj_set_style_shadow_opa(header, LV_OPA_20, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *chromeDotRed = lv_obj_create(header);
    lv_obj_set_size(chromeDotRed, 16, 16);
    lv_obj_align(chromeDotRed, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_radius(chromeDotRed, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(chromeDotRed, lv_color_hex(0xEA4335), 0);
    lv_obj_set_style_border_width(chromeDotRed, 0, 0);
    lv_obj_clear_flag(chromeDotRed, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *chromeDotYellow = lv_obj_create(header);
    lv_obj_set_size(chromeDotYellow, 16, 16);
    lv_obj_align_to(chromeDotYellow, chromeDotRed, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_set_style_radius(chromeDotYellow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(chromeDotYellow, lv_color_hex(0xFBBC05), 0);
    lv_obj_set_style_border_width(chromeDotYellow, 0, 0);
    lv_obj_clear_flag(chromeDotYellow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *chromeDotGreen = lv_obj_create(header);
    lv_obj_set_size(chromeDotGreen, 16, 16);
    lv_obj_align_to(chromeDotGreen, chromeDotYellow, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_set_style_radius(chromeDotGreen, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(chromeDotGreen, lv_color_hex(0x34A853), 0);
    lv_obj_set_style_border_width(chromeDotGreen, 0, 0);
    lv_obj_clear_flag(chromeDotGreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Mini Browser");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0F172A), 0);
    lv_obj_align(title, LV_ALIGN_RIGHT_MID, -10, 0);

    _statusLabel = lv_label_create(_screen);
    lv_obj_set_size(_statusLabel, content_width, status_height);
    lv_label_set_long_mode(_statusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_statusLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_statusLabel, lv_color_hex(0x334155), 0);
    lv_obj_set_pos(_statusLabel, horizontal_margin + 4, status_top);

    _searchArea = lv_textarea_create(_screen);
    lv_obj_set_size(_searchArea, content_width - 82, address_height);
    lv_obj_set_pos(_searchArea, horizontal_margin, address_top);
    lv_textarea_set_one_line(_searchArea, true);
    lv_textarea_set_placeholder_text(_searchArea, "Search Google or type a URL");
    lv_obj_set_style_radius(_searchArea, 27, 0);
    lv_obj_set_style_bg_color(_searchArea, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(_searchArea, 0, 0);
    lv_obj_add_event_cb(_searchArea, onSearchFocus, LV_EVENT_FOCUSED, this);

    lv_obj_t *searchButton = lv_btn_create(_screen);
    lv_obj_set_size(searchButton, 74, address_height);
    lv_obj_set_pos(searchButton, screen_width - horizontal_margin - 74, address_top);
    lv_obj_set_style_bg_color(searchButton, lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_border_width(searchButton, 0, 0);
    lv_obj_set_style_radius(searchButton, 27, 0);
    lv_obj_add_event_cb(searchButton, onSearchClicked, LV_EVENT_CLICKED, this);

    lv_obj_t *searchLabel = lv_label_create(searchButton);
    lv_label_set_text(searchLabel, "Go");
    lv_obj_set_style_text_color(searchLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(searchLabel);

    _resultsPanel = lv_obj_create(_screen);
    lv_obj_set_size(_resultsPanel, content_width, body_height);
    lv_obj_set_pos(_resultsPanel, horizontal_margin, body_top);
    lv_obj_set_style_radius(_resultsPanel, 18, 0);
    lv_obj_set_style_border_width(_resultsPanel, 0, 0);
    lv_obj_set_style_pad_all(_resultsPanel, 8, 0);
    lv_obj_set_style_bg_color(_resultsPanel, lv_color_hex(0xFFFFFF), 0);

    _resultsList = lv_list_create(_resultsPanel);
    lv_obj_set_size(_resultsList, content_width - 16, body_height - 16);
    lv_obj_center(_resultsList);

    _detailPanel = lv_obj_create(_screen);
    lv_obj_set_size(_detailPanel, content_width, body_height);
    lv_obj_set_pos(_detailPanel, horizontal_margin, body_top);
    lv_obj_set_style_radius(_detailPanel, 18, 0);
    lv_obj_set_style_border_width(_detailPanel, 0, 0);
    lv_obj_set_style_pad_all(_detailPanel, 10, 0);
    lv_obj_set_style_bg_color(_detailPanel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_flag(_detailPanel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *backButton = lv_btn_create(_detailPanel);
    lv_obj_set_size(backButton, 100, 38);
    lv_obj_align(backButton, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(backButton, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_border_width(backButton, 0, 0);
    lv_obj_add_event_cb(backButton, onBackToResultsClicked, LV_EVENT_CLICKED, this);

    lv_obj_t *backLabel = lv_label_create(backButton);
    lv_label_set_text(backLabel, LV_SYMBOL_LEFT " List");
    lv_obj_center(backLabel);

    _bookmarkButton = lv_btn_create(_detailPanel);
    lv_obj_set_size(_bookmarkButton, 102, 38);
    lv_obj_align(_bookmarkButton, LV_ALIGN_TOP_RIGHT, -106, 0);
    lv_obj_set_style_bg_color(_bookmarkButton, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_border_width(_bookmarkButton, 0, 0);
    lv_obj_add_event_cb(_bookmarkButton, onBookmarkToggleClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *bookmarkLabel = lv_label_create(_bookmarkButton);
    lv_label_set_text(bookmarkLabel, "Save");
    lv_obj_center(bookmarkLabel);

    lv_obj_t *bookmarksButton = lv_btn_create(_detailPanel);
    lv_obj_set_size(bookmarksButton, 96, 38);
    lv_obj_align(bookmarksButton, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(bookmarksButton, lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_border_width(bookmarksButton, 0, 0);
    lv_obj_add_event_cb(bookmarksButton, onBookmarksClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *bookmarksLabel = lv_label_create(bookmarksButton);
    lv_label_set_text(bookmarksLabel, "Marks");
    lv_obj_set_style_text_color(bookmarksLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(bookmarksLabel);

    _detailTitle = lv_label_create(_detailPanel);
    lv_obj_set_width(_detailTitle, content_width - 212);
    lv_label_set_long_mode(_detailTitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_detailTitle, &lv_font_montserrat_18, 0);
    lv_obj_align(_detailTitle, LV_ALIGN_TOP_LEFT, 106, 0);

    _detailMeta = lv_label_create(_detailPanel);
    lv_obj_set_width(_detailMeta, content_width - 12);
    lv_label_set_long_mode(_detailMeta, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_detailMeta, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_detailMeta, lv_color_hex(0x475569), 0);
    lv_obj_align(_detailMeta, LV_ALIGN_TOP_LEFT, 0, 46);

    _detailScroll = lv_obj_create(_detailPanel);
    lv_obj_set_size(_detailScroll, content_width - 12, body_height - 118);
    lv_obj_align(_detailScroll, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(_detailScroll, 14, 0);
    lv_obj_set_style_border_width(_detailScroll, 0, 0);
    lv_obj_set_style_bg_color(_detailScroll, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_all(_detailScroll, 8, 0);
    lv_obj_set_scrollbar_mode(_detailScroll, LV_SCROLLBAR_MODE_AUTO);

    _detailDocument = lv_obj_create(_detailScroll);
    lv_obj_set_width(_detailDocument, lv_pct(100));
    lv_obj_set_height(_detailDocument, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(_detailDocument, 0, 0);
    lv_obj_set_style_bg_opa(_detailDocument, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(_detailDocument, 0, 0);
    lv_obj_set_style_pad_row(_detailDocument, 8, 0);
    lv_obj_clear_flag(_detailDocument, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(_detailDocument, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_detailDocument, LV_FLEX_FLOW_COLUMN);

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

    const std::string input = trim_copy(query);
    if (looks_like_url(input)) {
        _requestInFlight.store(false);
        startOpenPage(input, normalize_url(input), "Opening page...");
        return;
    }

    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    setStatus("Searching DuckDuckGo Lite...");
    _listMode = ListMode::SearchResults;

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

void P4Browser::startOpenPage(const std::string &title, const std::string &url, const char *status_text)
{
    if (_requestInFlight.exchange(true)) {
        setStatus("Wait for the current request to finish.");
        return;
    }

    if ((_keyboard != nullptr) && lv_obj_is_valid(_keyboard)) {
        lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    setStatus(status_text);

    WorkerContext *context = new WorkerContext();
    context->app = this;
    context->action = WorkerAction::Open;
    context->title = title;
    context->url = url;
    if (xTaskCreate(workerTask, "p4_page", 10240, context, 5, nullptr) != pdPASS) {
        delete context;
        _requestInFlight.store(false);
        setStatus("Failed to start page fetch.");
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

    setStatus("Loading mobile page...");
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

void P4Browser::startOpenBookmark(size_t index)
{
    if (index >= _bookmarks.size()) {
        return;
    }
    startOpenPage(_bookmarks[index].title.c_str(), _bookmarks[index].url.c_str(), "Opening bookmark...");
}

void P4Browser::startSubmitForm(size_t index)
{
    if (index >= _currentPage.forms.size()) {
        return;
    }

    const PageForm &form = _currentPage.forms[index];
    std::string url = form.action.c_str();
    bool has_query = (url.find('?') != std::string::npos);
    for (const PageFormField &field : form.fields) {
        if (field.name.empty()) {
            continue;
        }
        url += has_query ? '&' : '?';
        has_query = true;
        url += app_network_cache::percent_encode_component(field.name.c_str());
        url += '=';
        url += app_network_cache::percent_encode_component(field.value.c_str());
    }

    startOpenPage(form.label.c_str(), url, "Submitting simple GET form...");
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

void P4Browser::renderBookmarks()
{
    if ((_resultsList == nullptr) || !lv_obj_is_valid(_resultsList)) {
        return;
    }

    _buttonIndexMap.clear();
    lv_obj_clean(_resultsList);
    if (_bookmarks.empty()) {
        lv_obj_t *label = lv_label_create(_resultsList);
        lv_label_set_text(label, "No bookmarks yet. Open a page and tap Save.");
        return;
    }

    for (size_t index = 0; index < _bookmarks.size(); ++index) {
        std::string text = _bookmarks[index].title.c_str();
        text += "\n";
        text += _bookmarks[index].url.c_str();
        lv_obj_t *button = lv_list_add_btn(_resultsList, LV_SYMBOL_DIRECTORY, text.c_str());
        lv_obj_add_event_cb(button, onResultClicked, LV_EVENT_CLICKED, this);
        _buttonIndexMap[button] = index;
    }
}

void P4Browser::renderPageDocument()
{
    if ((_detailDocument == nullptr) || !lv_obj_is_valid(_detailDocument)) {
        return;
    }

    _pageLinkIndexMap.clear();
    _formIndexMap.clear();
    lv_obj_clean(_detailDocument);

    lv_obj_t *bodyLabel = lv_label_create(_detailDocument);
    lv_obj_set_width(bodyLabel, lv_pct(100));
    lv_label_set_long_mode(bodyLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(bodyLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bodyLabel, lv_color_hex(0x1E293B), 0);
    lv_label_set_text(bodyLabel, _currentPage.body.c_str());

    if (!_currentPage.links.empty()) {
        lv_obj_t *header = lv_label_create(_detailDocument);
        lv_label_set_text(header, "Links");
        lv_obj_set_style_text_font(header, &lv_font_montserrat_18, 0);

        for (size_t index = 0; index < _currentPage.links.size(); ++index) {
            lv_obj_t *button = lv_btn_create(_detailDocument);
            lv_obj_set_width(button, lv_pct(100));
            lv_obj_set_style_bg_color(button, lv_color_hex(0xDBEAFE), 0);
            lv_obj_set_style_border_width(button, 0, 0);
            lv_obj_add_event_cb(button, onPageLinkClicked, LV_EVENT_CLICKED, this);

            std::string text = std::to_string(index + 1) + ". " + _currentPage.links[index].label.c_str();
            lv_obj_t *label = lv_label_create(button);
            lv_obj_set_width(label, lv_pct(96));
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
            lv_label_set_text(label, text.c_str());
            lv_obj_center(label);
            _pageLinkIndexMap[button] = index;
        }
    }

    if (!_currentPage.images.empty()) {
        lv_obj_t *header = lv_label_create(_detailDocument);
        lv_label_set_text(header, "Small Images");
        lv_obj_set_style_text_font(header, &lv_font_montserrat_18, 0);

        for (const PageImage &image : _currentPage.images) {
            lv_obj_t *card = lv_obj_create(_detailDocument);
            lv_obj_set_width(card, lv_pct(100));
            lv_obj_set_height(card, LV_SIZE_CONTENT);
            lv_obj_set_style_border_width(card, 0, 0);
            lv_obj_set_style_bg_color(card, lv_color_hex(0xFEF3C7), 0);
            lv_obj_set_style_pad_all(card, 10, 0);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

            std::string text = image.alt.c_str();
            text += "\n";
            text += image.url.c_str();
            lv_obj_t *label = lv_label_create(card);
            lv_obj_set_width(label, lv_pct(100));
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
            lv_label_set_text(label, text.c_str());
        }
    }

    if (!_currentPage.forms.empty()) {
        lv_obj_t *header = lv_label_create(_detailDocument);
        lv_label_set_text(header, "Simple Forms");
        lv_obj_set_style_text_font(header, &lv_font_montserrat_18, 0);

        for (size_t index = 0; index < _currentPage.forms.size(); ++index) {
            const PageForm &form = _currentPage.forms[index];
            lv_obj_t *button = lv_btn_create(_detailDocument);
            lv_obj_set_width(button, lv_pct(100));
            lv_obj_set_style_bg_color(button, lv_color_hex(0xDCFCE7), 0);
            lv_obj_set_style_border_width(button, 0, 0);
            lv_obj_add_event_cb(button, onFormSubmitClicked, LV_EVENT_CLICKED, this);

            std::string text = form.label.c_str();
            text += "\n";
            text += form.action.c_str();
            if (!form.fields.empty()) {
                text += "\nFields: ";
                for (size_t field_index = 0; field_index < form.fields.size(); ++field_index) {
                    if (field_index > 0) {
                        text += ", ";
                    }
                    text += form.fields[field_index].name.c_str();
                }
            }
            lv_obj_t *label = lv_label_create(button);
            lv_obj_set_width(label, lv_pct(96));
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
            lv_label_set_text(label, text.c_str());
            lv_obj_center(label);
            _formIndexMap[button] = index;
        }
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

    if (_listMode == ListMode::Bookmarks) {
        renderBookmarks();
    } else {
        renderResults();
    }
}

void P4Browser::showPage(const PageDocument &page)
{
    if ((_detailPanel == nullptr) || (_detailTitle == nullptr) || (_detailDocument == nullptr) || (_detailMeta == nullptr)) {
        return;
    }

    _currentPage = page;
    lv_label_set_text(_detailTitle, _currentPage.title.c_str());
    std::string meta = std::string(_currentPage.url.c_str()) + "\nMini browser • Loaded from " +
                       app_network_cache::storage_label(_currentPage.storage);
    lv_label_set_text(_detailMeta, meta.c_str());
    renderPageDocument();
    if ((_detailScroll != nullptr) && lv_obj_is_valid(_detailScroll)) {
        lv_obj_scroll_to_y(_detailScroll, 0, LV_ANIM_OFF);
    }
    updateBookmarkButton();
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
    _detailScroll = nullptr;
    _detailDocument = nullptr;
    _bookmarkButton = nullptr;
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
        if (app->_listMode == ListMode::Bookmarks) {
            app->startOpenBookmark(found->second);
        } else {
            app->startOpenResult(found->second);
        }
    }
}

void P4Browser::onBackToResultsClicked(lv_event_t *event)
{
    P4Browser *app = static_cast<P4Browser *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->showResultList();
    }
}

void P4Browser::onBookmarkToggleClicked(lv_event_t *event)
{
    P4Browser *app = static_cast<P4Browser *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->toggleCurrentBookmark();
    }
}

void P4Browser::onBookmarksClicked(lv_event_t *event)
{
    P4Browser *app = static_cast<P4Browser *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->_listMode = ListMode::Bookmarks;
        app->setStatus("Showing bookmarks.");
        app->showResultList();
    }
}

void P4Browser::onPageLinkClicked(lv_event_t *event)
{
    P4Browser *app = static_cast<P4Browser *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(event);
    const auto found = app->_pageLinkIndexMap.find(target);
    if ((found == app->_pageLinkIndexMap.end()) || (found->second >= app->_currentPage.links.size())) {
        return;
    }

    const PageLink &link = app->_currentPage.links[found->second];
    if ((app->_searchArea != nullptr) && lv_obj_is_valid(app->_searchArea)) {
        lv_textarea_set_text(app->_searchArea, link.url.c_str());
    }
    app->startOpenPage(link.label.c_str(), link.url.c_str(), "Opening link...");
}

void P4Browser::onFormSubmitClicked(lv_event_t *event)
{
    P4Browser *app = static_cast<P4Browser *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(event);
    const auto found = app->_formIndexMap.find(target);
    if (found != app->_formIndexMap.end()) {
        app->startSubmitForm(found->second);
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
                                                            payload, result->status, false, 20000,
                                                            kMobileBrowserUserAgent, kHtmlAcceptHeader)) {
            result->success = false;
        } else {
            std::string html;
            result->success = app_network_cache::load_cached_text(payload, html, 56000);
            if (!result->success) {
                result->status = "Failed to load page response";
            } else {
                result->page = parse_page_document(context->url, context->title, html);
                result->page.storage = payload.storage;
                result->payload = std::move(payload);
                result->success = !result->page.body.empty();
                result->status = result->success ? "Mini page loaded" : "The fetched page did not contain readable text";
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
        result->app->_listMode = ListMode::SearchResults;
        result->app->renderResults();
        result->app->showResultList();
        return;
    }

    result->app->showPage(result->page);
}