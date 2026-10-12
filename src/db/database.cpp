#include "database.hpp"
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace db {

namespace {

std::string get_utc_now_iso() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm gm{};
#if defined(_WIN32)
    gmtime_s(&gm, &tt);
#else
    gmtime_r(&tt, &gm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &gm);
    return std::string(buf);
}

double get_time_now() {
    using namespace std::chrono;
    auto now = system_clock::now();
    return duration_cast<duration<double>>(now.time_since_epoch()).count();
}

} // namespace

Database::Database(const std::string& db_path) : db_path_(db_path) {}

Database::~Database() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::execute_script(const char* sql) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) {
            std::cerr << "SQLite error: " << err_msg << std::endl;
            sqlite3_free(err_msg);
        }
        return false;
    }
    return true;
}

bool Database::init() {
    std::lock_guard<std::mutex> lock(mutex_);
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    // Enable WAL mode for high concurrency
    execute_script("PRAGMA journal_mode = WAL;");

    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS links (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            code TEXT UNIQUE NOT NULL,
            long_url TEXT NOT NULL,
            clicks INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS reports (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            code TEXT NOT NULL,
            reason TEXT,
            reported_at TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS rate_limits (
            rate_key TEXT PRIMARY KEY,
            count INTEGER NOT NULL,
            reset_at REAL NOT NULL
        );
    )";

    return execute_script(schema);
}

std::optional<std::string> Database::get_code_by_url(const std::string& long_url) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "SELECT code FROM links WHERE long_url = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, long_url.c_str(), -1, SQLITE_STATIC);

    std::optional<std::string> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* val = sqlite3_column_text(stmt, 0);
        if (val) {
            result = reinterpret_cast<const char*>(val);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<LinkRecord> Database::get_link_by_code(const std::string& code) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "SELECT id, code, long_url, clicks, created_at FROM links WHERE code = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, code.c_str(), -1, SQLITE_STATIC);

    std::optional<LinkRecord> record;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        LinkRecord r;
        r.id = sqlite3_column_int(stmt, 0);
        r.code = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        r.long_url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        r.clicks = sqlite3_column_int(stmt, 3);
        r.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        record = r;
    }
    sqlite3_finalize(stmt);
    return record;
}

bool Database::insert_link(const std::string& code, const std::string& long_url) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "INSERT INTO links (code, long_url, clicks, created_at) VALUES (?, ?, 0, ?);";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    std::string now_iso = get_utc_now_iso();
    sqlite3_bind_text(stmt, 1, code.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, long_url.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, now_iso.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool Database::increment_clicks(const std::string& code) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "UPDATE links SET clicks = clicks + 1 WHERE code = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, code.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool Database::insert_report(const std::string& code, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "INSERT INTO reports (code, reason, reported_at) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    std::string now_iso = get_utc_now_iso();
    sqlite3_bind_text(stmt, 1, code.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, reason.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, now_iso.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool Database::check_rate_limit(const std::string& rate_key, int limit, int period_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    double now = get_time_now();

    const char* select_sql = "SELECT count, reset_at FROM rate_limits WHERE rate_key = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, select_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return true; // fail open on db prepare error
    }

    sqlite3_bind_text(stmt, 1, rate_key.c_str(), -1, SQLITE_STATIC);

    int count = 0;
    double reset_at = 0.0;
    bool exists = false;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = true;
        count = sqlite3_column_int(stmt, 0);
        reset_at = sqlite3_column_double(stmt, 1);
    }
    sqlite3_finalize(stmt);

    if (!exists || now >= reset_at) {
        const char* upsert_sql = R"(
            INSERT INTO rate_limits (rate_key, count, reset_at)
            VALUES (?, 1, ?)
            ON CONFLICT(rate_key)
            DO UPDATE SET count = 1, reset_at = excluded.reset_at;
        )";
        sqlite3_stmt* up_stmt = nullptr;
        if (sqlite3_prepare_v2(db_, upsert_sql, -1, &up_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(up_stmt, 1, rate_key.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_double(up_stmt, 2, now + period_seconds);
            sqlite3_step(up_stmt);
            sqlite3_finalize(up_stmt);
        }
        return true;
    }

    if (count >= limit) {
        return false;
    }

    const char* inc_sql = "UPDATE rate_limits SET count = count + 1 WHERE rate_key = ?;";
    sqlite3_stmt* inc_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, inc_sql, -1, &inc_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(inc_stmt, 1, rate_key.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(inc_stmt);
        sqlite3_finalize(inc_stmt);
    }
    return true;
}

std::string Database::generate_unique_code() {
    static const char ALPHANUM[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(ALPHANUM) - 2);

    while (true) {
        std::string code;
        for (int i = 0; i < 7; ++i) {
            code.push_back(ALPHANUM[dis(gen)]);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        const char* check_sql = "SELECT 1 FROM links WHERE code = ? LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, check_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, code.c_str(), -1, SQLITE_STATIC);
            bool found = (sqlite3_step(stmt) == SQLITE_ROW);
            sqlite3_finalize(stmt);
            if (!found) {
                return code;
            }
        } else {
            return code;
        }
    }
}

} // namespace db
