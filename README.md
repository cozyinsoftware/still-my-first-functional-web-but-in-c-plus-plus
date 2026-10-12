# Quick Tools — C++ High Performance Backend

This is the high-performance C++ backend for Quick Tools, migrated from the Flask implementation. It runs side-by-side with the existing `templates/` and `static/` directories.

## Architecture

- **Web Server & Routing**: [Crow C++](https://crowcpp.org/) (Asynchronous, multi-threaded HTTP server with standalone Asio).
- **Template Engine**: [Inja](https://github.com/pantor/inja) (Jinja2-compatible template engine using existing `templates/*.html`).
- **Database**: [SQLite3](https://sqlite.org/) in Write-Ahead-Logging (WAL) mode.
- **QR Code Generator**: Native C++ matrix calculation via Nayuki QR Code generator + pure C++ in-memory PNG encoder.
- **Security & Tokens**: Pure C++ HMAC-SHA256 signature generator for 5-second countdown ad-gate tokens.
- **Video Conversion**: Non-blocking asynchronous worker threads executing FFmpeg (`ffmpeg.exe`) with real-time log monitoring and automatic 30-minute artifact cleanup.

---

## Prerequisites (Windows)

1. **MinGW-w64 (GCC / G++)** with C++17 support:
   ```powershell
   gcc --version
   g++ --version
   ```
2. **CMake** (version 3.20 or newer):
   ```powershell
   cmake --version
   ```
3. **Git** (for CMake `FetchContent` to download Crow, Inja, and SQLite):
   ```powershell
   git --version
   ```
4. **FFmpeg** (installed and available on PATH or in `C:\ffmpeg\bin`):
   ```powershell
   ffmpeg -version
   ```

---

## How to Build

From the root project directory:

```powershell
cd "D:\Code Project\Vibecode\website\cpp_server"

# Generate build files using MinGW Makefiles or Ninja
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# Build the executable
cmake --build build -j4
```

The compiled binary will be located at:
`cpp_server/build/quicktools_server.exe`

---

## How to Run

Run from the root project folder so the server can locate `templates/` and `static/`:

```powershell
cd "D:\Code Project\Vibecode\website"
.\cpp_server\build\quicktools_server.exe
```

Open your browser at:
`http://localhost:5000`

---

## Configuration & Environment Variables

All settings can be configured using environment variables:

| Variable | Default | Description |
| :--- | :--- | :--- |
| `PORT` | `5000` | HTTP listening port |
| `SECRET_KEY` | `change-this-secret-key-before-production` | Secret key for HMAC token signing |
| `DATABASE` | `instance/app.db` | SQLite database file location |
| `STATIC_DIR` | `static` | Path to static assets folder |
| `TEMPLATES_DIR` | `templates` | Path to HTML templates folder |
| `VIDEO_MAX_BYTES` | `7516192768` (7 GB) | Maximum video upload size in bytes |
| `VIDEO_FFMPEG_TIMEOUT` | `7200` (2 hours) | Maximum conversion timeout in seconds |
| `VIDEO_CLEANUP_SECONDS`| `1800` (30 mins) | Time before expired converted videos are deleted |

---

## Endpoints

- `GET /` — QR Scanner
- `GET /image-to-text` — OCR / Tesseract browser scanner
- `GET /link-to-qr` — Link to QR code
- `GET /link-shortener` — URL shortener
- `GET /video-size-changer` — Video resolution and size changer
- `GET /s/<code>` — Ad-gate with 5-second countdown timer
- `GET /go/<code>` — Direct redirect
- `POST /api/generate-qr` — In-memory QR PNG generation (Base64 data URI)
- `POST /api/shorten` — URL shortener
- `POST /api/unlock` — Token unlock for ad-gate countdown
- `POST /api/report` — Report bad link
- `GET /api/video/info` — FFmpeg installation and version status
- `POST /api/video/convert` — Multipart video upload and background FFmpeg conversion
- `GET /api/video/status/<job_id>` — Conversion progress polling
- `GET /api/video/stream/<job_id>` — In-browser video streaming
- `GET /api/video/download/<job_id>` — Converted video attachment download
