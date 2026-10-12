#include "config.hpp"
#include "db/database.hpp"
#include "crypto/token.hpp"
#include "qr/qr_service.hpp"
#include "video/ffmpeg_runner.hpp"
#include "video/video_jobs.hpp"
#include "utils/url_validator.hpp"

#include <crow.h>
#include <crow/multipart.h>
#include <nlohmann/json.hpp>
#include <inja/inja.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// Helper to determine MIME type
std::string get_mime_type(const std::string& path) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = utils::to_lower(path.substr(dot + 1));
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "js") return "application/javascript; charset=utf-8";
    if (ext == "json") return "application/json";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    if (ext == "mp4") return "video/mp4";
    if (ext == "webm") return "video/webm";
    if (ext == "mov") return "video/quicktime";
    if (ext == "mkv") return "video/x-matroska";
    if (ext == "avi") return "video/x-msvideo";
    return "application/octet-stream";
}

std::string read_file_content(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string extract_block(const std::string& content, const std::string& block_name, const std::string& default_val = "") {
    std::regex re(R"(\{%\s*block\s+)" + block_name + R"(\s*%\}([\s\S]*?)\{%\s*endblock\s*%\})");
    std::smatch match;
    if (std::regex_search(content, match, re)) {
        return match[1].str();
    }
    return default_val;
}

std::string resolve_url_for(std::string text) {
    // Resolve {{ url_for('static', filename='...') }}
    std::regex static_re(R"(\{\{\s*url_for\(\s*['"]static['"]\s*,\s*filename=\s*['"]([^'"]+)['"]\s*\)\s*\}\})");
    text = std::regex_replace(text, static_re, "/static/$1");

    // Resolve simple page route url_for
    std::vector<std::pair<std::string, std::string>> routes = {
        {"scanner", "/"},
        {"image_to_text", "/image-to-text"},
        {"link_to_qr", "/link-to-qr"},
        {"link_shortener", "/link-shortener"},
        {"video_size_changer", "/video-size-changer"},
        {"about", "/about"},
        {"privacy", "/privacy-policy"},
        {"terms", "/terms"},
        {"contact", "/contact"}
    };

    for (const auto& [endpoint, path] : routes) {
        std::regex route_re(R"(\{\{\s*url_for\(\s*['"])" + endpoint + R"(['"]\s*\)\s*\}\})");
        text = std::regex_replace(text, route_re, path);
    }

    return text;
}

std::string render_template_page(
    const std::string& templates_dir,
    const std::string& filename,
    const json& data = json::object()
) {
    std::string child_path = (fs::path(templates_dir) / filename).string();
    std::string base_path = (fs::path(templates_dir) / "base.html").string();

    std::string child_content = read_file_content(child_path);
    if (child_content.empty()) return "Template not found: " + filename;

    std::string combined;
    if (child_content.find("{% extends \"base.html\" %}") != std::string::npos ||
        child_content.find("{% extends 'base.html' %}") != std::string::npos) {

        std::string base_content = read_file_content(base_path);
        std::string title = extract_block(child_content, "title", "Quick Tools");
        std::string content = extract_block(child_content, "content", "");
        std::string scripts = extract_block(child_content, "scripts", "");

        // Replace blocks in base.html
        std::regex title_re(R"(\{%\s*block\s+title\s*%\}([\s\S]*?)\{%\s*endblock\s*%\})");
        base_content = std::regex_replace(base_content, title_re, title);

        std::regex content_re(R"(\{%\s*block\s+content\s*%\}([\s\S]*?)\{%\s*endblock\s*%\})");
        base_content = std::regex_replace(base_content, content_re, content);

        std::regex scripts_re(R"(\{%\s*block\s+scripts\s*%\}([\s\S]*?)\{%\s*endblock\s*%\})");
        base_content = std::regex_replace(base_content, scripts_re, scripts);

        combined = base_content;
    } else {
        combined = child_content;
    }

    combined = resolve_url_for(combined);

    // Support {{ token|tojson }} by converting to {{ token }}
    std::regex tojson_re(R"(\{\{\s*([a-zA-Z0-9_]+)\s*\|\s*tojson\s*\}\})");
    combined = std::regex_replace(combined, tojson_re, "\"{{ $1 }}\"");

    try {
        inja::Environment env;
        return env.render(combined, data);
    } catch (const std::exception& e) {
        std::cerr << "Inja render error in " << filename << ": " << e.what() << std::endl;
        return combined; // fallback to unrendered template on parse edge case
    }
}

std::string get_client_ip(const crow::request& req) {
    std::string fwd = req.get_header_value("X-Forwarded-For");
    if (!fwd.empty()) {
        size_t comma = fwd.find(',');
        return (comma != std::string::npos) ? utils::trim(fwd.substr(0, comma)) : utils::trim(fwd);
    }
    return req.remote_ip_address.empty() ? "unknown" : req.remote_ip_address;
}

} // namespace

int main() {
    auto cfg = config::AppConfig::from_env();
    std::cout << "Starting Quick Tools C++ Server on port " << cfg.port << "..." << std::endl;

    fs::create_directories("instance");
    fs::create_directories(cfg.video_upload_dir);
    fs::create_directories(cfg.video_output_dir);

    db::Database db(cfg.db_path);
    if (!db.init()) {
        std::cerr << "Fatal: Failed to initialize SQLite database at " << cfg.db_path << std::endl;
        return 1;
    }

    crypto::TokenManager token_mgr(cfg.secret_key);
    video::VideoJobManager video_jobs(
        cfg.video_upload_dir,
        cfg.video_output_dir,
        cfg.video_ffmpeg_timeout,
        cfg.video_cleanup_seconds
    );

    crow::SimpleApp app;

    // -------------------------------------------------------------
    // HTML Pages
    // -------------------------------------------------------------
    CROW_ROUTE(app, "/")
    ([&cfg]() {
        return crow::response(200, "text/html", render_template_page(cfg.templates_dir, "scanner.html"));
    });

    CROW_ROUTE(app, "/image-to-text")
    ([&cfg]() {
        return crow::response(200, "text/html", render_template_page(cfg.templates_dir, "ocr.html"));
    });

    CROW_ROUTE(app, "/link-to-qr")
    ([&cfg]() {
        return crow::response(200, "text/html", render_template_page(cfg.templates_dir, "qr_generator.html"));
    });

    CROW_ROUTE(app, "/link-shortener")
    ([&cfg]() {
        return crow::response(200, "text/html", render_template_page(cfg.templates_dir, "shortener.html"));
    });

    CROW_ROUTE(app, "/video-size-changer")
    ([&cfg]() {
        auto info = video::FFmpegRunner::get_info();
        json data;
        data["ffmpeg_ready"] = info.available;
        data["ffmpeg_outdated"] = info.outdated;
        data["ffmpeg_version"] = info.version;
        return crow::response(200, "text/html", render_template_page(cfg.templates_dir, "video_size_changer.html", data));
    });

    CROW_ROUTE(app, "/about")
    ([&cfg]() {
        json data;
        data["title"] = "About";
        data["text"] = "This website provides simple browser tools for QR scanning, OCR, QR generation, and safe short links.";
        return crow::response(200, "text/html", render_template_page(cfg.templates_dir, "info.html", data));
    });

    CROW_ROUTE(app, "/privacy-policy")
    ([&cfg]() {
        json data;
        data["title"] = "Privacy Policy";
        data["text"] = "Uploaded images are handled in your browser for QR scanning and OCR. Short links and bad-link reports are saved in SQLite. Do not submit private information.";
        return crow::response(200, "text/html", render_template_page(cfg.templates_dir, "info.html", data));
    });

    CROW_ROUTE(app, "/terms")
    ([&cfg]() {
        json data;
        data["title"] = "Terms";
        data["text"] = "Use this website only for legal and safe links. Bad, harmful, misleading, or illegal links may be removed.";
        return crow::response(200, "text/html", render_template_page(cfg.templates_dir, "info.html", data));
    });

    CROW_ROUTE(app, "/contact")
    ([&cfg]() {
        json data;
        data["title"] = "Contact";
        data["text"] = "Add your email address or contact form details in this page before publishing the website.";
        return crow::response(200, "text/html", render_template_page(cfg.templates_dir, "info.html", data));
    });

    // -------------------------------------------------------------
    // API: QR Generator
    // -------------------------------------------------------------
    CROW_ROUTE(app, "/api/generate-qr").methods(crow::HTTPMethod::POST)
    ([&db](const crow::request& req) {
        std::string ip = get_client_ip(req);
        if (!db.check_rate_limit(ip + ":generate_qr", 30, 60)) {
            return crow::response(429, "application/json", "{\"error\":\"Too many requests. Please wait a minute.\"}");
        }

        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("url") || !body["url"].is_string()) {
            return crow::response(400, "application/json", "{\"error\":\"Please enter a valid link.\"}");
        }

        std::string url = utils::normalize_url(body["url"].get<std::string>());
        if (url.empty()) {
            return crow::response(400, "application/json", "{\"error\":\"Please enter a valid http:// or https:// link.\"}");
        }

        std::string image_data = qr::QrService::generate_qr_data_uri(url);
        json res = {
            {"url", url},
            {"image", image_data}
        };
        return crow::response(200, "application/json", res.dump());
    });

    // -------------------------------------------------------------
    // API: Link Shortener
    // -------------------------------------------------------------
    CROW_ROUTE(app, "/api/shorten").methods(crow::HTTPMethod::POST)
    ([&db](const crow::request& req) {
        std::string ip = get_client_ip(req);
        if (!db.check_rate_limit(ip + ":shorten", 15, 60)) {
            return crow::response(429, "application/json", "{\"error\":\"Too many requests. Please wait a minute.\"}");
        }

        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("url") || !body["url"].is_string()) {
            return crow::response(400, "application/json", "{\"error\":\"Please enter a valid link.\"}");
        }

        std::string long_url = utils::normalize_url(body["url"].get<std::string>());
        if (long_url.empty()) {
            return crow::response(400, "application/json", "{\"error\":\"Please enter a valid http:// or https:// link.\"}");
        }

        std::string req_host = req.get_header_value("Host");
        if (utils::points_to_this_site(long_url, req_host)) {
            return crow::response(400, "application/json", "{\"error\":\"You cannot shorten a link to this website.\"}");
        }

        auto existing = db.get_code_by_url(long_url);
        std::string code;
        if (existing) {
            code = *existing;
        } else {
            code = db.generate_unique_code();
            db.insert_link(code, long_url);
        }

        std::string base_url = "http://" + req_host;
        std::string short_url = base_url + "/s/" + code;

        json res = { {"short_url", short_url} };
        return crow::response(200, "application/json", res.dump());
    });

    // -------------------------------------------------------------
    // Short Link Ad Gate & Redirect
    // -------------------------------------------------------------
    CROW_ROUTE(app, "/s/<string>")
    ([&cfg, &db, &token_mgr](const std::string& code) {
        auto link = db.get_link_by_code(code);
        if (!link) {
            return crow::response(404, "text/html", render_template_page(cfg.templates_dir, "not_found.html"));
        }

        std::string token = token_mgr.create_wait_token(code);
        json data;
        data["code"] = code;
        data["token"] = token;
        return crow::response(200, "text/html", render_template_page(cfg.templates_dir, "ad_gate.html", data));
    });

    CROW_ROUTE(app, "/api/unlock").methods(crow::HTTPMethod::POST)
    ([&db, &token_mgr](const crow::request& req) {
        std::string ip = get_client_ip(req);
        if (!db.check_rate_limit(ip + ":unlock", 30, 60)) {
            return crow::response(429, "application/json", "{\"error\":\"Too many requests. Please wait a minute.\"}");
        }

        auto body = json::parse(req.body, nullptr, false);
        std::string token = (body.is_object() && body.contains("token")) ? body["token"].get<std::string>() : "";

        std::string err;
        auto token_data = token_mgr.verify_wait_token(token, true, err);
        if (!token_data) {
            json res = { {"error", err} };
            return crow::response(400, "application/json", res.dump());
        }

        auto link = db.get_link_by_code(token_data->code);
        if (!link) {
            json res = { {"error", "This short link does not exist."} };
            return crow::response(400, "application/json", res.dump());
        }

        std::string open_url = "/go/" + link->code + "?token=" + token;
        json res = { {"open_url", open_url} };
        return crow::response(200, "application/json", res.dump());
    });

    CROW_ROUTE(app, "/go/<string>")
    ([&cfg, &db, &token_mgr](const crow::request& req, const std::string& code) {
        std::string token = req.url_params.get("token") ? req.url_params.get("token") : "";

        std::string err;
        auto token_data = token_mgr.verify_wait_token(token, true, err);
        if (!token_data || token_data->code != code) {
            return crow::response(404, "text/html", render_template_page(cfg.templates_dir, "not_found.html"));
        }

        auto link = db.get_link_by_code(code);
        if (!link) {
            return crow::response(404, "text/html", render_template_page(cfg.templates_dir, "not_found.html"));
        }

        db.increment_clicks(code);

        crow::response res(302);
        res.set_header("Location", link->long_url);
        return res;
    });

    CROW_ROUTE(app, "/api/report").methods(crow::HTTPMethod::POST)
    ([&db, &token_mgr](const crow::request& req) {
        std::string ip = get_client_ip(req);
        if (!db.check_rate_limit(ip + ":report", 10, 60)) {
            return crow::response(429, "application/json", "{\"error\":\"Too many requests. Please wait a minute.\"}");
        }

        auto body = json::parse(req.body, nullptr, false);
        std::string token = (body.is_object() && body.contains("token")) ? body["token"].get<std::string>() : "";
        std::string reason = (body.is_object() && body.contains("reason")) ? body["reason"].get<std::string>() : "";

        std::string err;
        auto token_data = token_mgr.verify_wait_token(token, false, err);
        if (!token_data) {
            json res = { {"error", err} };
            return crow::response(400, "application/json", res.dump());
        }

        db.insert_report(token_data->code, reason);
        json res = { {"message", "Thanks. Your report was saved."} };
        return crow::response(200, "application/json", res.dump());
    });

    // -------------------------------------------------------------
    // API: Video Size Changer
    // -------------------------------------------------------------
    CROW_ROUTE(app, "/api/video/info")
    ([]() {
        auto info = video::FFmpegRunner::get_info();
        json res = {
            {"available", info.available},
            {"engine", info.available ? "FFmpeg" : "Not found"},
            {"path", info.path},
            {"version", info.version},
            {"outdated", info.outdated}
        };
        return crow::response(200, "application/json", res.dump());
    });

    CROW_ROUTE(app, "/api/video/estimate")
    ([](const crow::request& req) {
        int64_t orig = 0;
        try {
            if (req.url_params.get("original_bytes")) {
                orig = std::stoll(req.url_params.get("original_bytes"));
            }
        } catch (...) {}

        std::string quality = req.url_params.get("quality") ? req.url_params.get("quality") : "medium";
        std::string resolution = req.url_params.get("resolution") ? req.url_params.get("resolution") : "source";
        std::string vbitrate = req.url_params.get("video_bitrate") ? req.url_params.get("video_bitrate") : "";
        std::string abitrate = req.url_params.get("audio_bitrate") ? req.url_params.get("audio_bitrate") : "";

        double dur = 0.0;
        try {
            if (req.url_params.get("duration_sec")) {
                dur = std::stod(req.url_params.get("duration_sec"));
            }
        } catch (...) {}

        int64_t est = video::FFmpegRunner::estimate_size(orig, quality, resolution, vbitrate, abitrate, dur);
        json res = { {"estimated_bytes", est} };
        return crow::response(200, "application/json", res.dump());
    });

    CROW_ROUTE(app, "/api/video/convert").methods(crow::HTTPMethod::POST)
    ([&cfg, &db, &video_jobs](const crow::request& req) {
        std::string ip = get_client_ip(req);
        if (!db.check_rate_limit(ip + ":video_convert", 60, 60)) {
            return crow::response(429, "application/json", "{\"error\":\"Too many requests. Please wait a minute.\"}");
        }

        if (!video::FFmpegRunner::is_available()) {
            return crow::response(500, "application/json", "{\"error\":\"FFmpeg is not installed on the server.\"}");
        }

        crow::multipart::message msg(req);
        auto video_part = msg.get_part_by_name("video");
        if (video_part.body.empty()) {
            return crow::response(400, "application/json", "{\"error\":\"Please choose a video file first.\"}");
        }

        std::string filename;
        const auto& cd_params = video_part.get_header_object("content-disposition").params;
        auto fn_it = cd_params.find("filename");
        if (fn_it != cd_params.end()) {
            filename = fn_it->second;
        }

        if (filename.empty() || !video::FFmpegRunner::is_allowed_extension(filename)) {
            return crow::response(400, "application/json", "{\"error\":\"Unsupported file type. Please upload MP4, MOV, AVI, MKV, WebM, or MPEG.\"}");
        }

        if (static_cast<int64_t>(video_part.body.size()) > cfg.video_max_bytes) {
            return crow::response(400, "application/json", "{\"error\":\"File is too large.\"}");
        }

        std::string mode = "standard";
        auto mode_part = msg.get_part_by_name("mode");
        if (!mode_part.body.empty()) mode = mode_part.body;

        video::ValidationResult vres;
        if (mode == "standard") {
            std::string task = msg.get_part_by_name("task").body;
            std::string res = msg.get_part_by_name("resolution").body;
            std::string fps = msg.get_part_by_name("fps").body;
            std::string quality = msg.get_part_by_name("quality").body;
            vres = video::FFmpegRunner::validate_standard(task, res, fps, quality);
        } else if (mode == "professional") {
            vres = video::FFmpegRunner::validate_professional(
                msg.get_part_by_name("format").body,
                msg.get_part_by_name("video_codec").body,
                msg.get_part_by_name("audio_codec").body,
                msg.get_part_by_name("remove_audio").body == "1" || msg.get_part_by_name("remove_audio").body == "true",
                msg.get_part_by_name("width").body,
                msg.get_part_by_name("height").body,
                msg.get_part_by_name("fps").body,
                msg.get_part_by_name("video_bitrate").body,
                msg.get_part_by_name("audio_bitrate").body,
                msg.get_part_by_name("crf").body,
                msg.get_part_by_name("aspect").body,
                msg.get_part_by_name("rotation").body,
                msg.get_part_by_name("preset").body,
                msg.get_part_by_name("trim_start").body,
                msg.get_part_by_name("trim_end").body
            );
        } else {
            return crow::response(400, "application/json", "{\"error\":\"Invalid mode. Choose Standard or Professional.\"}");
        }

        if (!vres.success) {
            json res = { {"error", vres.error} };
            return crow::response(400, "application/json", res.dump());
        }

        std::string job_id = video_jobs.create_job_id();
        size_t dot = filename.rfind('.');
        std::string in_ext = (dot != std::string::npos) ? filename.substr(dot + 1) : "mp4";

        std::string input_path = (fs::path(cfg.video_upload_dir) / (job_id + "_in." + in_ext)).string();
        std::string output_path = video_jobs.get_output_filepath(job_id, vres.params.format);

        // Save uploaded bytes to disk
        std::ofstream out(input_path, std::ios::binary);
        if (!out.is_open()) {
            return crow::response(500, "application/json", "{\"error\":\"Could not save the upload. Please try again.\"}");
        }
        out.write(video_part.body.data(), video_part.body.size());
        out.close();

        video_jobs.enqueue_job(job_id, vres.params, input_path, output_path, vres.warnings, vres.params.format);

        json res = {
            {"job_id", job_id},
            {"status_url", "/api/video/status/" + job_id},
            {"message", "Upload received. Conversion started."}
        };
        return crow::response(202, "application/json", res.dump());
    });

    CROW_ROUTE(app, "/api/video/status/<string>")
    ([&video_jobs](const std::string& job_id) {
        auto job = video_jobs.get_job(job_id);
        if (!job) {
            return crow::response(404, "application/json", "{\"error\":\"Unknown job. It may have expired; please convert again.\"}");
        }

        json res = {
            {"job_id", job->job_id},
            {"state", job->state},
            {"progress", job->progress},
            {"format", job->format},
            {"output_bytes", job->output_bytes},
            {"warnings", job->warnings},
            {"error", job->error},
            {"error_detail", job->error_detail}
        };

        if (job->state == "done") {
            res["download_url"] = "/api/video/download/" + job_id;
            res["stream_url"] = "/api/video/stream/" + job_id;
        }

        return crow::response(200, "application/json", res.dump());
    });

    CROW_ROUTE(app, "/api/video/stream/<string>")
    ([&cfg, &video_jobs](crow::response& res, const std::string& job_id) {
        std::string path = video_jobs.find_output_filepath(job_id);
        if (path.empty() || !fs::exists(path)) {
            res.code = 404;
            res.set_header("Content-Type", "text/html");
            res.write(render_template_page(cfg.templates_dir, "not_found.html"));
            res.end();
            return;
        }

        std::string content = read_file_content(path);
        res.code = 200;
        res.set_header("Content-Type", get_mime_type(path));
        res.write(content);
        res.end();
    });

    CROW_ROUTE(app, "/api/video/download/<string>")
    ([&cfg, &video_jobs](crow::response& res, const std::string& job_id) {
        std::string path = video_jobs.find_output_filepath(job_id);
        if (path.empty() || !fs::exists(path)) {
            res.code = 404;
            res.set_header("Content-Type", "text/html");
            res.write(render_template_page(cfg.templates_dir, "not_found.html"));
            res.end();
            return;
        }

        size_t dot = path.rfind('.');
        std::string ext = (dot != std::string::npos) ? path.substr(dot + 1) : "mp4";

        std::string content = read_file_content(path);
        res.code = 200;
        res.set_header("Content-Type", "application/octet-stream");
        res.set_header("Content-Disposition", "attachment; filename=\"converted." + ext + "\"");
        res.write(content);
        res.end();
    });

    // -------------------------------------------------------------
    // 404 Error Handler
    // -------------------------------------------------------------
    CROW_CATCHALL_ROUTE(app)
    ([&cfg](crow::response& res) {
        res.code = 404;
        res.set_header("Content-Type", "text/html");
        res.write(render_template_page(cfg.templates_dir, "not_found.html"));
        res.end();
    });

    app.port(cfg.port).multithreaded().run();
    return 0;
}
