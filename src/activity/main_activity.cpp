/**
 * VitaABS - Main Activity implementation
 */

#include "activity/main_activity.hpp"
#include "view/home_tab.hpp"
#include "view/library_tab.hpp"
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

// Cached libraries for sidebar mode
static std::vector<Library> s_cachedLibraries;

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

        // Calculate dynamic sidebar width based on content
        int sidebarWidth = 200;

        std::vector<std::string> standardTabs = {"Home", "Library", "Search", "Downloads", "Settings"};
        for (const auto& tab : standardTabs) {
            sidebarWidth = std::max(sidebarWidth, calculateTextWidth(tab));
        }

        // If showing libraries in sidebar, check library names too
        if (settings.showLibrariesInSidebar) {
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            std::vector<Library> libraries;
            if (client.fetchLibraries(libraries)) {
                s_cachedLibraries = libraries;
                for (const auto& lib : libraries) {
                    sidebarWidth = std::max(sidebarWidth, calculateTextWidth(lib.name));
                }
            }
        }

        // Apply sidebar width
        sidebarWidth = std::min(sidebarWidth, 350);
        brls::View* sidebar = tabFrame->getView("brls/tab_frame/sidebar");
        if (sidebar) {
            if (settings.collapseSidebar) {
                sidebar->setWidth(160);
                brls::Logger::debug("MainActivity: Collapsed sidebar to 160px");
            } else {
                sidebar->setWidth(sidebarWidth);
                brls::Logger::debug("MainActivity: Dynamic sidebar width: {}px", sidebarWidth);
            }
        }

        // If showing libraries in sidebar
        if (settings.showLibrariesInSidebar) {
            // Home tab
            tabFrame->addTab("Home", []() { return new HomeTab(); });

            // Load library sections to sidebar
            loadLibrariesToSidebar();

            // Search
            tabFrame->addTab("Search", []() { return new SearchTab(); });
        } else {
            // Standard mode with premade tabs
            tabFrame->addTab("Home", []() { return new HomeTab(); });
            tabFrame->addTab("Library", []() { return new LibraryTab(); });
            tabFrame->addTab("Search", []() { return new SearchTab(); });
        }

        // Downloads tab (always available)
        tabFrame->addTab("Downloads", []() { return new DownloadsTab(); });

        // Settings always at the bottom
        tabFrame->addSeparator();
        tabFrame->addTab("Settings", []() { return new SettingsTab(); });

        // Focus first tab
        tabFrame->focusTab(0);
    }
}

void MainActivity::loadLibrariesToSidebar() {
    brls::Logger::debug("MainActivity: Loading libraries to sidebar...");

    tabFrame->addSeparator();

    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
    std::vector<Library> libraries;

    if (client.fetchLibraries(libraries)) {
        brls::Logger::info("MainActivity: Got {} libraries", libraries.size());

        std::string hiddenLibraries = Application::getInstance().getSettings().hiddenLibraries;

        auto isHidden = [&hiddenLibraries](const std::string& id) -> bool {
            if (hiddenLibraries.empty()) return false;
            std::string hidden = hiddenLibraries;
            size_t pos = 0;
            while ((pos = hidden.find(',')) != std::string::npos) {
                if (hidden.substr(0, pos) == id) return true;
                hidden.erase(0, pos + 1);
            }
            return (hidden == id);
        };

        for (const auto& lib : libraries) {
            if (isHidden(lib.id)) {
                brls::Logger::debug("MainActivity: Hiding library: {}", lib.name);
                continue;
            }

            std::string id = lib.id;
            std::string name = lib.name;

            tabFrame->addTab(name, [id, name]() {
                return new LibrarySectionTab(id, name);
            });

            brls::Logger::debug("MainActivity: Added sidebar tab for library: {}", name);
        }
    } else {
        brls::Logger::error("MainActivity: Failed to fetch libraries");
    }
}

} // namespace vitaabs
