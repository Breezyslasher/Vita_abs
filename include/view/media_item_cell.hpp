/**
 * VitaABS - Media Item Cell
 * A cell for displaying audiobook/podcast items in a grid
 */

#pragma once

#include <borealis.hpp>
#include "app/audiobookshelf_client.hpp"

namespace vitaabs {

class MediaItemCell : public brls::Box {
public:
    MediaItemCell();

    void setItem(const MediaItem& item);
    const MediaItem& getItem() const { return m_item; }

    void onFocusGained() override;
    void onFocusLost() override;

    static brls::View* create();

private:
    void loadCover();
    void updateFocusInfo(bool focused);

    MediaItem m_item;
    std::string m_originalTitle;  // Store original truncated title

    brls::Image* m_coverImage = nullptr;
    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_authorLabel = nullptr;
    brls::Label* m_durationLabel = nullptr;  // Shows on focus
    brls::Rectangle* m_progressBar = nullptr;
};

} // namespace vitaabs
