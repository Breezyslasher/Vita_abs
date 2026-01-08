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

// Download file info (for multi-file audiobooks)
struct DownloadFileInfo {
    std::string ino;            // File inode for download URL
    std::string filename;       // Local filename
    std::string localPath;      // Full local path
    int64_t size = 0;           // File size
    bool downloaded = false;    // Download complete
};

// Download item information
struct DownloadItem {
    std::string itemId;         // Audiobookshelf item ID
    std::string episodeId;      // Episode ID (for podcasts)
    std::string title;          // Display title
    std::string authorName;     // Author/narrator name
    std::string parentTitle;    // Series name or parent title (for display)
    std::string localPath;      // Local storage path (folder for multi-file)
    std::string coverUrl;       // Cover image URL
    int64_t totalBytes = 0;     // Total file size (all files combined)
    int64_t downloadedBytes = 0; // Downloaded so far
    float duration = 0.0f;      // Media duration in seconds
    float currentTime = 0.0f;   // Watch progress in seconds
    int64_t viewOffset = 0;     // Progress in milliseconds (for UI compatibility)
    DownloadState state = DownloadState::QUEUED;
    std::string mediaType;      // "book", "podcast"
    std::string seriesName;     // Series name for audiobooks
    int numChapters = 0;        // Number of chapters
    int numFiles = 1;           // Number of audio files (1 = single file)
    int currentFileIndex = 0;   // Current file being downloaded
    std::vector<DownloadFileInfo> files;  // Multi-file info
    time_t lastSynced = 0;      // Last time progress was synced to server
};

// Progress callback: (downloadedBytes, totalBytes)
using DownloadProgressCallback = std::function<void(float, float)>;

class DownloadsManager {
public:
    static DownloadsManager& getInstance();

    // Initialize downloads directory and load saved state
    bool init();

    // Queue an audiobook for download
    bool queueDownload(const std::string& itemId, const std::string& title,
                       const std::string& authorName, float duration,
                       const std::string& mediaType = "book",
                       const std::string& seriesName = "",
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

    // Get playback path for multi-file audiobooks (returns first file or single file path)
    std::string getPlaybackPath(const std::string& itemId) const;

    // Update watch progress for downloaded media
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

    // Register an already-downloaded file as a completed download
    // Used when streaming cache is saved to downloads folder
    bool registerCompletedDownload(const std::string& itemId, const std::string& episodeId,
                                   const std::string& title, const std::string& authorName,
                                   const std::string& localPath, int64_t fileSize,
                                   float duration, const std::string& mediaType = "book");

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
