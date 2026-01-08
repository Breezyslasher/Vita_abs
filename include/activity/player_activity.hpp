/**
 * VitaABS - Player Activity
 * Video playback screen with controls
 */

#pragma once

#include <borealis.hpp>
#include <borealis/core/timer.hpp>
#include <string>

// Forward declaration
namespace vitaabs { class VideoView; }

namespace vitaabs {

class PlayerActivity : public brls::Activity {
public:
    // Play audiobook/podcast item (single file or book)
    PlayerActivity(const std::string& itemId);

    // Play podcast episode or audiobook with specific episode
    PlayerActivity(const std::string& itemId, const std::string& episodeId,
                   float startTime = -1.0f);

    // Play local downloaded file
    PlayerActivity(const std::string& itemId, bool isLocalFile);

    // Play direct file path (for debug/testing)
    static PlayerActivity* createForDirectFile(const std::string& filePath);

    brls::View* createContentView() override;

    void onContentAvailable() override;

    void willDisappear(bool resetState) override;

private:
    void loadMedia();
    void updateProgress();
    void togglePlayPause();
    void seek(int seconds);

    std::string m_itemId;
    std::string m_episodeId;       // For podcast episodes
    std::string m_directFilePath;  // For direct file playback (debug)
    std::string m_tempFilePath;    // Temp file for streaming (downloaded before playback)
    bool m_isPlaying = false;
    bool m_isPhoto = false;
    bool m_isLocalFile = false;   // Playing from local download
    bool m_isDirectFile = false;  // Playing direct file path (debug)
    bool m_destroying = false;    // Flag to prevent timer callbacks during destruction
    bool m_loadingMedia = false;  // Flag to prevent rapid re-entry of loadMedia
    double m_pendingSeek = 0.0;   // Pending seek position (set when resuming)
    brls::RepeatingTimer m_updateTimer;

    BRLS_BIND(brls::Box, playerContainer, "player/container");
    BRLS_BIND(brls::Label, titleLabel, "player/title");
    BRLS_BIND(brls::Label, timeLabel, "player/time");
    BRLS_BIND(brls::Slider, progressSlider, "player/progress");
    BRLS_BIND(brls::Box, controlsBox, "player/controls");
    BRLS_BIND(brls::Image, photoImage, "player/photo");
    BRLS_BIND(VideoView, videoView, "player/video");
};

} // namespace vitaabs
