#include "base64.hpp"
#include <algorithm>

namespace utils {

static const char B64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i < len) {
        uint32_t octet_a = i < len ? data[i++] : 0;
        uint32_t octet_b = i < len ? data[i++] : 0;
        uint32_t octet_c = i < len ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        out.push_back(B64_CHARS[(triple >> 18) & 0x3F]);
        out.push_back(B64_CHARS[(triple >> 12) & 0x3F]);
        out.push_back((i > len + 1) ? '=' : B64_CHARS[(triple >> 6) & 0x3F]);
        out.push_back((i > len) ? '=' : B64_CHARS[triple & 0x3F]);
    }
    return out;
}

std::string base64_encode(const std::string& input) {
    return base64_encode(reinterpret_cast<const unsigned char*>(input.data()), input.size());
}

std::string base64_decode(const std::string& input) {
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[static_cast<unsigned char>(B64_CHARS[i])] = i;

    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (T[c] == -1) continue; // skip non-base64 characters
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string base64_url_encode(const unsigned char* data, size_t len) {
    std::string s = base64_encode(data, len);
    for (char& c : s) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    s.erase(std::remove(s.begin(), s.end(), '='), s.end());
    return s;
}

std::string base64_url_encode(const std::string& input) {
    return base64_url_encode(reinterpret_cast<const unsigned char*>(input.data()), input.size());
}

std::string base64_url_decode(const std::string& input) {
    std::string s = input;
    for (char& c : s) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (s.size() % 4 != 0) {
        s.push_back('=');
    }
    return base64_decode(s);
}

} // namespace utils
