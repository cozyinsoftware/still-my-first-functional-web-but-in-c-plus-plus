#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace utils {

std::string base64_encode(const unsigned char* data, size_t len);
std::string base64_encode(const std::string& input);
std::string base64_decode(const std::string& input);

// URL-safe base64 (replaces '+' with '-', '/' with '_', removes '=')
std::string base64_url_encode(const unsigned char* data, size_t len);
std::string base64_url_encode(const std::string& input);
std::string base64_url_decode(const std::string& input);

} // namespace utils
