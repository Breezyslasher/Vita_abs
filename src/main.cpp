/**
 * VitaABS - Audiobookshelf Client for PlayStation Vita
 * Main entry point with Borealis UI framework
 *
 * Based on switchfin architecture (https://github.com/dragonflylee/switchfin)
 * UI powered by Borealis (https://github.com/dragonflylee/borealis)
 */

#include <borealis.hpp>
#include "app/application.hpp"
#include "app/audiobookshelf_client.hpp"
#include "view/media_item_cell.hpp"
#include "view/recycling_grid.hpp"
#include "view/media_detail_view.hpp"
#include "app/downloads_manager.hpp"
#include "utils/http_client.hpp"
#include <clocale>

#if defined(__ANDROID__)
#include "platform/android_assets.hpp"
#include <SDL.h>
#endif

#ifdef __PS4__
#include <sys/stat.h>
#include <orbis/Sysmodule.h>
#include <thread>
#include <chrono>
#elif defined(__SWITCH__)
#include <sys/stat.h>
#endif

#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/power.h>
#include <psp2/apputil.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/net/http.h>
#include <psp2/libssl.h>
#include <psp2/common_dialog.h>
#include <psp2/io/stat.h>
#include <cstring>

// Memory configuration
int _newlib_heap_size_user = 192 * 1024 * 1024;  // 192 MB heap
unsigned int sceUserMainThreadStackSize = 2 * 1024 * 1024;  // 2 MB stack

#define NET_MEMORY_SIZE (4 * 1024 * 1024)
#define SSL_MEMORY_SIZE (512 * 1024)
#define HTTP_MEMORY_SIZE (2 * 1024 * 1024)

static char __attribute__((aligned(64))) netMemory[NET_MEMORY_SIZE];

/**
 * Load shader compiler module
 */
static bool loadShaderCompiler() {
    SceUID mod;

    mod = sceKernelLoadStartModule("ur0:data/libshacccg.suprx", 0, NULL, 0, NULL, NULL);
    if (mod >= 0) return true;

    mod = sceKernelLoadStartModule("vs0:sys/external/libshacccg.suprx", 0, NULL, 0, NULL, NULL);
    if (mod >= 0) return true;

    brls::Logger::warning("Could not load libshacccg.suprx - using fallback shaders");
    return true;
}

/**
 * Initialize Vita system modules
 */
static bool initVitaSystem() {
    brls::Logger::info("Initializing PS Vita system modules...");

    // App utilities
    SceAppUtilInitParam initParam;
    SceAppUtilBootParam bootParam;
    memset(&initParam, 0, sizeof(initParam));
    memset(&bootParam, 0, sizeof(bootParam));
    sceAppUtilInit(&initParam, &bootParam);

    // Set max clock speeds
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    // Load shader compiler
    loadShaderCompiler();

    // Load system modules
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);
    // SceAvPlayer intentionally NOT loaded - we use MPV/libmpv for playback.
    // Loading SceAvPlayer pulls in SceMp4 as a dependency, and "Mp4Demux Critical"
    // appears in crash dumps during HTTP streaming, indicating a conflict with
    // MPV's FFmpeg-based demuxer using the same SceAvcodec/SceVideodec resources.
    sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
    sceSysmoduleLoadModule(SCE_SYSMODULE_PGF);

    brls::Logger::info("System modules loaded");
    return true;
}

/**
 * Initialize networking
 */
static bool initVitaNetwork() {
    brls::Logger::info("Initializing networking...");

    SceNetInitParam netInitParam;
    netInitParam.memory = netMemory;
    netInitParam.size = NET_MEMORY_SIZE;
    netInitParam.flags = 0;

    int ret = sceNetInit(&netInitParam);
    if (ret < 0 && ret != (int)0x80410201) {
        brls::Logger::error("sceNetInit failed: {:#x}", ret);
        return false;
    }

    ret = sceNetCtlInit();
    if (ret < 0 && ret != (int)0x80412102) {
        brls::Logger::error("sceNetCtlInit failed: {:#x}", ret);
        return false;
    }

    ret = sceSslInit(SSL_MEMORY_SIZE);
    if (ret < 0 && ret != (int)0x80435001) {
        brls::Logger::error("sceSslInit failed: {:#x}", ret);
        return false;
    }

    ret = sceHttpInit(HTTP_MEMORY_SIZE);
    if (ret < 0 && ret != (int)0x80431002) {
        brls::Logger::error("sceHttpInit failed: {:#x}", ret);
        return false;
    }

    // Initialize curl
    vitaabs::HttpClient::globalInit();

    brls::Logger::info("Networking initialized");
    return true;
}

/**
 * Cleanup networking
 */
static void cleanupVitaNetwork() {
    vitaabs::HttpClient::globalCleanup();
    sceHttpTerm();
    sceSslTerm();
    sceNetCtlTerm();
    sceNetTerm();
}
#endif // __vita__

/**
 * Register custom views
 */
static void registerCustomViews() {
    brls::Application::registerXMLView("MediaItemCell", vitaabs::MediaItemCell::create);
    brls::Application::registerXMLView("RecyclingGrid", vitaabs::RecyclingGrid::create);
}

/**
 * Main entry point
 */
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::setlocale(LC_ALL, "C.UTF-8");

#ifdef __vita__
    // Initialize Vita-specific systems
    if (!initVitaSystem()) {
        sceKernelExitProcess(1);
        return 1;
    }

    if (!initVitaNetwork()) {
        sceKernelExitProcess(1);
        return 1;
    }

    // Create log directory and file on Vita
    sceIoMkdir("ux0:data/VitaABS", 0777);
    static FILE* logFile = std::fopen("ux0:data/VitaABS/vitaabs.log", "w");
    if (logFile) {
        setvbuf(logFile, NULL, _IOLBF, 0);
    }
#else
#ifdef __PS4__
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET) < 0)
        brls::Logger::error("Cannot load PS4 net module");
    std::this_thread::sleep_for(std::chrono::seconds(2));
#endif
    if (!vitaabs::HttpClient::globalInit()) {
        brls::Logger::error("Failed to initialize curl");
        return 1;
    }
#endif

#ifdef __PS4__
    mkdir("/data/VitaABS", 0777);
    static FILE* logFile = std::fopen("/data/VitaABS/debug.log", "w");
    if (logFile) {
        setvbuf(logFile, NULL, _IOLBF, 0);
    }
#elif defined(__SWITCH__)
    mkdir("sdmc:/VitaABS", 0777);
    static FILE* logFile = std::fopen("sdmc:/VitaABS/debug.log", "w");
    if (logFile) {
        setvbuf(logFile, NULL, _IOLBF, 0);
    }
#endif

    // Initialize Borealis
    brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);

#if defined(__ANDROID__)
    extractAndroidAssets();
#endif

    if (!brls::Application::init()) {
        brls::Logger::error("Failed to initialize Borealis");
#ifdef __vita__
        if (logFile) fclose(logFile);
        cleanupVitaNetwork();
        sceKernelExitProcess(1);
#else
        vitaabs::HttpClient::globalCleanup();
#endif
        return 1;
    }

#if defined(__ANDROID__)
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown");
#endif

#if defined(__vita__) || defined(__PS4__) || defined(__SWITCH__)
    if (logFile) {
        brls::Logger::getLogEvent()->subscribe([](brls::Logger::TimePoint time, brls::LogLevel level, std::string log) {
            if (!logFile) return;

            const char* levelStr = "UNKNOWN";
            switch (level) {
                case brls::LogLevel::LOG_ERROR: levelStr = "ERROR"; break;
                case brls::LogLevel::LOG_WARNING: levelStr = "WARNING"; break;
                case brls::LogLevel::LOG_INFO: levelStr = "INFO"; break;
                case brls::LogLevel::LOG_DEBUG: levelStr = "DEBUG"; break;
                case brls::LogLevel::LOG_VERBOSE: levelStr = "VERBOSE"; break;
            }

            std::time_t tt = std::chrono::system_clock::to_time_t(time);
            std::tm time_tm = *std::localtime(&tt);
            uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                time.time_since_epoch()).count() % 1000;

            fprintf(logFile, "%02d:%02d:%02d.%03d [%s] %s\n",
                    time_tm.tm_hour, time_tm.tm_min, time_tm.tm_sec,
                    (int)ms, levelStr, log.c_str());
        });
#ifdef __vita__
        brls::Logger::info("Log file initialized: ux0:data/VitaABS/vitaabs.log");
#elif defined(__SWITCH__)
        brls::Logger::info("Log file initialized: sdmc:/VitaABS/debug.log");
#else
        brls::Logger::info("Log file initialized: /data/VitaABS/debug.log");
#endif
    }
#endif

    // Override sidebar padding for better text visibility on Vita's small screen
    brls::Style style = brls::getStyle();
    style.addMetric("brls/sidebar/padding_left", 20.0f);
    style.addMetric("brls/sidebar/padding_right", 20.0f);

    // Create window
    brls::Application::createWindow("VitaABS");

    // Set theme colors
    brls::Application::getPlatform()->getThemeVariant();

    // Register custom views
    registerCustomViews();

    // Initialize downloads manager
    vitaabs::DownloadsManager::getInstance().init();

    // Initialize application
    vitaabs::Application& app = vitaabs::Application::getInstance();

    if (!app.init()) {
        brls::Logger::error("Failed to initialize VitaABS");
#ifdef __vita__
        cleanupVitaNetwork();
        sceKernelExitProcess(1);
#else
        vitaabs::HttpClient::globalCleanup();
#endif
        return 1;
    }

    // Run application (blocking)
    app.run();

    // Shutdown
    app.shutdown();

#ifdef __vita__
    cleanupVitaNetwork();
    sceKernelExitProcess(0);
#else
    vitaabs::HttpClient::globalCleanup();
#endif

    return 0;
}
