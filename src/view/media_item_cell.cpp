/**
 * VitaABS - Media Item Cell implementation
 */

#include "view/media_item_cell.hpp"
#include "app/audiobookshelf_client.hpp"
#include "utils/image_loader.hpp"

namespace vitaabs {

MediaItemCell::MediaItemCell() {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setPadding(5);
    this->setFocusable(true);
    this->setCornerRadius(8);
    this->setBackgroundColor(nvgRGBA(50, 50, 50, 255));

    // Cover image (square for audiobooks)
    m_coverImage = new brls::Image();
    m_coverImage->setWidth(110);
    m_coverImage->setHeight(110);
    m_coverImage->setScalingType(brls::ImageScalingType::FIT);
    m_coverImage->setCornerRadius(4);
    this->addView(m_coverImage);

    // Title label
    m_titleLabel = new brls::Label();
    m_titleLabel->setFontSize(12);
    m_titleLabel->setMarginTop(5);
    m_titleLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    this->addView(m_titleLabel);

    // Author label
    m_authorLabel = new brls::Label();
    m_authorLabel->setFontSize(10);
    m_authorLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_authorLabel->setVisibility(brls::Visibility::GONE);
    this->addView(m_authorLabel);

    // Duration label (shows on focus)
    m_durationLabel = new brls::Label();
    m_durationLabel->setFontSize(9);
    m_durationLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_durationLabel->setVisibility(brls::Visibility::GONE);
    this->addView(m_durationLabel);

    // Progress bar (for continue listening)
    m_progressBar = new brls::Rectangle();
    m_progressBar->setHeight(3);
    m_progressBar->setWidth(0);
    m_progressBar->setColor(nvgRGBA(140, 82, 255, 255)); // Purple for audiobooks
    m_progressBar->setVisibility(brls::Visibility::GONE);
    this->addView(m_progressBar);
}

void MediaItemCell::setItem(const MediaItem& item) {
    m_item = item;

    // Set title
    if (m_titleLabel) {
        std::string title = item.title;
        // Truncate long titles
        if (title.length() > 15) {
            title = title.substr(0, 13) + "...";
        }
        m_originalTitle = title;  // Store truncated title for focus restore
        m_titleLabel->setText(title);
    }

    // Set author for audiobooks
    if (m_authorLabel && !item.author.empty()) {
        std::string author = item.author;
        if (author.length() > 18) {
            author = author.substr(0, 16) + "...";
        }
        m_authorLabel->setText(author);
        m_authorLabel->setVisibility(brls::Visibility::VISIBLE);
    }

    // Show progress bar for items with progress
    if (m_progressBar && item.progress > 0.0f && item.progress < 1.0f) {
        m_progressBar->setWidth(110 * item.progress);
        m_progressBar->setVisibility(brls::Visibility::VISIBLE);
    }

    // Load cover image
    loadCover();
}

void MediaItemCell::loadCover() {
    if (!m_coverImage) return;

    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    // Use square cover art
    int width = 220;
    int height = 220;

    if (m_item.coverUrl.empty() && m_item.id.empty()) return;

    std::string url = client.getCoverUrl(m_item.id, width, height);

    ImageLoader::loadAsync(url, [this](brls::Image* image) {
        // Image loaded callback
    }, m_coverImage);
}

brls::View* MediaItemCell::create() {
    return new MediaItemCell();
}

void MediaItemCell::onFocusGained() {
    brls::Box::onFocusGained();
    updateFocusInfo(true);
}

void MediaItemCell::onFocusLost() {
    brls::Box::onFocusLost();
    updateFocusInfo(false);
}

void MediaItemCell::updateFocusInfo(bool focused) {
    if (!m_titleLabel || !m_durationLabel) return;

    if (focused) {
        // Show full title
        m_titleLabel->setText(m_item.title);

        // Show duration info
        std::string info;
        if (m_item.duration > 0) {
            int totalMinutes = static_cast<int>(m_item.duration / 60.0f);
            int hours = totalMinutes / 60;
            int minutes = totalMinutes % 60;
            if (hours > 0) {
                info = std::to_string(hours) + "h " + std::to_string(minutes) + "m";
            } else {
                info = std::to_string(minutes) + " min";
            }
        }

        // Add progress info
        if (m_item.progress > 0.0f && m_item.progress < 1.0f) {
            int percent = static_cast<int>(m_item.progress * 100);
            if (!info.empty()) info += " - ";
            info += std::to_string(percent) + "%";
        }

        if (!info.empty()) {
            m_durationLabel->setText(info);
            m_durationLabel->setVisibility(brls::Visibility::VISIBLE);
        }

        // Show series info for audiobooks
        if (m_item.mediaType == MediaType::BOOK && !m_item.series.empty()) {
            std::string seriesInfo = m_item.series;
            if (m_item.seriesSequence > 0) {
                seriesInfo += " #" + std::to_string(m_item.seriesSequence);
            }
            if (m_authorLabel) {
                m_authorLabel->setText(seriesInfo);
                m_authorLabel->setVisibility(brls::Visibility::VISIBLE);
            }
        }
    } else {
        // Restore truncated title
        m_titleLabel->setText(m_originalTitle);
        m_durationLabel->setVisibility(brls::Visibility::GONE);

        // Restore author for audiobooks
        if (m_item.mediaType == MediaType::BOOK && !m_item.author.empty()) {
            std::string author = m_item.author;
            if (author.length() > 18) {
                author = author.substr(0, 16) + "...";
            }
            if (m_authorLabel) {
                m_authorLabel->setText(author);
            }
        }
    }
}

} // namespace vitaabs
