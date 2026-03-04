/**
 * VitaABS - Media Detail View
 * Shows detailed information about a media item
 */

#pragma once

#include <borealis.hpp>
#include "app/audiobookshelf_client.hpp"

namespace vitaabs {

class MediaDetailView : public brls::Box {
public:
    MediaDetailView(const MediaItem& item);

    static brls::View* create();

private:
    void loadDetails();
    void loadChildren();
    void loadMusicCategories();
    void loadLocalCover(const std::string& localPath);
    void onPlay(bool resume = false);
    void startDownloadAndPlay(const std::string& itemId, const std::string& episodeId,
                              float startTime = -1.0f, bool downloadOnly = false);
    void startDownloadOnly(const std::string& itemId, const std::string& episodeId);
    void batchDownloadEpisodes(const std::vector<MediaItem>& episodes);
    void onDownload();
    void onDeleteDownload();
    void showDownloadOptions();
    void downloadAll();
    void downloadUnwatched(int maxCount = -1);
    void deleteAllDownloadedEpisodes();
    void showDeleteEpisodesDialog(const std::vector<std::pair<std::string, std::string>>& episodes,
                                   const std::string& podcastId,
                                   const std::string& podcastTitle);
    bool areAllEpisodesDownloaded();
    bool hasAnyDownloadedEpisodes();

    // Podcast episode management
    void findNewEpisodes();
    void showNewEpisodesDialog(const std::vector<MediaItem>& episodes,
                               const std::string& podcastId,
                               const std::string& podcastTitle);
    void downloadNewEpisodesToServer(const std::string& podcastId,
                                      const std::vector<MediaItem>& episodes);

    // Chapter display for audiobooks
    void populateChapters();

    brls::HScrollingFrame* createMediaRow(const std::string& title, brls::Box** contentOut);

    MediaItem m_item;
    std::vector<MediaItem> m_children;

    // Layout
    brls::ScrollingFrame* m_scrollView = nullptr;  // Scrolls the list (chapters or episodes)

    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_yearLabel = nullptr;
    brls::Label* m_ratingLabel = nullptr;
    brls::Label* m_durationLabel = nullptr;
    brls::Label* m_summaryLabel = nullptr;
    brls::Image* m_posterImage = nullptr;
    brls::Button* m_playButton = nullptr;
    brls::Button* m_downloadButton = nullptr;
    brls::Button* m_deleteButton = nullptr;
    brls::Button* m_findEpisodesButton = nullptr;
    brls::Box* m_childrenBox = nullptr;       // Podcast episode rows
    brls::Box* m_chaptersBox = nullptr;       // Audiobook chapter rows

    // Description expand/collapse
    std::string m_fullDescription;
    bool m_descriptionExpanded = false;
};

} // namespace vitaabs
