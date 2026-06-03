/**
 * VitaABS - Settings Tab
 * Application settings and user info
 */

#pragma once

#include <borealis.hpp>

namespace vitaabs {

class SettingsTab : public brls::Box {
public:
    SettingsTab();

private:
    void createAccountSection();
    void createUISection();
    void createLayoutSection();
    void createContentDisplaySection();
    void createPlaybackSection();
    void createAudioSection();
    void createDownloadsSection();
    void createAboutSection();
    void createDebugSection();

    void onLogout();
    void onTestLocalPlayback();
    void onThemeChanged(int index);
    void onSeekIntervalChanged(int index);
    void onManageSidebarOrder();

    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_contentBox = nullptr;

    // Account section
    brls::Label* m_userLabel = nullptr;
    brls::Label* m_serverLabel = nullptr;

    // UI section
    brls::SelectorCell* m_themeSelector = nullptr;
    brls::BooleanCell* m_debugLogToggle = nullptr;

    // Layout section
    brls::DetailCell* m_sidebarOrderCell = nullptr;

    // Content display section
    brls::BooleanCell* m_collectionsToggle = nullptr;

    // Playback section
    brls::BooleanCell* m_resumeToggle = nullptr;
    brls::SelectorCell* m_seekIntervalSelector = nullptr;

    // Downloads section
    brls::BooleanCell* m_autoStartDownloadsToggle = nullptr;
    brls::BooleanCell* m_deleteAfterWatchToggle = nullptr;
    brls::DetailCell* m_clearDownloadsCell = nullptr;
};

} // namespace vitaabs
