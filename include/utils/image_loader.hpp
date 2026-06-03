/**
 * VitaABS - Asynchronous Image Loader
 */

#pragma once

#include <borealis.hpp>
#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <atomic>
namespace vitaabs {

class ImageLoader {
public:
    using LoadCallback = std::function<void(brls::Image*)>;

    // Load image asynchronously from URL
    static void loadAsync(const std::string& url, LoadCallback callback,
                          brls::Image* target, std::shared_ptr<std::atomic<bool>> alive);

    // Clear image cache
    static void clearCache();

    // Cancel all pending loads
    static void cancelAll();

    // Pause/resume image loading (prevents new loads from being dispatched)
    static void setPaused(bool paused);

    // Check if loading is paused
    static bool isPaused();

private:
    static std::map<std::string, std::vector<uint8_t>> s_cache;
    static std::mutex s_cacheMutex;
    static bool s_paused;
};

} // namespace vitaabs
