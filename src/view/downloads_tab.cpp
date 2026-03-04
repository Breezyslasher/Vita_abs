/**
 * VitaABS - Downloads Tab Implementation
 * Shows download queue and completed downloads for offline playback
 * Based on Vita_Suwayomi design with audiobook-specific adaptations
 */

#include "view/downloads_tab.hpp"
#include "app/downloads_manager.hpp"
#include "app/application.hpp"
#include "activity/player_activity.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"
#include <fstream>
#include <memory>
#include <thread>
#include <chrono>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif

// Auto-refresh interval in milliseconds
static const int AUTO_REFRESH_INTERVAL_MS = 3000;
static const int AUTO_REFRESH_INTERVAL_LARGE_MS = 5000;
static const int LARGE_QUEUE_THRESHOLD = 50;

namespace vitaabs {

// Helper to load local cover image on Vita
static void loadLocalCoverImage(brls::Image* image, const std::string& localPath) {
    if (localPath.empty() || !image) return;

#ifdef __vita__
    SceUID fd = sceIoOpen(localPath.c_str(), SCE_O_RDONLY, 0);
    if (fd >= 0) {
        SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
        sceIoLseek(fd, 0, SCE_SEEK_SET);

        if (size > 0 && size < 10 * 1024 * 1024) {  // Max 10MB
            std::vector<uint8_t> data(size);
            if (sceIoRead(fd, data.data(), size) == size) {
                image->setImageFromMem(data.data(), data.size());
            }
        }
        sceIoClose(fd);
    }
#else
    std::ifstream file(localPath, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (size > 0 && size < 10 * 1024 * 1024) {
            std::vector<uint8_t> data(size);
            if (file.read(reinterpret_cast<char*>(data.data()), size)) {
                image->setImageFromMem(data.data(), data.size());
            }
        }
        file.close();
    }
#endif
}

DownloadsTab::DownloadsTab() {
    m_alive = std::make_shared<bool>(true);

    this->setAxis(brls::Axis::COLUMN);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Header row with title and action buttons
    auto headerRow = new brls::Box();
    headerRow->setAxis(brls::Axis::ROW);
    headerRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    headerRow->setAlignItems(brls::AlignItems::CENTER);
    headerRow->setMargins(0, 0, 15, 0);
    this->addView(headerRow);

    // Left side: title and status
    auto titleBox = new brls::Box();
    titleBox->setAxis(brls::Axis::ROW);
    titleBox->setAlignItems(brls::AlignItems::CENTER);
    titleBox->setShrink(0);
    titleBox->setGrow(1.0f);
    headerRow->addView(titleBox);

    auto header = new brls::Label();
    header->setText("Downloads");
    header->setFontSize(24);
    header->setSingleLine(true);
    titleBox->addView(header);

    // Download status label
    m_downloadStatusLabel = new brls::Label();
    m_downloadStatusLabel->setText("");
    m_downloadStatusLabel->setFontSize(14);
    m_downloadStatusLabel->setMarginLeft(15);
    m_downloadStatusLabel->setTextColor(nvgRGBA(150, 150, 150, 255));
    titleBox->addView(m_downloadStatusLabel);

    // Actions row
    m_actionsRow = new brls::Box();
    m_actionsRow->setAxis(brls::Axis::ROW);
    m_actionsRow->setAlignItems(brls::AlignItems::CENTER);
    headerRow->addView(m_actionsRow);

    // Start/Stop toggle button
    m_startStopBtn = new brls::Button();
    m_startStopBtn->setWidth(70);
    m_startStopBtn->setHeight(32);
    m_startStopBtn->setCornerRadius(6);
    m_startStopBtn->setMarginRight(8);
    m_startStopBtn->setPaddingLeft(8);
    m_startStopBtn->setPaddingRight(8);
    m_startStopBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_startStopBtn->setAlignItems(brls::AlignItems::CENTER);

    m_startStopLabel = new brls::Label();
    m_startStopLabel->setText("Start");
    m_startStopLabel->setFontSize(12);
    m_startStopBtn->addView(m_startStopLabel);

    m_startStopBtn->registerClickAction([this](brls::View*) {
        bool wasRunning = m_downloaderRunning;
        DownloadsManager& mgr = DownloadsManager::getInstance();
        if (wasRunning) {
            mgr.pauseDownloads();
            m_downloaderRunning = false;
            if (m_startStopLabel) m_startStopLabel->setText("Start");
            if (m_downloadStatusLabel) {
                m_downloadStatusLabel->setText("- Stopped");
                m_downloadStatusLabel->setTextColor(nvgRGBA(200, 150, 100, 255));
            }
            brls::Application::notify("Downloads paused");
        } else {
            // Resume incomplete downloads first, then start
            mgr.resumeIncompleteDownloads();
            mgr.startDownloads();
            m_downloaderRunning = true;
            if (m_startStopLabel) m_startStopLabel->setText("Pause");
            if (m_downloadStatusLabel) {
                m_downloadStatusLabel->setText("- Downloading");
                m_downloadStatusLabel->setTextColor(nvgRGBA(100, 200, 100, 255));
            }
            brls::Application::notify("Downloads started");
        }
        refresh();
        return true;
    });
    m_actionsRow->addView(m_startStopBtn);

    // Stop button
    m_pauseBtn = new brls::Button();
    m_pauseBtn->setWidth(60);
    m_pauseBtn->setHeight(32);
    m_pauseBtn->setCornerRadius(6);
    m_pauseBtn->setMarginRight(8);
    m_pauseBtn->setPaddingLeft(8);
    m_pauseBtn->setPaddingRight(8);
    m_pauseBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_pauseBtn->setAlignItems(brls::AlignItems::CENTER);

    auto* pauseLabel = new brls::Label();
    pauseLabel->setText("Stop");
    pauseLabel->setFontSize(12);
    m_pauseBtn->addView(pauseLabel);

    m_pauseBtn->registerClickAction([this](brls::View*) {
        DownloadsManager& mgr = DownloadsManager::getInstance();
        mgr.pauseDownloads();
        m_downloaderRunning = false;
        if (m_startStopLabel) m_startStopLabel->setText("Start");
        if (m_downloadStatusLabel) {
            m_downloadStatusLabel->setText("- Stopped");
            m_downloadStatusLabel->setTextColor(nvgRGBA(200, 150, 100, 255));
        }
        brls::Application::notify("Downloads stopped");
        refresh();
        return true;
    });
    m_actionsRow->addView(m_pauseBtn);

    // Clear button
    m_clearBtn = new brls::Button();
    m_clearBtn->setWidth(70);
    m_clearBtn->setHeight(32);
    m_clearBtn->setCornerRadius(6);
    m_clearBtn->setMarginRight(8);
    m_clearBtn->setPaddingLeft(8);
    m_clearBtn->setPaddingRight(8);
    m_clearBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_clearBtn->setAlignItems(brls::AlignItems::CENTER);

    auto* clearLabel = new brls::Label();
    clearLabel->setText("Clear");
    clearLabel->setFontSize(12);
    m_clearBtn->addView(clearLabel);

    m_clearBtn->registerClickAction([this](brls::View*) {
        DownloadsManager& mgr = DownloadsManager::getInstance();

        // Stop downloads first and wait
        mgr.pauseDownloads();
        mgr.waitForDownloadThread();

        // Cancel all non-completed downloads
        auto downloads = mgr.getDownloads();
        for (const auto& item : downloads) {
            if (item.state != DownloadState::COMPLETED) {
                mgr.cancelDownload(item.itemId);
            }
        }

        m_downloaderRunning = false;
        if (m_startStopLabel) m_startStopLabel->setText("Start");
        if (m_downloadStatusLabel) m_downloadStatusLabel->setText("");
        brls::Application::notify("Queue cleared");
        refresh();
        return true;
    });
    m_actionsRow->addView(m_clearBtn);

    // Sync button
    m_syncBtn = new brls::Button();
    m_syncBtn->setWidth(70);
    m_syncBtn->setHeight(32);
    m_syncBtn->setCornerRadius(6);
    m_syncBtn->setPaddingLeft(8);
    m_syncBtn->setPaddingRight(8);
    m_syncBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_syncBtn->setAlignItems(brls::AlignItems::CENTER);

    auto* syncLabel = new brls::Label();
    syncLabel->setText("Sync");
    syncLabel->setFontSize(12);
    m_syncBtn->addView(syncLabel);

    m_syncBtn->registerClickAction([](brls::View*) {
        asyncRun([]() {
            DownloadsManager::getInstance().syncProgressToServer();
            brls::sync([]() {
                brls::Application::notify("Progress synced to server");
            });
        });
        return true;
    });
    m_actionsRow->addView(m_syncBtn);

    // === Download Queue Section (active downloads) ===
    m_queueSection = new brls::Box();
    m_queueSection->setAxis(brls::Axis::COLUMN);
    m_queueSection->setGrow(1.0f);
    m_queueSection->setMargins(0, 0, 15, 0);
    m_queueSection->setVisibility(brls::Visibility::GONE);
    this->addView(m_queueSection);

    m_queueHeader = new brls::Label();
    m_queueHeader->setText("Download Queue");
    m_queueHeader->setFontSize(18);
    m_queueHeader->setMargins(0, 0, 10, 0);
    m_queueSection->addView(m_queueHeader);

    m_queueEmptyLabel = new brls::Label();
    m_queueEmptyLabel->setText("No active downloads");
    m_queueEmptyLabel->setFontSize(14);
    m_queueEmptyLabel->setTextColor(nvgRGBA(120, 120, 120, 255));
    m_queueEmptyLabel->setMargins(10, 0, 10, 0);
    m_queueSection->addView(m_queueEmptyLabel);

    m_queueScroll = new brls::ScrollingFrame();
    m_queueScroll->setGrow(1.0f);
    m_queueScroll->setVisibility(brls::Visibility::GONE);
    m_queueSection->addView(m_queueScroll);

    m_queueContainer = new brls::Box();
    m_queueContainer->setAxis(brls::Axis::COLUMN);
    m_queueScroll->setContentView(m_queueContainer);

    // === Completed Downloads Section ===
    m_completedSection = new brls::Box();
    m_completedSection->setAxis(brls::Axis::COLUMN);
    m_completedSection->setGrow(1.0f);
    m_completedSection->setVisibility(brls::Visibility::GONE);
    this->addView(m_completedSection);

    m_completedHeader = new brls::Label();
    m_completedHeader->setText("Completed Downloads");
    m_completedHeader->setFontSize(18);
    m_completedHeader->setMargins(0, 0, 10, 0);
    m_completedSection->addView(m_completedHeader);

    m_completedEmptyLabel = new brls::Label();
    m_completedEmptyLabel->setText("No completed downloads");
    m_completedEmptyLabel->setFontSize(14);
    m_completedEmptyLabel->setTextColor(nvgRGBA(120, 120, 120, 255));
    m_completedEmptyLabel->setMargins(10, 0, 10, 0);
    m_completedSection->addView(m_completedEmptyLabel);

    m_completedScroll = new brls::ScrollingFrame();
    m_completedScroll->setGrow(1.0f);
    m_completedScroll->setVisibility(brls::Visibility::GONE);
    m_completedSection->addView(m_completedScroll);

    m_completedContainer = new brls::Box();
    m_completedContainer->setAxis(brls::Axis::COLUMN);
    m_completedScroll->setContentView(m_completedContainer);

    // Empty state
    m_emptyStateBox = new brls::Box();
    m_emptyStateBox->setAxis(brls::Axis::COLUMN);
    m_emptyStateBox->setJustifyContent(brls::JustifyContent::CENTER);
    m_emptyStateBox->setAlignItems(brls::AlignItems::CENTER);
    m_emptyStateBox->setGrow(1.0f);
    m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);

    auto* emptyIcon = new brls::Label();
    emptyIcon->setText("No Downloads");
    emptyIcon->setFontSize(24);
    emptyIcon->setTextColor(nvgRGB(128, 128, 128));
    emptyIcon->setMarginBottom(10);
    m_emptyStateBox->addView(emptyIcon);

    auto* emptyHint = new brls::Label();
    emptyHint->setText("Use the download button on media details to save for offline listening");
    emptyHint->setFontSize(16);
    emptyHint->setTextColor(nvgRGB(100, 100, 100));
    m_emptyStateBox->addView(emptyHint);

    this->addView(m_emptyStateBox);
}

void DownloadsTab::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);

    // Re-arm alive flag
    m_alive = std::make_shared<bool>(true);

    // Clear tracking vectors for fresh start
    m_queueRowElements.clear();
    m_completedRowElements.clear();
    m_lastQueueItems.clear();
    m_lastCompletedItems.clear();

    // Remove stale row views
    if (m_queueContainer) {
        while (m_queueContainer->getChildren().size() > 0)
            m_queueContainer->removeView(m_queueContainer->getChildren()[0]);
    }
    if (m_completedContainer) {
        while (m_completedContainer->getChildren().size() > 0)
            m_completedContainer->removeView(m_completedContainer->getChildren()[0]);
    }

    m_lastProgressRefresh = std::chrono::steady_clock::now();

    // Register progress callback for live UI updates
    DownloadsManager& mgr = DownloadsManager::getInstance();
    std::weak_ptr<bool> aliveWeak = m_alive;
    mgr.setProgressCallback([this, aliveWeak](float downloadedBytes, float totalBytes) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastProgressRefresh).count();

        // Update every 100ms for smooth progress, or immediately for completion
        const int FAST_PROGRESS_INTERVAL_MS = 100;
        if (elapsed >= FAST_PROGRESS_INTERVAL_MS || downloadedBytes >= totalBytes) {
            m_lastProgressRefresh = now;
            if (m_autoRefreshEnabled.load()) {
                brls::sync([this, aliveWeak]() {
                    auto alive = aliveWeak.lock();
                    if (!alive || !*alive) return;
                    if (m_autoRefreshEnabled.load()) {
                        refreshQueue();
                    }
                });
            }
        }
    });

    // Register item completion callback
    mgr.setItemCompletionCallback([this, aliveWeak](const std::string& itemId, const std::string& episodeId, bool success) {
        if (!m_autoRefreshEnabled.load()) return;

        brls::sync([this, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            if (!m_autoRefreshEnabled.load()) return;
            // Full refresh to move item from queue to completed
            refresh();
        });
    });

    refresh();
    startAutoRefresh();
}

DownloadsTab::~DownloadsTab() {
    if (m_alive) *m_alive = false;
}

void DownloadsTab::willDisappear(bool resetState) {
    brls::Box::willDisappear(resetState);

    if (m_alive) *m_alive = false;

    stopAutoRefresh();

    DownloadsManager& mgr = DownloadsManager::getInstance();
    mgr.setProgressCallback(nullptr);
    mgr.setItemCompletionCallback(nullptr);

    ImageLoader::cancelAll();

    m_queueRowElements.clear();
    m_completedRowElements.clear();
}

void DownloadsTab::refresh() {
    refreshQueue();
    refreshCompleted();
}

void DownloadsTab::refreshQueue() {
    DownloadsManager& mgr = DownloadsManager::getInstance();
    mgr.init();

    auto downloads = mgr.getDownloads();

    // Build list of active (non-completed) items
    struct QueueInfo {
        std::string itemId, episodeId, title, authorName, coverUrl, localCoverPath;
        int64_t downloadedBytes, totalBytes;
        int state;
    };
    std::vector<QueueInfo> queueItems;

    bool isAnyDownloading = false;
    for (const auto& item : downloads) {
        if (item.state != DownloadState::COMPLETED) {
            QueueInfo qi;
            qi.itemId = item.itemId;
            qi.episodeId = item.episodeId;
            qi.title = item.title;
            qi.authorName = item.authorName;
            qi.coverUrl = item.coverUrl;
            qi.localCoverPath = item.localCoverPath;
            qi.downloadedBytes = item.downloadedBytes;
            qi.totalBytes = item.totalBytes;
            qi.state = static_cast<int>(item.state);
            queueItems.push_back(qi);
            if (item.state == DownloadState::DOWNLOADING) isAnyDownloading = true;
        }
    }

    // Update status
    m_downloaderRunning = isAnyDownloading || mgr.isDownloading();
    if (m_startStopLabel) {
        m_startStopLabel->setText(m_downloaderRunning ? "Pause" : "Start");
    }
    if (m_downloadStatusLabel) {
        if (queueItems.empty()) {
            m_downloadStatusLabel->setText("");
        } else if (m_downloaderRunning) {
            m_downloadStatusLabel->setText("- Downloading");
            m_downloadStatusLabel->setTextColor(nvgRGBA(100, 200, 100, 255));
        } else {
            bool hasFailed = false;
            for (const auto& qi : queueItems) {
                if (qi.state == static_cast<int>(DownloadState::FAILED)) { hasFailed = true; break; }
            }
            if (hasFailed) {
                m_downloadStatusLabel->setText("- Error");
                m_downloadStatusLabel->setTextColor(nvgRGBA(200, 100, 100, 255));
            } else {
                m_downloadStatusLabel->setText("- Stopped");
                m_downloadStatusLabel->setTextColor(nvgRGBA(200, 150, 100, 255));
            }
        }
    }

    if (queueItems.empty()) {
        m_queueSection->setVisibility(brls::Visibility::GONE);
        m_lastQueueItems.clear();
        m_queueRowElements.clear();
        while (m_queueContainer->getChildren().size() > 0) {
            m_queueContainer->removeView(m_queueContainer->getChildren()[0]);
        }

        // Show empty state if completed is also empty
        bool hasCompleted = !m_lastCompletedItems.empty();
        for (const auto& item : downloads) {
            if (item.state == DownloadState::COMPLETED) { hasCompleted = true; break; }
        }
        if (!hasCompleted && m_emptyStateBox) {
            m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);
        }
        return;
    }

    // Hide empty state
    if (m_emptyStateBox) m_emptyStateBox->setVisibility(brls::Visibility::GONE);
    m_queueSection->setVisibility(brls::Visibility::VISIBLE);
    m_queueEmptyLabel->setVisibility(brls::Visibility::GONE);
    m_queueScroll->setVisibility(brls::Visibility::VISIBLE);

    // Build new cache
    std::vector<CachedQueueItem> newCache;
    for (const auto& qi : queueItems) {
        CachedQueueItem cached;
        cached.itemId = qi.itemId;
        cached.episodeId = qi.episodeId;
        cached.downloadedBytes = qi.downloadedBytes;
        cached.totalBytes = qi.totalBytes;
        cached.state = qi.state;
        newCache.push_back(cached);
    }

    // Check for in-place progress updates (same items, just progress changed)
    bool structureChanged = (newCache.size() != m_lastQueueItems.size());
    if (!structureChanged) {
        for (size_t i = 0; i < newCache.size(); i++) {
            if (newCache[i].itemId != m_lastQueueItems[i].itemId ||
                newCache[i].episodeId != m_lastQueueItems[i].episodeId) {
                structureChanged = true;
                break;
            }
        }
    }

    // Update in-place if possible
    if (!structureChanged) {
        for (size_t i = 0; i < newCache.size() && i < m_queueRowElements.size(); i++) {
            const auto& newItem = newCache[i];
            const auto& oldItem = m_lastQueueItems[i];
            if (newItem.downloadedBytes != oldItem.downloadedBytes ||
                newItem.state != oldItem.state) {
                if (m_queueRowElements[i].progressLabel) {
                    std::string progressText;
                    if (newItem.state == static_cast<int>(DownloadState::DOWNLOADING)) {
                        if (newItem.totalBytes > 0) {
                            int percent = static_cast<int>((newItem.downloadedBytes * 100) / newItem.totalBytes);
                            progressText = "Downloading... " + std::to_string(percent) + "%";
                        } else {
                            progressText = "Downloading...";
                        }
                        m_queueRowElements[i].progressLabel->setTextColor(nvgRGBA(100, 200, 100, 255));
                    } else if (newItem.state == static_cast<int>(DownloadState::QUEUED)) {
                        progressText = "Queued";
                        m_queueRowElements[i].progressLabel->setTextColor(nvgRGBA(255, 255, 255, 255));
                    } else if (newItem.state == static_cast<int>(DownloadState::PAUSED)) {
                        progressText = "Paused";
                        m_queueRowElements[i].progressLabel->setTextColor(nvgRGBA(200, 150, 100, 255));
                    } else if (newItem.state == static_cast<int>(DownloadState::FAILED)) {
                        progressText = "Failed";
                        m_queueRowElements[i].progressLabel->setTextColor(nvgRGBA(200, 100, 100, 255));
                    }
                    m_queueRowElements[i].progressLabel->setText(progressText);

                    // Update background color
                    if (m_queueRowElements[i].row) {
                        if (newItem.state == static_cast<int>(DownloadState::DOWNLOADING)) {
                            m_queueRowElements[i].row->setBackgroundColor(nvgRGBA(30, 60, 30, 200));
                        } else if (newItem.state == static_cast<int>(DownloadState::FAILED)) {
                            m_queueRowElements[i].row->setBackgroundColor(nvgRGBA(60, 30, 30, 200));
                        } else {
                            m_queueRowElements[i].row->setBackgroundColor(nvgRGBA(40, 40, 40, 200));
                        }
                    }
                }
            }
        }
        m_lastQueueItems = newCache;
        return;
    }

    // Full rebuild needed
    m_lastQueueItems = newCache;
    m_queueRowElements.clear();
    while (m_queueContainer->getChildren().size() > 0) {
        m_queueContainer->removeView(m_queueContainer->getChildren()[0]);
    }

    for (const auto& qi : queueItems) {
        brls::Label* progressLabel = nullptr;
        auto* row = createQueueRow(qi.itemId, qi.episodeId, qi.title, qi.authorName,
                                    qi.downloadedBytes, qi.totalBytes, qi.state,
                                    qi.coverUrl, qi.localCoverPath, progressLabel);
        m_queueContainer->addView(row);

        QueueRowElements elem;
        elem.row = row;
        elem.progressLabel = progressLabel;
        elem.itemId = qi.itemId;
        elem.episodeId = qi.episodeId;
        m_queueRowElements.push_back(elem);
    }

    updateNavigationRoutes();
}

void DownloadsTab::refreshCompleted() {
    DownloadsManager& mgr = DownloadsManager::getInstance();
    auto downloads = mgr.getDownloads();

    // Build list of completed items
    struct CompletedInfo {
        std::string itemId, episodeId, title, authorName, coverUrl, localCoverPath, mediaType;
        float currentTime, duration;
    };
    std::vector<CompletedInfo> completedItems;

    for (const auto& item : downloads) {
        if (item.state == DownloadState::COMPLETED) {
            CompletedInfo ci;
            ci.itemId = item.itemId;
            ci.episodeId = item.episodeId;
            ci.title = item.title;
            ci.authorName = item.authorName;
            ci.coverUrl = item.coverUrl;
            ci.localCoverPath = item.localCoverPath;
            ci.mediaType = item.mediaType;
            ci.currentTime = item.currentTime;
            ci.duration = item.duration;
            completedItems.push_back(ci);
        }
    }

    if (completedItems.empty()) {
        m_completedSection->setVisibility(brls::Visibility::GONE);
        m_lastCompletedItems.clear();
        m_completedRowElements.clear();
        while (m_completedContainer->getChildren().size() > 0) {
            m_completedContainer->removeView(m_completedContainer->getChildren()[0]);
        }

        // Show empty state if queue is also empty
        if (m_lastQueueItems.empty() && m_emptyStateBox) {
            m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);
        }
        return;
    }

    if (m_emptyStateBox) m_emptyStateBox->setVisibility(brls::Visibility::GONE);
    m_completedSection->setVisibility(brls::Visibility::VISIBLE);
    m_completedEmptyLabel->setVisibility(brls::Visibility::GONE);
    m_completedScroll->setVisibility(brls::Visibility::VISIBLE);

    // Check if structure changed
    bool structureChanged = (completedItems.size() != m_lastCompletedItems.size());
    if (!structureChanged) {
        for (size_t i = 0; i < completedItems.size(); i++) {
            if (completedItems[i].itemId != m_lastCompletedItems[i].itemId ||
                completedItems[i].episodeId != m_lastCompletedItems[i].episodeId) {
                structureChanged = true;
                break;
            }
        }
    }

    // Build new cache
    std::vector<CachedCompletedItem> newCache;
    for (const auto& ci : completedItems) {
        CachedCompletedItem cached;
        cached.itemId = ci.itemId;
        cached.episodeId = ci.episodeId;
        cached.currentTime = ci.currentTime;
        newCache.push_back(cached);
    }

    if (!structureChanged) {
        // Update progress labels in-place
        for (size_t i = 0; i < newCache.size() && i < m_completedRowElements.size(); i++) {
            if (newCache[i].currentTime != m_lastCompletedItems[i].currentTime) {
                if (m_completedRowElements[i].statusLabel) {
                    std::string statusText = "Ready to play";
                    if (newCache[i].currentTime > 0) {
                        int minutes = static_cast<int>(newCache[i].currentTime / 60.0f);
                        statusText += " (" + std::to_string(minutes) + " min listened)";
                    }
                    m_completedRowElements[i].statusLabel->setText(statusText);
                }
            }
        }
        m_lastCompletedItems = newCache;
        return;
    }

    // Full rebuild
    m_lastCompletedItems = newCache;
    m_completedRowElements.clear();
    while (m_completedContainer->getChildren().size() > 0) {
        m_completedContainer->removeView(m_completedContainer->getChildren()[0]);
    }

    for (const auto& ci : completedItems) {
        brls::Label* statusLabel = nullptr;
        auto* row = createCompletedRow(ci.itemId, ci.episodeId, ci.title, ci.authorName,
                                        ci.currentTime, ci.duration,
                                        ci.coverUrl, ci.localCoverPath, ci.mediaType,
                                        statusLabel);
        m_completedContainer->addView(row);

        CompletedRowElements elem;
        elem.row = row;
        elem.statusLabel = statusLabel;
        elem.itemId = ci.itemId;
        elem.episodeId = ci.episodeId;
        m_completedRowElements.push_back(elem);
    }

    updateNavigationRoutes();
}

brls::Box* DownloadsTab::createQueueRow(const std::string& itemId, const std::string& episodeId,
                                         const std::string& title, const std::string& authorName,
                                         int64_t downloadedBytes, int64_t totalBytes, int state,
                                         const std::string& coverUrl, const std::string& localCoverPath,
                                         brls::Label*& outProgressLabel) {
    auto row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setPadding(10);
    row->setMargins(0, 0, 8, 0);
    row->setCornerRadius(8);
    row->setFocusable(true);

    // Background color based on state
    if (state == static_cast<int>(DownloadState::DOWNLOADING)) {
        row->setBackgroundColor(nvgRGBA(30, 60, 30, 200));
    } else if (state == static_cast<int>(DownloadState::FAILED)) {
        row->setBackgroundColor(nvgRGBA(60, 30, 30, 200));
    } else {
        row->setBackgroundColor(nvgRGBA(40, 40, 40, 200));
    }

    // Cover image
    auto coverImage = new brls::Image();
    coverImage->setWidth(50);
    coverImage->setHeight(50);
    coverImage->setCornerRadius(4);
    coverImage->setMargins(0, 12, 0, 0);
    row->addView(coverImage);

    if (!localCoverPath.empty()) {
        loadLocalCoverImage(coverImage, localCoverPath);
    } else if (!coverUrl.empty()) {
        ImageLoader::loadAsync(coverUrl, [](brls::Image*) {}, coverImage);
    }

    // Info column
    auto infoBox = new brls::Box();
    infoBox->setAxis(brls::Axis::COLUMN);
    infoBox->setGrow(1.0f);

    auto titleLabel = new brls::Label();
    titleLabel->setText(title);
    titleLabel->setFontSize(16);
    titleLabel->setSingleLine(true);
    infoBox->addView(titleLabel);

    if (!authorName.empty()) {
        auto authorLabel = new brls::Label();
        authorLabel->setText(authorName);
        authorLabel->setFontSize(13);
        authorLabel->setTextColor(nvgRGBA(180, 180, 180, 255));
        authorLabel->setSingleLine(true);
        infoBox->addView(authorLabel);
    }

    auto progressLabel = new brls::Label();
    progressLabel->setFontSize(13);
    std::string progressText;
    if (state == static_cast<int>(DownloadState::DOWNLOADING)) {
        if (totalBytes > 0) {
            int percent = static_cast<int>((downloadedBytes * 100) / totalBytes);
            progressText = "Downloading... " + std::to_string(percent) + "%";
        } else {
            progressText = "Downloading...";
        }
        progressLabel->setTextColor(nvgRGBA(100, 200, 100, 255));
    } else if (state == static_cast<int>(DownloadState::QUEUED)) {
        progressText = "Queued";
    } else if (state == static_cast<int>(DownloadState::PAUSED)) {
        progressText = "Paused";
        progressLabel->setTextColor(nvgRGBA(200, 150, 100, 255));
    } else if (state == static_cast<int>(DownloadState::FAILED)) {
        progressText = "Failed";
        progressLabel->setTextColor(nvgRGBA(200, 100, 100, 255));
    }
    progressLabel->setText(progressText);
    infoBox->addView(progressLabel);
    outProgressLabel = progressLabel;

    row->addView(infoBox);

    // Cancel action (X button)
    std::string capturedItemId = itemId;
    row->registerAction("Cancel", brls::ControllerButton::BUTTON_X, [this, capturedItemId](brls::View*) {
        DownloadsManager::getInstance().cancelDownload(capturedItemId);
        brls::Application::notify("Download cancelled");
        refresh();
        return true;
    });

    return row;
}

brls::Box* DownloadsTab::createCompletedRow(const std::string& itemId, const std::string& episodeId,
                                              const std::string& title, const std::string& authorName,
                                              float currentTime, float duration,
                                              const std::string& coverUrl, const std::string& localCoverPath,
                                              const std::string& mediaType,
                                              brls::Label*& outStatusLabel) {
    auto row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setPadding(10);
    row->setMargins(0, 0, 8, 0);
    row->setBackgroundColor(nvgRGBA(40, 40, 40, 200));
    row->setCornerRadius(8);
    row->setFocusable(true);

    // Cover image
    auto coverImage = new brls::Image();
    coverImage->setWidth(50);
    coverImage->setHeight(50);
    coverImage->setCornerRadius(4);
    coverImage->setMargins(0, 12, 0, 0);
    row->addView(coverImage);

    if (!localCoverPath.empty()) {
        loadLocalCoverImage(coverImage, localCoverPath);
    } else if (!coverUrl.empty()) {
        ImageLoader::loadAsync(coverUrl, [](brls::Image*) {}, coverImage);
    }

    // Info column
    auto infoBox = new brls::Box();
    infoBox->setAxis(brls::Axis::COLUMN);
    infoBox->setGrow(1.0f);

    auto titleLabel = new brls::Label();
    titleLabel->setText(title);
    titleLabel->setFontSize(16);
    titleLabel->setSingleLine(true);
    infoBox->addView(titleLabel);

    if (!authorName.empty()) {
        auto authorLabel = new brls::Label();
        authorLabel->setText(authorName);
        authorLabel->setFontSize(13);
        authorLabel->setTextColor(nvgRGBA(180, 180, 180, 255));
        authorLabel->setSingleLine(true);
        infoBox->addView(authorLabel);
    }

    auto statusLabel = new brls::Label();
    statusLabel->setFontSize(13);
    std::string statusText = "Ready to play";
    if (currentTime > 0) {
        int minutes = static_cast<int>(currentTime / 60.0f);
        statusText += " (" + std::to_string(minutes) + " min listened)";
    }
    statusLabel->setText(statusText);
    statusLabel->setTextColor(nvgRGBA(100, 180, 220, 255));
    infoBox->addView(statusLabel);
    outStatusLabel = statusLabel;

    row->addView(infoBox);

    // Play action (A button / default click)
    std::string capturedItemId = itemId;
    row->registerClickAction([capturedItemId](brls::View*) {
        brls::Application::pushActivity(new PlayerActivity(capturedItemId, true));
        return true;
    });

    // Delete action (X button)
    row->registerAction("Delete", brls::ControllerButton::BUTTON_X, [this, capturedItemId](brls::View*) {
        DownloadsManager::getInstance().deleteDownload(capturedItemId);
        brls::Application::notify("Download deleted");
        refresh();
        return true;
    });

    return row;
}

void DownloadsTab::startAutoRefresh() {
    if (m_autoRefreshTimerActive.load()) return;
    m_autoRefreshEnabled.store(true);
    m_autoRefreshTimerActive.store(true);

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, aliveWeak]() {
        while (m_autoRefreshEnabled.load()) {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) break;

            int totalItems = static_cast<int>(m_lastQueueItems.size() + m_lastCompletedItems.size());
            int interval = (totalItems > LARGE_QUEUE_THRESHOLD) ?
                           AUTO_REFRESH_INTERVAL_LARGE_MS : AUTO_REFRESH_INTERVAL_MS;

            std::this_thread::sleep_for(std::chrono::milliseconds(interval));

            alive = aliveWeak.lock();
            if (!alive || !*alive) break;
            if (!m_autoRefreshEnabled.load()) break;

            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (m_autoRefreshEnabled.load()) {
                    refresh();
                }
            });
        }
        m_autoRefreshTimerActive.store(false);
    });
}

void DownloadsTab::stopAutoRefresh() {
    m_autoRefreshEnabled.store(false);
}

void DownloadsTab::updateNavigationRoutes() {
    // Let borealis handle default navigation between focusable items
}

void DownloadsTab::showDownloadOptions(const std::string& ratingKey, const std::string& title) {
    // Not implemented - download options shown from media detail view
}

} // namespace vitaabs
