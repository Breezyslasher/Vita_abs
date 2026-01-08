/**
 * VitaABS - Downloads Manager Implementation
 * Handles offline media downloads and progress sync
 */

#include "app/downloads_manager.hpp"
#include "app/audiobookshelf_client.hpp"
#include "app/application.hpp"
#include "utils/http_client.hpp"
#include <borealis.hpp>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cstdlib>
#include <thread>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/kernel/threadmgr.h>
#endif

namespace vitaabs {

// Downloads directory on Vita
#ifdef __vita__
static const char* DOWNLOADS_DIR = "ux0:data/VitaABS/downloads";
static const char* STATE_FILE = "ux0:data/VitaABS/downloads/state.json";
#else
static const char* DOWNLOADS_DIR = "./downloads";
static const char* STATE_FILE = "./downloads/state.json";
#endif

DownloadsManager& DownloadsManager::getInstance() {
    static DownloadsManager instance;
    return instance;
}

bool DownloadsManager::init() {
    if (m_initialized) return true;

    m_downloadsPath = DOWNLOADS_DIR;

#ifdef __vita__
    // Create downloads directory if it doesn't exist
    sceIoMkdir("ux0:data/VitaABS", 0777);
    sceIoMkdir(DOWNLOADS_DIR, 0777);
#else
    // Create directory on other platforms
    std::system("mkdir -p ./downloads");
#endif

    // Load saved state
    loadState();

    m_initialized = true;
    brls::Logger::info("DownloadsManager: Initialized at {}", m_downloadsPath);
    return true;
}

bool DownloadsManager::queueDownload(const std::string& itemId, const std::string& title,
                                      const std::string& authorName, float duration,
                                      const std::string& mediaType,
                                      const std::string& seriesName,
                                      const std::string& episodeId) {
    brls::Logger::info("DownloadsManager::queueDownload called:");
    brls::Logger::info("  - itemId: {}", itemId);
    brls::Logger::info("  - title: {}", title);
    brls::Logger::info("  - mediaType: {}", mediaType);
    brls::Logger::info("  - episodeId: {}", episodeId.empty() ? "(none)" : episodeId);

    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if already in queue
    for (const auto& item : m_downloads) {
        if (item.itemId == itemId) {
            brls::Logger::warning("DownloadsManager: {} already in queue", title);
            return false;
        }
    }

    DownloadItem item;
    item.itemId = itemId;
    item.title = title;
    item.authorName = authorName;
    item.parentTitle = seriesName.empty() ? authorName : seriesName;
    item.duration = duration;
    item.mediaType = mediaType;
    item.seriesName = seriesName;
    item.episodeId = episodeId;
    item.state = DownloadState::QUEUED;

    // Get cover URL from client
    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
    item.coverUrl = client.getCoverUrl(itemId);

    // Generate local path - audiobooks are typically m4b, mp3, or other audio formats
    std::string extension;
    if (mediaType == "podcast" || !episodeId.empty()) {
        extension = ".mp3";
    } else {
        // For audiobooks, use m4b (common audiobook format) or mp3
        extension = ".m4b";
    }
    std::string filename = itemId + extension;
    item.localPath = m_downloadsPath + "/" + filename;

    brls::Logger::info("DownloadsManager: Local path: {}", item.localPath);

    m_downloads.push_back(item);
    saveState();

    brls::Logger::info("DownloadsManager: Successfully queued {} for download (total in queue: {})",
                       title, m_downloads.size());
    return true;
}

void DownloadsManager::startDownloads() {
    if (m_downloading) return;
    m_downloading = true;

    brls::Logger::info("DownloadsManager: Starting download queue");

    // Process downloads in background
    std::thread([this]() {
        brls::Logger::info("DownloadsManager: Download thread started");

        while (m_downloading) {
            DownloadItem* nextItem = nullptr;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto& item : m_downloads) {
                    if (item.state == DownloadState::QUEUED) {
                        item.state = DownloadState::DOWNLOADING;
                        nextItem = &item;
                        brls::Logger::info("DownloadsManager: Found queued item: {}", item.title);
                        break;
                    }
                }
            }

            if (nextItem) {
                brls::Logger::info("DownloadsManager: Starting download of {}", nextItem->title);
                downloadItem(*nextItem);
            } else {
                // No more queued items
                brls::Logger::info("DownloadsManager: No more queued items");
                break;
            }
        }
        m_downloading = false;
        brls::Logger::info("DownloadsManager: Download thread finished");
    }).detach();
}

void DownloadsManager::pauseDownloads() {
    m_downloading = false;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& item : m_downloads) {
        if (item.state == DownloadState::DOWNLOADING) {
            item.state = DownloadState::PAUSED;
        }
    }
    saveState();
}

bool DownloadsManager::cancelDownload(const std::string& itemId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto it = m_downloads.begin(); it != m_downloads.end(); ++it) {
        if (it->itemId == itemId) {
            // Delete partial file if exists
            if (!it->localPath.empty()) {
#ifdef __vita__
                sceIoRemove(it->localPath.c_str());
#else
                std::remove(it->localPath.c_str());
#endif
            }
            m_downloads.erase(it);
            saveState();
            return true;
        }
    }
    return false;
}

bool DownloadsManager::deleteDownload(const std::string& itemId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto it = m_downloads.begin(); it != m_downloads.end(); ++it) {
        if (it->itemId == itemId) {
            // Delete file
            if (!it->localPath.empty()) {
#ifdef __vita__
                sceIoRemove(it->localPath.c_str());
#else
                std::remove(it->localPath.c_str());
#endif
            }
            m_downloads.erase(it);
            saveState();
            brls::Logger::info("DownloadsManager: Deleted download {}", itemId);
            return true;
        }
    }
    return false;
}

std::vector<DownloadItem> DownloadsManager::getDownloads() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_downloads;
}

DownloadItem* DownloadsManager::getDownload(const std::string& itemId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& item : m_downloads) {
        if (item.itemId == itemId) {
            return &item;
        }
    }
    return nullptr;
}

bool DownloadsManager::isDownloaded(const std::string& itemId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& item : m_downloads) {
        if (item.itemId == itemId && item.state == DownloadState::COMPLETED) {
            return true;
        }
    }
    return false;
}

std::string DownloadsManager::getLocalPath(const std::string& itemId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& item : m_downloads) {
        if (item.itemId == itemId && item.state == DownloadState::COMPLETED) {
            return item.localPath;
        }
    }
    return "";
}

void DownloadsManager::updateProgress(const std::string& itemId, float currentTime) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& item : m_downloads) {
        if (item.itemId == itemId) {
            item.currentTime = currentTime;
            item.viewOffset = static_cast<int64_t>(currentTime * 1000.0f);  // Convert to milliseconds
            brls::Logger::debug("DownloadsManager: Updated progress for {} to {}s",
                               item.title, currentTime);
            break;
        }
    }
    // Don't save on every update - too frequent
}

void DownloadsManager::syncProgressToServer() {
    std::vector<DownloadItem> itemsToSync;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& item : m_downloads) {
            if (item.state == DownloadState::COMPLETED && item.currentTime > 0) {
                itemsToSync.push_back(item);
            }
        }
    }

    brls::Logger::info("DownloadsManager: Syncing {} items to server", itemsToSync.size());

    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    for (auto& item : itemsToSync) {
        // Update progress on the Audiobookshelf server
        // Signature: updateProgress(itemId, currentTime, duration, isFinished, episodeId)
        bool isFinished = (item.duration > 0 && item.currentTime >= item.duration * 0.95f);
        if (client.updateProgress(item.itemId, item.currentTime, item.duration, isFinished, item.episodeId)) {
            std::lock_guard<std::mutex> lock(m_mutex);
            // Update last synced time
            for (auto& d : m_downloads) {
                if (d.itemId == item.itemId) {
                    d.lastSynced = std::time(nullptr);
                    break;
                }
            }
            brls::Logger::debug("DownloadsManager: Synced progress for {}", item.title);
        }
    }

    saveState();
}

void DownloadsManager::downloadItem(DownloadItem& item) {
    brls::Logger::info("DownloadsManager: Starting download of {}", item.title);
    brls::Logger::info("DownloadsManager: Item ID: {}, Episode ID: {}, Type: {}",
                       item.itemId, item.episodeId.empty() ? "(none)" : item.episodeId, item.mediaType);

    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
    std::string serverUrl = client.getServerUrl();
    std::string token = client.getAuthToken();

    brls::Logger::debug("DownloadsManager: Server URL: {}", serverUrl);
    brls::Logger::debug("DownloadsManager: Auth token present: {}", !token.empty() ? "yes" : "no");

    if (serverUrl.empty() || token.empty()) {
        brls::Logger::error("DownloadsManager: Not connected to server");
        item.state = DownloadState::FAILED;
        saveState();
        return;
    }

    // Check if this is a multi-file audiobook
    std::vector<AudioFileInfo> audioFiles;
    if (item.episodeId.empty() && item.mediaType == "book") {
        brls::Logger::info("DownloadsManager: Checking for multi-file audiobook...");
        client.getAudioFiles(item.itemId, audioFiles);
        brls::Logger::info("DownloadsManager: Found {} audio files", audioFiles.size());
    }

    if (audioFiles.size() > 1) {
        // Multi-file audiobook - download all files
        brls::Logger::info("DownloadsManager: Multi-file audiobook with {} files", audioFiles.size());

        item.numFiles = static_cast<int>(audioFiles.size());
        item.files.clear();

        // Create folder for multi-file item
        std::string folderPath = m_downloadsPath + "/" + item.itemId;
#ifdef __vita__
        sceIoMkdir(folderPath.c_str(), 0777);
#else
        std::system(("mkdir -p \"" + folderPath + "\"").c_str());
#endif
        item.localPath = folderPath;

        // Calculate total size
        item.totalBytes = 0;
        for (const auto& af : audioFiles) {
            DownloadFileInfo fi;
            fi.ino = af.ino;
            fi.filename = af.filename;
            fi.localPath = folderPath + "/" + af.filename;
            fi.size = af.size;
            fi.downloaded = false;
            item.files.push_back(fi);
            item.totalBytes += af.size;
        }

        // Download each file
        HttpClient http;
        http.setDefaultHeader("Authorization", "Bearer " + token);

        for (size_t i = 0; i < item.files.size() && m_downloading; ++i) {
            auto& fi = item.files[i];
            item.currentFileIndex = static_cast<int>(i);

            std::string url = client.getFileDownloadUrlByIno(item.itemId, fi.ino);
            brls::Logger::info("DownloadsManager: Downloading file {}/{}: {}",
                              i + 1, item.files.size(), fi.filename);

#ifdef __vita__
            SceUID fd = sceIoOpen(fi.localPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
            if (fd < 0) {
                brls::Logger::error("DownloadsManager: Failed to create file {}", fi.localPath);
                continue;
            }
#else
            std::ofstream file(fi.localPath, std::ios::binary);
            if (!file.is_open()) {
                brls::Logger::error("DownloadsManager: Failed to create file {}", fi.localPath);
                continue;
            }
#endif

            bool success = http.downloadFile(url,
                [&](const char* data, size_t size) {
#ifdef __vita__
                    sceIoWrite(fd, data, size);
#else
                    file.write(data, size);
#endif
                    item.downloadedBytes += size;
                    if (m_progressCallback && item.totalBytes > 0) {
                        m_progressCallback(static_cast<float>(item.downloadedBytes),
                                           static_cast<float>(item.totalBytes));
                    }
                    return m_downloading;
                },
                [&](int64_t total) {
                    brls::Logger::debug("DownloadsManager: File size: {} bytes", total);
                }
            );

#ifdef __vita__
            sceIoClose(fd);
#else
            file.close();
#endif

            fi.downloaded = success;
        }

        // Check if all files downloaded
        bool allComplete = true;
        for (const auto& fi : item.files) {
            if (!fi.downloaded) allComplete = false;
        }

        if (allComplete) {
            item.state = DownloadState::COMPLETED;
            brls::Logger::info("DownloadsManager: Completed download of all {} files", item.files.size());
        } else if (!m_downloading) {
            item.state = DownloadState::PAUSED;
        } else {
            item.state = DownloadState::FAILED;
        }
        saveState();
        return;
    }

    // Single file download (original logic)
    brls::Logger::info("DownloadsManager: Single file download mode");
    brls::Logger::info("DownloadsManager: Getting download URL for item: {}, episode: {}",
                       item.itemId, item.episodeId.empty() ? "(none)" : item.episodeId);

    std::string url = client.getFileDownloadUrl(item.itemId, item.episodeId);

    if (url.empty()) {
        brls::Logger::error("DownloadsManager: Failed to get download URL for {}", item.itemId);
        brls::Logger::error("DownloadsManager: This usually means the file ino could not be found");
        item.state = DownloadState::FAILED;
        saveState();
        return;
    }

    brls::Logger::info("DownloadsManager: Download URL: {}", url);
    brls::Logger::info("DownloadsManager: Local path: {}", item.localPath);

    // Open local file for writing
#ifdef __vita__
    SceUID fd = sceIoOpen(item.localPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) {
        brls::Logger::error("DownloadsManager: Failed to create file {}", item.localPath);
        item.state = DownloadState::FAILED;
        saveState();
        return;
    }
#else
    std::ofstream file(item.localPath, std::ios::binary);
    if (!file.is_open()) {
        brls::Logger::error("DownloadsManager: Failed to create file {}", item.localPath);
        item.state = DownloadState::FAILED;
        saveState();
        return;
    }
#endif

    // Download with progress tracking
    HttpClient http;

    // Set auth header
    http.setDefaultHeader("Authorization", "Bearer " + token);

    bool success = http.downloadFile(url,
        [&](const char* data, size_t size) {
            // Write chunk to file
#ifdef __vita__
            sceIoWrite(fd, data, size);
#else
            file.write(data, size);
#endif
            item.downloadedBytes += size;

            // Call progress callback
            if (m_progressCallback && item.totalBytes > 0) {
                m_progressCallback(static_cast<float>(item.downloadedBytes),
                                   static_cast<float>(item.totalBytes));
            }

            return m_downloading; // Return false to cancel
        },
        [&](int64_t total) {
            item.totalBytes = total;
            brls::Logger::debug("DownloadsManager: Total size: {} bytes", total);
        }
    );

#ifdef __vita__
    sceIoClose(fd);
#else
    file.close();
#endif

    if (success && m_downloading) {
        item.state = DownloadState::COMPLETED;
        brls::Logger::info("DownloadsManager: Completed download of {}", item.title);
    } else if (!m_downloading) {
        item.state = DownloadState::PAUSED;
        brls::Logger::info("DownloadsManager: Paused download of {}", item.title);
    } else {
        item.state = DownloadState::FAILED;
        brls::Logger::error("DownloadsManager: Failed to download {}", item.title);
        // Delete partial file
#ifdef __vita__
        sceIoRemove(item.localPath.c_str());
#else
        std::remove(item.localPath.c_str());
#endif
    }

    saveState();
}

void DownloadsManager::saveState() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Simple JSON-like format for state
    std::stringstream ss;
    ss << "{\n\"downloads\":[\n";

    for (size_t i = 0; i < m_downloads.size(); ++i) {
        const auto& item = m_downloads[i];
        ss << "{\n"
           << "\"itemId\":\"" << item.itemId << "\",\n"
           << "\"episodeId\":\"" << item.episodeId << "\",\n"
           << "\"title\":\"" << item.title << "\",\n"
           << "\"authorName\":\"" << item.authorName << "\",\n"
           << "\"parentTitle\":\"" << item.parentTitle << "\",\n"
           << "\"localPath\":\"" << item.localPath << "\",\n"
           << "\"coverUrl\":\"" << item.coverUrl << "\",\n"
           << "\"mediaType\":\"" << item.mediaType << "\",\n"
           << "\"seriesName\":\"" << item.seriesName << "\",\n"
           << "\"totalBytes\":" << item.totalBytes << ",\n"
           << "\"downloadedBytes\":" << item.downloadedBytes << ",\n"
           << "\"duration\":" << item.duration << ",\n"
           << "\"currentTime\":" << item.currentTime << ",\n"
           << "\"viewOffset\":" << item.viewOffset << ",\n"
           << "\"numChapters\":" << item.numChapters << ",\n"
           << "\"state\":" << static_cast<int>(item.state) << ",\n"
           << "\"lastSynced\":" << item.lastSynced << "\n"
           << "}";
        if (i < m_downloads.size() - 1) ss << ",";
        ss << "\n";
    }

    ss << "]\n}";

#ifdef __vita__
    SceUID fd = sceIoOpen(STATE_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd >= 0) {
        std::string data = ss.str();
        sceIoWrite(fd, data.c_str(), data.size());
        sceIoClose(fd);
    }
#else
    std::ofstream file(STATE_FILE);
    if (file.is_open()) {
        file << ss.str();
        file.close();
    }
#endif

    brls::Logger::debug("DownloadsManager: Saved state ({} items)", m_downloads.size());
}

void DownloadsManager::loadState() {
    std::string content;

#ifdef __vita__
    SceUID fd = sceIoOpen(STATE_FILE, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        char buffer[4096];
        int read;
        while ((read = sceIoRead(fd, buffer, sizeof(buffer))) > 0) {
            content.append(buffer, read);
        }
        sceIoClose(fd);
    }
#else
    std::ifstream file(STATE_FILE);
    if (file.is_open()) {
        std::stringstream ss;
        ss << file.rdbuf();
        content = ss.str();
        file.close();
    }
#endif

    if (content.empty()) {
        brls::Logger::debug("DownloadsManager: No saved state found");
        return;
    }

    // Simple parsing - in production use proper JSON parser
    brls::Logger::info("DownloadsManager: Loading saved state...");

    // TODO: Implement proper JSON parsing for download items
}

void DownloadsManager::setProgressCallback(DownloadProgressCallback callback) {
    m_progressCallback = callback;
}

std::string DownloadsManager::getDownloadsPath() const {
    return m_downloadsPath;
}

} // namespace vitaabs
