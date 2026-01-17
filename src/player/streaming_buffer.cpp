/**
 * VitaABS - Streaming Buffer Manager Implementation
 *
 * Uses native Vita HTTP (via HttpClient/libcurl) to download audio
 * progressively to a temp file, allowing MPV to play from local file
 * while download continues.
 */

#include "player/streaming_buffer.hpp"
#include "app/temp_file_manager.hpp"
#include "utils/http_client.hpp"
#include "utils/async.hpp"

#include <borealis.hpp>
#include <cstdio>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#endif

namespace vitaabs {

StreamingBufferManager::StreamingBufferManager(const std::string& itemId,
                                               const std::string& episodeId)
    : m_itemId(itemId), m_episodeId(episodeId) {
    brls::Logger::debug("StreamingBufferManager created for item: {}", itemId);
}

StreamingBufferManager::~StreamingBufferManager() {
    stop();
}

double StreamingBufferManager::getBufferPercent() const {
    int64_t total = m_totalSize.load();
    if (total <= 0) return 0.0;
    return (double)m_bufferedSize.load() / total * 100.0;
}

bool StreamingBufferManager::startDownload(const std::string& streamUrl,
                                           const std::string& extension) {
    if (m_downloading.load()) {
        brls::Logger::warning("StreamingBufferManager: Already downloading");
        return false;
    }

    // Get temp file path from TempFileManager
    m_tempPath = TempFileManager::getInstance().getTempFilePath(
        m_itemId + "_stream", m_episodeId, extension);

    if (m_tempPath.empty()) {
        brls::Logger::error("StreamingBufferManager: Failed to get temp file path");
        m_errorMessage = "Failed to create temp file";
        setState(BufferState::ERROR);
        return false;
    }

    brls::Logger::info("StreamingBufferManager: Starting download to {}", m_tempPath);
    brls::Logger::info("StreamingBufferManager: URL: {}", streamUrl);

    m_downloading.store(true);
    m_cancelled.store(false);
    m_bufferedSize.store(0);
    m_totalSize.store(0);
    m_thresholdReached = false;

    setState(BufferState::BUFFERING);

    // Start download in background thread with large stack
    asyncRunLargeStack([this, streamUrl, extension]() {
        downloadWorker(streamUrl, extension);
    });

    return true;
}

void StreamingBufferManager::downloadWorker(const std::string& streamUrl,
                                            const std::string& extension) {
    brls::Logger::info("StreamingBufferManager: Download thread started");

#ifdef __vita__
    // Open temp file for writing using Vita API
    SceUID fd = sceIoOpen(m_tempPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) {
        brls::Logger::error("StreamingBufferManager: Failed to open temp file: {} (error: {:#x})",
                          m_tempPath, fd);
        m_errorMessage = "Failed to create temp file";
        m_downloading.store(false);
        setState(BufferState::ERROR);
        return;
    }

    // Create HTTP client for download
    HttpClient httpClient;
    httpClient.setTimeout(300);  // 5 minute timeout for large files

    int64_t lastProgressUpdate = 0;

    // Download with write callback
    bool success = httpClient.downloadFile(streamUrl,
        // Write callback - called for each chunk of data
        [this, fd, &lastProgressUpdate](const char* data, size_t size) -> bool {
            if (m_cancelled.load()) {
                return false;  // Cancel download
            }

            // Write to file
            int written = sceIoWrite(fd, data, size);
            if (written < 0 || static_cast<size_t>(written) != size) {
                brls::Logger::error("StreamingBufferManager: Write error: {}", written);
                return false;
            }

            // Update buffered size
            int64_t buffered = m_bufferedSize.fetch_add(size) + size;

            // Check if we've reached the threshold
            if (!m_thresholdReached && buffered >= m_bufferThreshold) {
                m_thresholdReached = true;
                brls::Logger::info("StreamingBufferManager: Buffer threshold reached ({}MB)",
                                  buffered / (1024 * 1024));

                // Sync file to ensure data is written
                sceIoSyncByFd(fd, 0);

                setState(BufferState::READY);
            }

            // Update progress every 256KB
            if (buffered - lastProgressUpdate >= 256 * 1024) {
                lastProgressUpdate = buffered;
                if (m_progressCallback) {
                    int64_t total = m_totalSize.load();
                    brls::sync([this, buffered, total]() {
                        if (m_progressCallback) {
                            m_progressCallback(buffered, total);
                        }
                    });
                }
            }

            return true;
        },
        // Size callback - called when total size is known
        [this](int64_t totalSize) {
            m_totalSize.store(totalSize);
            brls::Logger::info("StreamingBufferManager: Total size: {}MB",
                              totalSize / (1024 * 1024));
        }
    );

    // Close file
    sceIoClose(fd);

    m_downloading.store(false);

    if (m_cancelled.load()) {
        brls::Logger::info("StreamingBufferManager: Download cancelled");
        // Delete incomplete file
        sceIoRemove(m_tempPath.c_str());
        setState(BufferState::IDLE);
        return;
    }

    if (success) {
        brls::Logger::info("StreamingBufferManager: Download completed successfully ({}MB)",
                          m_bufferedSize.load() / (1024 * 1024));

        // Register temp file with manager
        TempFileManager::getInstance().registerTempFile(
            m_itemId + "_stream", m_episodeId, m_tempPath, m_itemId, m_bufferedSize.load());

        setState(BufferState::COMPLETE);

        // Final progress callback
        if (m_progressCallback) {
            int64_t buffered = m_bufferedSize.load();
            int64_t total = m_totalSize.load();
            brls::sync([this, buffered, total]() {
                if (m_progressCallback) {
                    m_progressCallback(buffered, total);
                }
            });
        }
    } else {
        brls::Logger::error("StreamingBufferManager: Download failed");
        m_errorMessage = "Download failed";
        // Delete incomplete file
        sceIoRemove(m_tempPath.c_str());
        setState(BufferState::ERROR);
    }

#else
    // Non-Vita implementation using standard C file I/O
    FILE* file = std::fopen(m_tempPath.c_str(), "wb");
    if (!file) {
        brls::Logger::error("StreamingBufferManager: Failed to open temp file: {}", m_tempPath);
        m_errorMessage = "Failed to create temp file";
        m_downloading.store(false);
        setState(BufferState::ERROR);
        return;
    }

    HttpClient httpClient;
    httpClient.setTimeout(300);

    bool success = httpClient.downloadFile(streamUrl,
        [this, file](const char* data, size_t size) -> bool {
            if (m_cancelled.load()) return false;

            size_t written = std::fwrite(data, 1, size, file);
            if (written != size) return false;

            int64_t buffered = m_bufferedSize.fetch_add(size) + size;

            if (!m_thresholdReached && buffered >= m_bufferThreshold) {
                m_thresholdReached = true;
                std::fflush(file);
                setState(BufferState::READY);
            }

            return true;
        },
        [this](int64_t totalSize) {
            m_totalSize.store(totalSize);
        }
    );

    std::fclose(file);
    m_downloading.store(false);

    if (success && !m_cancelled.load()) {
        setState(BufferState::COMPLETE);
    } else if (!m_cancelled.load()) {
        setState(BufferState::ERROR);
    }
#endif
}

void StreamingBufferManager::stop() {
    if (!m_downloading.load() && m_state.load() == BufferState::IDLE) {
        return;
    }

    brls::Logger::debug("StreamingBufferManager: Stopping...");
    m_cancelled.store(true);

    // Wait for download thread to finish
    int timeout = 50;  // 5 seconds max
    while (m_downloading.load() && timeout > 0) {
#ifdef __vita__
        sceKernelDelayThread(100000);  // 100ms
#endif
        timeout--;
    }

    setState(BufferState::IDLE);
    brls::Logger::debug("StreamingBufferManager: Stopped");
}

void StreamingBufferManager::cancel() {
    stop();

    // Delete the temp file
    if (!m_tempPath.empty()) {
#ifdef __vita__
        sceIoRemove(m_tempPath.c_str());
#else
        std::remove(m_tempPath.c_str());
#endif
        brls::Logger::debug("StreamingBufferManager: Temp file deleted");
    }
}

void StreamingBufferManager::setState(BufferState newState) {
    BufferState oldState = m_state.exchange(newState);

    if (oldState != newState) {
        brls::Logger::debug("StreamingBufferManager: State change: {} -> {}",
                          (int)oldState, (int)newState);

        if (m_stateCallback) {
            brls::sync([this, newState]() {
                if (m_stateCallback) {
                    m_stateCallback(newState);
                }
            });
        }
    }
}

} // namespace vitaabs
