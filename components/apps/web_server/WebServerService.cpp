#include "WebServerService.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <limits>
#include <new>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>
#include <vector>

#include "bsp/esp-bsp.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mdns.h"
#include "storage_access.h"

namespace {

static const char *TAG = "WebServerSvc";
static constexpr const char *kSdWebRoot = "/sdcard/web";
static constexpr const char *kSpiffsWebRoot = BSP_SPIFFS_MOUNT_POINT "/web";
static constexpr const char *kMdnsHostName = "jc4880-web";
static constexpr const char *kMdnsInstanceName = "JC4880 Web Server";
static constexpr size_t kIoBufferSize = 2048;

template <typename T>
class PsramAllocator {
public:
    using value_type = T;

    PsramAllocator() noexcept = default;

    template <typename U>
    PsramAllocator(const PsramAllocator<U> &) noexcept
    {
    }

    [[nodiscard]] T *allocate(std::size_t count)
    {
        if (count > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
            std::abort();
        }

        void *storage = heap_caps_malloc(count * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (storage == nullptr) {
            storage = heap_caps_malloc(count * sizeof(T), MALLOC_CAP_8BIT);
        }
        if (storage == nullptr) {
            std::abort();
        }

        return static_cast<T *>(storage);
    }

    void deallocate(T *ptr, std::size_t) noexcept
    {
        heap_caps_free(ptr);
    }

    template <typename U>
    bool operator==(const PsramAllocator<U> &) const noexcept
    {
        return true;
    }

    template <typename U>
    bool operator!=(const PsramAllocator<U> &) const noexcept
    {
        return false;
    }
};

using PsramString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

template <typename T>
using PsramVector = std::vector<T, PsramAllocator<T>>;

using PsramPathCandidate = std::pair<PsramString, bool>;

const char *kEmbeddedIndexHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>JC4880 Web Server</title>
  <style>
    :root { color-scheme: light; }
    body { margin: 0; font-family: "Segoe UI", sans-serif; background: linear-gradient(160deg,#dbeafe,#f8fafc 50%,#cffafe); color:#102a43; }
    main { max-width: 840px; margin: 0 auto; padding: 48px 24px 64px; }
    .hero { background: rgba(255,255,255,.78); backdrop-filter: blur(12px); border-radius: 28px; padding: 28px; box-shadow: 0 20px 45px rgba(15,23,42,.12); }
    h1 { margin: 0 0 12px; font-size: 2.2rem; }
    p { line-height: 1.6; }
    .pill { display:inline-block; padding: 8px 12px; border-radius: 999px; background:#0f766e; color:#fff; font-weight:600; margin: 0 12px 12px 0; }
    a.button { display:inline-block; margin-top:18px; padding: 12px 18px; border-radius: 14px; background:#2563eb; color:#fff; text-decoration:none; font-weight:700; }
    code { background:#e2e8f0; padding: 2px 6px; border-radius: 8px; }
  </style>
</head>
<body>
  <main>
    <section class="hero">
      <span class="pill">Embedded Fallback</span>
      <span class="pill">SD /web preferred</span>
      <h1>JC4880 Web Server is online</h1>
      <p>No deployable site was found under <code>/sdcard/web</code> or <code>/spiffs/web</code>, so the device is serving its built-in recovery landing page.</p>
      <p>Upload a corrected site bundle through recovery and it will take over automatically on the next refresh. If AP mode is enabled, captive-portal probe requests will also be redirected here.</p>
      <a class="button" href="/recovery">Open Recovery</a>
    </section>
  </main>
</body>
</html>
)HTML";

const char *kEmbeddedRecoveryHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>JC4880 Recovery</title>
  <style>
    body { margin:0; font-family:"Segoe UI",sans-serif; background:#0f172a; color:#e2e8f0; }
    main { max-width:960px; margin:0 auto; padding:32px 20px 60px; }
    .panel { background:linear-gradient(180deg,rgba(30,41,59,.95),rgba(15,23,42,.95)); border:1px solid rgba(148,163,184,.2); border-radius:24px; padding:24px; box-shadow:0 22px 50px rgba(2,6,23,.45); }
    h1 { margin:0 0 8px; font-size:2rem; }
    p { line-height:1.55; color:#cbd5e1; }
    .row { display:flex; flex-wrap:wrap; gap:12px; margin:18px 0; }
    button, label.cta { border:none; border-radius:14px; padding:12px 18px; font-size:1rem; font-weight:700; cursor:pointer; }
    button.primary, label.cta { background:#22c55e; color:#052e16; }
    button.secondary { background:#1d4ed8; color:#eff6ff; }
    button.ghost { background:#334155; color:#e2e8f0; }
    input[type=file] { display:none; }
    #status { min-height:24px; margin:8px 0 18px; font-weight:600; color:#7dd3fc; }
    ul { list-style:none; padding:0; margin:0; }
    li { padding:10px 0; border-bottom:1px solid rgba(148,163,184,.12); }
    code { background:#1e293b; padding:2px 6px; border-radius:8px; color:#bfdbfe; }
  </style>
</head>
<body>
  <main>
    <section class="panel">
      <h1>Recovery Uploader</h1>
      <p>Use this page when the SD card site is missing or damaged. Choose a folder or one or more files and the device will upload them into <code>/sdcard/web</code>.</p>
      <div class="row">
        <label class="cta" for="folderPicker">Choose Folder</label>
        <input id="folderPicker" type="file" webkitdirectory directory multiple>
        <label class="cta" for="filePicker">Choose Files</label>
        <input id="filePicker" type="file" multiple>
        <button class="secondary" id="refreshButton">Refresh Listing</button>
        <button class="ghost" id="openRoot">Open Root</button>
      </div>
      <div id="status">Waiting for files.</div>
      <h2>Detected Files</h2>
      <ul id="files"></ul>
    </section>
  </main>
  <script>
    const statusEl = document.getElementById('status');
    const filesEl = document.getElementById('files');

    function setStatus(text, isError = false) {
      statusEl.textContent = text;
      statusEl.style.color = isError ? '#fca5a5' : '#7dd3fc';
    }

    function safePath(file) {
      const source = file.webkitRelativePath || file.name;
      return source.replace(/^\/+/, '').replace(/\\/g, '/');
    }

    async function loadListing() {
      setStatus('Loading file listing...');
      try {
        const response = await fetch('/api/files');
        const payload = await response.json();
        filesEl.innerHTML = '';
        if (!payload.files || payload.files.length === 0) {
          const li = document.createElement('li');
          li.textContent = 'No deployed files detected yet.';
          filesEl.appendChild(li);
        } else {
          for (const entry of payload.files) {
            const li = document.createElement('li');
            li.textContent = `${entry.root}: ${entry.path}`;
            filesEl.appendChild(li);
          }
        }
        setStatus(`Live source: ${payload.live_source}. Recovery target: ${payload.recovery_target}.`);
      } catch (error) {
        setStatus(`Failed to load listing: ${error}`, true);
      }
    }

    async function uploadFiles(fileList) {
      if (!fileList || fileList.length === 0) {
        return;
      }
      let uploaded = 0;
      for (const file of fileList) {
        const path = safePath(file);
        setStatus(`Uploading ${path} (${uploaded + 1}/${fileList.length})...`);
        const response = await fetch(`/api/upload?path=${encodeURIComponent(path)}`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/octet-stream' },
          body: await file.arrayBuffer(),
        });
        if (!response.ok) {
          const text = await response.text();
          throw new Error(text || `Upload failed for ${path}`);
        }
        uploaded += 1;
      }
      setStatus(`Uploaded ${uploaded} file(s) into /sdcard/web.`);
      await loadListing();
    }

    document.getElementById('folderPicker').addEventListener('change', async (event) => {
      try {
        await uploadFiles(event.target.files);
      } catch (error) {
        setStatus(String(error), true);
      } finally {
        event.target.value = '';
      }
    });

    document.getElementById('filePicker').addEventListener('change', async (event) => {
      try {
        await uploadFiles(event.target.files);
      } catch (error) {
        setStatus(String(error), true);
      } finally {
        event.target.value = '';
      }
    });

    document.getElementById('refreshButton').addEventListener('click', loadListing);
    document.getElementById('openRoot').addEventListener('click', () => window.location.href = '/');
    loadListing();
  </script>
</body>
</html>
)HTML";

bool is_directory_path(const std::string &path)
{
    struct stat st = {};
    return (stat(path.c_str(), &st) == 0) && S_ISDIR(st.st_mode);
}

bool file_exists(const std::string &path)
{
    struct stat st = {};
    return stat(path.c_str(), &st) == 0;
}

bool ensure_directory_recursive(const std::string &path)
{
    if (path.empty()) {
        return false;
    }

    if (is_directory_path(path)) {
        return true;
    }

    std::string partial;
    partial.reserve(path.size());
    for (size_t index = 0; index < path.size(); ++index) {
        partial.push_back(path[index]);
        if ((path[index] != '/') || partial.empty()) {
            continue;
        }

        if (partial.size() == 1) {
            continue;
        }

        if (!is_directory_path(partial) && (mkdir(partial.c_str(), 0775) != 0) && (errno != EEXIST)) {
            return false;
        }
    }

    return is_directory_path(path) || ((mkdir(path.c_str(), 0775) == 0) || (errno == EEXIST));
}

std::string strip_query(const char *uri)
{
    std::string path = (uri != nullptr) ? uri : "/";
    const size_t query_pos = path.find('?');
    if (query_pos != std::string::npos) {
        path.erase(query_pos);
    }
    return path;
}

std::string trim_gzip_suffix(const std::string &path)
{
    if ((path.size() > 3) && (path.rfind(".gz") == (path.size() - 3))) {
        return path.substr(0, path.size() - 3);
    }
    return path;
}

const char *content_type_for_path(const std::string &path)
{
    const std::string normalized = trim_gzip_suffix(path);
    const size_t dot = normalized.find_last_of('.');
    const std::string extension = (dot == std::string::npos) ? std::string{} : normalized.substr(dot + 1);
    if (extension == "html") {
        return "text/html; charset=utf-8";
    }
    if (extension == "css") {
        return "text/css; charset=utf-8";
    }
    if (extension == "js") {
        return "application/javascript; charset=utf-8";
    }
    if (extension == "json") {
        return "application/json; charset=utf-8";
    }
    if (extension == "svg") {
        return "image/svg+xml";
    }
    if (extension == "png") {
        return "image/png";
    }
    if ((extension == "jpg") || (extension == "jpeg")) {
        return "image/jpeg";
    }
    if (extension == "gif") {
        return "image/gif";
    }
    if (extension == "ico") {
        return "image/x-icon";
    }
    if (extension == "txt") {
        return "text/plain; charset=utf-8";
    }
    return "application/octet-stream";
}

PsramString json_escape(const std::string &value)
{
    PsramString output;
    output.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            output += "\\\\";
            break;
        case '"':
            output += "\\\"";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            output.push_back(ch);
            break;
        }
    }
    return output;
}

PsramString bool_json(bool value)
{
    return value ? "true" : "false";
}

bool request_accepts_gzip(httpd_req_t *req)
{
    const size_t header_len = httpd_req_get_hdr_value_len(req, "Accept-Encoding");
    if (header_len == 0) {
        return false;
    }

    PsramString value(header_len + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, "Accept-Encoding", value.data(), value.size()) != ESP_OK) {
        return false;
    }
    return value.find("gzip") != std::string::npos;
}

std::string join_path(const std::string &base, const std::string &relative)
{
    if (relative.empty()) {
        return base;
    }
    if (base.empty() || (base == "/")) {
        return "/" + relative;
    }
    return base + "/" + relative;
}

bool sanitize_relative_path(std::string &path)
{
    while (!path.empty() && (path.front() == '/')) {
        path.erase(path.begin());
    }
    while (!path.empty() && (path.back() == '/')) {
        path.pop_back();
    }
    if (path.find("..") != std::string::npos) {
        return false;
    }
    return !path.empty();
}

std::string relative_path_for_uri(const std::string &uri)
{
    std::string path = uri;
    while (!path.empty() && (path.front() == '/')) {
        path.erase(path.begin());
    }
    if (path.empty()) {
        path = "index.html";
    }
    if (!path.empty() && (path.back() == '/')) {
        path += "index.html";
    }
    if (path == "index") {
        path = "index.html";
    }
    return path;
}

std::string read_query_value(httpd_req_t *req, const char *key)
{
    const size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len == 0) {
        return {};
    }

    PsramString query(query_len + 1, '\0');
    if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) {
        return {};
    }

    char value[256] = {};
    if (httpd_query_key_value(query.c_str(), key, value, sizeof(value)) != ESP_OK) {
        return {};
    }
    return value;
}

esp_err_t send_text(httpd_req_t *req, const char *type, const PsramString &body)
{
    httpd_resp_set_type(req, type);
    return httpd_resp_send(req, body.c_str(), body.size());
}

esp_err_t send_string(httpd_req_t *req, const char *type, const char *body)
{
    httpd_resp_set_type(req, type);
    return httpd_resp_sendstr(req, body);
}

esp_err_t serve_file(httpd_req_t *req, const std::string &path, bool gzip)
{
    FILE *file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
    }

    httpd_resp_set_type(req, content_type_for_path(path));
    if (gzip) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    char buffer[kIoBufferSize];
    size_t bytes_read = 0;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        const esp_err_t err = httpd_resp_send_chunk(req, buffer, bytes_read);
        if (err != ESP_OK) {
            fclose(file);
            return err;
        }
    }

    fclose(file);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

void append_file_entries(const char *root_label, const std::string &root_path, PsramVector<PsramString> &json_entries, const std::string &relative = {})
{
    const std::string full_path = relative.empty() ? root_path : join_path(root_path, relative);
    DIR *dir = opendir(full_path.c_str());
    if (dir == nullptr) {
        return;
    }

    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }

        const std::string child_relative = relative.empty() ? entry->d_name : (relative + "/" + entry->d_name);
        const std::string child_path = join_path(root_path, child_relative);
        struct stat st = {};
        if (stat(child_path.c_str(), &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            append_file_entries(root_label, root_path, json_entries, child_relative);
            continue;
        }

        json_entries.push_back("{\"root\":\"" + json_escape(root_label) + "\",\"path\":\"" +
                               json_escape(child_relative) + "\"}");
    }

    closedir(dir);
}

bool has_ap_ip()
{
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif == nullptr) {
        return false;
    }
    esp_netif_ip_info_t ip_info = {};
    return (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) && (ip_info.ip.addr != 0);
}

std::string build_url_for_ifkey(const char *ifkey)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(ifkey);
    if (netif == nullptr) {
        return {};
    }

    esp_netif_ip_info_t ip_info = {};
    if ((esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) || (ip_info.ip.addr == 0)) {
        return {};
    }

    char url[64] = {};
    snprintf(url,
             sizeof(url),
             "http://" IPSTR,
             IP2STR(&ip_info.ip));
    return url;
}

WebServerService *service_from_handle(httpd_req_t *req)
{
    auto *server = static_cast<WebServerService *>(req->user_ctx);
    return server;
}

esp_err_t handle_status_json(httpd_req_t *req)
{
    WebServerService *service = service_from_handle(req);
    if (service == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server unavailable");
    }

    const PsramString body = PsramString("{") +
                             "\"running\":" + bool_json(service->isRunning()) +
                             ",\"ap_mode\":" + bool_json(service->isApModeActive()) +
                             ",\"source\":\"" + json_escape(service->sourceSummary()) + "\"" +
                             ",\"url\":\"" + json_escape(service->primaryUrl()) + "\"" +
                             ",\"recovery_url\":\"" + json_escape(service->recoveryUrl()) + "\"" +
                             ",\"mdns_url\":\"" + json_escape(service->mdnsUrl()) + "\"" +
                             ",\"status\":\"" + json_escape(service->statusText()) + "\"}";
    return send_text(req, "application/json", body);
}

esp_err_t handle_files_json(httpd_req_t *req)
{
    WebServerService *service = service_from_handle(req);
    if (service == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server unavailable");
    }

    PsramVector<PsramString> file_entries;
    if (app_storage_ensure_sdcard_available() && is_directory_path(kSdWebRoot)) {
        append_file_entries("SD Card", kSdWebRoot, file_entries);
    }
    if (is_directory_path(kSpiffsWebRoot)) {
        append_file_entries("SPIFFS", kSpiffsWebRoot, file_entries);
    }

    PsramString files_json = "[";
    for (size_t index = 0; index < file_entries.size(); ++index) {
        if (index > 0) {
            files_json += ",";
        }
        files_json += file_entries[index];
    }
    files_json += "]";

    const PsramString body = PsramString("{") +
                             "\"live_source\":\"" + json_escape(service->sourceSummary()) + "\"" +
                             ",\"recovery_target\":\"/sdcard/web\"" +
                             ",\"files\":" + files_json + "}";
    return send_text(req, "application/json", body);
}

esp_err_t handle_upload(httpd_req_t *req)
{
    if (!app_storage_ensure_sdcard_available()) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_string(req, "text/plain; charset=utf-8", "Insert the SD card before uploading recovery files");
    }

    std::string relative_path = read_query_value(req, "path");
    if (!sanitize_relative_path(relative_path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid upload path");
    }

    if (!ensure_directory_recursive(kSdWebRoot)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create /sdcard/web");
    }

    const size_t slash = relative_path.find_last_of('/');
    if (slash != std::string::npos) {
        const std::string directory = join_path(kSdWebRoot, relative_path.substr(0, slash));
        if (!ensure_directory_recursive(directory)) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create nested upload directory");
        }
    }

    const std::string destination = join_path(kSdWebRoot, relative_path);
    FILE *file = fopen(destination.c_str(), "wb");
    if (file == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open destination file");
    }

    int remaining = req->content_len;
    char buffer[kIoBufferSize];
    while (remaining > 0) {
        const int read_size = httpd_req_recv(req, buffer, (remaining > static_cast<int>(sizeof(buffer))) ? sizeof(buffer) : remaining);
        if (read_size <= 0) {
            fclose(file);
            remove(destination.c_str());
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload stream failed");
        }
        if (fwrite(buffer, 1, read_size, file) != static_cast<size_t>(read_size)) {
            fclose(file);
            remove(destination.c_str());
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write uploaded file");
        }
        remaining -= read_size;
    }

    fclose(file);
    return send_text(req, "application/json", PsramString("{\"saved\":\"") + json_escape(relative_path) + "\"}");
}

esp_err_t handle_captive_redirect(httpd_req_t *req)
{
    const std::string destination = build_url_for_ifkey("WIFI_AP_DEF");
    if (destination.empty()) {
        return send_string(req, "text/plain; charset=utf-8", "Captive portal is waiting for AP mode IP assignment.");
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", destination.c_str());
    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t handle_static_or_embedded(httpd_req_t *req)
{
    WebServerService *service = service_from_handle(req);
    if (service == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server unavailable");
    }

    const std::string uri = strip_query(req->uri);
    if ((uri == "/generate_204") || (uri == "/hotspot-detect.html") || (uri == "/connecttest.txt") ||
        (uri == "/ncsi.txt") || (uri == "/library/test/success.html") || (uri == "/success.txt")) {
        return handle_captive_redirect(req);
    }

    if (uri == "/recovery") {
        return send_string(req, "text/html; charset=utf-8", kEmbeddedRecoveryHtml);
    }

    const WebServerService::ContentRoot root = service->resolveContentRoot(true);
    if (root == WebServerService::ContentRoot::Embedded) {
        return send_string(req, "text/html; charset=utf-8", (uri == "/") ? kEmbeddedIndexHtml : kEmbeddedRecoveryHtml);
    }

    const std::string base = (root == WebServerService::ContentRoot::SdCard) ? kSdWebRoot : kSpiffsWebRoot;
    const std::string relative = relative_path_for_uri(uri);
    const bool explicit_gzip_request = (relative.size() > 3) && (relative.rfind(".gz") == (relative.size() - 3));
    const bool accepts_gzip = request_accepts_gzip(req);

    std::vector<std::pair<std::string, bool>> candidates;
    if (!explicit_gzip_request && accepts_gzip) {
        candidates.emplace_back(join_path(base, relative + ".gz"), true);
    }
    candidates.emplace_back(join_path(base, relative), explicit_gzip_request);

    if ((relative == "index.html") && !explicit_gzip_request) {
        candidates.emplace_back(join_path(base, "index.htm"), false);
        if (accepts_gzip) {
            candidates.emplace_back(join_path(base, "index.htm.gz"), true);
        }
    }

    for (const auto &candidate : candidates) {
        if (file_exists(candidate.first)) {
            return serve_file(req, candidate.first, candidate.second);
        }
    }

    if (uri == "/") {
        return send_string(req, "text/html; charset=utf-8", kEmbeddedIndexHtml);
    }

    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Resource not found");
}

} // namespace

WebServerService &WebServerService::instance()
{
    static WebServerService service;
    return service;
}

WebServerService::WebServerService()
    : _server(nullptr),
      _running(false),
      _mdnsStarted(false),
      _lastError{}
{
}

WebServerService::~WebServerService()
{
    stop();
}

bool WebServerService::start()
{
    if (_running) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 12;
    config.stack_size = 8192;
    config.server_port = 80;
    config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

    httpd_handle_t handle = nullptr;
    esp_err_t err = httpd_start(&handle, &config);
    if (err != ESP_OK) {
        setLastError("HTTP server start failed: %s", esp_err_to_name(err));
        return false;
    }

    _server = handle;
    if (!registerHandlers()) {
        httpd_stop(static_cast<httpd_handle_t>(_server));
        _server = nullptr;
        return false;
    }

    if (!startMdns()) {
        ESP_LOGW(TAG, "mDNS registration failed; continuing without hostname advertisement");
    }

    _running = true;
    _lastError[0] = '\0';
    ESP_LOGI(TAG, "Web server started");
    return true;
}

bool WebServerService::stop()
{
    if (!_running && (_server == nullptr)) {
        return true;
    }

    stopMdns();
    if (_server != nullptr) {
        httpd_stop(static_cast<httpd_handle_t>(_server));
        _server = nullptr;
    }
    _running = false;
    ESP_LOGI(TAG, "Web server stopped");
    return true;
}

bool WebServerService::toggle()
{
    return isRunning() ? stop() : start();
}

bool WebServerService::isRunning() const
{
    return _running;
}

bool WebServerService::isApModeActive() const
{
    return has_ap_ip();
}

std::string WebServerService::statusText() const
{
    if (!_running) {
        return lastErrorText().empty() ? "Web server is stopped." : lastErrorText();
    }

    const std::string url = primaryUrl();
    if (!url.empty()) {
        return std::string("Serving ") + sourceSummary() + " at " + url;
    }

    return std::string("Server is running from ") + sourceSummary() + ". Enable Wi-Fi or AP mode for a reachable URL.";
}

std::string WebServerService::primaryUrl() const
{
    std::string url = build_url_for_ifkey("WIFI_STA_DEF");
    if (!url.empty()) {
        return url;
    }
    return build_url_for_ifkey("WIFI_AP_DEF");
}

std::string WebServerService::recoveryUrl() const
{
    const std::string url = primaryUrl();
    return url.empty() ? std::string{} : (url + "/recovery");
}

std::string WebServerService::sourceSummary() const
{
    return contentRootLabel(resolveContentRoot(true));
}

std::string WebServerService::mdnsUrl() const
{
    return _mdnsStarted ? std::string("http://") + kMdnsHostName + ".local" : std::string{};
}

bool WebServerService::registerHandlers()
{
    httpd_handle_t handle = static_cast<httpd_handle_t>(_server);
    if (handle == nullptr) {
        setLastError("HTTP server handle is invalid");
        return false;
    }

    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = handle_status_json,
        .user_ctx = this,
    };
    httpd_uri_t files_uri = {
        .uri = "/api/files",
        .method = HTTP_GET,
        .handler = handle_files_json,
        .user_ctx = this,
    };
    httpd_uri_t upload_uri = {
        .uri = "/api/upload",
        .method = HTTP_POST,
        .handler = handle_upload,
        .user_ctx = this,
    };
    httpd_uri_t static_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = handle_static_or_embedded,
        .user_ctx = this,
    };

    const httpd_uri_t handlers[] = {status_uri, files_uri, upload_uri, static_uri};
    for (const httpd_uri_t &uri : handlers) {
        const esp_err_t err = httpd_register_uri_handler(handle, &uri);
        if (err != ESP_OK) {
            setLastError("Failed to register %s: %s", uri.uri, esp_err_to_name(err));
            return false;
        }
    }
    return true;
}

bool WebServerService::startMdns()
{
    if (_mdnsStarted) {
        return true;
    }

    esp_err_t err = mdns_init();
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        setLastError("mDNS init failed: %s", esp_err_to_name(err));
        return false;
    }

    mdns_hostname_set(kMdnsHostName);
    mdns_instance_name_set(kMdnsInstanceName);

    mdns_txt_item_t txt[] = {
        {"board", "jc4880"},
        {"path", "/recovery"},
    };
    err = mdns_service_add(kMdnsInstanceName, "_http", "_tcp", 80, txt, sizeof(txt) / sizeof(txt[0]));
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        setLastError("mDNS service add failed: %s", esp_err_to_name(err));
        return false;
    }

    _mdnsStarted = true;
    return true;
}

void WebServerService::stopMdns()
{
    if (!_mdnsStarted) {
        return;
    }
    mdns_service_remove("_http", "_tcp");
    mdns_free();
    _mdnsStarted = false;
}

WebServerService::ContentRoot WebServerService::resolveContentRoot(bool allowMount) const
{
    if ((allowMount ? app_storage_ensure_sdcard_available() : app_storage_is_sdcard_mounted()) && is_directory_path(kSdWebRoot)) {
        return ContentRoot::SdCard;
    }
    if (is_directory_path(kSpiffsWebRoot)) {
        return ContentRoot::Spiffs;
    }
    return ContentRoot::Embedded;
}

std::string WebServerService::contentRootLabel(ContentRoot root) const
{
    switch (root) {
    case ContentRoot::SdCard:
        return "SD card /web";
    case ContentRoot::Spiffs:
        return "SPIFFS /web";
    case ContentRoot::Embedded:
    default:
        return "embedded recovery";
    }
}

std::string WebServerService::lastErrorText() const
{
    return (_lastError[0] == '\0') ? std::string{} : std::string(_lastError);
}

void WebServerService::setLastError(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(_lastError, sizeof(_lastError), format, args);
    va_end(args);
    ESP_LOGW(TAG, "%s", _lastError);
}