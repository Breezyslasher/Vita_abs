/**
 * VitaABS - Settings Tab implementation
 */

#include "view/settings_tab.hpp"
#include "app/application.hpp"
#include "app/audiobookshelf_client.hpp"
#include "app/downloads_manager.hpp"
#include "player/mpv_player.hpp"
#include "activity/player_activity.hpp"
#include <set>

namespace vitaabs {

SettingsTab::SettingsTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setGrow(1.0f);

    // Create scrolling container
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);

    m_contentBox = new brls::Box();
    m_contentBox->setAxis(brls::Axis::COLUMN);
    m_contentBox->setPadding(20);
    m_contentBox->setGrow(1.0f);

    // Create all sections
    createAccountSection();
    createUISection();
    createPlaybackSection();
    createAudioQualitySection();
    createChapterSection();
    createDownloadsSection();
    createDebugSection();
    createAboutSection();

    m_scrollView->setContentView(m_contentBox);
    this->addView(m_scrollView);
}

void SettingsTab::createAccountSection() {
    Application& app = Application::getInstance();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Account");
    m_contentBox->addView(header);

    // User info cell
    m_userLabel = new brls::Label();
    m_userLabel->setText("User: " + (app.getUsername().empty() ? "Not logged in" : app.getUsername()));
    m_userLabel->setFontSize(18);
    m_userLabel->setMarginLeft(16);
    m_userLabel->setMarginBottom(8);
    m_contentBox->addView(m_userLabel);

    // Server info cell
    m_serverLabel = new brls::Label();
    m_serverLabel->setText("Server: " + (app.getServerUrl().empty() ? "Not connected" : app.getServerUrl()));
    m_serverLabel->setFontSize(18);
    m_serverLabel->setMarginLeft(16);
    m_serverLabel->setMarginBottom(16);
    m_contentBox->addView(m_serverLabel);

    // Logout button
    auto* logoutCell = new brls::DetailCell();
    logoutCell->setText("Logout");
    logoutCell->setDetailText("Sign out from current server");
    logoutCell->registerClickAction([this](brls::View* view) {
        onLogout();
        return true;
    });
    m_contentBox->addView(logoutCell);
}

void SettingsTab::createUISection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("User Interface");
    m_contentBox->addView(header);

    // Theme selector
    m_themeSelector = new brls::SelectorCell();
    m_themeSelector->init("Theme", {"System", "Light", "Dark"}, static_cast<int>(settings.theme),
        [this](int index) {
            onThemeChanged(index);
        });
    m_contentBox->addView(m_themeSelector);

    // Show clock toggle
    m_clockToggle = new brls::BooleanCell();
    m_clockToggle->init("Show Clock", settings.showClock, [&settings](bool value) {
        settings.showClock = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_clockToggle);

    // Debug logging toggle
    m_debugLogToggle = new brls::BooleanCell();
    m_debugLogToggle->init("Debug Logging", settings.debugLogging, [&settings](bool value) {
        settings.debugLogging = value;
        Application::getInstance().applyLogLevel();
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_debugLogToggle);

    // Manage hidden libraries
    m_hiddenLibrariesCell = new brls::DetailCell();
    m_hiddenLibrariesCell->setText("Manage Hidden Libraries");
    int hiddenCount = 0;
    if (!settings.hiddenLibraries.empty()) {
        hiddenCount = 1;
        for (char c : settings.hiddenLibraries) {
            if (c == ',') hiddenCount++;
        }
    }
    m_hiddenLibrariesCell->setDetailText(hiddenCount > 0 ? std::to_string(hiddenCount) + " hidden" : "None hidden");
    m_hiddenLibrariesCell->registerClickAction([this](brls::View* view) {
        onManageHiddenLibraries();
        return true;
    });
    m_contentBox->addView(m_hiddenLibrariesCell);
}

void SettingsTab::createPlaybackSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Playback");
    m_contentBox->addView(header);

    // Playback speed selector
    m_playbackSpeedSelector = new brls::SelectorCell();
    m_playbackSpeedSelector->init("Playback Speed",
        {"0.5x", "0.75x", "1.0x (Normal)", "1.25x", "1.5x", "1.75x", "2.0x"},
        static_cast<int>(settings.playbackSpeed),
        [this](int index) {
            onPlaybackSpeedChanged(index);
        });
    m_contentBox->addView(m_playbackSpeedSelector);

    // Sleep timer selector
    m_sleepTimerSelector = new brls::SelectorCell();
    m_sleepTimerSelector->init("Sleep Timer",
        {"Off", "15 min", "30 min", "45 min", "1 hour", "End of Chapter"},
        static_cast<int>(settings.sleepTimer),
        [this](int index) {
            onSleepTimerChanged(index);
        });
    m_contentBox->addView(m_sleepTimerSelector);

    // Auto-play next toggle
    m_autoPlayToggle = new brls::BooleanCell();
    m_autoPlayToggle->init("Auto-Play Next Chapter", settings.autoPlayNext, [&settings](bool value) {
        settings.autoPlayNext = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_autoPlayToggle);

    // Resume playback toggle
    m_resumeToggle = new brls::BooleanCell();
    m_resumeToggle->init("Resume Playback", settings.resumePlayback, [&settings](bool value) {
        settings.resumePlayback = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_resumeToggle);

    // Seek interval selector
    m_seekIntervalSelector = new brls::SelectorCell();
    m_seekIntervalSelector->init("Seek Interval",
        {"10 seconds", "15 seconds", "30 seconds", "60 seconds"},
        settings.seekInterval == 10 ? 0 :
        settings.seekInterval == 15 ? 1 :
        settings.seekInterval == 30 ? 2 : 3,
        [this](int index) {
            onSeekIntervalChanged(index);
        });
    m_contentBox->addView(m_seekIntervalSelector);
}

void SettingsTab::createAudioQualitySection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Audio Quality");
    m_contentBox->addView(header);

    // Audio quality selector
    m_audioQualitySelector = new brls::SelectorCell();
    m_audioQualitySelector->init("Streaming Quality",
        {"Original", "High (256 kbps)", "Medium (128 kbps)", "Low (64 kbps)"},
        static_cast<int>(settings.audioQuality),
        [this](int index) {
            onAudioQualityChanged(index);
        });
    m_contentBox->addView(m_audioQualitySelector);
}

void SettingsTab::createChapterSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Chapters");
    m_contentBox->addView(header);

    // Show chapters toggle
    m_showChaptersToggle = new brls::BooleanCell();
    m_showChaptersToggle->init("Show Chapters", settings.showChapters, [&settings](bool value) {
        settings.showChapters = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_showChaptersToggle);

    // Mark chapters as read toggle
    m_markChaptersToggle = new brls::BooleanCell();
    m_markChaptersToggle->init("Auto-Mark Chapters Read", settings.markChaptersAsRead, [&settings](bool value) {
        settings.markChaptersAsRead = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_markChaptersToggle);

    // Skip intro toggle
    m_skipIntroToggle = new brls::BooleanCell();
    m_skipIntroToggle->init("Skip Intro", settings.skipIntro, [&settings](bool value) {
        settings.skipIntro = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_skipIntroToggle);

    // Skip credits toggle
    m_skipCreditsToggle = new brls::BooleanCell();
    m_skipCreditsToggle->init("Skip Credits", settings.skipCredits, [&settings](bool value) {
        settings.skipCredits = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_skipCreditsToggle);
}

void SettingsTab::createDownloadsSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Downloads");
    m_contentBox->addView(header);

    // Auto-start downloads toggle
    m_autoStartDownloadsToggle = new brls::BooleanCell();
    m_autoStartDownloadsToggle->init("Auto-Start Downloads", settings.autoStartDownloads, [&settings](bool value) {
        settings.autoStartDownloads = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_autoStartDownloadsToggle);

    // WiFi only toggle
    m_wifiOnlyToggle = new brls::BooleanCell();
    m_wifiOnlyToggle->init("Download Over WiFi Only", settings.downloadOverWifiOnly, [&settings](bool value) {
        settings.downloadOverWifiOnly = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_wifiOnlyToggle);

    // Concurrent downloads selector
    m_concurrentDownloadsSelector = new brls::SelectorCell();
    m_concurrentDownloadsSelector->init("Max Concurrent Downloads",
        {"1", "2", "3"},
        settings.maxConcurrentDownloads - 1,
        [&settings](int index) {
            settings.maxConcurrentDownloads = index + 1;
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(m_concurrentDownloadsSelector);

    // Delete after listen toggle
    m_deleteAfterListenToggle = new brls::BooleanCell();
    m_deleteAfterListenToggle->init("Delete After Listening", settings.deleteAfterListen, [&settings](bool value) {
        settings.deleteAfterListen = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_deleteAfterListenToggle);

    // Sync progress toggle
    m_syncProgressToggle = new brls::BooleanCell();
    m_syncProgressToggle->init("Sync Progress on Connect", settings.syncProgressOnConnect, [&settings](bool value) {
        settings.syncProgressOnConnect = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_syncProgressToggle);

    // Clear all downloads
    m_clearDownloadsCell = new brls::DetailCell();
    m_clearDownloadsCell->setText("Clear All Downloads");
    auto downloads = DownloadsManager::getInstance().getDownloads();
    m_clearDownloadsCell->setDetailText(std::to_string(downloads.size()) + " items");
    m_clearDownloadsCell->registerClickAction([this](brls::View* view) {
        brls::Dialog* dialog = new brls::Dialog("Delete all downloaded audiobooks?");

        dialog->addButton("Cancel", [dialog]() {
            dialog->close();
        });

        dialog->addButton("Delete All", [dialog, this]() {
            auto downloads = DownloadsManager::getInstance().getDownloads();
            for (const auto& item : downloads) {
                DownloadsManager::getInstance().deleteDownload(item.itemId);
            }
            if (m_clearDownloadsCell) {
                m_clearDownloadsCell->setDetailText("0 items");
            }
            dialog->close();
            brls::Application::notify("All downloads deleted");
        });

        dialog->open();
        return true;
    });
    m_contentBox->addView(m_clearDownloadsCell);

    // Downloads storage path info
    auto* pathLabel = new brls::Label();
    pathLabel->setText("Storage: " + DownloadsManager::getInstance().getDownloadsPath());
    pathLabel->setFontSize(14);
    pathLabel->setMarginLeft(16);
    pathLabel->setMarginTop(8);
    m_contentBox->addView(pathLabel);
}

void SettingsTab::createDebugSection() {
    // Section header
    auto* header = new brls::Header();
    header->setTitle("Debug");
    m_contentBox->addView(header);

    // Test local playback button
    auto* testLocalCell = new brls::DetailCell();
    testLocalCell->setText("Test Local Playback");
    testLocalCell->setDetailText("ux0:data/VitaABS/test.mp3");
    testLocalCell->registerClickAction([this](brls::View* view) {
        onTestLocalPlayback();
        return true;
    });
    m_contentBox->addView(testLocalCell);

    // Info label
    auto* infoLabel = new brls::Label();
    infoLabel->setText("Place test.mp3 or test.m4b in ux0:data/VitaABS/");
    infoLabel->setFontSize(14);
    infoLabel->setMarginLeft(16);
    infoLabel->setMarginTop(8);
    infoLabel->setMarginBottom(16);
    m_contentBox->addView(infoLabel);
}

void SettingsTab::createAboutSection() {
    // Section header
    auto* header = new brls::Header();
    header->setTitle("About");
    m_contentBox->addView(header);

    // Version info
    auto* versionCell = new brls::DetailCell();
    versionCell->setText("Version");
    versionCell->setDetailText(VITA_ABS_VERSION);
    m_contentBox->addView(versionCell);

    // App description
    auto* descLabel = new brls::Label();
    descLabel->setText("VitaABS - Audiobookshelf Client for PlayStation Vita");
    descLabel->setFontSize(16);
    descLabel->setMarginLeft(16);
    descLabel->setMarginTop(8);
    m_contentBox->addView(descLabel);

    // Credit
    auto* creditLabel = new brls::Label();
    creditLabel->setText("UI powered by Borealis");
    creditLabel->setFontSize(14);
    creditLabel->setMarginLeft(16);
    creditLabel->setMarginTop(4);
    creditLabel->setMarginBottom(20);
    m_contentBox->addView(creditLabel);
}

void SettingsTab::onLogout() {
    brls::Dialog* dialog = new brls::Dialog("Are you sure you want to logout?");

    dialog->addButton("Cancel", [dialog]() {
        dialog->close();
    });

    dialog->addButton("Logout", [dialog, this]() {
        dialog->close();

        // Clear credentials
        AudiobookshelfClient::getInstance().logout();
        Application::getInstance().setAuthToken("");
        Application::getInstance().setServerUrl("");
        Application::getInstance().setUsername("");
        Application::getInstance().saveSettings();

        // Go back to login
        Application::getInstance().pushLoginActivity();
    });

    dialog->open();
}

void SettingsTab::onThemeChanged(int index) {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    settings.theme = static_cast<AppTheme>(index);
    app.applyTheme();
    app.saveSettings();
}

void SettingsTab::onPlaybackSpeedChanged(int index) {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    settings.playbackSpeed = static_cast<PlaybackSpeed>(index);
    app.saveSettings();

    // Update current player if playing
    MpvPlayer& player = MpvPlayer::getInstance();
    if (player.isPlaying()) {
        player.setSpeed(app.getPlaybackSpeedValue());
    }
}

void SettingsTab::onAudioQualityChanged(int index) {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    settings.audioQuality = static_cast<AudioQuality>(index);
    app.saveSettings();
}

void SettingsTab::onSleepTimerChanged(int index) {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    settings.sleepTimer = static_cast<SleepTimer>(index);
    app.saveSettings();
}

void SettingsTab::onSeekIntervalChanged(int index) {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    switch (index) {
        case 0: settings.seekInterval = 10; break;
        case 1: settings.seekInterval = 15; break;
        case 2: settings.seekInterval = 30; break;
        case 3: settings.seekInterval = 60; break;
    }

    app.saveSettings();
}

void SettingsTab::onManageHiddenLibraries() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Fetch libraries
    std::vector<Library> libraries;
    AudiobookshelfClient::getInstance().fetchLibraries(libraries);

    if (libraries.empty()) {
        brls::Dialog* dialog = new brls::Dialog("No libraries found");
        dialog->addButton("OK", [dialog]() { dialog->close(); });
        dialog->open();
        return;
    }

    // Parse currently hidden libraries
    std::set<std::string> hiddenIds;
    std::string hidden = settings.hiddenLibraries;
    size_t pos = 0;
    while ((pos = hidden.find(',')) != std::string::npos) {
        std::string id = hidden.substr(0, pos);
        if (!id.empty()) hiddenIds.insert(id);
        hidden.erase(0, pos + 1);
    }
    if (!hidden.empty()) hiddenIds.insert(hidden);

    // Create scrollable dialog content
    brls::Box* outerBox = new brls::Box();
    outerBox->setAxis(brls::Axis::COLUMN);
    outerBox->setWidth(400);
    outerBox->setHeight(350);

    auto* title = new brls::Label();
    title->setText("Select libraries to hide:");
    title->setFontSize(20);
    title->setMarginBottom(15);
    title->setMarginLeft(20);
    title->setMarginTop(20);
    outerBox->addView(title);

    // Scrolling frame for checkboxes
    brls::ScrollingFrame* scrollFrame = new brls::ScrollingFrame();
    scrollFrame->setGrow(1.0f);

    brls::Box* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setPaddingLeft(20);
    content->setPaddingRight(20);

    std::vector<std::pair<std::string, brls::BooleanCell*>> checkboxes;

    for (const auto& library : libraries) {
        auto* checkbox = new brls::BooleanCell();
        bool isHidden = (hiddenIds.find(library.id) != hiddenIds.end());
        checkbox->init(library.name, isHidden, [](bool value) {});
        content->addView(checkbox);
        checkboxes.push_back({library.id, checkbox});
    }

    scrollFrame->setContentView(content);
    outerBox->addView(scrollFrame);

    brls::Dialog* dialog = new brls::Dialog(outerBox);

    dialog->addButton("Cancel", [dialog]() {
        dialog->close();
    });

    dialog->addButton("Save", [dialog, checkboxes, this]() {
        Application& app = Application::getInstance();
        AppSettings& settings = app.getSettings();

        std::string newHidden;
        for (const auto& pair : checkboxes) {
            if (pair.second->isOn()) {
                if (!newHidden.empty()) newHidden += ",";
                newHidden += pair.first;
            }
        }

        settings.hiddenLibraries = newHidden;
        app.saveSettings();

        // Update the cell text
        int count = 0;
        if (!newHidden.empty()) {
            count = 1;
            for (char c : newHidden) {
                if (c == ',') count++;
            }
        }
        if (m_hiddenLibrariesCell) {
            m_hiddenLibrariesCell->setDetailText(count > 0 ? std::to_string(count) + " hidden" : "None hidden");
        }

        dialog->close();
    });

    dialog->open();
}

void SettingsTab::onTestLocalPlayback() {
    brls::Logger::info("SettingsTab: Testing local playback...");

    // Check for test files
    const std::string basePath = "ux0:data/VitaABS/";
    std::string testFile;

    // Try audiobook formats
    std::vector<std::string> testFiles = {
        basePath + "test.m4b",
        basePath + "test.mp3",
        basePath + "test.m4a",
        basePath + "test.ogg",
        basePath + "test.flac"
    };

    for (const auto& file : testFiles) {
        FILE* f = fopen(file.c_str(), "rb");
        if (f) {
            fclose(f);
            testFile = file;
            brls::Logger::info("SettingsTab: Found test file: {}", testFile);
            break;
        }
    }

    if (testFile.empty()) {
        brls::Application::notify("No test file found in ux0:data/VitaABS/");
        brls::Logger::error("SettingsTab: No test file found");
        return;
    }

    // Push player activity with the test file
    brls::Logger::info("SettingsTab: Pushing player activity for: {}", testFile);
    PlayerActivity* activity = PlayerActivity::createForDirectFile(testFile);
    brls::Application::pushActivity(activity);
}

} // namespace vitaabs
