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

// Cached library sections for sidebar mode
static std::vector<Library> s_cachedSections;

// Helper to calculate text width (approximate based on character count)
// Average character width at default font size is about 8-10 pixels
static int calculateTextWidth(const std::string& text) {
    // Base width per character (approximate for sidebar font size 22)
    const int charWidth = 12;
    // Add minimal padding for accent bar and margins (sidebar padding is now 20+20=40)
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
        int sidebarWidth = 200;  // Minimum width

        // Standard tab names to consider
        std::vector<std::string> standardTabs = {"Home", "Library", "Search", "Downloads", "Settings"};
        for (const auto& tab : standardTabs) {
            sidebarWidth = std::max(sidebarWidth, calculateTextWidth(tab));
        }

        // If showing libraries in sidebar, check library names too
        if (settings.showLibrariesInSidebar) {
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            std::vector<Library> sections;
            if (client.fetchLibraries(sections)) {
                s_cachedSections = sections;  // Cache for later use
                for (const auto& section : sections) {
                    sidebarWidth = std::max(sidebarWidth, calculateTextWidth(section.name));
                }
            }
        }

        // Apply sidebar width (with reasonable bounds)
        sidebarWidth = std::min(sidebarWidth, 350);  // Max width
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

        // If showing libraries in sidebar, only show actual library sections
        // Don't show premade tabs like "Library", "Music"
        if (settings.showLibrariesInSidebar) {
            // Home tab
            tabFrame->addTab("Home", []() { return new HomeTab(); });

            // Load actual library sections to sidebar
            loadLibrariesToSidebar();

            // Search
            tabFrame->addTab("Search", []() { return new SearchTab(); });
        } else {
            // Standard mode with premade tabs for Audiobookshelf
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

    // Add separator before libraries
    tabFrame->addSeparator();

    // Fetch libraries synchronously to maintain correct sidebar order
    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
    std::vector<Library> sections;

    if (client.fetchLibraries(sections)) {
        brls::Logger::info("MainActivity: Got {} library sections", sections.size());

        // Get hidden libraries setting
        std::string hiddenLibraries = Application::getInstance().getSettings().hiddenLibraries;

        // Helper to check if hidden
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

        // Add library tabs
        for (const auto& section : sections) {
            if (isHidden(section.id)) {
                brls::Logger::debug("MainActivity: Hiding library: {}", section.name);
                continue;
            }

            std::string id = section.id;
            std::string name = section.name;

            tabFrame->addTab(name, [id, name]() {
                return new LibrarySectionTab(id, name);
            });

            brls::Logger::debug("MainActivity: Added sidebar tab for library: {}", name);
        }
    } else {
        brls::Logger::error("MainActivity: Failed to fetch library sections");
    }
}

} // namespace vitaabs
