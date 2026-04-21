#include "music_library.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <new>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include <errno.h>
#include <unistd.h>

#include "audio_player.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "storage_access.h"

namespace {

template <typename T>
class PsramAllocator {
public:
    using value_type = T;

    PsramAllocator() noexcept = default;

    template <typename U>
    PsramAllocator(const PsramAllocator<U> &) noexcept
    {
    }

    T *allocate(std::size_t count)
    {
        if (count > (static_cast<std::size_t>(-1) / sizeof(T))) {
            std::abort();
        }

        void *ptr = heap_caps_malloc(count * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ptr == nullptr) {
            ptr = heap_caps_malloc(count * sizeof(T), MALLOC_CAP_8BIT);
        }
        if (ptr == nullptr) {
            std::abort();
        }

        return static_cast<T *>(ptr);
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

using MusicString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

template <typename T>
using PsramVector = std::vector<T, PsramAllocator<T>>;

constexpr const char *kSdRoot = "/sdcard";
constexpr const char *kSpiffsRoot = BSP_SPIFFS_MOUNT_POINT;
constexpr const char *kMusicStateDir = "/sdcard/.jc4880_music";
constexpr const char *kPlaylistFilePath = "/sdcard/.jc4880_music/playlist_v2.txt";
constexpr const char *kPlaylistTempFilePath = "/sdcard/.jc4880_music/playlist_v2.tmp";
constexpr const char *kLegacyPlaylistFilePath = BSP_SPIFFS_MOUNT_POINT "/.jc4880_music_playlist_v2.txt";
constexpr const char *kLegacyPlaylistTempFilePath = BSP_SPIFFS_MOUNT_POINT "/.jc4880_music_playlist_v2.tmp";
constexpr const char *kSdIndexFilePath = "/sdcard/.jc4880_music/sd_index_v2.txt";
constexpr const char *kSdIndexTempFilePath = "/sdcard/.jc4880_music/sd_index_v2.tmp";
constexpr const char *kLegacySdIndexFilePath = BSP_SPIFFS_MOUNT_POINT "/.jc4880_music_sd_index_v2.txt";
constexpr const char *kLegacySdIndexTempFilePath = BSP_SPIFFS_MOUNT_POINT "/.jc4880_music_sd_index_v2.tmp";
constexpr const char *kFolderPlaylistFileName = ".jc4880_music_folder_playlist_v2.txt";
constexpr const char *kFolderPlaylistTempFileName = ".jc4880_music_folder_playlist_v2.tmp";
constexpr const char *kPlaylistMagic = "JC4880_PLAYLIST_V2";
constexpr const char *kIndexMagic = "JC4880_SD_INDEX_V2";
constexpr const char *kFolderPlaylistMagic = "JC4880_FOLDER_PLAYLIST_V2";
constexpr const char *kExternalIndexFileName = ".index";
constexpr const char *kDownloadDir = "/sdcard/Downloads/Music";

struct LibrarySignature {
    uint32_t supportedFileCount;
    uint64_t totalSize;
    uint64_t newestMtime;
    uint64_t contentHash;
};

struct TrackInfo {
    MusicString title;
    MusicString artist;
    MusicString genre;
    MusicString info;
    MusicString path;
    uint32_t durationSeconds;
    bool favorite;
    bool infoResolved;
};

struct BrowserEntry {
    MusicString name;
    MusicString path;
    MusicString meta;
    bool isDirectory;
    bool supported;
    bool canAdd;
};

struct BrowserState {
    music_library_storage_root_t root;
    MusicString currentPath;
    PsramVector<BrowserEntry> entries;
    bool available;
};

struct BrowserAddRequest {
    music_library_storage_root_t root;
    MusicString path;
    MusicString name;
    bool isDirectory;
    bool forceRebuild;
};

struct BrowserRefreshRequest {
    music_library_storage_root_t root;
    MusicString path;
    bool reindexFolder;
};

struct StatusProgressContext {
    const char *prefix;
    bool useBrowserAddStatus;
    uint32_t lastReported;
};

using ScanProgressCallback = void (*)(uint32_t scannedFiles, uint32_t trackCount, void *context);

static const char *TAG = "music_library";
static const char *kEmptyTitle = "Playlist is empty";
static const char *kEmptyArtist = "Use Playlist to add files, folders, or a download URL";
static const char *kEmptyGenre = "MP3, AAC, M4A, FLAC, WAV";

static PsramVector<TrackInfo> s_tracks;
static PsramVector<TrackInfo> s_sdIndexTracks;
static uint32_t s_currentIndex = 0;
static bool s_shuffleEnabled = false;
static bool s_cycleEnabled = false;
static MusicString s_lastMessage = "Playlist ready";
static BrowserState s_sdBrowser = {MUSIC_LIBRARY_STORAGE_SD, kSdRoot, {}, false};
static BrowserState s_spiffsBrowser = {MUSIC_LIBRARY_STORAGE_SPIFFS, kSpiffsRoot, {}, false};
static music_library_browser_mode_t s_browserMode = MUSIC_LIBRARY_BROWSER_MODE_FILE;
static std::atomic<music_library_index_state_t> s_indexState{MUSIC_LIBRARY_INDEX_STATE_IDLE};
static std::atomic<uint32_t> s_indexScannedFiles{0};
static std::atomic<uint32_t> s_indexedTrackCount{0};
static std::atomic<bool> s_hasCachedIndex{false};
static std::atomic<music_library_browser_add_state_t> s_browserAddState{MUSIC_LIBRARY_BROWSER_ADD_STATE_IDLE};
static std::atomic<music_library_browser_refresh_state_t> s_browserRefreshState{MUSIC_LIBRARY_BROWSER_REFRESH_STATE_IDLE};
static std::atomic<music_library_download_state_t> s_downloadState{MUSIC_LIBRARY_DOWNLOAD_STATE_IDLE};
static std::atomic<int32_t> s_downloadProgress{-1};
static char s_browserAddStatus[192] = "Idle";
static char s_browserRefreshStatus[192] = "Idle";
static char s_downloadStatus[192] = "Idle";

bool ensure_music_audio_ready();
bool ensure_root_available(music_library_storage_root_t root, bool allowMount);
BrowserState &browser_state(music_library_storage_root_t root);
const char *root_path(music_library_storage_root_t root);
const char *root_label(music_library_storage_root_t root);
bool is_supported_extension(const MusicString &path);
MusicString trim_extension(const MusicString &name);
MusicString filename_from_path(const MusicString &path);
MusicString make_parent_label(const MusicString &path);
MusicString make_genre_label(const MusicString &path, music_library_storage_root_t root);
MusicString trim_ascii_whitespace(const MusicString &value);
MusicString escape_index_field(const MusicString &value);
PsramVector<MusicString> split_index_line(const MusicString &line);
PsramVector<MusicString> split_external_index_csv_line(const MusicString &line);
MusicString folder_playlist_cache_path(const MusicString &directoryPath, bool tempFile);
bool parse_track_line(const MusicString &line, TrackInfo &track);
bool save_track_file(const char *filePath, const char *tempPath, const char *magic, const PsramVector<TrackInfo> &tracks,
                     const LibrarySignature *signature);
bool load_track_file(const char *filePath, const char *magic, PsramVector<TrackInfo> &tracks, LibrarySignature *signature);
bool prefer_sd_metadata_storage(bool allowMount);
bool load_track_file_prefer_sd(const char *sdFilePath, const char *sdTempPath, const char *legacyFilePath,
                               const char *legacyTempPath, const char *magic, PsramVector<TrackInfo> &tracks,
                               LibrarySignature *signature);
bool save_track_file_prefer_sd(const char *sdFilePath, const char *sdTempPath, const char *legacyFilePath,
                               const char *legacyTempPath, const char *magic, const PsramVector<TrackInfo> &tracks,
                               const LibrarySignature *signature);
bool load_external_index_tracks_for_directory(const MusicString &directoryPath, music_library_storage_root_t root,
                                              PsramVector<TrackInfo> &tracks);
bool folder_playlist_cache_exists(const MusicString &directoryPath);
bool load_folder_playlist_cache(const MusicString &directoryPath, PsramVector<TrackInfo> &tracks);
bool save_folder_playlist_cache(const MusicString &directoryPath, const PsramVector<TrackInfo> &tracks);
void init_signature(LibrarySignature &signature);
void mix_signature_u64(LibrarySignature &signature, uint64_t value);
void update_signature_for_track(LibrarySignature &signature, const MusicString &path, const struct stat &st);
bool parse_signature_line(const MusicString &line, LibrarySignature &signature);
bool signatures_match(const LibrarySignature &left, const LibrarySignature &right);
void scan_directory_recursive(const MusicString &path, music_library_storage_root_t root, PsramVector<TrackInfo> &tracks,
                              LibrarySignature *signature, bool reportProgress, ScanProgressCallback progressCallback,
                              void *progressContext);
bool refresh_sd_index_cache(bool forceRebuild);
bool load_playlist();
bool save_playlist();
bool load_playlist_file(const char *filePath, uint32_t *currentIndex, bool *shuffleEnabled, bool *cycleEnabled,
                        PsramVector<TrackInfo> &tracks);
bool save_playlist_file(const char *filePath, const char *tempPath, const PsramVector<TrackInfo> &tracks,
                        uint32_t currentIndex, bool shuffleEnabled, bool cycleEnabled);
bool playlist_contains_path(const MusicString &path);
bool build_track_from_path(const MusicString &path, music_library_storage_root_t root, TrackInfo &track);
bool add_track_to_playlist(const TrackInfo &track, bool persist);
bool add_path_to_playlist(const MusicString &path, music_library_storage_root_t root, bool persist);
bool append_tracks_to_playlist(const PsramVector<TrackInfo> &candidates, uint32_t *processedCount, uint32_t *addedCount,
                               uint32_t *duplicateCount, uint32_t *missingCount);
bool collect_folder_tracks(const MusicString &path, music_library_storage_root_t root, bool forceRebuild,
                           PsramVector<TrackInfo> &candidates, bool *usedCache, ScanProgressCallback progressCallback,
                           void *progressContext);
bool add_folder_to_playlist(const MusicString &path, music_library_storage_root_t root, bool forceRebuild);
bool rebuild_folder_playlist_cache(const MusicString &path, music_library_storage_root_t root);
bool refresh_browser(BrowserState &state);
bool is_path_directory(const MusicString &path);
MusicString format_size(uint64_t size);
bool ensure_directory_exists(const MusicString &path);
void set_last_message(const MusicString &message);
void set_browser_add_status(const MusicString &message);
void set_browser_refresh_status(const MusicString &message);
void set_download_status(const MusicString &message);
void folder_scan_progress_cb(uint32_t scannedFiles, uint32_t trackCount, void *context);
void fill_track_metadata(TrackInfo &track);
void populate_track_runtime_fields(TrackInfo &track);
MusicString make_track_info_label(const TrackInfo &track, uint64_t fileSizeBytes);
void music_library_index_task(void *context);
void music_library_browser_add_task(void *context);
void music_library_browser_refresh_task(void *context);
void music_library_download_task(void *context);

bool ensure_music_audio_ready()
{
    esp_err_t ret = bsp_extra_codec_dev_stop();
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE)) {
        ESP_LOGW(TAG, "Failed to stop codec output path before music playback: %s", esp_err_to_name(ret));
        return false;
    }

    const esp_err_t codec_init_err = bsp_extra_codec_init();
    if (codec_init_err != ESP_OK) {
        ESP_LOGW(TAG, "Music playback unavailable due to limited audio resources: %s", esp_err_to_name(codec_init_err));
        return false;
    }

    ret = bsp_extra_player_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize shared audio player for music playback: %s", esp_err_to_name(ret));
        return false;
    }

    ret = bsp_extra_codec_dev_resume();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to resume codec output path for music playback: %s", esp_err_to_name(ret));
        return false;
    }

    const int currentVolume = bsp_extra_codec_volume_get();
    const int restoreVolume = currentVolume >= 0 ? currentVolume : 60;
    ret = bsp_extra_codec_volume_set(restoreVolume, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to restore music playback volume: %s", esp_err_to_name(ret));
        return false;
    }

    ret = bsp_extra_codec_mute_set(false);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to unmute music playback path: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

BrowserState &browser_state(music_library_storage_root_t root)
{
    return (root == MUSIC_LIBRARY_STORAGE_SD) ? s_sdBrowser : s_spiffsBrowser;
}

const char *root_path(music_library_storage_root_t root)
{
    return (root == MUSIC_LIBRARY_STORAGE_SD) ? kSdRoot : kSpiffsRoot;
}

const char *root_label(music_library_storage_root_t root)
{
    return (root == MUSIC_LIBRARY_STORAGE_SD) ? "SD Card" : "SPIFFS";
}

bool ensure_root_available(music_library_storage_root_t root, bool allowMount)
{
    if (root == MUSIC_LIBRARY_STORAGE_SPIFFS) {
        return is_path_directory(kSpiffsRoot);
    }

    if (allowMount && !app_storage_ensure_sdcard_available()) {
        return false;
    }

    return app_storage_is_sdcard_mounted() && is_path_directory(kSdRoot);
}

bool is_supported_extension(const MusicString &path)
{
    static const char *extensions[] = {".mp3", ".mpga", ".aac", ".flac", ".m4a", ".mp4", ".wav", ".wave"};
    const size_t dot = path.find_last_of('.');
    if (dot == MusicString::npos) {
        return false;
    }

    MusicString extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    for (const char *candidate : extensions) {
        if (extension == candidate) {
            return true;
        }
    }
    return false;
}

MusicString trim_extension(const MusicString &name)
{
    const size_t dot = name.find_last_of('.');
    return (dot == MusicString::npos) ? name : name.substr(0, dot);
}

MusicString filename_from_path(const MusicString &path)
{
    const size_t slash = path.find_last_of('/');
    return (slash == MusicString::npos) ? path : path.substr(slash + 1);
}

MusicString make_parent_label(const MusicString &path)
{
    const size_t slash = path.find_last_of('/');
    if ((slash == MusicString::npos) || (slash == 0)) {
        return "Root";
    }

    const size_t previous = path.find_last_of('/', slash - 1);
    if (previous == MusicString::npos) {
        return path.substr(0, slash);
    }

    return path.substr(previous + 1, slash - previous - 1);
}

MusicString make_genre_label(const MusicString &path, music_library_storage_root_t root)
{
    const size_t dot = path.find_last_of('.');
    MusicString extension = (dot == MusicString::npos) ? "Audio" : path.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return extension + "  вЂў  " + root_label(root);
}

MusicString trim_ascii_whitespace(const MusicString &value)
{
    const size_t start = value.find_first_not_of(" \t\r\n\0", 0);
    if (start == MusicString::npos) {
        return "";
    }

    const size_t end = value.find_last_not_of(" \t\r\n\0");
    return value.substr(start, end - start + 1);
}

MusicString escape_index_field(const MusicString &value)
{
    MusicString escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '|':
            escaped += "\\|";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

PsramVector<MusicString> split_index_line(const MusicString &line)
{
    PsramVector<MusicString> fields;
    MusicString current;
    current.reserve(line.size());
    bool escaping = false;

    for (char ch : line) {
        if (!escaping) {
            if (ch == '\\') {
                escaping = true;
                continue;
            }
            if (ch == '|') {
                fields.push_back(current);
                current.clear();
                continue;
            }
            current.push_back(ch);
            continue;
        }

        switch (ch) {
        case 'n':
            current.push_back('\n');
            break;
        case 'r':
            current.push_back('\r');
            break;
        case 't':
            current.push_back('\t');
            break;
        case '|':
        case '\\':
            current.push_back(ch);
            break;
        default:
            current.push_back(ch);
            break;
        }
        escaping = false;
    }

    if (escaping) {
        current.push_back('\\');
    }
    fields.push_back(current);
    return fields;
}

PsramVector<MusicString> split_external_index_csv_line(const MusicString &line)
{
    PsramVector<MusicString> fields;
    MusicString current;
    current.reserve(line.size());

    for (char ch : line) {
        if (ch == ',') {
            fields.push_back(trim_ascii_whitespace(current));
            current.clear();
            continue;
        }

        if (ch != '\r' && ch != '\n') {
            current.push_back(ch);
        }
    }

    fields.push_back(trim_ascii_whitespace(current));
    return fields;
}

MusicString folder_playlist_cache_path(const MusicString &directoryPath, bool tempFile)
{
    return directoryPath + "/" + (tempFile ? kFolderPlaylistTempFileName : kFolderPlaylistFileName);
}

bool parse_track_line(const MusicString &line, TrackInfo &track)
{
    const PsramVector<MusicString> fields = split_index_line(line);
    if ((fields.size() != 5) && (fields.size() != 6)) {
        return false;
    }

    char *endPtr = nullptr;
    const unsigned long parsedDuration = std::strtoul(fields[4].c_str(), &endPtr, 10);
    if ((endPtr == nullptr) || (*endPtr != '\0')) {
        return false;
    }

    track.title = fields[0];
    track.artist = fields[1];
    track.genre = fields[2];
    track.path = fields[3];
    track.durationSeconds = static_cast<uint32_t>(parsedDuration);
    track.favorite = (fields.size() == 6) && (fields[5] == "1");
    return true;
}

bool save_playlist_file(const char *filePath, const char *tempPath, const PsramVector<TrackInfo> &tracks,
                        uint32_t currentIndex, bool shuffleEnabled, bool cycleEnabled)
{
    FILE *file = std::fopen(tempPath, "wb");
    if (file == nullptr) {
        return false;
    }

    const uint32_t persistedIndex = tracks.empty() ? 0 : std::min<uint32_t>(currentIndex, static_cast<uint32_t>(tracks.size() - 1));
    bool ok = std::fprintf(file, "%s\n", kPlaylistMagic) >= 0;
    if (ok) {
        ok = std::fprintf(file, "STATE|%lu|%d|%d\n",
                          static_cast<unsigned long>(persistedIndex),
                          shuffleEnabled ? 1 : 0,
                          cycleEnabled ? 1 : 0) >= 0;
    }

    for (const TrackInfo &track : tracks) {
        if (!ok) {
            break;
        }
        ok = std::fprintf(file,
                          "%s|%s|%s|%s|%lu|%d\n",
                          escape_index_field(track.title).c_str(),
                          escape_index_field(track.artist).c_str(),
                          escape_index_field(track.genre).c_str(),
                          escape_index_field(track.path).c_str(),
                          static_cast<unsigned long>(track.durationSeconds),
                          track.favorite ? 1 : 0) >= 0;
    }

    if (std::fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        std::remove(tempPath);
        return false;
    }

    std::remove(filePath);
    if (std::rename(tempPath, filePath) != 0) {
        std::remove(tempPath);
        return false;
    }

    return true;
}

bool load_playlist_file(const char *filePath, uint32_t *currentIndex, bool *shuffleEnabled, bool *cycleEnabled,
                        PsramVector<TrackInfo> &tracks)
{
    tracks.clear();
    if (currentIndex != nullptr) {
        *currentIndex = 0;
    }
    if (shuffleEnabled != nullptr) {
        *shuffleEnabled = false;
    }
    if (cycleEnabled != nullptr) {
        *cycleEnabled = false;
    }

    FILE *file = std::fopen(filePath, "rb");
    if (file == nullptr) {
        return false;
    }

    char line[2048] = {};
    if (std::fgets(line, sizeof(line), file) == nullptr) {
        std::fclose(file);
        return false;
    }
    if (trim_ascii_whitespace(line) != kPlaylistMagic) {
        std::fclose(file);
        return false;
    }

    while (std::fgets(line, sizeof(line), file) != nullptr) {
        const MusicString entryLine = trim_ascii_whitespace(line);
        if (entryLine.empty()) {
            continue;
        }

        const PsramVector<MusicString> fields = split_index_line(entryLine);
        if ((fields.size() == 4) && (fields[0] == "STATE")) {
            if (currentIndex != nullptr) {
                *currentIndex = static_cast<uint32_t>(std::strtoul(fields[1].c_str(), nullptr, 10));
            }
            if (shuffleEnabled != nullptr) {
                *shuffleEnabled = (fields[2] == "1");
            }
            if (cycleEnabled != nullptr) {
                *cycleEnabled = (fields[3] == "1");
            }
            continue;
        }

        TrackInfo track = {};
        if (!parse_track_line(entryLine, track)) {
            std::fclose(file);
            return false;
        }
        tracks.push_back(std::move(track));
    }

    std::fclose(file);
    return true;
}

void init_signature(LibrarySignature &signature)
{
    signature.supportedFileCount = 0;
    signature.totalSize = 0;
    signature.newestMtime = 0;
    signature.contentHash = 1469598103934665603ULL;
}

void mix_signature_u64(LibrarySignature &signature, uint64_t value)
{
    for (size_t shift = 0; shift < sizeof(value) * 8; shift += 8) {
        signature.contentHash ^= (value >> shift) & 0xFFULL;
        signature.contentHash *= 1099511628211ULL;
    }
}

void update_signature_for_track(LibrarySignature &signature, const MusicString &path, const struct stat &st)
{
    signature.supportedFileCount++;
    signature.totalSize += static_cast<uint64_t>(st.st_size);
    signature.newestMtime = std::max(signature.newestMtime, static_cast<uint64_t>(st.st_mtime));

    for (unsigned char ch : path) {
        signature.contentHash ^= static_cast<uint64_t>(ch);
        signature.contentHash *= 1099511628211ULL;
    }
    mix_signature_u64(signature, static_cast<uint64_t>(st.st_size));
    mix_signature_u64(signature, static_cast<uint64_t>(st.st_mtime));
}

bool parse_signature_line(const MusicString &line, LibrarySignature &signature)
{
    const PsramVector<MusicString> fields = split_index_line(line);
    if ((fields.size() != 5) || (fields[0] != "SIG")) {
        return false;
    }

    char *endPtr = nullptr;
    signature.supportedFileCount = static_cast<uint32_t>(std::strtoul(fields[1].c_str(), &endPtr, 10));
    if ((endPtr == nullptr) || (*endPtr != '\0')) {
        return false;
    }
    signature.totalSize = std::strtoull(fields[2].c_str(), &endPtr, 10);
    if ((endPtr == nullptr) || (*endPtr != '\0')) {
        return false;
    }
    signature.newestMtime = std::strtoull(fields[3].c_str(), &endPtr, 10);
    if ((endPtr == nullptr) || (*endPtr != '\0')) {
        return false;
    }
    signature.contentHash = std::strtoull(fields[4].c_str(), &endPtr, 10);
    return (endPtr != nullptr) && (*endPtr == '\0');
}

bool signatures_match(const LibrarySignature &left, const LibrarySignature &right)
{
    return (left.supportedFileCount == right.supportedFileCount) &&
           (left.totalSize == right.totalSize) &&
           (left.newestMtime == right.newestMtime) &&
           (left.contentHash == right.contentHash);
}

bool save_track_file(const char *filePath, const char *tempPath, const char *magic, const PsramVector<TrackInfo> &tracks,
                     const LibrarySignature *signature)
{
    FILE *file = std::fopen(tempPath, "wb");
    if (file == nullptr) {
        return false;
    }

    bool ok = std::fprintf(file, "%s\n", magic) >= 0;
    if (ok && (signature != nullptr)) {
        ok = std::fprintf(file,
                          "SIG|%u|%llu|%llu|%llu\n",
                          signature->supportedFileCount,
                          static_cast<unsigned long long>(signature->totalSize),
                          static_cast<unsigned long long>(signature->newestMtime),
                          static_cast<unsigned long long>(signature->contentHash)) >= 0;
    }

    for (const TrackInfo &track : tracks) {
        if (!ok) {
            break;
        }
        ok = std::fprintf(file,
                          "%s|%s|%s|%s|%lu\n",
                          escape_index_field(track.title).c_str(),
                          escape_index_field(track.artist).c_str(),
                          escape_index_field(track.genre).c_str(),
                          escape_index_field(track.path).c_str(),
                          static_cast<unsigned long>(track.durationSeconds)) >= 0;
    }

    if (std::fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        std::remove(tempPath);
        return false;
    }

    std::remove(filePath);
    if (std::rename(tempPath, filePath) != 0) {
        std::remove(tempPath);
        return false;
    }
    return true;
}

bool load_track_file(const char *filePath, const char *magic, PsramVector<TrackInfo> &tracks, LibrarySignature *signature)
{
    tracks.clear();
    if (signature != nullptr) {
        init_signature(*signature);
    }

    FILE *file = std::fopen(filePath, "rb");
    if (file == nullptr) {
        return false;
    }

    char line[2048] = {};
    if (std::fgets(line, sizeof(line), file) == nullptr) {
        std::fclose(file);
        return false;
    }
    if (trim_ascii_whitespace(line) != magic) {
        std::fclose(file);
        return false;
    }

    if (signature != nullptr) {
        if ((std::fgets(line, sizeof(line), file) == nullptr) ||
            !parse_signature_line(trim_ascii_whitespace(line), *signature)) {
            std::fclose(file);
            return false;
        }
    }

    while (std::fgets(line, sizeof(line), file) != nullptr) {
        const MusicString entryLine = trim_ascii_whitespace(line);
        if (entryLine.empty()) {
            continue;
        }

        TrackInfo track = {};
        if (!parse_track_line(entryLine, track)) {
            std::fclose(file);
            return false;
        }
        fill_track_metadata(track);
        tracks.push_back(std::move(track));
    }

    std::fclose(file);
    return true;
}

bool prefer_sd_metadata_storage(bool allowMount)
{
    const bool sdAvailable = allowMount ? app_storage_ensure_sdcard_available() : app_storage_is_sdcard_mounted();
    return sdAvailable && ensure_directory_exists(kMusicStateDir);
}

bool load_track_file_prefer_sd(const char *sdFilePath, const char *sdTempPath, const char *legacyFilePath,
                               const char *legacyTempPath, const char *magic, PsramVector<TrackInfo> &tracks,
                               LibrarySignature *signature)
{
    if (prefer_sd_metadata_storage(true)) {
        if (load_track_file(sdFilePath, magic, tracks, signature)) {
            return true;
        }

        if (load_track_file(legacyFilePath, magic, tracks, signature)) {
            if (save_track_file(sdFilePath, sdTempPath, magic, tracks, signature)) {
                std::remove(legacyFilePath);
                std::remove(legacyTempPath);
                ESP_LOGI(TAG, "Migrated %s from SPIFFS to SD card", magic);
            }
            return true;
        }

        return false;
    }

    return load_track_file(legacyFilePath, magic, tracks, signature);
}

bool save_track_file_prefer_sd(const char *sdFilePath, const char *sdTempPath, const char *legacyFilePath,
                               const char *legacyTempPath, const char *magic, const PsramVector<TrackInfo> &tracks,
                               const LibrarySignature *signature)
{
    if (prefer_sd_metadata_storage(true)) {
        if (save_track_file(sdFilePath, sdTempPath, magic, tracks, signature)) {
            std::remove(legacyFilePath);
            std::remove(legacyTempPath);
            return true;
        }

        ESP_LOGW(TAG, "Failed to save %s on SD card, falling back to SPIFFS", magic);
    }

    return save_track_file(legacyFilePath, legacyTempPath, magic, tracks, signature);
}

bool load_external_index_tracks_for_directory(const MusicString &directoryPath, music_library_storage_root_t root,
                                              PsramVector<TrackInfo> &tracks)
{
    tracks.clear();

    const MusicString indexPath = directoryPath + "/" + kExternalIndexFileName;
    FILE *file = std::fopen(indexPath.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }

    char line[2048] = {};
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        const MusicString entryLine = trim_ascii_whitespace(line);
        if (entryLine.empty()) {
            continue;
        }

        const PsramVector<MusicString> fields = split_external_index_csv_line(entryLine);
        if (fields.empty()) {
            continue;
        }

        const MusicString fileName = trim_ascii_whitespace(fields[0]);
        if (fileName.empty() || (fileName == kExternalIndexFileName)) {
            continue;
        }

        const MusicString fullPath = directoryPath + "/" + fileName;
        struct stat st = {};
        if ((stat(fullPath.c_str(), &st) != 0) || S_ISDIR(st.st_mode) || !is_supported_extension(fullPath)) {
            continue;
        }

        TrackInfo track = {};
        track.path = fullPath;
        track.title = trim_extension(fileName);
        track.artist = make_parent_label(fullPath);
        track.genre = make_genre_label(fullPath, root);
        track.durationSeconds = 180;
        tracks.push_back(std::move(track));
    }

    std::fclose(file);
    return !tracks.empty();
}

bool folder_playlist_cache_exists(const MusicString &directoryPath)
{
    const MusicString cachePath = folder_playlist_cache_path(directoryPath, false);
    return access(cachePath.c_str(), F_OK) == 0;
}

bool load_folder_playlist_cache(const MusicString &directoryPath, PsramVector<TrackInfo> &tracks)
{
    const MusicString cachePath = folder_playlist_cache_path(directoryPath, false);
    return load_track_file(cachePath.c_str(), kFolderPlaylistMagic, tracks, nullptr);
}

bool save_folder_playlist_cache(const MusicString &directoryPath, const PsramVector<TrackInfo> &tracks)
{
    const MusicString cachePath = folder_playlist_cache_path(directoryPath, false);
    const MusicString tempPath = folder_playlist_cache_path(directoryPath, true);
    return save_track_file(cachePath.c_str(), tempPath.c_str(), kFolderPlaylistMagic, tracks, nullptr);
}

bool is_path_directory(const MusicString &path)
{
    struct stat st = {};
    return (stat(path.c_str(), &st) == 0) && S_ISDIR(st.st_mode);
}

MusicString format_size(uint64_t size)
{
    char buffer[32] = {};
    if (size >= (1024ULL * 1024ULL)) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(size) / (1024.0 * 1024.0));
    } else if (size >= 1024ULL) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(size) / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(size));
    }
    return buffer;
}

void set_last_message(const MusicString &message)
{
    s_lastMessage = message;
    ESP_LOGI(TAG, "%s", message.c_str());
}

void set_browser_add_status(const MusicString &message)
{
    std::snprintf(s_browserAddStatus, sizeof(s_browserAddStatus), "%s", message.c_str());
}

void set_browser_refresh_status(const MusicString &message)
{
    std::snprintf(s_browserRefreshStatus, sizeof(s_browserRefreshStatus), "%s", message.c_str());
}

void set_download_status(const MusicString &message)
{
    std::snprintf(s_downloadStatus, sizeof(s_downloadStatus), "%s", message.c_str());
}

void folder_scan_progress_cb(uint32_t scannedFiles, uint32_t trackCount, void *context)
{
    StatusProgressContext *statusContext = static_cast<StatusProgressContext *>(context);
    if (statusContext == nullptr) {
        return;
    }
    if ((scannedFiles != 0) && (scannedFiles == statusContext->lastReported)) {
        return;
    }
    if ((scannedFiles != 0) && (statusContext->lastReported != 0) && ((scannedFiles - statusContext->lastReported) < 8)) {
        return;
    }

    statusContext->lastReported = scannedFiles;
    char message[160] = {};
    std::snprintf(message,
                  sizeof(message),
                  "%s %lu files, %lu tracks",
                  statusContext->prefix,
                  static_cast<unsigned long>(scannedFiles),
                  static_cast<unsigned long>(trackCount));
    if (statusContext->useBrowserAddStatus) {
        set_browser_add_status(message);
    } else {
        set_browser_refresh_status(message);
    }
}

void scan_directory_recursive(const MusicString &path, music_library_storage_root_t root, PsramVector<TrackInfo> &tracks,
                              LibrarySignature *signature, bool reportProgress, ScanProgressCallback progressCallback,
                              void *progressContext)
{
    PsramVector<MusicString> pendingDirs = {path};
    while (!pendingDirs.empty()) {
        const MusicString current = pendingDirs.back();
        pendingDirs.pop_back();

        PsramVector<TrackInfo> indexedTracks;
        const bool hasExternalIndex = load_external_index_tracks_for_directory(current, root, indexedTracks);
        if (hasExternalIndex) {
            for (const TrackInfo &indexedTrack : indexedTracks) {
                struct stat st = {};
                if (stat(indexedTrack.path.c_str(), &st) != 0) {
                    continue;
                }

                if (reportProgress) {
                    s_indexScannedFiles.fetch_add(1);
                }
                if (signature != nullptr) {
                    update_signature_for_track(*signature, indexedTrack.path, st);
                }

                tracks.push_back(indexedTrack);
                if (reportProgress) {
                    s_indexedTrackCount.store(static_cast<uint32_t>(tracks.size()));
                }
                if (progressCallback != nullptr) {
                    progressCallback(reportProgress ? s_indexScannedFiles.load() : static_cast<uint32_t>(tracks.size()),
                                     static_cast<uint32_t>(tracks.size()),
                                     progressContext);
                }
            }
        }

        DIR *dir = opendir(current.c_str());
        if (dir == nullptr) {
            continue;
        }

        struct dirent *entry = nullptr;
        while ((entry = readdir(dir)) != nullptr) {
            if ((std::strcmp(entry->d_name, ".") == 0) || (std::strcmp(entry->d_name, "..") == 0)) {
                continue;
            }
            if (std::strcmp(entry->d_name, kExternalIndexFileName) == 0) {
                continue;
            }

            const MusicString childPath = current + "/" + entry->d_name;
            struct stat st = {};
            if (stat(childPath.c_str(), &st) != 0) {
                continue;
            }

            if (S_ISDIR(st.st_mode)) {
                pendingDirs.push_back(childPath);
                continue;
            }

            if (hasExternalIndex && is_supported_extension(childPath)) {
                continue;
            }

            if (reportProgress) {
                s_indexScannedFiles.fetch_add(1);
            }
            if (!is_supported_extension(childPath)) {
                continue;
            }

            if (signature != nullptr) {
                update_signature_for_track(*signature, childPath, st);
            }

            TrackInfo track = {};
            track.path = childPath;
            track.title = trim_extension(filename_from_path(childPath));
            track.artist = make_parent_label(childPath);
            track.genre = make_genre_label(childPath, root);
            track.durationSeconds = 180;
            tracks.push_back(std::move(track));

            if (reportProgress) {
                s_indexedTrackCount.store(static_cast<uint32_t>(tracks.size()));
            }
            if (progressCallback != nullptr) {
                progressCallback(reportProgress ? s_indexScannedFiles.load() : static_cast<uint32_t>(tracks.size()),
                                 static_cast<uint32_t>(tracks.size()),
                                 progressContext);
            }
        }
        closedir(dir);
    }

    std::sort(tracks.begin(), tracks.end(), [](const TrackInfo &left, const TrackInfo &right) {
        return left.path < right.path;
    });
}

bool refresh_sd_index_cache(bool forceRebuild)
{
    if (!ensure_root_available(MUSIC_LIBRARY_STORAGE_SD, true)) {
        s_hasCachedIndex.store(false);
        s_sdIndexTracks.clear();
        return false;
    }

    PsramVector<TrackInfo> cachedTracks;
    LibrarySignature cachedSignature = {};
    bool loaded = false;
    if (!forceRebuild) {
        loaded = load_track_file_prefer_sd(kSdIndexFilePath,
                                           kSdIndexTempFilePath,
                                           kLegacySdIndexFilePath,
                                           kLegacySdIndexTempFilePath,
                                           kIndexMagic,
                                           cachedTracks,
                                           &cachedSignature);
    }

    LibrarySignature currentSignature = {};
    init_signature(currentSignature);
    PsramVector<TrackInfo> signatureTracks;
    scan_directory_recursive(kSdRoot, MUSIC_LIBRARY_STORAGE_SD, signatureTracks, &currentSignature, false, nullptr, nullptr);

    if (loaded && signatures_match(cachedSignature, currentSignature)) {
        s_sdIndexTracks = std::move(cachedTracks);
        s_hasCachedIndex.store(true);
        return true;
    }

    if (!save_track_file_prefer_sd(kSdIndexFilePath,
                                   kSdIndexTempFilePath,
                                   kLegacySdIndexFilePath,
                                   kLegacySdIndexTempFilePath,
                                   kIndexMagic,
                                   signatureTracks,
                                   &currentSignature)) {
        s_hasCachedIndex.store(false);
        return false;
    }

    s_sdIndexTracks = std::move(signatureTracks);
    s_hasCachedIndex.store(true);
    return true;
}

bool load_playlist()
{
    PsramVector<TrackInfo> tracks;
    uint32_t currentIndex = 0;
    bool shuffleEnabled = false;
    bool cycleEnabled = false;

    if (prefer_sd_metadata_storage(true)) {
        if (!load_playlist_file(kPlaylistFilePath, &currentIndex, &shuffleEnabled, &cycleEnabled, tracks)) {
            if (load_playlist_file(kLegacyPlaylistFilePath, &currentIndex, &shuffleEnabled, &cycleEnabled, tracks)) {
                (void)save_playlist_file(kPlaylistFilePath, kPlaylistTempFilePath, tracks, currentIndex, shuffleEnabled, cycleEnabled);
                std::remove(kLegacyPlaylistFilePath);
                std::remove(kLegacyPlaylistTempFilePath);
            } else if (!load_track_file(kLegacyPlaylistFilePath, kPlaylistMagic, tracks, nullptr) &&
                       !load_track_file(kPlaylistFilePath, kPlaylistMagic, tracks, nullptr)) {
                s_tracks.clear();
                s_currentIndex = 0;
                s_shuffleEnabled = false;
                s_cycleEnabled = false;
                return false;
            }
        }
    } else if (!load_playlist_file(kLegacyPlaylistFilePath, &currentIndex, &shuffleEnabled, &cycleEnabled, tracks) &&
               !load_track_file(kLegacyPlaylistFilePath, kPlaylistMagic, tracks, nullptr)) {
        s_tracks.clear();
        s_currentIndex = 0;
        s_shuffleEnabled = false;
        s_cycleEnabled = false;
        return false;
    }

    for (TrackInfo &track : tracks) {
        fill_track_metadata(track);
    }

    s_tracks = std::move(tracks);
    s_currentIndex = (s_tracks.empty() || (currentIndex >= s_tracks.size())) ? 0 : currentIndex;
    s_shuffleEnabled = shuffleEnabled;
    s_cycleEnabled = cycleEnabled;
    return true;
}

bool save_playlist()
{
    if (prefer_sd_metadata_storage(true)) {
        return save_playlist_file(kPlaylistFilePath,
                                  kPlaylistTempFilePath,
                                  s_tracks,
                                  s_currentIndex,
                                  s_shuffleEnabled,
                                  s_cycleEnabled);
    }

    return save_playlist_file(kLegacyPlaylistFilePath,
                              kLegacyPlaylistTempFilePath,
                              s_tracks,
                              s_currentIndex,
                              s_shuffleEnabled,
                              s_cycleEnabled);
}

bool playlist_contains_path(const MusicString &path)
{
    return std::any_of(s_tracks.begin(), s_tracks.end(), [&](const TrackInfo &track) {
        return track.path == path;
    });
}

void fill_track_metadata(TrackInfo &track)
{
    if (track.title.empty()) {
        track.title = trim_extension(filename_from_path(track.path));
    }
    if (track.artist.empty()) {
        track.artist = make_parent_label(track.path);
    }
    if (track.genre.empty()) {
        const music_library_storage_root_t root = (track.path.rfind(kSdRoot, 0) == 0) ? MUSIC_LIBRARY_STORAGE_SD : MUSIC_LIBRARY_STORAGE_SPIFFS;
        track.genre = make_genre_label(track.path, root);
    }
    if (track.durationSeconds == 0) {
        track.durationSeconds = 180;
    }

    if (track.info.empty()) {
        MusicString info = track.genre;
        const size_t separator = info.find("  вЂў  ");
        if (separator != MusicString::npos) {
            info = info.substr(0, separator);
        }
        track.info = std::move(info);
    }
}

MusicString make_track_info_label(const TrackInfo &track, uint64_t fileSizeBytes)
{
    MusicString info = track.genre;
    const size_t separator = info.find("  вЂў  ");
    if (separator != MusicString::npos) {
        info = info.substr(0, separator);
    }

    if ((track.durationSeconds > 0) && (fileSizeBytes > 0)) {
        const uint64_t bitrate = (fileSizeBytes * 8ULL) / (static_cast<uint64_t>(track.durationSeconds) * 1000ULL);
        if (bitrate > 0) {
            char bitrateBuffer[32] = {};
            std::snprintf(bitrateBuffer, sizeof(bitrateBuffer), "%llu", static_cast<unsigned long long>(bitrate));
            info += "  вЂў  ~";
            info += bitrateBuffer;
            info += " kbps";
        }
    }

    if (fileSizeBytes > 0) {
        info += "  вЂў  ";
        info += format_size(fileSizeBytes);
    }

    return info;
}

void populate_track_runtime_fields(TrackInfo &track)
{
    if (track.infoResolved) {
        return;
    }

    struct stat st = {};
    const uint64_t fileSizeBytes = (stat(track.path.c_str(), &st) == 0) ? static_cast<uint64_t>(st.st_size) : 0;
    track.info = make_track_info_label(track, fileSizeBytes);
    track.infoResolved = true;
}

bool build_track_from_path(const MusicString &path, music_library_storage_root_t root, TrackInfo &track)
{
    struct stat st = {};
    if ((stat(path.c_str(), &st) != 0) || S_ISDIR(st.st_mode) || !is_supported_extension(path)) {
        return false;
    }

    if ((root == MUSIC_LIBRARY_STORAGE_SD) && refresh_sd_index_cache(false)) {
        auto cached = std::find_if(s_sdIndexTracks.begin(), s_sdIndexTracks.end(), [&](const TrackInfo &candidate) {
            return candidate.path == path;
        });
        if (cached != s_sdIndexTracks.end()) {
            track = *cached;
            return true;
        }
    }

    track = {};
    track.path = path;
    track.title = trim_extension(filename_from_path(path));
    track.artist = make_parent_label(path);
    track.genre = make_genre_label(path, root);
    track.durationSeconds = 180;
    track.favorite = false;
    track.infoResolved = false;
    fill_track_metadata(track);
    return true;
}

bool add_track_to_playlist(const TrackInfo &track, bool persist)
{
    if (playlist_contains_path(track.path)) {
        return false;
    }

    s_tracks.push_back(track);
    if (s_tracks.size() == 1) {
        s_currentIndex = 0;
    }
    if (persist && !save_playlist()) {
        s_tracks.pop_back();
        return false;
    }
    return true;
}

bool add_path_to_playlist(const MusicString &path, music_library_storage_root_t root, bool persist)
{
    TrackInfo track = {};
    if (!build_track_from_path(path, root, track)) {
        return false;
    }
    return add_track_to_playlist(track, persist);
}

bool append_tracks_to_playlist(const PsramVector<TrackInfo> &candidates, uint32_t *processedCount, uint32_t *addedCount,
                               uint32_t *duplicateCount, uint32_t *missingCount)
{
    const size_t originalSize = s_tracks.size();
    uint32_t localProcessed = 0;
    uint32_t localAdded = 0;
    uint32_t localDuplicates = 0;
    uint32_t localMissing = 0;

    for (const TrackInfo &track : candidates) {
        localProcessed++;
        if (access(track.path.c_str(), F_OK) != 0) {
            localMissing++;
            continue;
        }
        if (playlist_contains_path(track.path)) {
            localDuplicates++;
            continue;
        }

        s_tracks.push_back(track);
        localAdded++;

        if ((localProcessed == 1) || ((localProcessed % 8) == 0) || (localProcessed == candidates.size())) {
            char status[160] = {};
            std::snprintf(status,
                          sizeof(status),
                          "Adding folder to playlist... %lu/%lu processed",
                          static_cast<unsigned long>(localProcessed),
                          static_cast<unsigned long>(candidates.size()));
            set_browser_add_status(status);
        }
    }

    if ((localAdded == 0) || !save_playlist()) {
        s_tracks.resize(originalSize);
        return false;
    }

    if ((originalSize == 0) && !s_tracks.empty()) {
        s_currentIndex = 0;
    }

    if (processedCount != nullptr) {
        *processedCount = localProcessed;
    }
    if (addedCount != nullptr) {
        *addedCount = localAdded;
    }
    if (duplicateCount != nullptr) {
        *duplicateCount = localDuplicates;
    }
    if (missingCount != nullptr) {
        *missingCount = localMissing;
    }
    return true;
}

bool collect_folder_tracks(const MusicString &path, music_library_storage_root_t root, bool forceRebuild,
                           PsramVector<TrackInfo> &candidates, bool *usedCache, ScanProgressCallback progressCallback,
                           void *progressContext)
{
    candidates.clear();
    if (usedCache != nullptr) {
        *usedCache = false;
    }

    if (!forceRebuild && load_folder_playlist_cache(path, candidates)) {
        if (usedCache != nullptr) {
            *usedCache = true;
        }
        return !candidates.empty();
    }

    scan_directory_recursive(path, root, candidates, nullptr, false, progressCallback, progressContext);
    if (candidates.empty()) {
        return false;
    }

    (void)save_folder_playlist_cache(path, candidates);
    return true;
}

bool add_folder_to_playlist(const MusicString &path, music_library_storage_root_t root, bool forceRebuild)
{
    PsramVector<TrackInfo> candidates;
    bool usedCache = false;
    StatusProgressContext progressContext = {"Indexing folder...", true, 0};

    if (!collect_folder_tracks(path, root, forceRebuild, candidates, &usedCache, folder_scan_progress_cb, &progressContext)) {
        return false;
    }

    if (usedCache) {
        char cachedStatus[160] = {};
        std::snprintf(cachedStatus,
                      sizeof(cachedStatus),
                      "Loaded cached folder playlist with %lu files",
                      static_cast<unsigned long>(candidates.size()));
        set_browser_add_status(cachedStatus);
    }

    uint32_t processedCount = 0;
    uint32_t addedCount = 0;
    uint32_t duplicateCount = 0;
    uint32_t missingCount = 0;
    if (!append_tracks_to_playlist(candidates, &processedCount, &addedCount, &duplicateCount, &missingCount)) {
        return false;
    }

    char message[160] = {};
    std::snprintf(message,
                  sizeof(message),
                  "%s %lu tracks from folder (%lu processed%s%s)",
                  usedCache ? "Loaded" : "Added",
                  static_cast<unsigned long>(addedCount),
                  static_cast<unsigned long>(processedCount),
                  (duplicateCount > 0) ? ", duplicates skipped" : "",
                  (missingCount > 0) ? ", missing files skipped" : "");
    set_last_message(message);
    return true;
}

bool rebuild_folder_playlist_cache(const MusicString &path, music_library_storage_root_t root)
{
    PsramVector<TrackInfo> candidates;
    StatusProgressContext progressContext = {"Reindexing folder...", false, 0};
    scan_directory_recursive(path, root, candidates, nullptr, false, folder_scan_progress_cb, &progressContext);
    if (candidates.empty()) {
        set_browser_refresh_status("No supported files found in this folder");
        return false;
    }
    if (!save_folder_playlist_cache(path, candidates)) {
        set_browser_refresh_status("Folder scan finished but saving the cached playlist failed");
        return false;
    }

    char message[160] = {};
    std::snprintf(message,
                  sizeof(message),
                  "Reindexed folder playlist with %lu files",
                  static_cast<unsigned long>(candidates.size()));
    set_last_message(message);
    set_browser_refresh_status(message);
    return true;
}

bool refresh_browser(BrowserState &state)
{
    state.available = ensure_root_available(state.root, state.root == MUSIC_LIBRARY_STORAGE_SD);
    state.entries.clear();
    if (!state.available) {
        state.currentPath = root_path(state.root);
        return false;
    }

    if (!is_path_directory(state.currentPath)) {
        state.currentPath = root_path(state.root);
    }

    DIR *dir = opendir(state.currentPath.c_str());
    if (dir == nullptr) {
        return false;
    }

    PsramVector<TrackInfo> indexedTracks;
    const bool hasExternalIndex = load_external_index_tracks_for_directory(state.currentPath, state.root, indexedTracks);

    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if ((std::strcmp(entry->d_name, ".") == 0) || (std::strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        if ((std::strcmp(entry->d_name, kExternalIndexFileName) == 0) ||
            (std::strcmp(entry->d_name, kFolderPlaylistFileName) == 0) ||
            (std::strcmp(entry->d_name, kFolderPlaylistTempFileName) == 0)) {
            continue;
        }

        BrowserEntry browserEntry = {};
        browserEntry.name = entry->d_name;
        browserEntry.path = state.currentPath + "/" + entry->d_name;

        struct stat st = {};
        if (stat(browserEntry.path.c_str(), &st) != 0) {
            continue;
        }

        browserEntry.isDirectory = S_ISDIR(st.st_mode);
        browserEntry.supported = !browserEntry.isDirectory && is_supported_extension(browserEntry.path);
        if (hasExternalIndex && browserEntry.supported) {
            continue;
        }
        browserEntry.canAdd = (s_browserMode == MUSIC_LIBRARY_BROWSER_MODE_FILE) ? browserEntry.supported : browserEntry.isDirectory;
        browserEntry.meta = browserEntry.isDirectory
                                ? (folder_playlist_cache_exists(browserEntry.path) ? "Folder playlist cached" : "Folder")
                                : format_size(static_cast<uint64_t>(st.st_size));

        if ((s_browserMode == MUSIC_LIBRARY_BROWSER_MODE_FILE) && !browserEntry.isDirectory && !browserEntry.supported) {
            continue;
        }
        if ((s_browserMode == MUSIC_LIBRARY_BROWSER_MODE_FOLDER) && !browserEntry.isDirectory && !browserEntry.supported) {
            continue;
        }

        state.entries.push_back(std::move(browserEntry));
    }
    closedir(dir);

    if (hasExternalIndex) {
        for (const TrackInfo &track : indexedTracks) {
            struct stat st = {};
            if (stat(track.path.c_str(), &st) != 0) {
                continue;
            }

            BrowserEntry browserEntry = {};
            browserEntry.name = filename_from_path(track.path);
            browserEntry.path = track.path;
            browserEntry.meta = format_size(static_cast<uint64_t>(st.st_size));
            browserEntry.isDirectory = false;
            browserEntry.supported = true;
            browserEntry.canAdd = (s_browserMode == MUSIC_LIBRARY_BROWSER_MODE_FILE);
            state.entries.push_back(std::move(browserEntry));
        }
    }

    std::sort(state.entries.begin(), state.entries.end(), [](const BrowserEntry &left, const BrowserEntry &right) {
        if (left.isDirectory != right.isDirectory) {
            return left.isDirectory > right.isDirectory;
        }
        return left.name < right.name;
    });
    return true;
}

bool ensure_directory_exists(const MusicString &path)
{
    if (path.empty()) {
        return false;
    }
    if (is_path_directory(path)) {
        return true;
    }

    size_t start = 1;
    while (start < path.size()) {
        const size_t slash = path.find('/', start);
        const MusicString partial = (slash == MusicString::npos) ? path : path.substr(0, slash);
        if (!partial.empty() && !is_path_directory(partial)) {
            if ((mkdir(partial.c_str(), 0775) != 0) && (errno != EEXIST)) {
                return false;
            }
        }
        if (slash == MusicString::npos) {
            break;
        }
        start = slash + 1;
    }
    return is_path_directory(path);
}

MusicString sanitize_filename(const MusicString &value)
{
    MusicString name = value;
    for (char &ch : name) {
        if ((ch == '/') || (ch == '\\') || (ch == ':') || (ch == '*') || (ch == '?') || (ch == '"') ||
            (ch == '<') || (ch == '>') || (ch == '|')) {
            ch = '_';
        }
    }
    return name;
}

MusicString infer_filename_from_url(const MusicString &url)
{
    MusicString trimmed = url;
    const size_t query = trimmed.find_first_of("?#");
    if (query != MusicString::npos) {
        trimmed = trimmed.substr(0, query);
    }

    const size_t slash = trimmed.find_last_of('/');
    MusicString filename = (slash == MusicString::npos) ? trimmed : trimmed.substr(slash + 1);
    return sanitize_filename(filename);
}

void music_library_index_task(void *context)
{
    (void)context;

    PsramVector<TrackInfo> tracks;
    LibrarySignature signature = {};
    bool success = false;
    if (ensure_root_available(MUSIC_LIBRARY_STORAGE_SD, true)) {
        s_indexScannedFiles.store(0);
        s_indexedTrackCount.store(0);
        scan_directory_recursive(kSdRoot, MUSIC_LIBRARY_STORAGE_SD, tracks, &signature, true, nullptr, nullptr);
        success = save_track_file_prefer_sd(kSdIndexFilePath,
                            kSdIndexTempFilePath,
                            kLegacySdIndexFilePath,
                            kLegacySdIndexTempFilePath,
                            kIndexMagic,
                            tracks,
                            &signature);
    }

    if (success) {
        s_sdIndexTracks = std::move(tracks);
        s_hasCachedIndex.store(true);
        s_indexState.store(MUSIC_LIBRARY_INDEX_STATE_COMPLETED);
    } else {
        s_indexState.store(MUSIC_LIBRARY_INDEX_STATE_FAILED);
    }

    vTaskDeleteWithCaps(nullptr);
}

void music_library_browser_add_task(void *context)
{
    BrowserAddRequest *request = static_cast<BrowserAddRequest *>(context);
    if (request == nullptr) {
        set_last_message("Failed to start playlist add");
        set_browser_add_status("Failed to start playlist add");
        s_browserAddState.store(MUSIC_LIBRARY_BROWSER_ADD_STATE_FAILED);
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    const music_library_storage_root_t root = request->root;
    const MusicString path = request->path;
    const MusicString name = request->name;
    const bool isDirectory = request->isDirectory;
    const bool forceRebuild = request->forceRebuild;
    delete request;

    bool success = false;
    if (isDirectory) {
        success = add_folder_to_playlist(path, root, forceRebuild);
        if (!success) {
            set_last_message(forceRebuild ? "Folder reindex failed or no supported files were found" : "No supported files found in that folder");
        }
    } else {
        success = add_path_to_playlist(path, root, true);
        if (success) {
            set_last_message(MusicString("Added ") + name);
        } else {
            set_last_message(playlist_contains_path(path) ? "That file is already in the playlist" : "Failed to add file");
        }
    }

    set_browser_add_status(s_lastMessage);
    s_browserAddState.store(success ? MUSIC_LIBRARY_BROWSER_ADD_STATE_COMPLETED : MUSIC_LIBRARY_BROWSER_ADD_STATE_FAILED);
    vTaskDeleteWithCaps(nullptr);
}

void music_library_browser_refresh_task(void *context)
{
    BrowserRefreshRequest *request = static_cast<BrowserRefreshRequest *>(context);
    if (request == nullptr) {
        set_browser_refresh_status("Failed to start browser refresh");
        s_browserRefreshState.store(MUSIC_LIBRARY_BROWSER_REFRESH_STATE_FAILED);
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    const music_library_storage_root_t root = request->root;
    const MusicString path = request->path;
    const bool reindexFolder = request->reindexFolder;
    delete request;

    bool success = false;
    if (reindexFolder) {
        success = rebuild_folder_playlist_cache(path, root);
        if (success) {
            BrowserState refreshedState = browser_state(root);
            refreshedState.currentPath = path;
            success = refresh_browser(refreshedState);
            browser_state(root) = std::move(refreshedState);
        }
    } else {
        BrowserState refreshedState = browser_state(root);
        refreshedState.currentPath = path;
        set_browser_refresh_status(MusicString("Loading ") + root_label(root) + "...");
        success = refresh_browser(refreshedState);
        browser_state(root) = std::move(refreshedState);
        if (success) {
            char message[160] = {};
            std::snprintf(message,
                          sizeof(message),
                          "Loaded %lu items from %s",
                          static_cast<unsigned long>(browser_state(root).entries.size()),
                          root_label(root));
            set_browser_refresh_status(message);
        } else {
            set_browser_refresh_status(MusicString(root_label(root)) + " is unavailable");
        }
    }

    s_browserRefreshState.store(success ? MUSIC_LIBRARY_BROWSER_REFRESH_STATE_COMPLETED : MUSIC_LIBRARY_BROWSER_REFRESH_STATE_FAILED);
    vTaskDeleteWithCaps(nullptr);
}

void music_library_download_task(void *context)
{
    MusicString *urlHolder = static_cast<MusicString *>(context);
    const MusicString url = (urlHolder != nullptr) ? *urlHolder : MusicString();
    delete urlHolder;

    if (!ensure_root_available(MUSIC_LIBRARY_STORAGE_SD, true)) {
        set_download_status("Insert an SD card before downloading.");
        s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_FAILED);
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    const MusicString filename = infer_filename_from_url(url);
    if (filename.empty() || !is_supported_extension(filename)) {
        set_download_status("URL must end with .mp3, .mpga, .aac, .flac, .m4a, .mp4, .wav, or .wave.");
        s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_FAILED);
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    if (!ensure_directory_exists(kDownloadDir)) {
        set_download_status("Failed to prepare /sdcard/Downloads/Music.");
        s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_FAILED);
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    MusicString destination = MusicString(kDownloadDir) + "/" + filename;
    if (playlist_contains_path(destination)) {
        const size_t dot = filename.find_last_of('.');
        const MusicString base = (dot == MusicString::npos) ? filename : filename.substr(0, dot);
        const MusicString ext = (dot == MusicString::npos) ? MusicString() : filename.substr(dot);
        for (uint32_t index = 2; index < 1000; ++index) {
            MusicString candidate = MusicString(kDownloadDir) + "/" + base + "_" + MusicString(std::to_string(index).c_str()) + ext;
            if (!playlist_contains_path(candidate)) {
                destination = std::move(candidate);
                break;
            }
        }
    }

    const MusicString tempPath = destination + ".tmp";
    FILE *file = std::fopen(tempPath.c_str(), "wb");
    if (file == nullptr) {
        set_download_status("Failed to create download file.");
        s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_FAILED);
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.disable_auto_redirect = false;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        std::fclose(file);
        std::remove(tempPath.c_str());
        set_download_status("Failed to create HTTP client.");
        s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_FAILED);
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    esp_http_client_set_header(client, "Accept", "audio/*,application/octet-stream,*/*");
    esp_http_client_set_header(client, "User-Agent", "JC4880P443C-IW-Remote");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        std::fclose(file);
        std::remove(tempPath.c_str());
        esp_http_client_cleanup(client);
        set_download_status(MusicString("Failed to open URL: ") + esp_err_to_name(err));
        s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_FAILED);
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    const int headersResult = esp_http_client_fetch_headers(client);
    const int statusCode = esp_http_client_get_status_code(client);
    if ((headersResult < 0) || ((statusCode / 100) != 2)) {
        std::fclose(file);
        std::remove(tempPath.c_str());
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        set_download_status("URL is reachable but the server did not return an audio file.");
        s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_FAILED);
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    const int64_t contentLength = esp_http_client_get_content_length(client);
    PsramVector<uint8_t> buffer(4096);
    size_t totalWritten = 0;
    s_downloadProgress.store((contentLength > 0) ? 0 : -1);
    set_download_status("Downloading to /sdcard/Downloads/Music...");

    bool ok = true;
    while (true) {
        const int readBytes = esp_http_client_read(client, reinterpret_cast<char *>(buffer.data()), buffer.size());
        if (readBytes < 0) {
            ok = false;
            set_download_status("Download failed while reading data.");
            break;
        }
        if (readBytes == 0) {
            break;
        }
        if (std::fwrite(buffer.data(), 1, static_cast<size_t>(readBytes), file) != static_cast<size_t>(readBytes)) {
            ok = false;
            set_download_status("Download failed while writing to SD.");
            break;
        }

        totalWritten += static_cast<size_t>(readBytes);
        if (contentLength > 0) {
            const int32_t progress = static_cast<int32_t>((totalWritten * 100ULL) / static_cast<uint64_t>(contentLength));
            s_downloadProgress.store(progress);
            char message[96] = {};
            std::snprintf(message, sizeof(message), "Downloading... %ld%%", static_cast<long>(progress));
            set_download_status(message);
        }
    }

    std::fclose(file);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!ok) {
        std::remove(tempPath.c_str());
        s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_FAILED);
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    std::remove(destination.c_str());
    if (std::rename(tempPath.c_str(), destination.c_str()) != 0) {
        std::remove(tempPath.c_str());
        set_download_status("Download finished but publishing the file failed.");
        s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_FAILED);
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    if (!add_path_to_playlist(destination, MUSIC_LIBRARY_STORAGE_SD, true)) {
        set_download_status("Downloaded file is already in the playlist or could not be added.");
        s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_FAILED);
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    refresh_sd_index_cache(true);
    set_last_message(MusicString("Downloaded and added ") + filename);
    set_download_status(MusicString("Downloaded and added ") + filename);
    s_downloadProgress.store(100);
    s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_COMPLETED);
    vTaskDeleteWithCaps(nullptr);
}

const TrackInfo *get_track(uint32_t trackId)
{
    return (trackId < s_tracks.size()) ? &s_tracks[trackId] : nullptr;
}

bool step_track_index(music_library_track_step_t step, uint32_t *trackId)
{
    if (s_tracks.empty()) {
        if (trackId != nullptr) {
            *trackId = 0;
        }
        return false;
    }

    uint32_t candidate = std::min<uint32_t>(s_currentIndex, static_cast<uint32_t>(s_tracks.size() - 1));

    if (s_shuffleEnabled && (s_tracks.size() > 1)) {
        uint32_t randomIndex = candidate;
        while (randomIndex == candidate) {
            randomIndex = esp_random() % s_tracks.size();
        }
        candidate = randomIndex;
    } else if (step == MUSIC_LIBRARY_TRACK_STEP_PREVIOUS) {
        if (candidate > 0) {
            candidate--;
        } else if (s_cycleEnabled) {
            candidate = static_cast<uint32_t>(s_tracks.size() - 1);
        }
    } else {
        if (candidate + 1 < s_tracks.size()) {
            candidate++;
        } else if (s_cycleEnabled || (step == MUSIC_LIBRARY_TRACK_STEP_AUTOPLAY_NEXT)) {
            candidate = 0;
        }
    }

    if ((step == MUSIC_LIBRARY_TRACK_STEP_AUTOPLAY_NEXT) && !s_shuffleEnabled && !s_cycleEnabled &&
        (candidate == s_currentIndex) && (s_currentIndex + 1 >= s_tracks.size())) {
        if (trackId != nullptr) {
            *trackId = s_currentIndex;
        }
        return false;
    }

    s_currentIndex = candidate;
    if (trackId != nullptr) {
        *trackId = candidate;
    }
    return true;
}

} // namespace

extern "C" bool music_library_init(void)
{
    if (s_indexState.load() == MUSIC_LIBRARY_INDEX_STATE_COMPLETED) {
        (void)music_library_finalize_index();
    }

    s_sdBrowser.currentPath = kSdRoot;
    s_spiffsBrowser.currentPath = kSpiffsRoot;
    s_browserMode = MUSIC_LIBRARY_BROWSER_MODE_FILE;
    s_browserAddState.store(MUSIC_LIBRARY_BROWSER_ADD_STATE_IDLE);
    set_browser_add_status("Idle");
    s_browserRefreshState.store(MUSIC_LIBRARY_BROWSER_REFRESH_STATE_IDLE);
    set_browser_refresh_status("Idle");
    s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_IDLE);
    s_downloadProgress.store(-1);
    set_download_status("Idle");
    s_shuffleEnabled = false;
    s_cycleEnabled = false;

    const bool loaded = load_playlist();
    if (loaded) {
        char message[96] = {};
        std::snprintf(message, sizeof(message), "Loaded %lu playlist entries", static_cast<unsigned long>(s_tracks.size()));
        set_last_message(message);
    } else {
        set_last_message("Playlist is empty");
    }
    return loaded;
}

extern "C" void music_library_deinit(void)
{
    PsramVector<TrackInfo>().swap(s_tracks);
    PsramVector<TrackInfo>().swap(s_sdIndexTracks);
    PsramVector<BrowserEntry>().swap(s_sdBrowser.entries);
    PsramVector<BrowserEntry>().swap(s_spiffsBrowser.entries);

    s_currentIndex = 0;
    s_shuffleEnabled = false;
    s_cycleEnabled = false;
    s_lastMessage = "Playlist ready";
    s_sdBrowser.currentPath = kSdRoot;
    s_sdBrowser.available = false;
    s_spiffsBrowser.currentPath = kSpiffsRoot;
    s_spiffsBrowser.available = false;
    s_browserMode = MUSIC_LIBRARY_BROWSER_MODE_FILE;
    s_hasCachedIndex.store(false);
    s_indexState.store(MUSIC_LIBRARY_INDEX_STATE_IDLE);
    s_indexScannedFiles.store(0);
    s_indexedTrackCount.store(0);
    s_browserAddState.store(MUSIC_LIBRARY_BROWSER_ADD_STATE_IDLE);
    set_browser_add_status("Idle");
    s_browserRefreshState.store(MUSIC_LIBRARY_BROWSER_REFRESH_STATE_IDLE);
    set_browser_refresh_status("Idle");
    s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_IDLE);
    s_downloadProgress.store(-1);
    set_download_status("Idle");
}

extern "C" bool music_library_refresh(void)
{
    const bool refreshed = refresh_sd_index_cache(true);
    if (refreshed) {
        char message[96] = {};
        std::snprintf(message, sizeof(message), "Indexed %lu SD tracks", static_cast<unsigned long>(s_sdIndexTracks.size()));
        set_last_message(message);
    }
    return refreshed;
}

extern "C" bool music_library_start_index(void)
{
    if (s_indexState.load() == MUSIC_LIBRARY_INDEX_STATE_RUNNING) {
        return false;
    }
    if (!ensure_root_available(MUSIC_LIBRARY_STORAGE_SD, true)) {
        s_indexState.store(MUSIC_LIBRARY_INDEX_STATE_FAILED);
        return false;
    }

    s_indexScannedFiles.store(0);
    s_indexedTrackCount.store(0);
    s_indexState.store(MUSIC_LIBRARY_INDEX_STATE_RUNNING);

    TaskHandle_t task = nullptr;
    if (xTaskCreatePinnedToCoreWithCaps(
            music_library_index_task,
            "music_index",
            8 * 1024,
            nullptr,
            4,
            &task,
            tskNO_AFFINITY,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_indexState.store(MUSIC_LIBRARY_INDEX_STATE_FAILED);
        return false;
    }
    return true;
}

extern "C" bool music_library_finalize_index(void)
{
    if (s_indexState.load() != MUSIC_LIBRARY_INDEX_STATE_COMPLETED) {
        return false;
    }

    PsramVector<TrackInfo> tracks;
    LibrarySignature signature = {};
    if (!load_track_file_prefer_sd(kSdIndexFilePath,
                                   kSdIndexTempFilePath,
                                   kLegacySdIndexFilePath,
                                   kLegacySdIndexTempFilePath,
                                   kIndexMagic,
                                   tracks,
                                   &signature)) {
        s_indexState.store(MUSIC_LIBRARY_INDEX_STATE_FAILED);
        return false;
    }

    s_sdIndexTracks = std::move(tracks);
    s_hasCachedIndex.store(true);
    s_indexedTrackCount.store(static_cast<uint32_t>(s_sdIndexTracks.size()));
    s_indexState.store(MUSIC_LIBRARY_INDEX_STATE_IDLE);
    return true;
}

extern "C" music_library_index_state_t music_library_get_index_state(void)
{
    return s_indexState.load();
}

extern "C" uint32_t music_library_get_index_scanned_files(void)
{
    return s_indexScannedFiles.load();
}

extern "C" uint32_t music_library_get_indexed_track_count(void)
{
    return s_indexedTrackCount.load();
}

extern "C" bool music_library_has_cached_index(void)
{
    return s_hasCachedIndex.load();
}

extern "C" uint32_t music_library_get_count(void)
{
    return static_cast<uint32_t>(s_tracks.size());
}

extern "C" const char *music_library_get_title(uint32_t trackId)
{
    const TrackInfo *track = get_track(trackId);
    return (track == nullptr) ? kEmptyTitle : track->title.c_str();
}

extern "C" const char *music_library_get_artist(uint32_t trackId)
{
    const TrackInfo *track = get_track(trackId);
    return (track == nullptr) ? kEmptyArtist : track->artist.c_str();
}

extern "C" const char *music_library_get_genre(uint32_t trackId)
{
    const TrackInfo *track = get_track(trackId);
    return (track == nullptr) ? kEmptyGenre : track->genre.c_str();
}

extern "C" const char *music_library_get_info(uint32_t trackId)
{
    TrackInfo *track = (trackId < s_tracks.size()) ? &s_tracks[trackId] : nullptr;
    if (track != nullptr) {
        populate_track_runtime_fields(*track);
    }
    return (track == nullptr) ? kEmptyGenre : track->info.c_str();
}

extern "C" uint32_t music_library_get_track_length(uint32_t trackId)
{
    const TrackInfo *track = get_track(trackId);
    return (track == nullptr) ? 0 : track->durationSeconds;
}

extern "C" const char *music_library_get_path(uint32_t trackId)
{
    const TrackInfo *track = get_track(trackId);
    return (track == nullptr) ? nullptr : track->path.c_str();
}

extern "C" bool music_library_play(uint32_t trackId)
{
    const TrackInfo *track = get_track(trackId);
    if (track == nullptr) {
        set_last_message("Nothing to play");
        return false;
    }

    const bool needsSd = track->path.rfind(kSdRoot, 0) == 0;
    if (needsSd && !ensure_root_available(MUSIC_LIBRARY_STORAGE_SD, true)) {
        set_last_message("Insert the SD card to play this item");
        return false;
    }
    if (access(track->path.c_str(), F_OK) != 0) {
        set_last_message("The selected file is no longer available");
        return false;
    }
    if (!ensure_music_audio_ready()) {
        set_last_message("Audio output is not available right now");
        return false;
    }
    if (bsp_extra_player_play_file(track->path.c_str()) != ESP_OK) {
        set_last_message("Playback failed for the selected file");
        return false;
    }

    s_currentIndex = trackId;
    (void)save_playlist();
    set_last_message(MusicString("Playing ") + track->title);
    return true;
}

extern "C" bool music_library_is_playing(uint32_t trackId)
{
    const TrackInfo *track = get_track(trackId);
    return (track != nullptr) && bsp_extra_player_is_playing_by_path(track->path.c_str());
}

extern "C" uint32_t music_library_get_current_index(void)
{
    return s_currentIndex;
}

extern "C" bool music_library_set_current_index(uint32_t trackId)
{
    if (trackId >= s_tracks.size()) {
        return false;
    }
    s_currentIndex = trackId;
    return true;
}

extern "C" bool music_library_persist_state(void)
{
    return save_playlist();
}

extern "C" bool music_library_step_current_track(music_library_track_step_t step, uint32_t *trackId)
{
    return step_track_index(step, trackId);
}

extern "C" bool music_library_is_shuffle_enabled(void)
{
    return s_shuffleEnabled;
}

extern "C" bool music_library_set_shuffle_enabled(bool enabled)
{
    s_shuffleEnabled = enabled;
    return save_playlist();
}

extern "C" bool music_library_toggle_shuffle(void)
{
    return music_library_set_shuffle_enabled(!s_shuffleEnabled);
}

extern "C" bool music_library_is_cycle_enabled(void)
{
    return s_cycleEnabled;
}

extern "C" bool music_library_set_cycle_enabled(bool enabled)
{
    s_cycleEnabled = enabled;
    return save_playlist();
}

extern "C" bool music_library_toggle_cycle(void)
{
    return music_library_set_cycle_enabled(!s_cycleEnabled);
}

extern "C" bool music_library_is_favorite(uint32_t trackId)
{
    const TrackInfo *track = get_track(trackId);
    return (track != nullptr) && track->favorite;
}

extern "C" bool music_library_toggle_favorite(uint32_t trackId)
{
    if (trackId >= s_tracks.size()) {
        return false;
    }

    s_tracks[trackId].favorite = !s_tracks[trackId].favorite;
    if (!save_playlist()) {
        s_tracks[trackId].favorite = !s_tracks[trackId].favorite;
        return false;
    }

    set_last_message(s_tracks[trackId].favorite ? MusicString("Added to favorites: ") + s_tracks[trackId].title
                                                : MusicString("Removed from favorites: ") + s_tracks[trackId].title);
    return true;
}

extern "C" bool music_library_playlist_clear(void)
{
    s_tracks.clear();
    s_currentIndex = 0;
    s_shuffleEnabled = false;
    s_cycleEnabled = false;
    if (!save_playlist()) {
        set_last_message("Failed to clear playlist");
        return false;
    }
    set_last_message("Playlist cleared");
    return true;
}

extern "C" const char *music_library_get_last_message(void)
{
    return s_lastMessage.c_str();
}

extern "C" bool music_library_browser_open(music_library_browser_mode_t mode)
{
    s_browserMode = mode;
    s_sdBrowser.currentPath = kSdRoot;
    s_sdBrowser.entries.clear();
    s_sdBrowser.available = false;
    s_spiffsBrowser.currentPath = kSpiffsRoot;
    s_spiffsBrowser.entries.clear();
    s_spiffsBrowser.available = false;
    set_last_message((mode == MUSIC_LIBRARY_BROWSER_MODE_FILE) ? "Select a file to add" : "Select a folder to add");
    return true;
}

extern "C" music_library_browser_mode_t music_library_browser_get_mode(void)
{
    return s_browserMode;
}

extern "C" uint32_t music_library_browser_get_count(music_library_storage_root_t root)
{
    return static_cast<uint32_t>(browser_state(root).entries.size());
}

extern "C" const char *music_library_browser_get_path(music_library_storage_root_t root)
{
    return browser_state(root).currentPath.c_str();
}

extern "C" const char *music_library_browser_get_name(music_library_storage_root_t root, uint32_t entryId)
{
    BrowserState &state = browser_state(root);
    return (entryId < state.entries.size()) ? state.entries[entryId].name.c_str() : "";
}

extern "C" const char *music_library_browser_get_meta(music_library_storage_root_t root, uint32_t entryId)
{
    BrowserState &state = browser_state(root);
    if (entryId >= state.entries.size()) {
        return state.available ? "" : "Storage unavailable";
    }
    return state.entries[entryId].meta.c_str();
}

extern "C" bool music_library_browser_entry_is_directory(music_library_storage_root_t root, uint32_t entryId)
{
    BrowserState &state = browser_state(root);
    return (entryId < state.entries.size()) && state.entries[entryId].isDirectory;
}

extern "C" bool music_library_browser_entry_can_add(music_library_storage_root_t root, uint32_t entryId)
{
    BrowserState &state = browser_state(root);
    return (entryId < state.entries.size()) && state.entries[entryId].canAdd;
}

extern "C" bool music_library_browser_entry_is_available(music_library_storage_root_t root, uint32_t entryId)
{
    BrowserState &state = browser_state(root);
    return state.available && (entryId < state.entries.size());
}

extern "C" bool music_library_browser_root_available(music_library_storage_root_t root)
{
    return browser_state(root).available;
}

extern "C" bool music_library_browser_navigate_up(music_library_storage_root_t root)
{
    BrowserState &state = browser_state(root);
    const MusicString rootPath = root_path(root);
    if (state.currentPath == rootPath) {
        state.entries.clear();
        return true;
    }

    const size_t split = state.currentPath.find_last_of('/');
    if ((split == MusicString::npos) || (split <= rootPath.size())) {
        state.currentPath = rootPath;
    } else {
        state.currentPath = state.currentPath.substr(0, split);
    }
    state.entries.clear();
    return true;
}

extern "C" bool music_library_browser_enter_directory(music_library_storage_root_t root, uint32_t entryId)
{
    BrowserState &state = browser_state(root);
    if ((entryId >= state.entries.size()) || !state.entries[entryId].isDirectory) {
        return false;
    }
    state.currentPath = state.entries[entryId].path;
    state.entries.clear();
    return true;
}

extern "C" bool music_library_browser_refresh_async(music_library_storage_root_t root)
{
    if (s_browserRefreshState.load() == MUSIC_LIBRARY_BROWSER_REFRESH_STATE_RUNNING) {
        set_browser_refresh_status("Browser refresh already in progress");
        return false;
    }

    BrowserRefreshRequest *request = new BrowserRefreshRequest{root, browser_state(root).currentPath, false};
    s_browserRefreshState.store(MUSIC_LIBRARY_BROWSER_REFRESH_STATE_RUNNING);
    set_browser_refresh_status(MusicString("Loading ") + root_label(root) + "...");

    TaskHandle_t task = nullptr;
    if (xTaskCreatePinnedToCoreWithCaps(
            music_library_browser_refresh_task,
            "music_browser_refresh",
            10 * 1024,
            request,
            4,
            &task,
            tskNO_AFFINITY,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        delete request;
        s_browserRefreshState.store(MUSIC_LIBRARY_BROWSER_REFRESH_STATE_FAILED);
        set_browser_refresh_status("Failed to start browser refresh");
        return false;
    }

    return true;
}

extern "C" bool music_library_browser_reindex_current_async(music_library_storage_root_t root)
{
    if (s_browserRefreshState.load() == MUSIC_LIBRARY_BROWSER_REFRESH_STATE_RUNNING) {
        set_browser_refresh_status("A browser task is already running");
        return false;
    }

    if (s_browserAddState.load() == MUSIC_LIBRARY_BROWSER_ADD_STATE_RUNNING) {
        set_browser_refresh_status("Wait for the current playlist add to finish first");
        return false;
    }

    BrowserRefreshRequest *request = new BrowserRefreshRequest{root, browser_state(root).currentPath, true};
    s_browserRefreshState.store(MUSIC_LIBRARY_BROWSER_REFRESH_STATE_RUNNING);
    set_browser_refresh_status("Reindexing folder playlist...");

    TaskHandle_t task = nullptr;
    if (xTaskCreatePinnedToCoreWithCaps(
            music_library_browser_refresh_task,
            "music_browser_reindex",
            12 * 1024,
            request,
            4,
            &task,
            tskNO_AFFINITY,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        delete request;
        s_browserRefreshState.store(MUSIC_LIBRARY_BROWSER_REFRESH_STATE_FAILED);
        set_browser_refresh_status("Failed to start folder reindex");
        return false;
    }

    return true;
}

extern "C" bool music_library_browser_add_entry(music_library_storage_root_t root, uint32_t entryId)
{
    BrowserState &state = browser_state(root);
    if (entryId >= state.entries.size()) {
        set_last_message("Invalid selection");
        return false;
    }

    const BrowserEntry &entry = state.entries[entryId];
    if (!entry.canAdd) {
        set_last_message("This selection cannot be added");
        return false;
    }

    if (entry.isDirectory) {
        if (!add_folder_to_playlist(entry.path, root, false)) {
            set_last_message("No supported files found in that folder");
            return false;
        }
        return true;
    }

    if (!add_path_to_playlist(entry.path, root, true)) {
        set_last_message(playlist_contains_path(entry.path) ? "That file is already in the playlist" : "Failed to add file");
        return false;
    }

    set_last_message(MusicString("Added ") + entry.name);
    return true;
}

extern "C" bool music_library_browser_add_entry_async(music_library_storage_root_t root, uint32_t entryId)
{
    if (s_browserAddState.load() == MUSIC_LIBRARY_BROWSER_ADD_STATE_RUNNING) {
        set_last_message("Playlist add already in progress");
        set_browser_add_status(s_lastMessage);
        return false;
    }

    BrowserState &state = browser_state(root);
    if (entryId >= state.entries.size()) {
        set_last_message("Invalid selection");
        set_browser_add_status(s_lastMessage);
        return false;
    }

    const BrowserEntry &entry = state.entries[entryId];
    if (!entry.canAdd) {
        set_last_message("This selection cannot be added");
        set_browser_add_status(s_lastMessage);
        return false;
    }

    BrowserAddRequest *request = new BrowserAddRequest{root, entry.path, entry.name, entry.isDirectory, false};
    s_browserAddState.store(MUSIC_LIBRARY_BROWSER_ADD_STATE_RUNNING);
    set_browser_add_status(entry.isDirectory ? "Adding folder to playlist..." : "Adding file to playlist...");

    TaskHandle_t task = nullptr;
    if (xTaskCreatePinnedToCoreWithCaps(
            music_library_browser_add_task,
            "music_add_entry",
            10 * 1024,
            request,
            4,
            &task,
            tskNO_AFFINITY,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        delete request;
        s_browserAddState.store(MUSIC_LIBRARY_BROWSER_ADD_STATE_FAILED);
        set_last_message("Failed to start playlist add");
        set_browser_add_status(s_lastMessage);
        return false;
    }

    return true;
}

extern "C" music_library_browser_add_state_t music_library_browser_add_get_state(void)
{
    return s_browserAddState.load();
}

extern "C" const char *music_library_browser_add_get_status(void)
{
    return s_browserAddStatus;
}

extern "C" void music_library_browser_add_reset(void)
{
    if (s_browserAddState.load() != MUSIC_LIBRARY_BROWSER_ADD_STATE_RUNNING) {
        s_browserAddState.store(MUSIC_LIBRARY_BROWSER_ADD_STATE_IDLE);
        set_browser_add_status("Idle");
    }
}

extern "C" music_library_browser_refresh_state_t music_library_browser_refresh_get_state(void)
{
    return s_browserRefreshState.load();
}

extern "C" const char *music_library_browser_refresh_get_status(void)
{
    return s_browserRefreshStatus;
}

extern "C" void music_library_browser_refresh_reset(void)
{
    if (s_browserRefreshState.load() != MUSIC_LIBRARY_BROWSER_REFRESH_STATE_RUNNING) {
        s_browserRefreshState.store(MUSIC_LIBRARY_BROWSER_REFRESH_STATE_IDLE);
        set_browser_refresh_status("Idle");
    }
}

extern "C" bool music_library_download_start(const char *url)
{
    if ((url == nullptr) || (url[0] == '\0')) {
        set_download_status("Enter a download URL first.");
        s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_FAILED);
        return false;
    }
    if (s_downloadState.load() == MUSIC_LIBRARY_DOWNLOAD_STATE_RUNNING) {
        return false;
    }

    s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_RUNNING);
    s_downloadProgress.store(-1);
    set_download_status("Checking URL...");

    MusicString *urlCopy = new MusicString(url);
    TaskHandle_t task = nullptr;
    if (xTaskCreatePinnedToCoreWithCaps(
            music_library_download_task,
            "music_url_dl",
            10 * 1024,
            urlCopy,
            4,
            &task,
            tskNO_AFFINITY,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        delete urlCopy;
        set_download_status("Failed to start download task.");
        s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_FAILED);
        return false;
    }
    return true;
}

extern "C" music_library_download_state_t music_library_download_get_state(void)
{
    return s_downloadState.load();
}

extern "C" int32_t music_library_download_get_progress(void)
{
    return s_downloadProgress.load();
}

extern "C" const char *music_library_download_get_status(void)
{
    return s_downloadStatus;
}

extern "C" void music_library_download_reset(void)
{
    if (s_downloadState.load() != MUSIC_LIBRARY_DOWNLOAD_STATE_RUNNING) {
        s_downloadState.store(MUSIC_LIBRARY_DOWNLOAD_STATE_IDLE);
        s_downloadProgress.store(-1);
        set_download_status("Idle");
    }
}