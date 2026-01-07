/**
 * VitaABS - JWT Authentication utilities
 * Implements token handling for Audiobookshelf authentication
 */

#pragma once

#include <string>
#include <cstdint>

namespace vitaabs {

/**
 * JWT Token Manager
 * Handles token storage and validation for Audiobookshelf
 */
class JwtAuth {
public:
    static JwtAuth& getInstance();

    /**
     * Initialize token storage
     * Tokens are stored in ux0:data/VitaABS/auth/
     */
    bool initialize();

    /**
     * Store authentication token
     */
    bool storeToken(const std::string& token);

    /**
     * Load token from storage
     */
    std::string loadToken();

    /**
     * Clear stored token
     */
    void clearToken();

    /**
     * Check if we have a stored token
     */
    bool hasToken() const;

    /**
     * Decode JWT payload (base64 decode, no verification)
     * Returns the payload JSON string
     */
    std::string decodePayload(const std::string& token) const;

    /**
     * Check if token is expired (based on exp claim)
     */
    bool isTokenExpired(const std::string& token) const;

private:
    JwtAuth() = default;

    bool m_initialized = false;
    std::string m_storedToken;

    // Helper functions
    std::string base64UrlDecode(const std::string& input) const;
    int64_t getCurrentTimestamp() const;
};

}  // namespace vitaabs
