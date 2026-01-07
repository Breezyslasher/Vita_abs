/**
 * VitaABS - JWT Authentication implementation
 * Simple token storage and validation for Audiobookshelf
 */

#include "utils/jwt_auth.hpp"
#include <borealis.hpp>
#include <cstring>
#include <ctime>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/rtc.h>
#else
#include <fstream>
#endif

namespace vitaabs {

JwtAuth& JwtAuth::getInstance() {
    static JwtAuth instance;
    return instance;
}

bool JwtAuth::initialize() {
    if (m_initialized) return true;

    // Create auth directory
#ifdef __vita__
    sceIoMkdir("ux0:data/VitaABS", 0777);
    sceIoMkdir("ux0:data/VitaABS/auth", 0777);
#endif

    // Try to load existing token
    m_storedToken = loadToken();

    m_initialized = true;
    brls::Logger::info("JwtAuth: Initialized");
    return true;
}

bool JwtAuth::storeToken(const std::string& token) {
    m_storedToken = token;

#ifdef __vita__
    const char* tokenPath = "ux0:data/VitaABS/auth/token";

    SceUID fd = sceIoOpen(tokenPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0600);
    if (fd < 0) {
        brls::Logger::error("JwtAuth: Failed to save token");
        return false;
    }

    sceIoWrite(fd, token.c_str(), token.length());
    sceIoClose(fd);
#else
    std::ofstream file("vitaabs_token", std::ios::binary);
    if (!file) {
        brls::Logger::error("JwtAuth: Failed to save token");
        return false;
    }
    file << token;
    file.close();
#endif

    brls::Logger::debug("JwtAuth: Token stored");
    return true;
}

std::string JwtAuth::loadToken() {
    std::string token;

#ifdef __vita__
    const char* tokenPath = "ux0:data/VitaABS/auth/token";

    SceUID fd = sceIoOpen(tokenPath, SCE_O_RDONLY, 0);
    if (fd < 0) {
        return "";
    }

    char buffer[4096];
    int read = sceIoRead(fd, buffer, sizeof(buffer) - 1);
    sceIoClose(fd);

    if (read > 0) {
        buffer[read] = '\0';
        token = buffer;
    }
#else
    std::ifstream file("vitaabs_token", std::ios::binary);
    if (file) {
        std::getline(file, token);
        file.close();
    }
#endif

    return token;
}

void JwtAuth::clearToken() {
    m_storedToken.clear();

#ifdef __vita__
    sceIoRemove("ux0:data/VitaABS/auth/token");
#else
    std::remove("vitaabs_token");
#endif

    brls::Logger::debug("JwtAuth: Token cleared");
}

bool JwtAuth::hasToken() const {
    return !m_storedToken.empty();
}

std::string JwtAuth::decodePayload(const std::string& token) const {
    // JWT format: header.payload.signature
    // Find the payload part

    size_t firstDot = token.find('.');
    if (firstDot == std::string::npos) return "";

    size_t secondDot = token.find('.', firstDot + 1);
    if (secondDot == std::string::npos) return "";

    std::string payload = token.substr(firstDot + 1, secondDot - firstDot - 1);

    // Base64url decode the payload
    return base64UrlDecode(payload);
}

bool JwtAuth::isTokenExpired(const std::string& token) const {
    std::string payload = decodePayload(token);
    if (payload.empty()) return true;

    // Simple parsing - look for "exp":number
    size_t expPos = payload.find("\"exp\"");
    if (expPos == std::string::npos) return false;  // No expiry = not expired

    size_t colonPos = payload.find(':', expPos);
    if (colonPos == std::string::npos) return true;

    // Extract the number
    size_t numStart = colonPos + 1;
    while (numStart < payload.length() && (payload[numStart] == ' ' || payload[numStart] == '\t')) {
        numStart++;
    }

    size_t numEnd = numStart;
    while (numEnd < payload.length() && payload[numEnd] >= '0' && payload[numEnd] <= '9') {
        numEnd++;
    }

    if (numEnd == numStart) return true;

    std::string expStr = payload.substr(numStart, numEnd - numStart);
    int64_t exp = std::stoll(expStr);
    int64_t now = getCurrentTimestamp();

    return now >= exp;
}

std::string JwtAuth::base64UrlDecode(const std::string& input) const {
    // Base64url uses - and _ instead of + and /
    std::string b64 = input;
    for (char& c : b64) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }

    // Add padding if needed
    while (b64.length() % 4 != 0) {
        b64 += '=';
    }

    // Decode base64
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string result;
    result.reserve((b64.length() / 4) * 3);

    for (size_t i = 0; i < b64.length(); i += 4) {
        int n = 0;
        int padding = 0;

        for (int j = 0; j < 4; j++) {
            n <<= 6;
            if (b64[i + j] == '=') {
                padding++;
            } else {
                size_t pos = chars.find(b64[i + j]);
                if (pos != std::string::npos) {
                    n |= (int)pos;
                }
            }
        }

        if (padding < 3) result += (char)((n >> 16) & 0xFF);
        if (padding < 2) result += (char)((n >> 8) & 0xFF);
        if (padding < 1) result += (char)(n & 0xFF);
    }

    return result;
}

int64_t JwtAuth::getCurrentTimestamp() const {
#ifdef __vita__
    SceDateTime time;
    sceRtcGetCurrentClockUtc(&time);

    // Convert to Unix timestamp
    struct tm t = {};
    t.tm_year = time.year - 1900;
    t.tm_mon = time.month - 1;
    t.tm_mday = time.day;
    t.tm_hour = time.hour;
    t.tm_min = time.minute;
    t.tm_sec = time.second;

    return (int64_t)mktime(&t);
#else
    return (int64_t)std::time(nullptr);
#endif
}

}  // namespace vitaabs
