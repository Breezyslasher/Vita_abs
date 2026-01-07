/**
 * VitaABS - Live TV Tab
 * Placeholder - Audiobookshelf does not support Live TV
 */

#pragma once

#include <borealis.hpp>

namespace vitaabs {

class LiveTVTab : public brls::Box {
public:
    LiveTVTab();

private:
    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_messageLabel = nullptr;
};

} // namespace vitaabs
