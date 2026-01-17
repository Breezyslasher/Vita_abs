/**
 * VitaABS - sceAvPlayer wrapper for native Vita streaming
 *
 * Uses Vita's native media player for HTTP streaming,
 * avoiding the crashes that occur with MPV's internal HTTP handling.
 *
 * Supports:
 * - Direct HTTP/HTTPS streaming
 * - Local file playback
 * - Seeking, pause/resume
 * - Playback speed control
 */

#pragma once

#include <string>
#include <atomic>
#include <functional>
#include <mutex>

#ifdef __vita__
#include <psp2/avplayer.h>
#include <psp2/audiodec.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#endif

namespace vitaabs {

// Player state
enum class AvPlayerState {
    IDLE,
    LOADING,
    BUFFERING,
    PLAYING,
    PAUSED,
    STOPPED,
    ENDED,
    ERROR
};

// Playback info
struct AvPlaybackInfo {
    std::string title;
    double duration = 0.0;      // Total duration in seconds
    double position = 0.0;      // Current position in seconds
    int sampleRate = 44100;
    int channels = 2;
    bool isStreaming = false;
};

// Callbacks
using AvStateCallback = std::function<void(AvPlayerState state)>;
using AvErrorCallback = std::function<void(const std::string& error)>;

/**
 * Native Vita audio player using sceAvPlayer
 */
class AvPlayer {
public:
    static AvPlayer& getInstance();

    ~AvPlayer();

    // Lifecycle
    bool init();
    void shutdown();
    bool isInitialized() const { return m_initialized; }

    // Playback
    bool loadUrl(const std::string& url, const std::string& title = "");
    bool loadFile(const std::string& path, const std::string& title = "");
    void play();
    void pause();
    void stop();
    void togglePlayPause();

    // Seeking
    void seek(double seconds);
    void seekRelative(double seconds);

    // State
    AvPlayerState getState() const { return m_state.load(); }
    bool isPlaying() const { return m_state.load() == AvPlayerState::PLAYING; }
    bool isPaused() const { return m_state.load() == AvPlayerState::PAUSED; }
    bool isLoading() const { return m_state.load() == AvPlayerState::LOADING || m_state.load() == AvPlayerState::BUFFERING; }

    // Position/Duration
    double getPosition();
    double getDuration() const { return m_playbackInfo.duration; }

    // Speed control
    void setSpeed(float speed);
    float getSpeed() const { return m_speed; }

    // Volume
    void setVolume(int volume);  // 0-100
    int getVolume() const { return m_volume; }

    // Info
    const AvPlaybackInfo& getPlaybackInfo() const { return m_playbackInfo; }
    std::string getErrorMessage() const { return m_errorMessage; }

    // Callbacks
    void setStateCallback(AvStateCallback callback) { m_stateCallback = callback; }
    void setErrorCallback(AvErrorCallback callback) { m_errorCallback = callback; }

    // Update (call from main loop)
    void update();

private:
    AvPlayer();
    AvPlayer(const AvPlayer&) = delete;
    AvPlayer& operator=(const AvPlayer&) = delete;

    void setState(AvPlayerState newState);
    void processAudio();

#ifdef __vita__
    // sceAvPlayer callbacks
    static void* playerAllocate(void* argP, uint32_t argAlignment, uint32_t argSize);
    static void playerDeallocate(void* argP, void* argMemory);
    static void playerEventCallback(void* argP, int32_t argEventId, int32_t argSourceId, void* argEventData);

    // Audio output thread
    static int audioThread(SceSize args, void* argp);
    void audioLoop();

    SceUID m_avPlayer = 0;
    SceUID m_audioThread = 0;
    int m_audioPort = -1;
#endif

    std::atomic<AvPlayerState> m_state{AvPlayerState::IDLE};
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_audioRunning{false};

    AvPlaybackInfo m_playbackInfo;
    std::string m_currentUrl;
    std::string m_errorMessage;

    float m_speed = 1.0f;
    int m_volume = 100;

    AvStateCallback m_stateCallback;
    AvErrorCallback m_errorCallback;

    std::mutex m_mutex;
};

} // namespace vitaabs
