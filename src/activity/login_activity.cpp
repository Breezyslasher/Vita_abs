/**
 * VitaABS - Login Activity implementation
 * Handles user authentication for Audiobookshelf server
 */

#include "activity/login_activity.hpp"
#include "app/application.hpp"
#include "app/audiobookshelf_client.hpp"
#include "view/progress_dialog.hpp"
#include "utils/async.hpp"

#include <memory>

namespace vitaabs {

LoginActivity::LoginActivity() {
    brls::Logger::debug("LoginActivity created");
}

brls::View* LoginActivity::createContentView() {
    return brls::View::createFromXMLResource("activity/login.xml");
}

void LoginActivity::onContentAvailable() {
    brls::Logger::debug("LoginActivity content available");

    // Set initial values
    if (titleLabel) {
        titleLabel->setText("VitaABS");
    }

    if (statusLabel) {
        statusLabel->setText("Enter your Audiobookshelf server URL and credentials");
    }

    if (pinCodeLabel) {
        pinCodeLabel->setVisibility(brls::Visibility::GONE);
    }

    // Server URL input
    if (serverLabel) {
        serverLabel->setText(std::string("Server: ") + (m_serverUrl.empty() ? "Not set" : m_serverUrl));
        serverLabel->registerClickAction([this](brls::View* view) {
            brls::Application::getImeManager()->openForText([this](std::string text) {
                m_serverUrl = text;
                serverLabel->setText(std::string("Server: ") + text);
            }, "Enter Server URL", "http://your-server:13378", 256, m_serverUrl);
            return true;
        });
        serverLabel->addGestureRecognizer(new brls::TapGestureRecognizer(serverLabel));
    }

    // Username input
    if (usernameLabel) {
        usernameLabel->setText(std::string("Username: ") + (m_username.empty() ? "Not set" : m_username));
        usernameLabel->registerClickAction([this](brls::View* view) {
            brls::Application::getImeManager()->openForText([this](std::string text) {
                m_username = text;
                usernameLabel->setText(std::string("Username: ") + text);
            }, "Enter Username", "", 128, m_username);
            return true;
        });
        usernameLabel->addGestureRecognizer(new brls::TapGestureRecognizer(usernameLabel));
    }

    // Password input
    if (passwordLabel) {
        passwordLabel->setText(std::string("Password: ") + (m_password.empty() ? "Not set" : "********"));
        passwordLabel->registerClickAction([this](brls::View* view) {
            brls::Application::getImeManager()->openForPassword([this](std::string text) {
                m_password = text;
                passwordLabel->setText("Password: ********");
            }, "Enter Password", "", 128, "");
            return true;
        });
        passwordLabel->addGestureRecognizer(new brls::TapGestureRecognizer(passwordLabel));
    }

    // Login button
    if (loginButton) {
        loginButton->setText("Login");
        loginButton->registerClickAction([this](brls::View* view) {
            onLoginPressed();
            return true;
        });
    }

    // Test connection button (repurpose PIN button)
    if (pinButton) {
        pinButton->setText("Test Connection");
        pinButton->registerClickAction([this](brls::View* view) {
            onTestConnectionPressed();
            return true;
        });
    }
}

void LoginActivity::onTestConnectionPressed() {
    if (m_serverUrl.empty()) {
        if (statusLabel) statusLabel->setText("Please enter server URL first");
        return;
    }

    if (statusLabel) statusLabel->setText("Testing connection...");

    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    if (client.pingServer(m_serverUrl)) {
        if (statusLabel) statusLabel->setText("Server is reachable!");
    } else {
        if (statusLabel) statusLabel->setText("Cannot reach server - check URL");
    }
}

void LoginActivity::onLoginPressed() {
    if (m_serverUrl.empty()) {
        if (statusLabel) statusLabel->setText("Please enter server URL");
        return;
    }

    if (m_username.empty() || m_password.empty()) {
        if (statusLabel) statusLabel->setText("Please enter username and password");
        return;
    }

    if (statusLabel) statusLabel->setText("Logging in...");

    // Perform login
    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    if (client.login(m_serverUrl, m_username, m_password)) {
        // Save credentials
        Application::getInstance().setUsername(m_username);
        Application::getInstance().setServerUrl(m_serverUrl);
        Application::getInstance().setAuthToken(client.getToken());
        Application::getInstance().saveSettings();

        if (statusLabel) statusLabel->setText("Login successful!");

        brls::sync([this]() {
            Application::getInstance().pushMainActivity();
        });
    } else {
        if (statusLabel) statusLabel->setText("Login failed - check credentials");
    }
}

} // namespace vitaabs
