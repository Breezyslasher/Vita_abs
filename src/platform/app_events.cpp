/**
 * VitaABS - Vita application events (see app_events.hpp).
 *
 * The quit handling is VitaPlex's (src/utils/background_audio_psv.cpp), which
 * learned it on hardware; the rest is the logging a test build reports.
 */

#include "platform/app_events.hpp"

#ifdef __vita__

#include "player/mpv_player.hpp"

#include <borealis.hpp>
#include <borealis/core/timer.hpp>

#include <atomic>
#include <cstring>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

// Not declared by this SDK's headers; the stubs are in libSceAppMgr_stub,
// which the app links already. psp2/appmgr.h is left out on purpose, so a
// later SDK declaring these with its own struct types cannot clash.
extern "C" {
int sceAppMgrReceiveEventNum(int* eventNum);
int sceAppMgrReceiveEvent(void* event);
}

namespace vitaabs {
namespace appevents {

namespace {

// SceAppMgrEvent as firmware 3.60 fills it in.
struct AppEvent {
    int event;
    int appId;
    char param[56];
};
static_assert(sizeof(AppEvent) == 0x40, "SceAppMgrEvent is 0x40 bytes");

constexpr int kEventActivate    = 0x10000001;  // back in front
constexpr int kEventDeactivate  = 0x10000002;  // left with PS: keeps running
constexpr int kEventResume      = 0x10000003;
constexpr int kEventRequestQuit = 0x20000001;  // asked to quit: MUST be answered

constexpr SceUInt  kTickUs    = 250 * 1000;
constexpr int      kBeatTicks = 20;            // a heartbeat line every 5 s away
// How long the normal way out gets after the system asks the app to quit,
// before the watcher ends the process itself. Short while the UI thread has
// not even taken the request, since then it is stuck (in a request to a
// server it cannot reach, say). Longer once it has, so a shutdown already
// under way is not cut off: it rewrites settings.json, and ending the
// process mid-write would lose the login. VitaPlex's device log (280b1532)
// had the whole of it take 1.2 s.
constexpr SceInt64 kQuitGraceUs    = 2 * 1000000;
constexpr SceInt64 kQuitShutdownUs = 10 * 1000000;

// Written by the UI loop's 1 Hz timer, read by the watcher: proof, in a log
// taken while away, that the UI loop (and so the player's own timers — the
// Music row heartbeat, progress sync) is still running. The watcher never
// touches mpv itself; mpv may be torn down on the UI thread at any moment.
std::atomic<unsigned> g_uiTicks{0};
std::atomic<int>      g_playing{0};
std::atomic<int>      g_positionS{-1};

// Watcher thread only.
bool     g_away = false;
SceInt64 g_awayAt = 0;
unsigned g_awayUiTicks = 0;
int      g_awayTicks = 0;
SceInt64 g_quitAskedAt = 0;
// Set on the UI thread once it has taken the quit request.
std::atomic<bool> g_quitUnderway{false};

void onRequestQuit(SceInt64 now) {
    if (g_quitAskedAt != 0) return;
    g_quitAskedAt = now;
    brls::Logger::info("[appevents] the system asked the app to quit; quitting");
    // The normal way out first: it saves the settings and lets the player
    // close its session. The watcher ends the process if that stalls.
    brls::sync([]() {
        g_quitUnderway = true;
        brls::Application::quit();
    });
}

void drainEvents(SceInt64 now) {
    int n = 0;
    if (sceAppMgrReceiveEventNum(&n) < 0 || n <= 0) return;
    for (int i = 0; i < n; i++) {
        AppEvent ev;
        std::memset(&ev, 0, sizeof(ev));
        if (sceAppMgrReceiveEvent(&ev) < 0) break;
        switch (ev.event) {
            case kEventDeactivate:
                g_away = true;
                g_awayAt = now;
                g_awayUiTicks = g_uiTicks;
                g_awayTicks = 0;
                brls::Logger::info("[appevents] left with PS ({}, at {}s); still running",
                                   g_playing ? "playing" : "not playing", g_positionS.load());
                break;
            case kEventActivate:
                if (g_away) {
                    brls::Logger::info("[appevents] back after {:.1f}s; UI loop ran {} times "
                                       "while away", (now - g_awayAt) / 1e6,
                                       g_uiTicks - g_awayUiTicks);
                } else {
                    brls::Logger::info("[appevents] activated");
                }
                g_away = false;
                break;
            case kEventResume:
                brls::Logger::info("[appevents] resume");
                break;
            case kEventRequestQuit:
                onRequestQuit(now);
                break;
            default:
                brls::Logger::info("[appevents] event 0x{:08X}", (unsigned)ev.event);
                break;
        }
    }
}

int watcherMain(SceSize, void*) {
    brls::Logger::info("[appevents] watcher running");
    for (;;) {
        sceKernelDelayThread(kTickUs);
        const SceInt64 now = sceKernelGetSystemTimeWide();

        drainEvents(now);

        // An unanswered quit hangs the console, so this is not optional.
        if (g_quitAskedAt != 0) {
            const bool underway = g_quitUnderway;
            if (now - g_quitAskedAt > (underway ? kQuitShutdownUs : kQuitGraceUs)) {
                brls::Logger::warning("[appevents] still running {:.1f}s after the system "
                                      "asked to quit ({}); ending the process",
                                      (now - g_quitAskedAt) / 1e6,
                                      underway ? "the shutdown did not finish"
                                               : "the UI never took the request");
                sceKernelExitProcess(0);
            }
        }

        if (g_away && ++g_awayTicks % kBeatTicks == 0) {
            brls::Logger::info("[appevents] away {:.0f}s: {}, at {}s, UI loop +{}",
                               (now - g_awayAt) / 1e6,
                               g_playing ? "playing" : "not playing", g_positionS.load(),
                               g_uiTicks - g_awayUiTicks);
        }
    }
    return 0;
}

}  // namespace

void init() {
    static bool started = false;
    if (started) return;
    started = true;

    // A UI-loop timer: counts the loop's runs and samples the player on the
    // UI thread, where touching mpv is safe. Deliberately never destroyed —
    // it must outlive every activity and must not run its destructor during
    // static teardown after borealis has gone.
    auto* uiTimer = new brls::RepeatingTimer();
    uiTimer->setCallback([]() {
        g_uiTicks++;
        MpvPlayer& player = MpvPlayer::getInstance();
        if (player.isInitialized()) {
            g_playing = player.isPlaying() ? 1 : 0;
            g_positionS = static_cast<int>(player.getPosition());
        } else {
            g_playing = 0;
            g_positionS = -1;
        }
    });
    uiTimer->start(1000);

    SceUID thid = sceKernelCreateThread("VitaABSAppEvents", watcherMain, 0x10000100, 0x10000,
                                        0, 0, nullptr);
    if (thid < 0 || sceKernelStartThread(thid, 0, nullptr) < 0) {
        brls::Logger::error("[appevents] watcher did not start (0x{:08X}); a quit request "
                            "from the system would go unanswered", (unsigned)thid);
    }
}

}  // namespace appevents
}  // namespace vitaabs

#endif  // __vita__
