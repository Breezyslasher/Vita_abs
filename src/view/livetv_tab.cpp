/**
 * VitaABS - Live TV Tab implementation
 * Placeholder - Audiobookshelf does not support Live TV
 */

#include "view/livetv_tab.hpp"

namespace vitaabs {

LiveTVTab::LiveTVTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::CENTER);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Live TV");
    m_titleLabel->setFontSize(28);
    m_titleLabel->setMarginBottom(20);
    this->addView(m_titleLabel);

    // Message
    m_messageLabel = new brls::Label();
    m_messageLabel->setText("Live TV is not available.\nAudiobookshelf does not support this feature.");
    m_messageLabel->setFontSize(16);
    m_messageLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    this->addView(m_messageLabel);
}

} // namespace vitaabs
