/**
 * VitaABS - Media Detail View implementation
 */

#include "view/media_detail_view.hpp"
#include "app/audiobookshelf_client.hpp"
#include "view/media_item_cell.hpp"
#include "view/progress_dialog.hpp"
#include "app/application.hpp"
#include "app/downloads_manager.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"
#include <thread>

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#endif

namespace vitaabs {

MediaDetailView::MediaDetailView(const MediaItem& item)
    : m_item(item) {

    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setGrow(1.0f);

    // Register back button (B/Circle) to pop this activity
    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    }, false, false, brls::Sound::SOUND_BACK);

    // Create scrollable content
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);

    m_mainContent = new brls::Box();
    m_mainContent->setAxis(brls::Axis::COLUMN);
    m_mainContent->setPadding(30);

    // Top row - poster and info
    auto* topRow = new brls::Box();
    topRow->setAxis(brls::Axis::ROW);
    topRow->setJustifyContent(brls::JustifyContent::FLEX_START);
    topRow->setAlignItems(brls::AlignItems::FLEX_START);
    topRow->setMarginBottom(20);

    // Left side - poster (square for book/podcast covers)
    auto* leftBox = new brls::Box();
    leftBox->setAxis(brls::Axis::COLUMN);
    leftBox->setWidth(200);
    leftBox->setMarginRight(30);

    m_posterImage = new brls::Image();
    m_posterImage->setWidth(200);
    m_posterImage->setHeight(200);  // Square cover
    m_posterImage->setScalingType(brls::ImageScalingType::FIT);
    leftBox->addView(m_posterImage);

    // Play button - for books and podcast episodes
    if (m_item.mediaType == MediaType::BOOK || m_item.mediaType == MediaType::PODCAST_EPISODE) {
        m_playButton = new brls::Button();
        m_playButton->setText("Play");
        m_playButton->setWidth(200);
        m_playButton->setMarginTop(20);
        m_playButton->registerClickAction([this](brls::View* view) {
            onPlay(false);
            return true;
        });
        leftBox->addView(m_playButton);

        if (m_item.currentTime > 0) {
            m_resumeButton = new brls::Button();
            m_resumeButton->setText("Resume");
            m_resumeButton->setWidth(200);
            m_resumeButton->setMarginTop(10);
            m_resumeButton->registerClickAction([this](brls::View* view) {
                onPlay(true);
                return true;
            });
            leftBox->addView(m_resumeButton);
        }

        // Download button for directly playable content
        m_downloadButton = new brls::Button();
        if (DownloadsManager::getInstance().isDownloaded(m_item.id)) {
            m_downloadButton->setText("Downloaded");
        } else {
            m_downloadButton->setText("Download");
        }
        m_downloadButton->setWidth(200);
        m_downloadButton->setMarginTop(10);
        m_downloadButton->registerClickAction([this](brls::View* view) {
            onDownload();
            return true;
        });
        leftBox->addView(m_downloadButton);
    }

    // For podcasts, show download options
    if (m_item.mediaType == MediaType::PODCAST) {
        m_playButton = new brls::Button();
        m_playButton->setText("Play Latest");
        m_playButton->setWidth(200);
        m_playButton->setMarginTop(20);
        m_playButton->registerClickAction([this](brls::View* view) {
            onPlay(false);
            return true;
        });
        leftBox->addView(m_playButton);

        // Find New Episodes button - check RSS for new episodes
        m_findEpisodesButton = new brls::Button();
        m_findEpisodesButton->setText("Find New Episodes");
        m_findEpisodesButton->setWidth(200);
        m_findEpisodesButton->setMarginTop(10);
        m_findEpisodesButton->registerClickAction([this](brls::View* view) {
            findNewEpisodes();
            return true;
        });
        leftBox->addView(m_findEpisodesButton);

        m_downloadButton = new brls::Button();
        m_downloadButton->setText("Download...");
        m_downloadButton->setWidth(200);
        m_downloadButton->setMarginTop(10);
        m_downloadButton->registerClickAction([this](brls::View* view) {
            showDownloadOptions();
            return true;
        });
        leftBox->addView(m_downloadButton);
    }

    topRow->addView(leftBox);

    // Right side - details
    auto* rightBox = new brls::Box();
    rightBox->setAxis(brls::Axis::COLUMN);
    rightBox->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText(m_item.title);
    m_titleLabel->setFontSize(26);
    m_titleLabel->setMarginBottom(10);
    rightBox->addView(m_titleLabel);

    // Author/Series info
    if (!m_item.authorName.empty()) {
        auto* authorLabel = new brls::Label();
        authorLabel->setText("By: " + m_item.authorName);
        authorLabel->setFontSize(18);
        authorLabel->setMarginBottom(10);
        rightBox->addView(authorLabel);
    }

    // Metadata row
    auto* metaBox = new brls::Box();
    metaBox->setAxis(brls::Axis::ROW);
    metaBox->setMarginBottom(15);

    if (!m_item.publishedYear.empty()) {
        m_yearLabel = new brls::Label();
        m_yearLabel->setText(m_item.publishedYear);
        m_yearLabel->setFontSize(16);
        m_yearLabel->setMarginRight(15);
        metaBox->addView(m_yearLabel);
    }

    if (m_item.duration > 0) {
        m_durationLabel = new brls::Label();
        int hours = (int)(m_item.duration / 3600.0f);
        int mins = (int)((m_item.duration - hours * 3600) / 60.0f);
        if (hours > 0) {
            m_durationLabel->setText(std::to_string(hours) + "h " + std::to_string(mins) + "m");
        } else {
            m_durationLabel->setText(std::to_string(mins) + " min");
        }
        m_durationLabel->setFontSize(16);
        metaBox->addView(m_durationLabel);
    }

    rightBox->addView(metaBox);

    // Summary/Description
    if (!m_item.description.empty()) {
        m_summaryLabel = new brls::Label();
        m_summaryLabel->setText(m_item.description);
        m_summaryLabel->setFontSize(16);
        m_summaryLabel->setMarginBottom(20);
        rightBox->addView(m_summaryLabel);
    }

    topRow->addView(rightBox);
    m_mainContent->addView(topRow);

    // Episodes container for podcasts
    if (m_item.mediaType == MediaType::PODCAST) {
        auto* episodesLabel = new brls::Label();
        episodesLabel->setText("Episodes");
        episodesLabel->setFontSize(20);
        episodesLabel->setMarginBottom(10);
        m_mainContent->addView(episodesLabel);

        auto* episodesScroll = new brls::HScrollingFrame();
        episodesScroll->setHeight(180);
        episodesScroll->setMarginBottom(20);

        m_childrenBox = new brls::Box();
        m_childrenBox->setAxis(brls::Axis::ROW);
        m_childrenBox->setJustifyContent(brls::JustifyContent::FLEX_START);

        episodesScroll->setContentView(m_childrenBox);
        m_mainContent->addView(episodesScroll);
    }

    // Chapters container for books
    if (m_item.mediaType == MediaType::BOOK) {
        auto* chaptersLabel = new brls::Label();
        chaptersLabel->setText("Chapters");
        chaptersLabel->setFontSize(20);
        chaptersLabel->setMarginBottom(10);
        chaptersLabel->setMarginTop(10);
        m_mainContent->addView(chaptersLabel);

        // Scrollable chapters list
        m_chaptersScroll = new brls::ScrollingFrame();
        m_chaptersScroll->setHeight(250);
        m_chaptersScroll->setMarginBottom(20);

        m_chaptersBox = new brls::Box();
        m_chaptersBox->setAxis(brls::Axis::COLUMN);

        m_chaptersScroll->setContentView(m_chaptersBox);
        m_mainContent->addView(m_chaptersScroll);
    }

    m_scrollView->setContentView(m_mainContent);
    this->addView(m_scrollView);

    // Load full details
    loadDetails();
}

brls::HScrollingFrame* MediaDetailView::createMediaRow(const std::string& title, brls::Box** contentOut) {
    auto* label = new brls::Label();
    label->setText(title);
    label->setFontSize(20);
    label->setMarginBottom(10);
    label->setMarginTop(15);
    if (m_musicCategoriesBox) {
        m_musicCategoriesBox->addView(label);
    }

    auto* scrollFrame = new brls::HScrollingFrame();
    scrollFrame->setHeight(150);
    scrollFrame->setMarginBottom(10);

    auto* content = new brls::Box();
    content->setAxis(brls::Axis::ROW);
    content->setJustifyContent(brls::JustifyContent::FLEX_START);

    scrollFrame->setContentView(content);
    if (m_musicCategoriesBox) {
        m_musicCategoriesBox->addView(scrollFrame);
    }

    if (contentOut) {
        *contentOut = content;
    }

    return scrollFrame;
}

brls::View* MediaDetailView::create() {
    return nullptr; // Factory not used
}

void MediaDetailView::loadDetails() {
    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    // Load full details
    MediaItem fullItem;
    if (client.fetchItem(m_item.id, fullItem)) {
        m_item = fullItem;

        // Update UI with full details
        if (m_titleLabel && !m_item.title.empty()) {
            m_titleLabel->setText(m_item.title);
        }

        if (m_summaryLabel && !m_item.description.empty()) {
            m_summaryLabel->setText(m_item.description);
        }

        // Update download button state
        if (m_downloadButton && !m_item.audioTracks.empty()) {
            if (DownloadsManager::getInstance().isDownloaded(m_item.id)) {
                m_downloadButton->setText("Downloaded");
            } else {
                m_downloadButton->setText("Download");
            }
        }

        // Create chapters container if it wasn't created in constructor
        // (happens when mediaType wasn't known from library listing)
        if (m_item.mediaType == MediaType::BOOK && !m_chaptersBox && m_mainContent) {
            brls::Logger::debug("Creating chapters container after loading full item details");

            auto* chaptersLabel = new brls::Label();
            chaptersLabel->setText("Chapters");
            chaptersLabel->setFontSize(20);
            chaptersLabel->setMarginBottom(10);
            chaptersLabel->setMarginTop(10);
            m_mainContent->addView(chaptersLabel);

            m_chaptersScroll = new brls::ScrollingFrame();
            m_chaptersScroll->setHeight(250);
            m_chaptersScroll->setMarginBottom(20);

            m_chaptersBox = new brls::Box();
            m_chaptersBox->setAxis(brls::Axis::COLUMN);

            m_chaptersScroll->setContentView(m_chaptersBox);
            m_mainContent->addView(m_chaptersScroll);
        }
    }

    // Load thumbnail - use item ID for cover URL
    if (m_posterImage && !m_item.id.empty()) {
        std::string url = client.getCoverUrl(m_item.id, 400, 400);
        ImageLoader::loadAsync(url, [this](brls::Image* image) {
            // Image loaded
        }, m_posterImage);
    }

    // Load children (podcast episodes)
    if (m_item.mediaType == MediaType::PODCAST) {
        loadChildren();
    }

    // Populate chapters for audiobooks
    if (m_item.mediaType == MediaType::BOOK) {
        brls::Logger::debug("Populating chapters for book: {} chapters available", m_item.chapters.size());
        populateChapters();
    }
}

void MediaDetailView::loadChildren() {
    if (!m_childrenBox) return;

    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    if (client.fetchPodcastEpisodes(m_item.id, m_children)) {
        m_childrenBox->clearViews();

        for (const auto& child : m_children) {
            auto* cell = new MediaItemCell();
            cell->setItem(child);
            cell->setWidth(120);
            cell->setHeight(150);
            cell->setMarginRight(10);

            cell->registerClickAction([this, child](brls::View* view) {
                // Navigate to episode detail or play directly
                if (child.mediaType == MediaType::PODCAST_EPISODE) {
                    Application::getInstance().pushPlayerActivity(child.podcastId, child.episodeId);
                } else {
                    auto* detailView = new MediaDetailView(child);
                    brls::Application::pushActivity(new brls::Activity(detailView));
                }
                return true;
            });

            m_childrenBox->addView(cell);
        }
    }
}

void MediaDetailView::loadMusicCategories() {
    // Not used for Audiobookshelf
}

void MediaDetailView::populateChapters() {
    if (!m_chaptersBox) return;

    m_chaptersBox->clearViews();

    // Check if we have chapters
    if (m_item.chapters.empty()) {
        // Show message if no chapters
        auto* noChaptersLabel = new brls::Label();
        noChaptersLabel->setText("No chapter information available");
        noChaptersLabel->setFontSize(14);
        noChaptersLabel->setTextColor(nvgRGB(150, 150, 150));
        noChaptersLabel->setMarginTop(10);
        m_chaptersBox->addView(noChaptersLabel);
        return;
    }

    // Helper to format time
    auto formatTime = [](float seconds) -> std::string {
        int totalSec = static_cast<int>(seconds);
        int hours = totalSec / 3600;
        int mins = (totalSec % 3600) / 60;
        int secs = totalSec % 60;

        char buf[32];
        if (hours > 0) {
            snprintf(buf, sizeof(buf), "%d:%02d:%02d", hours, mins, secs);
        } else {
            snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);
        }
        return std::string(buf);
    };

    // Current playback position for highlighting
    float currentTime = m_item.currentTime;

    for (size_t i = 0; i < m_item.chapters.size(); ++i) {
        const auto& chapter = m_item.chapters[i];

        auto* chapterRow = new brls::Box();
        chapterRow->setAxis(brls::Axis::ROW);
        chapterRow->setAlignItems(brls::AlignItems::CENTER);
        chapterRow->setHeight(50);
        chapterRow->setMarginBottom(8);
        chapterRow->setPadding(12);
        chapterRow->setCornerRadius(6);
        chapterRow->setFocusable(true);

        // Highlight current chapter
        bool isCurrentChapter = (currentTime >= chapter.start && currentTime < chapter.end);
        if (isCurrentChapter) {
            chapterRow->setBackgroundColor(nvgRGBA(80, 120, 80, 255));
        } else {
            chapterRow->setBackgroundColor(nvgRGBA(50, 50, 50, 255));
        }

        // Chapter number
        auto* numLabel = new brls::Label();
        numLabel->setText(std::to_string(i + 1));
        numLabel->setFontSize(14);
        numLabel->setWidth(35);
        numLabel->setTextColor(nvgRGB(150, 150, 150));
        chapterRow->addView(numLabel);

        // Chapter title
        auto* titleLabel = new brls::Label();
        std::string title = chapter.title;
        if (title.empty()) {
            title = "Chapter " + std::to_string(i + 1);
        }
        // Truncate if too long
        if (title.length() > 45) {
            title = title.substr(0, 42) + "...";
        }
        titleLabel->setText(title);
        titleLabel->setFontSize(15);
        titleLabel->setGrow(1.0f);
        chapterRow->addView(titleLabel);

        // Duration/time
        auto* timeLabel = new brls::Label();
        float duration = chapter.end - chapter.start;
        timeLabel->setText(formatTime(chapter.start) + " (" + formatTime(duration) + ")");
        timeLabel->setFontSize(13);
        timeLabel->setTextColor(nvgRGB(150, 150, 150));
        chapterRow->addView(timeLabel);

        // Click to play from chapter
        std::string itemId = m_item.id;
        float chapterStart = chapter.start;
        chapterRow->registerClickAction([itemId, chapterStart](brls::View*) {
            // Start playback from this chapter's start time
            Application::getInstance().pushPlayerActivity(itemId, "", chapterStart);
            return true;
        });

        m_chaptersBox->addView(chapterRow);
    }
}

void MediaDetailView::onPlay(bool resume) {
    if (m_item.mediaType == MediaType::BOOK) {
        Application::getInstance().pushPlayerActivity(m_item.id);
    } else if (m_item.mediaType == MediaType::PODCAST_EPISODE) {
        Application::getInstance().pushPlayerActivity(m_item.podcastId, m_item.episodeId);
    } else if (m_item.mediaType == MediaType::PODCAST) {
        // Play latest/first episode
        if (!m_children.empty()) {
            const auto& episode = m_children[0];
            Application::getInstance().pushPlayerActivity(episode.podcastId, episode.episodeId);
        } else {
            // Fetch episodes first
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            std::vector<MediaItem> episodes;
            if (client.fetchPodcastEpisodes(m_item.id, episodes) && !episodes.empty()) {
                Application::getInstance().pushPlayerActivity(episodes[0].podcastId, episodes[0].episodeId);
            }
        }
    }
}

void MediaDetailView::onDownload() {
    brls::Logger::info("onDownload called for item: {} ({})", m_item.title, m_item.id);

    // Check if already downloaded
    if (DownloadsManager::getInstance().isDownloaded(m_item.id)) {
        brls::Application::notify("Already downloaded");
        return;
    }

    // Check if we have audio track info
    brls::Logger::debug("Current audioTracks count: {}", m_item.audioTracks.size());

    if (m_item.audioTracks.empty()) {
        brls::Application::notify("Loading media info...");

        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        MediaItem fullItem;
        brls::Logger::debug("Fetching full item details for download...");
        if (client.fetchItem(m_item.id, fullItem)) {
            brls::Logger::debug("Fetched item: {} tracks, {} chapters",
                               fullItem.audioTracks.size(), fullItem.chapters.size());
            if (!fullItem.audioTracks.empty()) {
                m_item = fullItem;
            } else {
                brls::Logger::warning("Fetched item has no audio tracks");
            }
        } else {
            brls::Logger::error("Failed to fetch item details");
            brls::Application::notify("Unable to download - media info not available");
            return;
        }

        // Still no tracks after fetching
        if (m_item.audioTracks.empty()) {
            brls::Logger::error("No audio tracks available for download");
            brls::Application::notify("Unable to download - no audio tracks found");
            return;
        }
    }

    // Determine media type
    std::string mediaType = "book";
    if (m_item.mediaType == MediaType::PODCAST_EPISODE) {
        mediaType = "episode";
    }

    // Queue the download
    bool queued = DownloadsManager::getInstance().queueDownload(
        m_item.id,
        m_item.title,
        m_item.authorName,
        m_item.duration,
        mediaType,
        m_item.seriesName,
        m_item.episodeId
    );

    if (queued) {
        if (m_downloadButton) {
            m_downloadButton->setText("Downloading...");
        }

        // Show progress dialog
        auto* progressDialog = new ProgressDialog("Downloading");
        progressDialog->setStatus(m_item.title);
        progressDialog->setProgress(0);
        progressDialog->show();

        std::string itemId = m_item.id;
        brls::Button* downloadBtn = m_downloadButton;

        // Set progress callback
        DownloadsManager::getInstance().setProgressCallback(
            [progressDialog](float downloaded, float total) {
                brls::sync([progressDialog, downloaded, total]() {
                    progressDialog->updateDownloadProgress(static_cast<int64_t>(downloaded),
                                                           static_cast<int64_t>(total));
                });
            }
        );

        progressDialog->setCancelCallback([progressDialog, downloadBtn]() {
            brls::Application::notify("Download continues in background");
            DownloadsManager::getInstance().setProgressCallback(nullptr);
        });

        DownloadsManager::getInstance().startDownloads();

        // Monitor for completion
        asyncRun([progressDialog, downloadBtn, itemId]() {
            while (true) {
                auto* item = DownloadsManager::getInstance().getDownload(itemId);
                if (!item) break;

                if (item->state == DownloadState::COMPLETED) {
                    brls::sync([progressDialog, downloadBtn]() {
                        progressDialog->setStatus("Download complete!");
                        progressDialog->setProgress(1.0f);
                        if (downloadBtn) {
                            downloadBtn->setText("Downloaded");
                        }
                        brls::delay(1500, [progressDialog]() {
                            progressDialog->dismiss();
                        });
                    });
                    break;
                } else if (item->state == DownloadState::FAILED) {
                    brls::sync([progressDialog, downloadBtn]() {
                        progressDialog->setStatus("Download failed");
                        if (downloadBtn) {
                            downloadBtn->setText("Download");
                        }
                        brls::delay(2000, [progressDialog]() {
                            progressDialog->dismiss();
                        });
                    });
                    break;
                }

#ifdef __vita__
                sceKernelDelayThread(500 * 1000);
#else
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif
            }

            DownloadsManager::getInstance().setProgressCallback(nullptr);
        });
    } else {
        brls::Application::notify("Failed to queue download");
    }
}

void MediaDetailView::showDownloadOptions() {
    auto* dialog = new brls::Dialog("Download Options");

    auto* optionsBox = new brls::Box();
    optionsBox->setAxis(brls::Axis::COLUMN);
    optionsBox->setPadding(20);

    if (m_item.mediaType == MediaType::PODCAST) {
        auto* downloadAllBtn = new brls::Button();
        downloadAllBtn->setText("Download All Episodes");
        downloadAllBtn->setMarginBottom(10);
        downloadAllBtn->registerClickAction([this, dialog](brls::View*) {
            dialog->dismiss();
            downloadAll();
            return true;
        });
        optionsBox->addView(downloadAllBtn);

        auto* downloadUnheardBtn = new brls::Button();
        downloadUnheardBtn->setText("Download Unheard");
        downloadUnheardBtn->setMarginBottom(10);
        downloadUnheardBtn->registerClickAction([this, dialog](brls::View*) {
            dialog->dismiss();
            downloadUnwatched();
            return true;
        });
        optionsBox->addView(downloadUnheardBtn);

        auto* downloadNext5Btn = new brls::Button();
        downloadNext5Btn->setText("Download Next 5 Unheard");
        downloadNext5Btn->setMarginBottom(10);
        downloadNext5Btn->registerClickAction([this, dialog](brls::View*) {
            dialog->dismiss();
            downloadUnwatched(5);
            return true;
        });
        optionsBox->addView(downloadNext5Btn);
    }

    auto* cancelBtn = new brls::Button();
    cancelBtn->setText("Cancel");
    cancelBtn->registerClickAction([dialog](brls::View*) {
        dialog->dismiss();
        return true;
    });
    optionsBox->addView(cancelBtn);

    dialog->addView(optionsBox);
    brls::Application::pushActivity(new brls::Activity(dialog));
}

void MediaDetailView::downloadAll() {
    auto* progressDialog = new ProgressDialog("Preparing Downloads");
    progressDialog->setStatus("Fetching episode list...");
    progressDialog->show();

    std::string podcastId = m_item.id;
    std::string podcastTitle = m_item.title;

    asyncRun([this, progressDialog, podcastId, podcastTitle]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> episodes;
        int queued = 0;

        if (client.fetchPodcastEpisodes(podcastId, episodes)) {
            size_t itemCount = episodes.size();
            brls::sync([progressDialog, itemCount]() {
                progressDialog->setStatus("Found " + std::to_string(itemCount) + " episodes");
                progressDialog->setProgress(0.1f);
            });

            for (size_t i = 0; i < episodes.size(); i++) {
                const auto& ep = episodes[i];

                // For podcast episodes, ep.id is the podcast ID and ep.episodeId is the episode ID
                // We use the episode's own data rather than fetching the parent podcast
                brls::Logger::debug("Queueing episode: {} (podcastId: {}, episodeId: {})",
                                   ep.title, ep.podcastId, ep.episodeId);

                if (!ep.episodeId.empty()) {
                    if (DownloadsManager::getInstance().queueDownload(
                        ep.podcastId,      // Use podcast ID as item ID
                        ep.title,          // Episode title
                        "",                // Author (not applicable for episodes)
                        ep.duration,
                        "episode",         // Media type
                        podcastTitle,      // Parent title
                        ep.episodeId       // Episode ID for the specific episode
                    )) {
                        queued++;
                    }
                }

                size_t currentIndex = i;
                brls::sync([progressDialog, currentIndex, itemCount, queued]() {
                    progressDialog->setStatus("Queued " + std::to_string(queued) + " of " +
                                             std::to_string(itemCount));
                    progressDialog->setProgress(0.1f + 0.9f * static_cast<float>(currentIndex + 1) / itemCount);
                });
            }
        }

        DownloadsManager::getInstance().startDownloads();

        brls::sync([progressDialog, queued]() {
            progressDialog->setStatus("Queued " + std::to_string(queued) + " downloads");
            brls::delay(1500, [progressDialog]() {
                progressDialog->dismiss();
            });
        });
    });
}

void MediaDetailView::downloadUnwatched(int maxCount) {
    auto* progressDialog = new ProgressDialog("Preparing Downloads");
    progressDialog->setStatus("Fetching unheard episodes...");
    progressDialog->show();

    std::string podcastId = m_item.id;
    std::string podcastTitle = m_item.title;

    asyncRun([this, progressDialog, podcastId, podcastTitle, maxCount]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> unheardEpisodes;
        int queued = 0;

        std::vector<MediaItem> allEpisodes;
        if (client.fetchPodcastEpisodes(podcastId, allEpisodes)) {
            for (auto& ep : allEpisodes) {
                if (!ep.isFinished && ep.currentTime == 0) {
                    unheardEpisodes.push_back(ep);
                    if (maxCount > 0 && (int)unheardEpisodes.size() >= maxCount) {
                        break;
                    }
                }
            }
        }

        size_t itemCount = unheardEpisodes.size();
        brls::sync([progressDialog, itemCount]() {
            progressDialog->setStatus("Found " + std::to_string(itemCount) + " unheard");
            progressDialog->setProgress(0.1f);
        });

        if (unheardEpisodes.empty()) {
            brls::sync([progressDialog]() {
                progressDialog->setStatus("No unheard episodes found");
                brls::delay(1500, [progressDialog]() {
                    progressDialog->dismiss();
                });
            });
            return;
        }

        for (size_t i = 0; i < unheardEpisodes.size(); i++) {
            const auto& ep = unheardEpisodes[i];

            // For podcast episodes, ep.id is the podcast ID and ep.episodeId is the episode ID
            brls::Logger::debug("Queueing unheard episode: {} (podcastId: {}, episodeId: {})",
                               ep.title, ep.podcastId, ep.episodeId);

            if (!ep.episodeId.empty()) {
                if (DownloadsManager::getInstance().queueDownload(
                    ep.podcastId,      // Use podcast ID as item ID
                    ep.title,          // Episode title
                    "",                // Author (not applicable for episodes)
                    ep.duration,
                    "episode",         // Media type
                    podcastTitle,      // Parent title
                    ep.episodeId       // Episode ID for the specific episode
                )) {
                    queued++;
                }
            }

            size_t currentIndex = i;
            brls::sync([progressDialog, currentIndex, itemCount, queued]() {
                progressDialog->setStatus("Queued " + std::to_string(queued) + " of " +
                                         std::to_string(itemCount));
                progressDialog->setProgress(0.1f + 0.9f * static_cast<float>(currentIndex + 1) / itemCount);
            });
        }

        DownloadsManager::getInstance().startDownloads();

        brls::sync([progressDialog, queued]() {
            progressDialog->setStatus("Queued " + std::to_string(queued) + " downloads");
            brls::delay(1500, [progressDialog]() {
                progressDialog->dismiss();
            });
        });
    });
}

void MediaDetailView::findNewEpisodes() {
    brls::Application::notify("Checking RSS feed for new episodes...");

    std::string podcastId = m_item.id;
    std::string podcastTitle = m_item.title;

    asyncRun([this, podcastId, podcastTitle]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> newEpisodes;

        if (client.checkNewEpisodes(podcastId, newEpisodes)) {
            brls::sync([this, newEpisodes, podcastId, podcastTitle]() {
                if (newEpisodes.empty()) {
                    brls::Application::notify("No new episodes found");
                } else {
                    showNewEpisodesDialog(newEpisodes, podcastId, podcastTitle);
                }
            });
        } else {
            brls::sync([]() {
                brls::Application::notify("Failed to check for new episodes");
            });
        }
    });
}

void MediaDetailView::showNewEpisodesDialog(const std::vector<MediaItem>& episodes,
                                             const std::string& podcastId,
                                             const std::string& podcastTitle) {
    // Create a dialog to show new episodes
    auto* dialog = new brls::Dialog("New Episodes (" + std::to_string(episodes.size()) + ")");

    // Register circle button to close dialog
    dialog->registerAction("Back", brls::ControllerButton::BUTTON_B, [dialog](brls::View*) {
        dialog->dismiss();
        return true;
    }, true);

    auto* contentBox = new brls::Box();
    contentBox->setAxis(brls::Axis::COLUMN);
    contentBox->setPadding(15);
    contentBox->setWidth(700);

    // Add "Download All" button at the top
    auto* downloadAllBtn = new brls::Button();
    downloadAllBtn->setText("Download All to Server");
    downloadAllBtn->setMarginBottom(15);
    downloadAllBtn->registerClickAction([this, episodes, podcastId, dialog](brls::View*) {
        dialog->dismiss();
        downloadNewEpisodesToServer(podcastId, episodes);
        return true;
    });
    contentBox->addView(downloadAllBtn);

    // Separator
    auto* separator = new brls::Rectangle();
    separator->setHeight(1);
    separator->setColor(nvgRGB(80, 80, 80));
    separator->setMarginBottom(15);
    contentBox->addView(separator);

    // Instructions
    auto* instructionsLabel = new brls::Label();
    instructionsLabel->setText("Select episodes to add to server:");
    instructionsLabel->setFontSize(14);
    instructionsLabel->setTextColor(nvgRGB(180, 180, 180));
    instructionsLabel->setMarginBottom(10);
    contentBox->addView(instructionsLabel);

    // Create scrollable list of episodes
    auto* scrollFrame = new brls::ScrollingFrame();
    scrollFrame->setHeight(350);

    auto* episodesList = new brls::Box();
    episodesList->setAxis(brls::Axis::COLUMN);

    // Store selected episodes
    auto selectedEpisodes = std::make_shared<std::vector<std::string>>();

    for (const auto& ep : episodes) {
        auto* episodeRow = new brls::Box();
        episodeRow->setAxis(brls::Axis::ROW);
        episodeRow->setAlignItems(brls::AlignItems::CENTER);
        episodeRow->setMarginBottom(15);  // More space between episodes
        episodeRow->setPadding(15);
        episodeRow->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
        episodeRow->setCornerRadius(8);
        episodeRow->setFocusable(true);

        // Episode info
        auto* infoBox = new brls::Box();
        infoBox->setAxis(brls::Axis::COLUMN);
        infoBox->setGrow(1.0f);
        infoBox->setJustifyContent(brls::JustifyContent::CENTER);

        // Title - show first line
        auto* titleLabel = new brls::Label();
        std::string title = ep.title;
        std::string title2;

        // Split long titles into two lines
        if (title.length() > 50) {
            size_t splitPos = title.rfind(' ', 50);
            if (splitPos != std::string::npos && splitPos > 20) {
                title2 = title.substr(splitPos + 1);
                title = title.substr(0, splitPos);
                // Truncate second line if still too long
                if (title2.length() > 50) {
                    title2 = title2.substr(0, 47) + "...";
                }
            } else {
                // No good split point, just truncate
                title = title.substr(0, 47) + "...";
            }
        }
        titleLabel->setText(title);
        titleLabel->setFontSize(15);
        infoBox->addView(titleLabel);

        // Second line of title if needed
        if (!title2.empty()) {
            auto* title2Label = new brls::Label();
            title2Label->setText(title2);
            title2Label->setFontSize(14);
            title2Label->setTextColor(nvgRGB(200, 200, 200));
            title2Label->setMarginTop(2);
            infoBox->addView(title2Label);
        }

        if (!ep.pubDate.empty()) {
            auto* dateLabel = new brls::Label();
            dateLabel->setText(ep.pubDate);
            dateLabel->setFontSize(12);
            dateLabel->setTextColor(nvgRGB(150, 150, 150));
            dateLabel->setMarginTop(6);
            infoBox->addView(dateLabel);
        }

        episodeRow->addView(infoBox);

        // Status label (shows "Added!" after adding)
        auto* statusLabel = new brls::Label();
        statusLabel->setText("");
        statusLabel->setFontSize(14);
        statusLabel->setTextColor(nvgRGB(100, 200, 100));
        statusLabel->setWidth(80);
        episodeRow->addView(statusLabel);

        // Click on row to add episode - pass full episode data for new RSS episodes
        MediaItem epCopy = ep;
        std::string pId = podcastId;
        episodeRow->registerClickAction([this, epCopy, pId, statusLabel](brls::View*) {
            // Download this single episode to server with full data
            std::vector<MediaItem> eps = {epCopy};
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            if (client.downloadNewEpisodesToServer(pId, eps)) {
                statusLabel->setText("Added!");
                brls::Application::notify("Episode queued for download on server");
            } else {
                brls::Application::notify("Failed to add episode");
            }
            return true;
        });

        episodesList->addView(episodeRow);
    }

    scrollFrame->setContentView(episodesList);
    contentBox->addView(scrollFrame);

    // Close button
    auto* closeBtn = new brls::Button();
    closeBtn->setText("Close");
    closeBtn->setMarginTop(15);
    closeBtn->registerClickAction([dialog](brls::View*) {
        dialog->dismiss();
        return true;
    });
    contentBox->addView(closeBtn);

    dialog->addView(contentBox);
    brls::Application::pushActivity(new brls::Activity(dialog));
}

void MediaDetailView::downloadNewEpisodesToServer(const std::string& podcastId,
                                                   const std::vector<MediaItem>& episodes) {
    brls::Application::notify("Adding " + std::to_string(episodes.size()) + " episodes to server...");

    asyncRun([podcastId, episodes]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        // Use full episode data for new RSS episodes
        bool success = client.downloadNewEpisodesToServer(podcastId, episodes);

        brls::sync([success, episodes]() {
            if (success) {
                brls::Application::notify("Queued " + std::to_string(episodes.size()) + " episodes for download");
            } else {
                brls::Application::notify("Failed to queue episodes");
            }
        });
    });
}

} // namespace vitaabs
