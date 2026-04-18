#include "music_library.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <string>
#include <utility>
#include <vector>
#include <sys/stat.h>

#include "esp_log.h"
#include "bsp_board_extra.h"

namespace {

constexpr const char *kSdRoot = "/sdcard";

struct TrackInfo {
    std::string title;
    std::string artist;
    std::string genre;
    std::string path;
    uint32_t durationSeconds;
};

static const char *TAG = "music_library";
static const char *kEmptyTitle = "No SD media found";
static const char *kEmptyArtist = "Insert an SD card with audio files";
static const char *kEmptyGenre = "MP3, WAV";
static std::vector<TrackInfo> s_tracks;
static uint32_t s_currentIndex = 0;

bool is_supported_extension(const std::string &path)
{
    static const char *extensions[] = {
        ".mp3", ".wav"
    };

    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }

    std::string extension = path.substr(dot);
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

std::string trim_extension(const std::string &name)
{
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) {
        return name;
    }

    return name.substr(0, dot);
}

std::string make_relative_path(const std::string &path)
{
    if (path.rfind(kSdRoot, 0) == 0) {
        return path.substr(std::strlen(kSdRoot));
    }

    return path;
}

std::string make_parent_label(const std::string &path)
{
    const std::string relativePath = make_relative_path(path);
    const size_t slash = relativePath.find_last_of('/');
    if ((slash == std::string::npos) || (slash == 0)) {
        return "SD Card";
    }

    return relativePath.substr(0, slash);
}

std::string make_genre_label(const std::string &path)
{
    const size_t dot = path.find_last_of('.');
    std::string extension = (dot == std::string::npos) ? "Audio" : path.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    return extension + "  •  SD Card";
}

void scan_directory(const std::string &path)
{
    DIR *dir = opendir(path.c_str());
    if (dir == nullptr) {
        return;
    }

    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if ((std::strcmp(entry->d_name, ".") == 0) || (std::strcmp(entry->d_name, "..") == 0)) {
            continue;
        }

        const std::string childPath = path + "/" + entry->d_name;
        struct stat st = {};
        if (stat(childPath.c_str(), &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            scan_directory(childPath);
            continue;
        }

        if (!is_supported_extension(childPath)) {
            continue;
        }

        s_tracks.push_back({
            .title = trim_extension(entry->d_name),
            .artist = make_parent_label(childPath),
            .genre = make_genre_label(childPath),
            .path = childPath,
            .durationSeconds = 180,
        });
    }

    closedir(dir);
}

const TrackInfo *get_track(uint32_t track_id)
{
    if (track_id >= s_tracks.size()) {
        return nullptr;
    }

    return &s_tracks[track_id];
}

} // namespace

extern "C" bool music_library_refresh(void)
{
    s_tracks.clear();
    scan_directory(kSdRoot);

    std::sort(s_tracks.begin(), s_tracks.end(), [](const TrackInfo &left, const TrackInfo &right) {
        return left.path < right.path;
    });

    if (s_currentIndex >= s_tracks.size()) {
        s_currentIndex = 0;
    }

    ESP_LOGI(TAG, "Discovered %u media files on SD card", static_cast<unsigned int>(s_tracks.size()));
    return !s_tracks.empty();
}

extern "C" uint32_t music_library_get_count(void)
{
    return static_cast<uint32_t>(s_tracks.size());
}

extern "C" const char *music_library_get_title(uint32_t track_id)
{
    const TrackInfo *track = get_track(track_id);
    return (track == nullptr) ? kEmptyTitle : track->title.c_str();
}

extern "C" const char *music_library_get_artist(uint32_t track_id)
{
    const TrackInfo *track = get_track(track_id);
    return (track == nullptr) ? kEmptyArtist : track->artist.c_str();
}

extern "C" const char *music_library_get_genre(uint32_t track_id)
{
    const TrackInfo *track = get_track(track_id);
    return (track == nullptr) ? kEmptyGenre : track->genre.c_str();
}

extern "C" uint32_t music_library_get_track_length(uint32_t track_id)
{
    const TrackInfo *track = get_track(track_id);
    return (track == nullptr) ? 0 : track->durationSeconds;
}

extern "C" const char *music_library_get_path(uint32_t track_id)
{
    const TrackInfo *track = get_track(track_id);
    return (track == nullptr) ? nullptr : track->path.c_str();
}

extern "C" bool music_library_play(uint32_t track_id)
{
    const TrackInfo *track = get_track(track_id);
    if (track == nullptr) {
        return false;
    }

    if (bsp_extra_player_play_file(track->path.c_str()) != ESP_OK) {
        return false;
    }

    s_currentIndex = track_id;
    return true;
}

extern "C" bool music_library_is_playing(uint32_t track_id)
{
    const TrackInfo *track = get_track(track_id);
    if (track == nullptr) {
        return false;
    }

    return bsp_extra_player_is_playing_by_path(track->path.c_str());
}

extern "C" uint32_t music_library_get_current_index(void)
{
    return s_currentIndex;
}

extern "C" bool music_library_set_current_index(uint32_t track_id)
{
    if (track_id >= s_tracks.size()) {
        return false;
    }

    s_currentIndex = track_id;
    return true;
}