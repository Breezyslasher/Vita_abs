/**
 * VitaABS - Library Tab
 * Browse audiobook and podcast libraries
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
    void loadLibraries();
    void loadContent(const std::string& libraryId);
    void onLibrarySelected(const Library& library);
    void onItemSelected(const MediaItem& item);

    brls::Label* m_titleLabel = nullptr;
    brls::HScrollingFrame* m_librariesScroll = nullptr;
    brls::Box* m_librariesBox = nullptr;
    RecyclingGrid* m_contentGrid = nullptr;

    std::vector<Library> m_libraries;
    std::vector<MediaItem> m_items;
    std::string m_currentLibrary;
    bool m_loaded = false;
};

} // namespace vitaabs
