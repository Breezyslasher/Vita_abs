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

    // Left side - poster
    auto* leftBox = new brls::Box();
    leftBox->setAxis(brls::Axis::COLUMN);
    leftBox->setWidth(200);
    leftBox->setMarginRight(30);

    m_posterImage = new brls::Image();
    m_posterImage->setWidth(200);
    m_posterImage->setHeight(300);
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
    if (m_item.mediaType == MediaType::BOOK && m_item.numChapters > 0) {
        auto* chaptersLabel = new brls::Label();
        chaptersLabel->setText("Chapters: " + std::to_string(m_item.numChapters));
        chaptersLabel->setFontSize(18);
        chaptersLabel->setMarginBottom(10);
        m_mainContent->addView(chaptersLabel);
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
    }

    // Load thumbnail
    if (m_posterImage && !m_item.coverPath.empty()) {
        std::string url = client.getCoverUrl(m_item.coverPath, 400, 600);
        ImageLoader::loadAsync(url, [this](brls::Image* image) {
            // Image loaded
        }, m_posterImage);
    }

    // Load children (podcast episodes)
    if (m_item.mediaType == MediaType::PODCAST) {
        loadChildren();
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
    // Check if already downloaded
    if (DownloadsManager::getInstance().isDownloaded(m_item.id)) {
        brls::Application::notify("Already downloaded");
        return;
    }

    // Check if we have audio track info
    if (m_item.audioTracks.empty()) {
        brls::Application::notify("Loading media info...");

        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        MediaItem fullItem;
        if (client.fetchItem(m_item.id, fullItem) && !fullItem.audioTracks.empty()) {
            m_item = fullItem;
        } else {
            brls::Application::notify("Unable to download - media info not available");
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
            [progressDialog](int64_t downloaded, int64_t total) {
                brls::sync([progressDialog, downloaded, total]() {
                    progressDialog->updateDownloadProgress(downloaded, total);
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

                MediaItem fullItem;
                if (client.fetchItem(ep.id, fullItem) && !fullItem.audioTracks.empty()) {
                    if (DownloadsManager::getInstance().queueDownload(
                        fullItem.id,
                        fullItem.title,
                        fullItem.authorName,
                        fullItem.duration,
                        "episode",
                        podcastTitle,
                        fullItem.episodeId
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

            MediaItem fullItem;
            if (client.fetchItem(ep.id, fullItem) && !fullItem.audioTracks.empty()) {
                if (DownloadsManager::getInstance().queueDownload(
                    fullItem.id,
                    fullItem.title,
                    fullItem.authorName,
                    fullItem.duration,
                    "episode",
                    podcastTitle,
                    fullItem.episodeId
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

} // namespace vitaabs
