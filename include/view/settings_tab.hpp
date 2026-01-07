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
    void onAudioQualityChanged(int index);
    void onPlaybackSpeedChanged(int index);
    void onSleepTimerChanged(int index);
    void onSeekIntervalChanged(int index);
    void onManageHiddenLibraries();

    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_contentBox = nullptr;

    // Account section
    brls::Label* m_userLabel = nullptr;
    brls::Label* m_serverLabel = nullptr;

    // UI section
    brls::SelectorCell* m_themeSelector = nullptr;
    brls::BooleanCell* m_clockToggle = nullptr;
    brls::BooleanCell* m_animationsToggle = nullptr;
    brls::BooleanCell* m_debugLogToggle = nullptr;

    // Layout section
    brls::BooleanCell* m_sidebarLibrariesToggle = nullptr;
    brls::BooleanCell* m_collapseSidebarToggle = nullptr;
    brls::DetailCell* m_hiddenLibrariesCell = nullptr;

    // Content display section
    brls::BooleanCell* m_collectionsToggle = nullptr;
    brls::BooleanCell* m_seriesToggle = nullptr;
    brls::BooleanCell* m_authorsToggle = nullptr;
    brls::BooleanCell* m_progressToggle = nullptr;

    // Playback section
    brls::BooleanCell* m_autoPlayNextToggle = nullptr;
    brls::BooleanCell* m_resumeToggle = nullptr;
    brls::SelectorCell* m_playbackSpeedSelector = nullptr;
    brls::SelectorCell* m_sleepTimerSelector = nullptr;
    brls::SelectorCell* m_seekIntervalSelector = nullptr;
    brls::SelectorCell* m_longSeekIntervalSelector = nullptr;
    brls::BooleanCell* m_chapterListToggle = nullptr;

    // Audio section
    brls::SelectorCell* m_audioQualitySelector = nullptr;
    brls::BooleanCell* m_volumeBoostToggle = nullptr;
    brls::BooleanCell* m_autoBookmarkToggle = nullptr;

    // Downloads section
    brls::BooleanCell* m_autoStartDownloadsToggle = nullptr;
    brls::BooleanCell* m_wifiOnlyToggle = nullptr;
    brls::SelectorCell* m_concurrentDownloadsSelector = nullptr;
    brls::BooleanCell* m_deleteAfterFinishToggle = nullptr;
    brls::BooleanCell* m_syncProgressToggle = nullptr;
    brls::DetailCell* m_clearDownloadsCell = nullptr;
};

} // namespace vitaabs
