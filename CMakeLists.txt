cmake_minimum_required(VERSION 3.20)
project(quicktools_server LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization & warning flags
if(MSVC)
    add_compile_options(/W3 /O2)
else()
    add_compile_options(-Wall -Wextra -O3)
endif()

# Required for Standalone Asio / Crow on Windows MinGW (MSYS2)
if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 ASIO_STANDALONE)
endif()

include(FetchContent)

# 1. nlohmann/json
message(STATUS "Fetching nlohmann_json...")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# 2. inja template engine
message(STATUS "Fetching inja...")
FetchContent_Declare(
    inja
    GIT_REPOSITORY https://github.com/pantor/inja.git
    GIT_TAG v3.4.0
)
set(INJA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INJA_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(INJA_USE_EMBEDDED_JSON OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(inja)

# 3. Asio (Standalone Asio required by Crow)
message(STATUS "Fetching standalone asio...")
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
)
FetchContent_MakeAvailable(asio)
set(ASIO_INCLUDE_DIR "${asio_SOURCE_DIR}/asio/include" CACHE PATH "Path to Asio headers" FORCE)
include_directories(${ASIO_INCLUDE_DIR})

# 4. Crow C++ Framework
message(STATUS "Fetching Crow...")
FetchContent_Declare(
    Crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG v1.2.0
)
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CROW_AMALGAMATION OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Crow)

# 5. SQLite3
# Add MSYS2 paths so Windows native CMake can locate libraries
list(APPEND CMAKE_PREFIX_PATH "C:/msys64/ucrt64" "C:/msys64/mingw64")

find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    find_path(SQLITE3_INC sqlite3.h PATHS "C:/msys64/ucrt64/include" "C:/msys64/mingw64/include")
    find_library(SQLITE3_LIB NAMES sqlite3 PATHS "C:/msys64/ucrt64/lib" "C:/msys64/mingw64/lib")
    if(SQLITE3_INC AND SQLITE3_LIB)
        message(STATUS "Found MSYS2 SQLite3: ${SQLITE3_LIB}")
        add_library(SQLite::SQLite3 UNKNOWN IMPORTED)
        set_target_properties(SQLite::SQLite3 PROPERTIES
            IMPORTED_LOCATION "${SQLITE3_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${SQLITE3_INC}"
        )
    else()
        message(STATUS "Fetching official SQLite3 zip...")
        FetchContent_Declare(
            sqlite_official
            URL https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip
        )
        FetchContent_GetProperties(sqlite_official)
        if(NOT sqlite_official_POPULATED)
            FetchContent_Populate(sqlite_official)
            add_library(sqlite3_static STATIC "${sqlite_official_SOURCE_DIR}/sqlite3.c")
            target_include_directories(sqlite3_static PUBLIC "${sqlite_official_SOURCE_DIR}")
            add_library(SQLite::SQLite3 ALIAS sqlite3_static)
        endif()
    endif()
endif()

# Project Source Files
set(SOURCES
    src/main.cpp
    src/utils/base64.cpp
    src/crypto/sha256.cpp
    src/crypto/token.cpp
    src/db/database.cpp
    src/qr/qrcodegen.cpp
    src/qr/qr_service.cpp
    src/video/ffmpeg_runner.cpp
    src/video/video_jobs.cpp
)

add_executable(quicktools_server ${SOURCES})

target_include_directories(quicktools_server PRIVATE
    src
    src/db
    src/crypto
    src/qr
    src/video
    src/utils
    ${inja_SOURCE_DIR}/include
    ${nlohmann_json_SOURCE_DIR}/include
)

target_link_libraries(quicktools_server PRIVATE
    Crow::Crow
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
)

# On Windows, link winsock libraries
if(WIN32)
    target_link_libraries(quicktools_server PRIVATE ws2_32 mswsock)
endif()
