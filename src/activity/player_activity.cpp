/**
 * VitaABS - Player Activity implementation
 */

#include "activity/player_activity.hpp"
#include "app/audiobookshelf_client.hpp"
#include "app/downloads_manager.hpp"
#include "app/application.hpp"
#include "player/mpv_player.hpp"
#include "utils/image_loader.hpp"
#include "view/video_view.hpp"

namespace vitaabs {

PlayerActivity::PlayerActivity(const std::string& itemId, const std::string& episodeId, bool isLocalFile)
    : m_itemId(itemId), m_episodeId(episodeId), m_isLocalFile(isLocalFile) {
    brls::Logger::debug("PlayerActivity created for item: {}, episode: {}, local: {}",
                       itemId, episodeId, isLocalFile);
}

PlayerActivity* PlayerActivity::createForDirectFile(const std::string& filePath) {
    PlayerActivity* activity = new PlayerActivity("", "", false);
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

    // Set up controls
    if (progressSlider) {
        progressSlider->setProgress(0.0f);
        progressSlider->getProgressEvent()->subscribe([this](float progress) {
            // Seek to position
            MpvPlayer& player = MpvPlayer::getInstance();
            double duration = player.getDuration();
            player.seekTo(duration * progress);
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

    this->registerAction("Rewind", brls::ControllerButton::BUTTON_LB, [this](brls::View* view) {
        int seekInterval = Application::getInstance().getSettings().seekInterval;
        seek(-seekInterval);
        return true;
    });

    this->registerAction("Forward", brls::ControllerButton::BUTTON_RB, [this](brls::View* view) {
        int seekInterval = Application::getInstance().getSettings().seekInterval;
        seek(seekInterval);
        return true;
    });

    this->registerAction("Prev Chapter", brls::ControllerButton::BUTTON_LSTICK, [this](brls::View* view) {
        prevChapter();
        return true;
    });

    this->registerAction("Next Chapter", brls::ControllerButton::BUTTON_RSTICK, [this](brls::View* view) {
        nextChapter();
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

    // Stop playback and save progress
    MpvPlayer& player = MpvPlayer::getInstance();

    // Only try to save progress if player is in a valid state
    if (player.isInitialized() && (player.isPlaying() || player.isPaused())) {
        double position = player.getPosition();
        if (position > 0) {
            if (m_isLocalFile) {
                // Save progress for downloaded media
                DownloadsManager::getInstance().updateProgress(m_itemId, position);
                DownloadsManager::getInstance().saveState();
                brls::Logger::info("PlayerActivity: Saved local progress {}s for {}", position, m_itemId);
            } else if (!m_sessionId.empty()) {
                // Sync progress to Audiobookshelf server
                AudiobookshelfClient::getInstance().syncPlaybackProgress(
                    m_sessionId, position, player.getDuration(), player.isPlaying());
                brls::Logger::info("PlayerActivity: Synced progress {}s for session {}", position, m_sessionId);
            }
        }
    }

    // Close the playback session
    if (!m_sessionId.empty()) {
        AudiobookshelfClient::getInstance().closePlaybackSession(m_sessionId);
    }

    // Stop playback (safe to call even if not playing)
    if (player.isInitialized()) {
        player.stop();
    }

    m_isPlaying = false;
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

        if (titleLabel) {
            // Extract filename from path
            size_t lastSlash = m_directFilePath.find_last_of("/\\");
            std::string filename = (lastSlash != std::string::npos)
                ? m_directFilePath.substr(lastSlash + 1)
                : m_directFilePath;
            titleLabel->setText(filename);
        }

        MpvPlayer& player = MpvPlayer::getInstance();

        if (!player.isInitialized()) {
            if (!player.init()) {
                brls::Logger::error("Failed to initialize MPV player");
                m_loadingMedia = false;
                return;
            }
        }

        // Set playback speed from settings
        player.setSpeed(Application::getInstance().getPlaybackSpeedValue());

        // Load direct file
        if (!player.loadUrl(m_directFilePath, "Test File")) {
            brls::Logger::error("Failed to load direct file: {}", m_directFilePath);
            m_loadingMedia = false;
            return;
        }

        // Show video view for visualizer/controls
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

        brls::Logger::info("PlayerActivity: Playing local file: {}", download->localPath);

        if (titleLabel) {
            std::string title = download->title;
            if (!download->parentTitle.empty()) {
                title = download->parentTitle + " - " + download->title;
            }
            titleLabel->setText(title);
        }

        MpvPlayer& player = MpvPlayer::getInstance();

        if (!player.isInitialized()) {
            if (!player.init()) {
                brls::Logger::error("Failed to initialize MPV player");
                m_loadingMedia = false;
                return;
            }
        }

        // Set playback speed from settings
        player.setSpeed(Application::getInstance().getPlaybackSpeedValue());

        // Load local file
        if (!player.loadUrl(download->localPath, download->title)) {
            brls::Logger::error("Failed to load local file: {}", download->localPath);
            m_loadingMedia = false;
            return;
        }

        // Show video view
        if (videoView) {
            videoView->setVisibility(brls::Visibility::VISIBLE);
            videoView->setVideoVisible(true);
        }

        // Resume from saved position
        if (download->currentTime > 0) {
            m_pendingSeek = download->currentTime;
        }

        m_isPlaying = true;
        m_loadingMedia = false;
        return;
    }

    // Remote playback from Audiobookshelf server
    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    // Start a playback session
    PlaybackSession session;
    if (!client.startPlaybackSession(m_itemId, m_episodeId, session)) {
        brls::Logger::error("Failed to start playback session for: {}", m_itemId);
        m_loadingMedia = false;
        return;
    }

    m_sessionId = session.id;
    m_chapters = session.chapters;

    brls::Logger::info("PlayerActivity: Started playback session: {}", m_sessionId);

    if (titleLabel) {
        titleLabel->setText(session.displayTitle);
    }

    if (authorLabel && !session.displayAuthor.empty()) {
        authorLabel->setText(session.displayAuthor);
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

    // Set playback speed from settings
    player.setSpeed(Application::getInstance().getPlaybackSpeedValue());

    // Load the streaming URL
    if (session.audioTracks.empty()) {
        brls::Logger::error("No audio tracks in playback session");
        m_loadingMedia = false;
        return;
    }

    std::string streamUrl = client.getStreamUrl(m_itemId, m_episodeId);
    if (!player.loadUrl(streamUrl, session.displayTitle)) {
        brls::Logger::error("Failed to load URL: {}", streamUrl);
        m_loadingMedia = false;
        return;
    }

    // Show video view for audio visualizer/controls
    if (videoView) {
        videoView->setVisibility(brls::Visibility::VISIBLE);
        videoView->setVideoVisible(true);
    }

    // Resume from server-saved position
    if (session.currentTime > 0) {
        m_pendingSeek = session.currentTime;
    }

    m_isPlaying = true;
    m_loadingMedia = false;
}

void PlayerActivity::updateProgress() {
    // Don't update if destroying
    if (m_destroying) return;

    MpvPlayer& player = MpvPlayer::getInstance();

    if (!player.isInitialized()) return;

    // Always process MPV events to handle state transitions
    player.update();

    // Skip UI updates while MPV is still loading
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

    if (duration > 0) {
        if (progressSlider) {
            progressSlider->setProgress((float)(position / duration));
        }

        if (timeLabel) {
            int posMin = (int)position / 60;
            int posSec = (int)position % 60;
            int durHr = (int)duration / 3600;
            int durMin = ((int)duration % 3600) / 60;
            int durSec = (int)duration % 60;

            char timeStr[64];
            if (durHr > 0) {
                int posHr = (int)position / 3600;
                int posM = ((int)position % 3600) / 60;
                int posS = (int)position % 60;
                snprintf(timeStr, sizeof(timeStr), "%d:%02d:%02d / %d:%02d:%02d",
                         posHr, posM, posS, durHr, durMin, durSec);
            } else {
                snprintf(timeStr, sizeof(timeStr), "%02d:%02d / %02d:%02d",
                         posMin, posSec, durMin, durSec);
            }
            timeLabel->setText(timeStr);
        }

        // Update chapter label
        if (chapterLabel && !m_chapters.empty()) {
            for (size_t i = 0; i < m_chapters.size(); i++) {
                if (position >= m_chapters[i].start &&
                    (i + 1 >= m_chapters.size() || position < m_chapters[i + 1].start)) {
                    m_currentChapter = static_cast<int>(i);
                    chapterLabel->setText("Ch " + std::to_string(i + 1) + ": " + m_chapters[i].title);
                    break;
                }
            }
        }
    }

    // Periodically sync progress to server (every 30 seconds)
    if (!m_sessionId.empty() && m_isPlaying) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastSync).count();
        if (elapsed >= 30) {
            AudiobookshelfClient::getInstance().syncPlaybackProgress(
                m_sessionId, position, duration, true);
            m_lastSync = now;
        }
    }

    // Check if playback ended
    if (m_isPlaying && player.hasEnded()) {
        m_isPlaying = false;
        // Mark as finished
        if (!m_sessionId.empty()) {
            AudiobookshelfClient::getInstance().markItemAsFinished(m_itemId, m_episodeId);
        }
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
}

void PlayerActivity::seek(int seconds) {
    MpvPlayer& player = MpvPlayer::getInstance();
    player.seekRelative(seconds);
}

void PlayerActivity::prevChapter() {
    if (m_chapters.empty() || m_currentChapter <= 0) return;

    MpvPlayer& player = MpvPlayer::getInstance();
    player.seekTo(m_chapters[m_currentChapter - 1].start);
    m_currentChapter--;
}

void PlayerActivity::nextChapter() {
    if (m_chapters.empty() || m_currentChapter >= static_cast<int>(m_chapters.size()) - 1) return;

    MpvPlayer& player = MpvPlayer::getInstance();
    player.seekTo(m_chapters[m_currentChapter + 1].start);
    m_currentChapter++;
}

} // namespace vitaabs
