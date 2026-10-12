#include "token.hpp"
#include "sha256.hpp"
#include "../utils/base64.hpp"

#include <chrono>
#include <sstream>

namespace crypto {

namespace {

double current_timestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    return duration_cast<duration<double>>(now.time_since_epoch()).count();
}

bool constant_time_equals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char result = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        result |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return result == 0;
}

} // namespace

std::string TokenManager::signing_key() const {
    return sha256_raw(secret_key_ + ":" + salt_);
}

std::string TokenManager::create_wait_token(const std::string& code) const {
    double now = current_timestamp();
    std::string payload = code + ":" + std::to_string(now);
    std::string key = signing_key();
    std::string sig = hmac_sha256_raw(key, payload);

    return utils::base64_url_encode(payload) + "." + utils::base64_url_encode(sig);
}

std::optional<TokenData> TokenManager::verify_wait_token(
    const std::string& token,
    bool require_wait,
    std::string& error_msg
) const {
    size_t dot_pos = token.find('.');
    if (dot_pos == std::string::npos) {
        error_msg = "Invalid waiting token.";
        return std::nullopt;
    }

    std::string enc_payload = token.substr(0, dot_pos);
    std::string enc_sig = token.substr(dot_pos + 1);

    std::string payload = utils::base64_url_decode(enc_payload);
    std::string sig = utils::base64_url_decode(enc_sig);

    std::string expected_sig = hmac_sha256_raw(signing_key(), payload);
    if (!constant_time_equals(sig, expected_sig)) {
        error_msg = "Invalid waiting token.";
        return std::nullopt;
    }

    size_t colon_pos = payload.find(':');
    if (colon_pos == std::string::npos) {
        error_msg = "Invalid waiting token.";
        return std::nullopt;
    }

    std::string code = payload.substr(0, colon_pos);
    double issued_at = 0.0;
    try {
        issued_at = std::stod(payload.substr(colon_pos + 1));
    } catch (...) {
        error_msg = "Invalid waiting token.";
        return std::nullopt;
    }

    double now = current_timestamp();
    if (now - issued_at > 600.0) {
        error_msg = "This waiting link has expired. Please open the short link again.";
        return std::nullopt;
    }

    if (require_wait && (now - issued_at < 5.0)) {
        error_msg = "Please wait for the countdown to finish.";
        return std::nullopt;
    }

    return TokenData{code, issued_at};
}

} // namespace crypto
