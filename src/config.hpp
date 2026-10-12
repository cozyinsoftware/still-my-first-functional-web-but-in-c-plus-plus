#pragma once

#include <string>
#include <cstdint>
#include <cstdlib>

namespace config {

inline std::string get_env(const char* name, const std::string& default_val) {
    const char* val = std::getenv(name);
    return val ? std::string(val) : default_val;
}

inline int get_env_int(const char* name, int default_val) {
    const char* val = std::getenv(name);
    return val ? std::atoi(val) : default_val;
}

inline int64_t get_env_int64(const char* name, int64_t default_val) {
    const char* val = std::getenv(name);
    return val ? std::atoll(val) : default_val;
}

struct AppConfig {
    int port = 5000;
    std::string secret_key = "change-this-secret-key-before-production";
    std::string db_path = "instance/app.db";
    std::string static_dir = "static";
    std::string templates_dir = "templates";
    std::string video_upload_dir = "instance/video_uploads";
    std::string video_output_dir = "instance/video_outputs";

    int64_t video_max_bytes = 7LL * 1024 * 1024 * 1024; // 7 GB
    int video_ffmpeg_timeout = 7200; // 2 hours
    int video_cleanup_seconds = 1800; // 30 minutes

    static AppConfig from_env() {
        AppConfig cfg;
        cfg.port = get_env_int("PORT", 5000);
        cfg.secret_key = get_env("SECRET_KEY", "change-this-secret-key-before-production");
        cfg.db_path = get_env("DATABASE", "instance/app.db");
        cfg.static_dir = get_env("STATIC_DIR", "static");
        cfg.templates_dir = get_env("TEMPLATES_DIR", "templates");
        cfg.video_upload_dir = get_env("VIDEO_UPLOAD_DIR", "instance/video_uploads");
        cfg.video_output_dir = get_env("VIDEO_OUTPUT_DIR", "instance/video_outputs");
        cfg.video_max_bytes = get_env_int64("VIDEO_MAX_BYTES", 7LL * 1024 * 1024 * 1024);
        cfg.video_ffmpeg_timeout = get_env_int("VIDEO_FFMPEG_TIMEOUT", 7200);
        cfg.video_cleanup_seconds = get_env_int("VIDEO_CLEANUP_SECONDS", 1800);
        return cfg;
    }
};

} // namespace config
