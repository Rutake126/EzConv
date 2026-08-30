#define _CRT_SECURE_NO_WARNINGS
#include "image_encoder.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace fs = std::filesystem;

static void write_to_stream_cb(void* context, void* data, int size) {
    auto* stream = static_cast<std::ofstream*>(context);
    if (stream && stream->is_open() && data && size > 0) {
        stream->write(static_cast<const char*>(data), size);
    }
}

static fs::path to_clean_path(const std::string& u8str) {
#if defined(_WIN32)
    if (u8str.empty()) return fs::path();
    int size = MultiByteToWideChar(CP_UTF8, 0, u8str.data(), (int)u8str.size(), nullptr, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, u8str.data(), (int)u8str.size(), &wstr[0], size);
    for (auto& ch : wstr) {
        if (ch == L'/') ch = L'\\';
    }
    std::wstring clean;
    for (size_t i = 0; i < wstr.size(); ++i) {
        if (wstr[i] == L'\\' && i > 0 && i + 1 < wstr.size() && wstr[i + 1] == L'\\') {
            continue;
        }
        clean += wstr[i];
    }
    return fs::path(clean);
#else
    return fs::u8path(u8str);
#endif
}

bool ImageEncoder::save_as_jpeg(const std::string& output_path, 
                               int width, int height, int channels, 
                               const uint8_t* data, 
                               int quality) {
    if (!data || width <= 0 || height <= 0 || channels <= 0) {
        return false;
    }
    
    try {
        fs::path p = to_clean_path(output_path);
        if (p.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
        }

#if defined(_WIN32)
        std::ofstream ofs(p.wstring(), std::ios::binary);
#else
        std::ofstream ofs(p, std::ios::binary);
#endif
        if (!ofs.is_open()) {
            return false;
        }

        int q = std::max(1, std::min(100, quality));
        int result = stbi_write_jpg_to_func(write_to_stream_cb, &ofs, width, height, channels, data, q);
        ofs.close();
        return result != 0;
    } catch (...) {
        return false;
    }
}

bool ImageEncoder::save_as_png(const std::string& output_path, 
                              int width, int height, int channels, 
                              const uint8_t* data) {
    if (!data || width <= 0 || height <= 0 || channels <= 0) {
        return false;
    }

    try {
        fs::path p = to_clean_path(output_path);
        if (p.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
        }

#if defined(_WIN32)
        std::ofstream ofs(p.wstring(), std::ios::binary);
#else
        std::ofstream ofs(p, std::ios::binary);
#endif
        if (!ofs.is_open()) {
            return false;
        }

        int stride = width * channels;
        int result = stbi_write_png_to_func(write_to_stream_cb, &ofs, width, height, channels, data, stride);
        ofs.close();
        return result != 0;
    } catch (...) {
        return false;
    }
}

bool ImageEncoder::encode_to_file(const std::string& output_path, 
                                 const RawImage& image, 
                                 ImageFormat format, 
                                 int quality) {
    if (image.data.empty() || image.width <= 0 || image.height <= 0 || image.channels <= 0) {
        return false;
    }

    if (format == ImageFormat::JPG) {
        return save_as_jpeg(output_path, image.width, image.height, image.channels, image.data.data(), quality);
    } else if (format == ImageFormat::PNG) {
        return save_as_png(output_path, image.width, image.height, image.channels, image.data.data());
    }
    return false;
}
