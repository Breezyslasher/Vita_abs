/**
 * VitaABS - Library Section Tab
 * Shows content for a single library (for sidebar mode)
 * Collections, series, authors appear as browsable content within the tab
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include "app/audiobookshelf_client.hpp"
#include "view/recycling_grid.hpp"

namespace vitaabs {

// View mode for the library section
enum class LibraryViewMode {
    ALL_ITEMS,      // Show all items in the library
    COLLECTIONS,    // Show collections as browsable items
    SERIES,         // Show series as browsable items
    AUTHORS,        // Show authors as browsable items
    FILTERED        // Showing items filtered by collection, series, or author
};

class LibrarySectionTab : public brls::Box {
public:
    LibrarySectionTab(const std::string& libraryId, const std::string& title, const std::string& mediaType = "");
    ~LibrarySectionTab() override;

    void onFocusGained() override;

private:
    void loadContent();
    void loadCollections();
    void loadSeries();
    void loadAuthors();
    void showAllItems();
    void showCollections();
    void showSeries();
    void showAuthors();
    void onItemSelected(const MediaItem& item);
    void onCollectionSelected(const Collection& collection);
    void onSeriesSelected(const Series& series);
    void onAuthorSelected(const Author& author);
    void updateViewModeButtons();

    // Check if this tab is still valid (not destroyed)
    bool isValid() const { return m_alive && *m_alive; }

    std::string m_libraryId;
    std::string m_title;
    std::string m_mediaType;  // "book" or "podcast"

    brls::Label* m_titleLabel = nullptr;

    // View mode selector buttons
    brls::Box* m_viewModeBox = nullptr;
    brls::Button* m_allBtn = nullptr;
    brls::Button* m_collectionsBtn = nullptr;
    brls::Button* m_seriesBtn = nullptr;
    brls::Button* m_authorsBtn = nullptr;
    brls::Button* m_backBtn = nullptr;

    // Main content grid
    RecyclingGrid* m_contentGrid = nullptr;

    // Data
    std::vector<MediaItem> m_items;
    std::vector<Collection> m_collections;
    std::vector<Series> m_series;
    std::vector<Author> m_authors;

    LibraryViewMode m_viewMode = LibraryViewMode::ALL_ITEMS;
    std::string m_filterTitle;  // Title of current filter
    bool m_loaded = false;
    bool m_collectionsLoaded = false;
    bool m_seriesLoaded = false;
    bool m_authorsLoaded = false;

    // Shared pointer to track if this object is still alive
    std::shared_ptr<bool> m_alive;
};

} // namespace vitaabs
