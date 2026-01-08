/**
 * VitaABS - Media Detail View implementation
 */

#include "view/media_detail_view.hpp"
#include "app/audiobookshelf_client.hpp"
#include "view/media_item_cell.hpp"
#include "view/progress_dialog.hpp"
#include "app/application.hpp"
#include "app/downloads_manager.hpp"
#include "app/temp_file_manager.hpp"
#include "utils/image_loader.hpp"
#include "utils/http_client.hpp"
#include "utils/audio_utils.hpp"
#include "utils/async.hpp"
#include <thread>

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
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
        // Hide if saveToDownloads is enabled (files are auto-saved when played)
        AppSettings& settings = Application::getInstance().getSettings();
        if (!settings.saveToDownloads) {
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

        // Hide download button if saveToDownloads is enabled (files are auto-saved when played)
        AppSettings& podcastSettings = Application::getInstance().getSettings();
        if (!podcastSettings.saveToDownloads) {
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
                    // Download then play
                    startDownloadAndPlay(child.podcastId, child.episodeId);
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
        chapterRow->registerClickAction([this, itemId, chapterStart](brls::View*) {
            // Start playback from this chapter's start time (with download)
            startDownloadAndPlay(itemId, "", chapterStart);
            return true;
        });

        m_chaptersBox->addView(chapterRow);
    }
}

void MediaDetailView::onPlay(bool resume) {
    // For podcasts, handle episode selection first
    if (m_item.mediaType == MediaType::PODCAST) {
        std::string podcastId, episodeId;
        if (!m_children.empty()) {
            podcastId = m_children[0].podcastId;
            episodeId = m_children[0].episodeId;
        } else {
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            std::vector<MediaItem> episodes;
            if (client.fetchPodcastEpisodes(m_item.id, episodes) && !episodes.empty()) {
                podcastId = episodes[0].podcastId;
                episodeId = episodes[0].episodeId;
            } else {
                brls::Application::notify("No episodes available");
                return;
            }
        }
        // Start download for podcast episode
        startDownloadAndPlay(podcastId, episodeId);
        return;
    }

    // For books and podcast episodes, start download
    std::string itemId = (m_item.mediaType == MediaType::PODCAST_EPISODE) ? m_item.podcastId : m_item.id;
    std::string episodeId = (m_item.mediaType == MediaType::PODCAST_EPISODE) ? m_item.episodeId : "";

    startDownloadAndPlay(itemId, episodeId);
}

void MediaDetailView::startDownloadAndPlay(const std::string& itemId, const std::string& episodeId,
                                            float requestedStartTime) {
    brls::Logger::info("startDownloadAndPlay: itemId={}, episodeId={}, startTime={}",
                       itemId, episodeId, requestedStartTime);

    // Get settings
    AppSettings& settings = Application::getInstance().getSettings();
    bool useDownloads = settings.saveToDownloads;

    // Initialize managers
    TempFileManager& tempMgr = TempFileManager::getInstance();
    DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
    tempMgr.init();
    downloadsMgr.init();

    // Check if we have a cached version (in temp or downloads)
    std::string cachedPath;
    if (useDownloads) {
        if (downloadsMgr.isDownloaded(itemId)) {
            cachedPath = downloadsMgr.getPlaybackPath(itemId);
        }
    } else {
        cachedPath = tempMgr.getCachedFilePath(itemId, episodeId);
        if (!cachedPath.empty()) {
            tempMgr.touchTempFile(itemId, episodeId);
        }
    }

    // If cached, play immediately
    if (!cachedPath.empty()) {
        brls::Logger::info("Using cached file: {}", cachedPath);
        Application::getInstance().pushPlayerActivityWithFile(itemId, episodeId, cachedPath, requestedStartTime);
        return;
    }

    // Need to download - show progress dialog
    auto* progressDialog = ProgressDialog::showDownloading(m_item.title);

    // Store necessary data for the async operation
    std::string title = m_item.title;
    std::string authorName = m_item.authorName;
    std::string itemType = m_item.type;
    float duration = m_item.duration;

    // Run download in background
    asyncRun([this, progressDialog, itemId, episodeId, title, authorName, itemType, duration, useDownloads, requestedStartTime]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

        // Start playback session
        PlaybackSession session;
        if (!client.startPlaybackSession(itemId, session, episodeId)) {
            brls::Logger::error("Failed to start playback session");
            brls::sync([progressDialog]() {
                progressDialog->setStatus("Failed to start session");
                brls::delay(2000, [progressDialog]() { progressDialog->dismiss(); });
            });
            return;
        }

        brls::Logger::info("Session started: {} tracks", session.audioTracks.size());

        // Determine file extension and multi-file status
        bool isMultiFile = session.audioTracks.size() > 1;
        std::string mimeType = "audio/mpeg";
        if (!session.audioTracks.empty() && !session.audioTracks[0].mimeType.empty()) {
            mimeType = session.audioTracks[0].mimeType;
        }

        std::string ext = ".mp3";
        if (mimeType.find("mp4") != std::string::npos || mimeType.find("m4a") != std::string::npos ||
            mimeType.find("m4b") != std::string::npos) {
            ext = ".m4a";
        } else if (mimeType.find("flac") != std::string::npos) {
            ext = ".flac";
        } else if (mimeType.find("ogg") != std::string::npos) {
            ext = ".ogg";
        }

        std::string finalExt = isMultiFile ? ".m4b" : ext;

        // Determine destination path
        TempFileManager& tempMgr = TempFileManager::getInstance();
        DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
        std::string destPath;

        if (useDownloads) {
            std::string filename = itemId;
            if (!episodeId.empty()) {
                filename += "_" + episodeId;
            }
            filename += finalExt;
            destPath = downloadsMgr.getDownloadsPath() + "/" + filename;
        } else {
            tempMgr.cleanupTempFiles();  // Clean up before downloading
            destPath = tempMgr.getTempFilePath(itemId, episodeId, finalExt);
        }

        // Use requested start time if specified, otherwise use session's current time
        float startTime = (requestedStartTime >= 0) ? requestedStartTime : session.currentTime;

#ifdef __vita__
        HttpClient httpClient;
        httpClient.setTimeout(300);  // 5 minute timeout
        int64_t totalDownloaded = 0;
        bool downloadSuccess = true;

        if (isMultiFile) {
            // Multi-file audiobook: download all tracks, start playing when first is ready
            int numTracks = static_cast<int>(session.audioTracks.size());
            std::vector<std::string> trackFiles;
            std::string currentTrackPath;
            int currentTrackIdx = 0;
            float trackSeekTime = startTime;

            // Find which track contains the current playback position
            for (size_t i = 0; i < session.audioTracks.size(); i++) {
                const AudioTrack& track = session.audioTracks[i];
                float trackEnd = track.startOffset + track.duration;

                if (startTime >= track.startOffset && startTime < trackEnd) {
                    currentTrackIdx = static_cast<int>(i);
                    trackSeekTime = startTime - track.startOffset;
                    brls::Logger::info("Current position {}s is in track {} (offset {}s, seek {}s)",
                                      startTime, currentTrackIdx, track.startOffset, trackSeekTime);
                    break;
                }
            }

            // Download all tracks, starting with the current one
            brls::Logger::info("Downloading {} tracks for multi-file audiobook", numTracks);

            // First, download the current track so we can start playing
            {
                const AudioTrack& track = session.audioTracks[currentTrackIdx];
                std::string trackUrl = client.getStreamUrl(track.contentUrl, "");

                if (trackUrl.empty()) {
                    brls::Logger::error("Failed to get URL for current track {}", currentTrackIdx);
                    downloadSuccess = false;
                } else {
                    std::string trackExt = ext;
                    if (!track.mimeType.empty() && (track.mimeType.find("mp4") != std::string::npos ||
                        track.mimeType.find("m4a") != std::string::npos)) {
                        trackExt = ".m4a";
                    }

                    currentTrackPath = tempMgr.getTempFilePath(itemId + "_track" + std::to_string(currentTrackIdx), "", trackExt);

                    brls::sync([progressDialog, currentTrackIdx, numTracks]() {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "Downloading track %d/%d...", currentTrackIdx + 1, numTracks);
                        progressDialog->setStatus(buf);
                    });

                    SceUID fd = sceIoOpen(currentTrackPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
                    if (fd >= 0) {
                        int64_t totalSize = 0;
                        downloadSuccess = httpClient.downloadFile(trackUrl,
                            [&](const char* data, size_t size) -> bool {
                                int written = sceIoWrite(fd, data, size);
                                if (written < 0) return false;
                                totalDownloaded += size;
                                if (totalSize > 0) {
                                    brls::sync([progressDialog, totalDownloaded, totalSize]() {
                                        progressDialog->updateDownloadProgress(totalDownloaded, totalSize);
                                    });
                                }
                                return true;
                            },
                            [&](int64_t size) { totalSize = size; }
                        );
                        sceIoClose(fd);
                        if (!downloadSuccess) {
                            sceIoRemove(currentTrackPath.c_str());
                        }
                    } else {
                        downloadSuccess = false;
                    }
                }
            }

            if (downloadSuccess) {
                // Start playback immediately with the current track
                float seekTime = trackSeekTime;
                brls::sync([progressDialog, itemId, episodeId, currentTrackPath, seekTime]() {
                    progressDialog->dismiss();
                    Application::getInstance().pushPlayerActivityWithFile(itemId, episodeId, currentTrackPath, seekTime);
                });

                // Now download remaining tracks in background and combine when done
                std::vector<AudioTrack> allTracks = session.audioTracks;
                std::string baseExt = ext;
                std::string finalPath = useDownloads
                    ? downloadsMgr.getDownloadsPath() + "/" + itemId + ".m4b"
                    : tempMgr.getTempFilePath(itemId, episodeId, ".m4b");

                asyncRun([allTracks, currentTrackIdx, currentTrackPath, itemId, episodeId, baseExt, finalPath, title, authorName, duration, itemType, useDownloads]() {
                    brls::Logger::info("Background: Downloading remaining tracks...");

                    HttpClient bgHttpClient;
                    bgHttpClient.setTimeout(300);
                    TempFileManager& bgTempMgr = TempFileManager::getInstance();
                    DownloadsManager& bgDownloadsMgr = DownloadsManager::getInstance();

                    std::vector<std::string> allTrackFiles(allTracks.size());
                    allTrackFiles[currentTrackIdx] = currentTrackPath;

                    // Download all other tracks
                    AudiobookshelfClient& bgClient = AudiobookshelfClient::getInstance();
                    bool allDownloaded = true;

                    for (size_t i = 0; i < allTracks.size() && allDownloaded; i++) {
                        if (static_cast<int>(i) == currentTrackIdx) continue;  // Already downloaded

                        const AudioTrack& track = allTracks[i];
                        std::string trackUrl = bgClient.getStreamUrl(track.contentUrl, "");

                        if (trackUrl.empty()) {
                            brls::Logger::error("Background: Failed to get URL for track {}", i);
                            allDownloaded = false;
                            break;
                        }

                        std::string trackExt = baseExt;
                        if (!track.mimeType.empty() && (track.mimeType.find("mp4") != std::string::npos ||
                            track.mimeType.find("m4a") != std::string::npos)) {
                            trackExt = ".m4a";
                        }

                        std::string trackPath = bgTempMgr.getTempFilePath(itemId + "_track" + std::to_string(i), "", trackExt);
                        allTrackFiles[i] = trackPath;

                        brls::Logger::info("Background: Downloading track {}/{}...", i + 1, allTracks.size());

                        SceUID fd = sceIoOpen(trackPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
                        if (fd < 0) {
                            allDownloaded = false;
                            break;
                        }

                        bool trackOk = bgHttpClient.downloadFile(trackUrl,
                            [&](const char* data, size_t size) -> bool {
                                return sceIoWrite(fd, data, size) >= 0;
                            },
                            nullptr
                        );

                        sceIoClose(fd);

                        if (!trackOk) {
                            brls::Logger::error("Background: Failed to download track {}", i);
                            sceIoRemove(trackPath.c_str());
                            allDownloaded = false;
                        }
                    }

                    // Combine all tracks if all downloaded successfully
                    if (allDownloaded) {
                        brls::Logger::info("Background: Combining {} tracks...", allTracks.size());

                        if (concatenateAudioFiles(allTrackFiles, finalPath)) {
                            brls::Logger::info("Background: Successfully combined into {}", finalPath);

                            // Get total file size
                            SceIoStat stat;
                            int64_t totalSize = 0;
                            if (sceIoGetstat(finalPath.c_str(), &stat) >= 0) {
                                totalSize = stat.st_size;
                            }

                            // Register the combined file
                            if (useDownloads) {
                                bgDownloadsMgr.registerCompletedDownload(itemId, episodeId, title,
                                    authorName, finalPath, totalSize, duration, itemType);
                            } else {
                                bgTempMgr.registerTempFile(itemId, episodeId, finalPath, title, totalSize);
                            }

                            // Clean up individual track files
                            for (const auto& trackFile : allTrackFiles) {
                                sceIoRemove(trackFile.c_str());
                            }

                            brls::sync([]() {
                                brls::Application::notify("Audiobook fully downloaded");
                            });
                        } else {
                            brls::Logger::error("Background: Failed to combine tracks");
                        }
                    } else {
                        // Clean up partial downloads
                        for (const auto& trackFile : allTrackFiles) {
                            if (!trackFile.empty()) {
                                sceIoRemove(trackFile.c_str());
                            }
                        }
                    }
                });

                // Return early - playback already started
                return;
            }

        } else {
            // Single file download
            std::string streamUrl;
            if (!session.audioTracks.empty() && !session.audioTracks[0].contentUrl.empty()) {
                streamUrl = client.getStreamUrl(session.audioTracks[0].contentUrl, "");
            } else {
                streamUrl = client.getDirectStreamUrl(itemId, 0);
            }

            if (streamUrl.empty()) {
                brls::Logger::error("Failed to get stream URL");
                downloadSuccess = false;
            } else {
                SceUID fd = sceIoOpen(destPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
                if (fd < 0) {
                    brls::Logger::error("Failed to create file: {}", destPath);
                    downloadSuccess = false;
                } else {
                    int64_t totalSize = 0;

                    downloadSuccess = httpClient.downloadFile(streamUrl,
                        [&](const char* data, size_t size) -> bool {
                            int written = sceIoWrite(fd, data, size);
                            if (written < 0) return false;
                            totalDownloaded += size;

                            // Update progress with speed display
                            if (totalSize > 0) {
                                brls::sync([progressDialog, totalDownloaded, totalSize]() {
                                    progressDialog->updateDownloadProgress(totalDownloaded, totalSize);
                                });
                            }
                            return true;
                        },
                        [&](int64_t size) {
                            totalSize = size;
                            brls::Logger::info("File size: {} bytes", size);
                        }
                    );

                    sceIoClose(fd);

                    if (!downloadSuccess) {
                        sceIoRemove(destPath.c_str());
                    }
                }
            }
        }

        // Handle result
        if (downloadSuccess) {
            // Register the file
            if (useDownloads) {
                downloadsMgr.registerCompletedDownload(itemId, episodeId, title,
                    authorName, destPath, totalDownloaded, duration, itemType);
            } else {
                tempMgr.registerTempFile(itemId, episodeId, destPath, title, totalDownloaded);
            }

            brls::Logger::info("Download complete: {}", destPath);

            // Push to player with pre-downloaded file
            float seekTime = startTime;
            brls::sync([progressDialog, itemId, episodeId, destPath, seekTime]() {
                progressDialog->dismiss();
                Application::getInstance().pushPlayerActivityWithFile(itemId, episodeId, destPath, seekTime);
            });
        } else {
            brls::sync([progressDialog]() {
                progressDialog->setStatus("Download failed");
                brls::delay(2000, [progressDialog]() { progressDialog->dismiss(); });
            });
        }
#else
        // Non-Vita: just use streaming URL directly
        std::string streamUrl;
        if (!session.audioTracks.empty() && !session.audioTracks[0].contentUrl.empty()) {
            streamUrl = client.getStreamUrl(session.audioTracks[0].contentUrl, "");
        } else {
            streamUrl = client.getDirectStreamUrl(itemId, 0);
        }

        brls::sync([progressDialog, itemId, episodeId, streamUrl, startTime]() {
            progressDialog->dismiss();
            Application::getInstance().pushPlayerActivityWithFile(itemId, episodeId, streamUrl, startTime);
        });
#endif
    });
}

void MediaDetailView::onDownload() {
    brls::Logger::info("onDownload called for item: {} ({})", m_item.title, m_item.id);

    // Check if already downloaded
    if (DownloadsManager::getInstance().isDownloaded(m_item.id)) {
        brls::Application::notify("Already downloaded");
        return;
    }

    // Use the same download logic as streaming but save to downloads
    std::string itemId = (m_item.mediaType == MediaType::PODCAST_EPISODE) ? m_item.podcastId : m_item.id;
    std::string episodeId = (m_item.mediaType == MediaType::PODCAST_EPISODE) ? m_item.episodeId : "";

    startDownloadOnly(itemId, episodeId);
}

void MediaDetailView::startDownloadOnly(const std::string& itemId, const std::string& episodeId) {
    brls::Logger::info("startDownloadOnly: itemId={}, episodeId={}", itemId, episodeId);

    // Check if already downloaded
    DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
    downloadsMgr.init();

    if (downloadsMgr.isDownloaded(itemId)) {
        brls::Application::notify("Already downloaded");
        return;
    }

    // Show progress dialog
    auto* progressDialog = ProgressDialog::showDownloading(m_item.title);
    brls::Button* downloadBtn = m_downloadButton;

    if (downloadBtn) {
        downloadBtn->setText("Downloading...");
    }

    // Store necessary data for the async operation
    std::string title = m_item.title;
    std::string authorName = m_item.authorName;
    std::string itemType = m_item.type;
    float duration = m_item.duration;

    // Run download in background
    asyncRun([this, progressDialog, downloadBtn, itemId, episodeId, title, authorName, itemType, duration]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

        // Start playback session to get audio track info
        PlaybackSession session;
        if (!client.startPlaybackSession(itemId, session, episodeId)) {
            brls::Logger::error("Failed to start playback session for download");
            brls::sync([progressDialog, downloadBtn]() {
                progressDialog->setStatus("Failed to start session");
                if (downloadBtn) downloadBtn->setText("Download");
                brls::delay(2000, [progressDialog]() { progressDialog->dismiss(); });
            });
            return;
        }

        brls::Logger::info("Download session started: {} tracks", session.audioTracks.size());

        // Determine file extension and multi-file status
        bool isMultiFile = session.audioTracks.size() > 1;
        std::string mimeType = "audio/mpeg";
        if (!session.audioTracks.empty() && !session.audioTracks[0].mimeType.empty()) {
            mimeType = session.audioTracks[0].mimeType;
        }

        std::string ext = ".mp3";
        if (mimeType.find("mp4") != std::string::npos || mimeType.find("m4a") != std::string::npos ||
            mimeType.find("m4b") != std::string::npos) {
            ext = ".m4a";
        } else if (mimeType.find("flac") != std::string::npos) {
            ext = ".flac";
        } else if (mimeType.find("ogg") != std::string::npos) {
            ext = ".ogg";
        }

        std::string finalExt = isMultiFile ? ".m4b" : ext;

        // Destination path (always downloads folder)
        DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
        TempFileManager& tempMgr = TempFileManager::getInstance();
        tempMgr.init();

        std::string filename = itemId;
        if (!episodeId.empty()) {
            filename += "_" + episodeId;
        }
        filename += finalExt;
        std::string destPath = downloadsMgr.getDownloadsPath() + "/" + filename;

#ifdef __vita__
        HttpClient httpClient;
        httpClient.setTimeout(300);
        int64_t totalDownloaded = 0;
        bool downloadSuccess = true;

        if (isMultiFile) {
            // Multi-file audiobook: download all tracks and combine
            int numTracks = static_cast<int>(session.audioTracks.size());
            std::vector<std::string> trackFiles(numTracks);

            brls::sync([progressDialog, numTracks]() {
                char buf[64];
                snprintf(buf, sizeof(buf), "Downloading track 1/%d...", numTracks);
                progressDialog->setStatus(buf);
            });

            for (int i = 0; i < numTracks && downloadSuccess; i++) {
                const AudioTrack& track = session.audioTracks[i];
                std::string trackUrl = client.getStreamUrl(track.contentUrl, "");

                if (trackUrl.empty()) {
                    brls::Logger::error("Failed to get URL for track {}", i);
                    downloadSuccess = false;
                    break;
                }

                std::string trackExt = ext;
                if (!track.mimeType.empty() && (track.mimeType.find("mp4") != std::string::npos ||
                    track.mimeType.find("m4a") != std::string::npos)) {
                    trackExt = ".m4a";
                }

                std::string trackPath = tempMgr.getTempFilePath(itemId + "_dltrack" + std::to_string(i), "", trackExt);
                trackFiles[i] = trackPath;

                int trackNum = i + 1;
                brls::sync([progressDialog, trackNum, numTracks]() {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Downloading track %d/%d...", trackNum, numTracks);
                    progressDialog->setStatus(buf);
                });

                SceUID fd = sceIoOpen(trackPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
                if (fd < 0) {
                    downloadSuccess = false;
                    break;
                }

                int64_t trackSize = 0;
                int64_t trackDownloaded = 0;
                bool trackOk = httpClient.downloadFile(trackUrl,
                    [&](const char* data, size_t size) -> bool {
                        int written = sceIoWrite(fd, data, size);
                        if (written < 0) return false;
                        trackDownloaded += size;
                        if (trackSize > 0) {
                            brls::sync([progressDialog, trackDownloaded, trackSize]() {
                                progressDialog->updateDownloadProgress(trackDownloaded, trackSize);
                            });
                        }
                        return true;
                    },
                    [&](int64_t size) { trackSize = size; }
                );

                sceIoClose(fd);

                if (!trackOk) {
                    brls::Logger::error("Failed to download track {}", i);
                    sceIoRemove(trackPath.c_str());
                    downloadSuccess = false;
                }

                totalDownloaded += trackDownloaded;
            }

            // Combine tracks if all downloaded
            if (downloadSuccess) {
                brls::sync([progressDialog]() {
                    progressDialog->setStatus("Combining audio files...");
                });

                if (concatenateAudioFiles(trackFiles, destPath)) {
                    brls::Logger::info("Successfully combined into {}", destPath);
                    // Clean up track files
                    for (const auto& f : trackFiles) {
                        sceIoRemove(f.c_str());
                    }
                } else {
                    brls::Logger::error("Failed to combine tracks");
                    downloadSuccess = false;
                    for (const auto& f : trackFiles) {
                        sceIoRemove(f.c_str());
                    }
                }
            } else {
                // Clean up on failure
                for (const auto& f : trackFiles) {
                    if (!f.empty()) sceIoRemove(f.c_str());
                }
            }

        } else {
            // Single file download
            std::string streamUrl;
            if (!session.audioTracks.empty() && !session.audioTracks[0].contentUrl.empty()) {
                streamUrl = client.getStreamUrl(session.audioTracks[0].contentUrl, "");
            } else {
                streamUrl = client.getDirectStreamUrl(itemId, 0);
            }

            if (streamUrl.empty()) {
                brls::Logger::error("Failed to get stream URL");
                downloadSuccess = false;
            } else {
                SceUID fd = sceIoOpen(destPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
                if (fd < 0) {
                    brls::Logger::error("Failed to create file: {}", destPath);
                    downloadSuccess = false;
                } else {
                    int64_t totalSize = 0;

                    downloadSuccess = httpClient.downloadFile(streamUrl,
                        [&](const char* data, size_t size) -> bool {
                            int written = sceIoWrite(fd, data, size);
                            if (written < 0) return false;
                            totalDownloaded += size;

                            if (totalSize > 0) {
                                brls::sync([progressDialog, totalDownloaded, totalSize]() {
                                    progressDialog->updateDownloadProgress(totalDownloaded, totalSize);
                                });
                            }
                            return true;
                        },
                        [&](int64_t size) {
                            totalSize = size;
                            brls::Logger::info("File size: {} bytes", size);
                        }
                    );

                    sceIoClose(fd);

                    if (!downloadSuccess) {
                        sceIoRemove(destPath.c_str());
                    }
                }
            }
        }

        // Handle result
        if (downloadSuccess) {
            // Get file size
            SceIoStat stat;
            int64_t fileSize = 0;
            if (sceIoGetstat(destPath.c_str(), &stat) >= 0) {
                fileSize = stat.st_size;
            }

            // Register the download
            downloadsMgr.registerCompletedDownload(itemId, episodeId, title,
                authorName, destPath, fileSize, duration, itemType);

            brls::Logger::info("Download complete: {}", destPath);

            brls::sync([progressDialog, downloadBtn]() {
                progressDialog->setStatus("Download complete!");
                if (downloadBtn) downloadBtn->setText("Downloaded");
                brls::delay(1500, [progressDialog]() { progressDialog->dismiss(); });
            });
        } else {
            brls::sync([progressDialog, downloadBtn]() {
                progressDialog->setStatus("Download failed");
                if (downloadBtn) downloadBtn->setText("Download");
                brls::delay(2000, [progressDialog]() { progressDialog->dismiss(); });
            });
        }
#else
        brls::sync([progressDialog, downloadBtn]() {
            progressDialog->setStatus("Downloads only supported on Vita");
            if (downloadBtn) downloadBtn->setText("Download");
            brls::delay(2000, [progressDialog]() { progressDialog->dismiss(); });
        });
#endif
    });
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
