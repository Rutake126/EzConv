#include "converter_engine.hpp"
#include <iostream>
#include <mutex>
#include <algorithm>

namespace fs = std::filesystem;

static bool has_jp2_extension(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".jp2" || ext == ".j2k" || ext == ".jpf" || ext == ".jpc";
}

std::vector<fs::path> ConverterEngine::scan_files(const fs::path& root_path, bool recursive) {
    std::vector<fs::path> results;
    if (!fs::exists(root_path)) {
        return results;
    }

    if (fs::is_regular_file(root_path)) {
        if (has_jp2_extension(root_path)) {
            results.push_back(root_path);
        }
        return results;
    }

    if (fs::is_directory(root_path)) {
        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(root_path, fs::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file() && has_jp2_extension(entry.path())) {
                    results.push_back(entry.path());
                }
            }
        } else {
            for (const auto& entry : fs::directory_iterator(root_path, fs::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file() && has_jp2_extension(entry.path())) {
                    results.push_back(entry.path());
                }
            }
        }
    }

    return results;
}

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

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

bool ConverterEngine::convert_single(const std::string& src_path, 
                                    const std::string& dst_path, 
                                    ImageFormat format, 
                                    int quality) {
    RawImage raw;
    if (!JP2Decoder::decode_file(src_path, raw)) {
        return false;
    }

    fs::path target_file = to_clean_path(dst_path);
    if (target_file.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(target_file.parent_path(), ec);
    }

    return ImageEncoder::encode_to_file(dst_path, raw, format, quality);
}

ConvertStats ConverterEngine::convert_batch(const ConvertOptions& options, 
                                          ProgressCallback progress_cb) {
    ConvertStats stats;
    auto start_time = std::chrono::steady_clock::now();

    fs::path in_path(options.input_path);
    std::vector<fs::path> files = scan_files(in_path, options.recursive);
    stats.total_files = files.size();

    if (files.empty()) {
        auto end_time = std::chrono::steady_clock::now();
        stats.total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        return stats;
    }

    // Determine output directory
    fs::path out_base_dir;
    if (!options.output_path.empty()) {
        out_base_dir = fs::path(options.output_path);
    } else {
        if (fs::is_directory(in_path)) {
            out_base_dir = in_path / "converted_images";
        } else {
            out_base_dir = in_path.parent_path() / "converted_images";
        }
    }

    std::error_code ec;
    fs::create_directories(out_base_dir, ec);

    size_t num_threads = (options.thread_count > 0) 
        ? options.thread_count 
        : std::max(1u, std::thread::hardware_concurrency());

    ThreadPool pool(num_threads);
    std::mutex stats_mutex;
    size_t completed_count = 0;

    std::string out_ext = (options.format == ImageFormat::JPG) ? ".jpg" : ".png";

    for (const auto& file : files) {
        pool.enqueue([&, file]() {
            fs::path relative_file;
            if (fs::is_directory(in_path) && options.recursive) {
                relative_file = fs::relative(file, in_path);
            } else {
                relative_file = file.filename();
            }

            fs::path dest_file = out_base_dir / relative_file;
            dest_file.replace_extension(out_ext);

            std::string err_msg;
            bool success = convert_single(file.string(), dest_file.string(), options.format, options.quality);
            if (!success) {
                err_msg = "Decode/Encode failed";
            }

            size_t curr_done = 0;
            {
                std::lock_guard<std::mutex> lock(stats_mutex);
                if (success) {
                    stats.success_count++;
                } else {
                    stats.fail_count++;
                }
                completed_count++;
                curr_done = completed_count;
            }

            if (progress_cb) {
                progress_cb(curr_done, stats.total_files, file.filename().string(), success, err_msg);
            }
        });
    }

    pool.wait_all();

    auto end_time = std::chrono::steady_clock::now();
    stats.total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    return stats;
}
