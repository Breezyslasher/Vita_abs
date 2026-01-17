/**
 * VitaABS - Streaming Buffer Manager
 * Progressive download + playback using native Vita HTTP
 *
 * This avoids MPV's internal HTTP handling which crashes on Vita,
 * by using libcurl (native Vita HTTP) to download to a temp file
 * and having MPV play from the local file.
 */

#pragma once

#include <string>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace vitaabs {

// Streaming buffer state
enum class BufferState {
    IDLE,           // Not downloading
    BUFFERING,      // Downloading, waiting for threshold
    READY,          // Enough buffered, ready to play
    STREAMING,      // Playing while still downloading
    COMPLETE,       // Download complete
    ERROR           // Error occurred
};

// Streaming buffer callbacks
using BufferStateCallback = std::function<void(BufferState state)>;
using BufferProgressCallback = std::function<void(int64_t buffered, int64_t total)>;

/**
 * Manages progressive download + playback of audio streams
 *
 * Usage:
 *   auto buffer = std::make_shared<StreamingBufferManager>(itemId, episodeId);
 *   buffer->setStateCallback([](BufferState s) {
 *       if (s == BufferState::READY) { player.loadFile(buffer->getTempPath()); }
 *   });
 *   buffer->startDownload(streamUrl, ".mp3");
 */
class StreamingBufferManager {
public:
    StreamingBufferManager(const std::string& itemId, const std::string& episodeId = "");
    ~StreamingBufferManager();

    // Lifecycle
    bool startDownload(const std::string& streamUrl, const std::string& extension = ".mp3");
    void stop();
    void cancel();

    // Callbacks
    void setStateCallback(BufferStateCallback callback) { m_stateCallback = callback; }
    void setProgressCallback(BufferProgressCallback callback) { m_progressCallback = callback; }

    // Status
    BufferState getState() const { return m_state.load(); }
    std::string getTempPath() const { return m_tempPath; }
    int64_t getBufferedSize() const { return m_bufferedSize.load(); }
    int64_t getTotalSize() const { return m_totalSize.load(); }
    double getBufferPercent() const;
    bool isReady() const { return m_state.load() >= BufferState::READY; }
    bool isComplete() const { return m_state.load() == BufferState::COMPLETE; }
    std::string getErrorMessage() const { return m_errorMessage; }

    // Configuration
    void setBufferThreshold(int64_t bytes) { m_bufferThreshold = bytes; }
    int64_t getBufferThreshold() const { return m_bufferThreshold; }

private:
    // Download worker thread
    void downloadWorker(const std::string& streamUrl, const std::string& extension);

    // State notification
    void setState(BufferState newState);

    std::string m_itemId;
    std::string m_episodeId;
    std::string m_tempPath;
    std::string m_errorMessage;

    std::atomic<BufferState> m_state{BufferState::IDLE};
    std::atomic<int64_t> m_bufferedSize{0};
    std::atomic<int64_t> m_totalSize{0};
    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_downloading{false};

    int64_t m_bufferThreshold = 2 * 1024 * 1024;  // 2 MB default

    BufferStateCallback m_stateCallback;
    BufferProgressCallback m_progressCallback;

    std::mutex m_fileMutex;

    bool m_thresholdReached = false;
};

} // namespace vitaabs
