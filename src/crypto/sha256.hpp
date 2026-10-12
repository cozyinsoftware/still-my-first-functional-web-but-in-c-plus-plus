#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace crypto {

std::string sha256_raw(const std::string& data);
std::string sha256_hex(const std::string& data);

std::string hmac_sha256_raw(const std::string& key, const std::string& data);
std::string hmac_sha256_hex(const std::string& key, const std::string& data);

} // namespace crypto
