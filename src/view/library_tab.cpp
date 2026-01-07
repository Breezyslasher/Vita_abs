/**
 * VitaABS - Library Tab implementation
 */

#include "view/library_tab.hpp"
#include "view/media_item_cell.hpp"
#include "view/media_detail_view.hpp"
#include "app/application.hpp"
#include "utils/async.hpp"

namespace vitaabs {

LibraryTab::LibraryTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Library");
    m_titleLabel->setFontSize(28);
    m_titleLabel->setMarginBottom(20);
    this->addView(m_titleLabel);

    // Libraries row with horizontal scrolling
    m_librariesScroll = new brls::HScrollingFrame();
    m_librariesScroll->setHeight(50);
    m_librariesScroll->setMarginBottom(20);

    m_librariesBox = new brls::Box();
    m_librariesBox->setAxis(brls::Axis::ROW);
    m_librariesBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_librariesBox->setAlignItems(brls::AlignItems::CENTER);

    m_librariesScroll->setContentView(m_librariesBox);
    this->addView(m_librariesScroll);

    // Content grid
    m_contentGrid = new RecyclingGrid();
    m_contentGrid->setGrow(1.0f);
    m_contentGrid->setOnItemSelected([this](const MediaItem& item) {
        onItemSelected(item);
    });
    this->addView(m_contentGrid);

    // Load libraries immediately
    brls::Logger::debug("LibraryTab: Loading libraries...");
    loadLibraries();
}

void LibraryTab::onFocusGained() {
    brls::Box::onFocusGained();

    if (!m_loaded) {
        loadLibraries();
    }
}

// Helper function to check if a library is hidden
static bool isLibraryHidden(const std::string& id, const std::string& hiddenLibraries) {
    if (hiddenLibraries.empty()) return false;

    std::string hidden = hiddenLibraries;
    size_t pos = 0;
    while ((pos = hidden.find(',')) != std::string::npos) {
        std::string hiddenId = hidden.substr(0, pos);
        if (hiddenId == id) return true;
        hidden.erase(0, pos + 1);
    }
    return (hidden == id);
}

void LibraryTab::loadLibraries() {
    brls::Logger::debug("LibraryTab::loadLibraries - Starting async load");

    asyncRun([this]() {
        brls::Logger::debug("LibraryTab: Fetching libraries (async)...");
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<Library> libraries;

        if (client.fetchLibraries(libraries)) {
            brls::Logger::info("LibraryTab: Got {} libraries", libraries.size());

            // Get hidden libraries setting
            std::string hiddenLibraries = Application::getInstance().getSettings().hiddenLibraries;

            // Filter out hidden libraries
            std::vector<Library> visibleLibraries;
            for (const auto& library : libraries) {
                if (!isLibraryHidden(library.id, hiddenLibraries)) {
                    visibleLibraries.push_back(library);
                } else {
                    brls::Logger::debug("LibraryTab: Hiding library: {}", library.name);
                }
            }

            // Update UI on main thread
            brls::sync([this, visibleLibraries]() {
                m_libraries = visibleLibraries;
                m_librariesBox->clearViews();

                for (const auto& library : m_libraries) {
                    brls::Logger::debug("LibraryTab: Adding library button: {}", library.name);
                    auto* btn = new brls::Button();
                    btn->setText(library.name);
                    btn->setMarginRight(10);

                    Library capturedLibrary = library;
                    btn->registerClickAction([this, capturedLibrary](brls::View* view) {
                        onLibrarySelected(capturedLibrary);
                        return true;
                    });

                    m_librariesBox->addView(btn);
                }

                // Load first library by default
                if (!m_libraries.empty()) {
                    brls::Logger::debug("LibraryTab: Loading first library: {}", m_libraries[0].name);
                    onLibrarySelected(m_libraries[0]);
                }

                m_loaded = true;
                brls::Logger::debug("LibraryTab: Libraries loading complete");
            });
        } else {
            brls::Logger::error("LibraryTab: Failed to fetch libraries");
            brls::sync([this]() {
                m_loaded = true;
            });
        }
    });
}

void LibraryTab::loadContent(const std::string& libraryId) {
    brls::Logger::debug("LibraryTab::loadContent - library: {} (async)", libraryId);

    std::string id = libraryId;  // Capture by value
    asyncRun([this, id]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> items;

        if (client.fetchLibraryItems(id, items)) {
            brls::Logger::info("LibraryTab: Got {} items for library {}", items.size(), id);

            // Update UI on main thread
            brls::sync([this, items]() {
                m_items = items;
                m_contentGrid->setDataSource(m_items);
            });
        } else {
            brls::Logger::error("LibraryTab: Failed to load content for library {}", id);
        }
    });
}

void LibraryTab::onLibrarySelected(const Library& library) {
    m_currentLibrary = library.id;
    m_titleLabel->setText("Library - " + library.name);
    loadContent(library.id);
}

void LibraryTab::onItemSelected(const MediaItem& item) {
    // For podcast episodes, play directly
    if (item.mediaType == MediaType::PODCAST_EPISODE) {
        Application::getInstance().pushPlayerActivity(item.id, item.episodeId);
        return;
    }

    // Show media detail view for audiobooks and podcasts
    auto* detailView = new MediaDetailView(item);
    brls::Application::pushActivity(new brls::Activity(detailView));
}

} // namespace vitaabs
