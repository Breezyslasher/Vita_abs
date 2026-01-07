/**
 * VitaABS - Library Tab
 * Browse library sections and content
 */

#pragma once

#include <borealis.hpp>
#include "app/audiobookshelf_client.hpp"
#include "view/recycling_grid.hpp"

namespace vitaabs {

class LibraryTab : public brls::Box {
public:
    LibraryTab();

    void onFocusGained() override;

private:
    void loadSections();
    void loadContent(const std::string& sectionKey);
    void onSectionSelected(const Library& section);
    void onItemSelected(const MediaItem& item);

    brls::Label* m_titleLabel = nullptr;
    brls::HScrollingFrame* m_sectionsScroll = nullptr;
    brls::Box* m_sectionsBox = nullptr;
    RecyclingGrid* m_contentGrid = nullptr;

    std::vector<Library> m_sections;
    std::vector<MediaItem> m_items;
    std::string m_currentSection;
    bool m_loaded = false;
};

} // namespace vitaabs
