/**
 * VitaABS - Audiobookshelf API Client implementation
 */

#include "app/audiobookshelf_client.hpp"
#include "utils/http_client.hpp"

#include <borealis.hpp>
#include <cstring>
#include <ctime>

namespace vitaabs {

AudiobookshelfClient& AudiobookshelfClient::getInstance() {
    static AudiobookshelfClient instance;
    return instance;
}

std::string AudiobookshelfClient::buildApiUrl(const std::string& endpoint) {
    std::string url = m_serverUrl;

    // Remove trailing slash
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    url += endpoint;
    return url;
}

MediaType AudiobookshelfClient::parseMediaType(const std::string& typeStr) {
    if (typeStr == "book") return MediaType::BOOK;
    if (typeStr == "podcast") return MediaType::PODCAST;
    if (typeStr == "podcastEpisode") return MediaType::PODCAST_EPISODE;
    if (typeStr == "music") return MediaType::MUSIC;
    return MediaType::UNKNOWN;
}

bool AudiobookshelfClient::login(const std::string& serverUrl, const std::string& username, const std::string& password) {
    brls::Logger::info("Attempting login to {} for user: {}", serverUrl, username);

    m_serverUrl = serverUrl;

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/login");
    req.method = "POST";
    req.headers["Content-Type"] = "application/json";

    // Build JSON body
    req.body = "{\"username\":\"" + username + "\",\"password\":\"" + password + "\"}";

    HttpResponse resp = client.request(req);

    if (resp.success && (resp.statusCode == 200 || resp.statusCode == 201)) {
        // Parse token from response
        // Simple JSON parsing - look for "token" field
        size_t tokenPos = resp.body.find("\"token\"");
        if (tokenPos != std::string::npos) {
            size_t start = resp.body.find("\"", tokenPos + 7);
            if (start != std::string::npos) {
                size_t end = resp.body.find("\"", start + 1);
                if (end != std::string::npos) {
                    m_token = resp.body.substr(start + 1, end - start - 1);
                }
            }
        }

        // Parse user info
        size_t userPos = resp.body.find("\"user\"");
        if (userPos != std::string::npos) {
            size_t idPos = resp.body.find("\"id\"", userPos);
            if (idPos != std::string::npos) {
                size_t start = resp.body.find("\"", idPos + 4);
                if (start != std::string::npos) {
                    size_t end = resp.body.find("\"", start + 1);
                    if (end != std::string::npos) {
                        m_userInfo.id = resp.body.substr(start + 1, end - start - 1);
                    }
                }
            }
            size_t unPos = resp.body.find("\"username\"", userPos);
            if (unPos != std::string::npos) {
                size_t start = resp.body.find("\"", unPos + 10);
                if (start != std::string::npos) {
                    size_t end = resp.body.find("\"", start + 1);
                    if (end != std::string::npos) {
                        m_userInfo.username = resp.body.substr(start + 1, end - start - 1);
                    }
                }
            }
        }

        m_userInfo.token = m_token;

        if (!m_token.empty()) {
            brls::Logger::info("Login successful");
            return true;
        }
    }

    brls::Logger::error("Login failed: {}", resp.statusCode);
    return false;
}

bool AudiobookshelfClient::loginWithToken(const std::string& serverUrl, const std::string& token) {
    m_serverUrl = serverUrl;
    m_token = token;

    // Verify token by fetching user info
    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/me");
    req.method = "GET";
    req.headers["Authorization"] = "Bearer " + m_token;

    HttpResponse resp = client.request(req);

    if (resp.success && resp.statusCode == 200) {
        brls::Logger::info("Token login successful");
        return true;
    }

    brls::Logger::error("Token login failed");
    m_token.clear();
    return false;
}

void AudiobookshelfClient::logout() {
    m_token.clear();
    m_serverUrl.clear();
    m_userInfo = UserInfo();
}

bool AudiobookshelfClient::isLoggedIn() const {
    return !m_token.empty() && !m_serverUrl.empty();
}

bool AudiobookshelfClient::pingServer(const std::string& url) {
    HttpClient client;
    HttpRequest req;
    req.url = url + "/ping";
    req.method = "GET";
    req.timeout = 10;

    HttpResponse resp = client.request(req);
    return resp.success && resp.statusCode == 200;
}

bool AudiobookshelfClient::fetchServerInfo(ServerInfo& info) {
    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/status");
    req.method = "GET";

    HttpResponse resp = client.request(req);

    if (resp.success && resp.statusCode == 200) {
        // Parse server info from JSON
        // Simple parsing for now
        info.url = m_serverUrl;
        return true;
    }

    return false;
}

bool AudiobookshelfClient::fetchLibraries(std::vector<Library>& libraries) {
    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/libraries");
    req.method = "GET";
    req.headers["Authorization"] = "Bearer " + m_token;

    HttpResponse resp = client.request(req);

    if (!resp.success || resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch libraries: {}", resp.statusCode);
        return false;
    }

    libraries.clear();

    // Parse libraries from JSON response
    // Look for "libraries" array
    size_t pos = 0;
    while ((pos = resp.body.find("\"id\"", pos)) != std::string::npos) {
        Library lib;

        // Extract id
        size_t start = resp.body.find("\"", pos + 4);
        if (start != std::string::npos) {
            size_t end = resp.body.find("\"", start + 1);
            if (end != std::string::npos) {
                lib.id = resp.body.substr(start + 1, end - start - 1);
            }
        }

        // Find and extract name
        size_t namePos = resp.body.find("\"name\"", pos);
        if (namePos != std::string::npos && namePos < pos + 500) {
            start = resp.body.find("\"", namePos + 6);
            if (start != std::string::npos) {
                size_t end = resp.body.find("\"", start + 1);
                if (end != std::string::npos) {
                    lib.name = resp.body.substr(start + 1, end - start - 1);
                }
            }
        }

        if (!lib.id.empty() && !lib.name.empty()) {
            libraries.push_back(lib);
        }

        pos++;
    }

    brls::Logger::info("Found {} libraries", libraries.size());
    return true;
}

bool AudiobookshelfClient::fetchLibraryItems(const std::string& libraryId, std::vector<MediaItem>& items) {
    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/libraries/" + libraryId + "/items");
    req.method = "GET";
    req.headers["Authorization"] = "Bearer " + m_token;

    HttpResponse resp = client.request(req);

    if (!resp.success || resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch library items: {}", resp.statusCode);
        return false;
    }

    items.clear();
    // TODO: Parse items from response

    return true;
}

bool AudiobookshelfClient::fetchRecentlyPlayed(std::vector<MediaItem>& items) {
    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/me/listening-sessions");
    req.method = "GET";
    req.headers["Authorization"] = "Bearer " + m_token;

    HttpResponse resp = client.request(req);

    if (!resp.success || resp.statusCode != 200) {
        return false;
    }

    items.clear();
    // TODO: Parse items from response

    return true;
}

bool AudiobookshelfClient::fetchContinueListening(std::vector<MediaItem>& items) {
    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/me/items-in-progress");
    req.method = "GET";
    req.headers["Authorization"] = "Bearer " + m_token;

    HttpResponse resp = client.request(req);

    if (!resp.success || resp.statusCode != 200) {
        return false;
    }

    items.clear();
    // TODO: Parse items from response

    return true;
}

bool AudiobookshelfClient::fetchMediaDetails(const std::string& itemId, MediaItem& item) {
    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/items/" + itemId);
    req.method = "GET";
    req.headers["Authorization"] = "Bearer " + m_token;

    HttpResponse resp = client.request(req);

    if (!resp.success || resp.statusCode != 200) {
        return false;
    }

    // TODO: Parse item details from response
    item.id = itemId;

    return true;
}

bool AudiobookshelfClient::fetchPodcastEpisodes(const std::string& podcastId, std::vector<MediaItem>& episodes) {
    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/podcasts/" + podcastId + "/episodes");
    req.method = "GET";
    req.headers["Authorization"] = "Bearer " + m_token;

    HttpResponse resp = client.request(req);

    if (!resp.success || resp.statusCode != 200) {
        return false;
    }

    episodes.clear();
    // TODO: Parse episodes from response

    return true;
}

bool AudiobookshelfClient::search(const std::string& query, std::vector<MediaItem>& results) {
    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/search?q=" + HttpClient::urlEncode(query));
    req.method = "GET";
    req.headers["Authorization"] = "Bearer " + m_token;

    HttpResponse resp = client.request(req);

    if (!resp.success || resp.statusCode != 200) {
        return false;
    }

    results.clear();
    // TODO: Parse search results from response

    return true;
}

bool AudiobookshelfClient::getPlaybackUrl(const std::string& itemId, std::string& url, const std::string& episodeId) {
    // Audiobookshelf streaming endpoint
    if (!episodeId.empty()) {
        url = buildApiUrl("/api/items/" + itemId + "/play/" + episodeId);
    } else {
        url = buildApiUrl("/api/items/" + itemId + "/play");
    }

    // Add auth token as query param for streaming
    url += "?token=" + m_token;

    return true;
}

bool AudiobookshelfClient::getDownloadUrl(const std::string& itemId, std::string& url, const std::string& episodeId) {
    // Audiobookshelf download endpoint
    if (!episodeId.empty()) {
        url = buildApiUrl("/api/items/" + itemId + "/file/" + episodeId);
    } else {
        url = buildApiUrl("/api/items/" + itemId + "/download");
    }

    return true;
}

bool AudiobookshelfClient::updateProgress(const std::string& itemId, float currentTime, float duration,
                                          bool isFinished, const std::string& episodeId) {
    HttpClient client;
    HttpRequest req;

    if (!episodeId.empty()) {
        req.url = buildApiUrl("/api/me/progress/" + itemId + "/" + episodeId);
    } else {
        req.url = buildApiUrl("/api/me/progress/" + itemId);
    }

    req.method = "PATCH";
    req.headers["Authorization"] = "Bearer " + m_token;
    req.headers["Content-Type"] = "application/json";

    // Calculate progress percentage
    float progress = (duration > 0) ? (currentTime / duration) : 0;

    char body[256];
    snprintf(body, sizeof(body),
             "{\"currentTime\":%.2f,\"duration\":%.2f,\"progress\":%.4f,\"isFinished\":%s}",
             currentTime, duration, progress, isFinished ? "true" : "false");
    req.body = body;

    HttpResponse resp = client.request(req);

    return resp.success && (resp.statusCode == 200 || resp.statusCode == 204);
}

bool AudiobookshelfClient::syncProgress(const ProgressUpdate& progress) {
    return updateProgress(progress.itemId, progress.currentTime, progress.duration,
                         progress.isFinished, progress.episodeId);
}

std::string AudiobookshelfClient::getCoverUrl(const std::string& itemId, int width) {
    std::string url = buildApiUrl("/api/items/" + itemId + "/cover");
    url += "?width=" + std::to_string(width);
    url += "&token=" + m_token;
    return url;
}

} // namespace vitaabs
