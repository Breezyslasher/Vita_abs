/**
 * VitaABS - Streaming Audio Player
 *
 * Implements cspot_vita-style streaming with:
 * - Circular buffer for PCM audio data
 * - Dedicated thread for sceAudioOut playback
 * - Progressive HTTP download with on-the-fly decoding
 * - FFmpeg for audio decoding to PCM
 */

#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <functional>

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/audioout.h>
#endif

namespace vitaabs {

// Callback for playback state changes
using PlaybackStateCallback = std::function<void(bool playing, float position, float duration)>;
using PlaybackErrorCallback = std::function<void(const std::string& error)>;
using StreamingProgressCallback = std::function<void(int64_t downloaded, int64_t total)>;

class StreamingAudioPlayer {
public:
    static StreamingAudioPlayer& getInstance();

    // Initialize the player
    bool init();
    void shutdown();

    // Start streaming from URL
    bool startStreaming(const std::string& url, float startPosition = 0.0f);

    // Playback control
    void play();
    void pause();
    void stop();
    void seekTo(float seconds);

    // State queries
    bool isPlaying() const { return m_isPlaying; }
    bool isPaused() const { return m_isPaused; }
    bool isStreaming() const { return m_isStreaming; }
    float getPosition() const { return m_currentPosition; }
    float getDuration() const { return m_duration; }
    float getBufferedSeconds() const;

    // Volume control (0.0 - 1.0)
    void setVolume(float volume);
    float getVolume() const { return m_volume; }

    // Playback speed
    void setSpeed(float speed);
    float getSpeed() const { return m_speed; }

    // Callbacks
    void setStateCallback(PlaybackStateCallback callback) { m_stateCallback = callback; }
    void setErrorCallback(PlaybackErrorCallback callback) { m_errorCallback = callback; }
    void setStreamingProgressCallback(StreamingProgressCallback callback) { m_streamingCallback = callback; }

private:
    StreamingAudioPlayer() = default;
    ~StreamingAudioPlayer();
    StreamingAudioPlayer(const StreamingAudioPlayer&) = delete;
    StreamingAudioPlayer& operator=(const StreamingAudioPlayer&) = delete;

    // Audio output thread
    static int audioThreadFunc(SceSize args, void* argp);
    void audioThreadLoop();

    // Download/decode thread
    static int downloadThreadFunc(SceSize args, void* argp);
    void downloadThreadLoop();

    // Circular buffer operations
    size_t bufferWrite(const uint8_t* data, size_t bytes);
    size_t bufferRead(uint8_t* data, size_t bytes);
    size_t bufferAvailable() const;
    size_t bufferFree() const;
    void bufferClear();

    // FFmpeg decoding
    bool initDecoder(const std::string& url);
    void closeDecoder();
    bool decodeFrame(std::vector<uint8_t>& pcmOut);

    // State
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_isPlaying{false};
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_isStreaming{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<float> m_currentPosition{0.0f};
    std::atomic<float> m_duration{0.0f};
    std::atomic<float> m_volume{1.0f};
    std::atomic<float> m_speed{1.0f};
    std::atomic<float> m_seekTarget{-1.0f};

    // Audio output
    int m_audioPort = -1;

    // Circular buffer for PCM data
    // 16KB buffer = ~0.09 seconds at 44100Hz stereo 16-bit
    // Use 256KB for ~1.5 seconds buffer
    static constexpr size_t BUFFER_SIZE = 256 * 1024;
    std::vector<uint8_t> m_circularBuffer;
    std::atomic<size_t> m_bufferReadPos{0};
    std::atomic<size_t> m_bufferWritePos{0};
    mutable std::mutex m_bufferMutex;

    // Threads
#ifdef __vita__
    SceUID m_audioThread = -1;
    SceUID m_downloadThread = -1;
#endif

    // FFmpeg context (opaque pointers to avoid including FFmpeg headers)
    void* m_formatCtx = nullptr;
    void* m_codecCtx = nullptr;
    void* m_swrCtx = nullptr;
    int m_audioStreamIndex = -1;

    // Current URL
    std::string m_currentUrl;

    // Callbacks
    PlaybackStateCallback m_stateCallback;
    PlaybackErrorCallback m_errorCallback;
    StreamingProgressCallback m_streamingCallback;

    // Download progress
    std::atomic<int64_t> m_downloadedBytes{0};
    std::atomic<int64_t> m_totalBytes{0};
};

} // namespace vitaabs
