/**
 * VitaABS - Search Tab
 * Search for audiobooks, podcasts, and authors
 */

#pragma once

#include <borealis.hpp>
#include "app/audiobookshelf_client.hpp"
#include "view/recycling_grid.hpp"

namespace vitaabs {

class SearchTab : public brls::Box {
public:
    SearchTab();

    void onFocusGained() override;

private:
    void performSearch(const std::string& query);
    void onItemSelected(const MediaItem& item);
    void populateRow(brls::Box* rowContent, const std::vector<MediaItem>& items);

    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_searchLabel = nullptr;
    brls::Label* m_resultsLabel = nullptr;

    // Scrollable content for organized results
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_scrollContent = nullptr;

    // Category rows
    brls::HScrollingFrame* m_audiobooksRow = nullptr;
    brls::Box* m_audiobooksContent = nullptr;
    brls::HScrollingFrame* m_podcastsRow = nullptr;
    brls::Box* m_podcastsContent = nullptr;
    brls::HScrollingFrame* m_authorsRow = nullptr;
    brls::Box* m_authorsContent = nullptr;
    brls::HScrollingFrame* m_seriesRow = nullptr;
    brls::Box* m_seriesContent = nullptr;

    std::string m_searchQuery;
    std::vector<MediaItem> m_results;
    std::vector<MediaItem> m_audiobooks;
    std::vector<MediaItem> m_podcasts;
};

} // namespace vitaabs
