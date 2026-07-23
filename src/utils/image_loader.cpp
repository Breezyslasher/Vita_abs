/**
 * VitaABS - Asynchronous Image Loader implementation
 */

#include "utils/image_loader.hpp"
#include "utils/http_client.hpp"

namespace vitaabs {

std::map<std::string, std::vector<uint8_t>> ImageLoader::s_cache;
std::mutex ImageLoader::s_cacheMutex;
bool ImageLoader::s_paused = false;

void ImageLoader::loadAsync(const std::string& url, LoadCallback callback, brls::Image* target,
                            std::weak_ptr<bool> alive) {
    if (url.empty() || !target) return;

    if (s_paused) {
        brls::Logger::debug("ImageLoader: Paused, skipping load for {}", url);
        return;
    }

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_cache.find(url);
        if (it != s_cache.end()) {
            target->setImageFromMem(it->second.data(), it->second.size());
            if (callback) callback(target);
            return;
        }
    }

    // Load asynchronously
    brls::async([url, callback, target, alive]() {
        HttpClient client;
        HttpResponse resp = client.get(url);

        if (resp.success && !resp.body.empty()) {
            brls::Logger::debug("ImageLoader: Successfully loaded {} bytes from {}", resp.body.size(), url);
            std::vector<uint8_t> imageData(resp.body.begin(), resp.body.end());

            {
                std::lock_guard<std::mutex> lock(s_cacheMutex);
                if (s_cache.size() > 100) {
                    s_cache.clear();
                }
                s_cache[url] = imageData;
            }

            brls::sync([imageData, callback, target, alive]() {
                auto alivePtr = alive.lock();
                if (alivePtr && !*alivePtr) return;
                target->setImageFromMem(imageData.data(), imageData.size());
                if (callback) callback(target);
            });
        } else {
            brls::Logger::error("ImageLoader: Failed to load {}: status={} error={}",
                url, resp.statusCode, resp.error.empty() ? "empty response" : resp.error);
        }
    });
}

void ImageLoader::clearCache() {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    s_cache.clear();
}

void ImageLoader::cancelAll() {
    // Borealis handles thread cancellation
}

void ImageLoader::setPaused(bool paused) {
    s_paused = paused;
    if (paused) {
        brls::Logger::debug("ImageLoader: Paused - new loads will be skipped");
    } else {
        brls::Logger::debug("ImageLoader: Resumed - loads enabled");
    }
}

bool ImageLoader::isPaused() {
    return s_paused;
}

} // namespace vitaabs
