/**
 * VitaABS - Player Activity implementation
 */

#include "activity/player_activity.hpp"
#include "app/audiobookshelf_client.hpp"
#include "app/application.hpp"
#include "app/downloads_manager.hpp"
#include "app/temp_file_manager.hpp"
#include "player/mpv_player.hpp"
#include "player/streaming_buffer.hpp"
#include "utils/http_client.hpp"
#include "utils/image_loader.hpp"
#include "utils/audio_utils.hpp"
#include "view/video_view.hpp"
#include "view/progress_dialog.hpp"

#include <cstdio>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <fstream>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

namespace vitaabs {

// Helper function to check if content should be marked as finished based on settings
static bool shouldMarkAsFinished(float currentTime, float totalDuration, bool isPodcast) {
    if (totalDuration <= 0) return false;

    // For podcasts, use the podcast auto-complete setting
    if (isPodcast) {
        AppSettings& settings = Application::getInstance().getSettings();
        AutoCompleteThreshold threshold = settings.podcastAutoComplete;

        switch (threshold) {
            case AutoCompleteThreshold::DISABLED:
                return false;
            case AutoCompleteThreshold::LAST_10_SEC:
                return (totalDuration - currentTime) <= 10.0f;
            case AutoCompleteThreshold::LAST_30_SEC:
                return (totalDuration - currentTime) <= 30.0f;
            case AutoCompleteThreshold::LAST_60_SEC:
                return (totalDuration - currentTime) <= 60.0f;
            case AutoCompleteThreshold::PERCENT_90:
                return currentTime >= totalDuration * 0.90f;
            case AutoCompleteThreshold::PERCENT_95:
                return currentTime >= totalDuration * 0.95f;
            case AutoCompleteThreshold::PERCENT_99:
                return currentTime >= totalDuration * 0.99f;
            default:
                return currentTime >= totalDuration * 0.95f;
        }
    }

    // For audiobooks, use default 95% threshold
    return currentTime >= totalDuration * 0.95f;
}

PlayerActivity::PlayerActivity(const std::string& itemId)
    : m_itemId(itemId), m_isLocalFile(false) {
    brls::Logger::debug("PlayerActivity created for item: {}", itemId);
}

PlayerActivity::PlayerActivity(const std::string& itemId, const std::string& episodeId,
                               float startTime)
    : m_itemId(itemId), m_episodeId(episodeId), m_isLocalFile(false) {
    brls::Logger::debug("PlayerActivity created for item: {}, episode: {}", itemId, episodeId);
    // If startTime is specified (>= 0), use it as pending seek position
    if (startTime >= 0) {
        m_pendingSeek = static_cast<double>(startTime);
        brls::Logger::debug("Starting at position: {}s", startTime);
    }
}

PlayerActivity::PlayerActivity(const std::string& itemId, bool isLocalFile)
    : m_itemId(itemId), m_isLocalFile(isLocalFile) {
    brls::Logger::debug("PlayerActivity created for {} item: {}",
                       isLocalFile ? "local" : "remote", itemId);
}

PlayerActivity::PlayerActivity(const std::string& itemId, const std::string& episodeId,
                               const std::string& preDownloadedPath, float startTime)
    : m_itemId(itemId), m_episodeId(episodeId), m_isLocalFile(false), m_isPreDownloaded(true) {
    m_tempFilePath = preDownloadedPath;
    if (startTime >= 0) {
        m_pendingSeek = static_cast<double>(startTime);
        brls::Logger::info("PlayerActivity: Will resume from {}s", startTime);
    }
    brls::Logger::debug("PlayerActivity created with pre-downloaded file: {}", preDownloadedPath);
}

PlayerActivity* PlayerActivity::createForDirectFile(const std::string& filePath) {
    PlayerActivity* activity = new PlayerActivity("", false);
    activity->m_isDirectFile = true;
    activity->m_directFilePath = filePath;
    brls::Logger::debug("PlayerActivity created for direct file: {}", filePath);
    return activity;
}

brls::View* PlayerActivity::createContentView() {
    return brls::View::createFromXMLResource("activity/player.xml");
}

void PlayerActivity::onContentAvailable() {
    brls::Logger::debug("PlayerActivity content available");

    // Load media details
    loadMedia();

    // Set up progress slider
    if (progressSlider) {
        progressSlider->setProgress(0.0f);
        progressSlider->getProgressEvent()->subscribe([this](float progress) {
            // Seek to position
            MpvPlayer& player = MpvPlayer::getInstance();
            double duration = player.getDuration();
            player.seekTo(duration * progress);
        });
    }

    // Set up button click handlers
    if (btnPlayPause) {
        btnPlayPause->registerClickAction([this](brls::View* view) {
            togglePlayPause();
            return true;
        });
        btnPlayPause->setFocusable(true);
    }

    // Get seek interval from settings
    AppSettings& settings = Application::getInstance().getSettings();
    int seekInterval = settings.seekInterval;

    // Update skip button labels
    if (rewindLabel) {
        rewindLabel->setText("-" + std::to_string(seekInterval));
    }
    if (forwardLabel) {
        forwardLabel->setText("+" + std::to_string(seekInterval));
    }

    if (btnRewind) {
        btnRewind->registerClickAction([this, seekInterval](brls::View* view) {
            seek(-seekInterval);
            return true;
        });
    }

    if (btnForward) {
        btnForward->registerClickAction([this, seekInterval](brls::View* view) {
            seek(seekInterval);
            return true;
        });
    }

    // Register controller actions
    this->registerAction("Play/Pause", brls::ControllerButton::BUTTON_A, [this](brls::View* view) {
        togglePlayPause();
        return true;
    });

    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });

    std::string rewindAction = "Rewind " + std::to_string(seekInterval) + "s";
    this->registerAction(rewindAction, brls::ControllerButton::BUTTON_LB, [this, seekInterval](brls::View* view) {
        seek(-seekInterval);
        return true;
    });

    std::string forwardAction = "Forward " + std::to_string(seekInterval) + "s";
    this->registerAction(forwardAction, brls::ControllerButton::BUTTON_RB, [this, seekInterval](brls::View* view) {
        seek(seekInterval);
        return true;
    });

    // Set up speed button
    if (btnSpeed) {
        btnSpeed->registerClickAction([this](brls::View* view) {
            cyclePlaybackSpeed();
            return true;
        });
    }

    // Initialize speed label from settings
    updateSpeedLabel();

    // Start update timer
    m_updateTimer.setCallback([this]() {
        updateProgress();
    });
    m_updateTimer.start(1000); // Update every second
}

void PlayerActivity::willDisappear(bool resetState) {
    brls::Activity::willDisappear(resetState);

    // Mark as destroying to prevent timer callbacks
    m_destroying = true;

    // Stop update timer first
    m_updateTimer.stop();

    // Stop and clean up streaming buffer (deletes temp cache file)
    if (m_streamingBuffer) {
        brls::Logger::debug("PlayerActivity: Cleaning up streaming buffer");
        m_streamingBuffer->cancel();  // This deletes the temp file
        m_streamingBuffer.reset();
    }

    // Hide video view
    if (videoView) {
        videoView->setVideoVisible(false);
    }

    // For photos, nothing to stop
    if (m_isPhoto) {
        return;
    }

    // Stop playback and save progress
    MpvPlayer& player = MpvPlayer::getInstance();

    // Only try to save progress if player is in a valid state
    if (player.isInitialized() && (player.isPlaying() || player.isPaused())) {
        double position = player.getPosition();
        if (position > 0) {
            float currentTime = (float)position;
            float totalDuration = (float)player.getDuration();

            if (m_isLocalFile) {
                // Save progress for downloaded media (in seconds)
                DownloadsManager::getInstance().updateProgress(m_itemId, currentTime, m_episodeId);
                DownloadsManager::getInstance().saveState();
                brls::Logger::info("PlayerActivity: Saved local progress {}s for {} (episode: {})",
                                  currentTime, m_itemId, m_episodeId.empty() ? "none" : m_episodeId);

                // Also sync to server if online
                AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
                if (client.isAuthenticated()) {
                    bool isPodcast = !m_episodeId.empty();
                    bool isFinished = shouldMarkAsFinished(currentTime, totalDuration, isPodcast);
                    client.updateProgress(m_itemId, currentTime, totalDuration, isFinished, m_episodeId);
                    brls::Logger::info("PlayerActivity: Synced local progress to server");
                }
            } else {
                // Close playback session with final position
                if (!m_sessionId.empty()) {
                    float timeListened = currentTime - m_lastSyncedTime;
                    if (timeListened < 0) timeListened = 0;
                    AudiobookshelfClient::getInstance().closePlaybackSession(
                        m_sessionId, currentTime, totalDuration, timeListened);
                    brls::Logger::info("PlayerActivity: Closed session {} at {}s", m_sessionId, currentTime);
                } else {
                    // Fallback to progress update if no session
                    AudiobookshelfClient::getInstance().updateProgress(m_itemId, currentTime, totalDuration, false, m_episodeId);
                }
            }
        }
    }

    // Stop playback (safe to call even if not playing)
    if (player.isInitialized()) {
        player.stop();
    }

    m_isPlaying = false;

    // Clear temp file path (we don't use temp caching anymore)
    m_tempFilePath.clear();
}

void PlayerActivity::loadMedia() {
    // Prevent rapid re-entry
    if (m_loadingMedia) {
        brls::Logger::debug("PlayerActivity: Already loading media, skipping");
        return;
    }
    m_loadingMedia = true;

    // Handle pre-downloaded file (downloaded in media detail view before player push)
    if (m_isPreDownloaded && !m_tempFilePath.empty()) {
        brls::Logger::info("PlayerActivity: Playing pre-downloaded file: {}", m_tempFilePath);

        // First try to get metadata from downloads manager (works offline)
        DownloadsManager& downloads = DownloadsManager::getInstance();

        // Find the matching download item (works for both books and podcast episodes)
        std::string offlineTitle, offlineAuthor, offlineCoverPath, offlineCoverUrl;
        bool foundInDownloads = false;

        auto allDownloads = downloads.getDownloads();
        for (const auto& dl : allDownloads) {
            if (dl.itemId == m_itemId && dl.state == DownloadState::COMPLETED) {
                // For episodes, also match episodeId
                if (m_episodeId.empty() || dl.episodeId == m_episodeId) {
                    offlineTitle = dl.title;
                    offlineAuthor = dl.authorName;
                    offlineCoverPath = dl.localCoverPath;
                    offlineCoverUrl = dl.coverUrl;
                    foundInDownloads = true;
                    break;
                }
            }
        }

        bool metadataLoaded = false;
        if (foundInDownloads) {
            brls::Logger::info("PlayerActivity: Using offline metadata from downloads manager");
            if (titleLabel && !offlineTitle.empty()) {
                titleLabel->setText(offlineTitle);
                metadataLoaded = true;
            }
            if (authorLabel && !offlineAuthor.empty()) {
                authorLabel->setText(offlineAuthor);
            }
            // Load local cover if available, otherwise try server
            if (!offlineCoverPath.empty()) {
                brls::Logger::info("PlayerActivity: Loading local cover: {}", offlineCoverPath);
                loadCoverArt(offlineCoverPath);
            } else if (!offlineCoverUrl.empty()) {
                loadCoverArt(offlineCoverUrl);
            }
        }

        // If no metadata from downloads, try server (when online)
        if (!metadataLoaded) {
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            MediaItem item;
            if (client.fetchItem(m_itemId, item)) {
                if (titleLabel) titleLabel->setText(item.title);
                if (authorLabel && !item.authorName.empty()) authorLabel->setText(item.authorName);
                if (!item.coverPath.empty()) {
                    std::string fullCoverUrl = client.getCoverUrl(m_itemId);
                    loadCoverArt(fullCoverUrl);
                }
            } else {
                brls::Logger::warning("PlayerActivity: Could not fetch metadata (offline or error)");
            }
        }

        // Initialize and load player
        MpvPlayer& player = MpvPlayer::getInstance();
        if (!player.isInitialized()) {
            if (!player.init()) {
                brls::Logger::error("Failed to initialize MPV player");
                m_loadingMedia = false;
                return;
            }
        }

        std::string title = titleLabel ? titleLabel->getFullText() : m_itemId;
        // Pass start time directly to loadUrl for more reliable seeking
        double startTime = m_pendingSeek;
        m_pendingSeek = 0.0;  // Clear pending seek since we're handling it via loadUrl
        brls::Logger::info("PlayerActivity: Loading pre-downloaded file with startTime={}s", startTime);
        if (!player.loadUrl(m_tempFilePath, title, startTime)) {
            brls::Logger::error("Failed to load pre-downloaded file: {}", m_tempFilePath);
            m_loadingMedia = false;
            return;
        }

        // Apply saved playback speed
        AppSettings& preDownloadSettings = Application::getInstance().getSettings();
        float preDownloadSpeed = getSpeedValue(static_cast<int>(preDownloadSettings.playbackSpeed));
        if (preDownloadSpeed != 1.0f) {
            player.setSpeed(preDownloadSpeed);
        }

        if (videoView) {
            videoView->setVisibility(brls::Visibility::VISIBLE);
            videoView->setVideoVisible(true);
        }

        m_isPlaying = true;
        m_loadingMedia = false;
        return;
    }

    // Handle direct file playback (debug/testing)
    if (m_isDirectFile) {
        brls::Logger::info("PlayerActivity: Playing direct file: {}", m_directFilePath);

        // Extract filename from path for title
        size_t lastSlash = m_directFilePath.find_last_of("/\\");
        std::string filename = (lastSlash != std::string::npos)
            ? m_directFilePath.substr(lastSlash + 1)
            : m_directFilePath;

        if (titleLabel) {
            titleLabel->setText(filename);
        }

        if (authorLabel) {
            authorLabel->setText("Local File");
        }

        MpvPlayer& player = MpvPlayer::getInstance();

        if (!player.isInitialized()) {
            if (!player.init()) {
                brls::Logger::error("Failed to initialize MPV player");
                m_loadingMedia = false;
                return;
            }
        }

        // Load direct file
        if (!player.loadUrl(m_directFilePath, "Test File")) {
            brls::Logger::error("Failed to load direct file: {}", m_directFilePath);
            m_loadingMedia = false;
            return;
        }

        // Show video view
        if (videoView) {
            videoView->setVisibility(brls::Visibility::VISIBLE);
            videoView->setVideoVisible(true);
        }

        m_isPlaying = true;
        m_loadingMedia = false;
        return;
    }

    // Handle local file playback (downloaded media)
    if (m_isLocalFile) {
        DownloadsManager& downloads = DownloadsManager::getInstance();

        // Try to fetch latest progress from server before playing (if online)
        // This ensures we have the most up-to-date resume position
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        if (client.isAuthenticated()) {
            brls::Logger::info("PlayerActivity: Fetching latest progress from server for {}", m_itemId);
            downloads.fetchProgressFromServer(m_itemId, m_episodeId);
        }

        // Get download item (using episodeId for podcast episodes)
        DownloadItem* download = downloads.getDownload(m_itemId, m_episodeId);

        if (!download || download->state != DownloadState::COMPLETED) {
            brls::Logger::error("PlayerActivity: Downloaded media not found or incomplete");
            m_loadingMedia = false;
            return;
        }

        // Get the playback path (handles multi-file audiobooks)
        std::string playbackPath = downloads.getPlaybackPath(m_itemId);
        if (playbackPath.empty()) {
            brls::Logger::error("PlayerActivity: Could not get playback path for: {}", m_itemId);
            m_loadingMedia = false;
            return;
        }

        brls::Logger::info("PlayerActivity: Playing local file: {}", playbackPath);

        // Set title
        if (titleLabel) {
            titleLabel->setText(download->title);
        }

        // Set author/parent title
        if (authorLabel) {
            if (!download->authorName.empty()) {
                authorLabel->setText(download->authorName);
            } else if (!download->parentTitle.empty()) {
                authorLabel->setText(download->parentTitle);
            }
        }

        // Load cover art if available
        if (!download->coverUrl.empty()) {
            loadCoverArt(download->coverUrl);
        }

        MpvPlayer& player = MpvPlayer::getInstance();

        if (!player.isInitialized()) {
            if (!player.init()) {
                brls::Logger::error("Failed to initialize MPV player");
                m_loadingMedia = false;
                return;
            }
        }

        // Calculate start time from saved viewOffset
        double startTime = (download->viewOffset > 0) ? download->viewOffset / 1000.0 : -1.0;
        brls::Logger::info("PlayerActivity: Loading local file with startTime={}s", startTime);

        // Load local file (using playback path for multi-file support)
        if (!player.loadUrl(playbackPath, download->title, startTime)) {
            brls::Logger::error("Failed to load local file: {}", playbackPath);
            m_loadingMedia = false;
            return;
        }

        // Apply saved playback speed
        AppSettings& localSettings = Application::getInstance().getSettings();
        float localSpeed = getSpeedValue(static_cast<int>(localSettings.playbackSpeed));
        if (localSpeed != 1.0f) {
            player.setSpeed(localSpeed);
        }

        // Show video view
        if (videoView) {
            videoView->setVisibility(brls::Visibility::VISIBLE);
            videoView->setVideoVisible(true);
        }

        m_isPlaying = true;
        m_loadingMedia = false;
        return;
    }

    // Before attempting remote playback, check if content is downloaded
    // This handles the case when coming from library view with a downloaded episode
    {
        DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
        downloadsMgr.init();

        // Check if this item (or episode) is downloaded
        if (downloadsMgr.isDownloaded(m_itemId, m_episodeId)) {
            brls::Logger::info("PlayerActivity: Item is downloaded, using local playback");

            // Try to fetch latest progress from server before playing (if online)
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            if (client.isAuthenticated()) {
                brls::Logger::info("PlayerActivity: Fetching latest progress from server for downloaded item");
                downloadsMgr.fetchProgressFromServer(m_itemId, m_episodeId);
            }

            // Find the download info (after potential server update)
            auto allDownloads = downloadsMgr.getDownloads();
            for (const auto& dl : allDownloads) {
                if (dl.itemId == m_itemId && dl.state == DownloadState::COMPLETED) {
                    if (m_episodeId.empty() || dl.episodeId == m_episodeId) {
                        // Found the matching download - use local playback
                        std::string playbackPath = dl.localPath;

                        // Set metadata
                        if (titleLabel && !dl.title.empty()) {
                            titleLabel->setText(dl.title);
                        }
                        if (authorLabel && !dl.authorName.empty()) {
                            authorLabel->setText(dl.authorName);
                        }

                        // Load cover (prefer local, fall back to URL)
                        if (!dl.localCoverPath.empty()) {
                            loadCoverArt(dl.localCoverPath);
                        } else if (!dl.coverUrl.empty()) {
                            loadCoverArt(dl.coverUrl);
                        }

                        // Initialize player
                        MpvPlayer& player = MpvPlayer::getInstance();
                        if (!player.isInitialized()) {
                            if (!player.init()) {
                                brls::Logger::error("Failed to initialize MPV player");
                                m_loadingMedia = false;
                                return;
                            }
                        }

                        // Calculate start time from saved position
                        double startTime = -1.0;
                        if (dl.currentTime > 0) {
                            startTime = dl.currentTime;
                        } else if (dl.viewOffset > 0) {
                            startTime = dl.viewOffset / 1000.0;
                        }

                        // Load local file with start time
                        brls::Logger::info("PlayerActivity: Loading downloaded file: {} (startTime={}s)", playbackPath, startTime);
                        if (!player.loadUrl(playbackPath, dl.title, startTime)) {
                            brls::Logger::error("Failed to load downloaded file: {}", playbackPath);
                            m_loadingMedia = false;
                            return;
                        }

                        // Apply saved playback speed
                        AppSettings& dlSettings = Application::getInstance().getSettings();
                        float dlSpeed = getSpeedValue(static_cast<int>(dlSettings.playbackSpeed));
                        if (dlSpeed != 1.0f) {
                            player.setSpeed(dlSpeed);
                        }

                        // Show video view
                        if (videoView) {
                            videoView->setVisibility(brls::Visibility::VISIBLE);
                            videoView->setVideoVisible(true);
                        }

                        // Mark as local file for progress saving
                        m_isLocalFile = true;

                        m_isPlaying = true;
                        m_loadingMedia = false;
                        return;
                    }
                }
            }
        }
    }

    // Remote playback from Audiobookshelf server
    // Supports HTTP streaming for single-file audiobooks (default)
    // Multi-file audiobooks still require download + concatenation
    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
    MediaItem item;

    if (client.fetchItem(m_itemId, item)) {
        // Set title
        if (titleLabel) {
            titleLabel->setText(item.title);
        }

        // Set author
        if (authorLabel && !item.authorName.empty()) {
            authorLabel->setText(item.authorName);
        }

        // Load cover art
        if (!item.coverPath.empty()) {
            std::string fullCoverUrl = client.getCoverUrl(m_itemId);
            loadCoverArt(fullCoverUrl);
        }

        // Start a playback session with Audiobookshelf
        PlaybackSession session;
        brls::Logger::info("PlayerActivity: Starting playback session for item: {}, episode: {}",
                          m_itemId, m_episodeId.empty() ? "(none)" : m_episodeId);

        if (!client.startPlaybackSession(m_itemId, session, m_episodeId)) {
            brls::Logger::error("Failed to start playback session for: {}", m_itemId);
            m_loadingMedia = false;
            return;
        }

        // Store session ID for periodic sync
        m_sessionId = session.id;

        brls::Logger::info("PlayerActivity: Session created - id: {}, audioTracks: {}, playMethod: {}",
                          session.id, session.audioTracks.size(), session.playMethod);

        // Check if this is a multi-file audiobook
        bool isMultiFile = session.audioTracks.size() > 1;
        brls::Logger::info("PlayerActivity: Audiobook has {} tracks (multifile={})",
                          session.audioTracks.size(), isMultiFile ? "yes" : "no");

        // Get mime type from first track
        std::string mimeType = "audio/mpeg";
        if (!session.audioTracks.empty() && !session.audioTracks[0].mimeType.empty()) {
            mimeType = session.audioTracks[0].mimeType;
        }

        float startTime = session.currentTime;
        brls::Logger::debug("PlayerActivity: Will resume from position: {}s", startTime);

        // Determine file extension from mime type
        std::string ext = ".mp3";
        if (mimeType.find("mp4") != std::string::npos || mimeType.find("m4a") != std::string::npos ||
            mimeType.find("m4b") != std::string::npos) {
            ext = ".m4a";
        } else if (mimeType.find("flac") != std::string::npos) {
            ext = ".flac";
        } else if (mimeType.find("ogg") != std::string::npos) {
            ext = ".ogg";
        }

        // For multi-file, use mp4 container only for m4a sources
        // For mp3/ogg/flac sources, keep the same format (Vita FFmpeg lacks mp4 muxer)
        std::string finalExt = ext;
        if (isMultiFile && ext == ".m4a") {
            finalExt = ".m4b";  // Only use m4b for m4a sources
        }

        // Get settings
        AppSettings& settings = Application::getInstance().getSettings();

        // Initialize downloads manager
        DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
        downloadsMgr.init();

        // Debug logging
        brls::Logger::info("PlayerActivity: useHttpStreaming={}, checkDownloadsFirst={}, backgroundDownload={}",
                          settings.useHttpStreaming, settings.checkDownloadsFirst, settings.backgroundDownload);

        // ================================================================
        // STEP 1: Check if file is already downloaded (if setting enabled)
        // ================================================================
        if (settings.checkDownloadsFirst) {
            if (downloadsMgr.isDownloaded(m_itemId, m_episodeId)) {
                std::string downloadedPath = downloadsMgr.getPlaybackPath(m_itemId);
                if (!downloadedPath.empty()) {
                    brls::Logger::info("PlayerActivity: Using downloaded file: {}", downloadedPath);

                    if (chapterInfoLabel) {
                        chapterInfoLabel->setText("Playing downloaded file...");
                    }

                    // Play from downloaded file
                    MpvPlayer& player = MpvPlayer::getInstance();
                    if (!player.isInitialized()) {
                        if (!player.init()) {
                            brls::Logger::error("Failed to initialize MPV player");
                            m_loadingMedia = false;
                            return;
                        }
                    }

                    if (!player.loadUrl(downloadedPath, item.title, startTime > 0 ? static_cast<double>(startTime) : -1.0)) {
                        brls::Logger::error("Failed to load downloaded file: {}", downloadedPath);
                        m_loadingMedia = false;
                        return;
                    }

                    // Apply saved playback speed
                    float speed = getSpeedValue(static_cast<int>(settings.playbackSpeed));
                    if (speed != 1.0f) {
                        player.setSpeed(speed);
                    }

                    if (videoView) {
                        videoView->setVisibility(brls::Visibility::VISIBLE);
                        videoView->setVideoVisible(true);
                    }

                    m_isPlaying = true;
                    m_isLocalFile = true;
                    m_loadingMedia = false;
                    return;  // Early return - playing downloaded file
                }
            }
        }

        // ================================================================
        // STEP 2: HTTP Streaming Mode
        // ================================================================
        if (settings.useHttpStreaming) {
            // Initialize player for streaming
            MpvPlayer& player = MpvPlayer::getInstance();

            if (!player.isInitialized()) {
                brls::Logger::info("PlayerActivity: Initializing MPV player for streaming...");
                if (!player.init()) {
                    brls::Logger::error("Failed to initialize MPV player");
                    m_loadingMedia = false;
                    return;
                }
            }

            if (isMultiFile) {
                // ================================================================
                // MULTI-FILE STREAMING - Use playlist to stream tracks sequentially
                // ================================================================
                brls::Logger::info("PlayerActivity: Multi-file streaming mode ({} tracks)", session.audioTracks.size());

                if (chapterInfoLabel) {
                    chapterInfoLabel->setText("Loading playlist...");
                }

                // Build playlist from audio tracks
                std::vector<PlaylistTrack> playlist;
                for (const auto& track : session.audioTracks) {
                    PlaylistTrack pt;
                    pt.url = client.getStreamUrl(track.contentUrl, "");
                    pt.title = track.title;
                    pt.startOffset = track.startOffset;
                    pt.duration = track.duration;

                    if (pt.url.empty()) {
                        brls::Logger::error("Failed to get stream URL for track: {}", track.title);
                        m_loadingMedia = false;
                        return;
                    }

                    playlist.push_back(pt);
                    brls::Logger::debug("PlayerActivity: Track {}: {} ({}s)", track.index, pt.title, pt.duration);
                }

                brls::Logger::info("PlayerActivity: Loading playlist (startTime={}s)", startTime);

                // Load playlist - MPV will handle streaming each track
                if (!player.loadPlaylist(playlist, item.title, startTime > 0 ? static_cast<double>(startTime) : -1.0)) {
                    brls::Logger::error("Failed to load playlist");
                    m_loadingMedia = false;
                    return;
                }

                brls::Logger::info("PlayerActivity: Playlist streaming started");

            } else {
                // ================================================================
                // SINGLE-FILE STREAMING - Use native HTTP + buffer for Vita
                // Downloads to temp cache using native Vita HTTP, plays once buffered
                // Temp file is deleted when player exits (not a permanent download)
                // ================================================================
                std::string streamUrl;
                if (!session.audioTracks.empty() && !session.audioTracks[0].contentUrl.empty()) {
                    streamUrl = client.getStreamUrl(session.audioTracks[0].contentUrl, "");
                } else {
                    streamUrl = client.getDirectStreamUrl(m_itemId, 0);
                }

                if (streamUrl.empty()) {
                    brls::Logger::error("Failed to get stream URL for: {}", m_itemId);
                    m_loadingMedia = false;
                    return;
                }

                brls::Logger::info("PlayerActivity: Using streaming mode (native HTTP + buffer)");
                brls::Logger::info("PlayerActivity: Stream URL: {}", streamUrl);

                if (chapterInfoLabel) {
                    chapterInfoLabel->setText("Buffering...");
                }

                // Create streaming buffer manager
                m_streamingBuffer = std::make_shared<StreamingBufferManager>(m_itemId, m_episodeId);

                // Capture needed values for callbacks
                std::string itemTitle = item.title;
                float resumeTime = startTime;

                // Set up state callback for when buffer is ready
                m_streamingBuffer->setStateCallback([this, itemTitle, resumeTime, &settings](BufferState state) {
                    switch (state) {
                        case BufferState::READY: {
                            brls::Logger::info("PlayerActivity: Buffer ready, starting playback");

                            // Initialize player if needed
                            MpvPlayer& player = MpvPlayer::getInstance();
                            if (!player.isInitialized()) {
                                if (!player.init()) {
                                    brls::Logger::error("Failed to initialize MPV player");
                                    return;
                                }
                            }

                            // Load the buffered temp file (local file, not HTTP)
                            std::string tempPath = m_streamingBuffer->getTempPath();
                            brls::Logger::info("PlayerActivity: Loading buffered file: {}", tempPath);

                            if (!player.loadFile(tempPath, resumeTime > 0 ? static_cast<double>(resumeTime) : -1.0)) {
                                brls::Logger::error("Failed to load buffered file");
                                if (chapterInfoLabel) {
                                    chapterInfoLabel->setText("Playback failed");
                                }
                                return;
                            }

                            // Apply playback speed
                            float speed = getSpeedValue(static_cast<int>(settings.playbackSpeed));
                            if (speed != 1.0f) {
                                player.setSpeed(speed);
                            }

                            // Update UI
                            if (chapterInfoLabel) {
                                chapterInfoLabel->setText("");
                            }
                            if (videoView) {
                                videoView->setVisibility(brls::Visibility::VISIBLE);
                                videoView->setVideoVisible(true);
                            }

                            m_isPlaying = true;
                            m_loadingMedia = false;
                            break;
                        }

                        case BufferState::COMPLETE:
                            brls::Logger::info("PlayerActivity: Stream buffer complete");
                            break;

                        case BufferState::ERROR:
                            brls::Logger::error("PlayerActivity: Stream buffer error: {}",
                                              m_streamingBuffer->getErrorMessage());
                            if (chapterInfoLabel) {
                                chapterInfoLabel->setText("Streaming error");
                            }
                            m_loadingMedia = false;
                            break;

                        default:
                            break;
                    }
                });

                // Set up progress callback
                m_streamingBuffer->setProgressCallback([this](int64_t buffered, int64_t total) {
                    if (chapterInfoLabel && !m_isPlaying) {
                        if (total > 0) {
                            double percent = (double)buffered / total * 100.0;
                            char buf[64];
                            snprintf(buf, sizeof(buf), "Buffering %.0f%%", percent);
                            chapterInfoLabel->setText(buf);
                        } else {
                            char buf[64];
                            snprintf(buf, sizeof(buf), "Buffering %dMB...", (int)(buffered / (1024 * 1024)));
                            chapterInfoLabel->setText(buf);
                        }
                    }
                });

                // Start the download with appropriate extension
                if (!m_streamingBuffer->startDownload(streamUrl, ext)) {
                    brls::Logger::error("Failed to start streaming buffer");
                    if (chapterInfoLabel) {
                        chapterInfoLabel->setText("Failed to start stream");
                    }
                    m_loadingMedia = false;
                    return;
                }

                brls::Logger::info("PlayerActivity: Streaming buffer started, waiting for data...");
                // Don't return yet - we need to continue to the background download section
                // But mark that we're in streaming mode
                return;  // Early return - callback will handle playback start
            }

            // ================================================================
            // STEP 3: Start background download if enabled
            // ================================================================
            if (settings.backgroundDownload && !isMultiFile) {
                brls::Logger::info("PlayerActivity: Starting background download while streaming");

                // Queue download in background - it will save to downloads folder
                std::string downloadUrl;
                if (!session.audioTracks.empty() && !session.audioTracks[0].contentUrl.empty()) {
                    downloadUrl = client.getStreamUrl(session.audioTracks[0].contentUrl, "");
                } else {
                    downloadUrl = client.getDirectStreamUrl(m_itemId, 0);
                }

                if (!downloadUrl.empty()) {
                    // Use DownloadsManager to queue the download
                    // Note: queueDownload doesn't support URL parameter directly,
                    // so we just queue the item and let DownloadsManager fetch the URL
                    downloadsMgr.queueDownload(m_itemId, item.title, item.authorName,
                                              item.duration, item.type);
                    brls::Logger::info("PlayerActivity: Background download queued");
                }
            }

            // Apply saved playback speed
            float speed = getSpeedValue(static_cast<int>(settings.playbackSpeed));
            if (speed != 1.0f) {
                player.setSpeed(speed);
            }

            // Show video view for audio playback (shows progress/controls)
            if (videoView) {
                videoView->setVisibility(brls::Visibility::VISIBLE);
                videoView->setVideoVisible(true);
            }

            m_isPlaying = true;
            m_loadingMedia = false;
            return;  // Early return - streaming mode
        } else {
            // ================================================================
            // STREAMING DISABLED - Check for downloaded file only
            // No temp cache, no download-first - user must download explicitly
            // ================================================================
            brls::Logger::info("PlayerActivity: Streaming disabled, checking for downloaded file");

            // Check if file is downloaded
            if (downloadsMgr.isDownloaded(m_itemId, m_episodeId)) {
                std::string downloadedPath = downloadsMgr.getPlaybackPath(m_itemId);
                if (!downloadedPath.empty()) {
                    brls::Logger::info("PlayerActivity: Playing downloaded file: {}", downloadedPath);

                    MpvPlayer& player = MpvPlayer::getInstance();
                    if (!player.isInitialized()) {
                        if (!player.init()) {
                            brls::Logger::error("Failed to initialize MPV player");
                            m_loadingMedia = false;
                            return;
                        }
                    }

                    if (!player.loadUrl(downloadedPath, item.title, startTime > 0 ? static_cast<double>(startTime) : -1.0)) {
                        brls::Logger::error("Failed to load downloaded file: {}", downloadedPath);
                        m_loadingMedia = false;
                        return;
                    }

                    // Apply saved playback speed
                    float speed = getSpeedValue(static_cast<int>(settings.playbackSpeed));
                    if (speed != 1.0f) {
                        player.setSpeed(speed);
                    }

                    if (videoView) {
                        videoView->setVisibility(brls::Visibility::VISIBLE);
                        videoView->setVideoVisible(true);
                    }

                    m_isPlaying = true;
                    m_isLocalFile = true;
                    m_loadingMedia = false;
                    return;
                }
            }

            // No downloaded file - show error
            brls::Logger::error("PlayerActivity: Streaming disabled and no downloaded file found");
            if (chapterInfoLabel) {
                chapterInfoLabel->setText("Please download first or enable streaming");
            }
            m_loadingMedia = false;
            return;
        }  // End of else { // Streaming disabled }
    } else {
        brls::Logger::error("Failed to fetch item details for: {}", m_itemId);
    }

    m_loadingMedia = false;
}

void PlayerActivity::updateProgress() {
    // Don't update if destroying or showing photo
    if (m_destroying || m_isPhoto) return;

    MpvPlayer& player = MpvPlayer::getInstance();

    if (!player.isInitialized()) return;

    // Always process MPV events to handle state transitions
    player.update();

    // Update play/pause button state
    updatePlayPauseButton();

    // Handle buffering state for HTTP streaming
    const MpvPlaybackInfo& info = player.getPlaybackInfo();
    if (info.buffering && chapterInfoLabel) {
        chapterInfoLabel->setText("Buffering...");
    } else if (!info.buffering && chapterInfoLabel) {
        // Clear buffering message if it was showing
        std::string currentText = chapterInfoLabel->getFullText();
        if (currentText == "Buffering...") {
            chapterInfoLabel->setText("");
        }
    }

    // Skip UI updates while MPV is still loading - be gentle on Vita's limited hardware
    if (player.isLoading()) {
        return;
    }

    // Handle pending seek when playback becomes ready
    static bool justSeeked = false;
    if (m_pendingSeek > 0.0) {
        // Try to seek once player is ready (playing or paused with valid duration)
        if (player.isPlaying() || (player.isPaused() && player.getDuration() > 0)) {
            brls::Logger::info("PlayerActivity: Seeking to resume position {}s", m_pendingSeek);
            player.seekTo(m_pendingSeek);
            m_pendingSeek = 0.0;
            justSeeked = true;
        }
    }

    // Use combined position/duration for playlist mode (multi-file audiobooks)
    double position = player.getCombinedPosition();
    double duration = player.getCombinedDuration();

    // Log position after seek to verify it worked
    if (justSeeked && position > 0) {
        brls::Logger::info("PlayerActivity: Position after seek: {}s", position);
        justSeeked = false;
    }

    // Store duration for later use
    if (duration > 0) {
        m_totalDuration = duration;
    }

    // Show track info for multi-file audiobooks
    if (player.getTrackCount() > 1 && chapterInfoLabel) {
        char trackInfo[64];
        snprintf(trackInfo, sizeof(trackInfo), "Track %d/%d",
                 player.getCurrentTrackIndex() + 1, player.getTrackCount());
        std::string currentText = chapterInfoLabel->getFullText();
        if (currentText != trackInfo && currentText != "Buffering...") {
            chapterInfoLabel->setText(trackInfo);
        }
    }

    if (duration > 0) {
        // Update progress slider
        if (progressSlider) {
            progressSlider->setProgress((float)(position / duration));
        }

        // Update elapsed time label
        if (timeElapsedLabel) {
            timeElapsedLabel->setText(formatTime(position));
        }

        // Update remaining time label
        if (timeRemainingLabel) {
            double remaining = duration - position;
            timeRemainingLabel->setText(formatTimeRemaining(remaining));
        }

        // Legacy time label (for compatibility)
        if (timeLabel) {
            char timeStr[32];
            snprintf(timeStr, sizeof(timeStr), "%s / %s",
                     formatTime(position).c_str(), formatTime(duration).c_str());
            timeLabel->setText(timeStr);
        }
    }

    // Periodic progress sync (every 30 seconds while playing)
    if (m_isPlaying && !m_isDirectFile) {
        m_syncCounter++;
        if (m_syncCounter >= 30) {  // Every 30 updates (30 seconds)
            m_syncCounter = 0;
            float currentPos = static_cast<float>(position);
            // Only sync/save if position changed significantly (more than 5 seconds)
            if (std::abs(currentPos - m_lastSyncedTime) > 5.0f) {
                if (m_isLocalFile) {
                    // Save progress for downloaded media locally
                    DownloadsManager::getInstance().updateProgress(m_itemId, currentPos, m_episodeId);
                    DownloadsManager::getInstance().saveState();
                    brls::Logger::debug("PlayerActivity: Auto-saved local progress {}s", currentPos);

                    // Also sync to server if online
                    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
                    if (client.isAuthenticated()) {
                        float totalDuration = static_cast<float>(duration);
                        bool isPodcast = !m_episodeId.empty();
                        bool isFinished = shouldMarkAsFinished(currentPos, totalDuration, isPodcast);
                        client.updateProgress(m_itemId, currentPos, totalDuration, isFinished, m_episodeId);
                    }
                    m_lastSyncedTime = currentPos;
                } else {
                    // Remote playback - sync to server
                    syncProgressToServer();
                }
            }
        }
    }

    // Check for and display background download progress (multi-file audiobooks)
    AppSettings& settings = Application::getInstance().getSettings();
    if (settings.showDownloadProgress && chapterInfoLabel) {
        BackgroundDownloadProgress bgProgress = Application::getInstance().getBackgroundDownloadProgress();
        if (bgProgress.active && bgProgress.itemId == m_itemId) {
            // Show download progress
            char progressBuf[128];
            if (bgProgress.totalBytes > 0) {
                int percent = static_cast<int>((bgProgress.downloadedBytes * 100) / bgProgress.totalBytes);
                int dlMB = static_cast<int>(bgProgress.downloadedBytes / (1024 * 1024));
                int totalMB = static_cast<int>(bgProgress.totalBytes / (1024 * 1024));
                snprintf(progressBuf, sizeof(progressBuf), "Track %d/%d - %d%% (%d/%d MB)",
                        bgProgress.currentTrack, bgProgress.totalTracks,
                        percent, dlMB, totalMB);
            } else {
                snprintf(progressBuf, sizeof(progressBuf), "Track %d/%d - %s",
                        bgProgress.currentTrack, bgProgress.totalTracks,
                        bgProgress.status.c_str());
            }
            chapterInfoLabel->setText(progressBuf);
        } else if (!bgProgress.active && chapterInfoLabel->getFullText().find("Track") != std::string::npos) {
            // Clear the label when download completes
            chapterInfoLabel->setText("");
        }
    }

    // Check if playback ended (only if we were actually playing)
    if (m_isPlaying && player.hasEnded()) {
        m_isPlaying = false;  // Prevent multiple triggers
        float totalDuration = (float)player.getDuration();

        if (m_isLocalFile) {
            // Save completed progress for downloaded media
            DownloadsManager::getInstance().updateProgress(m_itemId, totalDuration, m_episodeId);
            DownloadsManager::getInstance().saveState();
            brls::Logger::info("PlayerActivity: Saved completed progress for local file");
        }

        // Mark as finished with Audiobookshelf (set isFinished=true)
        AudiobookshelfClient::getInstance().updateProgress(m_itemId, totalDuration, totalDuration, true, m_episodeId);
        brls::Application::popActivity();
    }
}

void PlayerActivity::togglePlayPause() {
    MpvPlayer& player = MpvPlayer::getInstance();

    if (player.isPlaying()) {
        player.pause();
        m_isPlaying = false;
    } else if (player.isPaused()) {
        player.play();
        m_isPlaying = true;
    }

    // Update button immediately
    updatePlayPauseButton();
}

void PlayerActivity::updatePlayPauseButton() {
    if (!playPauseIcon) return;

    MpvPlayer& player = MpvPlayer::getInstance();

    if (player.isPlaying()) {
        playPauseIcon->setText("||");  // Pause icon (show pause when playing)
    } else {
        playPauseIcon->setText(">");   // Play icon (show play when paused)
    }
}

void PlayerActivity::seek(int seconds) {
    MpvPlayer& player = MpvPlayer::getInstance();
    player.seekRelative(seconds);
}

std::string PlayerActivity::formatTime(double seconds) {
    if (seconds < 0) seconds = 0;

    int totalSecs = (int)seconds;
    int hours = totalSecs / 3600;
    int mins = (totalSecs % 3600) / 60;
    int secs = totalSecs % 60;

    char buf[32];
    if (hours > 0) {
        snprintf(buf, sizeof(buf), "%d:%02d:%02d", hours, mins, secs);
    } else {
        snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);
    }
    return std::string(buf);
}

std::string PlayerActivity::formatTimeRemaining(double remaining) {
    if (remaining < 0) remaining = 0;

    int totalSecs = (int)remaining;
    int hours = totalSecs / 3600;
    int mins = (totalSecs % 3600) / 60;
    int secs = totalSecs % 60;

    char buf[32];
    if (hours > 0) {
        snprintf(buf, sizeof(buf), "-%d:%02d:%02d", hours, mins, secs);
    } else {
        snprintf(buf, sizeof(buf), "-%d:%02d", mins, secs);
    }
    return std::string(buf);
}

void PlayerActivity::loadCoverArt(const std::string& coverUrl) {
    if (coverUrl.empty() || !coverImage) return;

    brls::Logger::debug("Loading cover art: {}", coverUrl);

    // Check if this is a local file path (starts with ux0: or / or doesn't start with http)
    bool isLocalPath = (coverUrl.find("ux0:") == 0 ||
                        coverUrl.find("/") == 0 ||
                        coverUrl.find("http") != 0);

    if (isLocalPath) {
        // Load local file directly
        brls::Logger::info("Loading local cover image: {}", coverUrl);
#ifdef __vita__
        SceUID fd = sceIoOpen(coverUrl.c_str(), SCE_O_RDONLY, 0);
        if (fd >= 0) {
            // Get file size
            SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
            sceIoLseek(fd, 0, SCE_SEEK_SET);

            if (size > 0 && size < 10 * 1024 * 1024) {  // Max 10MB for cover
                std::vector<uint8_t> data(size);
                if (sceIoRead(fd, data.data(), size) == size) {
                    coverImage->setImageFromMem(data.data(), data.size());
                    brls::Logger::debug("Local cover art loaded ({} bytes)", size);
                }
            }
            sceIoClose(fd);
        } else {
            brls::Logger::warning("Failed to open local cover: {}", coverUrl);
        }
#else
        // Non-Vita: use standard file I/O
        std::ifstream file(coverUrl, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);

            if (size > 0 && size < 10 * 1024 * 1024) {
                std::vector<uint8_t> data(size);
                if (file.read(reinterpret_cast<char*>(data.data()), size)) {
                    coverImage->setImageFromMem(data.data(), data.size());
                    brls::Logger::debug("Local cover art loaded ({} bytes)", size);
                }
            }
            file.close();
        }
#endif
    } else {
        // Use the image loader to load the cover asynchronously (HTTP)
        ImageLoader::loadAsync(coverUrl, [this](brls::Image* img) {
            // Image is loaded into the target automatically
            brls::Logger::debug("Cover art loaded");
        }, coverImage);
    }
}

float PlayerActivity::getSpeedValue(int index) {
    // Speed values matching PlaybackSpeed enum: 0.5x, 0.75x, 1.0x, 1.25x, 1.5x, 1.75x, 2.0x
    static const float speeds[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
    if (index >= 0 && index < 7) {
        return speeds[index];
    }
    return 1.0f;
}

void PlayerActivity::updateSpeedLabel() {
    if (!speedLabel) return;

    AppSettings& settings = Application::getInstance().getSettings();
    float speed = getSpeedValue(static_cast<int>(settings.playbackSpeed));

    // Format speed with one decimal place
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << speed << "x";
    speedLabel->setText(ss.str());
}

void PlayerActivity::cyclePlaybackSpeed() {
    AppSettings& settings = Application::getInstance().getSettings();

    // Cycle through speeds: 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0
    int currentIndex = static_cast<int>(settings.playbackSpeed);
    int nextIndex = (currentIndex + 1) % 7;  // 7 speed options

    settings.playbackSpeed = static_cast<PlaybackSpeed>(nextIndex);
    Application::getInstance().saveSettings();

    // Apply new speed to player
    MpvPlayer& player = MpvPlayer::getInstance();
    float speed = getSpeedValue(nextIndex);
    player.setSpeed(speed);

    // Update the label
    updateSpeedLabel();

    brls::Logger::info("Playback speed changed to {}x", speed);
}

void PlayerActivity::syncProgressToServer() {
    MpvPlayer& player = MpvPlayer::getInstance();
    if (!player.isInitialized()) return;

    float currentTime = static_cast<float>(player.getPosition());
    float duration = static_cast<float>(player.getDuration());

    if (duration <= 0 || currentTime < 0) return;

    brls::Logger::debug("PlayerActivity: Periodic sync - {}s of {}s", currentTime, duration);

    // Use session sync if we have an active session, otherwise use progress update
    if (!m_sessionId.empty()) {
        AudiobookshelfClient::getInstance().syncPlaybackSession(m_sessionId, currentTime, duration);
    } else {
        AudiobookshelfClient::getInstance().updateProgress(m_itemId, currentTime, duration, false, m_episodeId);
    }

    m_lastSyncedTime = currentTime;
}

} // namespace vitaabs
