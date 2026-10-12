#pragma once

#include <string>
#include <optional>
#include <mutex>
#include <memory>
#include <sqlite3.h>

namespace db {

struct LinkRecord {
    int id;
    std::string code;
    std::string long_url;
    int clicks;
    std::string created_at;
};

class Database {
public:
    explicit Database(const std::string& db_path);
    ~Database();

    // Disallow copy
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool init();

    std::optional<std::string> get_code_by_url(const std::string& long_url);
    std::optional<LinkRecord> get_link_by_code(const std::string& code);
    bool insert_link(const std::string& code, const std::string& long_url);
    bool increment_clicks(const std::string& code);
    bool insert_report(const std::string& code, const std::string& reason);

    // Rate limiting: returns true if allowed, false if limit exceeded
    bool check_rate_limit(const std::string& rate_key, int limit, int period_seconds);

    std::string generate_unique_code();

private:
    std::string db_path_;
    sqlite3* db_ = nullptr;
    std::mutex mutex_;

    bool execute_script(const char* sql);
};

} // namespace db
