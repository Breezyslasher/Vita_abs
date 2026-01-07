/**
 * VitaABS - Library Section Tab implementation
 */

#include "view/library_section_tab.hpp"
#include "view/media_item_cell.hpp"
#include "view/media_detail_view.hpp"
#include "app/application.hpp"
#include "utils/async.hpp"

namespace vitaabs {

LibrarySectionTab::LibrarySectionTab(const std::string& libraryId, const std::string& title)
    : m_libraryId(libraryId), m_title(title) {

    // Create alive flag for async callback safety
    m_alive = std::make_shared<bool>(true);

    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText(title);
    m_titleLabel->setFontSize(28);
    m_titleLabel->setMarginBottom(15);
    this->addView(m_titleLabel);

    const auto& settings = Application::getInstance().getSettings();

    // View mode selector (All / Collections / Authors / Series)
    m_viewModeBox = new brls::Box();
    m_viewModeBox->setAxis(brls::Axis::ROW);
    m_viewModeBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_viewModeBox->setAlignItems(brls::AlignItems::CENTER);
    m_viewModeBox->setMarginBottom(15);

    // All Items button
    m_allBtn = new brls::Button();
    m_allBtn->setText("All");
    m_allBtn->setMarginRight(10);
    m_allBtn->registerClickAction([this](brls::View* view) {
        showAllItems();
        return true;
    });
    m_viewModeBox->addView(m_allBtn);

    // Collections button
    if (settings.showCollections) {
        m_collectionsBtn = new brls::Button();
        m_collectionsBtn->setText("Collections");
        m_collectionsBtn->setMarginRight(10);
        m_collectionsBtn->registerClickAction([this](brls::View* view) {
            showCollections();
            return true;
        });
        m_viewModeBox->addView(m_collectionsBtn);
    }

    // Series button
    m_seriesBtn = new brls::Button();
    m_seriesBtn->setText("Series");
    m_seriesBtn->setMarginRight(10);
    m_seriesBtn->registerClickAction([this](brls::View* view) {
        showSeries();
        return true;
    });
    m_viewModeBox->addView(m_seriesBtn);

    // Authors button
    m_authorsBtn = new brls::Button();
    m_authorsBtn->setText("Authors");
    m_authorsBtn->setMarginRight(10);
    m_authorsBtn->registerClickAction([this](brls::View* view) {
        showAuthors();
        return true;
    });
    m_viewModeBox->addView(m_authorsBtn);

    // Back button (hidden by default, shown in filtered view)
    m_backBtn = new brls::Button();
    m_backBtn->setText("< Back");
    m_backBtn->setVisibility(brls::Visibility::GONE);
    m_backBtn->registerClickAction([this](brls::View* view) {
        showAllItems();
        return true;
    });
    m_viewModeBox->addView(m_backBtn);

    this->addView(m_viewModeBox);

    // Content grid
    m_contentGrid = new RecyclingGrid();
    m_contentGrid->setGrow(1.0f);
    m_contentGrid->setOnItemSelected([this](const MediaItem& item) {
        onItemSelected(item);
    });
    this->addView(m_contentGrid);

    // Load content immediately
    brls::Logger::debug("LibrarySectionTab: Created for library {} ({})", m_libraryId, m_title);
    loadContent();
}

LibrarySectionTab::~LibrarySectionTab() {
    // Mark as no longer alive to prevent async callbacks from updating destroyed UI
    if (m_alive) {
        *m_alive = false;
    }
    brls::Logger::debug("LibrarySectionTab: Destroyed for library {}", m_libraryId);
}

void LibrarySectionTab::onFocusGained() {
    brls::Box::onFocusGained();

    if (!m_loaded) {
        loadContent();
    }
}

void LibrarySectionTab::loadContent() {
    brls::Logger::debug("LibrarySectionTab::loadContent - library: {} (async)", m_libraryId);

    std::string id = m_libraryId;
    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, id, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> items;

        if (client.fetchLibraryItems(id, items)) {
            brls::Logger::info("LibrarySectionTab: Got {} items for library {}", items.size(), id);

            brls::sync([this, items, aliveWeak]() {
                // Check if object is still alive before updating UI
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) {
                    brls::Logger::debug("LibrarySectionTab: Tab destroyed, skipping UI update");
                    return;
                }

                m_items = items;
                m_contentGrid->setDataSource(m_items);
                m_loaded = true;
            });
        } else {
            brls::Logger::error("LibrarySectionTab: Failed to load content for library {}", id);
            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                m_loaded = true;
            });
        }
    });

    // Preload collections, series, and authors for quick switching
    const auto& settings = Application::getInstance().getSettings();
    if (settings.showCollections) {
        loadCollections();
    }
    loadSeries();
    loadAuthors();
}

void LibrarySectionTab::loadCollections() {
    std::string id = m_libraryId;
    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, id, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<Collection> collections;

        if (client.fetchCollections(id, collections)) {
            brls::Logger::info("LibrarySectionTab: Got {} collections for library {}", collections.size(), id);

            brls::sync([this, collections, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_collections = collections;
                m_collectionsLoaded = true;

                // Hide collections button if none available
                if (m_collections.empty() && m_collectionsBtn) {
                    m_collectionsBtn->setVisibility(brls::Visibility::GONE);
                }
            });
        } else {
            brls::Logger::debug("LibrarySectionTab: No collections for library {}", id);
            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_collectionsLoaded = true;
                if (m_collectionsBtn) {
                    m_collectionsBtn->setVisibility(brls::Visibility::GONE);
                }
            });
        }
    });
}

void LibrarySectionTab::loadSeries() {
    std::string id = m_libraryId;
    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, id, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<Series> seriesList;

        if (client.fetchSeries(id, seriesList)) {
            brls::Logger::info("LibrarySectionTab: Got {} series for library {}", seriesList.size(), id);

            brls::sync([this, seriesList, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_series = seriesList;
                m_seriesLoaded = true;

                // Hide series button if none available
                if (m_series.empty() && m_seriesBtn) {
                    m_seriesBtn->setVisibility(brls::Visibility::GONE);
                }
            });
        } else {
            brls::Logger::debug("LibrarySectionTab: No series for library {}", id);
            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_seriesLoaded = true;
                if (m_seriesBtn) {
                    m_seriesBtn->setVisibility(brls::Visibility::GONE);
                }
            });
        }
    });
}

void LibrarySectionTab::loadAuthors() {
    std::string id = m_libraryId;
    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, id, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<Author> authorsList;

        if (client.fetchAuthors(id, authorsList)) {
            brls::Logger::info("LibrarySectionTab: Got {} authors for library {}", authorsList.size(), id);

            brls::sync([this, authorsList, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_authors = authorsList;
                m_authorsLoaded = true;

                // Hide authors button if none available
                if (m_authors.empty() && m_authorsBtn) {
                    m_authorsBtn->setVisibility(brls::Visibility::GONE);
                }
            });
        } else {
            brls::Logger::debug("LibrarySectionTab: No authors for library {}", id);
            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_authorsLoaded = true;
                if (m_authorsBtn) {
                    m_authorsBtn->setVisibility(brls::Visibility::GONE);
                }
            });
        }
    });
}

void LibrarySectionTab::showAllItems() {
    m_viewMode = LibraryViewMode::ALL_ITEMS;
    m_titleLabel->setText(m_title);
    m_contentGrid->setDataSource(m_items);
    updateViewModeButtons();
}

void LibrarySectionTab::showCollections() {
    if (!m_collectionsLoaded) {
        brls::Application::notify("Loading collections...");
        return;
    }

    if (m_collections.empty()) {
        brls::Application::notify("No collections available");
        return;
    }

    m_viewMode = LibraryViewMode::COLLECTIONS;
    m_titleLabel->setText(m_title + " - Collections");

    // Convert collections to MediaItem format for the grid
    std::vector<MediaItem> collectionItems;
    for (const auto& collection : m_collections) {
        MediaItem item;
        item.id = collection.id;
        item.title = collection.name;
        item.description = collection.description;
        item.mediaType = MediaType::BOOK;  // Will be treated specially
        collectionItems.push_back(item);
    }

    m_contentGrid->setDataSource(collectionItems);
    updateViewModeButtons();
}

void LibrarySectionTab::showSeries() {
    if (!m_seriesLoaded) {
        brls::Application::notify("Loading series...");
        return;
    }

    if (m_series.empty()) {
        brls::Application::notify("No series available");
        return;
    }

    m_viewMode = LibraryViewMode::SERIES;
    m_titleLabel->setText(m_title + " - Series");

    // Convert series to MediaItem format for the grid
    std::vector<MediaItem> seriesItems;
    for (const auto& series : m_series) {
        MediaItem item;
        item.id = series.id;
        item.title = series.name;
        item.description = series.description;
        item.mediaType = MediaType::BOOK;  // Will be treated specially
        seriesItems.push_back(item);
    }

    m_contentGrid->setDataSource(seriesItems);
    updateViewModeButtons();
}

void LibrarySectionTab::showAuthors() {
    if (!m_authorsLoaded) {
        brls::Application::notify("Loading authors...");
        return;
    }

    if (m_authors.empty()) {
        brls::Application::notify("No authors available");
        return;
    }

    m_viewMode = LibraryViewMode::AUTHORS;
    m_titleLabel->setText(m_title + " - Authors");

    // Convert authors to MediaItem format for the grid
    std::vector<MediaItem> authorItems;
    for (const auto& author : m_authors) {
        MediaItem item;
        item.id = author.id;
        item.title = author.name;
        item.description = author.description;
        item.coverUrl = author.imagePath;
        item.mediaType = MediaType::BOOK;  // Will be treated specially
        authorItems.push_back(item);
    }

    m_contentGrid->setDataSource(authorItems);
    updateViewModeButtons();
}

void LibrarySectionTab::updateViewModeButtons() {
    // Show/hide back button
    bool inFilteredView = (m_viewMode == LibraryViewMode::FILTERED);
    m_backBtn->setVisibility(inFilteredView ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

    // Show/hide mode buttons
    bool showModeButtons = (m_viewMode != LibraryViewMode::FILTERED);
    m_allBtn->setVisibility(showModeButtons ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (m_collectionsBtn) {
        m_collectionsBtn->setVisibility(showModeButtons && !m_collections.empty() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    if (m_seriesBtn) {
        m_seriesBtn->setVisibility(showModeButtons && !m_series.empty() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    if (m_authorsBtn) {
        m_authorsBtn->setVisibility(showModeButtons && !m_authors.empty() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
}

void LibrarySectionTab::onItemSelected(const MediaItem& item) {
    // Handle selection based on current view mode
    if (m_viewMode == LibraryViewMode::COLLECTIONS) {
        // Selected a collection - show its contents
        onCollectionSelected(item);
        return;
    }

    if (m_viewMode == LibraryViewMode::SERIES) {
        // Selected a series - show its books
        onSeriesSelected(item);
        return;
    }

    if (m_viewMode == LibraryViewMode::AUTHORS) {
        // Selected an author - show their books
        onAuthorSelected(item);
        return;
    }

    // Normal item selection - show media detail view
    if (item.mediaType == MediaType::PODCAST_EPISODE) {
        Application::getInstance().pushPlayerActivity(item.id, item.episodeId);
        return;
    }

    auto* detailView = new MediaDetailView(item);
    brls::Application::pushActivity(new brls::Activity(detailView));
}

void LibrarySectionTab::onCollectionSelected(const MediaItem& collection) {
    brls::Logger::debug("LibrarySectionTab: Selected collection: {}", collection.title);

    m_filterTitle = collection.title;
    std::string collectionId = collection.id;
    std::string filterTitle = m_filterTitle;
    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, collectionId, filterTitle, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> items;

        if (client.fetchCollectionItems(collectionId, items)) {
            brls::Logger::info("LibrarySectionTab: Got {} items in collection", items.size());

            brls::sync([this, items, filterTitle, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_viewMode = LibraryViewMode::FILTERED;
                m_titleLabel->setText(m_title + " - " + filterTitle);
                m_contentGrid->setDataSource(items);
                updateViewModeButtons();
            });
        } else {
            brls::Logger::error("LibrarySectionTab: Failed to load collection content");
            brls::sync([aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                brls::Application::notify("Failed to load collection");
            });
        }
    });
}

void LibrarySectionTab::onSeriesSelected(const MediaItem& series) {
    brls::Logger::debug("LibrarySectionTab: Selected series: {}", series.title);

    m_filterTitle = series.title;
    std::string seriesId = series.id;
    std::string libraryId = m_libraryId;
    std::string filterTitle = m_filterTitle;
    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, seriesId, libraryId, filterTitle, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> items;

        if (client.fetchSeriesItems(libraryId, seriesId, items)) {
            brls::Logger::info("LibrarySectionTab: Got {} items in series", items.size());

            brls::sync([this, items, filterTitle, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_viewMode = LibraryViewMode::FILTERED;
                m_titleLabel->setText(m_title + " - " + filterTitle);
                m_contentGrid->setDataSource(items);
                updateViewModeButtons();
            });
        } else {
            brls::Logger::error("LibrarySectionTab: Failed to load series content");
            brls::sync([aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                brls::Application::notify("Failed to load series");
            });
        }
    });
}

void LibrarySectionTab::onAuthorSelected(const MediaItem& author) {
    brls::Logger::debug("LibrarySectionTab: Selected author: {}", author.title);

    m_filterTitle = author.title;
    std::string authorId = author.id;
    std::string libraryId = m_libraryId;
    std::string filterTitle = m_filterTitle;
    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, authorId, libraryId, filterTitle, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> items;

        if (client.fetchAuthorItems(libraryId, authorId, items)) {
            brls::Logger::info("LibrarySectionTab: Got {} items by author", items.size());

            brls::sync([this, items, filterTitle, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_viewMode = LibraryViewMode::FILTERED;
                m_titleLabel->setText(m_title + " - " + filterTitle);
                m_contentGrid->setDataSource(items);
                updateViewModeButtons();
            });
        } else {
            brls::Logger::error("LibrarySectionTab: Failed to load author content");
            brls::sync([aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                brls::Application::notify("Failed to load author");
            });
        }
    });
}

} // namespace vitaabs
