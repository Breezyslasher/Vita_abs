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

    // Thumbnail image
    m_thumbnailImage = new brls::Image();
    m_thumbnailImage->setWidth(110);
    m_thumbnailImage->setHeight(165);
    m_thumbnailImage->setScalingType(brls::ImageScalingType::FIT);
    m_thumbnailImage->setCornerRadius(4);
    this->addView(m_thumbnailImage);

    // Title label
    m_titleLabel = new brls::Label();
    m_titleLabel->setFontSize(12);
    m_titleLabel->setMarginTop(5);
    m_titleLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    this->addView(m_titleLabel);

    // Subtitle label (for episodes: Episode N)
    m_subtitleLabel = new brls::Label();
    m_subtitleLabel->setFontSize(10);
    m_subtitleLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_subtitleLabel->setVisibility(brls::Visibility::GONE);
    this->addView(m_subtitleLabel);

    // Description label (shows on focus)
    m_descriptionLabel = new brls::Label();
    m_descriptionLabel->setFontSize(9);
    m_descriptionLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_descriptionLabel->setVisibility(brls::Visibility::GONE);
    this->addView(m_descriptionLabel);

    // Progress bar (for continue listening)
    m_progressBar = new brls::Rectangle();
    m_progressBar->setHeight(3);
    m_progressBar->setWidth(0);
    m_progressBar->setColor(nvgRGBA(229, 160, 13, 255)); // Progress color
    m_progressBar->setVisibility(brls::Visibility::GONE);
    this->addView(m_progressBar);
}

void MediaItemCell::setItem(const MediaItem& item) {
    m_item = item;

    // Audiobookshelf uses square covers for most items
    // Portrait poster style
    m_thumbnailImage->setWidth(110);
    m_thumbnailImage->setHeight(165);

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

    // Set subtitle for podcast episodes
    if (m_subtitleLabel) {
        if (item.mediaType == MediaType::PODCAST_EPISODE) {
            if (item.index > 0) {
                m_subtitleLabel->setText("Episode " + std::to_string(item.index));
                m_subtitleLabel->setVisibility(brls::Visibility::VISIBLE);
            } else {
                m_subtitleLabel->setVisibility(brls::Visibility::GONE);
            }
        } else {
            m_subtitleLabel->setVisibility(brls::Visibility::GONE);
        }
    }

    // Show progress bar for items with listening progress
    if (m_progressBar && item.currentTime > 0 && item.duration > 0) {
        float progress = item.currentTime / item.duration;
        m_progressBar->setWidth(110 * progress);
        m_progressBar->setVisibility(brls::Visibility::VISIBLE);
    }

    // Load thumbnail
    loadThumbnail();
}

void MediaItemCell::loadThumbnail() {
    if (!m_thumbnailImage) return;

    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    int width = 220;
    int height = 330;

    std::string thumbPath = m_item.coverPath;
    if (thumbPath.empty()) return;

    std::string url = client.getCoverUrl(thumbPath, width, height);

    ImageLoader::loadAsync(url, [this](brls::Image* image) {
        // Image loaded callback
    }, m_thumbnailImage);
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
    if (!m_titleLabel || !m_descriptionLabel) return;

    // For podcast episodes, show extended info on focus
    if (m_item.mediaType == MediaType::PODCAST_EPISODE) {
        if (focused) {
            // Show full title
            m_titleLabel->setText(m_item.title);

            // Show duration and other info
            std::string info;
            if (m_item.duration > 0) {
                int minutes = (int)(m_item.duration / 60.0f);
                info = std::to_string(minutes) + " min";
            }
            if (!m_item.summary.empty()) {
                // Show first 50 chars of summary
                std::string summary = m_item.summary;
                if (summary.length() > 50) {
                    summary = summary.substr(0, 47) + "...";
                }
                if (!info.empty()) info += " - ";
                info += summary;
            }
            if (!info.empty()) {
                m_descriptionLabel->setText(info);
                m_descriptionLabel->setVisibility(brls::Visibility::VISIBLE);
            }
        } else {
            // Restore truncated title
            m_titleLabel->setText(m_originalTitle);
            m_descriptionLabel->setVisibility(brls::Visibility::GONE);
        }
    } else if (m_item.mediaType == MediaType::BOOK) {
        // Show author and duration for books on focus
        if (focused) {
            std::string info;
            if (!m_item.authorName.empty()) {
                info = m_item.authorName;
            }
            if (m_item.duration > 0) {
                int hours = (int)(m_item.duration / 3600.0f);
                int mins = (int)((m_item.duration - hours * 3600) / 60.0f);
                if (!info.empty()) info += " - ";
                info += std::to_string(hours) + "h " + std::to_string(mins) + "m";
            }
            if (!info.empty()) {
                m_descriptionLabel->setText(info);
                m_descriptionLabel->setVisibility(brls::Visibility::VISIBLE);
            }
            // Show full title
            m_titleLabel->setText(m_item.title);
        } else {
            m_titleLabel->setText(m_originalTitle);
            m_descriptionLabel->setVisibility(brls::Visibility::GONE);
        }
    } else if (m_item.mediaType == MediaType::PODCAST) {
        // Show episode count for podcasts on focus
        if (focused) {
            std::string info;
            if (m_item.numEpisodes > 0) {
                info = std::to_string(m_item.numEpisodes) + " episodes";
            }
            if (!info.empty()) {
                m_descriptionLabel->setText(info);
                m_descriptionLabel->setVisibility(brls::Visibility::VISIBLE);
            }
            m_titleLabel->setText(m_item.title);
        } else {
            m_titleLabel->setText(m_originalTitle);
            m_descriptionLabel->setVisibility(brls::Visibility::GONE);
        }
    }
}

} // namespace vitaabs
