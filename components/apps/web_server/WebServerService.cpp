#include "WebServerService.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <limits>
#include <new>
#include <algorithm>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "bsp/esp-bsp.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "mdns.h"
#include "storage_access.h"

namespace {

static const char *TAG = "WebServerSvc";
static constexpr const char *kSdCardRoot = "/sdcard";
static constexpr const char *kSdWebRoot = "/sdcard/web";
static constexpr const char *kSpiffsWebRoot = BSP_SPIFFS_MOUNT_POINT "/web";
static constexpr const char *kSpiffsRoot = BSP_SPIFFS_MOUNT_POINT;
static constexpr const char *kRecoveryStorageSd = "sd";
static constexpr const char *kRecoveryStorageSpiffs = "spiffs";
static constexpr const char *kMdnsHostName = "jc4880-web";
static constexpr const char *kMdnsInstanceName = "JC4880 Web Server";
static constexpr const char *kSpiffsDirectoryMarker = ".jc4880.keep";
static constexpr const char *kSpiffsDirectoryIndexFile = BSP_SPIFFS_MOUNT_POINT "/.jc4880_dirs.idx";
static constexpr size_t kIoBufferSize = 8192;
static constexpr int64_t kRecoveryStorageCacheTtlUs = 1500 * 1000;

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

struct RecoveryTargetInfo {
    const char *storage_id;
    const char *storage_label;
    const char *root_path;
    uint64_t total_bytes;
    uint64_t free_bytes;
};

struct RecoveryStorageStatsCache {
    uint64_t total_bytes;
    uint64_t free_bytes;
    int64_t updated_at_us;
    bool valid;
};

RecoveryStorageStatsCache s_sd_storage_stats_cache = {};
RecoveryStorageStatsCache s_spiffs_storage_stats_cache = {};

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
    h1 { margin:0 0 8px; font-size:2rem; text-align:center; text-transform:uppercase; letter-spacing:.08em; }
    p { line-height:1.55; color:#cbd5e1; }
    .storage-summary { margin:18px 0 8px; padding:14px 16px; border-radius:16px; background:rgba(15,23,42,.7); border:1px solid rgba(148,163,184,.18); color:#cbd5e1; font-weight:600; }
    .storage-summary-text { margin-bottom:10px; }
    .storage-summary-bar { width:100%; height:14px; overflow:hidden; border-radius:999px; background:rgba(51,65,85,.95); box-shadow:inset 0 1px 3px rgba(2,6,23,.45); }
    .storage-summary-used { height:100%; width:0%; background:linear-gradient(90deg,#f59e0b,#f97316); transition:width .18s ease; }
    .row { display:flex; flex-wrap:wrap; gap:12px; margin:18px 0 10px; align-items:center; }
    .toolbar-spacer { flex:1 1 auto; }
        .folder-create-row { display:none; flex-wrap:wrap; gap:10px; margin:0 0 14px; align-items:center; }
        .folder-create-row.visible { display:flex; }
    button, label.cta { border:none; border-radius:14px; padding:12px 18px; font-size:1rem; font-weight:700; cursor:pointer; }
    button.primary, label.cta { background:#22c55e; color:#052e16; }
    button.secondary { background:#1d4ed8; color:#eff6ff; }
    button.ghost { background:#334155; color:#e2e8f0; }
    .toolbar-icon { position:relative; width:48px; height:48px; min-width:48px; min-height:48px; padding:0; display:inline-flex; align-items:center; justify-content:center; font-size:1.15rem; box-sizing:border-box; flex:0 0 48px; transition:transform .14s ease, box-shadow .14s ease, filter .14s ease, background-color .14s ease; }
    .toolbar-icon:hover { transform:translateY(-2px); filter:brightness(1.08); box-shadow:0 12px 24px rgba(15,23,42,.28); }
    .toolbar-icon:active { transform:translateY(0) scale(.94); filter:brightness(.96); box-shadow:0 4px 10px rgba(15,23,42,.22) inset; }
    .toolbar-icon[data-tooltip]::after { content:attr(data-tooltip); position:absolute; left:50%; bottom:calc(100% + 10px); transform:translate(-50%, 6px); opacity:0; pointer-events:none; white-space:nowrap; background:rgba(15,23,42,.96); color:#e2e8f0; border:1px solid rgba(148,163,184,.26); border-radius:10px; padding:7px 10px; font-size:.82rem; font-weight:600; box-shadow:0 12px 28px rgba(2,6,23,.35); transition:opacity .14s ease, transform .14s ease; z-index:2; }
    .toolbar-icon[data-tooltip]::before { content:""; position:absolute; left:50%; bottom:calc(100% + 4px); width:10px; height:10px; background:rgba(15,23,42,.96); border-right:1px solid rgba(148,163,184,.26); border-bottom:1px solid rgba(148,163,184,.26); transform:translate(-50%, 6px) rotate(45deg); opacity:0; pointer-events:none; transition:opacity .14s ease, transform .14s ease; z-index:1; }
    .toolbar-icon:hover::after, .toolbar-icon:hover::before, .toolbar-icon:focus-visible::after, .toolbar-icon:focus-visible::before { opacity:1; transform:translate(-50%, 0); }
    .toolbar-icon:focus-visible { outline:2px solid rgba(125,211,252,.95); outline-offset:3px; }
    .storage-button { padding:0 16px; min-width:96px; height:48px; display:inline-flex; align-items:center; justify-content:center; box-sizing:border-box; }
    .storage-button.active { background:#0f766e; color:#ecfeff; }
    input[type=file] { display:none; }
    input[type=text] { min-width:220px; padding:12px 14px; border-radius:14px; border:1px solid rgba(148,163,184,.35); background:#0f172a; color:#e2e8f0; font-size:1rem; }
    #status { min-height:24px; margin:8px 0 18px; font-weight:600; color:#7dd3fc; }
    .upload-progress { margin:0 0 18px; padding:14px 16px; border-radius:16px; background:rgba(15,23,42,.7); border:1px solid rgba(148,163,184,.18); display:none; }
    .upload-progress.visible { display:block; }
    .upload-progress-header { display:flex; justify-content:space-between; gap:12px; margin-bottom:8px; color:#cbd5e1; font-weight:600; }
    .upload-progress-file { color:#e2e8f0; font-weight:700; word-break:break-word; margin-bottom:8px; }
    .upload-progress details { margin-top:8px; }
    .upload-progress summary { cursor:pointer; color:#93c5fd; }
    .upload-progress progress { width:100%; height:16px; }
    .upload-progress-meta { margin-top:8px; color:#94a3b8; font-size:.92rem; }
        #path { margin:0 0 14px; font-weight:600; color:#93c5fd; }
    ul { list-style:none; padding:0; margin:0; }
        li { display:flex; align-items:center; gap:12px; padding:10px 0; border-bottom:1px solid rgba(148,163,184,.12); }
        .entry-main { flex:1 1 auto; min-width:0; }
        li button.file-entry { width:100%; text-align:left; padding:10px 0; background:transparent; border:none; border-radius:0; color:#e2e8f0; font-size:1rem; font-weight:600; }
        li .meta { display:block; margin-top:4px; color:#94a3b8; font-size:.88rem; font-weight:500; }
        .entry-actions { display:flex; align-items:center; gap:8px; flex:0 0 auto; }
        .icon-button { min-width:42px; min-height:42px; padding:0; display:inline-flex; align-items:center; justify-content:center; border-radius:12px; font-size:1.05rem; }
        .icon-download { background:#1d4ed8; color:#eff6ff; }
        .icon-delete { background:#7f1d1d; color:#fee2e2; }
    code { background:#1e293b; padding:2px 6px; border-radius:8px; color:#bfdbfe; }
  </style>
</head>
<body>
  <main>
    <section class="panel">
      <h1>Recovery Uploader</h1>
            <div class="storage-summary" id="storageSummary">
                <div class="storage-summary-text" id="storageSummaryText">SD card: checking capacity...</div>
                <div class="storage-summary-bar"><div class="storage-summary-used" id="storageSummaryUsedBar"></div></div>
            </div>
      <div class="row">
                <button class="ghost toolbar-icon" id="upButton" title="Up" aria-label="Up" data-tooltip="Up">↥</button>
            <button class="ghost toolbar-icon" id="openRoot" title="Open root" aria-label="Open root" data-tooltip="Open root">⌂</button>
                <button class="primary toolbar-icon" id="createFolderButton" title="Create folder" aria-label="Create folder" data-tooltip="Create folder">＋</button>
                <label class="cta toolbar-icon" for="folderPicker" title="Upload folder" aria-label="Upload folder" data-tooltip="Upload folder">📁</label>
        <input id="folderPicker" type="file" webkitdirectory directory multiple>
                <label class="cta toolbar-icon" for="filePicker" title="Upload files" aria-label="Upload files" data-tooltip="Upload files">⬆</label>
        <input id="filePicker" type="file" multiple>
                        <button class="secondary toolbar-icon" id="refreshButton" title="Refresh" aria-label="Refresh" data-tooltip="Refresh">⟳</button>
                    <div class="toolbar-spacer"></div>
                    <button class="ghost storage-button active" id="storageSwitchSd" type="button">SDCARD</button>
                    <button class="ghost storage-button" id="storageSwitchSpiffs" type="button">SPIFFS</button>
            </div>
            <div class="folder-create-row" id="createFolderRow">
                <input id="createFolderName" type="text" placeholder="Folder name" maxlength="120">
                <button class="primary" id="confirmCreateFolderButton">Create</button>
                <button class="ghost" id="cancelCreateFolderButton">Cancel</button>
            </div>
            <div id="path">Current folder: /</div>
      <div id="status">Waiting for files.</div>
            <div class="upload-progress" id="uploadProgress">
                <div class="upload-progress-header">
                    <span id="uploadProgressSummary">Idle</span>
                    <span id="uploadProgressPercent">0%</span>
                </div>
                <div class="upload-progress-file" id="uploadProgressFile">No file selected</div>
                <progress id="uploadProgressBar" value="0" max="100"></progress>
                <div class="upload-progress-meta" id="uploadProgressMeta">0 B / 0 B at 0 B/s</div>
            </div>
      <ul id="files"></ul>
    </section>
  </main>
  <script>
    const statusEl = document.getElementById('status');
    const filesEl = document.getElementById('files');
        const pathEl = document.getElementById('path');
        const storageSummaryEl = document.getElementById('storageSummary');
    const storageSummaryTextEl = document.getElementById('storageSummaryText');
    const storageSummaryUsedBarEl = document.getElementById('storageSummaryUsedBar');
        const storageSwitchSdEl = document.getElementById('storageSwitchSd');
        const storageSwitchSpiffsEl = document.getElementById('storageSwitchSpiffs');
        const createFolderRowEl = document.getElementById('createFolderRow');
        const createFolderNameEl = document.getElementById('createFolderName');
        const uploadProgressEl = document.getElementById('uploadProgress');
        const uploadProgressSummaryEl = document.getElementById('uploadProgressSummary');
        const uploadProgressPercentEl = document.getElementById('uploadProgressPercent');
        const uploadProgressFileEl = document.getElementById('uploadProgressFile');
        const uploadProgressBarEl = document.getElementById('uploadProgressBar');
        const uploadProgressMetaEl = document.getElementById('uploadProgressMeta');
        let currentPath = '';
        let currentStorageId = 'sd';
        let currentStorageLabel = 'SD Card';
        let currentRootPath = '/sdcard';
        let storageSummaryRequestId = 0;

    function setStatus(text, isError = false) {
      statusEl.textContent = text;
      statusEl.style.color = isError ? '#fca5a5' : '#7dd3fc';
    }

        function normalizeRelativePath(path) {
            return (path || '').replace(/^\/+/, '').replace(/\\/g, '/').replace(/\/+/g, '/').replace(/\/$/, '');
        }

        function currentPathLabel() {
            return currentPath ? `${currentRootPath}/${currentPath}` : currentRootPath;
        }

        function updateStorageButtons() {
            storageSwitchSdEl.classList.toggle('active', currentStorageId === 'sd');
            storageSwitchSpiffsEl.classList.toggle('active', currentStorageId === 'spiffs');
        }

        function rootPathForStorage(storageId) {
            return storageId === 'spiffs' ? '/spiffs' : '/sdcard';
        }

        function setCreateFolderVisible(visible) {
            createFolderRowEl.classList.toggle('visible', visible);
            if (visible) {
                createFolderNameEl.focus();
                createFolderNameEl.select();
                return;
            }
            createFolderNameEl.value = '';
        }

        function joinRelativePath(base, leaf) {
            const cleanBase = normalizeRelativePath(base);
            const cleanLeaf = normalizeRelativePath(leaf);
            if (!cleanBase) {
                return cleanLeaf;
            }
            if (!cleanLeaf) {
                return cleanBase;
            }
            return `${cleanBase}/${cleanLeaf}`;
        }

        function formatBytes(bytes) {
            const value = Number(bytes) || 0;
            if (value < 1024) {
                return `${value} B`;
            }
            const units = ['KB', 'MB', 'GB'];
            let size = value / 1024;
            let unitIndex = 0;
            while (size >= 1024 && unitIndex < units.length - 1) {
                size /= 1024;
                unitIndex += 1;
            }
            return `${size.toFixed(size >= 10 ? 1 : 2)} ${units[unitIndex]}`;
        }

        function setUploadProgressVisible(visible) {
            uploadProgressEl.classList.toggle('visible', visible);
        }

        function updateStorageSummary(storageLabel, totalBytes, freeBytes) {
            const safeTotal = Number(totalBytes) || 0;
            const safeFree = Number(freeBytes) || 0;
            const usedBytes = Math.max(0, safeTotal - safeFree);
            if (safeTotal <= 0) {
                storageSummaryTextEl.textContent = `${storageLabel}: capacity unavailable`;
                storageSummaryUsedBarEl.style.width = '0%';
                return;
            }
            const usedPercent = Math.max(0, Math.min(100, (usedBytes / safeTotal) * 100));
            storageSummaryTextEl.textContent = `${storageLabel}: ${formatBytes(usedBytes)} used, ${formatBytes(safeFree)} free of ${formatBytes(safeTotal)}`;
            storageSummaryUsedBarEl.style.width = `${usedPercent}%`;
        }

        async function refreshStorageSummary(storageId = currentStorageId, remembered = false) {
            const requestId = ++storageSummaryRequestId;
            const query = remembered
                ? '/api/recovery/storage?remembered=1'
                : `/api/recovery/storage?storage=${encodeURIComponent(storageId)}`;
            const response = await fetch(query);
            if (!response.ok) {
                throw new Error(await response.text() || 'Capacity check failed');
            }
            const payload = await response.json();
            if (requestId !== storageSummaryRequestId) {
                return;
            }
            currentStorageId = payload.current_storage_id || currentStorageId;
            currentStorageLabel = payload.current_storage_label || currentStorageLabel;
            currentRootPath = payload.recovery_target || currentRootPath;
            updateStorageButtons();
            updateStorageSummary(currentStorageLabel, payload.storage_total_bytes, payload.storage_free_bytes);
        }

        function clearListingForError(storageId, message) {
            currentStorageId = storageId || currentStorageId;
            currentStorageLabel = currentStorageId === 'spiffs' ? 'SPIFFS' : 'SD Card';
            currentRootPath = rootPathForStorage(currentStorageId);
            currentPath = '';
            updateStorageButtons();
            pathEl.textContent = `Current folder: ${currentRootPath}`;
            updateStorageSummary(currentStorageLabel, 0, 0);
            filesEl.innerHTML = '';
            const li = document.createElement('li');
            li.textContent = message;
            filesEl.appendChild(li);
        }

        function updateUploadProgress({ summary, fileName, loaded, total, rateBytesPerSecond }) {
            const safeTotal = Number(total) || 0;
            const safeLoaded = Math.max(0, Number(loaded) || 0);
            const percent = safeTotal > 0 ? Math.min(100, Math.round((safeLoaded / safeTotal) * 100)) : 0;
            uploadProgressSummaryEl.textContent = summary;
            uploadProgressFileEl.textContent = fileName;
            uploadProgressPercentEl.textContent = `${percent}%`;
            uploadProgressBarEl.value = percent;
            uploadProgressMetaEl.textContent = `${formatBytes(safeLoaded)} / ${formatBytes(safeTotal)} at ${formatBytes(rateBytesPerSecond || 0)}/s`;
            setUploadProgressVisible(true);
        }

        function uploadSingleFile(file, path, fileIndex, totalFiles) {
            return new Promise((resolve, reject) => {
                const startedAt = performance.now();
                const xhr = new XMLHttpRequest();
                xhr.open('POST', `/api/upload?storage=${encodeURIComponent(currentStorageId)}&path=${encodeURIComponent(path)}`);
                xhr.setRequestHeader('Content-Type', 'application/octet-stream');
                xhr.upload.addEventListener('progress', (event) => {
                    if (!event.lengthComputable) {
                        return;
                    }
                    const elapsedSeconds = Math.max((performance.now() - startedAt) / 1000, 0.001);
                    updateUploadProgress({
                        summary: `Uploading file ${fileIndex} of ${totalFiles}`,
                        fileName: path,
                        loaded: event.loaded,
                        total: event.total,
                        rateBytesPerSecond: event.loaded / elapsedSeconds,
                    });
                });
                xhr.addEventListener('load', () => {
                    if (xhr.status >= 200 && xhr.status < 300) {
                        updateUploadProgress({
                            summary: `Uploaded file ${fileIndex} of ${totalFiles}`,
                            fileName: path,
                            loaded: file.size,
                            total: file.size,
                            rateBytesPerSecond: file.size / Math.max((performance.now() - startedAt) / 1000, 0.001),
                        });
                        resolve();
                        return;
                    }
                    reject(new Error(xhr.responseText || `Upload failed for ${path}`));
                });
                xhr.addEventListener('error', () => {
                    reject(new Error(`Network error while uploading ${path}`));
                });
                xhr.send(file);
            });
        }

                function buildUploadPath(file, basePath = currentPath) {
            const source = file.webkitRelativePath || file.name;
                        return joinRelativePath(basePath, source);
    }

                function buildEntryPath(entry) {
                        return joinRelativePath(currentPath, entry.name);
                }

        function triggerDownload(relativePath) {
            const link = document.createElement('a');
            link.href = `/api/recovery/download?storage=${encodeURIComponent(currentStorageId)}&path=${encodeURIComponent(relativePath)}`;
            link.download = '';
            document.body.appendChild(link);
            link.click();
            link.remove();
        }

        async function deleteEntry(entry) {
            const entryPath = entry.path || buildEntryPath(entry);
            const confirmed = confirm(`Delete ${entry.is_folder ? 'folder' : 'file'} ${currentRootPath}/${entryPath}?`);
            if (!confirmed) {
                return;
            }
            setStatus(`Deleting ${currentRootPath}/${entryPath}...`);
            const response = await fetch(`/api/recovery/delete?storage=${encodeURIComponent(currentStorageId)}&path=${encodeURIComponent(entryPath)}`, {
                method: 'POST'
            });
            if (!response.ok) {
                const text = await response.text();
                throw new Error(text || 'Delete failed');
            }
            setStatus(`Deleted ${currentRootPath}/${entryPath}.`);
            await loadListing(currentPath, currentStorageId);
        }

        function renderListing(payload) {
            currentStorageId = payload.current_storage_id || currentStorageId;
            currentStorageLabel = payload.current_storage_label || currentStorageLabel;
            currentRootPath = payload.recovery_target || currentRootPath;
            currentPath = normalizeRelativePath(payload.current_path || currentPath);
            pathEl.textContent = `Current folder: ${currentPathLabel()}`;
            updateStorageButtons();
            if (Object.prototype.hasOwnProperty.call(payload, 'storage_total_bytes') || Object.prototype.hasOwnProperty.call(payload, 'storage_free_bytes')) {
                updateStorageSummary(currentStorageLabel, payload.storage_total_bytes, payload.storage_free_bytes);
            }
            filesEl.innerHTML = '';
            const listedPath = currentPath;
            if (!payload.files || payload.files.length === 0) {
          const li = document.createElement('li');
                li.textContent = 'No files or folders in this location yet.';
          filesEl.appendChild(li);
            } else {
                payload.files.sort((left, right) => {
                    if (left.is_folder !== right.is_folder) {
                        return left.is_folder ? -1 : 1;
                    }
                    return String(left.name).localeCompare(String(right.name));
                });
                for (const entry of payload.files) {
            const entryPath = joinRelativePath(listedPath, entry.name);
            const li = document.createElement('li');
                        const main = document.createElement('div');
                        main.className = 'entry-main';
                        const button = document.createElement('button');
                        button.className = 'file-entry';
                        button.textContent = `${entry.is_folder ? '📁' : '📄'} ${entry.name}`;
                        if (entry.is_folder) {
                            button.addEventListener('click', () => loadListing(entryPath, currentStorageId));
                        } else {
                            button.disabled = true;
                            button.style.opacity = '0.9';
                            button.style.cursor = 'default';
                        }
                        main.appendChild(button);
                        if (!entry.is_folder) {
                            const meta = document.createElement('span');
                            meta.className = 'meta';
                            meta.textContent = entry.size ? `${entry.size} bytes` : 'File';
                            main.appendChild(meta);
                        }
                        li.appendChild(main);

                        const actions = document.createElement('div');
                        actions.className = 'entry-actions';

                        const downloadButton = document.createElement('button');
                        downloadButton.className = 'icon-button icon-download';
                        downloadButton.type = 'button';
                        downloadButton.title = `Download ${entry.name}`;
                        downloadButton.setAttribute('aria-label', `Download ${entry.name}`);
                        downloadButton.textContent = '⬇';
                        downloadButton.addEventListener('click', (event) => {
                            event.stopPropagation();
                            triggerDownload(entryPath);
                        });
                        actions.appendChild(downloadButton);

                        const deleteButton = document.createElement('button');
                        deleteButton.className = 'icon-button icon-delete';
                        deleteButton.type = 'button';
                        deleteButton.title = `Delete ${entry.name}`;
                        deleteButton.setAttribute('aria-label', `Delete ${entry.name}`);
                        deleteButton.textContent = '🗑';
                        deleteButton.addEventListener('click', async (event) => {
                            event.stopPropagation();
                            try {
                                await deleteEntry({
                                    ...entry,
                                    path: entryPath,
                                });
                            } catch (error) {
                                setStatus(String(error), true);
                            }
                        });
                        actions.appendChild(deleteButton);

                        li.appendChild(actions);
            filesEl.appendChild(li);
                                }
                        }
                        setStatus(`Live source: ${payload.live_source}. Recovery target: ${payload.recovery_target}. Browsing ${currentPathLabel()}.`);
                }

                async function loadListing(path = currentPath, storageId = currentStorageId, remembered = false) {
                        currentPath = normalizeRelativePath(path);
                        if (!remembered) {
                                currentStorageId = storageId || currentStorageId;
                        currentStorageLabel = currentStorageId === 'spiffs' ? 'SPIFFS' : 'SD Card';
                        currentRootPath = rootPathForStorage(currentStorageId);
                        updateStorageButtons();
                        refreshStorageSummary(currentStorageId).catch(() => {
                            updateStorageSummary(currentStorageLabel, 0, 0);
                        });
                        }
                        pathEl.textContent = `Current folder: ${currentPathLabel()}`;
            setStatus('Loading file listing...');
            try {
                                const query = remembered
                                        ? '/api/recovery/list?remembered=1'
                                        : `/api/recovery/list?storage=${encodeURIComponent(currentStorageId)}&path=${encodeURIComponent(currentPath)}`;
                                const response = await fetch(query);
                if (!response.ok) {
                    throw new Error(await response.text() || 'Failed to load listing');
                }
                const payload = await response.json();
                        renderListing(payload);
                        if (remembered) {
                            refreshStorageSummary(currentStorageId).catch(() => {
                        updateStorageSummary(currentStorageLabel, 0, 0);
                            });
                        }
            } catch (error) {
            clearListingForError(currentStorageId, String(error));
        setStatus(`Failed to load listing: ${error}`, true);
      }
    }

        async function uploadFiles(fileList, basePath = currentPath) {
      if (!fileList || fileList.length === 0) {
        return;
      }
            const uploadBasePath = normalizeRelativePath(basePath);
      let uploaded = 0;
      for (const file of fileList) {
                                const path = buildUploadPath(file, uploadBasePath);
                setStatus(`Uploading ${path} (${uploaded + 1}/${fileList.length})...`);
                await uploadSingleFile(file, path, uploaded + 1, fileList.length);
        uploaded += 1;
      }
            setUploadProgressVisible(false);
                        setStatus(`Uploaded ${uploaded} file(s) into ${uploadBasePath ? `${currentRootPath}/${uploadBasePath}` : currentRootPath}.`);
                    await loadListing(uploadBasePath, currentStorageId);
    }

        async function createFolder() {
            const folderName = createFolderNameEl.value.trim();
            if (!folderName) {
                setStatus('Enter a folder name first.', true);
                return;
            }
            const cleanName = normalizeRelativePath(folderName);
            if (!cleanName || cleanName.includes('/')) {
                setStatus('Folder name must be a single path segment.', true);
                return;
            }
            const destinationPath = joinRelativePath(currentPath, cleanName);
            setStatus(`Creating folder ${currentRootPath}/${destinationPath}...`);
            const response = await fetch(`/api/recovery/mkdir?storage=${encodeURIComponent(currentStorageId)}&path=${encodeURIComponent(destinationPath)}`, {
                method: 'POST'
            });
            if (!response.ok) {
                const text = await response.text();
                throw new Error(text || 'Folder creation failed');
            }
            setCreateFolderVisible(false);
            setStatus(`Created folder ${currentRootPath}/${destinationPath}.`);
            await loadListing(currentPath, currentStorageId);
        }

        document.getElementById('upButton').addEventListener('click', () => {
            if (!currentPath) {
                loadListing('', currentStorageId);
                return;
            }
            const segments = currentPath.split('/').filter(Boolean);
            segments.pop();
            loadListing(segments.join('/'), currentStorageId);
        });

        storageSwitchSdEl.addEventListener('click', () => loadListing('', 'sd'));
        storageSwitchSpiffsEl.addEventListener('click', () => loadListing('', 'spiffs'));

        document.getElementById('createFolderButton').addEventListener('click', () => {
            setCreateFolderVisible(!createFolderRowEl.classList.contains('visible'));
        });

        document.getElementById('confirmCreateFolderButton').addEventListener('click', async () => {
            try {
                await createFolder();
            } catch (error) {
                setStatus(String(error), true);
            }
        });

        document.getElementById('cancelCreateFolderButton').addEventListener('click', () => {
            setCreateFolderVisible(false);
        });

        createFolderNameEl.addEventListener('keydown', async (event) => {
            if (event.key === 'Enter') {
                event.preventDefault();
                try {
                    await createFolder();
                } catch (error) {
                    setStatus(String(error), true);
                }
            }
            if (event.key === 'Escape') {
                event.preventDefault();
                setCreateFolderVisible(false);
            }
        });

    document.getElementById('folderPicker').addEventListener('change', async (event) => {
      try {
                const uploadBasePath = currentPath;
                await uploadFiles(event.target.files, uploadBasePath);
      } catch (error) {
        setStatus(String(error), true);
      } finally {
        event.target.value = '';
      }
    });

    document.getElementById('filePicker').addEventListener('change', async (event) => {
      try {
                const uploadBasePath = currentPath;
                await uploadFiles(event.target.files, uploadBasePath);
      } catch (error) {
        setStatus(String(error), true);
      } finally {
        event.target.value = '';
      }
    });

    document.getElementById('refreshButton').addEventListener('click', () => loadListing(currentPath, currentStorageId));
        document.getElementById('openRoot').addEventListener('click', () => loadListing('', currentStorageId));
                refreshStorageSummary(currentStorageId, true).catch((error) => {
                    clearListingForError(currentStorageId, String(error));
                });
                loadListing('', currentStorageId, true);
  </script>
</body>
</html>
)HTML";

bool is_directory_path(const std::string &path)
{
    struct stat st = {};
    if ((stat(path.c_str(), &st) == 0) && S_ISDIR(st.st_mode)) {
        return true;
    }

    const size_t root_length = strlen(kSpiffsRoot);
    if ((path != kSpiffsRoot) && ((path.size() <= root_length) || (path.compare(0, root_length, kSpiffsRoot) != 0) ||
                                  (path[root_length] != '/'))) {
        return false;
    }

    DIR *dir = opendir(path.c_str());
    if (dir == nullptr) {
        return false;
    }
    closedir(dir);
    return true;
}

DIR *open_directory_checked(const std::string &path)
{
    return opendir(path.c_str());
}

bool file_exists(const std::string &path)
{
    struct stat st = {};
    if (stat(path.c_str(), &st) == 0) {
        return true;
    }
    return is_directory_path(path);
}

RecoveryStorageStatsCache &stats_cache_for_storage(const char *storage_id)
{
    return (strcmp(storage_id, kRecoveryStorageSpiffs) == 0) ? s_spiffs_storage_stats_cache : s_sd_storage_stats_cache;
}

void invalidate_recovery_storage_stats(const char *storage_id)
{
    RecoveryStorageStatsCache &cache = stats_cache_for_storage(storage_id);
    cache.valid = false;
    cache.updated_at_us = 0;
}

bool populate_recovery_target_stats(RecoveryTargetInfo &target)
{
    RecoveryStorageStatsCache &cache = stats_cache_for_storage(target.storage_id);
    const int64_t now_us = esp_timer_get_time();
    if (cache.valid && ((now_us - cache.updated_at_us) < kRecoveryStorageCacheTtlUs)) {
        target.total_bytes = cache.total_bytes;
        target.free_bytes = cache.free_bytes;
        return true;
    }

    if (strcmp(target.storage_id, kRecoveryStorageSd) == 0) {
        uint64_t total_bytes = 0;
        uint64_t free_bytes = 0;
        if (esp_vfs_fat_info(kSdCardRoot, &total_bytes, &free_bytes) != ESP_OK) {
            target.total_bytes = 0;
            target.free_bytes = 0;
            cache = {};
            return false;
        }
        target.total_bytes = total_bytes;
        target.free_bytes = free_bytes;
    } else {
        size_t total_bytes = 0;
        size_t used_bytes = 0;
        if (esp_spiffs_info(CONFIG_BSP_SPIFFS_PARTITION_LABEL, &total_bytes, &used_bytes) != ESP_OK) {
            target.total_bytes = 0;
            target.free_bytes = 0;
            cache = {};
            return false;
        }
        target.total_bytes = static_cast<uint64_t>(total_bytes);
        target.free_bytes = (total_bytes >= used_bytes) ? static_cast<uint64_t>(total_bytes - used_bytes) : 0;
    }

    cache.total_bytes = target.total_bytes;
    cache.free_bytes = target.free_bytes;
    cache.updated_at_us = now_us;
    cache.valid = true;
    return true;
}

char *allocate_io_buffer(size_t size = kIoBufferSize)
{
    void *buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == nullptr) {
        buffer = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return static_cast<char *>(buffer);
}

bool sanitize_relative_path_allow_empty(std::string &path);

bool is_supported_recovery_storage_id(const std::string &storage_id)
{
    return storage_id.empty() || (storage_id == kRecoveryStorageSd) || (storage_id == kRecoveryStorageSpiffs);
}

std::string normalized_recovery_storage_id(const std::string &storage_id)
{
    return (storage_id == kRecoveryStorageSpiffs) ? std::string(kRecoveryStorageSpiffs) : std::string(kRecoveryStorageSd);
}

bool resolve_recovery_target(const std::string &requested_storage_id, bool allow_mount, bool include_stats, RecoveryTargetInfo &target, std::string &error_message)
{
    const std::string storage_id = normalized_recovery_storage_id(requested_storage_id);
    if (storage_id == kRecoveryStorageSd) {
        const bool available = allow_mount ? app_storage_ensure_sdcard_available() : app_storage_is_sdcard_mounted();
        if (!available) {
            error_message = "Insert the SD card before browsing recovery files";
            return false;
        }
        target = {
            .storage_id = kRecoveryStorageSd,
            .storage_label = "SD Card",
            .root_path = kSdCardRoot,
            .total_bytes = 0,
            .free_bytes = 0,
        };
        if (include_stats) {
            populate_recovery_target_stats(target);
        }
        return true;
    }

    size_t spiffs_total_bytes = 0;
    size_t spiffs_used_bytes = 0;
    if (esp_spiffs_info(CONFIG_BSP_SPIFFS_PARTITION_LABEL, &spiffs_total_bytes, &spiffs_used_bytes) != ESP_OK) {
        error_message = "SPIFFS is unavailable";
        return false;
    }

    target = {
        .storage_id = kRecoveryStorageSpiffs,
        .storage_label = "SPIFFS",
        .root_path = kSpiffsRoot,
        .total_bytes = 0,
        .free_bytes = 0,
    };
    if (include_stats) {
        populate_recovery_target_stats(target);
    }
    return true;
}

bool is_spiffs_storage_path(const std::string &path)
{
    if (path == kSpiffsRoot) {
        return true;
    }

    const size_t root_length = strlen(kSpiffsRoot);
    return (path.size() > root_length) && (path.compare(0, root_length, kSpiffsRoot) == 0) && (path[root_length] == '/');
}

bool is_spiffs_directory_marker_name(const char *name)
{
    return (name != nullptr) && (strcmp(name, kSpiffsDirectoryMarker) == 0);
}

bool is_spiffs_internal_support_name(const char *name)
{
    return is_spiffs_directory_marker_name(name) || ((name != nullptr) && (strcmp(name, ".jc4880_dirs.idx") == 0));
}

std::string spiffs_directory_marker_path(const std::string &path)
{
    return path.empty() ? std::string(kSpiffsDirectoryMarker) : (path + "/" + kSpiffsDirectoryMarker);
}

bool ensure_spiffs_directory_marker(const std::string &path)
{
    if (!is_spiffs_storage_path(path)) {
        return false;
    }
    if (path == kSpiffsRoot) {
        return true;
    }

    FILE *marker = fopen(spiffs_directory_marker_path(path).c_str(), "ab");
    if (marker == nullptr) {
        return false;
    }
    fclose(marker);
    return true;
}

bool read_spiffs_directory_index(std::vector<std::string> &paths)
{
    paths.clear();
    FILE *file = fopen(kSpiffsDirectoryIndexFile, "rb");
    if (file == nullptr) {
        return true;
    }

    char line[256] = {};
    while (fgets(line, sizeof(line), file) != nullptr) {
        std::string value = line;
        while (!value.empty() && ((value.back() == '\n') || (value.back() == '\r'))) {
            value.pop_back();
        }
        if (!value.empty() && sanitize_relative_path_allow_empty(value)) {
            paths.push_back(value);
        }
    }

    fclose(file);
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return true;
}

bool write_spiffs_directory_index(const std::vector<std::string> &paths)
{
    FILE *file = fopen(kSpiffsDirectoryIndexFile, "wb");
    if (file == nullptr) {
        return false;
    }

    for (const std::string &path : paths) {
        if (!path.empty()) {
            if ((fwrite(path.c_str(), 1, path.size(), file) != path.size()) || (fwrite("\n", 1, 1, file) != 1)) {
                fclose(file);
                return false;
            }
        }
    }

    fclose(file);
    return true;
}

bool register_spiffs_directory_path(const std::string &relative_path)
{
    std::string normalized = relative_path;
    if (!sanitize_relative_path_allow_empty(normalized)) {
        return false;
    }
    if (normalized.empty()) {
        return true;
    }

    std::vector<std::string> paths;
    if (!read_spiffs_directory_index(paths)) {
        return false;
    }

    std::string current = normalized;
    while (!current.empty()) {
        paths.push_back(current);
        const size_t slash = current.find_last_of('/');
        if (slash == std::string::npos) {
            break;
        }
        current.erase(slash);
    }

    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return write_spiffs_directory_index(paths);
}

bool unregister_spiffs_directory_path(const std::string &relative_path)
{
    std::string normalized = relative_path;
    if (!sanitize_relative_path_allow_empty(normalized)) {
        return false;
    }
    if (normalized.empty()) {
        return true;
    }

    std::vector<std::string> paths;
    if (!read_spiffs_directory_index(paths)) {
        return false;
    }

    paths.erase(std::remove_if(paths.begin(), paths.end(), [&normalized](const std::string &candidate) {
        return (candidate == normalized) || ((candidate.size() > normalized.size()) && (candidate.compare(0, normalized.size(), normalized) == 0) && (candidate[normalized.size()] == '/'));
    }), paths.end());

    return write_spiffs_directory_index(paths);
}

void append_spiffs_index_children(const std::string &relative_path, std::vector<std::string> &children)
{
    std::vector<std::string> paths;
    if (!read_spiffs_directory_index(paths)) {
        return;
    }

    for (const std::string &candidate : paths) {
        std::string parent;
        std::string name = candidate;
        const size_t slash = candidate.find_last_of('/');
        if (slash != std::string::npos) {
            parent = candidate.substr(0, slash);
            name = candidate.substr(slash + 1);
        }
        if (parent == relative_path) {
            children.push_back(name);
        }
    }

    std::sort(children.begin(), children.end());
    children.erase(std::unique(children.begin(), children.end()), children.end());
}

bool ensure_directory_recursive(const std::string &path)
{
    if (path.empty()) {
        return false;
    }

    if (is_directory_path(path)) {
        return true;
    }

    const bool spiffs_path = is_spiffs_storage_path(path);
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

        if (spiffs_path) {
            if ((partial != kSpiffsRoot) && !ensure_spiffs_directory_marker(partial)) {
                return false;
            }
            continue;
        }

        if (!is_directory_path(partial) && (mkdir(partial.c_str(), 0775) != 0) && (errno != EEXIST)) {
            return false;
        }
    }

    if (spiffs_path) {
        return ensure_spiffs_directory_marker(path);
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
    std::replace(path.begin(), path.end(), '\\', '/');
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

bool sanitize_relative_path_allow_empty(std::string &path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && (path.front() == '/')) {
        path.erase(path.begin());
    }
    while (!path.empty() && (path.back() == '/')) {
        path.pop_back();
    }
    while (path.find("//") != std::string::npos) {
        path.replace(path.find("//"), 2, "/");
    }
    if (path.find("..") != std::string::npos) {
        return false;
    }
    return true;
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

    std::string decoded = value;
    std::string output;
    output.reserve(decoded.size());
    for (size_t index = 0; index < decoded.size(); ++index) {
        const char ch = decoded[index];
        if ((ch == '%') && ((index + 2) < decoded.size())) {
            const char hex[3] = {decoded[index + 1], decoded[index + 2], '\0'};
            char *end = nullptr;
            const long parsed = strtol(hex, &end, 16);
            if ((end != nullptr) && (*end == '\0')) {
                output.push_back(static_cast<char>(parsed));
                index += 2;
                continue;
            }
        }
        if (ch == '+') {
            output.push_back(' ');
            continue;
        }
        output.push_back(ch);
    }
    return output;
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

std::string base_name(const std::string &path)
{
    const size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

bool delete_path_recursive(const std::string &path)
{
    struct stat st = {};
    const bool stat_ok = (stat(path.c_str(), &st) == 0);
    const bool is_dir = (stat_ok && S_ISDIR(st.st_mode)) || is_directory_path(path);
    if (!is_dir) {
        if (!stat_ok) {
            return false;
        }
        return remove(path.c_str()) == 0;
    }

    DIR *dir = opendir(path.c_str());
    if (dir == nullptr) {
        return false;
    }

    bool success = true;
    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        if (is_spiffs_internal_support_name(entry->d_name)) {
            continue;
        }
        if (!delete_path_recursive(join_path(path, entry->d_name))) {
            success = false;
            break;
        }
    }
    closedir(dir);

    if (!success) {
        return false;
    }

    if (is_spiffs_storage_path(path)) {
        const std::string marker_path = spiffs_directory_marker_path(path);
        if (remove(marker_path.c_str()) != 0) {
            struct stat marker_stat = {};
            if (stat(marker_path.c_str(), &marker_stat) == 0) {
                return false;
            }
        }

        if (stat_ok && S_ISDIR(st.st_mode)) {
            return (rmdir(path.c_str()) == 0) || (errno == ENOENT);
        }
        return true;
    }

    return rmdir(path.c_str()) == 0;
}

void write_tar_octal(char *field, size_t field_size, unsigned long long value)
{
    if (field_size == 0) {
        return;
    }

    memset(field, '0', field_size);
    char buffer[32] = {};
    snprintf(buffer, sizeof(buffer), "%llo", value);
    const size_t digits = strnlen(buffer, sizeof(buffer));
    const size_t copy_size = (digits < (field_size - 1)) ? digits : (field_size - 1);
    memcpy(field + (field_size - 1 - copy_size), buffer, copy_size);
    field[field_size - 1] = ' ';
}

bool split_tar_path(const std::string &path, char *name_field, size_t name_size, char *prefix_field, size_t prefix_size)
{
    memset(name_field, 0, name_size);
    memset(prefix_field, 0, prefix_size);
    if (path.size() <= (name_size - 1)) {
        memcpy(name_field, path.c_str(), path.size());
        return true;
    }

    const size_t split = path.rfind('/');
    if ((split == std::string::npos) || (split > (prefix_size - 1)) || ((path.size() - split - 1) > (name_size - 1))) {
        return false;
    }

    memcpy(prefix_field, path.c_str(), split);
    memcpy(name_field, path.c_str() + split + 1, path.size() - split - 1);
    return true;
}

esp_err_t send_file_with_headers(httpd_req_t *req, const std::string &path, const char *content_disposition)
{
    FILE *file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
    }

    httpd_resp_set_type(req, content_type_for_path(path));
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Content-Disposition", content_disposition);

    char *buffer = allocate_io_buffer();
    if (buffer == nullptr) {
        fclose(file);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate transfer buffer");
    }
    size_t bytes_read = 0;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        const esp_err_t err = httpd_resp_send_chunk(req, buffer, bytes_read);
        if (err != ESP_OK) {
            free(buffer);
            fclose(file);
            return err;
        }
    }

    free(buffer);
    fclose(file);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t send_tar_path(httpd_req_t *req, const std::string &archive_path, const std::string &absolute_path)
{
    struct stat st = {};
    const bool stat_ok = (stat(absolute_path.c_str(), &st) == 0);
    const bool is_dir = (stat_ok && S_ISDIR(st.st_mode)) || is_directory_path(absolute_path);
    if (!stat_ok && !is_dir) {
        return ESP_FAIL;
    }

    char header[512] = {};
    std::string tar_name = archive_path;
    if (is_dir && (tar_name.empty() || (tar_name.back() != '/'))) {
        tar_name.push_back('/');
    }
    if (!split_tar_path(tar_name, header, 100, header + 345, 155)) {
        return ESP_FAIL;
    }

    write_tar_octal(header + 100, 8, is_dir ? 0755 : 0644);
    write_tar_octal(header + 108, 8, 0);
    write_tar_octal(header + 116, 8, 0);
    write_tar_octal(header + 124, 12, is_dir ? 0 : static_cast<unsigned long long>(st.st_size));
    write_tar_octal(header + 136, 12, static_cast<unsigned long long>(stat_ok ? st.st_mtime : 0));
    memset(header + 148, ' ', 8);
    header[156] = is_dir ? '5' : '0';
    memcpy(header + 257, "ustar", 5);
    memcpy(header + 263, "00", 2);

    unsigned int checksum = 0;
    for (unsigned char byte : header) {
        checksum += byte;
    }
    write_tar_octal(header + 148, 8, checksum);

    esp_err_t err = httpd_resp_send_chunk(req, header, sizeof(header));
    if (err != ESP_OK) {
        return err;
    }

    if (is_dir) {
        DIR *dir = opendir(absolute_path.c_str());
        if (dir == nullptr) {
            return ESP_FAIL;
        }
        struct dirent *entry = nullptr;
        while ((entry = readdir(dir)) != nullptr) {
            if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
                continue;
            }
            if (is_spiffs_internal_support_name(entry->d_name)) {
                continue;
            }
            err = send_tar_path(req, join_path(archive_path, entry->d_name), join_path(absolute_path, entry->d_name));
            if (err != ESP_OK) {
                closedir(dir);
                return err;
            }
        }
        closedir(dir);
        return ESP_OK;
    }

    FILE *file = fopen(absolute_path.c_str(), "rb");
    if (file == nullptr) {
        return ESP_FAIL;
    }

    char *buffer = allocate_io_buffer();
    if (buffer == nullptr) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    size_t bytes_read = 0;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        err = httpd_resp_send_chunk(req, buffer, bytes_read);
        if (err != ESP_OK) {
            free(buffer);
            fclose(file);
            return err;
        }
    }
    free(buffer);
    fclose(file);

    const size_t padding = (512 - (static_cast<size_t>(st.st_size) % 512)) % 512;
    if (padding > 0) {
        char zeros[512] = {};
        err = httpd_resp_send_chunk(req, zeros, padding);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
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

    char *buffer = allocate_io_buffer();
    if (buffer == nullptr) {
        fclose(file);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate transfer buffer");
    }
    size_t bytes_read = 0;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        const esp_err_t err = httpd_resp_send_chunk(req, buffer, bytes_read);
        if (err != ESP_OK) {
            free(buffer);
            fclose(file);
            return err;
        }
    }

    free(buffer);
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
        if (is_spiffs_internal_support_name(entry->d_name)) {
            continue;
        }

        const std::string child_relative = relative.empty() ? entry->d_name : (relative + "/" + entry->d_name);
        const std::string child_path = join_path(root_path, child_relative);
        if (is_directory_path(child_path)) {
            append_file_entries(root_label, root_path, json_entries, child_relative);
            continue;
        }

        struct stat st = {};
        if (stat(child_path.c_str(), &st) != 0) {
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

esp_err_t handle_recovery_browse_json(httpd_req_t *req)
{
    WebServerService *service = service_from_handle(req);
    if (service == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server unavailable");
    }

    const bool use_remembered = (read_query_value(req, "remembered") == "1");
    std::string requested_storage = use_remembered ? service->recoveryStorageId() : read_query_value(req, "storage");
    if (!is_supported_recovery_storage_id(requested_storage)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid recovery storage");
    }

    std::string relative_path = use_remembered ? service->recoveryPath() : read_query_value(req, "path");
    if (!sanitize_relative_path_allow_empty(relative_path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid browse path");
    }

    RecoveryTargetInfo target = {};
    std::string error_message;
    if (!resolve_recovery_target(requested_storage.empty() ? service->recoveryStorageId() : requested_storage, true, false, target, error_message)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_string(req, "text/plain; charset=utf-8", error_message.c_str());
    }

    const std::string absolute_path = relative_path.empty() ? target.root_path : join_path(target.root_path, relative_path);

    service->rememberRecoveryLocation(target.storage_id, relative_path);

    struct BrowseEntry {
        std::string name;
        bool is_folder;
        long long size;
    };

    std::vector<BrowseEntry> entries;
    DIR *dir = open_directory_checked(absolute_path);
    if (dir == nullptr) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Folder not found");
    }

    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        if (is_spiffs_internal_support_name(entry->d_name)) {
            continue;
        }
        const std::string name = entry->d_name;
        const std::string child_path = join_path(absolute_path, name);
        const bool is_folder = is_directory_path(child_path);
        struct stat st = {};
        if (!is_folder && (stat(child_path.c_str(), &st) != 0)) {
            continue;
        }
        entries.push_back({name, is_folder, is_folder ? 0LL : static_cast<long long>(st.st_size)});
    }
    closedir(dir);

    if (strcmp(target.storage_id, kRecoveryStorageSpiffs) == 0) {
        std::vector<std::string> indexed_children;
        append_spiffs_index_children(relative_path, indexed_children);
        for (const std::string &name : indexed_children) {
            const auto already_present = std::find_if(entries.begin(), entries.end(), [&name](const BrowseEntry &entry) {
                return entry.name == name;
            });
            if (already_present == entries.end()) {
                entries.push_back({name, true, 0LL});
            }
        }
    }

    std::sort(entries.begin(), entries.end(), [](const BrowseEntry &left, const BrowseEntry &right) {
        if (left.is_folder != right.is_folder) {
            return left.is_folder > right.is_folder;
        }
        return left.name < right.name;
    });

    PsramString files_json = "[";
    for (size_t index = 0; index < entries.size(); ++index) {
        if (index > 0) {
            files_json += ",";
        }
        files_json += "{";
        files_json += "\"name\":\"";
        files_json += json_escape(entries[index].name);
        files_json += "\",";
        files_json += "\"is_folder\":";
        files_json += bool_json(entries[index].is_folder);
        files_json += ",\"size\":";
        files_json += std::to_string(entries[index].size).c_str();
        files_json += "}";
    }
    files_json += "]";

    const PsramString body = PsramString("{") +
                             "\"live_source\":\"" + json_escape(service->sourceSummary()) + "\"" +
                             ",\"recovery_target\":\"" + json_escape(target.root_path) + "\"" +
                             ",\"current_storage_id\":\"" + json_escape(target.storage_id) + "\"" +
                             ",\"current_storage_label\":\"" + json_escape(target.storage_label) + "\"" +
                             ",\"current_path\":\"" + json_escape(relative_path) + "\"" +
                             ",\"files\":" + files_json + "}";
    return send_text(req, "application/json", body);
}

esp_err_t handle_recovery_storage_json(httpd_req_t *req)
{
    WebServerService *service = service_from_handle(req);
    if (service == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server unavailable");
    }

    const bool use_remembered = (read_query_value(req, "remembered") == "1");
    std::string requested_storage = use_remembered ? service->recoveryStorageId() : read_query_value(req, "storage");
    if (!is_supported_recovery_storage_id(requested_storage)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid recovery storage");
    }

    RecoveryTargetInfo target = {};
    std::string error_message;
    if (!resolve_recovery_target(requested_storage.empty() ? service->recoveryStorageId() : requested_storage, true, true, target, error_message)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_string(req, "text/plain; charset=utf-8", error_message.c_str());
    }

    const PsramString body = PsramString("{") +
                             "\"current_storage_id\":\"" + json_escape(target.storage_id) + "\"" +
                             ",\"current_storage_label\":\"" + json_escape(target.storage_label) + "\"" +
                             ",\"recovery_target\":\"" + json_escape(target.root_path) + "\"" +
                             ",\"storage_total_bytes\":" + std::to_string(target.total_bytes).c_str() +
                             ",\"storage_free_bytes\":" + std::to_string(target.free_bytes).c_str() + "}";
    return send_text(req, "application/json", body);
}

esp_err_t handle_recovery_mkdir(httpd_req_t *req)
{
    std::string requested_storage = read_query_value(req, "storage");
    if (!is_supported_recovery_storage_id(requested_storage)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid recovery storage");
    }

    RecoveryTargetInfo target = {};
    std::string error_message;
    if (!resolve_recovery_target(requested_storage, true, false, target, error_message)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_string(req, "text/plain; charset=utf-8", error_message.c_str());
    }

    std::string relative_path = read_query_value(req, "path");
    if (!sanitize_relative_path(relative_path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid folder path");
    }

    const std::string destination = join_path(target.root_path, relative_path);
    if (!ensure_directory_recursive(destination)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create folder");
    }
    if ((strcmp(target.storage_id, kRecoveryStorageSpiffs) == 0) && !register_spiffs_directory_path(relative_path)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to register SPIFFS folder");
    }

    invalidate_recovery_storage_stats(target.storage_id);

    return send_text(req, "application/json", PsramString("{\"created\":\"") + json_escape(relative_path) + "\"}");
}

esp_err_t handle_recovery_delete(httpd_req_t *req)
{
    std::string requested_storage = read_query_value(req, "storage");
    if (!is_supported_recovery_storage_id(requested_storage)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid recovery storage");
    }

    RecoveryTargetInfo target = {};
    std::string error_message;
    if (!resolve_recovery_target(requested_storage, true, false, target, error_message)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_string(req, "text/plain; charset=utf-8", error_message.c_str());
    }

    std::string relative_path = read_query_value(req, "path");
    if (!sanitize_relative_path(relative_path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid delete path");
    }

    const std::string destination = join_path(target.root_path, relative_path);
    const bool is_directory = is_directory_path(destination);
    if (!file_exists(destination)) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Path not found");
    }
    if (!delete_path_recursive(destination)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete path");
    }
    if (is_directory && (strcmp(target.storage_id, kRecoveryStorageSpiffs) == 0) && !unregister_spiffs_directory_path(relative_path)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to update SPIFFS folder index");
    }

    invalidate_recovery_storage_stats(target.storage_id);

    return send_text(req, "application/json", PsramString("{\"deleted\":\"") + json_escape(relative_path) + "\"}");
}

esp_err_t handle_recovery_download(httpd_req_t *req)
{
    std::string requested_storage = read_query_value(req, "storage");
    if (!is_supported_recovery_storage_id(requested_storage)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid recovery storage");
    }

    RecoveryTargetInfo target = {};
    std::string error_message;
    if (!resolve_recovery_target(requested_storage, true, false, target, error_message)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_string(req, "text/plain; charset=utf-8", error_message.c_str());
    }

    std::string relative_path = read_query_value(req, "path");
    if (!sanitize_relative_path(relative_path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid download path");
    }

    const std::string absolute_path = join_path(target.root_path, relative_path);
    struct stat st = {};
    const bool stat_ok = (stat(absolute_path.c_str(), &st) == 0);
    const bool is_dir = (stat_ok && S_ISDIR(st.st_mode)) || is_directory_path(absolute_path);
    if (!stat_ok && !is_dir) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Path not found");
    }

    if (!is_dir) {
        char disposition[192] = {};
        snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", base_name(relative_path).c_str());
        return send_file_with_headers(req, absolute_path, disposition);
    }

    const std::string folder_name = base_name(relative_path);
    char disposition[192] = {};
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s.tar\"", folder_name.empty() ? "folder" : folder_name.c_str());
    httpd_resp_set_type(req, "application/x-tar");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);

    esp_err_t err = send_tar_path(req, relative_path, absolute_path);
    if (err != ESP_OK) {
        return err;
    }
    char zeros[1024] = {};
    err = httpd_resp_send_chunk(req, zeros, sizeof(zeros));
    if (err != ESP_OK) {
        return err;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t handle_upload(httpd_req_t *req)
{
    std::string requested_storage = read_query_value(req, "storage");
    if (!is_supported_recovery_storage_id(requested_storage)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid recovery storage");
    }

    RecoveryTargetInfo target = {};
    std::string error_message;
    if (!resolve_recovery_target(requested_storage, true, false, target, error_message)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_string(req, "text/plain; charset=utf-8", error_message.c_str());
    }

    std::string relative_path = read_query_value(req, "path");
    if (!sanitize_relative_path(relative_path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid upload path");
    }

    const size_t slash = relative_path.find_last_of('/');
    if (slash != std::string::npos) {
        const std::string directory = join_path(target.root_path, relative_path.substr(0, slash));
        if (!ensure_directory_recursive(directory)) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create nested upload directory");
        }
        if ((strcmp(target.storage_id, kRecoveryStorageSpiffs) == 0) && !register_spiffs_directory_path(relative_path.substr(0, slash))) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to register SPIFFS upload directory");
        }
    }

    const std::string destination = join_path(target.root_path, relative_path);
    FILE *file = fopen(destination.c_str(), "wb");
    if (file == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open destination file");
    }

    int remaining = req->content_len;
    char *buffer = allocate_io_buffer();
    if (buffer == nullptr) {
        fclose(file);
        remove(destination.c_str());
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate upload buffer");
    }
    while (remaining > 0) {
        const int read_size = httpd_req_recv(req, buffer, (remaining > static_cast<int>(kIoBufferSize)) ? static_cast<int>(kIoBufferSize) : remaining);
        if (read_size <= 0) {
            free(buffer);
            fclose(file);
            remove(destination.c_str());
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload stream failed");
        }
        if (fwrite(buffer, 1, read_size, file) != static_cast<size_t>(read_size)) {
            free(buffer);
            fclose(file);
            remove(destination.c_str());
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write uploaded file");
        }
        remaining -= read_size;
    }

    free(buffer);
    fclose(file);
    invalidate_recovery_storage_stats(target.storage_id);
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
    _recoveryStorageId(kRecoveryStorageSd),
    _recoveryPath(),
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

std::string WebServerService::recoveryStorageId() const
{
    return _recoveryStorageId.empty() ? std::string(kRecoveryStorageSd) : _recoveryStorageId;
}

std::string WebServerService::recoveryPath() const
{
    return _recoveryPath;
}

void WebServerService::rememberRecoveryLocation(const std::string &storageId, const std::string &relativePath)
{
    _recoveryStorageId = normalized_recovery_storage_id(storageId);
    _recoveryPath = relativePath;
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
    httpd_uri_t recovery_browse_uri = {
        .uri = "/api/recovery/list",
        .method = HTTP_GET,
        .handler = handle_recovery_browse_json,
        .user_ctx = this,
    };
    httpd_uri_t recovery_storage_uri = {
        .uri = "/api/recovery/storage",
        .method = HTTP_GET,
        .handler = handle_recovery_storage_json,
        .user_ctx = this,
    };
    httpd_uri_t recovery_mkdir_uri = {
        .uri = "/api/recovery/mkdir",
        .method = HTTP_POST,
        .handler = handle_recovery_mkdir,
        .user_ctx = this,
    };
    httpd_uri_t recovery_delete_uri = {
        .uri = "/api/recovery/delete",
        .method = HTTP_DELETE,
        .handler = handle_recovery_delete,
        .user_ctx = this,
    };
    httpd_uri_t recovery_delete_post_uri = {
        .uri = "/api/recovery/delete",
        .method = HTTP_POST,
        .handler = handle_recovery_delete,
        .user_ctx = this,
    };
    httpd_uri_t recovery_download_uri = {
        .uri = "/api/recovery/download",
        .method = HTTP_GET,
        .handler = handle_recovery_download,
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

    const httpd_uri_t handlers[] = {status_uri, files_uri, recovery_browse_uri, recovery_storage_uri, recovery_mkdir_uri, recovery_delete_uri, recovery_delete_post_uri, recovery_download_uri, upload_uri, static_uri};
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