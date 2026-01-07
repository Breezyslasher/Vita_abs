/**
 * VitaABS - Home Tab implementation
 */

#include "view/home_tab.hpp"
#include "view/media_item_cell.hpp"
#include "view/media_detail_view.hpp"
#include "app/application.hpp"
#include "utils/async.hpp"

namespace vitaabs {

HomeTab::HomeTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setGrow(1.0f);

    // Create vertical scrolling container for the entire tab
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);

    m_scrollContent = new brls::Box();
    m_scrollContent->setAxis(brls::Axis::COLUMN);
    m_scrollContent->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_scrollContent->setAlignItems(brls::AlignItems::STRETCH);
    m_scrollContent->setPadding(20);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Home");
    m_titleLabel->setFontSize(28);
    m_titleLabel->setMarginBottom(20);
    m_scrollContent->addView(m_titleLabel);

    // Continue Listening section
    auto* continueLabel = new brls::Label();
    continueLabel->setText("Continue Listening");
    continueLabel->setFontSize(22);
    continueLabel->setMarginBottom(10);
    m_scrollContent->addView(continueLabel);

    m_continueListeningRow = createMediaRow(&m_continueListeningContent);
    m_scrollContent->addView(m_continueListeningRow);

    // Recent Audiobooks section
    auto* audiobooksLabel = new brls::Label();
    audiobooksLabel->setText("Recent Audiobooks");
    audiobooksLabel->setFontSize(22);
    audiobooksLabel->setMarginBottom(10);
    audiobooksLabel->setMarginTop(15);
    m_scrollContent->addView(audiobooksLabel);

    m_audiobooksRow = createMediaRow(&m_audiobooksContent);
    m_scrollContent->addView(m_audiobooksRow);

    // Recent Podcasts section
    auto* podcastsLabel = new brls::Label();
    podcastsLabel->setText("Recent Podcasts");
    podcastsLabel->setFontSize(22);
    podcastsLabel->setMarginBottom(10);
    podcastsLabel->setMarginTop(15);
    m_scrollContent->addView(podcastsLabel);

    m_podcastsRow = createMediaRow(&m_podcastsContent);
    m_scrollContent->addView(m_podcastsRow);

    m_scrollView->setContentView(m_scrollContent);
    this->addView(m_scrollView);

    // Load content immediately
    brls::Logger::debug("HomeTab: Loading content...");
    loadContent();
}

brls::HScrollingFrame* HomeTab::createMediaRow(brls::Box** contentOut) {
    auto* scrollFrame = new brls::HScrollingFrame();
    scrollFrame->setHeight(180);
    scrollFrame->setMarginBottom(10);

    auto* content = new brls::Box();
    content->setAxis(brls::Axis::ROW);
    content->setJustifyContent(brls::JustifyContent::FLEX_START);
    content->setAlignItems(brls::AlignItems::CENTER);

    scrollFrame->setContentView(content);

    // Return content box via output parameter
    if (contentOut) {
        *contentOut = content;
    }

    return scrollFrame;
}

void HomeTab::populateRow(brls::Box* rowContent, const std::vector<MediaItem>& items) {
    if (!rowContent) return;

    rowContent->clearViews();

    for (const auto& item : items) {
        auto* cell = new MediaItemCell();
        cell->setItem(item);
        cell->setWidth(120);
        cell->setHeight(170);
        cell->setMarginRight(10);

        MediaItem capturedItem = item;
        cell->registerClickAction([this, capturedItem](brls::View* view) {
            onItemSelected(capturedItem);
            return true;
        });

        rowContent->addView(cell);
    }

    // Add placeholder if empty
    if (items.empty()) {
        auto* placeholder = new brls::Label();
        placeholder->setText("No items");
        placeholder->setFontSize(16);
        placeholder->setMarginLeft(10);
        rowContent->addView(placeholder);
    }
}

void HomeTab::onFocusGained() {
    brls::Box::onFocusGained();

    if (!m_loaded) {
        loadContent();
    }
}

void HomeTab::loadContent() {
    brls::Logger::debug("HomeTab::loadContent - Starting async load");

    // Load continue listening asynchronously
    asyncRun([this]() {
        brls::Logger::debug("HomeTab: Fetching continue listening (async)...");
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> items;

        if (client.fetchItemsInProgress(items)) {
            brls::Logger::info("HomeTab: Got {} continue listening items", items.size());

            brls::sync([this, items]() {
                m_continueListening = items;
                populateRow(m_continueListeningContent, m_continueListening);
            });
        } else {
            brls::Logger::error("HomeTab: Failed to fetch continue listening");
        }
    });

    // Load personalized content from libraries
    asyncRun([this]() {
        brls::Logger::debug("HomeTab: Fetching library content for home...");
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

        // First get all libraries
        std::vector<Library> libraries;
        if (!client.fetchLibraries(libraries)) {
            brls::Logger::error("HomeTab: Failed to fetch libraries");
            return;
        }

        // Get hidden libraries setting
        std::string hiddenLibraries = Application::getInstance().getSettings().hiddenLibraries;

        std::vector<MediaItem> audiobooks;
        std::vector<MediaItem> podcasts;

        // Helper to check if library is hidden
        auto isHidden = [&hiddenLibraries](const std::string& id) -> bool {
            if (hiddenLibraries.empty()) return false;
            std::string hidden = hiddenLibraries;
            size_t pos = 0;
            while ((pos = hidden.find(',')) != std::string::npos) {
                if (hidden.substr(0, pos) == id) return true;
                hidden.erase(0, pos + 1);
            }
            return (hidden == id);
        };

        // Fetch recent items from each library
        for (const auto& library : libraries) {
            // Skip hidden libraries
            if (isHidden(library.id)) {
                brls::Logger::debug("HomeTab: Skipping hidden library: {}", library.name);
                continue;
            }

            std::vector<MediaItem> libraryItems;

            // Fetch recent items using library endpoint
            if (client.fetchLibraryItems(library.id, libraryItems, 0, 20, "recent")) {
                // Sort items by library type
                for (auto& item : libraryItems) {
                    if (library.mediaType == "book") {
                        if (audiobooks.size() < 20) audiobooks.push_back(item);
                    } else if (library.mediaType == "podcast") {
                        if (podcasts.size() < 20) podcasts.push_back(item);
                    }
                }
            }
        }

        brls::Logger::info("HomeTab: Got {} audiobooks, {} podcasts",
                           audiobooks.size(), podcasts.size());

        // Update UI on main thread
        brls::sync([this, audiobooks, podcasts]() {
            m_recentAudiobooks = audiobooks;
            m_recentPodcasts = podcasts;

            populateRow(m_audiobooksContent, m_recentAudiobooks);
            populateRow(m_podcastsContent, m_recentPodcasts);
        });
    });

    m_loaded = true;
    brls::Logger::debug("HomeTab: Async content loading started");
}

void HomeTab::onItemSelected(const MediaItem& item) {
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
