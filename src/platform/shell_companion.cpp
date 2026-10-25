/**
 * VitaABS - vita-shell-companion bridge (Vita only; see shell_companion.hpp).
 */

#include "platform/shell_companion.hpp"

#ifdef __vita__

#include "platform/platform.hpp"
#include "utils/async.hpp"
#include "utils/http_client.hpp"

#include <borealis.hpp>

#include <atomic>
#include <mutex>
#include <vector>

#include <psp2/kernel/processmgr.h>

extern "C" {
#include "shell_companion.h"
#include "sc_cover.h"
}

namespace vitaabs {
namespace shell {

namespace {

// This test build's features. The plugin ignores (and logs) commands for a
// feature that was not asked for in the HELLO, so a bit goes in only once
// the app handles it.
constexpr uint32_t kFeatures = SC_FEAT_ROW;

constexpr const char* kTitleId      = "VABS00001";
constexpr const char* kDisplayName  = "Audiobookshelf";
constexpr const char* kBundledSuprx = "app0:module/shell_companion.suprx";

std::atomic<bool> g_initialised{false};
std::atomic<bool> g_open{false};

// Last values sent to the row, so per-tick calls only send real changes.
// Touched from the UI thread only.
std::string g_title, g_artist, g_album;
int g_playing = -1;

// Covers: serials must only grow during a boot (scCoverNextSerial), and a
// newer request supersedes an older one still fetching.
std::mutex g_coverMutex;
uint32_t g_lastSerial = 0;
std::atomic<uint32_t> g_coverGeneration{0};

void writeCover(const std::vector<uint8_t>& bytes, uint32_t generation) {
    if (bytes.empty() || !g_open) return;
    if (generation != g_coverGeneration.load()) return;  // superseded

    std::lock_guard<std::mutex> lock(g_coverMutex);
    uint32_t serial = scCoverNextSerial(g_lastSerial, sceKernelGetSystemTimeWide());
    char why[128] = {0};
    if (scCoverWrite(bytes.data(), bytes.size(), serial, why, sizeof(why)) != 0) {
        brls::Logger::warning("ShellCompanion: cover not written: {}", why);
        return;
    }
    if (generation != g_coverGeneration.load()) return;  // superseded while writing
    g_lastSerial = serial;
    int rc = scRowArt(serial);
    brls::Logger::info("ShellCompanion: row cover serial {} ({} bytes) -> {}", serial,
                       bytes.size(), rc);
}

// Which plugin answered. Several apps ship this plugin (VitaPlex does too),
// and the first one loaded after a reboot stays in SceShell until the next,
// so the copy answering may be older than the one in this VPK. A test report
// is only readable alongside this line.
void logPluginInfo() {
    ScPluginInfo info;
    if (scPluginInfo(&info) != 0) {
        brls::Logger::warning("ShellCompanion: the plugin in SceShell did not report a version — "
                              "it predates 0.2.0, probably loaded this boot by another app. "
                              "Restart the Vita to load this app's own copy.");
        return;
    }
    brls::Logger::info("ShellCompanion: plugin {} (code {}, protocol {}), has 0x{:X}, "
                       "granted 0x{:X} of asked 0x{:X}, caps [{}]",
                       info.version, info.versionCode, info.protocol, info.features,
                       info.granted, kFeatures, info.caps);
    if ((info.granted & kFeatures) != kFeatures) {
        brls::Logger::warning("ShellCompanion: not every feature asked for was granted "
                              "(missing 0x{:X})", kFeatures & ~info.granted);
    }
}

}  // namespace

void init() {
    if (g_initialised.exchange(true)) return;

    int rc = scInit(kTitleId, platform::dataDir().c_str(), kDisplayName, kFeatures,
                    kBundledSuprx);
    if (rc != 0) {
        brls::Logger::error("ShellCompanion: scInit failed ({})", rc);
        return;
    }

    // scOpen may load the plugin into SceShell (taiHEN), so keep it off the
    // UI thread. The log line is the first thing a test build reports.
    asyncRun([]() {
        char what[256] = {0};
        int open = scOpen(what, sizeof(what));
        g_open = (open == 0);
        if (open == 0) {
            brls::Logger::info("ShellCompanion: open ({})", what);
            logPluginInfo();
        } else {
            brls::Logger::error("ShellCompanion: scOpen failed, code {} ({})", open, what);
        }
    });
}

bool isOpen() { return g_open; }

void rowHeartbeat() {
    if (!g_open) return;
    scRowActivate();
}

void rowRelease() {
    if (!g_open) return;
    scRowRelease();
    // A later player starts from scratch: resend everything to the new owner.
    g_title.clear();
    g_artist.clear();
    g_album.clear();
    g_playing = -1;
    g_coverGeneration++;
}

void rowSong(const std::string& title, const std::string& artist, const std::string& album) {
    if (!g_open) return;
    if (title == g_title && artist == g_artist && album == g_album) return;
    g_title = title;
    g_artist = artist;
    g_album = album;
    scRowSong(title.c_str(), artist.c_str(), album.c_str());
}

void rowPlaying(bool playing) {
    if (!g_open) return;
    int p = playing ? 1 : 0;
    if (p == g_playing) return;
    g_playing = p;
    scRowPlaying(p);
}

void rowCoverFromUrl(const std::string& url) {
    if (!g_open || url.empty()) return;
    uint32_t generation = ++g_coverGeneration;
    asyncRun([url, generation]() {
        HttpClient http;
        std::string body;
        if (!http.get(url, body) || body.empty()) {
            brls::Logger::warning("ShellCompanion: cover fetch failed");
            return;
        }
        writeCover(std::vector<uint8_t>(body.begin(), body.end()), generation);
    });
}

void rowCoverFromFile(const std::string& path) {
    if (!g_open || path.empty()) return;
    uint32_t generation = ++g_coverGeneration;
    asyncRun([path, generation]() {
        writeCover(platform::readFile(path), generation);
    });
}

Press poll() {
    if (!g_open) return Press::None;
    switch (scPoll()) {
        case SC_PLAY_PAUSE: return Press::PlayPause;
        case SC_NEXT:       return Press::Next;
        case SC_PREVIOUS:   return Press::Previous;
        default:            return Press::None;
    }
}

}  // namespace shell
}  // namespace vitaabs

#endif  // __vita__
