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

    m_downloads.push_back(item);
    saveState();

    brls::Logger::info("DownloadsManager: Queued {} for download", title);
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

    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
    std::string serverUrl = client.getServerUrl();
    std::string token = client.getAuthToken();

    if (serverUrl.empty() || token.empty()) {
        brls::Logger::error("DownloadsManager: Not connected to server");
        item.state = DownloadState::FAILED;
        saveState();
        return;
    }

    // Get the download URL for the item
    // Audiobookshelf provides direct file access or stream URL
    std::string url = client.getStreamUrl(item.itemId, item.episodeId);

    if (url.empty()) {
        brls::Logger::error("DownloadsManager: Failed to get stream URL for {}", item.itemId);
        item.state = DownloadState::FAILED;
        saveState();
        return;
    }

    brls::Logger::debug("DownloadsManager: Downloading from {}", url);

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
