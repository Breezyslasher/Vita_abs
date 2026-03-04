/**
 * VitaABS - Downloads Tab
 * View for managing downloads with server queue + local downloads sections (like Suwayomi)
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
    void refreshServerQueue();
    void refreshLocalDownloads();
    void startAutoRefresh();
    void stopAutoRefresh();

    // Server download queue section (items downloading from server)
    brls::Box* m_serverSection = nullptr;
    brls::Label* m_serverHeader = nullptr;
    brls::ScrollingFrame* m_serverScroll = nullptr;
    brls::Box* m_serverContainer = nullptr;
    brls::Label* m_serverEmptyLabel = nullptr;

    // Local downloads section (completed items stored on device)
    brls::Box* m_localSection = nullptr;
    brls::Label* m_localHeader = nullptr;
    brls::ScrollingFrame* m_localScroll = nullptr;
    brls::Box* m_localContainer = nullptr;
    brls::Label* m_localEmptyLabel = nullptr;

    // Empty state (shown when both sections are empty)
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

    // Track currently focused X button icon (for show/hide on focus change)
    brls::Image* m_currentFocusedIcon = nullptr;

    // Cached state for smart incremental updates
    struct CachedServerItem {
        std::string itemId;
        std::string episodeId;
        int64_t downloadedBytes = 0;
        int64_t totalBytes = 0;
        int state = 0;
    };
    std::vector<CachedServerItem> m_lastServerItems;

    struct CachedLocalItem {
        std::string itemId;
        std::string episodeId;
        float currentTime = 0.0f;
    };
    std::vector<CachedLocalItem> m_lastLocalItems;

    // UI element tracking for incremental updates
    struct ServerRowElements {
        brls::Box* row = nullptr;
        brls::Label* progressLabel = nullptr;
        brls::Image* xButtonIcon = nullptr;
        std::string itemId;
        std::string episodeId;
    };
    std::vector<ServerRowElements> m_serverRowElements;

    struct LocalRowElements {
        brls::Box* row = nullptr;
        brls::Label* statusLabel = nullptr;
        brls::Image* xButtonIcon = nullptr;
        std::string itemId;
        std::string episodeId;
    };
    std::vector<LocalRowElements> m_localRowElements;

    // Helper methods
    brls::Box* createServerRow(const std::string& itemId, const std::string& episodeId,
                               const std::string& title, const std::string& authorName,
                               int64_t downloadedBytes, int64_t totalBytes, int state,
                               const std::string& coverUrl, const std::string& localCoverPath,
                               brls::Label*& outProgressLabel, brls::Image*& outXButtonIcon);
    brls::Box* createLocalRow(const std::string& itemId, const std::string& episodeId,
                              const std::string& title, const std::string& authorName,
                              float currentTime, float duration,
                              const std::string& coverUrl, const std::string& localCoverPath,
                              const std::string& mediaType,
                              brls::Label*& outStatusLabel, brls::Image*& outXButtonIcon);
    void updateNavigationRoutes();

    // Async lifetime guard
    std::shared_ptr<bool> m_alive;
};

} // namespace vitaabs
