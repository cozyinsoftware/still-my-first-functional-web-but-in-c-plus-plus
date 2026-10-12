#include "ffmpeg_runner.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace video {

namespace {

static const std::set<std::string> ALLOWED_EXTS = {
    "mp4", "mov", "avi", "mkv", "webm", "mpeg", "mpg", "m4v"
};

static const std::set<std::string> ALLOWED_FORMATS = {
    "mp4", "webm", "mov", "mkv", "avi"
};

static const std::set<std::string> ALLOWED_VCODECS = {
    "libx264", "libx265", "libvpx-vp9", "libvpx", "mpeg4"
};

static const std::set<std::string> ALLOWED_ACODECS = {
    "aac", "mp3", "opus", "vorbis", "ac3"
};

static const std::set<std::string> ALLOWED_PRESETS = {
    "ultrafast", "superfast", "veryfast", "faster",
    "fast", "medium", "slow", "slower", "veryslow"
};

static const std::set<std::string> ALLOWED_ASPECTS = {
    "source", "16:9", "4:3", "1:1", "9:16"
};

std::string trim_str(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string to_lower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::optional<double> parse_time_seconds(const std::string& raw) {
    std::string s = trim_str(raw);
    if (s.empty()) return std::nullopt;

    if (s.find(':') != std::string::npos) {
        std::stringstream ss(s);
        std::string part;
        std::vector<double> parts;
        while (std::getline(ss, part, ':')) {
            try {
                parts.push_back(std::stod(part));
            } catch (...) {
                return std::nullopt;
            }
        }
        if (parts.size() < 2 || parts.size() > 3) return std::nullopt;
        double total = 0.0;
        for (double p : parts) {
            if (p < 0) return std::nullopt;
            total = total * 60.0 + p;
        }
        return total;
    }

    try {
        double val = std::stod(s);
        return (val >= 0) ? std::optional<double>(val) : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

int64_t parse_bitrate_to_bps(const std::string& raw) {
    std::string s = to_lower_str(trim_str(raw));
    if (s.empty()) return 0;

    double multiplier = 1.0;
    if (s.back() == 'k') {
        multiplier = 1000.0;
        s.pop_back();
    } else if (s.back() == 'm') {
        multiplier = 1000000.0;
        s.pop_back();
    }

    try {
        double num = std::stod(s);
        return static_cast<int64_t>(num * multiplier);
    } catch (...) {
        return 0;
    }
}

std::string format_bitrate_arg(const std::string& raw) {
    std::string s = trim_str(raw);
    if (s.empty()) return "";
    if (s.back() == 'm') s.back() = 'M';
    return s;
}

} // namespace

bool FFmpegRunner::is_allowed_extension(const std::string& filename) {
    size_t dot = filename.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = to_lower_str(filename.substr(dot + 1));
    return ALLOWED_EXTS.find(ext) != ALLOWED_EXTS.end();
}

std::string FFmpegRunner::get_bin_path() {
    static std::string cached_path;
    static bool checked = false;
    if (checked && !cached_path.empty() && fs::exists(cached_path)) {
        return cached_path;
    }

    const char* env_path = std::getenv("FFMPEG_BINARY");
    if (!env_path) env_path = std::getenv("FFMPEG_PATH");
    if (env_path && fs::exists(env_path)) {
        cached_path = env_path;
        checked = true;
        return cached_path;
    }

#if defined(_WIN32)
    std::vector<std::string> candidates = {
        "ffmpeg.exe",
        "C:\\ffmpeg\\bin\\ffmpeg.exe",
        "C:\\ffmpeg\\ffmpeg.exe",
        "C:\\Program Files\\ffmpeg\\bin\\ffmpeg.exe",
        "C:\\ProgramData\\chocolatey\\bin\\ffmpeg.exe"
    };

    const char* local_app = std::getenv("LOCALAPPDATA");
    if (local_app) {
        candidates.push_back(std::string(local_app) + "\\Programs\\ffmpeg\\bin\\ffmpeg.exe");
    }

    const char* user_prof = std::getenv("USERPROFILE");
    if (user_prof) {
        candidates.push_back(std::string(user_prof) + "\\scoop\\shims\\ffmpeg.exe");
        candidates.push_back(std::string(user_prof) + "\\scoop\\apps\\ffmpeg\\current\\bin\\ffmpeg.exe");
    }

    for (const auto& path : candidates) {
        if (fs::exists(path)) {
            cached_path = path;
            checked = true;
            return cached_path;
        }
    }
#endif

    // Fallback: assume "ffmpeg" on PATH
    cached_path = "ffmpeg";
    checked = true;
    return cached_path;
}

FFmpegInfo FFmpegRunner::get_info() {
    FFmpegInfo info;
    info.path = get_bin_path();

    std::string test_cmd = info.path + " -version";
    FILE* pipe = nullptr;
#if defined(_WIN32)
    pipe = _popen(test_cmd.c_str(), "r");
#else
    pipe = popen(test_cmd.c_str(), "r");
#endif

    if (!pipe) {
        info.available = false;
        return info;
    }

    char buffer[256];
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        info.version = trim_str(buffer);
        if (info.version.find("ffmpeg version") != std::string::npos) {
            info.available = true;
        }
    }

#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    if (info.available) {
        std::regex re("200\\d|201[0-7]");
        info.outdated = std::regex_search(info.version, re);
    }
    return info;
}

bool FFmpegRunner::is_available() {
    return get_info().available;
}

ValidationResult FFmpegRunner::validate_standard(
    const std::string& task,
    const std::string& resolution,
    const std::string& fps,
    const std::string& quality
) {
    ValidationResult res;
    res.params.format = "mp4";
    res.params.video_codec = "libx264";
    res.params.audio_codec = "aac";
    res.params.preset = "veryfast";

    std::string t = to_lower_str(trim_str(task));
    if (t == "webm") {
        res.params.format = "webm";
        res.params.video_codec = "libvpx-vp9";
        res.params.audio_codec = "opus";
    }

    std::string r = to_lower_str(trim_str(resolution));
    if (r == "360p") res.params.height = 360;
    else if (r == "480p") res.params.height = 480;
    else if (r == "720p") res.params.height = 720;
    else if (r == "1080p") res.params.height = 1080;
    else if (r == "2160p") {
        res.params.height = 2160;
        res.warnings.push_back("4K output can be very large and slow to encode.");
    }

    std::string f = to_lower_str(trim_str(fps));
    if (f == "24") res.params.fps = 24.0;
    else if (f == "30") res.params.fps = 30.0;
    else if (f == "60") res.params.fps = 60.0;

    std::string q = to_lower_str(trim_str(quality));
    if (q == "low") res.params.crf = 30;
    else if (q == "high") {
        res.params.crf = 18;
        res.warnings.push_back("High quality setting creates significantly larger output files.");
    } else {
        res.params.crf = 23; // medium default
    }

    res.success = true;
    return res;
}

ValidationResult FFmpegRunner::validate_professional(
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
) {
    ValidationResult res;
    std::string fmt = to_lower_str(trim_str(format.empty() ? "mp4" : format));
    if (ALLOWED_FORMATS.find(fmt) == ALLOWED_FORMATS.end()) {
        res.error = "Invalid output format.";
        return res;
    }
    res.params.format = fmt;

    std::string vcodec = trim_str(video_codec.empty() ? "libx264" : video_codec);
    if (ALLOWED_VCODECS.find(vcodec) == ALLOWED_VCODECS.end()) {
        res.error = "Invalid video codec.";
        return res;
    }
    if (fmt == "webm" && vcodec != "libvpx-vp9" && vcodec != "libvpx") {
        res.error = "WebM only supports VP8 (libvpx) or VP9 (libvpx-vp9) video.";
        return res;
    }
    res.params.video_codec = vcodec;

    res.params.remove_audio = remove_audio;
    if (!remove_audio) {
        std::string acodec = trim_str(audio_codec.empty() ? "aac" : audio_codec);
        if (ALLOWED_ACODECS.find(acodec) == ALLOWED_ACODECS.end()) {
            res.error = "Invalid audio codec.";
            return res;
        }
        if (fmt == "webm" && acodec != "opus" && acodec != "vorbis") {
            res.error = "WebM audio must be opus or vorbis.";
            return res;
        }
        res.params.audio_codec = acodec;
    }

    if (!width.empty()) {
        try {
            int w = std::stoi(width);
            if (w < 16 || w > 7680) { res.error = "Width must be between 16 and 7680."; return res; }
            res.params.width = w;
        } catch (...) { res.error = "Width must be a valid number."; return res; }
    }
    if (!height.empty()) {
        try {
            int h = std::stoi(height);
            if (h < 16 || h > 7680) { res.error = "Height must be between 16 and 7680."; return res; }
            res.params.height = h;
        } catch (...) { res.error = "Height must be a valid number."; return res; }
    }

    if (!fps.empty()) {
        try {
            double f = std::stod(fps);
            if (f < 1.0 || f > 120.0) { res.error = "FPS must be between 1 and 120."; return res; }
            res.params.fps = f;
        } catch (...) { res.error = "FPS must be a valid number."; return res; }
    }

    if (!video_bitrate.empty()) {
        int64_t vb_bps = parse_bitrate_to_bps(video_bitrate);
        if (vb_bps < 50000 || vb_bps > 100000000) {
            res.error = "Video bitrate must be between 50k and 100M.";
            return res;
        }
        res.params.video_bitrate = format_bitrate_arg(video_bitrate);
    }

    if (!audio_bitrate.empty()) {
        int64_t ab_bps = parse_bitrate_to_bps(audio_bitrate);
        if (ab_bps < 32000 || ab_bps > 1000000) {
            res.error = "Audio bitrate must be between 32k and 1000k.";
            return res;
        }
        res.params.audio_bitrate = format_bitrate_arg(audio_bitrate);
    }

    if (!crf.empty()) {
        try {
            int c = std::stoi(crf);
            if (c < 0 || c > 51) { res.error = "CRF must be between 0 and 51."; return res; }
            res.params.crf = c;
        } catch (...) { res.error = "CRF must be a number."; return res; }
    }

    std::string asp = trim_str(aspect.empty() ? "source" : aspect);
    if (ALLOWED_ASPECTS.find(asp) == ALLOWED_ASPECTS.end()) {
        res.error = "Invalid aspect ratio.";
        return res;
    }
    res.params.aspect = asp;

    if (!rotation.empty()) {
        try {
            int rot = std::stoi(rotation);
            if (rot != 0 && rot != 90 && rot != 180 && rot != 270) {
                res.error = "Rotation must be 0, 90, 180, or 270.";
                return res;
            }
            res.params.rotation = rot;
        } catch (...) { res.error = "Rotation must be a valid number."; return res; }
    }

    std::string pr = trim_str(preset.empty() ? "veryfast" : preset);
    if (ALLOWED_PRESETS.find(pr) == ALLOWED_PRESETS.end()) {
        res.error = "Invalid preset.";
        return res;
    }
    res.params.preset = pr;

    if (!trim_start.empty()) res.params.trim_start = parse_time_seconds(trim_start);
    if (!trim_end.empty()) res.params.trim_end = parse_time_seconds(trim_end);

    if (res.params.trim_start && res.params.trim_end && *res.params.trim_end <= *res.params.trim_start) {
        res.error = "Trim end must be greater than trim start.";
        return res;
    }

    res.success = true;
    return res;
}

std::vector<std::string> FFmpegRunner::build_ffmpeg_args(
    const VideoParams& p,
    const std::string& input_path,
    const std::string& output_path
) {
    std::vector<std::string> args = { get_bin_path(), "-nostdin", "-y" };

    if (p.trim_start) {
        args.push_back("-ss");
        args.push_back(std::to_string(*p.trim_start));
    }
    args.push_back("-i");
    args.push_back(input_path);

    if (p.trim_end) {
        args.push_back("-t");
        double dur = p.trim_start ? (*p.trim_end - *p.trim_start) : *p.trim_end;
        args.push_back(std::to_string(dur));
    }

    std::vector<std::string> filters;
    if (p.rotation == 90) filters.push_back("transpose=1");
    else if (p.rotation == 180) filters.push_back("transpose=1,transpose=1");
    else if (p.rotation == 270) filters.push_back("transpose=2");

    if (p.width && p.height) {
        int w = (*p.width / 2) * 2;
        int h = (*p.height / 2) * 2;
        filters.push_back("scale=" + std::to_string(w) + ":" + std::to_string(h));
    } else if (p.height && !p.width) {
        int h = (*p.height / 2) * 2;
        filters.push_back("scale=-2:" + std::to_string(h));
    } else if (p.width && !p.height) {
        int w = (*p.width / 2) * 2;
        filters.push_back("scale=" + std::to_string(w) + ":-2");
    }

    if (p.aspect != "source") {
        int w = 1280, h = 720;
        if (p.aspect == "4:3") { w = 960; h = 720; }
        else if (p.aspect == "1:1") { w = 720; h = 720; }
        else if (p.aspect == "9:16") { w = 720; h = 1280; }
        filters.push_back("scale=" + std::to_string(w) + ":" + std::to_string(h) + ":force_original_aspect_ratio=decrease");
        filters.push_back("pad=" + std::to_string(w) + ":" + std::to_string(h) + ":(ow-iw)/2:(oh-ih)/2");
    }

    if ((p.video_codec == "libx264" || p.video_codec == "libx265") && filters.empty()) {
        filters.push_back("scale=trunc(iw/2)*2:trunc(ih/2)*2");
    }

    if (!filters.empty()) {
        std::string vf;
        for (size_t i = 0; i < filters.size(); ++i) {
            if (i > 0) vf += ",";
            vf += filters[i];
        }
        args.push_back("-vf");
        args.push_back(vf);
    }

    args.push_back("-c:v");
    args.push_back(p.video_codec);

    if (p.video_codec == "libx264" || p.video_codec == "libx265") {
        args.push_back("-pix_fmt");
        args.push_back("yuv420p");
        args.push_back("-preset");
        args.push_back(p.preset);
    }

    if (p.crf && p.video_codec != "mpeg4") {
        args.push_back("-crf");
        args.push_back(std::to_string(*p.crf));
    }

    if (!p.video_bitrate.empty()) {
        args.push_back("-b:v");
        args.push_back(p.video_bitrate);
    }

    if (p.fps) {
        args.push_back("-r");
        args.push_back(std::to_string(*p.fps));
    }

    if (p.format == "mp4" || p.format == "mov") {
        args.push_back("-movflags");
        args.push_back("+faststart");
    }

    args.push_back("-map");
    args.push_back("0:v:0");
    args.push_back("-sn");

    if (p.remove_audio) {
        args.push_back("-an");
    } else {
        args.push_back("-map");
        args.push_back("0:a?");
        args.push_back("-c:a");
        args.push_back(p.audio_codec);
        if (!p.audio_bitrate.empty()) {
            args.push_back("-b:a");
            args.push_back(p.audio_bitrate);
        }
    }

    args.push_back(output_path);
    return args;
}

bool FFmpegRunner::run_ffmpeg(
    const std::vector<std::string>& args,
    const std::string& log_file_path,
    int timeout_seconds,
    int& exit_code,
    std::string& log_tail
) {
    exit_code = -1;
    log_tail.clear();

#if defined(_WIN32)
    std::wstring cmdline;
    for (const auto& a : args) {
        if (!cmdline.empty()) cmdline += L" ";
        cmdline += L"\"";
        for (char c : a) {
            if (c == '\"') cmdline += L"\\\"";
            else cmdline += static_cast<wchar_t>(c);
        }
        cmdline += L"\"";
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    std::wstring wlog(log_file_path.begin(), log_file_path.end());
    HANDLE hLog = CreateFileW(
        wlog.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hLog == INVALID_HANDLE_VALUE) {
        return false;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = NULL;
    si.hStdOutput = NULL;
    si.hStdError = hLog;

    PROCESS_INFORMATION pi{};
    BOOL success = CreateProcessW(
        NULL,
        &cmdline[0],
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );

    CloseHandle(hLog);

    if (!success) {
        return false;
    }

    DWORD wait_res = WaitForSingleObject(pi.hProcess, timeout_seconds * 1000);
    if (wait_res == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        exit_code = static_cast<int>(code);
    } else {
        TerminateProcess(pi.hProcess, 1);
        exit_code = -2; // Timed out
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Read log tail
    std::ifstream log_file(log_file_path, std::ios::binary);
    if (log_file.is_open()) {
        log_file.seekg(0, std::ios::end);
        std::streamoff len = log_file.tellg();
        std::streamoff read_len = std::min(len, static_cast<std::streamoff>(4096));
        log_file.seekg(len - read_len, std::ios::beg);
        log_tail.resize(static_cast<size_t>(read_len));
        log_file.read(&log_tail[0], read_len);
    }

    return (exit_code == 0);
#else
    // POSIX fallback
    return false;
#endif
}

int64_t FFmpegRunner::estimate_size(
    int64_t original_bytes,
    const std::string& quality,
    const std::string& resolution,
    const std::string& video_bitrate,
    const std::string& audio_bitrate,
    double duration_sec
) {
    int64_t vb = parse_bitrate_to_bps(video_bitrate);
    int64_t ab = parse_bitrate_to_bps(audio_bitrate);

    if (vb > 0 && duration_sec > 0) {
        int64_t total_bps = vb + (ab > 0 ? ab : 128000);
        return static_cast<int64_t>((total_bps * duration_sec) / 8.0);
    }

    if (original_bytes <= 0) return 0;

    double q_factor = 0.6;
    if (quality == "low") q_factor = 0.35;
    else if (quality == "high") q_factor = 0.9;

    double r_factor = 1.0;
    if (resolution == "2160p") r_factor = 1.6;
    else if (resolution == "720p") r_factor = 0.6;
    else if (resolution == "480p") r_factor = 0.4;
    else if (resolution == "360p") r_factor = 0.3;

    return static_cast<int64_t>(original_bytes * q_factor * r_factor);
}

} // namespace video
