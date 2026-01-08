/**
 * VitaABS - Player Activity implementation
 */

#include "activity/player_activity.hpp"
#include "app/audiobookshelf_client.hpp"
#include "app/downloads_manager.hpp"
#include "player/mpv_player.hpp"
#include "utils/http_client.hpp"
#include "utils/image_loader.hpp"
#include "view/video_view.hpp"

#include <cstdio>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

namespace vitaabs {

// Temp directory for streaming playback
static const char* TEMP_DIR = "ux0:data/VitaABS/temp";

// Helper to ensure temp directory exists
static void ensureTempDir() {
#ifdef __vita__
    sceIoMkdir("ux0:data/VitaABS", 0777);
    sceIoMkdir(TEMP_DIR, 0777);
#endif
}

// Helper to delete temp file
static void deleteTempFile(const std::string& path) {
    if (!path.empty()) {
#ifdef __vita__
        sceIoRemove(path.c_str());
#else
        std::remove(path.c_str());
#endif
        brls::Logger::debug("Deleted temp file: {}", path);
    }
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
        btnPlayPause->getClickEvent()->subscribe([this](brls::View* view) {
            togglePlayPause();
        });
        btnPlayPause->setFocusable(true);
    }

    if (btnRewind) {
        btnRewind->getClickEvent()->subscribe([this](brls::View* view) {
            seek(-30);
        });
    }

    if (btnForward) {
        btnForward->getClickEvent()->subscribe([this](brls::View* view) {
            seek(30);
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

    this->registerAction("Rewind 30s", brls::ControllerButton::BUTTON_LB, [this](brls::View* view) {
        seek(-30);
        return true;
    });

    this->registerAction("Forward 30s", brls::ControllerButton::BUTTON_RB, [this](brls::View* view) {
        seek(30);
        return true;
    });

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

            if (m_isLocalFile) {
                // Save progress for downloaded media (in seconds)
                DownloadsManager::getInstance().updateProgress(m_itemId, currentTime);
                DownloadsManager::getInstance().saveState();
                brls::Logger::info("PlayerActivity: Saved local progress {}s for {}", currentTime, m_itemId);
            } else {
                // Save progress to Audiobookshelf server
                float totalDuration = (float)player.getDuration();
                AudiobookshelfClient::getInstance().updateProgress(m_itemId, currentTime, totalDuration, false, m_episodeId);
            }
        }
    }

    // Stop playback (safe to call even if not playing)
    if (player.isInitialized()) {
        player.stop();
    }

    m_isPlaying = false;

    // Clean up temp file if we were streaming
    if (!m_tempFilePath.empty()) {
        deleteTempFile(m_tempFilePath);
        m_tempFilePath.clear();
    }
}

void PlayerActivity::loadMedia() {
    // Prevent rapid re-entry
    if (m_loadingMedia) {
        brls::Logger::debug("PlayerActivity: Already loading media, skipping");
        return;
    }
    m_loadingMedia = true;

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
        DownloadItem* download = downloads.getDownload(m_itemId);

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

        // Load local file (using playback path for multi-file support)
        if (!player.loadUrl(playbackPath, download->title)) {
            brls::Logger::error("Failed to load local file: {}", playbackPath);
            m_loadingMedia = false;
            return;
        }

        // Show video view
        if (videoView) {
            videoView->setVisibility(brls::Visibility::VISIBLE);
            videoView->setVideoVisible(true);
        }

        // Resume from saved viewOffset
        if (download->viewOffset > 0) {
            m_pendingSeek = download->viewOffset / 1000.0;
        }

        m_isPlaying = true;
        m_loadingMedia = false;
        return;
    }

    // Remote playback from Audiobookshelf server
    // Download to temp file first, then play locally (streaming doesn't work well on Vita)
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
        if (!item.coverUrl.empty()) {
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

        brls::Logger::info("PlayerActivity: Session created - id: {}, audioTracks: {}, playMethod: {}",
                          session.id, session.audioTracks.size(), session.playMethod);

        // Get stream URL from the session's audio tracks
        std::string streamUrl;
        std::string mimeType = "audio/mpeg";
        if (!session.audioTracks.empty() && !session.audioTracks[0].contentUrl.empty()) {
            brls::Logger::info("PlayerActivity: Using audio track[0] contentUrl: {}",
                              session.audioTracks[0].contentUrl);
            streamUrl = client.getStreamUrl(session.audioTracks[0].contentUrl, "");
            if (!session.audioTracks[0].mimeType.empty()) {
                mimeType = session.audioTracks[0].mimeType;
            }
        } else {
            brls::Logger::warning("PlayerActivity: No audio tracks in session, using fallback direct stream URL");
            streamUrl = client.getDirectStreamUrl(m_itemId, 0);
        }

        if (streamUrl.empty()) {
            brls::Logger::error("Failed to get stream URL for: {}", m_itemId);
            m_loadingMedia = false;
            return;
        }

        float startTime = session.currentTime;
        brls::Logger::debug("PlayerActivity: Will resume from position: {}s", startTime);

        // Determine file extension from mime type
        std::string ext = ".mp3";
        if (mimeType.find("mp4") != std::string::npos || mimeType.find("m4a") != std::string::npos) {
            ext = ".m4a";
        } else if (mimeType.find("flac") != std::string::npos) {
            ext = ".flac";
        } else if (mimeType.find("ogg") != std::string::npos) {
            ext = ".ogg";
        }

        // Create temp file path
        ensureTempDir();
        m_tempFilePath = std::string(TEMP_DIR) + "/playback_temp" + ext;
        brls::Logger::info("PlayerActivity: Downloading to temp file: {}", m_tempFilePath);

        // Download the audio file to temp
        HttpClient httpClient;
        httpClient.setTimeout(120); // 2 minute timeout for large files

#ifdef __vita__
        SceUID fd = sceIoOpen(m_tempFilePath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (fd < 0) {
            brls::Logger::error("Failed to create temp file: {}", m_tempFilePath);
            m_tempFilePath.clear();
            m_loadingMedia = false;
            return;
        }

        int64_t totalSize = 0;
        int64_t downloadedSize = 0;

        bool downloadSuccess = httpClient.downloadFile(streamUrl,
            [&](const char* data, size_t size) -> bool {
                int written = sceIoWrite(fd, data, size);
                if (written < 0) {
                    brls::Logger::error("Failed to write to temp file");
                    return false;
                }
                downloadedSize += size;
                if (totalSize > 0 && downloadedSize % (1024 * 1024) < size) {
                    // Log progress every ~1MB
                    brls::Logger::debug("Download progress: {}%", (int)((downloadedSize * 100) / totalSize));
                }
                return true;
            },
            [&](int64_t size) {
                totalSize = size;
                brls::Logger::info("PlayerActivity: Downloading {} bytes", size);
            }
        );

        sceIoClose(fd);

        if (!downloadSuccess) {
            brls::Logger::error("Failed to download audio file");
            deleteTempFile(m_tempFilePath);
            m_tempFilePath.clear();
            m_loadingMedia = false;
            return;
        }

        brls::Logger::info("PlayerActivity: Download complete ({} bytes)", downloadedSize);
#else
        // Non-Vita: just use the stream URL directly
        m_tempFilePath = streamUrl;
#endif

        // Now play the local temp file
        MpvPlayer& player = MpvPlayer::getInstance();

        if (!player.isInitialized()) {
            brls::Logger::info("PlayerActivity: Initializing MPV player...");
            if (!player.init()) {
                brls::Logger::error("Failed to initialize MPV player");
                deleteTempFile(m_tempFilePath);
                m_tempFilePath.clear();
                m_loadingMedia = false;
                return;
            }
            brls::Logger::info("PlayerActivity: MPV player initialized successfully");
        }

        brls::Logger::info("PlayerActivity: Loading temp file: {}", m_tempFilePath);
        if (!player.loadUrl(m_tempFilePath, item.title)) {
            brls::Logger::error("Failed to load temp file: {}", m_tempFilePath);
            deleteTempFile(m_tempFilePath);
            m_tempFilePath.clear();
            m_loadingMedia = false;
            return;
        }
        brls::Logger::info("PlayerActivity: MPV loadUrl succeeded, waiting for playback to start...");

        // Show video view for audio playback (shows progress/controls)
        if (videoView) {
            videoView->setVisibility(brls::Visibility::VISIBLE);
            videoView->setVideoVisible(true);
        }

        // Resume from saved position if available
        if (startTime > 0) {
            m_pendingSeek = startTime;
        }

        m_isPlaying = true;
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

    // Skip UI updates while MPV is still loading - be gentle on Vita's limited hardware
    if (player.isLoading()) {
        return;
    }

    // Handle pending seek when playback becomes ready
    if (m_pendingSeek > 0.0 && player.isPlaying()) {
        player.seekTo(m_pendingSeek);
        m_pendingSeek = 0.0;
    }

    double position = player.getPosition();
    double duration = player.getDuration();

    // Store duration for later use
    if (duration > 0) {
        m_totalDuration = duration;
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

    // Check if playback ended (only if we were actually playing)
    if (m_isPlaying && player.hasEnded()) {
        m_isPlaying = false;  // Prevent multiple triggers
        // Mark as finished with Audiobookshelf (set isFinished=true)
        float totalDuration = (float)player.getDuration();
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

    // Use the image loader to load the cover asynchronously
    ImageLoader::loadAsync(coverUrl, [this](const std::string& imagePath) {
        if (!m_destroying && coverImage) {
            coverImage->setImageFromFile(imagePath);
        }
    });
}

} // namespace vitaabs
