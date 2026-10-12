#include "video_jobs.hpp"
#include <chrono>
#include <random>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace video {

namespace {

double get_time_seconds() {
    using namespace std::chrono;
    auto now = system_clock::now();
    return duration_cast<duration<double>>(now.time_since_epoch()).count();
}

} // namespace

VideoJobManager::VideoJobManager(
    std::string upload_dir,
    std::string output_dir,
    int timeout_seconds,
    int cleanup_seconds
) : upload_dir_(std::move(upload_dir)),
    output_dir_(std::move(output_dir)),
    timeout_seconds_(timeout_seconds),
    cleanup_seconds_(cleanup_seconds) {

    fs::create_directories(upload_dir_);
    fs::create_directories(output_dir_);

    cleanup_thread_ = std::thread(&VideoJobManager::cleanup_loop, this);
}

VideoJobManager::~VideoJobManager() {
    running_ = false;
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
}

std::string VideoJobManager::create_job_id() {
    static const char CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(CHARS) - 2);

    std::string id;
    for (int i = 0; i < 24; ++i) {
        id.push_back(CHARS[dis(gen)]);
    }
    return id;
}

std::string VideoJobManager::get_output_filepath(const std::string& job_id, const std::string& ext) const {
    return (fs::path(output_dir_) / (job_id + "_out." + ext)).string();
}

std::string VideoJobManager::find_output_filepath(const std::string& job_id) const {
    static const std::vector<std::string> exts = { "mp4", "webm", "mov", "mkv", "avi" };
    for (const auto& ext : exts) {
        std::string p = get_output_filepath(job_id, ext);
        if (fs::exists(p)) {
            return p;
        }
    }
    return "";
}

void VideoJobManager::enqueue_job(
    const std::string& job_id,
    const VideoParams& params,
    const std::string& input_path,
    const std::string& output_path,
    const std::vector<std::string>& warnings,
    const std::string& format
) {
    double now = get_time_seconds();
    {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        JobStatus js;
        js.job_id = job_id;
        js.state = "queued";
        js.progress = 0;
        js.format = format;
        js.warnings = warnings;
        js.created_at = now;
        js.updated_at = now;
        jobs_[job_id] = js;
    }

    std::thread([this, job_id, params, input_path, output_path, warnings, format]() {
        run_job(job_id, params, input_path, output_path, warnings, format);
    }).detach();
}

void VideoJobManager::run_job(
    std::string job_id,
    VideoParams params,
    std::string input_path,
    std::string output_path,
    std::vector<std::string> warnings,
    std::string format
) {
    {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        jobs_[job_id].state = "processing";
        jobs_[job_id].progress = 5;
        jobs_[job_id].updated_at = get_time_seconds();
    }

    std::string log_path = (fs::path(output_dir_) / (job_id + ".log")).string();
    auto args = FFmpegRunner::build_ffmpeg_args(params, input_path, output_path);

    int exit_code = 0;
    std::string log_tail;
    bool ok = FFmpegRunner::run_ffmpeg(args, log_path, timeout_seconds_, exit_code, log_tail);

    // Delete upload immediately
    std::error_code ec;
    fs::remove(input_path, ec);
    fs::remove(log_path, ec);

    std::lock_guard<std::mutex> lock(jobs_mutex_);
    JobStatus& js = jobs_[job_id];
    js.updated_at = get_time_seconds();

    if (!ok || exit_code != 0 || !fs::exists(output_path)) {
        js.state = "error";
        js.progress = 0;
        if (exit_code == -2) {
            js.error = "Conversion timed out. Try a shorter video or lower settings.";
        } else {
            js.error = "Conversion failed. Try different settings (e.g. MP4 + H.264 + AAC).";
            js.error_detail = log_tail.empty() ? "FFmpeg exited with non-zero code." : log_tail.substr(0, 500);
        }
        fs::remove(output_path, ec);
        return;
    }

    js.state = "done";
    js.progress = 100;
    js.format = format;
    try {
        js.output_bytes = fs::file_size(output_path);
    } catch (...) {
        js.output_bytes = 0;
    }
    js.warnings = warnings;
    if (js.output_bytes > 100 * 1024 * 1024) {
        js.warnings.push_back("Output is over 100 MB; downloads may be slow on mobile data.");
    }
}

std::optional<JobStatus> VideoJobManager::get_job(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(jobs_mutex_);
    auto it = jobs_.find(job_id);
    if (it != jobs_.end()) {
        JobStatus js = it->second;
        if (js.state == "processing") {
            double elapsed = get_time_seconds() - js.created_at;
            js.progress = std::min(95, 5 + static_cast<int>((elapsed / 1200.0) * 90.0));
        }
        return js;
    }

    // Fallback: check filesystem
    std::string out_file = find_output_filepath(job_id);
    if (!out_file.empty()) {
        JobStatus js;
        js.job_id = job_id;
        js.state = "done";
        js.progress = 100;
        try {
            js.output_bytes = fs::file_size(out_file);
        } catch (...) {
            js.output_bytes = 0;
        }
        size_t dot = out_file.rfind('.');
        if (dot != std::string::npos) {
            js.format = out_file.substr(dot + 1);
        }
        return js;
    }

    return std::nullopt;
}

void VideoJobManager::cleanup_loop() {
    while (running_) {
        for (int i = 0; i < 300 && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!running_) break;
        cleanup_old_files();
    }
}

void VideoJobManager::cleanup_old_files() {
    double now = get_time_seconds();
    std::error_code ec;

    for (const auto& dir : { upload_dir_, output_dir_ }) {
        if (!fs::exists(dir)) continue;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            auto ftime = entry.last_write_time(ec);
            if (ec) continue;

            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            double file_time = std::chrono::duration_cast<std::chrono::duration<double>>(sctp.time_since_epoch()).count();

            if (now - file_time > cleanup_seconds_) {
                fs::remove(entry.path(), ec);
            }
        }
    }

    // Prune jobs map
    std::lock_guard<std::mutex> lock(jobs_mutex_);
    for (auto it = jobs_.begin(); it != jobs_.end(); ) {
        if (now - it->second.updated_at > cleanup_seconds_ + 600) {
            it = jobs_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace video
