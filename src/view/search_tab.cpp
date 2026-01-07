/**
 * VitaABS - Search Tab implementation
 */

#include "view/search_tab.hpp"
#include "view/media_detail_view.hpp"
#include "view/media_item_cell.hpp"
#include "app/application.hpp"

namespace vitaabs {

SearchTab::SearchTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Search");
    m_titleLabel->setFontSize(28);
    m_titleLabel->setMarginBottom(20);
    this->addView(m_titleLabel);

    // Search input label (acts as button to open keyboard)
    m_searchLabel = new brls::Label();
    m_searchLabel->setText("Tap to search...");
    m_searchLabel->setFontSize(20);
    m_searchLabel->setMarginBottom(10);
    m_searchLabel->setFocusable(true);

    m_searchLabel->registerClickAction([this](brls::View* view) {
        brls::Application::getImeManager()->openForText([this](std::string text) {
            m_searchQuery = text;
            m_searchLabel->setText(std::string("Search: ") + text);
            performSearch(text);
        }, "Search", "Enter search query", 256, m_searchQuery);
        return true;
    });
    m_searchLabel->addGestureRecognizer(new brls::TapGestureRecognizer(m_searchLabel));

    this->addView(m_searchLabel);

    // Results label
    m_resultsLabel = new brls::Label();
    m_resultsLabel->setText("");
    m_resultsLabel->setFontSize(18);
    m_resultsLabel->setMarginBottom(10);
    this->addView(m_resultsLabel);

    // Scrollable content for results
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);

    m_scrollContent = new brls::Box();
    m_scrollContent->setAxis(brls::Axis::COLUMN);
    m_scrollContent->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_scrollContent->setAlignItems(brls::AlignItems::STRETCH);

    // Audiobooks row
    auto* audiobooksLabel = new brls::Label();
    audiobooksLabel->setText("Audiobooks");
    audiobooksLabel->setFontSize(20);
    audiobooksLabel->setMarginBottom(10);
    audiobooksLabel->setVisibility(brls::Visibility::GONE);
    m_scrollContent->addView(audiobooksLabel);

    m_audiobooksRow = new brls::HScrollingFrame();
    m_audiobooksRow->setHeight(180);
    m_audiobooksRow->setMarginBottom(15);
    m_audiobooksRow->setVisibility(brls::Visibility::GONE);

    m_audiobooksContent = new brls::Box();
    m_audiobooksContent->setAxis(brls::Axis::ROW);
    m_audiobooksContent->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_audiobooksRow->setContentView(m_audiobooksContent);
    m_scrollContent->addView(m_audiobooksRow);

    // Podcasts row
    auto* podcastsLabel = new brls::Label();
    podcastsLabel->setText("Podcasts");
    podcastsLabel->setFontSize(20);
    podcastsLabel->setMarginBottom(10);
    podcastsLabel->setVisibility(brls::Visibility::GONE);
    m_scrollContent->addView(podcastsLabel);

    m_podcastsRow = new brls::HScrollingFrame();
    m_podcastsRow->setHeight(180);
    m_podcastsRow->setMarginBottom(15);
    m_podcastsRow->setVisibility(brls::Visibility::GONE);

    m_podcastsContent = new brls::Box();
    m_podcastsContent->setAxis(brls::Axis::ROW);
    m_podcastsContent->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_podcastsRow->setContentView(m_podcastsContent);
    m_scrollContent->addView(m_podcastsRow);

    // Authors row
    auto* authorsLabel = new brls::Label();
    authorsLabel->setText("Authors");
    authorsLabel->setFontSize(20);
    authorsLabel->setMarginBottom(10);
    authorsLabel->setVisibility(brls::Visibility::GONE);
    m_scrollContent->addView(authorsLabel);

    m_authorsRow = new brls::HScrollingFrame();
    m_authorsRow->setHeight(180);
    m_authorsRow->setMarginBottom(15);
    m_authorsRow->setVisibility(brls::Visibility::GONE);

    m_authorsContent = new brls::Box();
    m_authorsContent->setAxis(brls::Axis::ROW);
    m_authorsContent->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_authorsRow->setContentView(m_authorsContent);
    m_scrollContent->addView(m_authorsRow);

    // Series row
    auto* seriesLabel = new brls::Label();
    seriesLabel->setText("Series");
    seriesLabel->setFontSize(20);
    seriesLabel->setMarginBottom(10);
    seriesLabel->setVisibility(brls::Visibility::GONE);
    m_scrollContent->addView(seriesLabel);

    m_seriesRow = new brls::HScrollingFrame();
    m_seriesRow->setHeight(180);
    m_seriesRow->setMarginBottom(15);
    m_seriesRow->setVisibility(brls::Visibility::GONE);

    m_seriesContent = new brls::Box();
    m_seriesContent->setAxis(brls::Axis::ROW);
    m_seriesContent->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_seriesRow->setContentView(m_seriesContent);
    m_scrollContent->addView(m_seriesRow);

    m_scrollView->setContentView(m_scrollContent);
    this->addView(m_scrollView);
}

void SearchTab::onFocusGained() {
    brls::Box::onFocusGained();

    // Focus search label
    if (m_searchLabel) {
        brls::Application::giveFocus(m_searchLabel);
    }
}

void SearchTab::populateRow(brls::Box* rowContent, const std::vector<MediaItem>& items) {
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
}

void SearchTab::performSearch(const std::string& query) {
    if (query.empty()) {
        m_resultsLabel->setText("");
        m_results.clear();
        m_audiobooks.clear();
        m_podcasts.clear();

        // Hide all rows
        m_audiobooksRow->setVisibility(brls::Visibility::GONE);
        m_podcastsRow->setVisibility(brls::Visibility::GONE);
        m_authorsRow->setVisibility(brls::Visibility::GONE);
        m_seriesRow->setVisibility(brls::Visibility::GONE);

        // Hide labels
        auto& views = m_scrollContent->getChildren();
        for (size_t i = 0; i < views.size(); i++) {
            views[i]->setVisibility(brls::Visibility::GONE);
        }
        return;
    }

    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    if (client.search(query, m_results)) {
        m_resultsLabel->setText("Found " + std::to_string(m_results.size()) + " results");

        // Organize results by type
        m_audiobooks.clear();
        m_podcasts.clear();
        std::vector<MediaItem> authors;
        std::vector<MediaItem> series;

        for (const auto& item : m_results) {
            if (item.mediaType == MediaType::BOOK) {
                m_audiobooks.push_back(item);
            } else if (item.mediaType == MediaType::PODCAST ||
                       item.mediaType == MediaType::PODCAST_EPISODE) {
                m_podcasts.push_back(item);
            }
            // Note: Authors and Series would need special handling
            // as they may come from separate search result fields
        }

        // Update rows visibility and content
        // Order: Audiobooks(0,1), Podcasts(2,3), Authors(4,5), Series(6,7)
        auto& views = m_scrollContent->getChildren();

        // Audiobooks (label at index 0, row at index 1)
        if (!m_audiobooks.empty()) {
            views[0]->setVisibility(brls::Visibility::VISIBLE);
            m_audiobooksRow->setVisibility(brls::Visibility::VISIBLE);
            populateRow(m_audiobooksContent, m_audiobooks);
        } else {
            views[0]->setVisibility(brls::Visibility::GONE);
            m_audiobooksRow->setVisibility(brls::Visibility::GONE);
        }

        // Podcasts (label at index 2, row at index 3)
        if (!m_podcasts.empty()) {
            views[2]->setVisibility(brls::Visibility::VISIBLE);
            m_podcastsRow->setVisibility(brls::Visibility::VISIBLE);
            populateRow(m_podcastsContent, m_podcasts);
        } else {
            views[2]->setVisibility(brls::Visibility::GONE);
            m_podcastsRow->setVisibility(brls::Visibility::GONE);
        }

        // Authors (label at index 4, row at index 5)
        if (!authors.empty()) {
            views[4]->setVisibility(brls::Visibility::VISIBLE);
            m_authorsRow->setVisibility(brls::Visibility::VISIBLE);
            populateRow(m_authorsContent, authors);
        } else {
            views[4]->setVisibility(brls::Visibility::GONE);
            m_authorsRow->setVisibility(brls::Visibility::GONE);
        }

        // Series (label at index 6, row at index 7)
        if (!series.empty()) {
            views[6]->setVisibility(brls::Visibility::VISIBLE);
            m_seriesRow->setVisibility(brls::Visibility::VISIBLE);
            populateRow(m_seriesContent, series);
        } else {
            views[6]->setVisibility(brls::Visibility::GONE);
            m_seriesRow->setVisibility(brls::Visibility::GONE);
        }

    } else {
        m_resultsLabel->setText("Search failed");
        m_results.clear();
    }
}

void SearchTab::onItemSelected(const MediaItem& item) {
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
