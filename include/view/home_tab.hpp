/**
 * VitaABS - Home Tab
 * Shows Continue Listening and Recently Added Episodes across all libraries
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include "app/audiobookshelf_client.hpp"
#include "view/recycling_grid.hpp"

namespace vitaabs {

class HomeTab : public brls::Box {
public:
    HomeTab();
    ~HomeTab() override;

    void onFocusGained() override;

private:
    void loadContent();
    void onItemSelected(const MediaItem& item);

    // Check if this tab is still valid (not destroyed)
    bool isValid() const { return m_alive && *m_alive; }

    brls::Label* m_titleLabel = nullptr;
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_contentBox = nullptr;

    // Continue Listening section
    brls::Label* m_continueLabel = nullptr;
    RecyclingGrid* m_continueGrid = nullptr;
    std::vector<MediaItem> m_continueItems;

    // Recently Added Episodes section
    brls::Label* m_recentEpisodesLabel = nullptr;
    RecyclingGrid* m_recentEpisodesGrid = nullptr;
    std::vector<MediaItem> m_recentEpisodes;

    bool m_loaded = false;

    // Shared pointer to track if this object is still alive
    std::shared_ptr<bool> m_alive;
};

} // namespace vitaabs
