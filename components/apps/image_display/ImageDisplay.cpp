#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>
#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_system.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "driver/jpeg_decode.h"
#include "jpeglib.h"
#include "../../../managed_components/lvgl__lvgl/src/extra/libs/sjpg/tjpgd.h"
#include "ImageDisplay.hpp"
#include "app_gui/app_image_display.h"
#include "storage_access.h"

#define IMAGE_DIR "/sdcard/image"
#define IMAGE_DIR_FALLBACK "/sdcard/imagefolder"
#define IMAGE_SYS_DIR "/sdcard/sys"
#define IMAGE_THUMB_CACHE_DIR "/sdcard/sys/thumbs"

namespace {

constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 800;
constexpr int kTileWidth = 212;
constexpr int kTileHeight = 168;
constexpr int kTilePreviewWidth = kTileWidth - 24;
constexpr int kTilePreviewHeight = 96;
constexpr int kHeaderHeight = 82;
constexpr int kTileTapSlopPx = 24;
constexpr uint32_t kImageDirectoryPollMs = 2000;
constexpr uint32_t kTransitionDurationMs = 420;
constexpr int64_t kIdleSlideshowUs = 15LL * 1000LL * 1000LL;
constexpr int kTransitionCount = 20;
constexpr size_t kTjpgdWorkspaceBytes = 16U * 1024U;
constexpr uint32_t kThumbnailCacheMagic = 0x54484D42U;
constexpr uint32_t kThumbnailCacheVersion = 1U;

SemaphoreHandle_t image_task_event = nullptr;
SemaphoreHandle_t image_muxe = nullptr;
SemaphoreHandle_t s_thumbnail_decode_event = nullptr;
SemaphoreHandle_t s_thumbnail_decode_mutex = nullptr;
esp_timer_handle_t time_refer_handle = nullptr;

lv_obj_t *s_gallery_screen = nullptr;
lv_obj_t *s_gallery_header = nullptr;
lv_obj_t *s_gallery_count_label = nullptr;
lv_obj_t *s_gallery_grid = nullptr;
lv_obj_t *s_viewer_layer = nullptr;
lv_obj_t *s_viewer_topbar = nullptr;
lv_obj_t *s_viewer_title = nullptr;
lv_obj_t *s_viewer_hint = nullptr;
lv_obj_t *s_transition_badge = nullptr;
lv_obj_t *s_viewer_canvas[2] = {nullptr, nullptr};
lv_color_t *s_viewer_buffers[2] = {nullptr, nullptr};
std::vector<lv_color_t *> s_tile_preview_buffers;
lv_timer_t *s_thumbnail_loader_timer = nullptr;

AppImageDisplay *s_active_app = nullptr;
int s_visible_canvas = 0;
int s_current_index = -1;
int s_last_transition = -1;
bool s_fullscreen_visible = false;
int s_debug_pending_open_index = -1;
bool s_debug_pending_open_animate = false;
const char *s_debug_pending_open_reason = nullptr;
bool s_gallery_rebuild_pending = false;
int s_tile_press_index = -1;
lv_point_t s_tile_press_point = {0, 0};
bool s_tile_press_moved = false;

static const char *TAG = "AppImageDisplay";

struct TjpgdSession {
    const uint8_t *input = nullptr;
    size_t input_size = 0;
    size_t input_offset = 0;
    uint8_t *decoded_rgb888 = nullptr;
    uint32_t decoded_width = 0;
    uint32_t decoded_height = 0;
};

struct LibjpegErrorManager {
    jpeg_error_mgr base;
    jmp_buf jump_buffer;
};

struct PendingPreview {
    size_t index = 0;
    lv_obj_t *preview = nullptr;
    lv_obj_t *canvas = nullptr;
    lv_obj_t *status = nullptr;
    lv_color_t *buffer = nullptr;
};

struct PendingViewerRequest {
    AppImageDisplay *app = nullptr;
    int index = -1;
    bool animate = false;
    const char *reason = nullptr;
};

struct PendingImageListRefresh {
    AppImageDisplay *app = nullptr;
    std::vector<std::string> image_paths;
};

struct ThumbnailDecodeRequest {
    AppImageDisplay *app = nullptr;
    size_t index = 0;
    uint32_t generation = 0;
    lv_obj_t *canvas = nullptr;
    lv_obj_t *status = nullptr;
    lv_color_t *target_buffer = nullptr;
    std::string path;
};

struct ThumbnailDecodeResult {
    AppImageDisplay *app = nullptr;
    size_t index = 0;
    uint32_t generation = 0;
    lv_obj_t *canvas = nullptr;
    lv_obj_t *status = nullptr;
    lv_color_t *target_buffer = nullptr;
    lv_color_t *decoded_buffer = nullptr;
    bool decoded = false;
    bool cache_hit = false;
};

struct ThumbnailCacheHeader {
    uint32_t magic = kThumbnailCacheMagic;
    uint32_t version = kThumbnailCacheVersion;
    uint32_t width = kTilePreviewWidth;
    uint32_t height = kTilePreviewHeight;
    uint32_t pixel_size = sizeof(lv_color_t);
    uint64_t source_size = 0;
    int64_t source_mtime = 0;
};

std::vector<PendingPreview> s_pending_previews;
size_t s_next_pending_preview = 0;
ThumbnailDecodeRequest s_thumbnail_decode_request;
bool s_thumbnail_decode_in_flight = false;
uint32_t s_thumbnail_generation = 1;

static void render_rgb565_to_buffer(const uint16_t *source,
                                    uint32_t source_width,
                                    uint32_t source_height,
                                    uint32_t source_stride,
                                    lv_color_t *destination,
                                    uint32_t destination_width,
                                    uint32_t destination_height);
static void render_rgb888_to_buffer(const uint8_t *source,
                                    uint32_t source_width,
                                    uint32_t source_height,
                                    lv_color_t *destination,
                                    uint32_t destination_width,
                                    uint32_t destination_height);
static void render_cmyk_to_buffer(const uint8_t *source,
                                  uint32_t source_width,
                                  uint32_t source_height,
                                  lv_color_t *destination,
                                  uint32_t destination_width,
                                  uint32_t destination_height);
static void render_rgb565a8_to_buffer(const uint8_t *source,
                                      uint32_t source_width,
                                      uint32_t source_height,
                                      lv_color_t *destination,
                                      uint32_t destination_width,
                                      uint32_t destination_height);
static bool decode_jpeg_to_buffer(const std::string &path,
                                  lv_color_t *destination,
                                  uint32_t destination_width,
                                  uint32_t destination_height);
static bool decode_jpeg_with_tjpgd(const std::string &path,
                                   const uint8_t *input_buffer,
                                   size_t input_size,
                                   lv_color_t *destination,
                                   uint32_t destination_width,
                                   uint32_t destination_height);
static bool decode_jpeg_with_libjpeg_turbo(const std::string &path,
                                           const uint8_t *input_buffer,
                                           size_t input_size,
                                           lv_color_t *destination,
                                           uint32_t destination_width,
                                           uint32_t destination_height);
static bool decode_png_to_buffer(const std::string &path,
                                 lv_color_t *destination,
                                 uint32_t destination_width,
                                 uint32_t destination_height);
static void draw_preview_placeholder(lv_obj_t *canvas, lv_color_t *preview_buffer, const std::string &path);
static bool queue_lvgl_async_locked(lv_async_cb_t callback, void *user_data);
static void show_image_index_async(void *context);
static void thumbnail_loader_timer_cb(lv_timer_t *timer);
static void start_thumbnail_loader();
static void stop_thumbnail_loader();
static void build_gallery_ui(AppImageDisplay *app);
static void free_tile_preview_buffers();
static void populate_gallery_tiles(AppImageDisplay *app);
static void update_gallery_count_label(size_t image_count);
static void apply_image_path_refresh_async(void *context);
static void image_directory_refresh_task(void *context);
static void apply_thumbnail_decode_async(void *context);
static void thumbnail_decode_task(void *context);
static bool decode_image_to_buffer(const std::string &path,
                                   lv_color_t *destination,
                                   uint32_t destination_width,
                                   uint32_t destination_height);
static bool load_or_generate_thumbnail(const std::string &path,
                                       lv_color_t *destination,
                                       bool *cache_hit);
static void prune_thumbnail_cache(const std::vector<std::string> &active_paths);

struct TransitionRecipe {
    int32_t incomingStartX;
    int32_t incomingStartY;
    int32_t outgoingEndX;
    int32_t outgoingEndY;
    int32_t incomingStartOpa;
    int32_t outgoingEndOpa;
    int32_t incomingStartZoom;
    int32_t outgoingEndZoom;
    int32_t incomingStartAngle;
    int32_t outgoingEndAngle;
};

constexpr TransitionRecipe kTransitionRecipes[kTransitionCount] = {
    {0, 0, 0, 0, 0, 0, 256, 256, 0, 0},
    {kScreenWidth, 0, -kScreenWidth, 0, 255, 255, 256, 256, 0, 0},
    {-kScreenWidth, 0, kScreenWidth, 0, 255, 255, 256, 256, 0, 0},
    {0, kScreenHeight, 0, -kScreenHeight, 255, 255, 256, 256, 0, 0},
    {0, -kScreenHeight, 0, kScreenHeight, 255, 255, 256, 256, 0, 0},
    {0, 0, 0, 0, 0, 0, 168, 320, 0, 0},
    {0, 0, 0, 0, 0, 0, 360, 180, 0, 0},
    {kScreenWidth, 0, 0, 0, 0, 0, 256, 200, 0, 0},
    {-kScreenWidth, 0, 0, 0, 0, 0, 256, 200, 0, 0},
    {0, kScreenHeight, 0, 0, 0, 0, 256, 200, 0, 0},
    {0, -kScreenHeight, 0, 0, 0, 0, 256, 200, 0, 0},
    {kScreenWidth, kScreenHeight, -kScreenWidth / 2, -kScreenHeight / 2, 0, 0, 256, 256, 0, 0},
    {-kScreenWidth, kScreenHeight, kScreenWidth / 2, -kScreenHeight / 2, 0, 0, 256, 256, 0, 0},
    {kScreenWidth, -kScreenHeight, -kScreenWidth / 2, kScreenHeight / 2, 0, 0, 256, 256, 0, 0},
    {-kScreenWidth, -kScreenHeight, kScreenWidth / 2, kScreenHeight / 2, 0, 0, 256, 256, 0, 0},
    {0, 0, 0, 0, 0, 0, 220, 300, 120, -90},
    {0, 0, 0, 0, 0, 0, 220, 300, -120, 90},
    {kScreenWidth / 3, 0, -kScreenWidth / 4, 0, 0, 0, 192, 352, 0, 45},
    {-kScreenWidth / 3, 0, kScreenWidth / 4, 0, 0, 0, 192, 352, 0, -45},
    {0, 0, 0, 0, 0, 0, 128, 420, 240, -180},
};

constexpr const char *kTransitionNames[kTransitionCount] = {
    "Fade",
    "Slide Left",
    "Slide Right",
    "Slide Up",
    "Slide Down",
    "Soft Zoom In",
    "Soft Zoom Out",
    "Drift Left",
    "Drift Right",
    "Drift Up",
    "Drift Down",
    "Diagonal South-East",
    "Diagonal South-West",
    "Diagonal North-East",
    "Diagonal North-West",
    "Spin Lift",
    "Spin Drop",
    "Parallax Left",
    "Parallax Right",
    "Orbit Fade",
};

static bool has_supported_image_extension(const char *name)
{
    if (name == nullptr) {
        return false;
    }

    const char *extension = strrchr(name, '.');
    if (extension == nullptr) {
        return false;
    }

    std::string lowered_extension(extension);
    std::transform(lowered_extension.begin(), lowered_extension.end(), lowered_extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });

    return (lowered_extension == ".jpg") || (lowered_extension == ".jpeg") ||
           (lowered_extension == ".jpe") || (lowered_extension == ".jfif") ||
           (lowered_extension == ".png");
}

static bool is_png_path(const std::string &path)
{
    const char *extension = strrchr(path.c_str(), '.');
    if (extension == nullptr) {
        return false;
    }

    std::string lowered_extension(extension);
    std::transform(lowered_extension.begin(), lowered_extension.end(), lowered_extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return lowered_extension == ".png";
}

static void scan_supported_images_in_directory(const char *directory_path, std::vector<std::string> &output_paths)
{
    DIR *directory = opendir(directory_path);
    if (directory == nullptr) {
        return;
    }

    struct dirent *entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        if (!has_supported_image_extension(entry->d_name)) {
            continue;
        }
        output_paths.emplace_back(std::string(directory_path) + "/" + entry->d_name);
    }

    closedir(directory);
}

static std::vector<std::string> scan_supported_image_paths()
{
    std::vector<std::string> image_paths;

    const char *directories[] = {
        IMAGE_DIR,
        IMAGE_DIR_FALLBACK,
    };

    if (!app_storage_ensure_sdcard_available()) {
        ESP_LOGW(TAG, "SD card is unavailable for image scan");
        return image_paths;
    }

    for (size_t index = 0; index < (sizeof(directories) / sizeof(directories[0])); ++index) {
        ESP_LOGI(TAG, "Scanning image directory %s", directories[index]);
        scan_supported_images_in_directory(directories[index], image_paths);
    }

    std::sort(image_paths.begin(), image_paths.end());
    image_paths.erase(std::unique(image_paths.begin(), image_paths.end()), image_paths.end());
    ESP_LOGI(TAG, "Found %u supported images on SD card", static_cast<unsigned>(image_paths.size()));
    for (size_t index = 0; index < image_paths.size(); ++index) {
        ESP_LOGI(TAG, "image[%u]=%s", static_cast<unsigned>(index), image_paths[index].c_str());
    }

    return image_paths;
}

static BaseType_t create_background_task_prefer_psram(TaskFunction_t task,
                                                      const char *name,
                                                      const uint32_t stack_depth,
                                                      void *arg,
                                                      const UBaseType_t priority,
                                                      TaskHandle_t *task_handle,
                                                      const BaseType_t core_id)
{
    if (xTaskCreatePinnedToCoreWithCaps(task,
                                        name,
                                        stack_depth,
                                        arg,
                                        priority,
                                        task_handle,
                                        core_id,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        return pdPASS;
    }

    ESP_LOGW(TAG,
             "Falling back to internal RAM stack for %s. Internal free=%u largest=%u PSRAM free=%u",
             name,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    return xTaskCreatePinnedToCore(task, name, stack_depth, arg, priority, task_handle, core_id);
}

static const char *base_name(const std::string &path)
{
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return path.c_str();
    }
    return path.c_str() + slash + 1;
}

static std::string file_extension_label(const std::string &path)
{
    const char *extension = strrchr(path.c_str(), '.');
    if (extension == nullptr) {
        return "FILE";
    }

    std::string label(extension + 1);
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    return label;
}

static bool ensure_directory_exists(const char *path)
{
    if ((path == nullptr) || (*path == '\0')) {
        return false;
    }

    struct stat info = {};
    if (stat(path, &info) == 0) {
        return S_ISDIR(info.st_mode);
    }

    if (mkdir(path, 0777) == 0) {
        return true;
    }

    if (errno == EEXIST) {
        return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
    }

    ESP_LOGW(TAG, "Failed to create directory %s errno=%d", path, errno);
    return false;
}

static bool ensure_thumbnail_cache_dirs()
{
    if (!app_storage_ensure_sdcard_available()) {
        return false;
    }

    return ensure_directory_exists(IMAGE_SYS_DIR) && ensure_directory_exists(IMAGE_THUMB_CACHE_DIR);
}

static bool get_file_metadata(const std::string &path, uint64_t *file_size, int64_t *mtime)
{
    if ((file_size == nullptr) || (mtime == nullptr)) {
        return false;
    }

    struct stat info = {};
    if (stat(path.c_str(), &info) != 0) {
        ESP_LOGW(TAG, "stat failed for %s errno=%d", path.c_str(), errno);
        return false;
    }

    *file_size = static_cast<uint64_t>(info.st_size);
    *mtime = static_cast<int64_t>(info.st_mtime);
    return true;
}

static uint64_t stable_path_hash(const std::string &path)
{
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char value : path) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static std::string thumbnail_cache_path_for_image(const std::string &path)
{
    char buffer[96] = {};
    snprintf(buffer,
             sizeof(buffer),
             "%s/%016llx.thm",
             IMAGE_THUMB_CACHE_DIR,
             static_cast<unsigned long long>(stable_path_hash(path)));
    return std::string(buffer);
}

static bool load_thumbnail_cache(const std::string &path, lv_color_t *destination)
{
    if (destination == nullptr) {
        return false;
    }

    uint64_t source_size = 0;
    int64_t source_mtime = 0;
    if (!get_file_metadata(path, &source_size, &source_mtime)) {
        return false;
    }

    FILE *file = fopen(thumbnail_cache_path_for_image(path).c_str(), "rb");
    if (file == nullptr) {
        return false;
    }

    ThumbnailCacheHeader header = {};
    const bool valid_header = fread(&header, sizeof(header), 1, file) == 1 &&
                              header.magic == kThumbnailCacheMagic &&
                              header.version == kThumbnailCacheVersion &&
                              header.width == kTilePreviewWidth &&
                              header.height == kTilePreviewHeight &&
                              header.pixel_size == sizeof(lv_color_t) &&
                              header.source_size == source_size &&
                              header.source_mtime == source_mtime;
    if (!valid_header) {
        fclose(file);
        return false;
    }

    const size_t thumbnail_bytes = static_cast<size_t>(kTilePreviewWidth) * static_cast<size_t>(kTilePreviewHeight) * sizeof(lv_color_t);
    const bool read_ok = fread(destination, 1, thumbnail_bytes, file) == thumbnail_bytes;
    fclose(file);
    return read_ok;
}

static void store_thumbnail_cache(const std::string &path, const lv_color_t *source)
{
    if ((source == nullptr) || !ensure_thumbnail_cache_dirs()) {
        return;
    }

    ThumbnailCacheHeader header = {};
    if (!get_file_metadata(path, &header.source_size, &header.source_mtime)) {
        return;
    }

    const std::string cache_path = thumbnail_cache_path_for_image(path);
    const std::string temp_path = cache_path + ".tmp";
    FILE *file = fopen(temp_path.c_str(), "wb");
    if (file == nullptr) {
        ESP_LOGW(TAG, "Failed to create thumbnail cache %s", temp_path.c_str());
        return;
    }

    const size_t thumbnail_bytes = static_cast<size_t>(kTilePreviewWidth) * static_cast<size_t>(kTilePreviewHeight) * sizeof(lv_color_t);
    const bool write_ok = fwrite(&header, sizeof(header), 1, file) == 1 &&
                          fwrite(source, 1, thumbnail_bytes, file) == thumbnail_bytes;
    fclose(file);
    if (!write_ok) {
        std::remove(temp_path.c_str());
        ESP_LOGW(TAG, "Failed to write thumbnail cache for %s", path.c_str());
        return;
    }

    std::remove(cache_path.c_str());
    if (rename(temp_path.c_str(), cache_path.c_str()) != 0) {
        std::remove(temp_path.c_str());
        ESP_LOGW(TAG, "Failed to finalize thumbnail cache for %s errno=%d", path.c_str(), errno);
    }
}

static bool load_or_generate_thumbnail(const std::string &path,
                                       lv_color_t *destination,
                                       bool *cache_hit)
{
    if (cache_hit != nullptr) {
        *cache_hit = false;
    }

    if (load_thumbnail_cache(path, destination)) {
        if (cache_hit != nullptr) {
            *cache_hit = true;
        }
        return true;
    }

    if (!decode_image_to_buffer(path, destination, kTilePreviewWidth, kTilePreviewHeight)) {
        return false;
    }

    store_thumbnail_cache(path, destination);
    return true;
}

static void prune_thumbnail_cache(const std::vector<std::string> &active_paths)
{
    if (!ensure_thumbnail_cache_dirs()) {
        return;
    }

    std::vector<std::string> expected_paths;
    expected_paths.reserve(active_paths.size());
    for (const std::string &path : active_paths) {
        expected_paths.push_back(thumbnail_cache_path_for_image(path));
    }
    std::sort(expected_paths.begin(), expected_paths.end());
    expected_paths.erase(std::unique(expected_paths.begin(), expected_paths.end()), expected_paths.end());

    DIR *directory = opendir(IMAGE_THUMB_CACHE_DIR);
    if (directory == nullptr) {
        return;
    }

    size_t removed_count = 0;
    struct dirent *entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }

        const std::string candidate = std::string(IMAGE_THUMB_CACHE_DIR) + "/" + entry->d_name;
        if (std::binary_search(expected_paths.begin(), expected_paths.end(), candidate)) {
            continue;
        }

        if (std::remove(candidate.c_str()) == 0) {
            ++removed_count;
        }
    }

    closedir(directory);
    if (removed_count > 0U) {
        ESP_LOGI(TAG,
                 "thumbnail cache pruned removed=%u active=%u",
                 static_cast<unsigned>(removed_count),
                 static_cast<unsigned>(active_paths.size()));
    }
}

static lv_color_t tile_color_for_path(const std::string &path)
{
    uint32_t hash = 2166136261u;
    for (char value : path) {
        hash ^= static_cast<uint8_t>(value);
        hash *= 16777619u;
    }

    const uint8_t red = static_cast<uint8_t>(96 + (hash & 0x3f));
    const uint8_t green = static_cast<uint8_t>(72 + ((hash >> 8) & 0x5f));
    const uint8_t blue = static_cast<uint8_t>(80 + ((hash >> 16) & 0x5f));
    return lv_color_make(red, green, blue);
}

static void stop_idle_slideshow()
{
    if (time_refer_handle != nullptr) {
        const esp_err_t result = esp_timer_stop(time_refer_handle);
        if ((result != ESP_OK) && (result != ESP_ERR_INVALID_STATE)) {
            ESP_LOGW(TAG, "Failed to stop image idle timer: %s", esp_err_to_name(result));
        } else {
            ESP_LOGI(TAG, "idle slideshow stop result=%s visible=%s current_index=%d",
                     esp_err_to_name(result),
                     s_fullscreen_visible ? "yes" : "no",
                     s_current_index);
        }
    }
}

static void arm_idle_slideshow()
{
    stop_idle_slideshow();
    if (!s_fullscreen_visible || (s_active_app == nullptr) || (s_active_app->imagePathCount() < 2)) {
        ESP_LOGI(TAG,
                 "idle slideshow not armed visible=%s app=%p count=%u",
                 s_fullscreen_visible ? "yes" : "no",
                 s_active_app,
                 s_active_app == nullptr ? 0U : static_cast<unsigned>(s_active_app->imagePathCount()));
        return;
    }

    const esp_err_t result = esp_timer_start_once(time_refer_handle, kIdleSlideshowUs);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Failed to arm image idle timer: %s", esp_err_to_name(result));
    } else {
        ESP_LOGI(TAG, "idle slideshow armed delay_us=%lld current_index=%d", kIdleSlideshowUs, s_current_index);
    }
}

static void note_viewer_interaction()
{
    ESP_LOGI(TAG, "viewer interaction visible=%s current_index=%d", s_fullscreen_visible ? "yes" : "no", s_current_index);
    arm_idle_slideshow();
}

static void reset_tile_press_state()
{
    s_tile_press_index = -1;
    s_tile_press_point.x = 0;
    s_tile_press_point.y = 0;
    s_tile_press_moved = false;
}

static bool queue_viewer_request(AppImageDisplay *app, int index, bool animate, const char *reason)
{
    if (app == nullptr) {
        ESP_LOGW(TAG, "queue_viewer_request ignored null app index=%d", index);
        return false;
    }

    PendingViewerRequest *request = new (std::nothrow) PendingViewerRequest{
        .app = app,
        .index = index,
        .animate = animate,
        .reason = reason,
    };
    if (request == nullptr) {
        ESP_LOGE(TAG, "queue_viewer_request alloc failed index=%d reason=%s", index, reason == nullptr ? "unknown" : reason);
        return false;
    }

    if (!queue_lvgl_async_locked(show_image_index_async, request)) {
        ESP_LOGW(TAG, "queue_viewer_request async queue failed index=%d reason=%s", index, reason == nullptr ? "unknown" : reason);
        delete request;
        return false;
    }

    return true;
}

static void maybe_queue_debug_open(AppImageDisplay *app)
{
    if ((app == nullptr) || (s_debug_pending_open_index < 0)) {
        return;
    }

    const int index = s_debug_pending_open_index;
    const bool animate = s_debug_pending_open_animate;
    const char *reason = (s_debug_pending_open_reason != nullptr) ? s_debug_pending_open_reason : "debug-open";

    if (index >= static_cast<int>(app->imagePathCount())) {
        ESP_LOGW(TAG, "maybe_queue_debug_open rejected index=%d count=%u", index, static_cast<unsigned>(app->imagePathCount()));
        s_debug_pending_open_index = -1;
        s_debug_pending_open_animate = false;
        s_debug_pending_open_reason = nullptr;
        return;
    }

    ESP_LOGI(TAG, "maybe_queue_debug_open index=%d animate=%s reason=%s", index, animate ? "yes" : "no", reason);
    if (queue_viewer_request(app, index, animate, reason)) {
        s_debug_pending_open_index = -1;
        s_debug_pending_open_animate = false;
        s_debug_pending_open_reason = nullptr;
    }
}

static bool queue_lvgl_async_locked(lv_async_cb_t callback, void *user_data)
{
    if (!bsp_display_lock(0)) {
        ESP_LOGW(TAG, "LVGL async queue failed to lock callback=%p", reinterpret_cast<void *>(callback));
        return false;
    }

    const lv_res_t result = lv_async_call(callback, user_data);
    bsp_display_unlock();
    if (result != LV_RES_OK) {
        ESP_LOGW(TAG, "LVGL async queue failed result=%d callback=%p", static_cast<int>(result), reinterpret_cast<void *>(callback));
    }
    return result == LV_RES_OK;
}

static uint32_t align_up_to_16(uint32_t value)
{
    return (value + 15U) & ~15U;
}

static bool read_jpeg_info_from_buffer(const std::string &path,
                                       const uint8_t *jpeg_bytes,
                                       size_t jpeg_size,
                                       jpeg_decode_picture_info_t *picture_info)
{
    if ((picture_info == nullptr) || (jpeg_bytes == nullptr) || (jpeg_size == 0U)) {
        return false;
    }

    memset(picture_info, 0, sizeof(*picture_info));
    const esp_err_t result = jpeg_decoder_get_info(jpeg_bytes, static_cast<uint32_t>(jpeg_size), picture_info);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JPEG header %s: %s", path.c_str(), esp_err_to_name(result));
        return false;
    }

    if ((picture_info->width == 0U) || (picture_info->height == 0U)) {
        ESP_LOGE(TAG,
                 "JPEG header returned invalid dimensions for %s (%u x %u from %u bytes)",
                 path.c_str(),
                 static_cast<unsigned>(picture_info->width),
                 static_cast<unsigned>(picture_info->height),
                 static_cast<unsigned>(jpeg_size));
        return false;
    }

    return true;
}

static const char *tjpgd_result_to_string(JRESULT result)
{
    switch (result) {
        case JDR_OK:
            return "JDR_OK";
        case JDR_INTR:
            return "JDR_INTR";
        case JDR_INP:
            return "JDR_INP";
        case JDR_MEM1:
            return "JDR_MEM1";
        case JDR_MEM2:
            return "JDR_MEM2";
        case JDR_PAR:
            return "JDR_PAR";
        case JDR_FMT1:
            return "JDR_FMT1";
        case JDR_FMT2:
            return "JDR_FMT2";
        case JDR_FMT3:
            return "JDR_FMT3";
        default:
            return "JDR_UNKNOWN";
    }
}

static size_t tjpgd_input_callback(JDEC *decoder, uint8_t *buffer, size_t bytes_to_read)
{
    if (decoder == nullptr) {
        return 0;
    }

    TjpgdSession *session = static_cast<TjpgdSession *>(decoder->device);
    if ((session == nullptr) || (session->input == nullptr) || (session->input_offset > session->input_size)) {
        return 0;
    }

    const size_t bytes_left = session->input_size - session->input_offset;
    const size_t bytes_available = std::min(bytes_to_read, bytes_left);
    if ((buffer != nullptr) && (bytes_available > 0U)) {
        memcpy(buffer, session->input + session->input_offset, bytes_available);
    }
    session->input_offset += bytes_available;
    return bytes_available;
}

static int tjpgd_output_callback(JDEC *decoder, void *bitmap, JRECT *rectangle)
{
    if ((decoder == nullptr) || (bitmap == nullptr) || (rectangle == nullptr)) {
        return 0;
    }

    TjpgdSession *session = static_cast<TjpgdSession *>(decoder->device);
    if ((session == nullptr) || (session->decoded_rgb888 == nullptr)) {
        return 0;
    }

    const uint32_t rect_width = static_cast<uint32_t>(rectangle->right - rectangle->left + 1U);
    const uint32_t rect_height = static_cast<uint32_t>(rectangle->bottom - rectangle->top + 1U);
    const uint8_t *source = static_cast<const uint8_t *>(bitmap);
    if ((rect_width == 0U) || (rect_height == 0U) || (source == nullptr)) {
        return 0;
    }

    for (uint32_t row = 0; row < rect_height; ++row) {
        const uint32_t destination_y = static_cast<uint32_t>(rectangle->top) + row;
        if (destination_y >= session->decoded_height) {
            return 0;
        }

        uint8_t *destination_row = session->decoded_rgb888 +
                                   ((static_cast<size_t>(destination_y) * session->decoded_width + static_cast<uint32_t>(rectangle->left)) * 3U);
        memcpy(destination_row, source + (static_cast<size_t>(row) * rect_width * 3U), rect_width * 3U);
    }

    return 1;
}

static uint8_t choose_tjpgd_scale(uint16_t source_width,
                                  uint16_t source_height,
                                  uint32_t destination_width,
                                  uint32_t destination_height)
{
    uint8_t scale = 0;
    while (scale < 3U) {
        const uint32_t scaled_width = std::max<uint32_t>(1U, (static_cast<uint32_t>(source_width) + ((1U << scale) - 1U)) >> scale);
        const uint32_t scaled_height = std::max<uint32_t>(1U, (static_cast<uint32_t>(source_height) + ((1U << scale) - 1U)) >> scale);
        if ((scaled_width <= (destination_width * 2U)) && (scaled_height <= (destination_height * 2U))) {
            break;
        }
        ++scale;
    }
    return scale;
}

static void libjpeg_error_exit(j_common_ptr common_info)
{
    LibjpegErrorManager *error_manager = reinterpret_cast<LibjpegErrorManager *>(common_info->err);
    longjmp(error_manager->jump_buffer, 1);
}

static void render_rgb565_to_buffer(const uint16_t *source,
                                    uint32_t source_width,
                                    uint32_t source_height,
                                    uint32_t source_stride,
                                    lv_color_t *destination,
                                    uint32_t destination_width,
                                    uint32_t destination_height)
{
    if ((source == nullptr) || (destination == nullptr) || (source_width == 0U) || (source_height == 0U)) {
        return;
    }

    uint16_t *destination_pixels = reinterpret_cast<uint16_t *>(destination);
    memset(destination_pixels, 0, destination_width * destination_height * sizeof(uint16_t));

    const uint32_t width_limited_height = (source_height * destination_width) / source_width;
    uint32_t scaled_width = destination_width;
    uint32_t scaled_height = width_limited_height;
    if (scaled_height > destination_height) {
        scaled_height = destination_height;
        scaled_width = (source_width * destination_height) / source_height;
    }

    if (scaled_width == 0U) {
        scaled_width = 1U;
    }
    if (scaled_height == 0U) {
        scaled_height = 1U;
    }

    const int32_t offset_x = (static_cast<int32_t>(destination_width) - static_cast<int32_t>(scaled_width)) / 2;
    const int32_t offset_y = (static_cast<int32_t>(destination_height) - static_cast<int32_t>(scaled_height)) / 2;

    for (uint32_t y = 0; y < scaled_height; ++y) {
        const uint32_t source_y = (y * source_height) / scaled_height;
        uint16_t *destination_row = destination_pixels + ((offset_y + static_cast<int32_t>(y)) * destination_width);
        const uint16_t *source_row = source + (source_y * source_stride);
        for (uint32_t x = 0; x < scaled_width; ++x) {
            const uint32_t source_x = (x * source_width) / scaled_width;
            destination_row[offset_x + static_cast<int32_t>(x)] = source_row[source_x];
        }
    }
}

static void render_rgb888_to_buffer(const uint8_t *source,
                                    uint32_t source_width,
                                    uint32_t source_height,
                                    lv_color_t *destination,
                                    uint32_t destination_width,
                                    uint32_t destination_height)
{
    if ((source == nullptr) || (destination == nullptr) || (source_width == 0U) || (source_height == 0U)) {
        return;
    }

    uint16_t *destination_pixels = reinterpret_cast<uint16_t *>(destination);
    memset(destination_pixels, 0, destination_width * destination_height * sizeof(uint16_t));

    const uint32_t width_limited_height = (source_height * destination_width) / source_width;
    uint32_t scaled_width = destination_width;
    uint32_t scaled_height = width_limited_height;
    if (scaled_height > destination_height) {
        scaled_height = destination_height;
        scaled_width = (source_width * destination_height) / source_height;
    }

    if (scaled_width == 0U) {
        scaled_width = 1U;
    }
    if (scaled_height == 0U) {
        scaled_height = 1U;
    }

    const int32_t offset_x = (static_cast<int32_t>(destination_width) - static_cast<int32_t>(scaled_width)) / 2;
    const int32_t offset_y = (static_cast<int32_t>(destination_height) - static_cast<int32_t>(scaled_height)) / 2;

    for (uint32_t y = 0; y < scaled_height; ++y) {
        const uint32_t source_y = (y * source_height) / scaled_height;
        uint16_t *destination_row = destination_pixels + ((offset_y + static_cast<int32_t>(y)) * destination_width);
        for (uint32_t x = 0; x < scaled_width; ++x) {
            const uint32_t source_x = (x * source_width) / scaled_width;
            const uint8_t *source_pixel = source + ((static_cast<size_t>(source_y) * source_width + source_x) * 3U);
            destination_row[offset_x + static_cast<int32_t>(x)] = static_cast<uint16_t>(((source_pixel[0] & 0xF8U) << 8U) |
                                                                                          ((source_pixel[1] & 0xFCU) << 3U) |
                                                                                          (source_pixel[2] >> 3U));
        }
    }
}

static void render_cmyk_to_buffer(const uint8_t *source,
                                  uint32_t source_width,
                                  uint32_t source_height,
                                  lv_color_t *destination,
                                  uint32_t destination_width,
                                  uint32_t destination_height)
{
    if ((source == nullptr) || (destination == nullptr) || (source_width == 0U) || (source_height == 0U)) {
        return;
    }

    uint16_t *destination_pixels = reinterpret_cast<uint16_t *>(destination);
    memset(destination_pixels, 0, destination_width * destination_height * sizeof(uint16_t));

    const uint32_t width_limited_height = (source_height * destination_width) / source_width;
    uint32_t scaled_width = destination_width;
    uint32_t scaled_height = width_limited_height;
    if (scaled_height > destination_height) {
        scaled_height = destination_height;
        scaled_width = (source_width * destination_height) / source_height;
    }

    if (scaled_width == 0U) {
        scaled_width = 1U;
    }
    if (scaled_height == 0U) {
        scaled_height = 1U;
    }

    const int32_t offset_x = (static_cast<int32_t>(destination_width) - static_cast<int32_t>(scaled_width)) / 2;
    const int32_t offset_y = (static_cast<int32_t>(destination_height) - static_cast<int32_t>(scaled_height)) / 2;

    for (uint32_t y = 0; y < scaled_height; ++y) {
        const uint32_t source_y = (y * source_height) / scaled_height;
        uint16_t *destination_row = destination_pixels + ((offset_y + static_cast<int32_t>(y)) * destination_width);
        for (uint32_t x = 0; x < scaled_width; ++x) {
            const uint32_t source_x = (x * source_width) / scaled_width;
            const uint8_t *source_pixel = source + ((static_cast<size_t>(source_y) * source_width + source_x) * 4U);
            const uint8_t red = static_cast<uint8_t>((static_cast<uint16_t>(source_pixel[0]) * source_pixel[3] + 127U) / 255U);
            const uint8_t green = static_cast<uint8_t>((static_cast<uint16_t>(source_pixel[1]) * source_pixel[3] + 127U) / 255U);
            const uint8_t blue = static_cast<uint8_t>((static_cast<uint16_t>(source_pixel[2]) * source_pixel[3] + 127U) / 255U);
            destination_row[offset_x + static_cast<int32_t>(x)] = static_cast<uint16_t>(((red & 0xF8U) << 8U) |
                                                                                          ((green & 0xFCU) << 3U) |
                                                                                          (blue >> 3U));
        }
    }
}

static void render_rgb565a8_to_buffer(const uint8_t *source,
                                      uint32_t source_width,
                                      uint32_t source_height,
                                      lv_color_t *destination,
                                      uint32_t destination_width,
                                      uint32_t destination_height)
{
    if ((source == nullptr) || (destination == nullptr) || (source_width == 0U) || (source_height == 0U)) {
        return;
    }

    uint16_t *destination_pixels = reinterpret_cast<uint16_t *>(destination);
    memset(destination_pixels, 0, destination_width * destination_height * sizeof(uint16_t));

    const uint32_t width_limited_height = (source_height * destination_width) / source_width;
    uint32_t scaled_width = destination_width;
    uint32_t scaled_height = width_limited_height;
    if (scaled_height > destination_height) {
        scaled_height = destination_height;
        scaled_width = (source_width * destination_height) / source_height;
    }

    if (scaled_width == 0U) {
        scaled_width = 1U;
    }
    if (scaled_height == 0U) {
        scaled_height = 1U;
    }

    const int32_t offset_x = (static_cast<int32_t>(destination_width) - static_cast<int32_t>(scaled_width)) / 2;
    const int32_t offset_y = (static_cast<int32_t>(destination_height) - static_cast<int32_t>(scaled_height)) / 2;

    for (uint32_t y = 0; y < scaled_height; ++y) {
        const uint32_t source_y = (y * source_height) / scaled_height;
        uint16_t *destination_row = destination_pixels + ((offset_y + static_cast<int32_t>(y)) * destination_width);
        for (uint32_t x = 0; x < scaled_width; ++x) {
            const uint32_t source_x = (x * source_width) / scaled_width;
            const uint8_t *source_pixel = source + ((source_y * source_width + source_x) * 3U);
            const uint16_t rgb565 = static_cast<uint16_t>(source_pixel[0]) | (static_cast<uint16_t>(source_pixel[1]) << 8U);
            const uint8_t alpha = source_pixel[2];
            if (alpha == 0U) {
                destination_row[offset_x + static_cast<int32_t>(x)] = 0;
                continue;
            }

            if (alpha == 255U) {
                destination_row[offset_x + static_cast<int32_t>(x)] = rgb565;
                continue;
            }

            uint8_t red = static_cast<uint8_t>(((rgb565 >> 11U) & 0x1FU) * 255U / 31U);
            uint8_t green = static_cast<uint8_t>(((rgb565 >> 5U) & 0x3FU) * 255U / 63U);
            uint8_t blue = static_cast<uint8_t>((rgb565 & 0x1FU) * 255U / 31U);
            red = static_cast<uint8_t>((red * alpha) / 255U);
            green = static_cast<uint8_t>((green * alpha) / 255U);
            blue = static_cast<uint8_t>((blue * alpha) / 255U);
            destination_row[offset_x + static_cast<int32_t>(x)] = static_cast<uint16_t>(((red & 0xF8U) << 8U) |
                                                                                          ((green & 0xFCU) << 3U) |
                                                                                          (blue >> 3U));
        }
    }
}

static bool read_file_to_psram_buffer(const std::string &path, uint8_t **buffer, size_t *buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == nullptr)) {
        return false;
    }

    *buffer = nullptr;
    *buffer_size = 0;

    FILE *file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        ESP_LOGE(TAG, "Failed to open image file %s", path.c_str());
        return false;
    }

    fseek(file, 0, SEEK_END);
    const long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size <= 0) {
        fclose(file);
        ESP_LOGE(TAG, "Image file has invalid size %s", path.c_str());
        return false;
    }

    uint8_t *file_buffer = static_cast<uint8_t *>(heap_caps_malloc(static_cast<size_t>(file_size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (file_buffer == nullptr) {
        fclose(file);
        ESP_LOGE(TAG, "Failed to allocate PSRAM for image file %s", path.c_str());
        return false;
    }

    const size_t bytes_read = fread(file_buffer, 1, static_cast<size_t>(file_size), file);
    fclose(file);
    if (bytes_read != static_cast<size_t>(file_size)) {
        free(file_buffer);
        ESP_LOGE(TAG, "Failed to read image file %s", path.c_str());
        return false;
    }

    *buffer = file_buffer;
    *buffer_size = bytes_read;
    return true;
}

static bool decode_jpeg_to_buffer(const std::string &path,
                                  lv_color_t *destination,
                                  uint32_t destination_width,
                                  uint32_t destination_height)
{
    uint8_t *input_buffer = nullptr;
    size_t bytes_read = 0;
    if (!read_file_to_psram_buffer(path, &input_buffer, &bytes_read)) {
        return false;
    }

    jpeg_decode_picture_info_t image_info = {};
    if (!read_jpeg_info_from_buffer(path, input_buffer, bytes_read, &image_info)) {
        ESP_LOGW(TAG, "Hardware JPEG header parse rejected %s, trying TJpgDec fallback", path.c_str());
        const bool fallback_result = decode_jpeg_with_tjpgd(path, input_buffer, bytes_read, destination, destination_width, destination_height);
        free(input_buffer);
        return fallback_result;
    }

    esp_err_t result = ESP_OK;

    const uint32_t padded_width = align_up_to_16(image_info.width);
    const uint32_t padded_height = align_up_to_16(image_info.height);
    const size_t decoded_bytes = static_cast<size_t>(padded_width) * static_cast<size_t>(padded_height) * sizeof(uint16_t);
    size_t decoder_output_capacity = decoded_bytes;
    uint8_t *decoded_buffer = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, decoded_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (decoded_buffer == nullptr) {
        decoded_buffer = static_cast<uint8_t *>(heap_caps_malloc(decoded_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (decoded_buffer == nullptr) {
        ESP_LOGE(TAG,
                 "Failed to allocate PSRAM JPEG decode buffer for %s (%u x %u, %u bytes)",
                 path.c_str(),
                 static_cast<unsigned>(image_info.width),
                 static_cast<unsigned>(image_info.height),
                 static_cast<unsigned>(decoded_bytes));
        const bool fallback_result = decode_jpeg_with_tjpgd(path, input_buffer, bytes_read, destination, destination_width, destination_height);
        free(input_buffer);
        return fallback_result;
    }

    jpeg_decode_engine_cfg_t engine_cfg = {
        .timeout_ms = 120,
    };
    jpeg_decoder_handle_t decoder_handle = nullptr;
    result = jpeg_new_decoder_engine(&engine_cfg, &decoder_handle);
    if (result != ESP_OK) {
        free(input_buffer);
        free(decoded_buffer);
        ESP_LOGE(TAG, "Failed to create JPEG decoder for %s: %s", path.c_str(), esp_err_to_name(result));
        return false;
    }

    uint32_t output_size = 0;
    jpeg_decode_cfg_t decode_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
    };
    result = jpeg_decoder_process(decoder_handle,
                                  &decode_cfg,
                                  input_buffer,
                                  static_cast<uint32_t>(bytes_read),
                                  decoded_buffer,
                                  static_cast<uint32_t>(decoder_output_capacity),
                                  &output_size);
    if (result != ESP_OK) {
        jpeg_del_decoder_engine(decoder_handle);
        free(decoded_buffer);
        ESP_LOGW(TAG, "Hardware JPEG decode failed for %s: %s, trying TJpgDec fallback", path.c_str(), esp_err_to_name(result));
        const bool fallback_result = decode_jpeg_with_tjpgd(path, input_buffer, bytes_read, destination, destination_width, destination_height);
        free(input_buffer);
        return fallback_result;
    }
    free(input_buffer);

    uint32_t stride_pixels = image_info.width;
    if ((image_info.height != 0U) && (output_size >= (image_info.height * sizeof(uint16_t)))) {
        const uint32_t computed_stride = (output_size / sizeof(uint16_t)) / image_info.height;
        if (computed_stride >= image_info.width) {
            stride_pixels = computed_stride;
        }
    }
    render_rgb565_to_buffer(reinterpret_cast<uint16_t *>(decoded_buffer),
                            image_info.width,
                            image_info.height,
                            stride_pixels,
                            destination,
                            destination_width,
                            destination_height);
    jpeg_del_decoder_engine(decoder_handle);
    free(decoded_buffer);
    return true;
}

static bool decode_jpeg_with_tjpgd(const std::string &path,
                                   const uint8_t *input_buffer,
                                   size_t input_size,
                                   lv_color_t *destination,
                                   uint32_t destination_width,
                                   uint32_t destination_height)
{
    if ((input_buffer == nullptr) || (input_size == 0U) || (destination == nullptr)) {
        return false;
    }

    uint8_t *workspace = static_cast<uint8_t *>(heap_caps_malloc(kTjpgdWorkspaceBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (workspace == nullptr) {
        workspace = static_cast<uint8_t *>(heap_caps_malloc(kTjpgdWorkspaceBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (workspace == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate TJpgDec workspace for %s", path.c_str());
        return false;
    }

    TjpgdSession session = {};
    session.input = input_buffer;
    session.input_size = input_size;

    JDEC decoder = {};
    JRESULT result = jd_prepare(&decoder, tjpgd_input_callback, workspace, kTjpgdWorkspaceBytes, &session);
    if (result != JDR_OK) {
        if (result == JDR_FMT3) {
            ESP_LOGW(TAG, "TJpgDec rejected unsupported JPEG standard for %s, trying libjpeg-turbo", path.c_str());
            free(workspace);
            return decode_jpeg_with_libjpeg_turbo(path, input_buffer, input_size, destination, destination_width, destination_height);
        }
        ESP_LOGE(TAG, "TJpgDec prepare failed for %s: %s", path.c_str(), tjpgd_result_to_string(result));
        free(workspace);
        return false;
    }

    const uint8_t scale = choose_tjpgd_scale(decoder.width, decoder.height, destination_width, destination_height);
    session.decoded_width = std::max<uint32_t>(1U, (static_cast<uint32_t>(decoder.width) + ((1U << scale) - 1U)) >> scale);
    session.decoded_height = std::max<uint32_t>(1U, (static_cast<uint32_t>(decoder.height) + ((1U << scale) - 1U)) >> scale);
    const size_t decoded_rgb888_bytes = static_cast<size_t>(session.decoded_width) * session.decoded_height * 3U;
    session.decoded_rgb888 = static_cast<uint8_t *>(heap_caps_malloc(decoded_rgb888_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (session.decoded_rgb888 == nullptr) {
        ESP_LOGE(TAG,
                 "Failed to allocate TJpgDec PSRAM buffer for %s (%u x %u, %u bytes)",
                 path.c_str(),
                 static_cast<unsigned>(session.decoded_width),
                 static_cast<unsigned>(session.decoded_height),
                 static_cast<unsigned>(decoded_rgb888_bytes));
        free(workspace);
        return false;
    }
    memset(session.decoded_rgb888, 0, decoded_rgb888_bytes);

    session.input_offset = 0;
    result = jd_decomp(&decoder, tjpgd_output_callback, scale);
    if (result != JDR_OK) {
        ESP_LOGE(TAG, "TJpgDec decode failed for %s: %s", path.c_str(), tjpgd_result_to_string(result));
        free(session.decoded_rgb888);
        free(workspace);
        return false;
    }

    render_rgb888_to_buffer(session.decoded_rgb888,
                            session.decoded_width,
                            session.decoded_height,
                            destination,
                            destination_width,
                            destination_height);
    ESP_LOGI(TAG,
             "TJpgDec fallback decoded %s at scale=%u source=%ux%u decoded=%ux%u",
             path.c_str(),
             static_cast<unsigned>(scale),
             static_cast<unsigned>(decoder.width),
             static_cast<unsigned>(decoder.height),
             static_cast<unsigned>(session.decoded_width),
             static_cast<unsigned>(session.decoded_height));
    free(session.decoded_rgb888);
    free(workspace);
    return true;
}

static bool decode_jpeg_with_libjpeg_turbo(const std::string &path,
                                           const uint8_t *input_buffer,
                                           size_t input_size,
                                           lv_color_t *destination,
                                           uint32_t destination_width,
                                           uint32_t destination_height)
{
    if ((input_buffer == nullptr) || (input_size == 0U) || (destination == nullptr)) {
        return false;
    }

    jpeg_decompress_struct decoder = {};
    LibjpegErrorManager error_manager = {};
    decoder.err = jpeg_std_error(&error_manager.base);
    error_manager.base.error_exit = libjpeg_error_exit;

    if (setjmp(error_manager.jump_buffer) != 0) {
        jpeg_destroy_decompress(&decoder);
        ESP_LOGE(TAG, "libjpeg-turbo decode failed for %s", path.c_str());
        return false;
    }

    jpeg_create_decompress(&decoder);
    jpeg_mem_src(&decoder, const_cast<unsigned char *>(input_buffer), static_cast<unsigned long>(input_size));
    const int header_result = jpeg_read_header(&decoder, TRUE);
    if (header_result != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&decoder);
        ESP_LOGE(TAG, "libjpeg-turbo header parse failed for %s", path.c_str());
        return false;
    }

    const uint8_t scale = choose_tjpgd_scale(static_cast<uint16_t>(decoder.image_width),
                                             static_cast<uint16_t>(decoder.image_height),
                                             destination_width,
                                             destination_height);
    decoder.scale_num = 1;
    decoder.scale_denom = 1U << scale;
    const bool cmyk_source = (decoder.jpeg_color_space == JCS_CMYK) || (decoder.jpeg_color_space == JCS_YCCK);
    decoder.out_color_space = cmyk_source ? JCS_CMYK : JCS_RGB;

    if (jpeg_start_decompress(&decoder) != TRUE) {
        jpeg_destroy_decompress(&decoder);
        ESP_LOGE(TAG,
                 "libjpeg-turbo start failed for %s (jpeg_color_space=%d out_color_space=%d)",
                 path.c_str(),
                 static_cast<int>(decoder.jpeg_color_space),
                 static_cast<int>(decoder.out_color_space));
        return false;
    }

    const size_t row_stride = static_cast<size_t>(decoder.output_width) * decoder.output_components;
    const size_t decoded_bytes = row_stride * decoder.output_height;
    uint8_t *decoded_rgb888 = static_cast<uint8_t *>(heap_caps_malloc(decoded_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (decoded_rgb888 == nullptr) {
        jpeg_finish_decompress(&decoder);
        jpeg_destroy_decompress(&decoder);
        ESP_LOGE(TAG,
                 "Failed to allocate libjpeg-turbo PSRAM buffer for %s (%u x %u, %u bytes)",
                 path.c_str(),
                 static_cast<unsigned>(decoder.output_width),
                 static_cast<unsigned>(decoder.output_height),
                 static_cast<unsigned>(decoded_bytes));
        return false;
    }

    while (decoder.output_scanline < decoder.output_height) {
        JSAMPROW row_pointer = decoded_rgb888 + (static_cast<size_t>(decoder.output_scanline) * row_stride);
        if (jpeg_read_scanlines(&decoder, &row_pointer, 1) != 1U) {
            free(decoded_rgb888);
            jpeg_finish_decompress(&decoder);
            jpeg_destroy_decompress(&decoder);
            ESP_LOGE(TAG, "libjpeg-turbo scanline decode failed for %s", path.c_str());
            return false;
        }
    }

    jpeg_finish_decompress(&decoder);
    jpeg_destroy_decompress(&decoder);
    if (cmyk_source) {
        render_cmyk_to_buffer(decoded_rgb888,
                              decoder.output_width,
                              decoder.output_height,
                              destination,
                              destination_width,
                              destination_height);
    } else {
        render_rgb888_to_buffer(decoded_rgb888,
                                decoder.output_width,
                                decoder.output_height,
                                destination,
                                destination_width,
                                destination_height);
    }
    ESP_LOGI(TAG,
             "libjpeg-turbo decoded %s at scale=%u source=%ux%u decoded=%ux%u components=%u color_space=%d",
             path.c_str(),
             static_cast<unsigned>(scale),
             static_cast<unsigned>(decoder.image_width),
             static_cast<unsigned>(decoder.image_height),
             static_cast<unsigned>(decoder.output_width),
             static_cast<unsigned>(decoder.output_height),
             static_cast<unsigned>(decoder.output_components),
             static_cast<int>(decoder.jpeg_color_space));
    free(decoded_rgb888);
    return true;
}

static bool decode_png_to_buffer(const std::string &path,
                                 lv_color_t *destination,
                                 uint32_t destination_width,
                                 uint32_t destination_height)
{
    uint8_t *png_bytes = nullptr;
    size_t png_size = 0;
    if (!read_file_to_psram_buffer(path, &png_bytes, &png_size)) {
        return false;
    }

    lv_img_dsc_t raw_png = {};
    raw_png.header.cf = LV_IMG_CF_RAW_ALPHA;
    raw_png.data_size = static_cast<uint32_t>(png_size);
    raw_png.data = png_bytes;

    lv_img_decoder_dsc_t decoder_dsc = {};
    const lv_res_t result = lv_img_decoder_open(&decoder_dsc, &raw_png, lv_color_white(), 0);
    if ((result != LV_RES_OK) || (decoder_dsc.img_data == nullptr)) {
        free(png_bytes);
        ESP_LOGE(TAG, "PNG decode failed through LVGL for %s", path.c_str());
        return false;
    }

    render_rgb565a8_to_buffer(decoder_dsc.img_data,
                              decoder_dsc.header.w,
                              decoder_dsc.header.h,
                              destination,
                              destination_width,
                              destination_height);
    lv_img_decoder_close(&decoder_dsc);
    free(png_bytes);
    return true;
}

static bool decode_image_to_buffer(const std::string &path,
                                   lv_color_t *destination,
                                   uint32_t destination_width,
                                   uint32_t destination_height)
{
    if (destination == nullptr) {
        return false;
    }

    if (is_png_path(path)) {
        return decode_png_to_buffer(path, destination, destination_width, destination_height);
    }
    return decode_jpeg_to_buffer(path, destination, destination_width, destination_height);
}

static void draw_preview_placeholder(lv_obj_t *canvas, lv_color_t *preview_buffer, const std::string &path)
{
    if ((canvas == nullptr) || (preview_buffer == nullptr)) {
        return;
    }

    memset(preview_buffer, 0, static_cast<size_t>(kTilePreviewWidth) * static_cast<size_t>(kTilePreviewHeight) * sizeof(lv_color_t));
    lv_draw_rect_dsc_t rect = {};
    lv_draw_rect_dsc_init(&rect);
    rect.bg_color = tile_color_for_path(path);
    rect.bg_grad.dir = LV_GRAD_DIR_VER;
    rect.bg_grad.stops[0].color = tile_color_for_path(path);
    rect.bg_grad.stops[1].color = lv_color_hex(0x05070A);
    lv_canvas_draw_rect(canvas, 0, 0, kTilePreviewWidth, kTilePreviewHeight, &rect);
    lv_obj_invalidate(canvas);
}

static void anim_set_x(void *object, int32_t value)
{
    lv_obj_set_x(static_cast<lv_obj_t *>(object), value);
}

static void anim_set_y(void *object, int32_t value)
{
    lv_obj_set_y(static_cast<lv_obj_t *>(object), value);
}

static void anim_set_opa(void *object, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), static_cast<lv_opa_t>(value), 0);
}

static void anim_set_zoom(void *object, int32_t value)
{
    lv_obj_set_style_transform_zoom(static_cast<lv_obj_t *>(object), static_cast<lv_coord_t>(value), 0);
}

static void anim_set_angle(void *object, int32_t value)
{
    lv_obj_set_style_transform_angle(static_cast<lv_obj_t *>(object), static_cast<lv_coord_t>(value), 0);
}

static void animate_value(void *target, lv_anim_exec_xcb_t exec_cb, int32_t start, int32_t end)
{
    if (start == end) {
        exec_cb(target, end);
        return;
    }

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, target);
    lv_anim_set_exec_cb(&animation, exec_cb);
    lv_anim_set_values(&animation, start, end);
    lv_anim_set_time(&animation, kTransitionDurationMs);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
}

static void reset_canvas_transform(lv_obj_t *canvas)
{
    if (canvas == nullptr) {
        return;
    }

    lv_anim_del(canvas, nullptr);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_set_style_opa(canvas, LV_OPA_TRANSP, 0);
    lv_obj_set_style_transform_zoom(canvas, 256, 0);
    lv_obj_set_style_transform_angle(canvas, 0, 0);
    lv_obj_set_style_transform_pivot_x(canvas, kScreenWidth / 2, 0);
    lv_obj_set_style_transform_pivot_y(canvas, kScreenHeight / 2, 0);
}

static int choose_transition_index()
{
    if (kTransitionCount <= 1) {
        return 0;
    }

    int transition_index = static_cast<int>(esp_random() % kTransitionCount);
    if (transition_index == s_last_transition) {
        transition_index = (transition_index + 1) % kTransitionCount;
    }
    s_last_transition = transition_index;
    return transition_index;
}

static void apply_transition(int transition_index, lv_obj_t *outgoing, lv_obj_t *incoming)
{
    const TransitionRecipe &recipe = kTransitionRecipes[transition_index % kTransitionCount];

    reset_canvas_transform(outgoing);
    reset_canvas_transform(incoming);
    lv_obj_clear_flag(outgoing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(incoming, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(incoming);

    lv_obj_set_pos(incoming, recipe.incomingStartX, recipe.incomingStartY);
    lv_obj_set_style_opa(incoming, static_cast<lv_opa_t>(recipe.incomingStartOpa), 0);
    lv_obj_set_style_transform_zoom(incoming, static_cast<lv_coord_t>(recipe.incomingStartZoom), 0);
    lv_obj_set_style_transform_angle(incoming, static_cast<lv_coord_t>(recipe.incomingStartAngle), 0);

    animate_value(incoming, anim_set_x, recipe.incomingStartX, 0);
    animate_value(incoming, anim_set_y, recipe.incomingStartY, 0);
    animate_value(incoming, anim_set_opa, recipe.incomingStartOpa, LV_OPA_COVER);
    animate_value(incoming, anim_set_zoom, recipe.incomingStartZoom, 256);
    animate_value(incoming, anim_set_angle, recipe.incomingStartAngle, 0);

    animate_value(outgoing, anim_set_x, 0, recipe.outgoingEndX);
    animate_value(outgoing, anim_set_y, 0, recipe.outgoingEndY);
    animate_value(outgoing, anim_set_opa, LV_OPA_COVER, recipe.outgoingEndOpa);
    animate_value(outgoing, anim_set_zoom, 256, recipe.outgoingEndZoom);
    animate_value(outgoing, anim_set_angle, 0, recipe.outgoingEndAngle);
}

static bool ensure_viewer_buffers()
{
    const size_t buffer_bytes = static_cast<size_t>(kScreenWidth) * static_cast<size_t>(kScreenHeight) * sizeof(lv_color_t);
    for (int index = 0; index < 2; ++index) {
        if (s_viewer_buffers[index] != nullptr) {
            continue;
        }

        s_viewer_buffers[index] = static_cast<lv_color_t *>(heap_caps_malloc(buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (s_viewer_buffers[index] == nullptr) {
            s_viewer_buffers[index] = static_cast<lv_color_t *>(heap_caps_malloc(buffer_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            ESP_LOGW(TAG, "Falling back to internal RAM for viewer buffer %d", index);
        }
        if (s_viewer_buffers[index] == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate viewer buffer %d", index);
            return false;
        }
        memset(s_viewer_buffers[index], 0, buffer_bytes);
    }
    return true;
}

static void update_viewer_title(const std::string &path, int transition_index)
{
    if (s_viewer_title != nullptr) {
        lv_label_set_text(s_viewer_title, base_name(path));
    }
    if (s_transition_badge != nullptr) {
        lv_label_set_text_fmt(s_transition_badge, "%s", kTransitionNames[transition_index % kTransitionCount]);
    }
}

static bool show_image_index(AppImageDisplay *app, int index, bool animate)
{
    if ((app == nullptr) || (index < 0) || (index >= static_cast<int>(app->imagePathCount()))) {
        ESP_LOGW(TAG, "show_image_index rejected app=%p index=%d count=%u",
                 app,
                 index,
                 app == nullptr ? 0U : static_cast<unsigned>(app->imagePathCount()));
        return false;
    }
    if (!ensure_viewer_buffers()) {
        ESP_LOGE(TAG, "show_image_index failed to allocate viewer buffers for index=%d", index);
        return false;
    }

    const int target_canvas = animate ? (1 - s_visible_canvas) : s_visible_canvas;
    const std::string &path = app->imagePathAt(static_cast<size_t>(index));
    if (!decode_image_to_buffer(path, s_viewer_buffers[target_canvas], kScreenWidth, kScreenHeight)) {
        ESP_LOGE(TAG, "show_image_index failed to decode image index=%d path=%s", index, path.c_str());
        return false;
    }

    ESP_LOGI(TAG,
             "show_image_index opening index=%d animate=%s target_canvas=%d current_index=%d path=%s",
             index,
             animate ? "yes" : "no",
             target_canvas,
             s_current_index,
             path.c_str());

    bsp_display_lock(0);
    lv_canvas_set_buffer(s_viewer_canvas[target_canvas], s_viewer_buffers[target_canvas], kScreenWidth, kScreenHeight, LV_IMG_CF_TRUE_COLOR);
    lv_obj_clear_flag(s_viewer_layer, LV_OBJ_FLAG_HIDDEN);
    if (s_gallery_header != nullptr) {
        lv_obj_add_flag(s_gallery_header, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_gallery_grid != nullptr) {
        lv_obj_add_flag(s_gallery_grid, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_move_foreground(s_viewer_layer);
    s_fullscreen_visible = true;

    int transition_index = 0;
    if ((s_current_index >= 0) && animate) {
        transition_index = choose_transition_index();
        apply_transition(transition_index, s_viewer_canvas[s_visible_canvas], s_viewer_canvas[target_canvas]);
        s_visible_canvas = target_canvas;
    } else {
        transition_index = (s_last_transition >= 0) ? s_last_transition : 0;
        reset_canvas_transform(s_viewer_canvas[target_canvas]);
        lv_obj_clear_flag(s_viewer_canvas[target_canvas], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(s_viewer_canvas[target_canvas], LV_OPA_COVER, 0);
        lv_obj_move_foreground(s_viewer_canvas[target_canvas]);
        lv_obj_add_flag(s_viewer_canvas[1 - target_canvas], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(s_viewer_canvas[1 - target_canvas], LV_OPA_TRANSP, 0);
        s_visible_canvas = target_canvas;
    }

    if (s_viewer_topbar != nullptr) {
        lv_obj_clear_flag(s_viewer_topbar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_viewer_topbar);
    }
    if (s_viewer_hint != nullptr) {
        lv_obj_clear_flag(s_viewer_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_viewer_hint);
    }

    update_viewer_title(path, transition_index);
    lv_obj_invalidate(s_viewer_layer);
    lv_obj_invalidate(s_viewer_canvas[target_canvas]);
    bsp_display_unlock();

    s_current_index = index;
    note_viewer_interaction();
    return true;
}

static void show_image_index_async(void *context)
{
    std::unique_ptr<PendingViewerRequest> request(static_cast<PendingViewerRequest *>(context));
    if ((request == nullptr) || (request->app == nullptr) || (image_muxe == nullptr)) {
        ESP_LOGW(TAG, "show_image_index_async ignored request=%p app=%p mutex=%p",
                 request.get(),
                 request == nullptr ? nullptr : request->app,
                 image_muxe);
        return;
    }

    ESP_LOGI(TAG,
             "show_image_index_async reason=%s index=%d animate=%s",
             request->reason == nullptr ? "unknown" : request->reason,
             request->index,
             request->animate ? "yes" : "no");
    if (xSemaphoreTakeRecursive(image_muxe, pdMS_TO_TICKS(50)) == pdTRUE) {
        const bool opened = show_image_index(request->app, request->index, request->animate);
        ESP_LOGI(TAG, "show_image_index_async result=%s index=%d", opened ? "ok" : "failed", request->index);
        xSemaphoreGiveRecursive(image_muxe);
    } else {
        ESP_LOGW(TAG, "show_image_index_async mutex timeout index=%d", request->index);
    }
}

static void close_viewer()
{
    stop_idle_slideshow();
    ESP_LOGI(TAG, "close_viewer current_index=%d", s_current_index);
    s_fullscreen_visible = false;
    if (s_gallery_header != nullptr) {
        lv_obj_clear_flag(s_gallery_header, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_gallery_grid != nullptr) {
        lv_obj_clear_flag(s_gallery_grid, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_viewer_layer != nullptr) {
        lv_obj_add_flag(s_viewer_layer, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_gallery_rebuild_pending && (s_active_app != nullptr)) {
        ESP_LOGI(TAG, "close_viewer applying deferred gallery rebuild count=%u", static_cast<unsigned>(s_active_app->imagePathCount()));
        build_gallery_ui(s_active_app);
        s_gallery_rebuild_pending = false;
    }
}

static void viewer_interaction_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    ESP_LOGI(TAG, "viewer_interaction_cb code=%d current_index=%d", static_cast<int>(code), s_current_index);
    if ((code == LV_EVENT_PRESSED) || (code == LV_EVENT_CLICKED)) {
        note_viewer_interaction();
    }
}

static void viewer_close_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        close_viewer();
    }
}

static void tile_open_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if ((code != LV_EVENT_PRESSED) && (code != LV_EVENT_PRESSING) && (code != LV_EVENT_RELEASED) && (code != LV_EVENT_PRESS_LOST)) {
        return;
    }

    const int index = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    lv_obj_t *target = lv_event_get_target(event);
    lv_indev_t *indev = lv_event_get_indev(event);

    if (code == LV_EVENT_PRESSED) {
        s_tile_press_index = index;
        s_tile_press_moved = false;
        if (indev != nullptr) {
            lv_indev_get_point(indev, &s_tile_press_point);
        } else {
            s_tile_press_point.x = 0;
            s_tile_press_point.y = 0;
        }
        ESP_LOGI(TAG, "tile_open_cb pressed index=%d target=%p point=(%d,%d)", index, target, s_tile_press_point.x, s_tile_press_point.y);
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        if ((index == s_tile_press_index) && (indev != nullptr) && !s_tile_press_moved) {
            lv_point_t point;
            lv_indev_get_point(indev, &point);
            const int dx = point.x - s_tile_press_point.x;
            const int dy = point.y - s_tile_press_point.y;
            if ((dx > kTileTapSlopPx) || (dx < -kTileTapSlopPx) || (dy > kTileTapSlopPx) || (dy < -kTileTapSlopPx)) {
                s_tile_press_moved = true;
                ESP_LOGI(TAG, "tile_open_cb move cancel index=%d target=%p point=(%d,%d) start=(%d,%d)",
                         index,
                         target,
                         point.x,
                         point.y,
                         s_tile_press_point.x,
                         s_tile_press_point.y);
            }
        }
        return;
    }

    if (code == LV_EVENT_PRESS_LOST) {
        ESP_LOGI(TAG, "tile_open_cb press lost index=%d target=%p moved=%s", index, target, s_tile_press_moved ? "yes" : "no");
        if (index == s_tile_press_index) {
            reset_tile_press_state();
        }
        return;
    }

    if (index != s_tile_press_index) {
        ESP_LOGI(TAG, "tile_open_cb release ignored index=%d target=%p tracked_index=%d",
                 index,
                 target,
                 s_tile_press_index);
        return;
    }

    if ((s_active_app == nullptr) || (image_muxe == nullptr)) {
        ESP_LOGW(TAG, "tile_open_cb ignored active_app=%p image_muxe=%p", s_active_app, image_muxe);
        reset_tile_press_state();
        return;
    }

    bool is_tap = !s_tile_press_moved;
    if ((indev != nullptr) && is_tap) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        const int dx = point.x - s_tile_press_point.x;
        const int dy = point.y - s_tile_press_point.y;
        is_tap = (dx <= kTileTapSlopPx) && (dx >= -kTileTapSlopPx) && (dy <= kTileTapSlopPx) && (dy >= -kTileTapSlopPx);
        if (!is_tap) {
            ESP_LOGI(TAG, "tile_open_cb release cancel index=%d target=%p point=(%d,%d) start=(%d,%d)",
                     index,
                     target,
                     point.x,
                     point.y,
                     s_tile_press_point.x,
                     s_tile_press_point.y);
        }
    }

    ESP_LOGI(TAG, "tile_open_cb code=%d index=%d current_index=%d count=%u",
             static_cast<int>(code),
             index,
             s_current_index,
             static_cast<unsigned>(s_active_app->imagePathCount()));

    reset_tile_press_state();
    if (!is_tap) {
        return;
    }

    if (!queue_viewer_request(s_active_app, index, s_current_index >= 0, "tile-open")) {
        ESP_LOGW(TAG, "tile_open_cb async queue failed index=%d", index);
    }
}

static void register_tile_open_target(lv_obj_t *object, size_t index)
{
    if (object == nullptr) {
        return;
    }

    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    void *user_data = reinterpret_cast<void *>(static_cast<uintptr_t>(index));
    lv_obj_add_event_cb(object, tile_open_cb, LV_EVENT_PRESSED, user_data);
    lv_obj_add_event_cb(object, tile_open_cb, LV_EVENT_PRESSING, user_data);
    lv_obj_add_event_cb(object, tile_open_cb, LV_EVENT_RELEASED, user_data);
    lv_obj_add_event_cb(object, tile_open_cb, LV_EVENT_PRESS_LOST, user_data);
}

static void create_gallery_tile(AppImageDisplay *app, size_t index)
{
    lv_obj_t *tile = lv_btn_create(s_gallery_grid);
    lv_obj_set_size(tile, kTileWidth, kTileHeight);
    lv_obj_set_style_radius(tile, 22, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x151A22), 0);
    lv_obj_set_style_bg_grad_color(tile, lv_color_hex(0x0B0F15), 0);
    lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(0x2C3543), 0);
    lv_obj_set_style_shadow_width(tile, 14, 0);
    lv_obj_set_style_shadow_color(tile, lv_color_hex(0x05070A), 0);
    lv_obj_set_style_pad_all(tile, 12, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    register_tile_open_target(tile, index);

    const std::string &path = app->imagePathAt(index);
    lv_obj_t *preview = lv_obj_create(tile);
    lv_obj_set_size(preview, kTilePreviewWidth, kTilePreviewHeight);
    lv_obj_align(preview, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(preview, 16, 0);
    lv_obj_set_style_border_width(preview, 0, 0);
    lv_obj_set_style_bg_color(preview, tile_color_for_path(path), 0);
    lv_obj_set_style_bg_grad_color(preview, lv_color_hex(0x05070A), 0);
    lv_obj_set_style_bg_grad_dir(preview, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_pad_all(preview, 0, 0);
    lv_obj_set_style_clip_corner(preview, true, 0);
    lv_obj_clear_flag(preview, LV_OBJ_FLAG_SCROLLABLE);
    register_tile_open_target(preview, index);

    lv_color_t *preview_buffer = static_cast<lv_color_t *>(heap_caps_malloc(static_cast<size_t>(kTilePreviewWidth) * static_cast<size_t>(kTilePreviewHeight) * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (preview_buffer == nullptr) {
        preview_buffer = static_cast<lv_color_t *>(heap_caps_malloc(static_cast<size_t>(kTilePreviewWidth) * static_cast<size_t>(kTilePreviewHeight) * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }

    if (preview_buffer != nullptr) {
        s_tile_preview_buffers.push_back(preview_buffer);
        lv_obj_t *preview_canvas = lv_canvas_create(preview);
        lv_obj_set_size(preview_canvas, kTilePreviewWidth, kTilePreviewHeight);
        lv_obj_center(preview_canvas);
        register_tile_open_target(preview_canvas, index);
        lv_canvas_set_buffer(preview_canvas, preview_buffer, kTilePreviewWidth, kTilePreviewHeight, LV_IMG_CF_TRUE_COLOR);
        draw_preview_placeholder(preview_canvas, preview_buffer, path);

        lv_obj_t *status = lv_label_create(preview);
        lv_label_set_text(status, "Loading...");
        lv_obj_set_style_text_font(status, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(status, lv_color_white(), 0);
        lv_obj_set_style_bg_color(status, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(status, LV_OPA_50, 0);
        lv_obj_set_style_radius(status, 10, 0);
        lv_obj_set_style_pad_left(status, 8, 0);
        lv_obj_set_style_pad_right(status, 8, 0);
        lv_obj_set_style_pad_top(status, 4, 0);
        lv_obj_set_style_pad_bottom(status, 4, 0);
        register_tile_open_target(status, index);
        lv_obj_align(status, LV_ALIGN_BOTTOM_RIGHT, -8, -8);

        s_pending_previews.push_back(PendingPreview{
            .index = index,
            .preview = preview,
            .canvas = preview_canvas,
            .status = status,
            .buffer = preview_buffer,
        });
        ESP_LOGI(TAG, "tile[%u] queued lazy preview path=%s", static_cast<unsigned>(index), path.c_str());

        lv_obj_t *extension = lv_label_create(preview);
        lv_label_set_text_fmt(extension, "%s", file_extension_label(path).c_str());
        lv_obj_set_style_text_font(extension, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(extension, lv_color_white(), 0);
        lv_obj_set_style_bg_color(extension, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(extension, LV_OPA_50, 0);
        lv_obj_set_style_radius(extension, 10, 0);
        lv_obj_set_style_pad_left(extension, 8, 0);
        lv_obj_set_style_pad_right(extension, 8, 0);
        lv_obj_set_style_pad_top(extension, 4, 0);
        lv_obj_set_style_pad_bottom(extension, 4, 0);
        register_tile_open_target(extension, index);
        lv_obj_align(extension, LV_ALIGN_TOP_LEFT, 8, 8);
    } else {
        ESP_LOGW(TAG, "tile[%u] preview buffer allocation failed path=%s",
                 static_cast<unsigned>(index),
                 path.c_str());
    }

    lv_obj_t *name = lv_label_create(tile);
    lv_obj_set_width(name, kTileWidth - 26);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text_fmt(name, "%s", base_name(path));
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0xF5F7FA), 0);
    register_tile_open_target(name, index);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, kTilePreviewHeight + 16);

    lv_obj_t *meta = lv_label_create(tile);
    lv_label_set_text_fmt(meta, "Tile %u", static_cast<unsigned>(index + 1));
    lv_obj_set_style_text_font(meta, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(meta, lv_color_hex(0x8B97A8), 0);
    register_tile_open_target(meta, index);
    lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, 0, -2);
}

static void build_gallery_ui(AppImageDisplay *app)
{
    stop_thumbnail_loader();
    free_tile_preview_buffers();
    s_gallery_screen = app_image_screen;
    lv_obj_clean(s_gallery_screen);
    lv_obj_set_style_bg_color(s_gallery_screen, lv_color_hex(0x090B10), 0);
    lv_obj_set_style_bg_opa(s_gallery_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_gallery_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_gallery_header = lv_obj_create(s_gallery_screen);
    lv_obj_set_size(s_gallery_header, kScreenWidth - 24, kHeaderHeight);
    lv_obj_align(s_gallery_header, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_radius(s_gallery_header, 24, 0);
    lv_obj_set_style_border_width(s_gallery_header, 0, 0);
    lv_obj_set_style_bg_color(s_gallery_header, lv_color_hex(0x111823), 0);
    lv_obj_set_style_bg_grad_color(s_gallery_header, lv_color_hex(0x1C2635), 0);
    lv_obj_set_style_bg_grad_dir(s_gallery_header, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_pad_all(s_gallery_header, 16, 0);
    lv_obj_clear_flag(s_gallery_header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_gallery_header);
    lv_label_set_text(title, "Image Gallery");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_gallery_count_label = lv_label_create(s_gallery_header);
    lv_obj_set_style_text_font(s_gallery_count_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_gallery_count_label, lv_color_hex(0xB7C2D0), 0);
    lv_obj_align(s_gallery_count_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    update_gallery_count_label(app == nullptr ? 0U : app->imagePathCount());

    s_gallery_grid = lv_obj_create(s_gallery_screen);
    lv_obj_set_size(s_gallery_grid, kScreenWidth, kScreenHeight - (kHeaderHeight + 28));
    lv_obj_align(s_gallery_grid, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_gallery_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_gallery_grid, 0, 0);
    lv_obj_set_style_pad_left(s_gallery_grid, 14, 0);
    lv_obj_set_style_pad_right(s_gallery_grid, 14, 0);
    lv_obj_set_style_pad_top(s_gallery_grid, 12, 0);
    lv_obj_set_style_pad_bottom(s_gallery_grid, 28, 0);
    lv_obj_set_style_pad_row(s_gallery_grid, 12, 0);
    lv_obj_set_style_pad_column(s_gallery_grid, 12, 0);
    lv_obj_set_layout(s_gallery_grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_gallery_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_gallery_grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    populate_gallery_tiles(app);

    s_viewer_layer = lv_obj_create(s_gallery_screen);
    lv_obj_set_size(s_viewer_layer, kScreenWidth, kScreenHeight);
    lv_obj_set_pos(s_viewer_layer, 0, 0);
    lv_obj_set_style_bg_color(s_viewer_layer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_viewer_layer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_viewer_layer, 0, 0);
    lv_obj_set_style_pad_all(s_viewer_layer, 0, 0);
    lv_obj_add_flag(s_viewer_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_viewer_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_viewer_layer, viewer_interaction_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_viewer_layer, AppImageDisplay::image_change_cb, LV_EVENT_GESTURE, app);

    for (int canvas_index = 0; canvas_index < 2; ++canvas_index) {
        s_viewer_canvas[canvas_index] = lv_canvas_create(s_viewer_layer);
        lv_obj_set_size(s_viewer_canvas[canvas_index], kScreenWidth, kScreenHeight);
        lv_obj_set_pos(s_viewer_canvas[canvas_index], 0, 0);
        lv_obj_set_style_border_width(s_viewer_canvas[canvas_index], 0, 0);
        lv_obj_set_style_bg_opa(s_viewer_canvas[canvas_index], LV_OPA_TRANSP, 0);
        reset_canvas_transform(s_viewer_canvas[canvas_index]);
    }

    s_viewer_topbar = lv_obj_create(s_viewer_layer);
    lv_obj_set_size(s_viewer_topbar, kScreenWidth - 20, 58);
    lv_obj_align(s_viewer_topbar, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_radius(s_viewer_topbar, 18, 0);
    lv_obj_set_style_bg_color(s_viewer_topbar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_viewer_topbar, 170, 0);
    lv_obj_set_style_border_width(s_viewer_topbar, 0, 0);
    lv_obj_set_style_pad_all(s_viewer_topbar, 10, 0);
    lv_obj_clear_flag(s_viewer_topbar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *close_button = lv_btn_create(s_viewer_topbar);
    lv_obj_set_size(close_button, 68, 36);
    lv_obj_align(close_button, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(close_button, 14, 0);
    lv_obj_set_style_bg_color(close_button, lv_color_hex(0x202A37), 0);
    lv_obj_add_event_cb(close_button, viewer_close_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *close_label = lv_label_create(close_button);
    lv_label_set_text(close_label, "Back");
    lv_obj_center(close_label);

    s_viewer_title = lv_label_create(s_viewer_topbar);
    lv_obj_set_width(s_viewer_title, 210);
    lv_label_set_long_mode(s_viewer_title, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_viewer_title, "");
    lv_obj_set_style_text_font(s_viewer_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_viewer_title, lv_color_white(), 0);
    lv_obj_align(s_viewer_title, LV_ALIGN_LEFT_MID, 82, 0);

    s_transition_badge = lv_label_create(s_viewer_topbar);
    lv_label_set_text(s_transition_badge, "Fade");
    lv_obj_set_style_text_font(s_transition_badge, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_transition_badge, lv_color_hex(0xD7DFEA), 0);
    lv_obj_align(s_transition_badge, LV_ALIGN_RIGHT_MID, 0, 0);

    s_viewer_hint = lv_label_create(s_viewer_layer);
    lv_label_set_text(s_viewer_hint, "Swipe left or right. If idle, fullscreen switches to a random image every 15 seconds.");
    lv_obj_set_width(s_viewer_hint, kScreenWidth - 36);
    lv_label_set_long_mode(s_viewer_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_viewer_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_viewer_hint, lv_color_hex(0xD7DFEA), 0);
    lv_obj_align(s_viewer_hint, LV_ALIGN_BOTTOM_MID, 0, -18);

    start_thumbnail_loader();
}

static void free_tile_preview_buffers()
{
    s_pending_previews.clear();
    s_next_pending_preview = 0;
    ++s_thumbnail_generation;
    if (s_thumbnail_generation == 0U) {
        s_thumbnail_generation = 1U;
    }

    for (lv_color_t *buffer : s_tile_preview_buffers) {
        free(buffer);
    }
    s_tile_preview_buffers.clear();
}

static void update_gallery_count_label(size_t image_count)
{
    if (s_gallery_count_label == nullptr) {
        return;
    }

    lv_label_set_text_fmt(s_gallery_count_label,
                          "%u %s",
                          static_cast<unsigned>(image_count),
                          image_count == 1U ? "file" : "files");
}

static void populate_gallery_tiles(AppImageDisplay *app)
{
    s_pending_previews.clear();
    s_next_pending_preview = 0;

    const size_t image_count = app == nullptr ? 0U : app->imagePathCount();
    if (image_count == 0U) {
        lv_obj_t *empty_card = lv_obj_create(s_gallery_grid);
        lv_obj_set_size(empty_card, kScreenWidth - 44, 180);
        lv_obj_set_style_radius(empty_card, 26, 0);
        lv_obj_set_style_border_width(empty_card, 0, 0);
        lv_obj_set_style_bg_color(empty_card, lv_color_hex(0x141A24), 0);
        lv_obj_set_style_pad_all(empty_card, 18, 0);
        lv_obj_clear_flag(empty_card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *empty_title = lv_label_create(empty_card);
        lv_label_set_text(empty_title, "No supported images found");
        lv_obj_set_style_text_font(empty_title, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(empty_title, lv_color_white(), 0);
        lv_obj_align(empty_title, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *empty_text = lv_label_create(empty_card);
        lv_obj_set_width(empty_text, kScreenWidth - 88);
        lv_label_set_long_mode(empty_text, LV_LABEL_LONG_WRAP);
        lv_label_set_text(empty_text,
                          "Put JPG, JPEG, or PNG files in /sdcard/image or /sdcard/imagefolder. New files are picked up automatically while this screen stays open.");
        lv_obj_set_style_text_font(empty_text, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(empty_text, lv_color_hex(0xC6D0DB), 0);
        lv_obj_align(empty_text, LV_ALIGN_TOP_LEFT, 0, 44);
        return;
    }

    for (size_t index = 0; index < image_count; ++index) {
        create_gallery_tile(app, index);
    }
}

static void stop_thumbnail_loader()
{
    if (s_thumbnail_loader_timer != nullptr) {
        lv_timer_del(s_thumbnail_loader_timer);
        s_thumbnail_loader_timer = nullptr;
    }
}

static void start_thumbnail_loader()
{
    stop_thumbnail_loader();
    if (s_pending_previews.empty()) {
        return;
    }

    s_thumbnail_loader_timer = lv_timer_create(thumbnail_loader_timer_cb, 40, nullptr);
    if (s_thumbnail_loader_timer != nullptr) {
        lv_timer_set_repeat_count(s_thumbnail_loader_timer, -1);
        lv_timer_ready(s_thumbnail_loader_timer);
        ESP_LOGI(TAG, "thumbnail loader started queued=%u", static_cast<unsigned>(s_pending_previews.size()));
    } else {
        ESP_LOGW(TAG, "thumbnail loader timer creation failed queued=%u", static_cast<unsigned>(s_pending_previews.size()));
    }
}

static void thumbnail_loader_timer_cb(lv_timer_t *timer)
{
    if (s_active_app == nullptr) {
        ESP_LOGI(TAG, "thumbnail loader complete processed=%u", static_cast<unsigned>(s_next_pending_preview));
        stop_thumbnail_loader();
        return;
    }

    if ((s_thumbnail_decode_event == nullptr) || (s_thumbnail_decode_mutex == nullptr)) {
        if (s_next_pending_preview >= s_pending_previews.size()) {
            ESP_LOGI(TAG, "thumbnail loader complete processed=%u", static_cast<unsigned>(s_next_pending_preview));
            stop_thumbnail_loader();
            return;
        }

        PendingPreview &pending = s_pending_previews[s_next_pending_preview++];
        if ((pending.canvas == nullptr) || (pending.buffer == nullptr) || (pending.index >= s_active_app->imagePathCount())) {
            ESP_LOGW(TAG, "thumbnail loader skipped invalid pending item index=%u", static_cast<unsigned>(pending.index));
            return;
        }

        const std::string &path = s_active_app->imagePathAt(pending.index);
        bool cache_hit = false;
        const bool decoded = load_or_generate_thumbnail(path, pending.buffer, &cache_hit);
        if (!decoded) {
            draw_preview_placeholder(pending.canvas, pending.buffer, path);
        } else {
            lv_obj_invalidate(pending.canvas);
        }
        if (pending.status != nullptr) {
            lv_label_set_text(pending.status, decoded ? (cache_hit ? "Cached" : "Ready") : "Fallback");
            lv_obj_add_flag(pending.status, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (xSemaphoreTake(s_thumbnail_decode_mutex, 0) != pdTRUE) {
        return;
    }

    if (s_thumbnail_decode_in_flight) {
        xSemaphoreGive(s_thumbnail_decode_mutex);
        return;
    }

    if (s_next_pending_preview >= s_pending_previews.size()) {
        xSemaphoreGive(s_thumbnail_decode_mutex);
        ESP_LOGI(TAG, "thumbnail loader complete processed=%u", static_cast<unsigned>(s_next_pending_preview));
        stop_thumbnail_loader();
        return;
    }

    PendingPreview &pending = s_pending_previews[s_next_pending_preview++];
    if ((pending.canvas == nullptr) || (pending.buffer == nullptr) || (pending.index >= s_active_app->imagePathCount())) {
        xSemaphoreGive(s_thumbnail_decode_mutex);
        ESP_LOGW(TAG, "thumbnail loader skipped invalid pending item index=%u", static_cast<unsigned>(pending.index));
        return;
    }

    const std::string &path = s_active_app->imagePathAt(pending.index);
    ESP_LOGI(TAG,
             "thumbnail loader decode start tile=%u remaining=%u path=%s",
             static_cast<unsigned>(pending.index),
             static_cast<unsigned>(s_pending_previews.size() - s_next_pending_preview),
             path.c_str());
    s_thumbnail_decode_request = ThumbnailDecodeRequest{
        .app = s_active_app,
        .index = pending.index,
        .generation = s_thumbnail_generation,
        .canvas = pending.canvas,
        .status = pending.status,
        .target_buffer = pending.buffer,
        .path = path,
    };
    s_thumbnail_decode_in_flight = true;
    xSemaphoreGive(s_thumbnail_decode_mutex);
    xSemaphoreGive(s_thumbnail_decode_event);
    (void)timer;
}

static void apply_thumbnail_decode_async(void *context)
{
    std::unique_ptr<ThumbnailDecodeResult> result(static_cast<ThumbnailDecodeResult *>(context));
    if (result == nullptr) {
        return;
    }

    if ((s_thumbnail_decode_mutex != nullptr) && (xSemaphoreTake(s_thumbnail_decode_mutex, pdMS_TO_TICKS(20)) == pdTRUE)) {
        s_thumbnail_decode_in_flight = false;
        xSemaphoreGive(s_thumbnail_decode_mutex);
    }

    const bool generation_matches = (result->generation == s_thumbnail_generation) && (result->app == s_active_app);
    if (generation_matches && result->decoded && (result->decoded_buffer != nullptr) && (result->target_buffer != nullptr)) {
        memcpy(result->target_buffer,
               result->decoded_buffer,
               static_cast<size_t>(kTilePreviewWidth) * static_cast<size_t>(kTilePreviewHeight) * sizeof(lv_color_t));
        if (result->canvas != nullptr) {
            lv_obj_invalidate(result->canvas);
        }
    }

    if (generation_matches && (result->status != nullptr)) {
        lv_label_set_text(result->status,
                          result->decoded ? (result->cache_hit ? "Cached" : "Ready") : "Fallback");
        lv_obj_add_flag(result->status, LV_OBJ_FLAG_HIDDEN);
    }

    ESP_LOGI(TAG,
             "thumbnail loader decode done tile=%u result=%s cache=%s generation=%u current_generation=%u",
             static_cast<unsigned>(result->index),
             result->decoded ? "ok" : "failed",
             result->cache_hit ? "hit" : "miss",
             static_cast<unsigned>(result->generation),
             static_cast<unsigned>(s_thumbnail_generation));

    if (result->decoded_buffer != nullptr) {
        free(result->decoded_buffer);
    }

    if (s_thumbnail_loader_timer != nullptr) {
        lv_timer_ready(s_thumbnail_loader_timer);
    }
}

static void thumbnail_decode_task(void *context)
{
    (void)context;

    while (true) {
        xSemaphoreTake(s_thumbnail_decode_event, portMAX_DELAY);

        ThumbnailDecodeRequest request;
        bool has_request = false;
        if ((s_thumbnail_decode_mutex != nullptr) && (xSemaphoreTake(s_thumbnail_decode_mutex, pdMS_TO_TICKS(20)) == pdTRUE)) {
            if (s_thumbnail_decode_in_flight) {
                request = s_thumbnail_decode_request;
                has_request = true;
            }
            xSemaphoreGive(s_thumbnail_decode_mutex);
        }

        if (!has_request) {
            continue;
        }

        ThumbnailDecodeResult *result = new (std::nothrow) ThumbnailDecodeResult{
            .app = request.app,
            .index = request.index,
            .generation = request.generation,
            .canvas = request.canvas,
            .status = request.status,
            .target_buffer = request.target_buffer,
            .decoded_buffer = nullptr,
            .decoded = false,
            .cache_hit = false,
        };
        if (result == nullptr) {
            ESP_LOGE(TAG, "thumbnail worker alloc failed tile=%u", static_cast<unsigned>(request.index));
            if ((s_thumbnail_decode_mutex != nullptr) && (xSemaphoreTake(s_thumbnail_decode_mutex, pdMS_TO_TICKS(20)) == pdTRUE)) {
                s_thumbnail_decode_in_flight = false;
                xSemaphoreGive(s_thumbnail_decode_mutex);
            }
            continue;
        }

        result->decoded_buffer = static_cast<lv_color_t *>(heap_caps_malloc(static_cast<size_t>(kTilePreviewWidth) * static_cast<size_t>(kTilePreviewHeight) * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (result->decoded_buffer == nullptr) {
            result->decoded_buffer = static_cast<lv_color_t *>(heap_caps_malloc(static_cast<size_t>(kTilePreviewWidth) * static_cast<size_t>(kTilePreviewHeight) * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }

        if (result->decoded_buffer != nullptr) {
            result->decoded = load_or_generate_thumbnail(request.path,
                                                         result->decoded_buffer,
                                                         &result->cache_hit);
            if (!result->decoded) {
                free(result->decoded_buffer);
                result->decoded_buffer = nullptr;
            }
        }

        if (!queue_lvgl_async_locked(apply_thumbnail_decode_async, result)) {
            if (result->decoded_buffer != nullptr) {
                free(result->decoded_buffer);
            }
            delete result;
            if ((s_thumbnail_decode_mutex != nullptr) && (xSemaphoreTake(s_thumbnail_decode_mutex, pdMS_TO_TICKS(20)) == pdTRUE)) {
                s_thumbnail_decode_in_flight = false;
                xSemaphoreGive(s_thumbnail_decode_mutex);
            }
        }
    }
}

static void apply_image_path_refresh_async(void *context)
{
    std::unique_ptr<PendingImageListRefresh> refresh(static_cast<PendingImageListRefresh *>(context));
    if ((refresh == nullptr) || (refresh->app == nullptr) || (s_active_app != refresh->app)) {
        return;
    }

    const std::vector<std::string> previous_paths = refresh->app->imagePathsSnapshot();
    if (previous_paths == refresh->image_paths) {
        update_gallery_count_label(previous_paths.size());
        return;
    }

    std::string current_path;
    if ((s_current_index >= 0) && (static_cast<size_t>(s_current_index) < previous_paths.size())) {
        current_path = previous_paths[static_cast<size_t>(s_current_index)];
    }

    refresh->app->replaceImagePaths(std::move(refresh->image_paths));
    update_gallery_count_label(refresh->app->imagePathCount());

    if (!current_path.empty()) {
        const int updated_index = refresh->app->findImagePathIndex(current_path);
        if (updated_index >= 0) {
            s_current_index = updated_index;
            if (s_fullscreen_visible) {
                update_viewer_title(refresh->app->imagePathAt(static_cast<size_t>(updated_index)),
                                    s_last_transition >= 0 ? s_last_transition : 0);
            }
        } else if (refresh->app->imagePathCount() == 0U) {
            close_viewer();
            s_current_index = -1;
        } else if (s_current_index >= static_cast<int>(refresh->app->imagePathCount())) {
            s_current_index = static_cast<int>(refresh->app->imagePathCount()) - 1;
        }
    }

    if (s_fullscreen_visible) {
        s_gallery_rebuild_pending = true;
    } else {
        build_gallery_ui(refresh->app);
        s_gallery_rebuild_pending = false;
    }

    maybe_queue_debug_open(refresh->app);
    ESP_LOGI(TAG,
             "Applied image directory refresh old_count=%u new_count=%u fullscreen=%s",
             static_cast<unsigned>(previous_paths.size()),
             static_cast<unsigned>(refresh->app->imagePathCount()),
             s_fullscreen_visible ? "yes" : "no");
}

static void image_directory_refresh_task(void *context)
{
    AppImageDisplay *app = static_cast<AppImageDisplay *>(context);
    std::vector<std::string> last_seen_paths = scan_supported_image_paths();
    prune_thumbnail_cache(last_seen_paths);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(kImageDirectoryPollMs));

        if ((app == nullptr) || (s_active_app != app)) {
            continue;
        }

        std::vector<std::string> scanned_paths = scan_supported_image_paths();
        if (scanned_paths == last_seen_paths) {
            continue;
        }

        ESP_LOGI(TAG,
                 "Detected image directory change old_count=%u new_count=%u",
                 static_cast<unsigned>(last_seen_paths.size()),
                 static_cast<unsigned>(scanned_paths.size()));
        prune_thumbnail_cache(scanned_paths);

        PendingImageListRefresh *refresh = new (std::nothrow) PendingImageListRefresh();
        if (refresh == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate image refresh payload");
            last_seen_paths = std::move(scanned_paths);
            continue;
        }

        refresh->app = app;
        refresh->image_paths = std::move(scanned_paths);
        last_seen_paths = refresh->image_paths;

        if (!queue_lvgl_async_locked(apply_image_path_refresh_async, refresh)) {
            ESP_LOGW(TAG, "Failed to queue image directory refresh");
            delete refresh;
        }
    }
}

} // namespace

LV_IMG_DECLARE(img_app_img_display);

AppImageDisplay::AppImageDisplay():
    ESP_Brookesia_PhoneApp("Image", &img_app_img_display, true),
    _image_name(nullptr),
    _image_file_iterator(nullptr),
    _image_paths_loaded(false)
{
}

AppImageDisplay::~AppImageDisplay()
{
}

bool AppImageDisplay::run(void)
{
    s_active_app = this;

    if (!_image_paths_loaded) {
        replaceImagePaths(scanImagePaths());
    }

    ESP_LOGI(TAG, "run begin image_count=%u", static_cast<unsigned>(imagePathCount()));

    if (image_task_event == nullptr) {
        image_task_event = xSemaphoreCreateBinary();
        if (image_task_event == nullptr) {
            ESP_LOGE(TAG, "Failed to create image task semaphore");
            return false;
        }
    }

    if (image_muxe == nullptr) {
        image_muxe = xSemaphoreCreateRecursiveMutex();
        if (image_muxe == nullptr) {
            ESP_LOGE(TAG, "Failed to create image mutex");
            return false;
        }
    }

    if (s_thumbnail_decode_event == nullptr) {
        s_thumbnail_decode_event = xSemaphoreCreateBinary();
        if (s_thumbnail_decode_event == nullptr) {
            ESP_LOGE(TAG, "Failed to create thumbnail decode event");
            return false;
        }
    }

    if (s_thumbnail_decode_mutex == nullptr) {
        s_thumbnail_decode_mutex = xSemaphoreCreateMutex();
        if (s_thumbnail_decode_mutex == nullptr) {
            ESP_LOGE(TAG, "Failed to create thumbnail decode mutex");
            return false;
        }
    }

    if (time_refer_handle == nullptr) {
        const esp_timer_create_args_t timer_args = {
            .callback = &timer_refersh_task,
            .name = "image_idle"
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &time_refer_handle));
    }

    static bool s_worker_started = false;
    if (!s_worker_started) {
        if (create_background_task_prefer_psram(reinterpret_cast<TaskFunction_t>(image_delay_change),
                                                "ImageFrame",
                                                4096,
                                                this,
                                                3,
                                                nullptr,
                                                0) != pdPASS) {
            ESP_LOGE(TAG, "Failed to start image worker");
            return false;
        }
        s_worker_started = true;
    }

    static bool s_directory_worker_started = false;
    if (!s_directory_worker_started) {
        if (create_background_task_prefer_psram(image_directory_refresh_task,
                                                "ImageScan",
                                                6144,
                                                this,
                                                2,
                                                nullptr,
                                                0) != pdPASS) {
            ESP_LOGE(TAG, "Failed to start image directory worker");
            return false;
        }
        s_directory_worker_started = true;
    }

    static bool s_thumbnail_worker_started = false;
    if (!s_thumbnail_worker_started) {
        if (create_background_task_prefer_psram(thumbnail_decode_task,
                                                "ImageThumb",
                                                8192,
                                                nullptr,
                                                1,
                                                nullptr,
                                                0) != pdPASS) {
            ESP_LOGE(TAG, "Failed to start thumbnail decode worker");
            return false;
        }
        s_thumbnail_worker_started = true;
    }

    app_image_display_init();
    build_gallery_ui(this);

    s_current_index = -1;
    s_fullscreen_visible = false;
    s_gallery_rebuild_pending = false;
    stop_idle_slideshow();
    maybe_queue_debug_open(this);
    ESP_LOGI(TAG, "run ready lazy_previews=%u", static_cast<unsigned>(s_pending_previews.size()));
    return true;
}

bool AppImageDisplay::pause(void)
{
    stop_idle_slideshow();
    ESP_LOGI(TAG, "pause current_index=%d", s_current_index);
    return true;
}

bool AppImageDisplay::resume(void)
{
    ESP_LOGI(TAG, "resume current_index=%d visible=%s", s_current_index, s_fullscreen_visible ? "yes" : "no");
    maybe_queue_debug_open(this);
    note_viewer_interaction();
    return true;
}

bool AppImageDisplay::back(void)
{
    if (s_fullscreen_visible) {
        close_viewer();
        return true;
    }
    return notifyCoreClosed();
}

bool AppImageDisplay::close(void)
{
    stop_idle_slideshow();
    stop_thumbnail_loader();
    s_fullscreen_visible = false;
    s_current_index = -1;
    s_active_app = nullptr;
    s_gallery_rebuild_pending = false;
    free_tile_preview_buffers();

    for (int index = 0; index < 2; ++index) {
        free(s_viewer_buffers[index]);
        s_viewer_buffers[index] = nullptr;
    }
    return true;
}

bool AppImageDisplay::init(void)
{
    _image_file_iterator = nullptr;
    _image_paths.clear();
    _image_paths_loaded = false;
    return true;
}

std::vector<std::string> AppImageDisplay::scanImagePaths() const
{
    return scan_supported_image_paths();
}

size_t AppImageDisplay::imagePathCount() const
{
    return _image_paths.size();
}

const std::string &AppImageDisplay::imagePathAt(size_t index) const
{
    return _image_paths.at(index);
}

std::vector<std::string> AppImageDisplay::imagePathsSnapshot() const
{
    return _image_paths;
}

void AppImageDisplay::replaceImagePaths(std::vector<std::string> image_paths)
{
    _image_paths = std::move(image_paths);
    _image_paths_loaded = true;
}

int AppImageDisplay::findImagePathIndex(const std::string &path) const
{
    const auto iterator = std::find(_image_paths.begin(), _image_paths.end(), path);
    if (iterator == _image_paths.end()) {
        return -1;
    }
    return static_cast<int>(std::distance(_image_paths.begin(), iterator));
}

bool AppImageDisplay::debugQueueOpenIndex(size_t index)
{
    if (!_image_paths_loaded) {
        replaceImagePaths(scanImagePaths());
    }

    if (index >= imagePathCount()) {
        ESP_LOGW(TAG, "debugQueueOpenIndex rejected index=%u count=%u", static_cast<unsigned>(index), static_cast<unsigned>(imagePathCount()));
        return false;
    }

    s_debug_pending_open_index = static_cast<int>(index);
    s_debug_pending_open_animate = false;
    s_debug_pending_open_reason = "serial-debug-open";
    ESP_LOGI(TAG,
             "debugQueueOpenIndex queued index=%u active=%s current_index=%d fullscreen=%s",
             static_cast<unsigned>(index),
             (s_active_app == this) ? "yes" : "no",
             s_current_index,
             s_fullscreen_visible ? "yes" : "no");

    if (s_active_app == this) {
        maybe_queue_debug_open(this);
    }
    return true;
}

std::string AppImageDisplay::debugDescribeState() const
{
    const char *current_name = "none";
    if ((s_current_index >= 0) && (static_cast<size_t>(s_current_index) < imagePathCount())) {
        current_name = base_name(imagePathAt(static_cast<size_t>(s_current_index)));
    }

    char buffer[320];
    snprintf(buffer,
             sizeof(buffer),
             "images=%u loaded=%s active=%s fullscreen=%s current_index=%d current_name=%s pending_debug_index=%d",
             static_cast<unsigned>(imagePathCount()),
             _image_paths_loaded ? "yes" : "no",
             (s_active_app == this) ? "yes" : "no",
             s_fullscreen_visible ? "yes" : "no",
             s_current_index,
             current_name,
             s_debug_pending_open_index);
    return std::string(buffer);
}

void AppImageDisplay::image_change_cb(lv_event_t *event)
{
    if ((lv_event_get_code(event) != LV_EVENT_GESTURE) || !s_fullscreen_visible || (s_active_app == nullptr) || (image_muxe == nullptr)) {
        return;
    }

    lv_indev_t *input_device = lv_indev_get_act();
    if (input_device == nullptr) {
        return;
    }

    lv_indev_wait_release(input_device);
    const lv_dir_t direction = lv_indev_get_gesture_dir(input_device);
    int next_index = s_current_index;
    if (direction == LV_DIR_LEFT) {
        next_index = (s_current_index + 1) % static_cast<int>(s_active_app->imagePathCount());
    } else if (direction == LV_DIR_RIGHT) {
        next_index = (s_current_index - 1 + static_cast<int>(s_active_app->imagePathCount())) % static_cast<int>(s_active_app->imagePathCount());
    } else {
        return;
    }

    ESP_LOGI(TAG, "image_change_cb direction=%d current_index=%d next_index=%d", static_cast<int>(direction), s_current_index, next_index);

    if (xSemaphoreTakeRecursive(image_muxe, pdMS_TO_TICKS(50)) == pdTRUE) {
        show_image_index(s_active_app, next_index, true);
        xSemaphoreGiveRecursive(image_muxe);
    }
}

void AppImageDisplay::image_delay_change(AppImageDisplay *app)
{
    while (true) {
        xSemaphoreTake(image_task_event, portMAX_DELAY);
        if (!s_fullscreen_visible || (app == nullptr) || (app->imagePathCount() < 2U) || (image_muxe == nullptr)) {
            ESP_LOGI(TAG,
                     "image_delay_change skip visible=%s app=%p count=%u mutex=%p",
                     s_fullscreen_visible ? "yes" : "no",
                     app,
                     app == nullptr ? 0U : static_cast<unsigned>(app->imagePathCount()),
                     image_muxe);
            continue;
        }

        int next_index = s_current_index;
        while (next_index == s_current_index) {
            next_index = static_cast<int>(esp_random() % app->imagePathCount());
        }

        ESP_LOGI(TAG, "image_delay_change queue slideshow current_index=%d next_index=%d", s_current_index, next_index);
        if (!queue_viewer_request(app, next_index, true, "idle-slideshow")) {
            ESP_LOGW(TAG, "image_delay_change failed to queue slideshow index=%d", next_index);
        }
    }
}

void AppImageDisplay::timer_refersh_task(void *arg)
{
    ESP_LOGI(TAG, "timer_refersh_task fired visible=%s current_index=%d", s_fullscreen_visible ? "yes" : "no", s_current_index);
    if (image_task_event != nullptr) {
        xSemaphoreGive(image_task_event);
    }
}


