#pragma once

#include <string>
#include <optional>
#include <cstdint>

namespace crypto {

struct TokenData {
    std::string code;
    double issued_at = 0.0;
};

class TokenManager {
public:
    explicit TokenManager(std::string secret_key, std::string salt = "short-link-wait")
        : secret_key_(std::move(secret_key)), salt_(std::move(salt)) {}

    std::string create_wait_token(const std::string& code) const;

    // Verifies token. Returns TokenData if valid, or std::nullopt and populates error_msg.
    std::optional<TokenData> verify_wait_token(
        const std::string& token,
        bool require_wait,
        std::string& error_msg
    ) const;

private:
    std::string secret_key_;
    std::string salt_;
    std::string signing_key() const;
};

} // namespace crypto
