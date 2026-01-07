/**
 * VitaABS - Media Detail View
 * Shows detailed information about an audiobook or podcast
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
    void loadChapters();
    void loadEpisodes();  // For podcasts
    void onPlay(bool resume = false);
    void onPlayChapter(int chapterIndex);
    void onDownload();
    void showDownloadOptions();

    brls::HScrollingFrame* createMediaRow(const std::string& title, brls::Box** contentOut);

    MediaItem m_item;
    std::vector<Chapter> m_chapters;
    std::vector<MediaItem> m_episodes;  // For podcasts

    // Main layout
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_mainContent = nullptr;

    // Book/Podcast info
    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_authorLabel = nullptr;
    brls::Label* m_narratorLabel = nullptr;
    brls::Label* m_seriesLabel = nullptr;
    brls::Label* m_durationLabel = nullptr;
    brls::Label* m_progressLabel = nullptr;
    brls::Label* m_summaryLabel = nullptr;
    brls::Image* m_coverImage = nullptr;

    // Action buttons
    brls::Button* m_playButton = nullptr;
    brls::Button* m_resumeButton = nullptr;
    brls::Button* m_downloadButton = nullptr;

    // Chapters/Episodes list
    brls::Box* m_chaptersBox = nullptr;
    brls::Box* m_episodesBox = nullptr;
};

} // namespace vitaabs
