/**
 * VitaABS - Player Activity
 * Audio playback screen with controls for audiobooks and podcasts
 */

#pragma once

#include <borealis.hpp>
#include <borealis/core/timer.hpp>
#include <string>

#include "app/audiobookshelf_client.hpp"

namespace vitaabs {

class PlayerActivity : public brls::Activity {
public:
    // Play from Audiobookshelf server
    PlayerActivity(const std::string& itemId, const std::string& episodeId = "");

    // Play local downloaded file
    PlayerActivity(const std::string& itemId, bool isLocalFile);

    brls::View* createContentView() override;

    void onContentAvailable() override;

    void willDisappear(bool resetState) override;

private:
    void loadMedia();
    void startPlayback();
    void updateProgress();
    void togglePlayPause();
    void seek(int seconds);
    void seekToChapter(int chapterIndex);
    void updateChapterDisplay();
    void syncProgress();
    void closeSession();

    std::string m_itemId;
    std::string m_episodeId;         // For podcast episodes
    std::string m_sessionId;         // Playback session ID

    MediaItem m_mediaItem;           // Current media item
    int m_currentChapter = 0;        // Current chapter index
    float m_startTime = 0.0f;        // Where playback started
    float m_timeListened = 0.0f;     // Total time listened in this session

    bool m_isPlaying = false;
    bool m_isLocalFile = false;      // Playing from local download
    bool m_destroying = false;       // Flag to prevent timer callbacks during destruction
    bool m_loadingMedia = false;     // Flag to prevent rapid re-entry of loadMedia

    brls::RepeatingTimer m_updateTimer;
    brls::RepeatingTimer m_syncTimer; // Timer to sync progress to server

    BRLS_BIND(brls::Box, playerContainer, "player/container");
    BRLS_BIND(brls::Label, titleLabel, "player/title");
    BRLS_BIND(brls::Label, authorLabel, "player/author");
    BRLS_BIND(brls::Label, chapterLabel, "player/chapter");
    BRLS_BIND(brls::Label, timeLabel, "player/time");
    BRLS_BIND(brls::Label, durationLabel, "player/duration");
    BRLS_BIND(brls::Slider, progressSlider, "player/progress");
    BRLS_BIND(brls::Box, controlsBox, "player/controls");
    BRLS_BIND(brls::Image, coverImage, "player/cover");
    BRLS_BIND(brls::Button, playPauseButton, "player/play_pause");
    BRLS_BIND(brls::Button, rewindButton, "player/rewind");
    BRLS_BIND(brls::Button, forwardButton, "player/forward");
    BRLS_BIND(brls::Button, prevChapterButton, "player/prev_chapter");
    BRLS_BIND(brls::Button, nextChapterButton, "player/next_chapter");
};

} // namespace vitaabs
