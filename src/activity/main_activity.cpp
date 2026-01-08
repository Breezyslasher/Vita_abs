/**
 * VitaABS - Main Activity implementation
 */

#include "activity/main_activity.hpp"
#include "view/library_section_tab.hpp"
#include "view/search_tab.hpp"
#include "view/settings_tab.hpp"
#include "view/downloads_tab.hpp"
#include "app/downloads_manager.hpp"
#include "app/application.hpp"
#include "app/audiobookshelf_client.hpp"
#include "utils/async.hpp"

#include <algorithm>

namespace vitaabs {

// Cached library sections
static std::vector<Library> s_cachedSections;

// Helper to calculate text width (approximate based on character count)
static int calculateTextWidth(const std::string& text) {
    const int charWidth = 12;
    const int padding = 50;
    return static_cast<int>(text.length()) * charWidth + padding;
}

MainActivity::MainActivity() {
    brls::Logger::debug("MainActivity created");
}

brls::View* MainActivity::createContentView() {
    return brls::View::createFromXMLResource("activity/main.xml");
}

void MainActivity::onContentAvailable() {
    brls::Logger::debug("MainActivity content available");

    if (tabFrame) {
        AppSettings& settings = Application::getInstance().getSettings();

        // Fetch libraries to calculate sidebar width and populate tabs
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<Library> sections;
        client.fetchLibraries(sections);
        s_cachedSections = sections;

        // Calculate dynamic sidebar width based on content
        int sidebarWidth = 200;  // Minimum width

        std::vector<std::string> standardTabs = {"Search", "Downloads", "Settings"};
        for (const auto& tab : standardTabs) {
            sidebarWidth = std::max(sidebarWidth, calculateTextWidth(tab));
        }

        // Check library names for width
        for (const auto& section : sections) {
            sidebarWidth = std::max(sidebarWidth, calculateTextWidth(section.name));
        }

        // Apply sidebar width (with reasonable bounds)
        sidebarWidth = std::min(sidebarWidth, 350);
        brls::View* sidebar = tabFrame->getView("brls/tab_frame/sidebar");
        if (sidebar) {
            if (settings.collapseSidebar) {
                sidebar->setWidth(160);
            } else {
                sidebar->setWidth(sidebarWidth);
            }
        }

        // Add library tabs directly (no Home/Library intermediate screens)
        // Sort by type: Audiobooks first, then Podcasts
        std::vector<Library> bookLibs, podcastLibs;
        for (const auto& lib : sections) {
            if (lib.mediaType == "book") {
                bookLibs.push_back(lib);
            } else if (lib.mediaType == "podcast") {
                podcastLibs.push_back(lib);
            }
        }

        // Add Audiobooks libraries
        for (const auto& lib : bookLibs) {
            std::string id = lib.id;
            std::string name = lib.name;
            tabFrame->addTab(name, [id, name]() {
                return new LibrarySectionTab(id, name, "book");
            });
            brls::Logger::debug("MainActivity: Added audiobook library tab: {}", name);
        }

        // Add Podcast libraries
        for (const auto& lib : podcastLibs) {
            std::string id = lib.id;
            std::string name = lib.name;
            tabFrame->addTab(name, [id, name]() {
                return new LibrarySectionTab(id, name, "podcast");
            });
            brls::Logger::debug("MainActivity: Added podcast library tab: {}", name);
        }

        // Utility tabs (no separators)
        tabFrame->addTab("Search", []() { return new SearchTab(); });
        tabFrame->addTab("Downloads", []() { return new DownloadsTab(); });
        tabFrame->addTab("Settings", []() { return new SettingsTab(); });

        // Focus first tab
        tabFrame->focusTab(0);
    }
}

} // namespace vitaabs
