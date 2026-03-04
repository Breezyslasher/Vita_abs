/**
 * VitaABS - Downloads Tab
 * View for managing offline downloads and showing download queue
 * Based on Vita_Suwayomi design with audiobook-specific adaptations
 */

#pragma once

#include <borealis.hpp>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>
#include <memory>

namespace vitaabs {

class DownloadsTab : public brls::Box {
public:
    DownloadsTab();
    ~DownloadsTab() override;

    void willAppear(bool resetState) override;
    void willDisappear(bool resetState) override;

private:
    void refresh();
    void refreshQueue();
    void refreshCompleted();
    void showDownloadOptions(const std::string& ratingKey, const std::string& title);
    void startAutoRefresh();
    void stopAutoRefresh();

    // Download queue section (active downloads)
    brls::Box* m_queueSection = nullptr;
    brls::Label* m_queueHeader = nullptr;
    brls::ScrollingFrame* m_queueScroll = nullptr;
    brls::Box* m_queueContainer = nullptr;
    brls::Label* m_queueEmptyLabel = nullptr;

    // Completed downloads section
    brls::Box* m_completedSection = nullptr;
    brls::Label* m_completedHeader = nullptr;
    brls::ScrollingFrame* m_completedScroll = nullptr;
    brls::Box* m_completedContainer = nullptr;
    brls::Label* m_completedEmptyLabel = nullptr;

    // Empty state (shown when no downloads in either section)
    brls::Box* m_emptyStateBox = nullptr;

    // Top action buttons
    brls::Box* m_actionsRow = nullptr;
    brls::Button* m_startStopBtn = nullptr;
    brls::Label* m_startStopLabel = nullptr;
    brls::Button* m_pauseBtn = nullptr;
    brls::Button* m_clearBtn = nullptr;
    brls::Button* m_syncBtn = nullptr;

    // Download status display
    brls::Label* m_downloadStatusLabel = nullptr;
    bool m_downloaderRunning = false;

    // Auto-refresh state
    std::atomic<bool> m_autoRefreshEnabled{false};
    std::atomic<bool> m_autoRefreshTimerActive{false};

    // Throttle progress updates
    std::chrono::steady_clock::time_point m_lastProgressRefresh;
    static constexpr int PROGRESS_REFRESH_INTERVAL_MS = 500;

    // Cached queue state for smart refresh
    struct CachedQueueItem {
        std::string itemId;
        std::string episodeId;
        int64_t downloadedBytes = 0;
        int64_t totalBytes = 0;
        int state = 0;
    };
    std::vector<CachedQueueItem> m_lastQueueItems;

    struct CachedCompletedItem {
        std::string itemId;
        std::string episodeId;
        float currentTime = 0.0f;
    };
    std::vector<CachedCompletedItem> m_lastCompletedItems;

    // UI element tracking for incremental updates
    struct QueueRowElements {
        brls::Box* row = nullptr;
        brls::Label* progressLabel = nullptr;
        std::string itemId;
        std::string episodeId;
    };
    std::vector<QueueRowElements> m_queueRowElements;

    struct CompletedRowElements {
        brls::Box* row = nullptr;
        brls::Label* statusLabel = nullptr;
        std::string itemId;
        std::string episodeId;
    };
    std::vector<CompletedRowElements> m_completedRowElements;

    // Helper methods
    brls::Box* createQueueRow(const std::string& itemId, const std::string& episodeId,
                              const std::string& title, const std::string& authorName,
                              int64_t downloadedBytes, int64_t totalBytes, int state,
                              const std::string& coverUrl, const std::string& localCoverPath,
                              brls::Label*& outProgressLabel);
    brls::Box* createCompletedRow(const std::string& itemId, const std::string& episodeId,
                                   const std::string& title, const std::string& authorName,
                                   float currentTime, float duration,
                                   const std::string& coverUrl, const std::string& localCoverPath,
                                   const std::string& mediaType,
                                   brls::Label*& outStatusLabel);
    void updateNavigationRoutes();

    // Async lifetime guard
    std::shared_ptr<bool> m_alive;
};

} // namespace vitaabs
