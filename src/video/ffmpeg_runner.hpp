#pragma once

#include <string>
#include <vector>
#include <set>
#include <optional>
#include <cstdint>

namespace video {

struct VideoParams {
    std::string format = "mp4";
    std::string video_codec = "libx264";
    std::string audio_codec = "aac";
    std::optional<int> width;
    std::optional<int> height;
    std::optional<double> fps;
    std::string video_bitrate;
    std::string audio_bitrate;
    std::optional<int> crf;
    std::string aspect = "source";
    int rotation = 0;
    std::optional<double> trim_start;
    std::optional<double> trim_end;
    bool remove_audio = false;
    std::string preset = "veryfast";
};

struct ValidationResult {
    bool success = false;
    VideoParams params;
    std::vector<std::string> warnings;
    std::string error;
};

struct FFmpegInfo {
    bool available = false;
    std::string path;
    std::string version;
    bool outdated = false;
};

class FFmpegRunner {
public:
    static FFmpegInfo get_info();
    static std::string get_bin_path();
    static bool is_available();

    static ValidationResult validate_standard(
        const std::string& task,
        const std::string& resolution,
        const std::string& fps,
        const std::string& quality
    );

    static ValidationResult validate_professional(
        const std::string& format,
        const std::string& video_codec,
        const std::string& audio_codec,
        bool remove_audio,
        const std::string& width,
        const std::string& height,
        const std::string& fps,
        const std::string& video_bitrate,
        const std::string& audio_bitrate,
        const std::string& crf,
        const std::string& aspect,
        const std::string& rotation,
        const std::string& preset,
        const std::string& trim_start,
        const std::string& trim_end
    );

    static std::vector<std::string> build_ffmpeg_args(
        const VideoParams& params,
        const std::string& input_path,
        const std::string& output_path
    );

    static bool run_ffmpeg(
        const std::vector<std::string>& args,
        const std::string& log_file_path,
        int timeout_seconds,
        int& exit_code,
        std::string& log_tail
    );

    static int64_t estimate_size(
        int64_t original_bytes,
        const std::string& quality,
        const std::string& resolution,
        const std::string& video_bitrate,
        const std::string& audio_bitrate,
        double duration_sec
    );

    static bool is_allowed_extension(const std::string& filename);
};

} // namespace video
