#include "music_library.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdio>
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

std::string trim_extension(const std::string &name);
std::string make_parent_label(const std::string &path);

uint32_t read_syncsafe_u32(const uint8_t *data)
{
    return (static_cast<uint32_t>(data[0] & 0x7F) << 21) |
           (static_cast<uint32_t>(data[1] & 0x7F) << 14) |
           (static_cast<uint32_t>(data[2] & 0x7F) << 7) |
           static_cast<uint32_t>(data[3] & 0x7F);
}

uint32_t read_be_u32(const uint8_t *data)
{
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

std::string trim_ascii_whitespace(const std::string &value)
{
    const size_t start = value.find_first_not_of(" \t\r\n\0", 0);
    if (start == std::string::npos) {
        return "";
    }

    const size_t end = value.find_last_not_of(" \t\r\n\0");
    return value.substr(start, end - start + 1);
}

std::string decode_latin1_text(const uint8_t *data, size_t length)
{
    std::string text;
    text.reserve(length);
    for (size_t index = 0; index < length; ++index) {
        const uint8_t byte = data[index];
        if (byte == 0) {
            break;
        }

        if (byte < 0x80) {
            text.push_back(static_cast<char>(byte));
        } else {
            text.push_back(static_cast<char>(0xC0 | (byte >> 6)));
            text.push_back(static_cast<char>(0x80 | (byte & 0x3F)));
        }
    }

    return trim_ascii_whitespace(text);
}

std::string decode_utf16_text(const uint8_t *data, size_t length, bool default_big_endian)
{
    if (length < 2) {
        return "";
    }

    bool big_endian = default_big_endian;
    size_t offset = 0;
    if ((length >= 2) && (data[0] == 0xFF) && (data[1] == 0xFE)) {
        big_endian = false;
        offset = 2;
    } else if ((length >= 2) && (data[0] == 0xFE) && (data[1] == 0xFF)) {
        big_endian = true;
        offset = 2;
    }

    std::string text;
    text.reserve(length / 2);
    for (; (offset + 1) < length; offset += 2) {
        const uint16_t code_unit = big_endian
                                       ? static_cast<uint16_t>((data[offset] << 8) | data[offset + 1])
                                       : static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
        if (code_unit == 0) {
            break;
        }

        if (code_unit < 0x80) {
            text.push_back(static_cast<char>(code_unit));
        } else if (code_unit < 0x800) {
            text.push_back(static_cast<char>(0xC0 | (code_unit >> 6)));
            text.push_back(static_cast<char>(0x80 | (code_unit & 0x3F)));
        } else {
            text.push_back(static_cast<char>(0xE0 | (code_unit >> 12)));
            text.push_back(static_cast<char>(0x80 | ((code_unit >> 6) & 0x3F)));
            text.push_back(static_cast<char>(0x80 | (code_unit & 0x3F)));
        }
    }

    return trim_ascii_whitespace(text);
}

std::string decode_id3_text_frame(const uint8_t *data, size_t length)
{
    if ((data == nullptr) || (length == 0)) {
        return "";
    }

    const uint8_t encoding = data[0];
    const uint8_t *payload = data + 1;
    const size_t payload_length = length - 1;

    switch (encoding) {
    case 0:
    case 3:
        return decode_latin1_text(payload, payload_length);
    case 1:
        return decode_utf16_text(payload, payload_length, false);
    case 2:
        return decode_utf16_text(payload, payload_length, true);
    default:
        return "";
    }
}

std::string filename_from_path(const std::string &path)
{
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return path;
    }

    return path.substr(slash + 1);
}

bool load_mp3_id3v2_metadata(FILE *file, std::string &title, std::string &artist)
{
    uint8_t header[10] = {};
    if (fseek(file, 0, SEEK_SET) != 0) {
        return false;
    }
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
        return false;
    }
    if (std::memcmp(header, "ID3", 3) != 0) {
        return false;
    }

    const uint8_t version = header[3];
    const uint32_t tag_size = read_syncsafe_u32(&header[6]);
    const long tag_end = static_cast<long>(10 + tag_size);
    long cursor = 10;

    while ((cursor + 10) <= tag_end) {
        uint8_t frame_header[10] = {};
        if (fseek(file, cursor, SEEK_SET) != 0) {
            break;
        }
        if (fread(frame_header, 1, sizeof(frame_header), file) != sizeof(frame_header)) {
            break;
        }

        if ((frame_header[0] == 0) && (frame_header[1] == 0) && (frame_header[2] == 0) && (frame_header[3] == 0)) {
            break;
        }

        const uint32_t frame_size = (version >= 4) ? read_syncsafe_u32(&frame_header[4]) : read_be_u32(&frame_header[4]);
        if ((frame_size == 0) || (cursor + 10 + static_cast<long>(frame_size) > tag_end)) {
            break;
        }

        const std::string frame_id(reinterpret_cast<char *>(frame_header), 4);
        if ((frame_id == "TIT2") || (frame_id == "TPE1")) {
            std::vector<uint8_t> payload(frame_size);
            if (fread(payload.data(), 1, frame_size, file) != frame_size) {
                break;
            }

            const std::string text = decode_id3_text_frame(payload.data(), payload.size());
            if (!text.empty()) {
                if ((frame_id == "TIT2") && title.empty()) {
                    title = text;
                } else if ((frame_id == "TPE1") && artist.empty()) {
                    artist = text;
                }
            }
        }

        cursor += 10 + static_cast<long>(frame_size);
    }

    return !title.empty() || !artist.empty();
}

bool load_mp3_id3v1_metadata(FILE *file, std::string &title, std::string &artist)
{
    uint8_t tag[128] = {};
    if (fseek(file, -128, SEEK_END) != 0) {
        return false;
    }
    if (fread(tag, 1, sizeof(tag), file) != sizeof(tag)) {
        return false;
    }
    if (std::memcmp(tag, "TAG", 3) != 0) {
        return false;
    }

    if (title.empty()) {
        title = trim_ascii_whitespace(std::string(reinterpret_cast<char *>(&tag[3]), 30));
    }
    if (artist.empty()) {
        artist = trim_ascii_whitespace(std::string(reinterpret_cast<char *>(&tag[33]), 30));
    }

    return !title.empty() || !artist.empty();
}

void fill_track_metadata(TrackInfo &track)
{
    const std::string display_name = trim_extension(filename_from_path(track.path));

    if (track.genre.rfind("MP3", 0) == 0) {
        FILE *file = std::fopen(track.path.c_str(), "rb");
        if (file != nullptr) {
            std::string title;
            std::string artist;
            (void)load_mp3_id3v2_metadata(file, title, artist);
            if (title.empty() || artist.empty()) {
                (void)load_mp3_id3v1_metadata(file, title, artist);
            }
            std::fclose(file);

            if (!title.empty()) {
                track.title = title;
            }
            if (!artist.empty()) {
                track.artist = artist;
            }
        }
    }

    if (track.title.empty()) {
        track.title = display_name;
    }
    if (track.artist.empty()) {
        track.artist = make_parent_label(track.path);
    }
}

bool is_supported_extension(const std::string &path)
{
    static const char *extensions[] = {
        ".mp3", ".wav", ".wave"
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
            .title = "",
            .artist = "",
            .genre = make_genre_label(childPath),
            .path = childPath,
            .durationSeconds = 180,
        });
        fill_track_metadata(s_tracks.back());
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