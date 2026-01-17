/**
 * VitaABS - sceAvPlayer implementation
 *
 * Native Vita media player for HTTP streaming and local playback.
 */

#include "player/av_player.hpp"
#include <borealis.hpp>
#include <cstring>
#include <cstdlib>

#ifdef __vita__
#include <malloc.h>  // For memalign
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/sysmodule.h>  // For loading AvPlayer module
#endif

namespace vitaabs {

// Audio buffer configuration
static constexpr int AUDIO_GRAIN = 1024;
static constexpr int AUDIO_CHANNELS = 2;
static constexpr int AUDIO_SAMPLE_RATE = 48000;

AvPlayer& AvPlayer::getInstance() {
    static AvPlayer instance;
    return instance;
}

AvPlayer::AvPlayer() {
    brls::Logger::debug("AvPlayer: Constructor");
}

AvPlayer::~AvPlayer() {
    shutdown();
}

#ifdef __vita__

void* AvPlayer::playerAllocate(void* argP, uint32_t argAlignment, uint32_t argSize) {
    (void)argP;
    return memalign(argAlignment, argSize);
}

void AvPlayer::playerDeallocate(void* argP, void* argMemory) {
    (void)argP;
    free(argMemory);
}

void AvPlayer::playerEventCallback(void* argP, int32_t argEventId, int32_t argSourceId, void* argEventData) {
    AvPlayer* player = static_cast<AvPlayer*>(argP);
    if (!player) return;

    (void)argSourceId;
    (void)argEventData;

    brls::Logger::debug("AvPlayer: Event callback - eventId={}", argEventId);

    // sceAvPlayer event IDs (not officially documented, derived from testing/community)
    // The actual values may vary - use polling via sceAvPlayerIsActive() as primary method
    switch (argEventId) {
        case 0:  // Stream ready/info available
            brls::Logger::info("AvPlayer: Stream info available");
            // Get stream info
            {
                SceAvPlayerStreamInfo info;
                if (sceAvPlayerGetStreamInfo(player->m_avPlayer, SCE_AVPLAYER_AUDIO, &info) == 0) {
                    player->m_playbackInfo.sampleRate = info.details.audio.sampleRate;
                    player->m_playbackInfo.channels = info.details.audio.channelCount;
                    brls::Logger::info("AvPlayer: Audio - {}Hz, {} channels",
                                      info.details.audio.sampleRate, info.details.audio.channelCount);
                }
            }
            break;

        case 1:  // Playback started
            brls::Logger::info("AvPlayer: Playback started");
            player->setState(AvPlayerState::PLAYING);
            break;

        case 2:  // Playback paused
            brls::Logger::info("AvPlayer: Playback paused");
            player->setState(AvPlayerState::PAUSED);
            break;

        case 3:  // Playback stopped
            brls::Logger::info("AvPlayer: Playback stopped");
            player->setState(AvPlayerState::STOPPED);
            break;

        case 4:  // End of stream
            brls::Logger::info("AvPlayer: End of stream");
            player->setState(AvPlayerState::ENDED);
            break;

        case 5:  // Error
            brls::Logger::error("AvPlayer: Error event");
            player->m_errorMessage = "Playback error";
            player->setState(AvPlayerState::ERROR);
            break;

        default:
            brls::Logger::debug("AvPlayer: Unknown event {}", argEventId);
            break;
    }
}

int AvPlayer::audioThread(SceSize args, void* argp) {
    (void)args;
    AvPlayer* player = *static_cast<AvPlayer**>(argp);
    player->audioLoop();
    return 0;
}

void AvPlayer::audioLoop() {
    brls::Logger::info("AvPlayer: Audio thread started");

    // Allocate audio buffer
    int16_t* audioBuffer = static_cast<int16_t*>(memalign(64, AUDIO_GRAIN * AUDIO_CHANNELS * sizeof(int16_t)));
    if (!audioBuffer) {
        brls::Logger::error("AvPlayer: Failed to allocate audio buffer");
        return;
    }

    while (m_audioRunning.load()) {
        if (m_state.load() != AvPlayerState::PLAYING) {
            sceKernelDelayThread(10000);  // 10ms
            continue;
        }

        // Get audio frame from avplayer
        SceAvPlayerFrameInfo frameInfo;
        memset(&frameInfo, 0, sizeof(frameInfo));

        if (sceAvPlayerGetAudioData(m_avPlayer, &frameInfo) == 0 && frameInfo.pData) {
            // Copy audio data
            int samples = frameInfo.details.audio.size / (frameInfo.details.audio.channelCount * sizeof(int16_t));
            if (samples > AUDIO_GRAIN) samples = AUDIO_GRAIN;

            memcpy(audioBuffer, frameInfo.pData, samples * AUDIO_CHANNELS * sizeof(int16_t));

            // Output to audio port
            sceAudioOutOutput(m_audioPort, audioBuffer);
        } else {
            // No data available, output silence
            memset(audioBuffer, 0, AUDIO_GRAIN * AUDIO_CHANNELS * sizeof(int16_t));
            sceAudioOutOutput(m_audioPort, audioBuffer);
        }
    }

    free(audioBuffer);
    brls::Logger::info("AvPlayer: Audio thread ended");
}

#endif // __vita__

bool AvPlayer::init() {
    if (m_initialized.load()) {
        brls::Logger::debug("AvPlayer: Already initialized");
        return true;
    }

    brls::Logger::info("AvPlayer: Initializing...");

#ifdef __vita__
    // Load the AvPlayer system module first
    int moduleRet = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
    if (moduleRet < 0 && moduleRet != SCE_SYSMODULE_LOADED) {
        brls::Logger::error("AvPlayer: Failed to load AVPLAYER module: {:#x}", moduleRet);
        m_errorMessage = "Failed to load AvPlayer module";
        return false;
    }
    brls::Logger::info("AvPlayer: AVPLAYER module loaded");

    // Initialize sceAvPlayer
    SceAvPlayerInitData initData;
    memset(&initData, 0, sizeof(initData));

    initData.memoryReplacement.allocate = playerAllocate;
    initData.memoryReplacement.deallocate = playerDeallocate;
    initData.memoryReplacement.allocateTexture = playerAllocate;
    initData.memoryReplacement.deallocateTexture = playerDeallocate;

    initData.eventReplacement.eventCallback = playerEventCallback;
    initData.eventReplacement.objectPointer = this;

    initData.basePriority = 0xA0;
    initData.numOutputVideoFrameBuffers = 2;
    initData.autoStart = SCE_FALSE;
    initData.debugLevel = 0;

    m_avPlayer = sceAvPlayerInit(&initData);
    if (m_avPlayer < 0) {
        brls::Logger::error("AvPlayer: sceAvPlayerInit failed: {:#x}", m_avPlayer);
        m_errorMessage = "Failed to initialize player";
        return false;
    }

    brls::Logger::info("AvPlayer: sceAvPlayerInit succeeded");

    // Open audio output port
    m_audioPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,
                                       AUDIO_GRAIN,
                                       AUDIO_SAMPLE_RATE,
                                       SCE_AUDIO_OUT_MODE_STEREO);
    if (m_audioPort < 0) {
        brls::Logger::error("AvPlayer: Failed to open audio port: {:#x}", m_audioPort);
        sceAvPlayerClose(m_avPlayer);
        m_avPlayer = 0;
        m_errorMessage = "Failed to open audio output";
        return false;
    }

    // Set initial volume
    int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    sceAudioOutSetVolume(m_audioPort, static_cast<SceAudioOutChannelFlag>(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vol);

    // Create audio thread
    m_audioRunning.store(true);
    m_audioThread = sceKernelCreateThread("AvPlayerAudio", audioThread, 0x10000100,
                                          0x10000, 0, 0, nullptr);
    if (m_audioThread < 0) {
        brls::Logger::error("AvPlayer: Failed to create audio thread: {:#x}", m_audioThread);
        sceAudioOutReleasePort(m_audioPort);
        sceAvPlayerClose(m_avPlayer);
        m_avPlayer = 0;
        m_audioPort = -1;
        m_errorMessage = "Failed to create audio thread";
        return false;
    }

    AvPlayer* self = this;
    sceKernelStartThread(m_audioThread, sizeof(self), &self);

    brls::Logger::info("AvPlayer: Initialized successfully");
#endif

    m_initialized.store(true);
    m_state.store(AvPlayerState::IDLE);
    return true;
}

void AvPlayer::shutdown() {
    if (!m_initialized.load()) return;

    brls::Logger::info("AvPlayer: Shutting down...");
    m_stopping.store(true);

#ifdef __vita__
    // Stop audio thread
    m_audioRunning.store(false);
    if (m_audioThread >= 0) {
        sceKernelWaitThreadEnd(m_audioThread, nullptr, nullptr);
        sceKernelDeleteThread(m_audioThread);
        m_audioThread = -1;
    }

    // Close audio port
    if (m_audioPort >= 0) {
        sceAudioOutReleasePort(m_audioPort);
        m_audioPort = -1;
    }

    // Close avplayer
    if (m_avPlayer > 0) {
        sceAvPlayerStop(m_avPlayer);
        sceAvPlayerClose(m_avPlayer);
        m_avPlayer = 0;
    }

    // Unload the AvPlayer module
    sceSysmoduleUnloadModule(SCE_SYSMODULE_AVPLAYER);
#endif

    m_initialized.store(false);
    m_stopping.store(false);
    m_state.store(AvPlayerState::IDLE);

    brls::Logger::info("AvPlayer: Shutdown complete");
}

bool AvPlayer::loadUrl(const std::string& url, const std::string& title) {
    if (!m_initialized.load()) {
        if (!init()) return false;
    }

    brls::Logger::info("AvPlayer: Loading URL: {}", url);

    m_currentUrl = url;
    m_playbackInfo = AvPlaybackInfo();
    m_playbackInfo.title = title;
    m_playbackInfo.isStreaming = true;

    setState(AvPlayerState::LOADING);

#ifdef __vita__
    // Stop any current playback
    sceAvPlayerStop(m_avPlayer);

    // Add source
    int ret = sceAvPlayerAddSource(m_avPlayer, url.c_str());
    if (ret < 0) {
        brls::Logger::error("AvPlayer: sceAvPlayerAddSource failed: {:#x}", ret);
        m_errorMessage = "Failed to load stream";
        setState(AvPlayerState::ERROR);
        return false;
    }

    brls::Logger::info("AvPlayer: Source added, starting playback...");

    // Start playback
    ret = sceAvPlayerStart(m_avPlayer);
    if (ret < 0) {
        brls::Logger::error("AvPlayer: sceAvPlayerStart failed: {:#x}", ret);
        m_errorMessage = "Failed to start playback";
        setState(AvPlayerState::ERROR);
        return false;
    }

    // Set initial state - will be updated by event callback or polling
    setState(AvPlayerState::BUFFERING);
#endif

    return true;
}

bool AvPlayer::loadFile(const std::string& path, const std::string& title) {
    if (!m_initialized.load()) {
        if (!init()) return false;
    }

    brls::Logger::info("AvPlayer: Loading file: {}", path);

    m_currentUrl = path;
    m_playbackInfo = AvPlaybackInfo();
    m_playbackInfo.title = title;
    m_playbackInfo.isStreaming = false;

    setState(AvPlayerState::LOADING);

#ifdef __vita__
    // Stop any current playback
    sceAvPlayerStop(m_avPlayer);

    // Add source (local file path)
    int ret = sceAvPlayerAddSource(m_avPlayer, path.c_str());
    if (ret < 0) {
        brls::Logger::error("AvPlayer: sceAvPlayerAddSource failed for file: {:#x}", ret);
        m_errorMessage = "Failed to load file";
        setState(AvPlayerState::ERROR);
        return false;
    }

    // Start playback
    ret = sceAvPlayerStart(m_avPlayer);
    if (ret < 0) {
        brls::Logger::error("AvPlayer: sceAvPlayerStart failed: {:#x}", ret);
        m_errorMessage = "Failed to start playback";
        setState(AvPlayerState::ERROR);
        return false;
    }

    // Set initial state
    setState(AvPlayerState::BUFFERING);
#endif

    return true;
}

void AvPlayer::play() {
#ifdef __vita__
    if (m_avPlayer > 0) {
        sceAvPlayerResume(m_avPlayer);
        setState(AvPlayerState::PLAYING);
    }
#endif
}

void AvPlayer::pause() {
#ifdef __vita__
    if (m_avPlayer > 0) {
        sceAvPlayerPause(m_avPlayer);
        setState(AvPlayerState::PAUSED);
    }
#endif
}

void AvPlayer::stop() {
#ifdef __vita__
    if (m_avPlayer > 0) {
        sceAvPlayerStop(m_avPlayer);
    }
#endif
    setState(AvPlayerState::STOPPED);
}

void AvPlayer::togglePlayPause() {
    if (m_state.load() == AvPlayerState::PLAYING) {
        pause();
    } else if (m_state.load() == AvPlayerState::PAUSED) {
        play();
    }
}

void AvPlayer::seek(double seconds) {
#ifdef __vita__
    if (m_avPlayer > 0) {
        uint64_t timeMs = static_cast<uint64_t>(seconds * 1000);
        sceAvPlayerJumpToTime(m_avPlayer, timeMs);
        brls::Logger::debug("AvPlayer: Seeking to {}s", seconds);
    }
#endif
}

void AvPlayer::seekRelative(double seconds) {
    double newPos = getPosition() + seconds;
    if (newPos < 0) newPos = 0;
    if (m_playbackInfo.duration > 0 && newPos > m_playbackInfo.duration) {
        newPos = m_playbackInfo.duration;
    }
    seek(newPos);
}

double AvPlayer::getPosition() {
#ifdef __vita__
    if (m_avPlayer > 0) {
        uint64_t timeMs = sceAvPlayerCurrentTime(m_avPlayer);
        m_playbackInfo.position = static_cast<double>(timeMs) / 1000.0;
    }
#endif
    return m_playbackInfo.position;
}

void AvPlayer::setSpeed(float speed) {
    m_speed = speed;
#ifdef __vita__
    if (m_avPlayer > 0) {
        // sceAvPlayer uses percentage: 100 = 1x, 200 = 2x, etc.
        int32_t trickSpeed = static_cast<int32_t>(speed * 100);
        sceAvPlayerSetTrickSpeed(m_avPlayer, trickSpeed);
        brls::Logger::debug("AvPlayer: Speed set to {}x", speed);
    }
#endif
}

void AvPlayer::setVolume(int volume) {
    m_volume = volume;
#ifdef __vita__
    if (m_audioPort >= 0) {
        int vol = (volume * SCE_AUDIO_VOLUME_0DB) / 100;
        int vols[2] = {vol, vol};
        sceAudioOutSetVolume(m_audioPort, static_cast<SceAudioOutChannelFlag>(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vols);
    }
#endif
}

void AvPlayer::update() {
#ifdef __vita__
    if (!m_initialized.load() || m_avPlayer <= 0) return;

    // Check if player is still active
    SceBool active = sceAvPlayerIsActive(m_avPlayer);

    // Update state based on activity
    AvPlayerState currentState = m_state.load();
    if (active) {
        // If we were loading/buffering and now have audio data, switch to playing
        if (currentState == AvPlayerState::LOADING || currentState == AvPlayerState::BUFFERING) {
            uint64_t timeMs = sceAvPlayerCurrentTime(m_avPlayer);
            if (timeMs > 0) {
                setState(AvPlayerState::PLAYING);
            }
        }
    } else {
        // Player is not active
        if (currentState == AvPlayerState::PLAYING) {
            // Playback has ended
            setState(AvPlayerState::ENDED);
        }
    }
#endif
}

void AvPlayer::setState(AvPlayerState newState) {
    AvPlayerState oldState = m_state.exchange(newState);

    if (oldState != newState) {
        brls::Logger::debug("AvPlayer: State change {} -> {}", (int)oldState, (int)newState);

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
