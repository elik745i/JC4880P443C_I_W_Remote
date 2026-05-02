#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <sys/stat.h>

#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "storage_access.h"

template <typename T>
class AppPsramAllocator {
public:
    using value_type = T;

    AppPsramAllocator() noexcept = default;

    template <typename U>
    AppPsramAllocator(const AppPsramAllocator<U> &) noexcept
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
    bool operator==(const AppPsramAllocator<U> &) const noexcept
    {
        return true;
    }

    template <typename U>
    bool operator!=(const AppPsramAllocator<U> &) const noexcept
    {
        return false;
    }
};

namespace app_network_cache {

using PsramString = std::basic_string<char, std::char_traits<char>, AppPsramAllocator<char>>;

enum class PayloadStorage {
    None,
    Psram,
    SdCard,
};

struct CachedPayload {
    PayloadStorage storage = PayloadStorage::None;
    std::string file_path;
    PsramString memory;
    size_t size = 0;

    void clear()
    {
        storage = PayloadStorage::None;
        file_path.clear();
        memory.clear();
        size = 0;
    }
};

inline bool is_directory_path(const char *path)
{
    struct stat info = {};
    return (path != nullptr) && (stat(path, &info) == 0) && S_ISDIR(info.st_mode);
}

inline bool file_exists(const std::string &path)
{
    struct stat info = {};
    return !path.empty() && (stat(path.c_str(), &info) == 0) && S_ISREG(info.st_mode);
}

inline bool ensure_directory_exists(const std::string &path)
{
    if (path.empty()) {
        return false;
    }

    if (is_directory_path(path.c_str())) {
        return true;
    }

    std::string partial;
    partial.reserve(path.size());
    for (size_t index = 0; index < path.size(); ++index) {
        partial.push_back(path[index]);
        const bool is_separator = (path[index] == '/');
        const bool is_last = (index + 1) == path.size();
        if (!is_separator && !is_last) {
            continue;
        }
        if (!partial.empty() && (partial.back() == '/')) {
            partial.pop_back();
        }
        if (partial.empty()) {
            partial.push_back('/');
            continue;
        }
        if ((mkdir(partial.c_str(), 0775) != 0) && (errno != EEXIST)) {
            return false;
        }
    }

    return is_directory_path(path.c_str()) || ((mkdir(path.c_str(), 0775) == 0) || (errno == EEXIST));
}

inline std::string percent_encode_component(const std::string &value)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(value.size() * 3);
    for (unsigned char ch: value) {
        const bool safe = ((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z')) ||
                          ((ch >= '0') && (ch <= '9')) || (ch == '-') || (ch == '_') ||
                          (ch == '.') || (ch == '~');
        if (safe) {
            output.push_back(static_cast<char>(ch));
            continue;
        }
        output.push_back('%');
        output.push_back(kHex[(ch >> 4) & 0x0F]);
        output.push_back(kHex[ch & 0x0F]);
    }
    return output;
}

inline std::string percent_decode_component(const std::string &value)
{
    auto decode_nibble = [](char ch) -> int {
        if ((ch >= '0') && (ch <= '9')) {
            return ch - '0';
        }
        if ((ch >= 'a') && (ch <= 'f')) {
            return 10 + (ch - 'a');
        }
        if ((ch >= 'A') && (ch <= 'F')) {
            return 10 + (ch - 'A');
        }
        return -1;
    };

    std::string output;
    output.reserve(value.size());
    for (size_t index = 0; index < value.size(); ++index) {
        if ((value[index] == '%') && ((index + 2) < value.size())) {
            const int hi = decode_nibble(value[index + 1]);
            const int lo = decode_nibble(value[index + 2]);
            if ((hi >= 0) && (lo >= 0)) {
                output.push_back(static_cast<char>((hi << 4) | lo));
                index += 2;
                continue;
            }
        }
        output.push_back(value[index] == '+' ? ' ' : value[index]);
    }
    return output;
}

inline uint32_t fnv1a_hash(const std::string &value)
{
    uint32_t hash = 2166136261u;
    for (unsigned char ch: value) {
        hash ^= ch;
        hash *= 16777619u;
    }
    return hash;
}

inline std::string make_cache_path(const char *cache_dir, const std::string &cache_key, const char *suffix)
{
    char hash_buffer[16] = {};
    std::snprintf(hash_buffer, sizeof(hash_buffer), "%08x", static_cast<unsigned int>(fnv1a_hash(cache_key)));
    return std::string(cache_dir) + "/" + hash_buffer + suffix;
}

inline bool fetch_text_with_sd_fallback(const std::string &url, const char *cache_dir, const std::string &cache_key,
                                        size_t sd_threshold_bytes, CachedPayload &output, std::string &error,
                                        bool prefer_cached_sd = false, int timeout_ms = 15000)
{
    output.clear();
    error.clear();

    const bool sd_ready = app_storage_ensure_sdcard_available() && ensure_directory_exists(cache_dir);
    const std::string cache_path = sd_ready ? make_cache_path(cache_dir, cache_key, ".cache") : std::string();
    const std::string temp_path = sd_ready ? (cache_path + ".tmp") : std::string();

    if (prefer_cached_sd && file_exists(cache_path)) {
        output.storage = PayloadStorage::SdCard;
        output.file_path = cache_path;
        struct stat info = {};
        if (stat(cache_path.c_str(), &info) == 0) {
            output.size = static_cast<size_t>(info.st_size);
        }
        return true;
    }

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = timeout_ms;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.user_agent = "JC4880P4Remote/1.3.1";
    config.disable_auto_redirect = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        error = "Failed to create HTTP client";
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        error = std::string("HTTP open failed: ") + esp_err_to_name(err);
        esp_http_client_cleanup(client);
        return false;
    }

    const int status_code = esp_http_client_fetch_headers(client);
    if (status_code < 0) {
        error = std::string("HTTP header fetch failed: ") + esp_err_to_name(static_cast<esp_err_t>(status_code));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    const int content_length = esp_http_client_get_content_length(client);
    FILE *file = nullptr;
    PsramString memory;
    if ((content_length > 0) && (content_length <= static_cast<int>(sd_threshold_bytes))) {
        memory.reserve(static_cast<size_t>(content_length));
    }

    char buffer[2048] = {};
    size_t total_bytes = 0;
    bool success = true;
    while (true) {
        const int bytes_read = esp_http_client_read(client, buffer, sizeof(buffer));
        if (bytes_read < 0) {
            error = "HTTP body read failed";
            success = false;
            break;
        }
        if (bytes_read == 0) {
            break;
        }

        total_bytes += static_cast<size_t>(bytes_read);
        if ((file == nullptr) && sd_ready && (total_bytes > sd_threshold_bytes)) {
            file = std::fopen(temp_path.c_str(), "wb");
            if (file == nullptr) {
                error = std::string("Failed to open cache file: ") + temp_path;
                success = false;
                break;
            }
            if (!memory.empty() && (std::fwrite(memory.data(), 1, memory.size(), file) != memory.size())) {
                error = "Failed to flush cached body to SD card";
                success = false;
                break;
            }
            memory.clear();
        }

        if (file != nullptr) {
            if (std::fwrite(buffer, 1, static_cast<size_t>(bytes_read), file) != static_cast<size_t>(bytes_read)) {
                error = "Failed to write cache file";
                success = false;
                break;
            }
        } else {
            memory.append(buffer, static_cast<size_t>(bytes_read));
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (file != nullptr) {
        std::fclose(file);
        file = nullptr;
    }

    if (!success) {
        if (!temp_path.empty()) {
            std::remove(temp_path.c_str());
        }
        return false;
    }

    if (!temp_path.empty() && file_exists(temp_path)) {
        std::remove(cache_path.c_str());
        if (std::rename(temp_path.c_str(), cache_path.c_str()) != 0) {
            error = "Failed to finalize SD cache file";
            std::remove(temp_path.c_str());
            return false;
        }
        output.storage = PayloadStorage::SdCard;
        output.file_path = cache_path;
        output.size = total_bytes;
        return true;
    }

    output.storage = PayloadStorage::Psram;
    output.memory = std::move(memory);
    output.size = total_bytes;
    return true;
}

inline bool load_cached_text(const CachedPayload &payload, std::string &text, size_t max_bytes = 65536)
{
    text.clear();
    if (payload.storage == PayloadStorage::Psram) {
        text.assign(payload.memory.data(), payload.memory.size());
        if (text.size() > max_bytes) {
            text.resize(max_bytes);
        }
        return true;
    }

    if ((payload.storage != PayloadStorage::SdCard) || payload.file_path.empty()) {
        return false;
    }

    FILE *file = std::fopen(payload.file_path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }

    char buffer[1024] = {};
    while (text.size() < max_bytes) {
        const size_t to_read = std::min(sizeof(buffer), max_bytes - text.size());
        const size_t bytes_read = std::fread(buffer, 1, to_read, file);
        if (bytes_read == 0) {
            break;
        }
        text.append(buffer, bytes_read);
    }

    std::fclose(file);
    return true;
}

inline const char *storage_label(PayloadStorage storage)
{
    switch (storage) {
    case PayloadStorage::SdCard:
        return "SD cache";
    case PayloadStorage::Psram:
        return "PSRAM cache";
    default:
        return "No cache";
    }
}

} // namespace app_network_cache