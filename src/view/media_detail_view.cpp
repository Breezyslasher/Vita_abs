/**
 * VitaABS - Media Detail View implementation
 */

#include "view/media_detail_view.hpp"
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

    // Top row - cover and info
    auto* topRow = new brls::Box();
    topRow->setAxis(brls::Axis::ROW);
    topRow->setJustifyContent(brls::JustifyContent::FLEX_START);
    topRow->setAlignItems(brls::AlignItems::FLEX_START);
    topRow->setMarginBottom(20);

    // Left side - cover image
    auto* leftBox = new brls::Box();
    leftBox->setAxis(brls::Axis::COLUMN);
    leftBox->setWidth(200);
    leftBox->setMarginRight(30);

    m_coverImage = new brls::Image();
    // Square cover art for audiobooks
    m_coverImage->setWidth(200);
    m_coverImage->setHeight(200);
    m_coverImage->setScalingType(brls::ImageScalingType::FIT);
    leftBox->addView(m_coverImage);

    // Play button
    m_playButton = new brls::Button();
    m_playButton->setText("Play");
    m_playButton->setWidth(200);
    m_playButton->setMarginTop(20);
    m_playButton->registerClickAction([this](brls::View* view) {
        onPlay(false);
        return true;
    });
    leftBox->addView(m_playButton);

    // Resume button if there's progress
    if (m_item.progress > 0.0f && m_item.progress < 1.0f) {
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

    // Download button
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

    // Author/Narrator row
    auto* authorBox = new brls::Box();
    authorBox->setAxis(brls::Axis::ROW);
    authorBox->setMarginBottom(10);

    if (!m_item.author.empty()) {
        m_authorLabel = new brls::Label();
        m_authorLabel->setText("By: " + m_item.author);
        m_authorLabel->setFontSize(18);
        m_authorLabel->setMarginRight(15);
        authorBox->addView(m_authorLabel);
    }

    if (!m_item.narrator.empty()) {
        m_narratorLabel = new brls::Label();
        m_narratorLabel->setText("Read by: " + m_item.narrator);
        m_narratorLabel->setFontSize(18);
        authorBox->addView(m_narratorLabel);
    }

    rightBox->addView(authorBox);

    // Metadata row
    auto* metaBox = new brls::Box();
    metaBox->setAxis(brls::Axis::ROW);
    metaBox->setMarginBottom(15);

    if (m_item.year > 0) {
        m_yearLabel = new brls::Label();
        m_yearLabel->setText(std::to_string(m_item.year));
        m_yearLabel->setFontSize(16);
        m_yearLabel->setMarginRight(15);
        metaBox->addView(m_yearLabel);
    }

    if (m_item.duration > 0) {
        m_durationLabel = new brls::Label();
        int totalMinutes = static_cast<int>(m_item.duration / 60.0f);
        int hours = totalMinutes / 60;
        int minutes = totalMinutes % 60;
        std::string durationStr;
        if (hours > 0) {
            durationStr = std::to_string(hours) + "h " + std::to_string(minutes) + "m";
        } else {
            durationStr = std::to_string(minutes) + " min";
        }
        m_durationLabel->setText(durationStr);
        m_durationLabel->setFontSize(16);
        metaBox->addView(m_durationLabel);
    }

    rightBox->addView(metaBox);

    // Series info
    if (!m_item.series.empty()) {
        auto* seriesLabel = new brls::Label();
        std::string seriesText = "Series: " + m_item.series;
        if (m_item.seriesSequence > 0) {
            seriesText += " #" + std::to_string(m_item.seriesSequence);
        }
        seriesLabel->setText(seriesText);
        seriesLabel->setFontSize(16);
        seriesLabel->setMarginBottom(10);
        rightBox->addView(seriesLabel);
    }

    // Genres
    if (!m_item.genres.empty()) {
        auto* genresLabel = new brls::Label();
        std::string genresText = "Genres: ";
        for (size_t i = 0; i < m_item.genres.size(); i++) {
            if (i > 0) genresText += ", ";
            genresText += m_item.genres[i];
        }
        genresLabel->setText(genresText);
        genresLabel->setFontSize(16);
        genresLabel->setMarginBottom(10);
        rightBox->addView(genresLabel);
    }

    // Progress info
    if (m_item.progress > 0.0f) {
        auto* progressLabel = new brls::Label();
        int percent = static_cast<int>(m_item.progress * 100);
        progressLabel->setText("Progress: " + std::to_string(percent) + "%");
        progressLabel->setFontSize(16);
        progressLabel->setMarginBottom(15);
        rightBox->addView(progressLabel);
    }

    // Summary
    if (!m_item.description.empty()) {
        m_summaryLabel = new brls::Label();
        m_summaryLabel->setText(m_item.description);
        m_summaryLabel->setFontSize(16);
        m_summaryLabel->setMarginBottom(20);
        rightBox->addView(m_summaryLabel);
    }

    topRow->addView(rightBox);
    m_mainContent->addView(topRow);

    // Chapters section (for audiobooks)
    if (m_item.mediaType == MediaType::BOOK) {
        auto* chaptersLabel = new brls::Label();
        chaptersLabel->setText("Chapters");
        chaptersLabel->setFontSize(20);
        chaptersLabel->setMarginBottom(10);
        m_mainContent->addView(chaptersLabel);

        m_chaptersBox = new brls::Box();
        m_chaptersBox->setAxis(brls::Axis::COLUMN);
        m_mainContent->addView(m_chaptersBox);
    }

    // Episodes section (for podcasts)
    if (m_item.mediaType == MediaType::PODCAST) {
        auto* episodesLabel = new brls::Label();
        episodesLabel->setText("Episodes");
        episodesLabel->setFontSize(20);
        episodesLabel->setMarginBottom(10);
        m_mainContent->addView(episodesLabel);

        auto* episodesScroll = new brls::HScrollingFrame();
        episodesScroll->setHeight(180);
        episodesScroll->setMarginBottom(20);

        m_episodesBox = new brls::Box();
        m_episodesBox->setAxis(brls::Axis::ROW);
        m_episodesBox->setJustifyContent(brls::JustifyContent::FLEX_START);

        episodesScroll->setContentView(m_episodesBox);
        m_mainContent->addView(episodesScroll);
    }

    m_scrollView->setContentView(m_mainContent);
    this->addView(m_scrollView);

    // Load full details
    loadDetails();
}

brls::View* MediaDetailView::create() {
    return nullptr; // Factory not used
}

void MediaDetailView::loadDetails() {
    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    // Load full item details
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
        if (m_downloadButton) {
            if (DownloadsManager::getInstance().isDownloaded(m_item.id)) {
                m_downloadButton->setText("Downloaded");
            } else {
                m_downloadButton->setText("Download");
            }
        }
    }

    // Load cover image
    if (m_coverImage && !m_item.coverUrl.empty()) {
        std::string coverUrl = client.getCoverUrl(m_item.id, 400, 400);
        ImageLoader::loadAsync(coverUrl, [this](brls::Image* image) {
            // Image loaded
        }, m_coverImage);
    }

    // Load chapters for audiobooks
    if (m_item.mediaType == MediaType::BOOK) {
        loadChapters();
    }

    // Load episodes for podcasts
    if (m_item.mediaType == MediaType::PODCAST) {
        loadEpisodes();
    }
}

void MediaDetailView::loadChapters() {
    if (!m_chaptersBox) return;

    m_chaptersBox->clearViews();

    // Display chapters from the item
    for (size_t i = 0; i < m_item.chapters.size(); i++) {
        const auto& chapter = m_item.chapters[i];

        auto* chapterRow = new brls::Box();
        chapterRow->setAxis(brls::Axis::ROW);
        chapterRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        chapterRow->setAlignItems(brls::AlignItems::CENTER);
        chapterRow->setPadding(10);
        chapterRow->setMarginBottom(5);
        chapterRow->setFocusable(true);

        auto* titleLabel = new brls::Label();
        titleLabel->setText(chapter.title);
        titleLabel->setFontSize(16);
        titleLabel->setGrow(1.0f);
        chapterRow->addView(titleLabel);

        auto* timeLabel = new brls::Label();
        int minutes = static_cast<int>(chapter.start / 60.0f);
        int seconds = static_cast<int>(chapter.start) % 60;
        char timeStr[16];
        snprintf(timeStr, sizeof(timeStr), "%d:%02d", minutes, seconds);
        timeLabel->setText(timeStr);
        timeLabel->setFontSize(14);
        chapterRow->addView(timeLabel);

        // Click to play from chapter
        size_t chapterIndex = i;
        float chapterStart = chapter.start;
        chapterRow->registerClickAction([this, chapterStart](brls::View* view) {
            // Start playback from this chapter
            Application::getInstance().pushPlayerActivity(m_item.id, "", chapterStart);
            return true;
        });

        m_chaptersBox->addView(chapterRow);
    }

    if (m_item.chapters.empty()) {
        auto* noChaptersLabel = new brls::Label();
        noChaptersLabel->setText("No chapters available");
        noChaptersLabel->setFontSize(16);
        m_chaptersBox->addView(noChaptersLabel);
    }
}

void MediaDetailView::loadEpisodes() {
    if (!m_episodesBox) return;

    asyncRun([this]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

        std::vector<MediaItem> episodes;
        if (!client.fetchPodcastEpisodes(m_item.id, episodes)) {
            brls::Logger::error("Failed to fetch podcast episodes");
            return;
        }

        brls::sync([this, episodes]() {
            m_episodesBox->clearViews();

            for (const auto& episode : episodes) {
                auto* cell = new MediaItemCell();
                cell->setItem(episode);
                cell->setWidth(120);
                cell->setHeight(150);
                cell->setMarginRight(10);

                MediaItem capturedEpisode = episode;
                cell->registerClickAction([this, capturedEpisode](brls::View* view) {
                    // Play the episode
                    Application::getInstance().pushPlayerActivity(
                        m_item.id, capturedEpisode.episodeId);
                    return true;
                });

                m_episodesBox->addView(cell);
            }
        });
    });
}

void MediaDetailView::onPlay(bool resume) {
    if (m_item.mediaType == MediaType::BOOK) {
        float startPosition = resume ? m_item.currentTime : 0.0f;
        Application::getInstance().pushPlayerActivity(m_item.id, "", startPosition);
    } else if (m_item.mediaType == MediaType::PODCAST) {
        // For podcasts, show episode selection or play most recent
        if (!m_episodes.empty()) {
            Application::getInstance().pushPlayerActivity(
                m_item.id, m_episodes[0].episodeId);
        }
    } else if (m_item.mediaType == MediaType::PODCAST_EPISODE) {
        Application::getInstance().pushPlayerActivity(m_item.id, m_item.episodeId);
    }
}

void MediaDetailView::onDownload() {
    // Check if already downloaded
    if (DownloadsManager::getInstance().isDownloaded(m_item.id)) {
        brls::Application::notify("Already downloaded");
        return;
    }

    // Queue the download
    bool queued = DownloadsManager::getInstance().queueDownload(
        m_item.id,
        m_item.title,
        "", // audioPath will be constructed by DownloadsManager
        m_item.duration,
        m_item.mediaType == MediaType::PODCAST_EPISODE ? "episode" : "book",
        m_item.author,
        0,
        0
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

        // Track the item ID to update button when done
        std::string itemId = m_item.id;
        brls::Button* downloadBtn = m_downloadButton;

        // Set progress callback
        DownloadsManager::getInstance().setProgressCallback(
            [progressDialog](int64_t downloaded, int64_t total) {
                brls::sync([progressDialog, downloaded, total]() {
                    progressDialog->updateDownloadProgress(downloaded, total);
                });
            }
        );

        // Allow dismissing dialog - download continues in background
        progressDialog->setCancelCallback([progressDialog, downloadBtn]() {
            brls::Application::notify("Download continues in background");
            DownloadsManager::getInstance().setProgressCallback(nullptr);
        });

        // Start downloading
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

                // Sleep briefly before checking again
#ifdef __vita__
                sceKernelDelayThread(500 * 1000);  // 500ms
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

} // namespace vitaabs
