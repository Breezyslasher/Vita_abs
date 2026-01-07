/**
 * VitaABS - Downloads Manager
 * Handles offline audiobook downloads and progress sync
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>

namespace vitaabs {

// Download state
enum class DownloadState {
    QUEUED,
    DOWNLOADING,
    PAUSED,
    COMPLETED,
    FAILED
};

// Download item information
struct DownloadItem {
    std::string itemId;         // Audiobookshelf item ID
    std::string title;          // Display title
    std::string audioPath;      // Path to audio file on server
    std::string localPath;      // Local storage path
    std::string coverUrl;       // Cover image URL
    std::string mediaType;      // "book", "podcast"
    std::string episodeId;      // Episode ID for podcasts
    float size = 0;             // Total file size in bytes
    float downloadedBytes = 0;  // Downloaded so far
    float currentTime = 0;      // Playback progress in seconds
    float duration = 0;         // Total duration in seconds
    DownloadState state = DownloadState::QUEUED;
    time_t lastSynced = 0;      // Last time progress was synced to server
};

// Progress callback: (downloadedBytes, totalBytes)
using DownloadProgressCallback = std::function<void(float, float)>;

class DownloadsManager {
public:
    static DownloadsManager& getInstance();

    // Initialize downloads directory and load saved state
    bool init();

    // Queue a media item for download
    bool queueDownload(const std::string& itemId, const std::string& title,
                       const std::string& audioPath, float size,
                       const std::string& mediaType, const std::string& coverUrl,
                       const std::string& episodeId = "");

    // Start downloading queued items
    void startDownloads();

    // Pause all downloads
    void pauseDownloads();

    // Cancel a specific download
    bool cancelDownload(const std::string& itemId);

    // Delete a downloaded item
    bool deleteDownload(const std::string& itemId);

    // Get all download items
    std::vector<DownloadItem> getDownloads() const;

    // Get a specific download by item ID
    DownloadItem* getDownload(const std::string& itemId);

    // Check if media is downloaded
    bool isDownloaded(const std::string& itemId) const;

    // Get local playback path for downloaded media
    std::string getLocalPath(const std::string& itemId) const;

    // Update playback progress for downloaded media
    void updateProgress(const std::string& itemId, float currentTime);

    // Sync all offline progress to server (call when online)
    void syncProgressToServer();

    // Save/load state to persistent storage
    void saveState();
    void loadState();

    // Set progress callback for UI updates
    void setProgressCallback(DownloadProgressCallback callback);

    // Get downloads directory path
    std::string getDownloadsPath() const;

private:
    DownloadsManager() = default;
    ~DownloadsManager() = default;
    DownloadsManager(const DownloadsManager&) = delete;
    DownloadsManager& operator=(const DownloadsManager&) = delete;

    // Download a single item (runs in background)
    void downloadItem(DownloadItem& item);

    std::vector<DownloadItem> m_downloads;
    mutable std::mutex m_mutex;
    bool m_downloading = false;
    bool m_initialized = false;
    DownloadProgressCallback m_progressCallback;
    std::string m_downloadsPath;
};

} // namespace vitaabs
