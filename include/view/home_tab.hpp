/**
 * VitaABS - Home Tab
 * Shows continue listening, recently added audiobooks and podcasts
 */

#pragma once

#include <borealis.hpp>
#include "app/audiobookshelf_client.hpp"
#include "view/recycling_grid.hpp"

namespace vitaabs {

class HomeTab : public brls::Box {
public:
    HomeTab();

    void onFocusGained() override;

private:
    void loadContent();
    void onItemSelected(const MediaItem& item);

    // Helper to create a media row with horizontal scrolling
    brls::HScrollingFrame* createMediaRow(brls::Box** contentOut);
    void populateRow(brls::Box* rowContent, const std::vector<MediaItem>& items);

    // Vertical scroll container
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_scrollContent = nullptr;

    brls::Label* m_titleLabel = nullptr;

    // Continue Listening section
    brls::HScrollingFrame* m_continueListeningRow = nullptr;
    brls::Box* m_continueListeningContent = nullptr;

    // Recently Added Audiobooks section
    brls::HScrollingFrame* m_audiobooksRow = nullptr;
    brls::Box* m_audiobooksContent = nullptr;

    // Recently Added Podcasts section
    brls::HScrollingFrame* m_podcastsRow = nullptr;
    brls::Box* m_podcastsContent = nullptr;

    std::vector<MediaItem> m_continueListening;
    std::vector<MediaItem> m_recentAudiobooks;
    std::vector<MediaItem> m_recentPodcasts;
    bool m_loaded = false;
};

} // namespace vitaabs
