/**
 * VitaABS - Media Item Cell
 * A cell for displaying media items in a grid
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include "app/audiobookshelf_client.hpp"

namespace vitaabs {

class MediaItemCell : public brls::Box {
public:
    MediaItemCell();
    ~MediaItemCell();

    void setItem(const MediaItem& item);
    const MediaItem& getItem() const { return m_item; }

    void onFocusGained() override;
    void onFocusLost() override;

    static brls::View* create();

private:
    void loadThumbnail();
    void updateFocusInfo(bool focused);

    MediaItem m_item;
    std::string m_originalTitle;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    brls::Image* m_thumbnailImage = nullptr;
    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_subtitleLabel = nullptr;
    brls::Label* m_descriptionLabel = nullptr;
    brls::Rectangle* m_progressBar = nullptr;
};

} // namespace vitaabs
