/**
 * VitaABS - Audiobookshelf API Client
 * Handles all communication with Audiobookshelf servers
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace vitaabs {

// Media types
enum class MediaType {
    UNKNOWN,
    BOOK,
    PODCAST,
    PODCAST_EPISODE,
    MUSIC
};

// Audio file info
struct AudioFile {
    std::string ino;
    std::string path;
    std::string name;
    std::string codec;
    float duration = 0;
    float size = 0;
    int bitrate = 0;
    int channels = 0;
};

// Media item info
struct MediaItem {
    std::string id;
    std::string libraryId;
    std::string title;
    std::string subtitle;
    std::string author;
    std::string narrator;
    std::string description;
    std::string coverUrl;
    MediaType mediaType = MediaType::UNKNOWN;
    float duration = 0;
    float currentTime = 0;
    float progress = 0;
    bool isFinished = false;

    // For podcasts
    std::string podcastId;
    std::string episodeId;
    std::string episodeTitle;
    int seasonNum = 0;
    int episodeNum = 0;

    // Audio files
    std::vector<AudioFile> audioFiles;

    // Download info
    std::string audioPath;  // Path to audio for download
    float size = 0;
};

// Library info
struct Library {
    std::string id;
    std::string name;
    std::string icon;
    std::string mediaType;
    int itemCount = 0;
};

// Server info
struct ServerInfo {
    std::string url;
    std::string name;
    std::string version;
};

// User info
struct UserInfo {
    std::string id;
    std::string username;
    std::string token;
};

// Progress update info
struct ProgressUpdate {
    std::string itemId;
    std::string episodeId;
    float currentTime = 0;
    float duration = 0;
    float progress = 0;
    bool isFinished = false;
};

/**
 * Audiobookshelf API Client singleton
 */
class AudiobookshelfClient {
public:
    static AudiobookshelfClient& getInstance();

    // Authentication
    bool login(const std::string& serverUrl, const std::string& username, const std::string& password);
    bool loginWithToken(const std::string& serverUrl, const std::string& token);
    void logout();
    bool isLoggedIn() const;

    // Server
    bool pingServer(const std::string& url);
    bool fetchServerInfo(ServerInfo& info);

    // Libraries
    bool fetchLibraries(std::vector<Library>& libraries);
    bool fetchLibraryItems(const std::string& libraryId, std::vector<MediaItem>& items);
    bool fetchRecentlyPlayed(std::vector<MediaItem>& items);
    bool fetchContinueListening(std::vector<MediaItem>& items);

    // Media
    bool fetchMediaDetails(const std::string& itemId, MediaItem& item);
    bool fetchPodcastEpisodes(const std::string& podcastId, std::vector<MediaItem>& episodes);

    // Search
    bool search(const std::string& query, std::vector<MediaItem>& results);

    // Playback & Progress
    bool getPlaybackUrl(const std::string& itemId, std::string& url, const std::string& episodeId = "");
    bool getDownloadUrl(const std::string& itemId, std::string& url, const std::string& episodeId = "");
    bool updateProgress(const std::string& itemId, float currentTime, float duration,
                       bool isFinished = false, const std::string& episodeId = "");
    bool syncProgress(const ProgressUpdate& progress);

    // Cover image URL
    std::string getCoverUrl(const std::string& itemId, int width = 300);

    // Configuration
    void setToken(const std::string& token) { m_token = token; }
    const std::string& getToken() const { return m_token; }
    void setServerUrl(const std::string& url) { m_serverUrl = url; }
    const std::string& getServerUrl() const { return m_serverUrl; }
    const UserInfo& getUserInfo() const { return m_userInfo; }

private:
    AudiobookshelfClient() = default;
    ~AudiobookshelfClient() = default;

    std::string buildApiUrl(const std::string& endpoint);
    MediaType parseMediaType(const std::string& typeStr);

    std::string m_token;
    std::string m_serverUrl;
    UserInfo m_userInfo;
};

} // namespace vitaabs
