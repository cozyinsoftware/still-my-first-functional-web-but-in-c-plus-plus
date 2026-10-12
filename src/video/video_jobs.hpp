#pragma once

#include "ffmpeg_runner.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <thread>
#include <atomic>
#include <optional>

namespace video {

struct JobStatus {
    std::string job_id;
    std::string state = "queued"; // "queued", "processing", "done", "error"
    int progress = 0;
    std::string format;
    int64_t output_bytes = 0;
    std::vector<std::string> warnings;
    std::string error;
    std::string error_detail;
    double created_at = 0.0;
    double updated_at = 0.0;
};

class VideoJobManager {
public:
    VideoJobManager(
        std::string upload_dir,
        std::string output_dir,
        int timeout_seconds = 7200,
        int cleanup_seconds = 1800
    );
    ~VideoJobManager();

    std::string create_job_id();

    void enqueue_job(
        const std::string& job_id,
        const VideoParams& params,
        const std::string& input_path,
        const std::string& output_path,
        const std::vector<std::string>& warnings,
        const std::string& format
    );

    std::optional<JobStatus> get_job(const std::string& job_id);

    std::string get_output_filepath(const std::string& job_id, const std::string& ext) const;
    std::string find_output_filepath(const std::string& job_id) const;

private:
    std::string upload_dir_;
    std::string output_dir_;
    int timeout_seconds_;
    int cleanup_seconds_;

    std::unordered_map<std::string, JobStatus> jobs_;
    mutable std::mutex jobs_mutex_;

    std::atomic<bool> running_{true};
    std::thread cleanup_thread_;

    void run_job(
        std::string job_id,
        VideoParams params,
        std::string input_path,
        std::string output_path,
        std::vector<std::string> warnings,
        std::string format
    );

    void cleanup_loop();
    void cleanup_old_files();
};

} // namespace video
