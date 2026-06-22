cmake_minimum_required(VERSION 3.22.1)
project(nativetextapp CXX)

set(CMAKE_CXX_STANDARD 17)

# ─── android_native_app_glue ─────────────────────────────────────────────────
# Provided by the NDK itself
add_library(android_native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)
target_include_directories(android_native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue
)

# ─── FreeType 2 ──────────────────────────────────────────────────────────────
# We download and build FreeType from source inside the NDK build.
# Using the FetchContent CMake module (available since CMake 3.11).
include(FetchContent)

FetchContent_Declare(
    freetype
    URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-2.tar.gz
    URL_HASH SHA256=427201f5d5151670d05c1f5b45bef5dda1f2e7dd971ef54f0feaaa7d7ca5b0a9
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(FT_DISABLE_ZLIB    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2   ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI  ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS  OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(freetype)

# ─── Our shared library ───────────────────────────────────────────────────────
add_library(nativetextapp SHARED
    src/main/cpp/main.cpp
)

target_include_directories(nativetextapp PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue
)

target_link_libraries(nativetextapp
    android_native_app_glue
    freetype
    android
    EGL
    GLESv2
    log
    dl
    m
)

# Hide internal symbols
target_compile_options(nativetextapp PRIVATE
    -fvisibility=hidden
    -O2
    -Wall
)
