#pragma once

#include <string>
#include <algorithm>
#include <cctype>

namespace utils {

inline std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

inline bool is_safe_http_url(const std::string& raw) {
    std::string s = trim(raw);
    if (s.rfind("http://", 0) != 0 && s.rfind("https://", 0) != 0) {
        return false;
    }
    size_t scheme_pos = s.find("://");
    if (scheme_pos == std::string::npos) return false;

    std::string rest = s.substr(scheme_pos + 3);
    if (rest.empty()) return false;

    size_t host_end = rest.find_first_of("/?#:");
    std::string host = (host_end == std::string::npos) ? rest : rest.substr(0, host_end);
    return !host.empty() && host.find('.') != std::string::npos;
}

inline std::string normalize_url(const std::string& raw) {
    std::string val = trim(raw);
    if (val.empty()) return "";

    if (val.rfind("http://", 0) != 0 && val.rfind("https://", 0) != 0) {
        val = "https://" + val;
    }

    if (!is_safe_http_url(val)) {
        return "";
    }
    return val;
}

inline std::string extract_host(const std::string& url) {
    size_t pos = url.find("://");
    if (pos == std::string::npos) return "";
    std::string rest = url.substr(pos + 3);
    size_t end = rest.find_first_of("/?#:");
    return to_lower((end == std::string::npos) ? rest : rest.substr(0, end));
}

inline bool points_to_this_site(const std::string& url, const std::string& request_host, const std::string& configured_host = "") {
    std::string target_host = extract_host(url);
    if (target_host.empty()) return false;

    std::string req_h = to_lower(request_host);
    size_t colon = req_h.find(':');
    if (colon != std::string::npos) req_h = req_h.substr(0, colon);

    if (target_host == req_h) return true;
    if (!configured_host.empty() && target_host == to_lower(configured_host)) return true;

    return false;
}

} // namespace utils
