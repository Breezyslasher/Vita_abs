/**
 * VitaABS - Streaming Audio Player Implementation
 *
 * Uses cspot_vita-style architecture:
 * - Circular buffer decouples download/decode from playback
 * - Dedicated audio thread feeds sceAudioOut continuously
 * - Download thread fetches HTTP and decodes to PCM with FFmpeg
 */

#include "player/streaming_audio_player.hpp"
#include <borealis/core/logger.hpp>
#include <cstring>
#include <algorithm>

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/audioout.h>
#include <psp2/kernel/processmgr.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}
#endif

namespace vitaabs {

// Audio constants
static constexpr int SAMPLE_RATE = 44100;
static constexpr int CHANNELS = 2;
static constexpr int BITS_PER_SAMPLE = 16;
static constexpr int BYTES_PER_SAMPLE = BITS_PER_SAMPLE / 8 * CHANNELS;
static constexpr int AUDIO_GRAIN = 1024;  // Samples per sceAudioOut call
static constexpr int AUDIO_BUFFER_BYTES = AUDIO_GRAIN * BYTES_PER_SAMPLE;

StreamingAudioPlayer& StreamingAudioPlayer::getInstance() {
    static StreamingAudioPlayer instance;
    return instance;
}

StreamingAudioPlayer::~StreamingAudioPlayer() {
    shutdown();
}

bool StreamingAudioPlayer::init() {
    if (m_initialized) return true;

#ifdef __vita__
    brls::Logger::info("StreamingAudioPlayer: Initializing...");

    // Allocate circular buffer
    m_circularBuffer.resize(BUFFER_SIZE);
    bufferClear();

    // Open audio port
    m_audioPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, AUDIO_GRAIN,
                                       SAMPLE_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    if (m_audioPort < 0) {
        brls::Logger::error("StreamingAudioPlayer: Failed to open audio port: {:#x}", m_audioPort);
        return false;
    }

    // Set initial volume (max)
    int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    sceAudioOutSetVolume(m_audioPort, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);

    m_initialized = true;
    brls::Logger::info("StreamingAudioPlayer: Initialized successfully");
    return true;
#else
    return false;
#endif
}

void StreamingAudioPlayer::shutdown() {
    if (!m_initialized) return;

    brls::Logger::info("StreamingAudioPlayer: Shutting down...");

    stop();

#ifdef __vita__
    if (m_audioPort >= 0) {
        sceAudioOutReleasePort(m_audioPort);
        m_audioPort = -1;
    }
#endif

    m_circularBuffer.clear();
    m_initialized = false;
}

bool StreamingAudioPlayer::startStreaming(const std::string& url, float startPosition) {
    if (!m_initialized) {
        if (!init()) return false;
    }

    brls::Logger::info("StreamingAudioPlayer: Starting stream from {}", url);

    // Stop any current playback
    stop();

    m_currentUrl = url;
    m_seekTarget = startPosition;
    m_currentPosition = 0.0f;
    m_downloadedBytes = 0;
    m_totalBytes = 0;
    m_stopRequested = false;

    // Clear buffer
    bufferClear();

#ifdef __vita__
    // Start download/decode thread
    m_downloadThread = sceKernelCreateThread("StreamDownload", downloadThreadFunc,
                                              0x10000100, 0x10000, 0, 0, nullptr);
    if (m_downloadThread < 0) {
        brls::Logger::error("StreamingAudioPlayer: Failed to create download thread");
        return false;
    }
    sceKernelStartThread(m_downloadThread, sizeof(this), &this);

    // Start audio output thread
    m_audioThread = sceKernelCreateThread("StreamAudio", audioThreadFunc,
                                           0x10000100, 0x4000, 0, 0, nullptr);
    if (m_audioThread < 0) {
        brls::Logger::error("StreamingAudioPlayer: Failed to create audio thread");
        m_stopRequested = true;
        sceKernelWaitThreadEnd(m_downloadThread, nullptr, nullptr);
        sceKernelDeleteThread(m_downloadThread);
        return false;
    }
    sceKernelStartThread(m_audioThread, sizeof(this), &this);

    m_isStreaming = true;
    m_isPlaying = true;
    m_isPaused = false;

    brls::Logger::info("StreamingAudioPlayer: Streaming started");
    return true;
#else
    return false;
#endif
}

void StreamingAudioPlayer::play() {
    if (!m_isStreaming) return;
    m_isPaused = false;
    m_isPlaying = true;
}

void StreamingAudioPlayer::pause() {
    if (!m_isStreaming) return;
    m_isPaused = true;
    m_isPlaying = false;
}

void StreamingAudioPlayer::stop() {
    if (!m_isStreaming) return;

    brls::Logger::info("StreamingAudioPlayer: Stopping playback");

    m_stopRequested = true;
    m_isPlaying = false;
    m_isPaused = false;

#ifdef __vita__
    // Wait for threads to finish
    if (m_audioThread >= 0) {
        sceKernelWaitThreadEnd(m_audioThread, nullptr, nullptr);
        sceKernelDeleteThread(m_audioThread);
        m_audioThread = -1;
    }
    if (m_downloadThread >= 0) {
        sceKernelWaitThreadEnd(m_downloadThread, nullptr, nullptr);
        sceKernelDeleteThread(m_downloadThread);
        m_downloadThread = -1;
    }
#endif

    closeDecoder();
    bufferClear();
    m_isStreaming = false;
    m_currentUrl.clear();
}

void StreamingAudioPlayer::seekTo(float seconds) {
    m_seekTarget = seconds;
}

float StreamingAudioPlayer::getBufferedSeconds() const {
    size_t buffered = bufferAvailable();
    return static_cast<float>(buffered) / (SAMPLE_RATE * BYTES_PER_SAMPLE);
}

void StreamingAudioPlayer::setVolume(float volume) {
    m_volume = std::clamp(volume, 0.0f, 1.0f);

#ifdef __vita__
    if (m_audioPort >= 0) {
        int scaledVol = static_cast<int>(m_volume * SCE_AUDIO_VOLUME_0DB);
        int vol[2] = {scaledVol, scaledVol};
        sceAudioOutSetVolume(m_audioPort, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
    }
#endif
}

void StreamingAudioPlayer::setSpeed(float speed) {
    m_speed = std::clamp(speed, 0.5f, 2.0f);
    // Note: Speed adjustment would require resampling, not implemented yet
}

// Circular buffer operations
size_t StreamingAudioPlayer::bufferWrite(const uint8_t* data, size_t bytes) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    size_t written = 0;
    while (written < bytes) {
        size_t writePos = m_bufferWritePos.load();
        size_t readPos = m_bufferReadPos.load();

        // Calculate free space
        size_t free;
        if (writePos >= readPos) {
            free = BUFFER_SIZE - writePos + readPos - 1;
        } else {
            free = readPos - writePos - 1;
        }

        if (free == 0) {
            // Buffer full, need to wait
            break;
        }

        // Write up to end of buffer or free space
        size_t toWrite = std::min(bytes - written, free);
        size_t toEnd = BUFFER_SIZE - writePos;

        if (toWrite <= toEnd) {
            std::memcpy(&m_circularBuffer[writePos], data + written, toWrite);
        } else {
            std::memcpy(&m_circularBuffer[writePos], data + written, toEnd);
            std::memcpy(&m_circularBuffer[0], data + written + toEnd, toWrite - toEnd);
        }

        m_bufferWritePos = (writePos + toWrite) % BUFFER_SIZE;
        written += toWrite;
    }

    return written;
}

size_t StreamingAudioPlayer::bufferRead(uint8_t* data, size_t bytes) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    size_t read = 0;
    size_t readPos = m_bufferReadPos.load();
    size_t writePos = m_bufferWritePos.load();

    // Calculate available data
    size_t available;
    if (writePos >= readPos) {
        available = writePos - readPos;
    } else {
        available = BUFFER_SIZE - readPos + writePos;
    }

    size_t toRead = std::min(bytes, available);
    if (toRead == 0) return 0;

    // Read from circular buffer
    size_t toEnd = BUFFER_SIZE - readPos;
    if (toRead <= toEnd) {
        std::memcpy(data, &m_circularBuffer[readPos], toRead);
    } else {
        std::memcpy(data, &m_circularBuffer[readPos], toEnd);
        std::memcpy(data + toEnd, &m_circularBuffer[0], toRead - toEnd);
    }

    m_bufferReadPos = (readPos + toRead) % BUFFER_SIZE;
    return toRead;
}

size_t StreamingAudioPlayer::bufferAvailable() const {
    size_t readPos = m_bufferReadPos.load();
    size_t writePos = m_bufferWritePos.load();

    if (writePos >= readPos) {
        return writePos - readPos;
    } else {
        return BUFFER_SIZE - readPos + writePos;
    }
}

size_t StreamingAudioPlayer::bufferFree() const {
    return BUFFER_SIZE - bufferAvailable() - 1;
}

void StreamingAudioPlayer::bufferClear() {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_bufferReadPos = 0;
    m_bufferWritePos = 0;
}

#ifdef __vita__

// Audio output thread - continuously feeds sceAudioOut
int StreamingAudioPlayer::audioThreadFunc(SceSize args, void* argp) {
    StreamingAudioPlayer* player = *reinterpret_cast<StreamingAudioPlayer**>(argp);
    player->audioThreadLoop();
    return 0;
}

void StreamingAudioPlayer::audioThreadLoop() {
    brls::Logger::info("StreamingAudioPlayer: Audio thread started");

    std::vector<uint8_t> audioBuffer(AUDIO_BUFFER_BYTES);

    while (!m_stopRequested) {
        if (m_isPaused) {
            // Output silence when paused
            std::memset(audioBuffer.data(), 0, AUDIO_BUFFER_BYTES);
            sceAudioOutOutput(m_audioPort, audioBuffer.data());
            continue;
        }

        // Try to read from circular buffer
        size_t bytesRead = bufferRead(audioBuffer.data(), AUDIO_BUFFER_BYTES);

        if (bytesRead < AUDIO_BUFFER_BYTES) {
            // Not enough data - fill rest with silence
            std::memset(audioBuffer.data() + bytesRead, 0, AUDIO_BUFFER_BYTES - bytesRead);

            if (bytesRead == 0) {
                // Buffer empty - wait a bit before trying again
                sceKernelDelayThread(10000);  // 10ms
                continue;
            }
        }

        // Output to audio hardware
        sceAudioOutOutput(m_audioPort, audioBuffer.data());

        // Update position (approximate)
        float secondsPlayed = static_cast<float>(AUDIO_GRAIN) / SAMPLE_RATE;
        m_currentPosition = m_currentPosition.load() + secondsPlayed;

        // Notify state callback
        if (m_stateCallback) {
            m_stateCallback(m_isPlaying, m_currentPosition, m_duration);
        }
    }

    brls::Logger::info("StreamingAudioPlayer: Audio thread stopped");
}

// Download/decode thread - fetches HTTP and decodes to PCM
int StreamingAudioPlayer::downloadThreadFunc(SceSize args, void* argp) {
    StreamingAudioPlayer* player = *reinterpret_cast<StreamingAudioPlayer**>(argp);
    player->downloadThreadLoop();
    return 0;
}

void StreamingAudioPlayer::downloadThreadLoop() {
    brls::Logger::info("StreamingAudioPlayer: Download thread started for {}", m_currentUrl);

    // Initialize FFmpeg decoder
    if (!initDecoder(m_currentUrl)) {
        brls::Logger::error("StreamingAudioPlayer: Failed to initialize decoder");
        if (m_errorCallback) {
            m_errorCallback("Failed to initialize audio decoder");
        }
        return;
    }

    // Handle initial seek if requested
    if (m_seekTarget >= 0) {
        AVFormatContext* fmt = static_cast<AVFormatContext*>(m_formatCtx);
        int64_t seekTs = static_cast<int64_t>(m_seekTarget.load() * AV_TIME_BASE);
        av_seek_frame(fmt, -1, seekTs, AVSEEK_FLAG_BACKWARD);
        m_currentPosition = m_seekTarget.load();
        m_seekTarget = -1.0f;
    }

    std::vector<uint8_t> pcmBuffer;

    while (!m_stopRequested) {
        // Check for seek request
        float seekTarget = m_seekTarget.load();
        if (seekTarget >= 0) {
            AVFormatContext* fmt = static_cast<AVFormatContext*>(m_formatCtx);
            int64_t seekTs = static_cast<int64_t>(seekTarget * AV_TIME_BASE);
            av_seek_frame(fmt, -1, seekTs, AVSEEK_FLAG_BACKWARD);
            bufferClear();
            m_currentPosition = seekTarget;
            m_seekTarget = -1.0f;
        }

        // Check if buffer is full
        if (bufferFree() < 16384) {
            // Buffer nearly full, wait a bit
            sceKernelDelayThread(10000);  // 10ms
            continue;
        }

        // Decode next frame
        if (!decodeFrame(pcmBuffer)) {
            // End of stream or error
            brls::Logger::info("StreamingAudioPlayer: End of stream");
            break;
        }

        // Write PCM to circular buffer
        size_t written = 0;
        while (written < pcmBuffer.size() && !m_stopRequested) {
            size_t w = bufferWrite(pcmBuffer.data() + written, pcmBuffer.size() - written);
            written += w;
            if (w == 0) {
                // Buffer full, wait
                sceKernelDelayThread(5000);  // 5ms
            }
        }
    }

    brls::Logger::info("StreamingAudioPlayer: Download thread stopped");
}

bool StreamingAudioPlayer::initDecoder(const std::string& url) {
    brls::Logger::info("StreamingAudioPlayer: Initializing decoder for {}", url);

    AVFormatContext* formatCtx = nullptr;

    // Set up options for network streaming
    AVDictionary* options = nullptr;
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "5", 0);
    av_dict_set(&options, "timeout", "10000000", 0);  // 10 seconds

    // Open input
    int ret = avformat_open_input(&formatCtx, url.c_str(), nullptr, &options);
    av_dict_free(&options);

    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        brls::Logger::error("StreamingAudioPlayer: Failed to open input: {}", errBuf);
        return false;
    }

    // Find stream info
    ret = avformat_find_stream_info(formatCtx, nullptr);
    if (ret < 0) {
        brls::Logger::error("StreamingAudioPlayer: Failed to find stream info");
        avformat_close_input(&formatCtx);
        return false;
    }

    // Find audio stream
    m_audioStreamIndex = -1;
    for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            m_audioStreamIndex = i;
            break;
        }
    }

    if (m_audioStreamIndex < 0) {
        brls::Logger::error("StreamingAudioPlayer: No audio stream found");
        avformat_close_input(&formatCtx);
        return false;
    }

    // Get duration
    if (formatCtx->duration != AV_NOPTS_VALUE) {
        m_duration = static_cast<float>(formatCtx->duration) / AV_TIME_BASE;
    }

    // Find decoder
    AVCodecParameters* codecPar = formatCtx->streams[m_audioStreamIndex]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        brls::Logger::error("StreamingAudioPlayer: Decoder not found for codec {}", codecPar->codec_id);
        avformat_close_input(&formatCtx);
        return false;
    }

    // Allocate codec context
    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        brls::Logger::error("StreamingAudioPlayer: Failed to allocate codec context");
        avformat_close_input(&formatCtx);
        return false;
    }

    // Copy codec parameters
    ret = avcodec_parameters_to_context(codecCtx, codecPar);
    if (ret < 0) {
        brls::Logger::error("StreamingAudioPlayer: Failed to copy codec parameters");
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return false;
    }

    // Open codec
    ret = avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        brls::Logger::error("StreamingAudioPlayer: Failed to open codec");
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return false;
    }

    // Set up resampler to convert to 44100Hz stereo S16
    SwrContext* swrCtx = swr_alloc();
    if (!swrCtx) {
        brls::Logger::error("StreamingAudioPlayer: Failed to allocate resampler");
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return false;
    }

    av_opt_set_int(swrCtx, "in_channel_layout", codecCtx->channel_layout ? codecCtx->channel_layout : AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(swrCtx, "in_sample_rate", codecCtx->sample_rate, 0);
    av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", codecCtx->sample_fmt, 0);
    av_opt_set_int(swrCtx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(swrCtx, "out_sample_rate", SAMPLE_RATE, 0);
    av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    ret = swr_init(swrCtx);
    if (ret < 0) {
        brls::Logger::error("StreamingAudioPlayer: Failed to initialize resampler");
        swr_free(&swrCtx);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return false;
    }

    m_formatCtx = formatCtx;
    m_codecCtx = codecCtx;
    m_swrCtx = swrCtx;

    brls::Logger::info("StreamingAudioPlayer: Decoder initialized, duration={}s, sampleRate={}, channels={}",
                       m_duration.load(), codecCtx->sample_rate, codecCtx->channels);

    return true;
}

void StreamingAudioPlayer::closeDecoder() {
    if (m_swrCtx) {
        swr_free(reinterpret_cast<SwrContext**>(&m_swrCtx));
        m_swrCtx = nullptr;
    }
    if (m_codecCtx) {
        avcodec_free_context(reinterpret_cast<AVCodecContext**>(&m_codecCtx));
        m_codecCtx = nullptr;
    }
    if (m_formatCtx) {
        avformat_close_input(reinterpret_cast<AVFormatContext**>(&m_formatCtx));
        m_formatCtx = nullptr;
    }
    m_audioStreamIndex = -1;
}

bool StreamingAudioPlayer::decodeFrame(std::vector<uint8_t>& pcmOut) {
    if (!m_formatCtx || !m_codecCtx) return false;

    AVFormatContext* formatCtx = static_cast<AVFormatContext*>(m_formatCtx);
    AVCodecContext* codecCtx = static_cast<AVCodecContext*>(m_codecCtx);
    SwrContext* swrCtx = static_cast<SwrContext*>(m_swrCtx);

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    pcmOut.clear();

    while (true) {
        // Read packet
        int ret = av_read_frame(formatCtx, packet);
        if (ret < 0) {
            // End of file or error
            av_packet_free(&packet);
            av_frame_free(&frame);
            return false;
        }

        // Skip non-audio packets
        if (packet->stream_index != m_audioStreamIndex) {
            av_packet_unref(packet);
            continue;
        }

        // Send packet to decoder
        ret = avcodec_send_packet(codecCtx, packet);
        av_packet_unref(packet);

        if (ret < 0) {
            continue;
        }

        // Receive decoded frame
        ret = avcodec_receive_frame(codecCtx, frame);
        if (ret == AVERROR(EAGAIN)) {
            continue;
        }
        if (ret < 0) {
            av_packet_free(&packet);
            av_frame_free(&frame);
            return false;
        }

        // Resample to output format
        int outSamples = av_rescale_rnd(swr_get_delay(swrCtx, codecCtx->sample_rate) + frame->nb_samples,
                                         SAMPLE_RATE, codecCtx->sample_rate, AV_ROUND_UP);

        pcmOut.resize(outSamples * CHANNELS * sizeof(int16_t));
        uint8_t* outPtr = pcmOut.data();

        int converted = swr_convert(swrCtx, &outPtr, outSamples,
                                    (const uint8_t**)frame->data, frame->nb_samples);

        if (converted > 0) {
            pcmOut.resize(converted * CHANNELS * sizeof(int16_t));
        } else {
            pcmOut.clear();
        }

        av_frame_unref(frame);
        av_packet_free(&packet);
        av_frame_free(&frame);

        return true;
    }
}

#endif // __vita__

} // namespace vitaabs
