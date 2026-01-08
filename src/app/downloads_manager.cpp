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
                                      const std::string& audioPath, float duration,
                                      const std::string& mediaType,
                                      const std::string& parentTitle,
                                      int seasonNum, int episodeNum) {
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
    item.audioPath = audioPath;
    item.duration = duration;
    item.mediaType = mediaType;
    item.coverUrl = coverUrl;
    item.episodeId = episodeId;
    item.state = DownloadState::QUEUED;

    // Generate local path - audiobooks are typically m4b, mp3, or other audio formats
    std::string extension;
    if (mediaType == "episode") {
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

std::string DownloadsManager::getPlaybackPath(const std::string& itemId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& item : m_downloads) {
        if (item.itemId == itemId && item.state == DownloadState::COMPLETED) {
            // For multi-file audiobooks, return the first file
            if (item.numFiles > 1 && !item.files.empty()) {
                brls::Logger::debug("DownloadsManager: Multi-file audiobook, returning first file: {}",
                                   item.files[0].localPath);
                return item.files[0].localPath;
            }
            // Single file or direct path
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
        if (client.updateProgress(item.itemId, "", item.currentTime, item.duration)) {
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
    std::string token = client.getToken();

    if (serverUrl.empty() || token.empty()) {
        brls::Logger::error("DownloadsManager: Not connected to server");
        item.state = DownloadState::FAILED;
        saveState();
        return;
    }

    // Get the download URL for the item
    // Audiobookshelf provides direct file access at /api/items/:id/file/:ino
    // Or we can use the stream URL for transcoded audio
    std::string url = client.getStreamUrl(item.itemId, "");

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

    // Add auth header
    std::map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + token;

    bool success = http.downloadFileWithHeaders(url, headers,
        [&](const char* data, size_t size) {
            // Write chunk to file
#ifdef __vita__
            sceIoWrite(fd, data, size);
#else
            file.write(data, size);
#endif
            item.downloadedBytes += size;

            // Call progress callback
            if (m_progressCallback) {
                m_progressCallback(item.downloadedBytes, item.size);
            }

            return m_downloading; // Return false to cancel
        },
        [&](int64_t total) {
            item.size = static_cast<float>(total);
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
           << "\"title\":\"" << item.title << "\",\n"
           << "\"audioPath\":\"" << item.audioPath << "\",\n"
           << "\"localPath\":\"" << item.localPath << "\",\n"
           << "\"coverUrl\":\"" << item.coverUrl << "\",\n"
           << "\"mediaType\":\"" << item.mediaType << "\",\n"
           << "\"episodeId\":\"" << item.episodeId << "\",\n"
           << "\"size\":" << item.size << ",\n"
           << "\"downloadedBytes\":" << item.downloadedBytes << ",\n"
           << "\"currentTime\":" << item.currentTime << ",\n"
           << "\"duration\":" << item.duration << ",\n"
           << "\"currentTime\":" << item.currentTime << ",\n"
           << "\"viewOffset\":" << item.viewOffset << ",\n"
           << "\"numChapters\":" << item.numChapters << ",\n"
           << "\"numFiles\":" << item.numFiles << ",\n"
           << "\"state\":" << static_cast<int>(item.state) << ",\n"
           << "\"lastSynced\":" << item.lastSynced << ",\n";

        // Save multi-file info
        ss << "\"files\":[";
        for (size_t j = 0; j < item.files.size(); ++j) {
            const auto& fi = item.files[j];
            ss << "{"
               << "\"ino\":\"" << fi.ino << "\","
               << "\"filename\":\"" << fi.filename << "\","
               << "\"localPath\":\"" << fi.localPath << "\","
               << "\"size\":" << fi.size << ","
               << "\"downloaded\":" << (fi.downloaded ? "true" : "false")
               << "}";
            if (j < item.files.size() - 1) ss << ",";
        }
        ss << "]\n}";

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

    brls::Logger::info("DownloadsManager: Loading saved state...");

    // Helper to extract string value from JSON
    auto extractValue = [](const std::string& json, const std::string& key) -> std::string {
        std::string searchKey = "\"" + key + "\":";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return "";

        size_t valueStart = json.find_first_not_of(" \t\n\r", keyPos + searchKey.length());
        if (valueStart == std::string::npos) return "";

        if (json[valueStart] == '"') {
            size_t valueEnd = valueStart + 1;
            while (valueEnd < json.length()) {
                if (json[valueEnd] == '"' && json[valueEnd - 1] != '\\') break;
                valueEnd++;
            }
            return json.substr(valueStart + 1, valueEnd - valueStart - 1);
        } else if (json[valueStart] == 't' || json[valueStart] == 'f') {
            // Boolean
            if (json.substr(valueStart, 4) == "true") return "true";
            if (json.substr(valueStart, 5) == "false") return "false";
            return "";
        } else {
            size_t valueEnd = json.find_first_of(",}]", valueStart);
            if (valueEnd == std::string::npos) return "";
            std::string value = json.substr(valueStart, valueEnd - valueStart);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\n' || value.back() == '\r')) {
                value.pop_back();
            }
            return value;
        }
    };

    // Find downloads array
    size_t arrStart = content.find("[");
    if (arrStart == std::string::npos) {
        brls::Logger::warning("DownloadsManager: Invalid state format");
        return;
    }

    // Parse each download item object
    size_t pos = arrStart;
    while (true) {
        size_t objStart = content.find('{', pos);
        if (objStart == std::string::npos) break;

        // Skip the files array objects - look for itemId to identify download items
        size_t itemIdPos = content.find("\"itemId\"", objStart);
        if (itemIdPos == std::string::npos) break;

        // Check if this itemId is within a reasonable distance (not a nested object)
        if (itemIdPos - objStart > 50) {
            pos = objStart + 1;
            continue;
        }

        // Find the end of this object (matching braces)
        int braceCount = 1;
        size_t objEnd = objStart + 1;
        while (braceCount > 0 && objEnd < content.length()) {
            if (content[objEnd] == '{') braceCount++;
            else if (content[objEnd] == '}') braceCount--;
            objEnd++;
        }

        std::string itemJson = content.substr(objStart, objEnd - objStart);

        DownloadItem item;
        item.itemId = extractValue(itemJson, "itemId");
        item.episodeId = extractValue(itemJson, "episodeId");
        item.title = extractValue(itemJson, "title");
        item.authorName = extractValue(itemJson, "authorName");
        item.parentTitle = extractValue(itemJson, "parentTitle");
        item.localPath = extractValue(itemJson, "localPath");
        item.coverUrl = extractValue(itemJson, "coverUrl");
        item.mediaType = extractValue(itemJson, "mediaType");
        item.seriesName = extractValue(itemJson, "seriesName");

        std::string totalBytesStr = extractValue(itemJson, "totalBytes");
        item.totalBytes = totalBytesStr.empty() ? 0 : std::stoll(totalBytesStr);

        std::string downloadedBytesStr = extractValue(itemJson, "downloadedBytes");
        item.downloadedBytes = downloadedBytesStr.empty() ? 0 : std::stoll(downloadedBytesStr);

        std::string durationStr = extractValue(itemJson, "duration");
        item.duration = durationStr.empty() ? 0.0f : std::stof(durationStr);

        std::string currentTimeStr = extractValue(itemJson, "currentTime");
        item.currentTime = currentTimeStr.empty() ? 0.0f : std::stof(currentTimeStr);

        std::string viewOffsetStr = extractValue(itemJson, "viewOffset");
        item.viewOffset = viewOffsetStr.empty() ? 0 : std::stoll(viewOffsetStr);

        std::string numChaptersStr = extractValue(itemJson, "numChapters");
        item.numChapters = numChaptersStr.empty() ? 0 : std::stoi(numChaptersStr);

        std::string numFilesStr = extractValue(itemJson, "numFiles");
        item.numFiles = numFilesStr.empty() ? 1 : std::stoi(numFilesStr);

        std::string stateStr = extractValue(itemJson, "state");
        item.state = stateStr.empty() ? DownloadState::QUEUED : static_cast<DownloadState>(std::stoi(stateStr));

        std::string lastSyncedStr = extractValue(itemJson, "lastSynced");
        item.lastSynced = lastSyncedStr.empty() ? 0 : std::stoll(lastSyncedStr);

        // Parse files array for multi-file downloads
        size_t filesStart = itemJson.find("\"files\":[");
        if (filesStart != std::string::npos) {
            size_t filesArrStart = itemJson.find('[', filesStart);
            size_t filesArrEnd = itemJson.find(']', filesArrStart);
            if (filesArrStart != std::string::npos && filesArrEnd != std::string::npos) {
                std::string filesJson = itemJson.substr(filesArrStart, filesArrEnd - filesArrStart + 1);

                size_t filePos = 0;
                while (true) {
                    size_t fileObjStart = filesJson.find('{', filePos);
                    if (fileObjStart == std::string::npos) break;

                    size_t fileObjEnd = filesJson.find('}', fileObjStart);
                    if (fileObjEnd == std::string::npos) break;

                    std::string fileJson = filesJson.substr(fileObjStart, fileObjEnd - fileObjStart + 1);

                    DownloadFileInfo fi;
                    fi.ino = extractValue(fileJson, "ino");
                    fi.filename = extractValue(fileJson, "filename");
                    fi.localPath = extractValue(fileJson, "localPath");

                    std::string sizeStr = extractValue(fileJson, "size");
                    fi.size = sizeStr.empty() ? 0 : std::stoll(sizeStr);

                    fi.downloaded = extractValue(fileJson, "downloaded") == "true";

                    if (!fi.localPath.empty()) {
                        item.files.push_back(fi);
                    }

                    filePos = fileObjEnd + 1;
                }
            }
        }

        if (!item.itemId.empty()) {
            m_downloads.push_back(item);
            brls::Logger::debug("DownloadsManager: Loaded download: {} (state: {})",
                               item.title, static_cast<int>(item.state));
        }

        pos = objEnd;
    }

    brls::Logger::info("DownloadsManager: Loaded {} downloads from state", m_downloads.size());
}

void DownloadsManager::setProgressCallback(DownloadProgressCallback callback) {
    m_progressCallback = callback;
}

std::string DownloadsManager::getDownloadsPath() const {
    return m_downloadsPath;
}

} // namespace vitaabs
